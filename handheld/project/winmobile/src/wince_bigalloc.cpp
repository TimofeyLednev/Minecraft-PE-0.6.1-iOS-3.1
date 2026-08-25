/*
 * wince_bigalloc.cpp -- an allocator whose memory is not in the 32 MB slot.
 *
 * Windows CE 5.x gives every process a 32 MB window of virtual address space
 * and reserves *inside* it: the mapped image, every thread stack, and every
 * heap segment.  new/malloc can therefore only ever hand out addresses in
 * 0x00010000..0x02000000, and on this port that runs out during world
 * generation with 160 MB of physical RAM still free.  The log said so
 * precisely: 96 KB of slot per chunk, 256 chunks to generate, and only 14 MB
 * of slot left when generation started.
 *
 * The way out is the region above the process slots.  The CE 5.0 user-mode
 * layout is
 *
 *     0x00000000 - 0x01FFFFFF   slot 0: the running process        (32 MB)
 *     0x02000000 - 0x03FFFFFF   slot 1: XIP/ROM DLLs
 *     0x04000000 - 0x41FFFFFF   slots 2..32: one per process       (32 MB each)
 *     0x42000000 - 0x7FFFFFFF   memory-mapped views, shared        (~1 GB)
 *
 * and views created with CreateFileMapping + MapViewOfFile land in that last
 * region rather than in the caller's slot.  That is the whole trick: pass
 * INVALID_HANDLE_VALUE as the file handle to get a RAM-backed mapping with no
 * file behind it, map it, and treat it as memory.  It is ordinary read/write
 * memory once mapped -- no commit call, no special access rules, safe to hand
 * to memset and to the level generator.
 *
 * What is here is a slab allocator, not a heap:
 *
 *   - Requests are rounded up to a power-of-two size class between 4 KB and
 *     64 KB and served from 1 MB views carved into equal slots.  The two sizes
 *     this port actually asks for -- 32768 for a chunk's block ids and 16384
 *     for each of its three nibble layers -- are exact powers of two, so a
 *     view is divided with nothing left over and no per-allocation header.
 *     That header is worth avoiding for its own sake: CE's heap adds one and
 *     then rounds to a page, which turned each 32 KB request into 36 KB and
 *     each 16 KB request into 20 KB, wasting 16 KB per chunk -- 4 MB across a
 *     level, purely in padding.
 *   - Requests above 64 KB get a view of their own, rounded up to CE's 64 KB
 *     view granularity, and it is unmapped when freed.
 *   - Requests below 4 KB are not worth a slot and go to the heap.
 *
 * Every path degrades to the process heap rather than failing: if the mapping
 * APIs refuse, if the shared region is full, or if the region table fills up,
 * wce_bigAlloc returns new[] memory and wce_bigFree notices that the pointer
 * belongs to no view and calls delete[].  So the worst case is exactly the
 * behaviour this file was written to replace, with a line in the log saying
 * why -- never a crash and never a leak.  That is also what makes the two
 * halves independently safe to mix, which matters for DataLayer: its unused
 * two-argument constructor adopts a buffer somebody else allocated.
 */

#include "wince_compat.h"

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

/* --------------------------------------------------------------- layout -- */

/** Size of a slab view.  Divides evenly by every size class, so slabs are
    carved with nothing left over.  Small enough that the last slab of a class
    wastes at most 1 MB, large enough that a 20 MB level needs ~20 of them. */
static const unsigned long REGION_BYTES = 1024ul * 1024ul;

/** CE reserves mapped views on 64 KB boundaries; a dedicated view is rounded
    up to that so the shared region is not fragmented by odd sizes. */
static const unsigned long VIEW_GRANULARITY = 0x10000ul;

static const int NUM_CLASSES = 5;
static const unsigned long CLASS_BYTES[NUM_CLASSES] = {
	4096ul, 8192ul, 16384ul, 32768ul, 65536ul
};

/** Ceiling on mapped views.  64 slabs is 64 MB, well past the ~20 MB a full
    level needs, so hitting it means something is leaking rather than that the
    limit is too low -- which is why it is logged as a fallback and not raised
    silently. */
static const int MAX_REGIONS = 64;

struct BigRegion {
	unsigned char* base;
	unsigned char* end;
	HANDLE         hMap;
	int            cls;      /* size-class index, or -1 for a dedicated view */
	unsigned long  bytes;
};

static BigRegion g_regions[MAX_REGIONS];
static int       g_regionCount = 0;

/** Per-class free lists, threaded through the free slots themselves: the first
    four bytes of a slot that is not handed out hold the next free slot. */
