#ifndef MAIN_WINMOBILE_H__
#define MAIN_WINMOBILE_H__

/*
 * main_winmobile.h -- entry point for Windows Mobile 6.x / Windows CE 5.2.
 *
 * Adapted from main_win32.h.  The differences are all forced by the platform,
 * and the non-obvious ones are:
 *
 *   * The entry point is main(), not WinMain().  That looks backwards for a CE
 *     GUI app, but mingw32ce's CRT *provides* WinMain (winmain_ce.o): it builds
 *     __argc/__argv from GetModuleFileNameW plus the wide command line and then
 *     tail-calls main().  Defining our own WinMain would replace the CRT's and
 *     throw that away for nothing.
 *
 *   * The display is rotated to landscape at startup and restored on exit.  The
 *     HD2's panel is 480x800 portrait; the game's UI is laid out for landscape.
 *     Rotating the display -- rather than rotating the projection matrix and
 *     transforming every touch coordinate -- keeps the whole engine, including
 *     the GUI hit-testing, unaware that anything unusual happened.
 *
 *   * The window is a borderless WS_POPUP covering the whole panel, with the
 *     shell's furniture hidden via SHFullScreen.  CE has no
 *     WS_OVERLAPPEDWINDOW/AdjustWindowRectEx worth using and no CS_OWNDC.
 *
 *   * While the app is not in the foreground the loop blocks in GetMessage
 *     instead of spinning in PeekMessage, so a backgrounded game (an incoming
 *     call, say) costs no CPU and no battery.  Timer clamps a resumed frame to
 *     one second / ten ticks, so the freeze cannot flood the tick loop.
 *
 *   * AppPlatform::_tick() is called from here.  Nothing in the engine calls it
 *     on any platform, and on CE it is what keeps the backlight on.
 */

#include "client/renderer/gles.h"
#include <EGL/egl.h>

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <windowsx.h>
#include <aygshell.h>

#include "platform/input/Keyboard.h"
#include "platform/input/Mouse.h"
#include "platform/input/Multitouch.h"
#include "platform/log.h"

#include "AppPlatform_winmobile.h"
#include "wince_compat.h"

/* ---------------------------------------------------------------- state -- */

static App*                    g_app      = 0;
static AppPlatform_winmobile*  g_platform = 0;
static volatile bool           g_running  = true;

/** False while another window is in front.  The loop stops rendering and
    ticking, and blocks for messages, until this goes true again. */
static bool g_active = true;

namespace {

const WCHAR WINDOW_CLASS[] = L"MinecraftPE";
const WCHAR WINDOW_TITLE[] = L"Minecraft";

/* Windows CE's display-orientation constants, spelled out rather than taken
 * from <wingdi.h>.
 *
 * That header defines them twice.  The genuine CE block is guarded with
 * "#else / * _WIN32_WCE * /", but the desktop block that follows it is guarded
 * only with "#if (WINVER >= 0x0501)" -- not "#ifndef _WIN32_WCE" -- so on a CE
 * build with WINVER >= 0x0501 the desktop values silently win:
 *
 *     CE:      DM_DISPLAYORIENTATION 0x00800000   DMDO_270 4
 *     desktop: DM_DISPLAYORIENTATION 0x00000080   DMDO_270 3   <- what we'd get
 *
 * DMDO_270 is 4 rather than 3 on CE because DM_DISPLAYQUERYORIENTATION returns
 * the *set* of supported orientations as a bitmask, which only works if each
 * one owns a bit.  Using the desktop values would ask the driver to change a
 * field it does not have and misread the capability mask.
 */
const DWORD CE_DM_DISPLAYORIENTATION      = 0x00800000;
const DWORD CE_DM_DISPLAYQUERYORIENTATION = 0x01000000;
const DWORD CE_DMDO_0                     = 0;
const DWORD CE_DMDO_90                    = 1;
const DWORD CE_DMDO_180                   = 2;
const DWORD CE_DMDO_270                   = 4;

/** Orientation the device was in when we started, restored on exit. */
DWORD g_entryOrientation = CE_DMDO_0;
bool  g_orientationChanged = false;

/** Framebuffer pixels per window pixel: 2 while the OS is DPI-virtualising us,
    1 when it is not.  Measured once by measureDisplay(); see the display-scaling
    block below for what it means and why it is not simply 1. */
int g_pixelScale = 1;

/** Size the game window is created at, and re-asserted at on every activation.
    g_pixelScale times the screen the OS admits to, so it deliberately overhangs
    that screen when virtualisation is in play. */
int g_winW = 0, g_winH = 0;

/** The GL surface's real dimensions, once there is one.  The engine is sized
    from these rather than from the window. */
int g_surfW = 0, g_surfH = 0;

/* ---------------------------------------------------------- orientation -- */

DWORD readOrientation()
{
	DEVMODE dm;
	memset(&dm, 0, sizeof(dm));
	dm.dmSize = sizeof(dm);

	if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm))
		return dm.dmDisplayOrientation;
	return CE_DMDO_0;
}

