#include "wince_compat.h"

/* Undo the call-site redirects so this file can reach the real CRT. */
#undef fopen
#undef _access
#undef _mkdir

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

/* For std::set_new_handler -- see wce_installOomHandler at the bottom. */
#include <new>

/* ---------------------------------------------------------------- paths -- */

std::wstring wce_widen(const char* s)
{
	if (!s || !*s)
		return std::wstring();

	int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
	if (n <= 0)
		return std::wstring();

	std::wstring out((size_t)(n - 1), L'\0');
	if (n > 1)
		MultiByteToWideChar(CP_ACP, 0, s, -1, &out[0], n);
	return out;
}

std::string wce_narrow(const wchar_t* s)
{
	if (!s || !*s)
		return std::string();

	int n = WideCharToMultiByte(CP_ACP, 0, s, -1, NULL, 0, NULL, NULL);
	if (n <= 0)
		return std::string();

	std::string out((size_t)(n - 1), '\0');
	if (n > 1)
		WideCharToMultiByte(CP_ACP, 0, s, -1, &out[0], n, NULL, NULL);
	return out;
}

const std::string& wce_exeDir()
{
	static std::string dir;
	static bool resolved = false;

	if (!resolved) {
		resolved = true;  /* set first: never retry, even on failure */

		WCHAR buf[MAX_PATH];
		DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
		if (n > 0 && n < MAX_PATH) {
			std::string full = wce_narrow(buf);
			std::string::size_type slash = full.find_last_of('\\');
			if (slash != std::string::npos)
				dir = full.substr(0, slash);
		}
		/* An empty dir means "device root", which is at least a valid path. */
	}
	return dir;
}

std::string wce_fixPath(const char* path)
{
	if (!path)
		return std::string();

	std::string p;
	p.reserve(MAX_PATH);

	/* CE has no CWD, so anchor anything relative to the .exe directory.
	   Skip a leading "./" or ".\" while doing so. */
	const char* s = path;
	bool absolute = (s[0] == '\\' || s[0] == '/');
	if (!absolute) {
		while (s[0] == '.' && (s[1] == '\\' || s[1] == '/'))
			s += 2;
		p = wce_exeDir();
		if (p.empty() || (p[p.size() - 1] != '\\'))
			p += '\\';
	}

	/* Flip separators and collapse runs of them.  A run is only collapsed
	   inside the path, never at the very start, so a UNC-style "\\host\share"
	   passed in deliberately still survives. */
	for (; *s; ++s) {
		char c = (*s == '/') ? '\\' : *s;
		if (c == '\\' && !p.empty() && p[p.size() - 1] == '\\')
			continue;
		p += c;
	}

	/* Resolve ".." lexically: CE's filesystem does not understand it, and the
	   game's asset paths ("../../data/x.png") are full of them. */
	std::string::size_type up;
	while ((up = p.find("\\..")) != std::string::npos) {
		if (up + 3 != p.size() && p[up + 3] != '\\')
			break;  /* a directory genuinely named "..foo" */
		std::string::size_type parent = p.rfind('\\', up ? up - 1 : 0);
		if (up == 0 || parent == std::string::npos)
			break;  /* would escape the root; leave it and let the API fail */
		p.erase(parent, (up + 3) - parent);
		if (p.empty())
			p = "\\";
	}

	return p;
}

/* ------------------------------------------------------------- file i/o -- */

FILE* wce_fopen(const char* path, const char* mode)
{
	std::string p = wce_fixPath(path);
	if (p.empty())
		return NULL;
	return fopen(p.c_str(), mode);
}

int wce_access(const char* path, int mode)
{
	std::string p = wce_fixPath(path);
	if (p.empty())
		return -1;
	return _access(p.c_str(), mode);
}

int wce_mkdir(const char* path)
{
	std::string p = wce_fixPath(path);
	if (p.empty())
		return -1;

	/* CE's _mkdir refuses to create intermediate directories, and the game
	   builds its save tree one level at a time except for the very first
	   call, which hands us a whole absolute path.  Walk it. */
	for (std::string::size_type i = 1; i <= p.size(); ++i) {
		if (i != p.size() && p[i] != '\\')
			continue;

		std::string part = p.substr(0, i);
		if (_access(part.c_str(), 0) == 0)
			continue;
		if (_mkdir(part.c_str()) != 0 && _access(part.c_str(), 0) != 0)
			return -1;
	}
	return 0;
}

