/*
 * wince_compat.h -- Windows CE / Windows Mobile compatibility layer.
 *
 * Force-included into every translation unit of the winmobile build
 * (see the -include flag in the Makefile), because it redirects the handful
 * of CRT calls the game makes at their call sites.  Nothing upstream needs
 * to change.
 *
 * Three things are broken about file I/O on Windows CE:
 *
 *   1. There is no current working directory.  Every relative path the game
 *      builds ("../../data/terrain.png", "./games/...") is meaningless.
 *   2. The filesystem does not accept '/' as a separator, and CE's fopen is
 *      a bare import thunk into coredll -- there is no CeGCC-side wrapper
 *      normalising anything.  The game builds paths with '/' throughout.
 *   3. There is no drive letter and no user profile; the whole device is one
 *      namespace rooted at '\'.
 *
 * wce_fopen / wce_access / wce_mkdir fix all three: they flip separators and
 * anchor anything relative to the directory the .exe was loaded from.
 *
 * There is also no stdout, so printf goes nowhere; wce_logPrintf writes to a
 * logfile next to the .exe and mirrors to the debugger.
 */
#ifndef WINCE_COMPAT_H__
#define WINCE_COMPAT_H__

#ifndef _WIN32_WCE
#error "wince_compat.h is for the Windows CE build only"
#endif

/* Pull these in *before* defining any macro below.  libstdc++'s <cstdio>
 * wrapper starts with "#undef fopen" and would silently eat our redirect if
 * some later header were the first to include it. */
#include <cstdio>
#include <cstdarg>
#include <string>
#include <io.h>

/* ---------------------------------------------------------------- paths -- */

/** Directory holding the running .exe, without a trailing separator.
    e.g. "\Program Files\Minecraft".  Computed once, then cached. */
const std::string& wce_exeDir();

/** Turns '/' into '\', collapses runs of separators, and prefixes
    wce_exeDir() if @a path is not already absolute (CE paths start at '\'). */
std::string wce_fixPath(const char* path);

/** Multibyte <-> UTF-16.  The whole CE API is Unicode-only; the game is
    entirely narrow-string, so every boundary crossing goes through these. */
std::wstring wce_widen(const char* s);
std::string  wce_narrow(const wchar_t* s);

/* ------------------------------------------------------------- file i/o -- */

FILE* wce_fopen(const char* path, const char* mode);
int   wce_access(const char* path, int mode);
int   wce_mkdir(const char* path);

/** Recursive directory delete.  Replaces the SHFileOperation() version in
    src/platform/file.h: shell32 does not exist on CE, and SHFileOperation
    does not link. */
bool wce_deleteTree(const char* dir);

/* ------------------------------------------------------------- logging -- */

/** Opens <exeDir>\minecraft.log (truncating).  Safe to call more than once;
    logging silently no-ops until it is called. */
void wce_logInit();
void wce_logShutdown();
void wce_logPrintf(const char* fmt, ...);
void wce_logVPrintf(const char* fmt, va_list ap);

/* -------------------------------------------------- crash diagnostics -- */

/** Writes one line describing where memory stands: the CE memory load, free
    physical and virtual bytes, and the largest contiguous free run in this
    process's 32 MB slot -- which is the number that actually runs out first,
    because thread stacks and the mapped image reserve address space in the slot
    whether or not they are committed.

    Allocates nothing, so it is safe from an out-of-memory handler.
    @a where is a short caller-supplied tag, e.g. "after levelgen". */
void wce_logMemory(const char* where);

/** Installs a std::new_handler that logs the failure and terminates.

    Worth doing because this build is compiled -fno-exceptions: when an
    allocation fails, libstdc++'s prebuilt operator new still throws
    std::bad_alloc, and unwinding into a frame compiled without exception
    support reaches std::terminate -> abort.  On a phone that is a process that
    vanishes with no message, no dialog and nothing in the log.  A new_handler
    runs *before* the throw, on the failing thread, with the call stack intact,
    which makes it the only place an out-of-memory death can be recorded.

    Call once, early -- before the first allocation worth diagnosing. */