/** Bitmask of the orientations the display driver will accept. 0 if unknown. */
DWORD supportedOrientations()
{
	DEVMODE dm;
	memset(&dm, 0, sizeof(dm));
	dm.dmSize = sizeof(dm);
	dm.dmFields = CE_DM_DISPLAYQUERYORIENTATION;

	if (ChangeDisplaySettingsEx(NULL, &dm, NULL, CDS_TEST, NULL)
	    != DISP_CHANGE_SUCCESSFUL)
		return 0;
	return dm.dmDisplayOrientation;
}

bool setOrientation(DWORD orientation)
{
	DEVMODE dm;
	memset(&dm, 0, sizeof(dm));
	dm.dmSize = sizeof(dm);
	dm.dmFields = CE_DM_DISPLAYORIENTATION;
	dm.dmDisplayOrientation = orientation;

	LONG r = ChangeDisplaySettingsEx(NULL, &dm, NULL, CDS_RESET, NULL);
	if (r != DISP_CHANGE_SUCCESSFUL) {
		/* Some CE display drivers only take the change with no flags at all. */
		r = ChangeDisplaySettingsEx(NULL, &dm, NULL, 0, NULL);
	}
	return r == DISP_CHANGE_SUCCESSFUL;
}

/** Rotates the panel to landscape, remembering what to put back.
    Failing is not fatal: the game reads its size from the window either way,
    so an unrotatable device just runs portrait. */