static void* g_free[NUM_CLASSES];

static unsigned long g_mapped = 0;   /* bytes of view mapped */
static unsigned long g_inUse  = 0;   /* bytes handed out */
static unsigned long g_peak   = 0;   /* high-water mark of g_inUse */

static CRITICAL_SECTION g_lock;
static bool g_lockReady = false;

/** Set the first time a mapping call fails, so a device that does not support
    this never pays for retrying it once per allocation. */
static bool g_disabled = false;

static bool g_reported = false;

/* ----------------------------------------------------------------- init -- */

void wce_bigAllocInit()
{
	if (g_lockReady)
		return;

	memset(g_regions, 0, sizeof(g_regions));
	for (int i = 0; i < NUM_CLASSES; ++i)
		g_free[i] = NULL;

	InitializeCriticalSection(&g_lock);
	g_lockReady = true;   /* last: nothing may take the lock before it exists */
}

/* ---------------------------------------------------------------- slabs -- */

/** Smallest size class that holds @a bytes, or -1 if it does not belong here. */
static int classFor(unsigned long bytes)
{
	if (bytes < CLASS_BYTES[0])
		return -1;

	for (int i = 0; i < NUM_CLASSES; ++i) {
		if (bytes <= CLASS_BYTES[i])
			return i;
	}
	return -1;
}

/** Reports where the first successful mapping landed.
 *
 * This is the one fact that decides whether any of this works, and it cannot be
 * known without asking the device: the layout above is the documented CE 5.0
 * one, but the address a view actually gets is up to the ROM's kernel
 * configuration.  Below 0x02000000 means the view came out of our own slot and
 * nothing has been gained; above it means the level's storage no longer
 * competes with the engine for the 32 MB.
 */
static void reportFirstMapping(const void* base, unsigned long bytes)
{
	const unsigned long a = (unsigned long)base;

	wce_logPrintf("bigalloc: first view at 0x%08lx, %luK\n", a, bytes / 1024);

	if (a < 0x02000000ul) {
		wce_logPrintf("bigalloc: WARNING -- 0x%08lx is inside this process's"
		              " 32 MB slot, so mapped views buy no address space on this"
		              " ROM and world generation will run out of memory the same"
		              " way it did before\n", a);
	} else {
		wce_logPrintf("bigalloc: views land above the process slot, so chunk"
		              " storage no longer competes with the 32 MB\n");
	}
}

/** Maps a new view of @a bytes and records it. Returns its index, or -1.
    Caller holds the lock. */
static int mapRegion(unsigned long bytes, int cls)
{
	if (g_regionCount >= MAX_REGIONS) {
		g_disabled = true;
		wce_logPrintf("bigalloc: all %d view slots used (%luK mapped); falling"
		              " back to the process heap from here on\n",
		              MAX_REGIONS, g_mapped / 1024);
		return -1;
	}

	/* INVALID_HANDLE_VALUE asks for a mapping with no file behind it, backed by
	   the system's RAM.  lpAttributes must be NULL on CE, and the high half of
	   the size must be 0 -- a single view cannot exceed 4 GB, which is not a
	   constraint anybody here is near. */
	HANDLE hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
	                                 0, bytes, NULL);
	if (!hMap) {
		g_disabled = true;
		wce_logPrintf("bigalloc: CreateFileMapping(%luK) failed (%lu); falling"
		              " back to the process heap\n",
		              bytes / 1024, (unsigned long)GetLastError());
		return -1;
	}

	/* FILE_MAP_WRITE first because it is the narrow request and the one CE
	   documents; FILE_MAP_ALL_ACCESS carries standard-rights bits that some CE
	   builds reject, so it is only worth trying as a fallback. */
	void* base = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, bytes);
	if (!base)
		base = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, bytes);

	if (!base) {
		g_disabled = true;
		wce_logPrintf("bigalloc: MapViewOfFile(%luK) failed (%lu); falling back"
		              " to the process heap\n",
		              bytes / 1024, (unsigned long)GetLastError());
		CloseHandle(hMap);
		return -1;
	}

	const int r = g_regionCount++;
	g_regions[r].base  = (unsigned char*)base;
	g_regions[r].end   = (unsigned char*)base + bytes;
	g_regions[r].hMap  = hMap;
	g_regions[r].cls   = cls;
	g_regions[r].bytes = bytes;
	g_mapped += bytes;

	if (!g_reported) {
		g_reported = true;
		reportFirstMapping(base, bytes);
	}

	return r;
}

