/*
 * wince_vibrate.cpp -- the vibrator, with a stop that cannot be missed.
 *
 * The first implementation of this drove the motor with the notification-LED
 * driver in *blink* mode (`OffOnBlink = 2`) and one meta cycle, on the theory
 * that the driver would run a single on/off cycle and stop by itself, so that a
 * missed "switch it off" call could not leave the motor running.  That theory
 * was wrong, and wrong in the worst available direction.  A meta cycle is not a
 * count of cycles to run: it is a pattern that *repeats* -- MetaCycleOn cycles
 * blinking, MetaCycleOff cycles dark, forever.  There is no terminating count in
 * the interface at all.  Blink mode means "blink until told otherwise".
 *
 * And nobody ever told it otherwise, because notification-LED state belongs to
 * the driver, not to the process that set it.  Windows CE does not reset it when
 * a process exits, so the motor kept running after the game was closed and only
 * a reboot stopped it.  Breaking one block could cost the player a soft reset.
 *
 * So the motor is now switched on steadily and switched off explicitly, and
 * everything here exists to make the off unmissable:
 *
 *   - A dedicated thread owns the off.  It waits for the deadline and switches
 *     the motor off, independently of frame rate, of whatever the main thread is
 *     busy with, and of how long a level takes to generate.
 *   - wce_vibrateStop() is called from every path that can end a buzz: losing
 *     focus, WM_CLOSE, WM_NCDESTROY, WM_HIBERNATE, the end of main(), atexit,
 *     and the out-of-memory handler.  It is idempotent and takes no lock, so it
 *     is safe from all of them.
 *   - wce_vibrateInit() switches the motor off before it does anything else.
 *     If a buzz is ever left running again -- by a kill that runs no cleanup at
 *     all, which is the one case nothing in a process can cover -- then starting
 *     the game once is the cure, instead of rebooting the device.
 *   - If no stop can be guaranteed, there is no buzz.  Should the stopper thread
 *     fail to start, vibration turns itself off rather than running unprotected:
 *     no haptics is a missing feature, a latched motor is a broken phone.
 *
 * The one thing that is *not* used to start a buzz is aygshell's Vibrate(), even
 * though the OS times that one out itself, which would be strictly safer.  It is
 * a Windows Mobile Standard API, absent from CeGCC's import library, and there is
 * no way to know from here whether this ROM has it or whether it obeys the
 * profile's "vibrate" setting -- while NLED on LED 1 is proven to drive this
 * device's motor.  Trading a mechanism that works for one that might is not worth
 * it.  Vibrate() is bound anyway and used as a fallback if the thread is missing,
 * and VibrateStop() joins the off path when it exists, since extra off switches
 * cost nothing.
 */

#include "wince_compat.h"

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

#include <cstdlib>   /* atexit */
#include <cstring>

/* Notification LED interface.  nled.h is absent from this SDK, so the two
   pieces we need are declared by hand; the layout is fixed by the CE driver ABI.
   NLedSetDevice is a real coredll export (ordinal 759) and is imported normally
   rather than bound by name -- unlike ExtEscape or Vibrate, it is proven present
   on this ROM, because the buzz worked from the first device build. */
extern "C" BOOL NLedSetDevice(UINT nDeviceId, void* pInput);