static bool deleteTreeW(std::wstring dir)
{
	if (!dir.empty() && dir[dir.size() - 1] == L'\\')
		dir.erase(dir.size() - 1);

	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			/* CE's FindFirstFile does not report "." or "..", but check
			   anyway -- recursing into them would not terminate. */
			if (fd.cFileName[0] == L'.' &&
			    (fd.cFileName[1] == L'\0' ||
			     (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0')))
				continue;

			std::wstring child = dir + L"\\" + fd.cFileName;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				deleteTreeW(child);
			} else {
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
					SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
				DeleteFileW(child.c_str());
			}
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}

	return RemoveDirectoryW(dir.c_str()) != 0;
}

bool wce_deleteTree(const char* dir)
{
	std::string p = wce_fixPath(dir);
	if (p.empty() || p == "\\")
		return false;  /* never turn a bad path into "delete the device" */
	return deleteTreeW(wce_widen(p.c_str()));
}

/* ------------------------------------------------- CRT gaps filled in here -- */

/* remove() and rename() are *replacements*, not wrappers, and they are named
 * exactly what the CRT calls them rather than wce_something.  Two reasons:
 *
 *   * remove() does not exist on Windows CE at all.  Both <stdio.h> and <io.h>
 *     put its prototype behind "#ifndef __COREDLL__", and coredll exports no
 *     such symbol, so the six call sites in the level-storage code would fail
 *     to compile, never mind link.  Defining it here (plus the declaration in
 *     wince_compat.h) satisfies all six with no upstream edit.  DeleteFileW is
 *     the CE equivalent; _unlink, which does link, would not fix the paths.
 *
 *   * A macro redirect -- the trick used for fopen/_access/_mkdir -- is not
 *     available for these two.  "remove" also names Entity::remove(),
 *     Particle::remove(), IntHashMap::remove(), ItemInstance::remove(),
 *     std::remove() from <algorithm> and a bool member of TileEntity; a macro
 *     would rewrite all of them.
 *
 * rename() *does* exist, in libmingwex, and the definition here overrides that
 * archive member -- an object file always wins over a library.  It is
 * overridden because the libmingwex one passes the path straight to MoveFileW,
 * and every path the game hands it looks like "games/com.mojang/x/level.dat":
 * relative, which CE cannot resolve, with separators CE does not accept.
 * wce_fixPath is the whole point.
 */

extern "C" int remove(const char* path)
{
	std::string p = wce_fixPath(path);
	if (p.empty())
		return -1;

	std::wstring w = wce_widen(p.c_str());

	/* Read-only is worth clearing rather than reporting: the game deletes its
	   own save files, and a file it wrote is one it is entitled to remove. */
	DWORD attrs = GetFileAttributesW(w.c_str());
	if (attrs != 0xFFFFFFFF && (attrs & FILE_ATTRIBUTE_READONLY))
		SetFileAttributesW(w.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);

	return DeleteFileW(w.c_str()) ? 0 : -1;
}

extern "C" int rename(const char* from, const char* to)
{
	std::string f = wce_fixPath(from);
	std::string t = wce_fixPath(to);
	if (f.empty() || t.empty())
		return -1;

	std::wstring wf = wce_widen(f.c_str());
	std::wstring wt = wce_widen(t.c_str());

	if (MoveFileW(wf.c_str(), wt.c_str()))
		return 0;

	/* MoveFileW fails if the destination exists, and CE has no
	   MoveFileEx/MOVEFILE_REPLACE_EXISTING.  ExternalFileLevelStorage relies on
	   overwriting: it writes level.dat_tmp and renames it over level.dat.
	   Only retried when the destination is what blocked us, so a genuine
	   failure is still a failure. */
	if (GetLastError() == ERROR_ALREADY_EXISTS ||
	    GetLastError() == ERROR_FILE_EXISTS) {
		if (DeleteFileW(wt.c_str()) && MoveFileW(wf.c_str(), wt.c_str()))
			return 0;
	}
	return -1;
}

/* Backing store for the <errno.h> in project/winmobile/include.  Windows CE has
 * no errno of its own, so this is the only one there is.
 *
 * Process-wide rather than per-thread.  Every use in this codebase either sets
 * it and logs it a line later on the same thread (FolderMethods) or sets it and
 * never reads it (rdlmalloc's MALLOC_FAILURE_ACTION), so a cross-thread race
 * can only corrupt a diagnostic.  Making it per-thread would mean a TlsAlloc
 * slot and an initialisation order problem for no behavioural gain.
 */
extern "C" int* _errno(void)
{
	static int e = 0;
	return &e;
}

/* ------------------------------------------------------------- logging -- */

static FILE*             g_log = NULL;
static CRITICAL_SECTION  g_logLock;
static bool              g_logLockReady = false;
static bool              g_logAtLineStart = true;

/** Writes @a s to the logfile and the debugger without allocating anything.
    wce_widen() would allocate, which is exactly what must not happen on the
    out-of-memory path, so the wide copy goes on the stack and is truncated
    rather than grown.  ASCII only, which every format string here is. */
static void logRaw(const char* s, int len)
{
	if (g_log) {
		fwrite(s, 1, (size_t)len, g_log);
		/* Flushed per call: a crash during bring-up must not lose the last
		   lines, which are the interesting ones. */
		fflush(g_log);
	}

	WCHAR w[512];
	int n = len < 511 ? len : 511;
	for (int i = 0; i < n; ++i)
		w[i] = (WCHAR)(unsigned char)s[i];
	w[n] = L'\0';
	OutputDebugStringW(w);
}

void wce_logInit()
{
	if (g_log)
		return;

	if (!g_logLockReady) {
		InitializeCriticalSection(&g_logLock);
		g_logLockReady = true;
	}

	std::string path = wce_exeDir();
	if (!path.empty() && path[path.size() - 1] != '\\')
		path += '\\';
	path += "minecraft.log";

	g_log = fopen(path.c_str(), "w");
}

void wce_logShutdown()
{
	if (g_log) {
		fclose(g_log);
		g_log = NULL;
	}
	if (g_logLockReady) {
		g_logLockReady = false;
		DeleteCriticalSection(&g_logLock);
	}
}

void wce_logVPrintf(const char* fmt, va_list ap)
{
	char buf[1024];

	/* CE's vsnprintf does not always terminate on truncation. */
	int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	if (n < 0 || n > (int)sizeof(buf) - 1)
		n = (int)sizeof(buf) - 1;
	buf[n] = '\0';

	/* Serialised because two threads log: level generation runs on its own
	   thread (Minecraft::prepareLevel_tspawn) while the main thread keeps
	   ticking the progress screen, and interleaved half-lines in a crash log
	   are worse than no log.  The lock is skipped before wce_logInit(), when
	   there is only one thread anyway. */
	if (g_logLockReady)
		EnterCriticalSection(&g_logLock);

	/* Millisecond stamp at the start of each line, so the log says *when* it
	   stopped as well as where -- the difference between "hung" and "died", and
	   the only clock available after the fact.  Emitted per line rather than per
	   call because not every LOGI in the engine ends with a newline. */
	if (g_logAtLineStart) {
		char stamp[24];
		int sn = _snprintf(stamp, sizeof(stamp) - 1, "[%8lu] ",
		                   (unsigned long)GetTickCount());
		if (sn > 0)
			logRaw(stamp, sn);
	}
	logRaw(buf, n);
	g_logAtLineStart = (n > 0 && buf[n - 1] == '\n');

	if (g_logLockReady)
		LeaveCriticalSection(&g_logLock);
}

void wce_logPrintf(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	wce_logVPrintf(fmt, ap);
	va_end(ap);
}

/* -------------------------------------------------- crash diagnostics -- */

/** Largest contiguous run of free address space in this process's slot, in KB.
 *
 * Windows CE gives every process a 32 MB slot and reserves *inside* it: the
 * mapped image, every thread stack (2 MB reserve each by default, committed
 * lazily but reserved immediately), and every heap segment.  So a level that
 * fits in free physical memory can still fail to allocate because nothing
 * contiguous is left, and GlobalMemoryStatus -- which reports the whole
 * device -- will happily say there are tens of megabytes free while new
 * throws.  This walk is the number that actually predicts the failure.
 *
 * The slot is 0x00010000..0x02000000 as seen from inside the process; the first
 * 64 KB is permanently unmapped so that a null-pointer dereference faults.
 */
static unsigned long largestFreeBlockKB()
{
	const unsigned long slotBegin = 0x00010000ul;
	const unsigned long slotEnd   = 0x02000000ul;   /* 32 MB */

	MEMORY_BASIC_INFORMATION mbi;
	unsigned long largest = 0;
	unsigned long addr    = slotBegin;

	while (addr < slotEnd) {
		memset(&mbi, 0, sizeof(mbi));
		if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi))
			break;

		if (mbi.State == MEM_FREE && mbi.RegionSize > largest)
			largest = (unsigned long)mbi.RegionSize;

		if (mbi.RegionSize == 0)
			break;                       /* would not terminate otherwise */
		addr += (unsigned long)mbi.RegionSize;
	}

	return largest / 1024;
}