/* ------------------------------------------------------------ interface -- */

void* wce_bigAlloc(size_t bytes)
{
	if (bytes == 0)
		return NULL;

	const unsigned long need = (unsigned long)bytes;

	/* Before init, after a mapping failure, or too small to be worth a slot. */
	if (!g_lockReady || g_disabled)
		return new unsigned char[bytes];

	const int cls = classFor(need);
	if (cls < 0 && need < CLASS_BYTES[0])
		return new unsigned char[bytes];

	void* out = NULL;

	EnterCriticalSection(&g_lock);

	if (cls < 0) {
		/* Larger than the largest class: a view of its own. */
		const unsigned long round =
			(need + VIEW_GRANULARITY - 1) & ~(VIEW_GRANULARITY - 1);
		const int r = mapRegion(round, -1);
		if (r >= 0) {
			out = g_regions[r].base;
			g_inUse += round;
		}
	} else {
		if (!g_free[cls]) {
			const int r = mapRegion(REGION_BYTES, cls);
			if (r >= 0) {
				/* Thread every slot of the new slab onto the free list.  Built
				   back to front so the list comes out in address order, which
				   keeps a level's chunks roughly sequential in memory. */
				const unsigned long slot  = CLASS_BYTES[cls];
				const unsigned long slots = REGION_BYTES / slot;
				for (unsigned long i = slots; i > 0; --i) {
					void* p = g_regions[r].base + (i - 1) * slot;
					*(void**)p = g_free[cls];
					g_free[cls] = p;
				}
			}
		}

		if (g_free[cls]) {
			out = g_free[cls];
			g_free[cls] = *(void**)out;
			g_inUse += CLASS_BYTES[cls];
		}
	}

	if (g_inUse > g_peak)
		g_peak = g_inUse;

	LeaveCriticalSection(&g_lock);

	if (!out)
		return new unsigned char[bytes];

	return out;
}

void wce_bigFree(void* p)
{
	if (!p)
		return;

	if (!g_lockReady) {
		delete[] (unsigned char*)p;
		return;
	}

	/* The lock is taken even to decide whether the pointer is ours.  A range
	   check outside it would be faster and wrong: views are mapped by the level
	   generation thread and freed by whichever thread tears the level down, and
	   without the lock's barrier that thread can read a stale view table and
	   pass a mapped pointer to delete[].  Big blocks are freed on the order of a
	   thousand times per level, so the lock costs nothing worth measuring. */
	EnterCriticalSection(&g_lock);

	int found = -1;
	for (int i = 0; i < g_regionCount; ++i) {
		if ((unsigned char*)p >= g_regions[i].base &&
		    (unsigned char*)p <  g_regions[i].end) {
			found = i;
			break;
		}
	}

	if (found >= 0) {
		if (g_regions[found].cls < 0) {
			/* Dedicated view: give it back to the shared region.  The table
			   entry is filled from the end rather than left as a hole, which is
			   safe because nothing outside this file holds an index and slab
			   views are never removed. */
			const unsigned long bytes = g_regions[found].bytes;
			UnmapViewOfFile(g_regions[found].base);
			CloseHandle(g_regions[found].hMap);
			g_mapped -= bytes;
			g_inUse  -= bytes;

			g_regions[found] = g_regions[g_regionCount - 1];
			memset(&g_regions[g_regionCount - 1], 0, sizeof(BigRegion));
			--g_regionCount;
		} else {
			const int cls = g_regions[found].cls;
			*(void**)p = g_free[cls];
			g_free[cls] = p;
			g_inUse -= CLASS_BYTES[cls];
		}
	}

	LeaveCriticalSection(&g_lock);

	/* Not in any view, so it came from the heap: either from a fallback above,
	   or from a caller that allocated with new[] and handed ownership over. */
	if (found < 0)
		delete[] (unsigned char*)p;
}

void wce_bigStats(unsigned long* kbMapped, unsigned long* kbInUse,
                  unsigned long* kbPeak, int* views)
{
	/* Deliberately unlocked: this is called from wce_logMemory, which is called
	   from the out-of-memory handler, which can be running while another thread
	   holds the lock.  Four words read without synchronisation can disagree
	   with each other by one allocation; that is the right trade for a
	   diagnostic that must never be the thing that hangs. */
	if (kbMapped) *kbMapped = g_mapped / 1024;
	if (kbInUse)  *kbInUse  = g_inUse  / 1024;
	if (kbPeak)   *kbPeak   = g_peak   / 1024;
	if (views)    *views    = g_regionCount;
}