void goLandscape()
{
	g_entryOrientation = readOrientation();

	if (GetSystemMetrics(SM_CXSCREEN) >= GetSystemMetrics(SM_CYSCREEN)) {
		LOGI("display is already landscape (orientation %lu)\n",
		     (unsigned long)g_entryOrientation);
		return;
	}

	const DWORD supported = supportedOrientations();

	/* 90 first: it puts the hardware buttons on the right, which is where a
	   player holding the phone two-handed expects them. */
	DWORD want = CE_DMDO_0;
	if (supported == 0 || (supported & CE_DMDO_90))
		want = CE_DMDO_90;
	else if (supported & CE_DMDO_270)
		want = CE_DMDO_270;

	if (want == CE_DMDO_0) {
		LOGW("display driver reports no landscape orientation (mask %lu);"
		     " running portrait\n", (unsigned long)supported);
		return;
	}

	if (setOrientation(want)) {
		g_orientationChanged = (want != g_entryOrientation);
		LOGI("rotated display to orientation %lu (%dx%d)\n",
		     (unsigned long)want,
		     GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	} else {
		LOGW("could not rotate display to orientation %lu; running portrait\n",
		     (unsigned long)want);
	}
}

void restoreOrientation()
{
	if (g_orientationChanged) {
		setOrientation(g_entryOrientation);
		g_orientationChanged = false;
	}
}

/* --------------------------------------------------------------- window -- */

/** Hides the taskbar, SIP button and start icon, then claims the space they
    were occupying.  SHFullScreen only hides the shell's furniture; it does not
    resize us.  Must be re-applied whenever we regain activation, because the
    shell puts the taskbar back as soon as another window comes forward. */
void goFullScreen(HWND hwnd)
{
	SHFullScreen(hwnd,
	             SHFS_HIDETASKBAR | SHFS_HIDESIPBUTTON | SHFS_HIDESTARTICON);

	/* g_winW/H rather than SM_CXSCREEN, because under DPI virtualisation the
	   window is meant to be bigger than the screen the OS reports -- resizing it
	   down to that screen here would undo measureDisplay() on the first
	   activation, which is a bug that only shows up after a phone call. */
	MoveWindow(hwnd, 0, 0,
	           (g_winW > 0) ? g_winW : GetSystemMetrics(SM_CXSCREEN),
	           (g_winH > 0) ? g_winH : GetSystemMetrics(SM_CYSCREEN),
	           TRUE);
}

/* ------------------------------------------------------ display scaling -- */

/* What the OS says the screen is, against what the panel actually is.
 *
 * Windows Mobile gives an executable it does not consider high-resolution aware
 * a virtual QVGA coordinate space: on the HD2, SM_CXSCREEN/SM_CYSCREEN come back
 * 400x240 in landscape for an 800x480 panel and LOGPIXELSX reports 96 instead of
 * 192.  GDI scales that space up on the way to the framebuffer.  EGL and GL take
 * no part in it -- the driver reads the window's client rect as a count of
 * *framebuffer* pixels, so a window covering the whole reported screen yields a
 * 400x240 surface, which then lands unscaled in the top-left quarter of the
 * panel.  The touch driver, meanwhile, reports positions in the virtual space,
 * already halved.  Both halves of that were visible in the first device photo:
 * the game drawn at quarter size in the corner, and taps landing twice as far
 * from the origin as the thing they were aimed at.
 *
 * src/hires.rc exists to opt out of the whole arrangement and is structurally
 * correct -- type 24, name 1, a single WORD 1, listed in the PE resource
 * directory, verified by hand in the shipped image -- but this device's ROM
 * ignores it: the log still says 400x240 at 96 dpi.  So the resource stays (it
 * costs two bytes and it is the documented mechanism) and the port stops
 * depending on it.  Instead it measures the panel and adapts:
 *
 *   * the window is created g_pixelScale times bigger than the screen the OS
 *     admits to, so that the surface the driver derives from its client rect
 *     covers the panel rather than a corner of it;
 *   * pointer coordinates are multiplied by g_pixelScale on the way in, because
 *     surface pixels are physical pixels while touch positions are virtual ones.
 *
 * The second one is what makes the first one safe to get wrong: it holds whatever
 * size the window ended up, since it depends only on the virtualisation factor.
 * Both reduce to the identity when that factor is 1 -- a device that honours the
 * resource, or a genuine QVGA device -- and that path is the one the code takes
 * when it cannot measure anything at all.
 */

inline int toSurfaceX(int x)
{
	return (g_pixelScale == 1) ? x : x * g_pixelScale;
}
inline int toSurfaceY(int y)
{
	return (g_pixelScale == 1) ? y : y * g_pixelScale;
}

/* The raw-framebuffer escape, spelled out because CeGCC's headers have
   ExtEscape but not this: <windev.h>/<gx.h> are a much larger dependency than a
   24-byte struct and one constant.  It is the call every GAPI-era game used to
   find the framebuffer, so a display driver has to answer it in real panel
   pixels to be of any use -- which is exactly the number DPI virtualisation
   hides.  Only the two pixel counts are read here; pFramePointer is deliberately
   never dereferenced, since GL owns the framebuffer. */
const int WCE_GETRAWFRAMEBUFFER = 0x00020001;

struct WceRawFrameBufferInfo {
	WORD  wFormat;
	WORD  wBPP;
	void* pFramePointer;
	int   cxStride;
	int   cyStride;
	int   cxPixels;
	int   cyPixels;
};

/** Physical panel size in real pixels, or false if the driver will not say.
 *
 *  ExtEscape is bound with GetProcAddress rather than imported: an import name
 *  the ROM's coredll does not export kills the process at load time with no
 *  message at all, and that is a bad trade for a call whose absence is
 *  survivable.  (It is a stock CE 5.x GDI export, so this should never fire --
 *  but "should" is how the last import list got written.)
 */
bool queryPanelSize(int* w, int* h)
{
	typedef int (WINAPI *ExtEscapeFn)(HDC, int, int, LPCSTR, int, LPSTR);
	static ExtEscapeFn extEscape = NULL;
	static bool        resolved  = false;

	if (!resolved) {
		resolved = true;
		HMODULE core = GetModuleHandle(L"coredll.dll");
		if (core)
			extEscape = (ExtEscapeFn)GetProcAddress(core, L"ExtEscape");
		if (!extEscape)
			LOGW("no ExtEscape in coredll; cannot measure the panel\n");
	}
	if (!extEscape)
		return false;

	HDC hdc = GetDC(NULL);
	if (!hdc)
		return false;

	WceRawFrameBufferInfo info;
	memset(&info, 0, sizeof(info));

	const int rc = extEscape(hdc, WCE_GETRAWFRAMEBUFFER, 0, NULL,
	                         sizeof(info), (LPSTR)&info);
	ReleaseDC(NULL, hdc);

	if (rc <= 0 || info.cxPixels <= 0 || info.cyPixels <= 0) {
		LOGW("GETRAWFRAMEBUFFER refused (returned %d); assuming the OS is"
		     " telling the truth about the screen\n", rc);
		return false;
	}

	LOGI("raw framebuffer %dx%d, %d bpp, stride %d/%d\n",
	     info.cxPixels, info.cyPixels, (int)info.wBPP,
	     info.cxStride, info.cyStride);

	*w = info.cxPixels;
	*h = info.cyPixels;
	return true;
}

/** Sets g_pixelScale, g_winW and g_winH.  Must run after the display has been
    rotated, so that the panel and the reported screen are compared in the same
    orientation. */
void measureDisplay()
{
	const int logW = GetSystemMetrics(SM_CXSCREEN);
	const int logH = GetSystemMetrics(SM_CYSCREEN);

	/* Defaults are the no-virtualisation answer, so every early return below
	   leaves the port behaving the way it would on a device that honours
	   hires.rc. */
	g_pixelScale = 1;
	g_winW       = logW;
	g_winH       = logH;

	int panW = 0, panH = 0;
	if (!queryPanelSize(&panW, &panH))
		return;

	/* Long edge against long edge: whether the escape follows the display
	   rotation is the driver's business, and this does not need to know. */
	const int logLong  = (logW > logH) ? logW : logH;
	const int logShort = (logW > logH) ? logH : logW;
	const int panLong  = (panW > panH) ? panW : panH;
	const int panShort = (panW > panH) ? panH : panW;

	if (logLong <= 0 || logShort <= 0)
		return;

	if (panLong == logLong && panShort == logShort) {
		LOGI("panel %dx%d matches the reported screen: no DPI virtualisation\n",
		     panW, panH);
		return;
	}

	const int sx = panLong / logLong;
	const int sy = panShort / logShort;

	if (sx != sy || sx < 1 || sx > 4
	    || sx * logLong != panLong || sy * logShort != panShort) {
		LOGW("a %dx%d panel against a reported screen of %dx%d is not a whole"
		     " ratio; leaving the window size and input alone\n",
		     panW, panH, logW, logH);
		return;
	}
	if (sx == 1)
		return;

	g_pixelScale = sx;
	g_winW       = logW * sx;
	g_winH       = logH * sx;

	LOGW("DPI virtualisation is active: the OS reports %dx%d for a %dx%d panel,"
	     " so HI_RES_AWARE did not take effect.  Creating a %dx%d window for a"
	     " full-panel GL surface and scaling pointer input by %d.\n",
	     logW, logH, panW, panH, g_winW, g_winH, sx);
}

/** Everything about the display that has been worth knowing at least once.
    Cheap, runs twice, and the first thing to read in a bug report. */
void logDisplayMetrics(const char* when, HWND hwnd)
{
	HDC hdc = GetDC(NULL);
	int dpiX = 0, dpiY = 0, bpp = 0;
	if (hdc) {
		dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
		dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
		bpp  = GetDeviceCaps(hdc, BITSPIXEL);
		ReleaseDC(NULL, hdc);
	}

	/* 96 dpi on a 480x800 panel is the tell-tale of DPI virtualisation: the OS
	   is describing the QVGA space it invented, not the screen. */
	LOGI("display %s: SM_CXSCREEN=%d SM_CYSCREEN=%d, %d dpi x %d dpi, %d bpp\n",
	     when,
	     GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
	     dpiX, dpiY, bpp);

	if (hwnd) {
		RECT r;
		GetClientRect(hwnd, &r);
		LOGI("display %s: client rect %dx%d\n", when,
		     (int)(r.right - r.left), (int)(r.bottom - r.top));
	}
}

}  /* namespace */