void wce_logMemory(const char* where)
{
	MEMORYSTATUS ms;
	memset(&ms, 0, sizeof(ms));
	ms.dwLength = sizeof(ms);
	GlobalMemoryStatus(&ms);

	unsigned long bigMapped = 0, bigInUse = 0, bigPeak = 0;
	int bigViews = 0;
	wce_bigStats(&bigMapped, &bigInUse, &bigPeak, &bigViews);

	/* One line, and deliberately not via wce_logPrintf's %s of a std::string:
	   this has to stay allocation-free to be usable from the new_handler.

	   "slot largest free" and "outside slot" are the two halves of the picture
	   and have to be read together: the level's chunk storage moved to the
	   second one on purpose (see wince_bigalloc.cpp), so the first one no longer
	   shrinks as generation proceeds, and if it does, something that was
	   supposed to be out of the slot is not. */
	char buf[320];
	int n = _snprintf(buf, sizeof(buf) - 1,
	                  "mem [%s]: load %lu%%, phys %luK/%luK free,"
	                  " virt %luK/%luK free, slot largest free %luK,"
	                  " outside slot %luK used of %luK in %d views (peak %luK)\n",
	                  where ? where : "",
	                  (unsigned long)ms.dwMemoryLoad,
	                  (unsigned long)(ms.dwAvailPhys / 1024),
	                  (unsigned long)(ms.dwTotalPhys / 1024),
	                  (unsigned long)(ms.dwAvailVirtual / 1024),
	                  (unsigned long)(ms.dwTotalVirtual / 1024),
	                  largestFreeBlockKB(),
	                  bigInUse, bigMapped, bigViews, bigPeak);
	if (n <= 0)
		return;
	buf[n] = '\0';

	if (g_logLockReady)
		EnterCriticalSection(&g_logLock);
	if (!g_logAtLineStart)
		logRaw("\n", 1);
	logRaw(buf, n);
	g_logAtLineStart = true;
	if (g_logLockReady)
		LeaveCriticalSection(&g_logLock);
}