namespace {

const UINT NLED_SETTINGS_INFO_ID = 2;

struct NledSettingsInfo {
	UINT LedNum;
	int  OffOnBlink;      /* 0 = off, 1 = on, 2 = blink (i.e. blink forever) */
	long TotalCycleTime;  /* microseconds */
	long OnTime;
	long OffTime;
	int  MetaCycleOn;
	int  MetaCycleOff;
};

/** The HD2's vibrator sits on notification LED 1 under WM6.5.  Logged at
    startup so that a device where it is some other index says so. */
const UINT VIBRATE_LED = 1;

/** Longest buzz this will ever ask for.  The game asks for 24 ms and nothing
    about breaking a block is improved by more; the clamp is here so that no
    caller can ask the motor to run for a minute, and so that the deadline can
    never be further away than this however it was computed. */
const long MAX_MS = 250;

/* aygshell, bound by name.  Signatures per the Windows Mobile SDK:
     BOOL Vibrate(int cvn, const VIBRATENODE* rgvn, BOOL fRepeat, DWORD dwTimeout)
     BOOL VibrateStop(void)
   cvn must be 0 and rgvn NULL for a plain buzz. */
typedef BOOL (WINAPI *VibrateFn)(int, const void*, BOOL, DWORD);
typedef BOOL (WINAPI *VibrateStopFn)(void);

VibrateFn     g_vibrate     = NULL;
VibrateStopFn g_vibrateStop = NULL;

CRITICAL_SECTION g_lock;
bool g_ready    = false;   /* the lock and the event exist */
bool g_disabled = false;   /* no stop can be guaranteed: do not start a buzz */

HANDLE g_wake   = NULL;    /* auto-reset: "a buzz was armed, or quit" */
HANDLE g_thread = NULL;

/** GetTickCount() value at which the motor must be off.  Compared by signed
    difference, so the 49-day tick wrap needs no special case. */
volatile LONG g_deadline = 0;
volatile LONG g_quit     = 0;

void motor(int offOnBlink)
{
	NledSettingsInfo info;
	memset(&info, 0, sizeof(info));
	info.LedNum     = VIBRATE_LED;
	info.OffOnBlink = offOnBlink;
	NLedSetDevice(NLED_SETTINGS_INFO_ID, &info);
}

void motorOff()
{
	motor(0);

	/* Also clear a system-level vibrate where the ROM has one.  Not required to
	   undo anything this file did -- it never starts one except in the no-thread
	   fallback -- but an off switch that costs nothing is worth pressing. */
	if (g_vibrateStop)
		g_vibrateStop();
}

/** Holds the motor on until the deadline, then switches it off.
 *
 * The deadline is re-read every pass, so a second block broken mid-buzz extends
 * the pulse instead of queueing another one -- which is also what stops a fast
 * miner from turning the motor into a rattle.
 */
DWORD WINAPI stopperThread(LPVOID)
{
	for (;;) {
		WaitForSingleObject(g_wake, INFINITE);
		if (g_quit)
			break;

		for (;;) {
			EnterCriticalSection(&g_lock);

			LONG rem = (LONG)((LONG)g_deadline - (LONG)GetTickCount());
			if (rem <= 0) {
				/* Switched off under the lock, which is the only reason the
				   lock is here: wce_vibrate arms the deadline and switches the
				   motor on while holding it, so a buzz that began after the
				   deadline was read cannot be cancelled by this pass. */
				motorOff();
				LeaveCriticalSection(&g_lock);
				break;
			}
			LeaveCriticalSection(&g_lock);

			/* Clamped for the same reason the request is: a deadline further
			   out than MAX_MS should not be possible, and if one ever is, this
			   turns an indefinite buzz into a slightly long one. */
			if (rem > MAX_MS)
				rem = MAX_MS;

			WaitForSingleObject(g_wake, (DWORD)rem);
			if (g_quit) {
				motorOff();
				return 0;
			}
		}
	}

	motorOff();
	return 0;
}

void stopAtExit()
{
	wce_vibrateStop();
}

}  /* namespace */

/* ----------------------------------------------------------------- init -- */