LRESULT WINAPI windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {

	case WM_KEYDOWN:
		/* The HD2's Back button, and the SIP's Esc key, both arrive as
		   VK_ESCAPE -- aygshell.h defines VK_TBACK as VK_ESCAPE, so there is
		   only one code to handle.  handleBack() returning false means the app
		   has nothing left to back out of, which is the quit gesture. */
		if (wParam == VK_ESCAPE) {
			if (g_app && !g_app->handleBack(true))
				g_running = false;
			return 0;
		}
		Keyboard::feed((unsigned char)wParam, 1);
		return 0;

	case WM_KEYUP:
		if (wParam == VK_ESCAPE) {
			if (g_app) g_app->handleBack(false);
			return 0;
		}
		Keyboard::feed((unsigned char)wParam, 0);
		return 0;

	case WM_CHAR:
		/* wParam is a wide character here (CE is Unicode-only) and feedText
		   takes a narrow one, so pass through only printable ASCII.  Anything
		   else would be truncated into a different character. */
		if (wParam >= 32 && wParam < 127)
			Keyboard::feedText((char)wParam);
		return 0;

	/* Pointer coordinates arrive in window space and the engine works in
	   surface space; toSurfaceX/Y is the identity unless DPI virtualisation
	   defeated src/hires.rc.  Converted once per message rather than once per
	   feed() so the two consumers cannot disagree. */
	case WM_LBUTTONDOWN: {
		const int mx = toSurfaceX(GET_X_LPARAM(lParam));
		const int my = toSurfaceY(GET_Y_LPARAM(lParam));
		Mouse::feed(MouseAction::ACTION_LEFT, MouseAction::DATA_DOWN, mx, my);
		Multitouch::feed(1, 1, mx, my, 0);
		return 0;
	}

	case WM_LBUTTONUP: {
		const int mx = toSurfaceX(GET_X_LPARAM(lParam));
		const int my = toSurfaceY(GET_Y_LPARAM(lParam));
		Mouse::feed(MouseAction::ACTION_LEFT, MouseAction::DATA_UP, mx, my);
		Multitouch::feed(1, 0, mx, my, 0);
		return 0;
	}

	case WM_MOUSEMOVE: {
		/* Only ever sent while the finger is down: a touch screen has no
		   hover, so there is no need to filter out button-less movement. */
		const int mx = toSurfaceX(GET_X_LPARAM(lParam));
		const int my = toSurfaceY(GET_Y_LPARAM(lParam));
		Mouse::feed(MouseAction::ACTION_MOVE, 0, mx, my);
		Multitouch::feed(0, 0, mx, my, 0);
		return 0;
	}

	case WM_ACTIVATE:
		g_active = (LOWORD(wParam) != WA_INACTIVE);
		if (g_active) {
			goFullScreen(hWnd);
		} else {
			/* Drop every held key and pointer.  Otherwise a player who
			   backgrounds the game mid-stride comes back still walking, with
			   no key-up ever arriving to stop them. */
			Multitouch::reset();
			Mouse::reset();
			Keyboard::reset();

			/* Same argument, one step worse: a buzz abandoned here would run
			   on in the background, because the motor belongs to the driver
			   and nothing in CE switches it off for us.  Also the moment the
			   render loop stops being ticked, so nothing else would. */
			wce_vibrateStop();
		}
		return 0;

	case WM_SETTINGCHANGE:
		/* Sent when the SIP is shown or hidden, among other things; the shell
		   may have redrawn its furniture over us. */
		if (g_active)
			goFullScreen(hWnd);
		return 0;

	case WM_HIBERNATE:
		/* CE asks every app to shed memory when the system is running low.
		   Nothing is freed here on purpose: the caches worth dropping are all
		   GL objects, and releasing them from an arbitrary message -- possibly
		   between a bind and a draw -- is a worse failure than being killed.
		   The chunk caches are already cut down by GFX_SMALLER_CHUNKS.  Logged
		   because it is the one warning we get before an out-of-memory kill. */
		LOGW("WM_HIBERNATE: system is low on memory\n");
		wce_vibrateStop();   /* the kill it warns about would run no cleanup */
		return 0;

	case WM_CLOSE:
		/* Handled rather than passed on, so the window outlives the teardown
		   below: deleting the app still makes GL calls into this surface. */
		g_running = false;
		wce_vibrateStop();
		return 0;

	case WM_SIZE: {
		/* Not scaled, unlike pointer coordinates: the client rect and the GL
		   surface are the same number of pixels even under DPI virtualisation,
		   because the driver reads that rect as a count of framebuffer pixels --
		   which is the whole reason the oversized window in measureDisplay()
		   works.  Arrives before g_platform exists during CreateWindow, hence the
		   pointer checks; the authoritative first size is set from
		   eglQuerySurface in main().

		   Honoured only while it agrees with the surface actually being rendered
		   into.  If the shell ever clamped the window back to the screen it
		   thinks we have, telling the engine it had shrunk while the surface
		   stayed the same size would rescale the whole UI and misplace every
		   tap -- worse than ignoring the message. */
		const int sw = (int)LOWORD(lParam);
		const int sh = (int)HIWORD(lParam);
		if (g_surfW > 0 && (sw != g_surfW || sh != g_surfH)) {
			LOGW("ignoring WM_SIZE %dx%d: the GL surface is %dx%d\n",
			     sw, sh, g_surfW, g_surfH);
			return 0;
		}
		if (g_platform)
			g_platform->setScreenSize(sw, sh);
		if (g_app)
			g_app->setSize(sw, sh);
		return 0;
	}

	case WM_NCDESTROY:
		g_running = false;
		wce_vibrateStop();
		break;

	default:
		break;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

namespace {

bool createGameWindow(HINSTANCE hInstance, HWND* out)
{
	WNDCLASS wc;
	memset(&wc, 0, sizeof(wc));

	/* No CS_OWNDC: CE does not support it, and RegisterClass fails outright on
	   styles it does not know. */
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc   = (WNDPROC)windowProc;
	wc.hInstance     = hInstance;
	wc.hIcon         = NULL;   /* IDI_WINLOGO does not exist on CE */
	wc.hCursor       = NULL;   /* nor does a pointer to draw */
	wc.hbrBackground = NULL;   /* GL paints every pixel every frame */
	wc.lpszMenuName  = NULL;
	wc.lpszClassName = WINDOW_CLASS;

	/* Once per process: this function can be called a second time if EGL refuses
	   the first window, and RegisterClass fails on a class that already
	   exists. */
	static bool classRegistered = false;
	if (!classRegistered) {
		if (!RegisterClass(&wc)) {
			LOGE("RegisterClass failed (%lu)\n", (unsigned long)GetLastError());
			return false;
		}
		classRegistered = true;
	}

	/* Measured, not asked for: g_winW/H is the panel, which is not necessarily
	   the screen the OS describes.  See the display-scaling block. */
	const int cx = (g_winW > 0) ? g_winW : GetSystemMetrics(SM_CXSCREEN);
	const int cy = (g_winH > 0) ? g_winH : GetSystemMetrics(SM_CYSCREEN);

	LOGI("creating a %dx%d window (OS screen %dx%d)\n", cx, cy,
	     GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

	HWND hwnd = CreateWindowEx(0, WINDOW_CLASS, WINDOW_TITLE,
	                           WS_VISIBLE | WS_POPUP,
	                           0, 0, cx, cy,
	                           NULL, NULL, hInstance, NULL);
	if (!hwnd) {
		LOGE("CreateWindowEx failed (%lu)\n", (unsigned long)GetLastError());
		return false;
	}

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	SetForegroundWindow(hwnd);
	SetFocus(hwnd);
	goFullScreen(hwnd);

	*out = hwnd;
	return true;
}

/** Brings up EGL on @a hwnd.  Returns false with everything released on
    failure, having logged which step failed and the EGL error behind it.
    On success *outW / *outH are the surface's real pixel dimensions, which are
    what the engine must be sized to -- see the display-scaling block above. */
bool initEGL(HWND hwnd, AppContext* ctx, HDC* outDC, int* outW, int* outH)
{
	/* RGB565, no alpha, 16-bit depth, no stencil.
	   The Adreno 200's native framebuffer format is 565; asking for 8888 costs
	   a third more fill bandwidth for a texture set that is 565 anyway, and
	   twice the framebuffer memory out of a 32 MB budget. */
	static const EGLint configAttribs[] = {
		EGL_RED_SIZE,        5,
		EGL_GREEN_SIZE,      6,
		EGL_BLUE_SIZE,       5,
		EGL_ALPHA_SIZE,      0,
		EGL_DEPTH_SIZE,      16,
		EGL_STENCIL_SIZE,    0,
		EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
		EGL_NONE
	};
	/* Fallback: let the driver pick colour depth, insist only on a window we
	   can depth-test in. */
	static const EGLint minimalAttribs[] = {
		EGL_DEPTH_SIZE,   16,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE
	};

	*outDC = NULL;
	*outW  = 0;
	*outH  = 0;

	/* Two spellings of "the display".  CE's EGLNativeDisplayType is an HDC and
	   the Imageon/Adreno driver documents eglGetDisplay(GetDC(hwnd)), but some
	   CE EGL builds only accept EGL_DEFAULT_DISPLAY -- so try the documented
	   one and fall back, logging which worked. */
	HDC hdc = GetDC(hwnd);
	EGLDisplay display = EGL_NO_DISPLAY;

	if (hdc) {
		display = eglGetDisplay((EGLNativeDisplayType)hdc);
		if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
			display = EGL_NO_DISPLAY;
			ReleaseDC(hwnd, hdc);
			hdc = NULL;
		} else {
			LOGI("EGL display from window HDC\n");
		}
	}
	if (display == EGL_NO_DISPLAY) {
		display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
		if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
			LOGE("eglInitialize failed for both HDC and default display"
			     " (error 0x%x)\n", (unsigned)eglGetError());
			return false;
		}
		LOGI("EGL display from EGL_DEFAULT_DISPLAY\n");
	}

	LOGI("EGL %s / %s\n",
	     eglQueryString(display, EGL_VERSION),
	     eglQueryString(display, EGL_VENDOR));

	EGLConfig config = 0;
	EGLint numConfigs = 0;

	if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs)
	    || numConfigs < 1) {
		LOGW("no RGB565 config (error 0x%x); retrying without a colour"
		     " preference\n", (unsigned)eglGetError());
		if (!eglChooseConfig(display, minimalAttribs, &config, 1, &numConfigs)
		    || numConfigs < 1) {
			LOGE("eglChooseConfig found nothing usable (error 0x%x)\n",
			     (unsigned)eglGetError());
			eglTerminate(display);
			if (hdc) ReleaseDC(hwnd, hdc);
			return false;
		}
	}

	EGLSurface surface = eglCreateWindowSurface(display, config,
	                                            (EGLNativeWindowType)hwnd, NULL);
	if (surface == EGL_NO_SURFACE) {
		LOGE("eglCreateWindowSurface failed (error 0x%x)\n",
		     (unsigned)eglGetError());
		eglTerminate(display);
		if (hdc) ReleaseDC(hwnd, hdc);
		return false;
	}

	/* NULL attributes: libGLES_CM is an ES 1.1 Common profile library, so the
	   default client version is already the one we want.  Passing
	   EGL_CONTEXT_CLIENT_VERSION is an ES 2 idiom that some ES 1 drivers
	   reject. */
	EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
	if (context == EGL_NO_CONTEXT) {
		LOGE("eglCreateContext failed (error 0x%x)\n", (unsigned)eglGetError());
		eglDestroySurface(display, surface);
		eglTerminate(display);
		if (hdc) ReleaseDC(hwnd, hdc);
		return false;
	}

	if (!eglMakeCurrent(display, surface, surface, context)) {
		LOGE("eglMakeCurrent failed (error 0x%x)\n", (unsigned)eglGetError());
		eglDestroyContext(display, context);
		eglDestroySurface(display, surface);
		eglTerminate(display);
		if (hdc) ReleaseDC(hwnd, hdc);
		return false;
	}

	EGLint surfW = 0, surfH = 0;
	eglQuerySurface(display, surface, EGL_WIDTH, &surfW);
	eglQuerySurface(display, surface, EGL_HEIGHT, &surfH);
	LOGI("EGL surface %dx%d\n", (int)surfW, (int)surfH);

	ctx->display = display;
	ctx->surface = surface;
	ctx->context = context;
	*outDC = hdc;
	*outW  = (int)surfW;
	*outH  = (int)surfH;
	return true;
}