/** Reports the allocation failure and kills the process.
 *
 * Never returns, which is the contract that matters: a new_handler that returns
 * tells operator new to retry, and since nothing here frees anything that would
 * spin forever.
 *
 * std::set_new_handler rather than a replacement operator new because
 * libstdc++.a is prebuilt and its operator new already calls the handler in a
 * loop -- there is nothing to hook that this does not hook, and no risk of the
 * library's internal allocations bypassing us.
 */
static void wce_newHandler()
{
	static long inHandler = 0;

	/* First, before the guard and before anything that touches the heap: the
	   process is about to be terminated, which runs no atexit handlers and no
	   destructors, and a vibrator left running would outlive it.  Idempotent and
	   lock-free, so it is safe here and safe to reach twice. */
	wce_vibrateStop();

	/* Reentrancy guard.  The first pass can still allocate indirectly (CE's
	   stdio buffers a flush, say) and come straight back in; on the second pass
	   there is nothing left to say, so die without touching the heap again. */
	if (InterlockedIncrement(&inHandler) != 1) {
		TerminateProcess(GetCurrentProcess(), 3);
		return;
	}

	static const char msg[] = "\n*** OUT OF MEMORY: operator new failed ***\n";
	logRaw(msg, (int)sizeof(msg) - 1);
	wce_logMemory("at failure");

	if (g_log) {
		fclose(g_log);
		g_log = NULL;
	}

	/* TerminateProcess rather than abort(): abort() on CE runs atexit handlers
	   and the C++ terminate path, both of which allocate. */
	TerminateProcess(GetCurrentProcess(), 3);
}

void wce_installOomHandler()
{
	std::set_new_handler(wce_newHandler);
}