void wce_installOomHandler();

/* --------------------------------------------- allocation outside the slot -- */

/* Memory from these does not come out of the process's 32 MB slot: it is a view
 * of a RAM-backed memory mapping, which Windows CE places in the shared region
 * above the process slots.  See wince_bigalloc.cpp for the layout and for why
 * the level's chunk storage has to live there.
 *
 * Declared here rather than in a header of their own because this file is
 * force-included into every translation unit, which is what lets the four call
 * sites in the shared game sources use them without an #include. */

/** Call once from main(), before any thread exists. */
void wce_bigAllocInit();

/** Returns @a bytes of writable, zeroed memory. Falls back to new[] for sizes
    below 4 KB and whenever the mapping APIs are unavailable or refuse, so it
    never returns NULL for a non-zero size and never needs checking for one. */
void* wce_bigAlloc(size_t bytes);

/** Frees a pointer from wce_bigAlloc. Also correct for a pointer from new[]:
    anything that belongs to no mapped view is passed to delete[], which is what
    makes it safe on buffers whose origin the caller does not know. */
void wce_bigFree(void* p);

/** Current mapping totals, in KB, and the number of views. Any argument may be
    NULL. Takes no lock, so it is safe from the out-of-memory handler. */
void wce_bigStats(unsigned long* kbMapped, unsigned long* kbInUse,
                  unsigned long* kbPeak, int* views);

/* ------------------------------------------------------------- vibrator -- */

/* Notification-LED state belongs to the driver, not to the process that set it:
 * Windows CE does not reset it when a process exits, so a buzz that is not
 * explicitly stopped keeps running after the game is gone and takes a reboot to
 * silence.  wince_vibrate.cpp exists to make the stop unmissable; the rules are
 * that wce_vibrateStop() is safe to call from anywhere at any time, and that it
 * should be called from anywhere a buzz could otherwise be abandoned. */

/** Call once from main(), after wce_logInit(). Switches the motor off first
    thing, so a buzz left running by a previous run does not need a reboot. */
void wce_vibrateInit();

/** Buzzes for @a milliSeconds (clamped), or does nothing if no stop can be
    guaranteed. Called on every block broken, so it is cheap and re-entrant:
    a second buzz during the first extends it rather than queueing. */
void wce_vibrate(int milliSeconds);

/** Switches the motor off. Idempotent, takes no lock and allocates nothing, so
    it is safe from a window message or from the out-of-memory handler. */
void wce_vibrateStop();

/** Stops the motor and joins the stopper thread. Call once, from the end of
    main(). */
void wce_vibrateShutdown();

/* ------------------------------------------------------ call-site fixups -- */

/* _access and _mkdir are what the WIN32 branch of FolderMethods.cpp already
 * calls, so redirecting them needs no source change either. */
#define fopen   wce_fopen
#define _access wce_access
#define _mkdir  wce_mkdir

/* remove() and rename() are handled the other way round -- not macros, but real
 * definitions in wince_compat.cpp under their own names.  A macro would be wrong
 * for both: "remove" also names Entity::remove(), ItemInstance::remove(),
 * std::remove() from <algorithm> and a bool member of TileEntity, and "rename"
 * is a method on LevelStorageSource.  See the block comment in wince_compat.cpp
 * for why each one needs us.
 *
 * The declaration below is needed because CeGCC's <stdio.h> and <io.h> both put
 * remove() behind "#ifndef __COREDLL__" -- CE genuinely has no remove(), so the
 * six call sites in the level-storage code would not even compile without this.
 * (libstdc++'s <cstdio> is patched to match: its "using ::remove;" is guarded by
 * __MINGW32CE__, so std::remove stays the <algorithm> template and nothing else.)
 * rename() *is* declared by both headers, so it needs no declaration here --
 * only the overriding definition. */
extern "C" int remove(const char* path);

#endif /* WINCE_COMPAT_H__ */