void wce_vibrateInit()
{
	if (g_ready)
		return;

	/* Before anything that can fail: switch the motor off.  This is what makes a
	   latched motor survivable -- see the header comment. */
	motor(0);

	/* aygshell is already in the import table (SHFullScreen), so it is loaded
	   and GetModuleHandle is enough; GetProcAddress rather than an import for
	   the usual CE reason -- an import name the ROM does not export kills the
	   process at load with no message, and neither of these is needed to run. */
	HMODULE aygshell = GetModuleHandle(L"aygshell.dll");
	if (aygshell) {
		g_vibrate     = (VibrateFn)GetProcAddress(aygshell, L"Vibrate");
		g_vibrateStop = (VibrateStopFn)GetProcAddress(aygshell, L"VibrateStop");
	}

	g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);   /* auto-reset, clear */
	if (g_wake) {
		InitializeCriticalSection(&g_lock);
		g_ready = true;   /* after the lock exists, before the thread starts */

		/* 16 KB rather than the image header's 1 MB: this thread's deepest frame
		   is an NLedSetDevice call, and a thread's stack reserve comes out of
		   the 32 MB slot whether it is ever touched or not. */
		DWORD tid = 0;
		g_thread = CreateThread(NULL, 16 * 1024, stopperThread, NULL, 0, &tid);
	}

	const DWORD err = GetLastError();

	if (g_thread) {
		wce_logPrintf("vibrate: NLED %u, stopper thread up%s\n",
		              VIBRATE_LED,
		              g_vibrateStop ? ", aygshell VibrateStop also available"
		                            : "");
	} else if (g_vibrate) {
		wce_logPrintf("vibrate: no stopper thread (%lu); using aygshell"
		              " Vibrate, which the OS times out itself\n",
		              (unsigned long)err);
	} else {
		g_disabled = true;
		wce_logPrintf("vibrate: no stopper thread (%lu) and no aygshell"
		              " Vibrate, so nothing here can guarantee the motor stops;"
		              " vibration is disabled\n", (unsigned long)err);
	}

	/* Covers an exit() from under us.  Not relied on: a TerminateProcess runs no
	   atexit handlers, which is why the OOM handler stops the motor itself. */
	atexit(stopAtExit);
}

/* ------------------------------------------------------------ interface -- */

void wce_vibrate(int milliSeconds)
{
	if (milliSeconds <= 0 || g_disabled)
		return;

	long ms = (long)milliSeconds;
	if (ms > MAX_MS)
		ms = MAX_MS;

	if (g_ready && g_thread) {
		EnterCriticalSection(&g_lock);
		g_deadline = (LONG)(GetTickCount() + (DWORD)ms);
		motor(1);
		LeaveCriticalSection(&g_lock);

		/* Outside the lock: the thread takes it immediately on waking, and
		   there is no reason to make it wait for us to let go. */
		SetEvent(g_wake);
		return;
	}

	/* No stopper thread, but the OS can time the pulse itself. */
	if (g_vibrate)
		g_vibrate(0, NULL, FALSE, (DWORD)ms);
}

void wce_vibrateStop()
{
	/* Deliberately takes no lock.  This is called from window messages and from
	   the out-of-memory handler, either of which can run while the stopper
	   thread holds it, and a stop that can block is a stop that can be missed.
	   The cost is that a buzz armed at this exact instant may be cut short --
	   which is the right way round for a function whose whole job is stopping.

	   The deadline store is a naked aligned 32-bit write, which is atomic on
	   ARM; volatile is what keeps the compiler from moving it. */
	g_deadline = (LONG)GetTickCount();   /* nothing left to wait for */
	motorOff();
}

void wce_vibrateShutdown()
{
	g_quit = 1;
	if (g_wake)
		SetEvent(g_wake);

	if (g_thread) {
		/* The thread is either in a bounded wait or already returning, so this
		   is a formality; it is bounded anyway, because hanging the shutdown of
		   a game over a vibrator would be absurd.  The motor is switched off
		   below regardless of what the thread managed to do. */
		WaitForSingleObject(g_thread, 1000);
		CloseHandle(g_thread);
		g_thread = NULL;
	}

	motorOff();   /* the last word, whether or not the thread had one */

	if (g_wake) {
		CloseHandle(g_wake);
		g_wake = NULL;
	}
	if (g_ready) {
		g_ready = false;   /* before the lock goes, so nothing can take it */
		DeleteCriticalSection(&g_lock);
	}
}