void shutdownEGL(HWND hwnd, AppContext* ctx, HDC hdc)
{
	eglMakeCurrent(ctx->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(ctx->display, ctx->context);
	eglDestroySurface(ctx->display, ctx->surface);
	eglTerminate(ctx->display);
	if (hdc)
		ReleaseDC(hwnd, hdc);
}

}  /* namespace */

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	wce_logInit();
	LOGI("Minecraft PE 0.6.1 -- Windows Mobile build\n");

	/* Before the first interesting allocation: this build is -fno-exceptions, so
	   without a new_handler an out-of-memory death is a process that simply
	   disappears.  A resident 16x16 level is ~20 MB of chunk storage, which is
	   why this is a real possibility rather than a theoretical one -- and why
	   that storage is now mapped outside the 32 MB slot instead. */
	wce_installOomHandler();

	/* Here rather than lazily: it creates the critical section the allocator
	   locks, and main() is the last point at which there is provably only one
	   thread to race over it. */
	wce_bigAllocInit();

	/* Early for two reasons: the same one-thread argument, and because the first
	   thing it does is switch the vibrator off -- so if a previous run ever
	   leaves the motor latched, starting the game clears it. */
	wce_vibrateInit();

	wce_logMemory("startup");

	HINSTANCE hInstance = GetModuleHandle(NULL);

	logDisplayMetrics("at startup", NULL);
	goLandscape();

	/* After the rotation, because it compares the panel with the screen the OS
	   reports and those have to be in the same orientation.  Decides how big the
	   window is and how pointer input is scaled. */
	measureDisplay();

	HWND hwnd = NULL;
	if (!createGameWindow(hInstance, &hwnd)) {
		restoreOrientation();
		wce_vibrateShutdown();
		wce_logShutdown();
		return 1;
	}

	logDisplayMetrics("after rotate", hwnd);

	AppContext appContext;
	memset(&appContext, 0, sizeof(appContext));
	/* main_win32.h leaves this uninitialised, which makes App::swapBuffers
	   depend on stack garbage.  Android and the Pi both set it explicitly. */
	appContext.doRender = true;

	HDC hdc = NULL;
	int surfW = 0, surfH = 0;
	bool eglUp = initEGL(hwnd, &appContext, &hdc, &surfW, &surfH);

	if (!eglUp && g_pixelScale > 1) {
		/* The only reason the window overhangs the screen is to get a full-panel
		   surface out of the driver; a driver that will not allocate one is not
		   worth dying over.  At the OS's own size the game draws into part of the
		   panel, as it did before any of this, but it still runs and taps still
		   land in the right place -- pointer scaling depends on the
		   virtualisation factor, not on the window. */
		LOGW("EGL would not come up on a %dx%d window; retrying at the %dx%d the"
		     " OS reports\n", g_winW, g_winH,
		     GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

		DestroyWindow(hwnd);
		hwnd   = NULL;
		g_winW = GetSystemMetrics(SM_CXSCREEN);
		g_winH = GetSystemMetrics(SM_CYSCREEN);

		if (createGameWindow(hInstance, &hwnd))
			eglUp = initEGL(hwnd, &appContext, &hdc, &surfW, &surfH);
	}

	if (!eglUp) {
		if (hwnd)
			DestroyWindow(hwnd);
		restoreOrientation();
		wce_vibrateShutdown();
		wce_logShutdown();
		return 1;
	}

	glInit();
	LOGI("GL_RENDERER: %s\n", (const char*)glGetString(GL_RENDERER));
	LOGI("GL_VERSION:  %s\n", (const char*)glGetString(GL_VERSION));

	/* The platform is told the real client size before the app is built: the
	   engine queries the screen dimensions while constructing its GUI. */
	g_platform = new AppPlatform_winmobile();
	appContext.platform = g_platform;

	RECT client;
	GetClientRect(hwnd, &client);
	const int clientW = (int)(client.right - client.left);
	const int clientH = (int)(client.bottom - client.top);

	/* The GL surface, not the window, is the authority on how many pixels the
	   engine has to fill -- they differ if DPI virtualisation is in play, and
	   drawing to the window's idea of the size is what put the whole game in the
	   top-left corner of the panel.  Falling back to the client rect if the
	   driver would not answer eglQuerySurface. */
	if (surfW <= 0 || surfH <= 0) {
		LOGW("eglQuerySurface gave no size; falling back to the client rect\n");
		surfW = clientW;
		surfH = clientH;
	}

	/* A disagreement here is a diagnostic rather than a decision -- the driver
	   derives the surface from the client rect, so they match unless something
	   unexpected happened, and if it did the log should say so before the
	   rendering makes it obvious. */
	if (surfW != clientW || surfH != clientH)
		LOGW("the GL surface is %dx%d but the client rect is %dx%d\n",
		     surfW, surfH, clientW, clientH);

	g_surfW = surfW;
	g_surfH = surfH;

	LOGI("render size %dx%d (window %dx%d, pointer input scaled by %d)\n",
	     surfW, surfH, clientW, clientH, g_pixelScale);
	g_platform->setScreenSize(surfW, surfH);

	MAIN_CLASS* app = new MAIN_CLASS();
	g_app = app;

	/* CE has no current directory, so "." -- which the win32 port uses -- has
	   nothing to resolve against.  wce_fixPath would anchor it to the exe
	   directory anyway; naming that directory outright is the same answer
	   without the indirection. */
	app->externalStoragePath      = wce_exeDir();
	app->externalCacheStoragePath = wce_exeDir();

	/* Through the App* on purpose: NinecraftApp declares its own init() with no
	   arguments, which hides App::init(AppContext&) from the derived type. */
	g_app->init(appContext);
	app->setSize(g_platform->getScreenWidth(), g_platform->getScreenHeight());

	/* Every texture and every static table is in memory by here, so this is the
	   baseline a level has to fit on top of. */
	wce_logMemory("after app init");

	MSG msg;
	while (g_running && !app->wantToQuit()) {

		if (!g_active) {
			/* Backgrounded: block until something happens rather than render
			   into a surface nobody can see. */
			if (GetMessage(&msg, NULL, 0, 0) <= 0) {
				g_running = false;
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}

		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) > 0) {
			if (msg.message == WM_QUIT) {
				g_running = false;
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		g_platform->_tick();
		app->update();
	}

	LOGI("shutting down\n");

	/* Order matters: the app destructor still deletes textures and buffers, so
	   the context has to stay current until it has finished. */
	delete app;
	g_app = 0;

	g_platform->finish();
	delete g_platform;
	g_platform = 0;
	appContext.platform = 0;

	shutdownEGL(hwnd, &appContext, hdc);
	DestroyWindow(hwnd);
	restoreOrientation();

	/* After the window is gone, so no message can arm another buzz between
	   the last stop and the process exiting. */
	wce_vibrateShutdown();

	wce_logShutdown();
	return 0;
}

#endif /*MAIN_WINMOBILE_H__*/
