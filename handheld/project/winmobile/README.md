# Minecraft PE 0.6.1 — Windows Mobile / Windows CE 5.2 port

Target device is the **HTC HD2** (Windows Mobile 6.5, ARMv7 Snapdragon QSD8250,
Adreno 200, 448 MB RAM but a **32 MB limit per process**). Everything specific to
this platform lives in this directory; the game sources under `../../src` are
shared with the iOS ARMv6 and Raspberry Pi ports and are only touched where a
`WINMOBILE` guard was unavoidable.

Built from the iOS 3.1 ARMv6 port, so the memory work done for that target
carries over — `GFX_SMALLER_CHUNKS` (reduced chunk-loading radius and smaller
render caches) is on, and the 22 MB PCM sound bank is compiled out.

## Building

Needs **CeGCC 4.4.0** (`arm-mingw32ce-`) in `/opt/mingw32ce`. C++98 throughout;
nothing here uses C++11.

    make                 # debug:   -O0 -g,  ~40 MB unstripped
    make release         # release: -Os -DNDEBUG -DPUBLISH, stripped
    make clean

    make CROSS=/path/to/bin/arm-mingw32ce-        # toolchain elsewhere

A clean release build is about 40 s on six cores: 247 game translation units,
4 platform ones, and 93 of RakNet.

Both configurations compile and link with zero errors and one warning (a
pre-existing `WCHAR*` conversion in RakNet's `RakString.cpp`). They keep their
objects in separate trees but write the same `Minecraft.exe`, so switching mode
forces a relink rather than leaving the other configuration's binary behind
under the expected name — `make release` after `make` really does replace the
39 MB debug exe with the 2 MB stripped one.

## Deploying

Copy two things into the same folder on the device, side by side:

    Minecraft.exe
    data\            (from ../../data — 3.9 MB, 234 files)

Windows CE has no working directory, so every relative path the game opens is
anchored to the `.exe`'s own directory by `wce_fixPath` in `wince_compat.cpp`.
`data\` has to sit next to the executable for that reason, and `Minecraft.exe`
should not be run from a path it cannot resolve.

Nothing else ships. `-static` links libstdc++, libgcc and libmingwex in, and the
three DLLs the binary imports are all already in the HD2's ROM:

    COREDLL / coredll.dll   158 imports   C runtime + Win32
    libGLES_CM.dll           62 imports   49 gl* + 13 egl*
    AYGSHELL                  1 import    SHFullScreen
    WS2                      28 imports   Winsock 2

`libGLES_CM.dll` covers EGL as well as GL on this device, which is why there is
no `-lEGL`. `coredll` appears twice because two import libraries spell it
differently (`_ecvt`/`_fcvt` come in lowercase); the CE loader resolves both.

The release binary is a 2.06 MB `pei-arm-wince-little` image: subsystem 9
(Windows CE) version **4.20** rather than CeGCC's default 3.0, to match what
eVC 4 and Visual Studio stamp; 1 MB `SizeOfStackReserve` rather than 2; and a
92-byte `.rsrc` holding one resource, which is HI_RES_AWARE — see below.

## What is in here

    Makefile                three source groups, three pattern rules; RakNet
                            goes into a static archive so unreferenced members
                            never reach the exe (27 of 93 do)
    src/main_winmobile.h    entry point: window, EGL context, message loop,
                            landscape rotation, SHFullScreen, WM_HIBERNATE,
                            panel measurement, window sizing, input scaling
    src/hires.rc            the HI_RES_AWARE resource — two bytes asking the OS
                            not to DPI-virtualise us.  This HD2's ROM ignores
                            it however it is filed, so `measureDisplay` in
                            main_winmobile.h is the fix that actually works and
                            this is now a courtesy to better-behaved devices
    src/AppPlatform_winmobile.cpp   the AppPlatform implementation
    src/wince_compat.{h,cpp}        the CRT, path and logging layer — see below
    src/wince_bigalloc.cpp          slab allocator for the level's chunk arrays,
                                    backed by memory-mapped views that land
                                    outside the 32 MB slot — see below
    src/wince_vibrate.cpp           the vibrator, and the thread whose only job
                                    is switching it off again — see below
    src/png_decode.{h,cpp}          self-contained PNG→RGBA decoder, so there is
                                    no libpng or zlib dependency
    include/                headers that must beat the sysroot — see
                            include/README.md
    lib/                    by-name import libraries generated from the stock
                            HD2 firmware DLLs, with the .def files they came from
    tools/                  gen_gl_headers.py, pe_exports.py — regenerate
                            include/GLES/gl.h and include/EGL/egl.h from a DLL

`wince_compat.h` is force-included into every translation unit
(`-include src/wince_compat.h`), which is how `fopen`/`_access`/`_mkdir` get
redirected to path-fixing versions without editing any call site, and how
`remove` gets a declaration at all — Windows CE has neither `remove` nor
`errno`, and CeGCC's `<stdio.h>`, `<io.h>` and `<errno.h>` are all unusable as
shipped. It is also why `wce_bigAlloc`/`wce_bigFree` are declared there rather
than in a header of their own: the four call sites that use them are in the
shared game sources, and none of them needed an `#include` adding.

## Display size, DPI and touch

Windows Mobile hands an application that has not said otherwise a **virtual
QVGA coordinate space** on a WVGA device: `SM_CXSCREEN` returns half the real
width, `LOGPIXELSX` reports 96 instead of 192, and GDI scales everything ×2 on
the way to the panel. EGL and GL are not part of that bargain — they rasterise
at the size they are given, in real pixels — so the first device build drew the
whole game into the top-left quarter of the screen while the touch driver
delivered coordinates that had already been divided down to match the window.
Nothing reports an error in that state; it just looks like a broken resolution.

`src/hires.rc` is the documented way out. It is one resource, `name 1, type 24`,
holding the single `WORD 0x0001` that Windows Mobile reads as **HI_RES_AWARE** —
the type has no keyword in `windres`, hence the bare `24`. The Makefile compiles
it with `windres -O coff`, links it alongside the objects rather than into the
RakNet archive (nothing references it by symbol, so as an archive member it would
never be pulled in), and then **fails the build** if `.rsrc` is missing from the
image:

    HI_RES_AWARE: present (.rsrc in image)

**This HD2's ROM ignores it.** The resource in the shipped image was decoded by
hand — directory type 24, name 1, `Size = 2`, `OffsetToData` a correct RVA,
payload `01 00`, `.rsrc` flagged `CONTENTS ALLOC LOAD DATA`, data directory
entry present — and it is structurally identical to what eVC 4 emits, yet the
device still reports 400x240 at 96 dpi after rotating. Its language was 0x0409;
it is now `LANGUAGE 0, 0` (neutral), because a localised ROM — the test device
runs Russian WM 6.5 — looks for its own language and then neutral, and need not
fall back to US English. That may be the whole story or none of it; it is
untested and it is not what the port relies on.

What the port relies on is **measuring the panel at startup** rather than
believing `SM_CXSCREEN`. `measureDisplay()` in `main_winmobile.h` runs right
after the landscape rotation and asks the display driver for the raw
framebuffer:

    ExtEscape(hdc, GETRAWFRAMEBUFFER /* 0x00020001 */, ...)

That GAPI-era escape is the one call that answers in **real panel pixels**
regardless of DPI virtualisation. Its whole-number ratio against `SM_CXSCREEN` /
`SM_CYSCREEN` — compared long-edge-to-long-edge, so orientation does not matter
— is the virtualisation factor `g_pixelScale`, and it drives two things:

* the game window is created at `SM_CXSCREEN × g_pixelScale`, so the GL surface
  covers the whole panel instead of a quarter of it;
* pointer coordinates arrive in the virtual space and are multiplied by
  `g_pixelScale` on the way in (`toSurfaceX/Y`), which is a constant, not a
  window-to-surface ratio. The earlier `computeInputScale` derived the factor
  from the client rect against the surface size — both of which were 400x240 —
  so it computed 1:1 and could never have done anything.

`ExtEscape` is bound with `GetProcAddress` on `coredll.dll` rather than imported,
because a name in the import table that the ROM does not export kills the process
at load with no message, and this one is not needed to run. If it is missing, or
the driver refuses the escape, or the ratio is not a small whole number, the
function logs why and leaves `g_pixelScale` at 1 — which is exactly the old
behaviour, and also the correct behaviour on any device that does honour
HI_RES_AWARE, where panel and screen already agree.

The engine is still sized from **`eglQuerySurface`**, not from the window: the
surface is the authority on how many pixels GL will actually fill. If EGL
refuses the larger window, `main()` destroys it, recreates it at the size the OS
reports and tries again, so the worst case is the old quarter-panel picture
rather than a failure to launch. `WM_SIZE` is ignored once a surface exists and
disagrees with it, since a resize the driver did not perform means nothing to GL.

At 800x480 the engine picks `Gui::GuiScale = 3.0f` (`Minecraft::setSize`) instead
of the 2.0 it used at 400x240 — that is the intended MCPE look on a WVGA phone,
and no scaling code of ours is involved.

Four log lines describe the outcome:

    raw framebuffer 800x480, 16 bpp, stride ...          the panel, measured
    DPI virtualisation is active: ...                    or: no DPI virtualisation
    creating a 800x480 window (OS screen 400x240)
    render size 800x480 (window 800x480, pointer input scaled by 2)

`logDisplayMetrics` still records `SM_CXSCREEN`, `LOGPIXELSX/Y`, colour depth
and the client rect at startup and again after the rotation; 96 dpi on a 480x800
panel in that output means the resource did not take effect, which is now a
diagnostic rather than a defect.

## Logging

The build writes **`minecraft.log`** next to `Minecraft.exe`, and does so in
release builds too. That is not the default this tree came with: `log.h` tested
`PUBLISH` before the platform, and `make release` passes `-DPUBLISH`, so on the
first device run every `LOGI`/`LOGW`/`LOGE` in the game had been compiled down to
`do {} while (0)` and the log file was zero bytes. `WINMOBILE` is now tested
first, deliberately, with a comment saying why.

Each line is prefixed with `GetTickCount()` and flushed immediately, so a log
that stops mid-world-creation still says how far it got and when. Lines also go
to `OutputDebugStringW`. Two threads write — level generation runs on its own
thread while the main thread ticks the progress screen — so the writes are
serialised behind a critical section.

`wce_logMemory("where")` adds a one-line memory snapshot. The line now has two
halves, and they have to be read together:

    mem [levelgen]: load 50%, phys 173032K/345016K free, virt 13760K/32768K free,
      slot largest free 11800K, outside slot 7680K used of 8192K in 8 views (peak 7680K)

`virt free` is `dwAvailVirtual`, which on CE means free address space **in this
process's slot** — it totals 32768K, and it is the figure that falls by a flat
amount per allocation. **`slot largest free`** is the largest *contiguous* run in
the same slot, found by walking `VirtualQuery`; it is what an allocation of a
given size actually needs, so it drops faster than `virt free` as the slot
fragments, and it is the number that hits zero first. `phys` is the whole 448 MB
device (345016K of it usable) and is nearly irrelevant here: on the run that
diagnosed the world-creation crash it still reported 164 MB free at the moment
`new` failed.

**`outside slot`** is the second half, from `wince_bigalloc.cpp`. The level's
chunk storage was moved out of the slot on purpose, so during generation
`outside slot` should now grow while `virt free` stays roughly flat. If `virt
free` is still falling by ~1536 KB per 16-chunk column, something that was
supposed to be out of the slot is not.

Snapshots are taken at startup, after app init, per column of chunks during
generation, and after the world is ready.

Out of memory is also no longer a silent death. This build is `-fno-exceptions`
on an SJLJ toolchain, so a `bad_alloc` thrown by libstdc++'s prebuilt
`operator new` unwinds into frames with no unwind information and the process
vanishes with no message. `std::set_new_handler` runs *before* that throw, so
`wce_installOomHandler` (called first thing in `main`) installs a handler that
logs `*** OUT OF MEMORY ***` plus a memory snapshot and terminates. It is
allocation-free on purpose, down to a stack buffer for the widening that
`OutputDebugStringW` needs. `SetUnhandledExceptionFilter` was the obvious
alternative and is simply absent from CeGCC's `libcoredll.a`.

One line at startup records how the vibrator came up. `vibrate: NLED 1, stopper
thread up` is the healthy case, with `, aygshell VibrateStop also available`
appended where the ROM exports it. The two unhealthy ones name the `CreateThread`
error and say what they fell back to — aygshell's self-timing `Vibrate`, or
nothing at all, because vibration disables itself rather than run without a
guaranteed stop.

## Fitting in 32 MB

`GFX_SMALLER_CHUNKS` from the iOS port is the big lever and it is on. What this
port adds, all of it forced by the fact that CE reserves *inside* the 32 MB slot
rather than out of a 2 GB address space:

* **The level's chunk arrays are not in the slot at all.** 20 of the 24 MB a
  resident level needs now comes from memory-mapped views above the process
  slots — see *Allocating outside the 32 MB slot* below. That is the change that
  world creation was actually blocked on; everything else in this list is what
  makes room for the engine around it.
* **Texture pixel copies are freed after upload** (`Textures::assignTexture`).
  The class otherwise keeps every decoded image in `loadedImages` for the life of
  the process — about 3.6 MB of RGBA here — so the record stays and the buffer
  goes. With one exception, and it is the exception that matters: of the seven
  callers of `getTemporaryTextureData`, six read only `->w` and `->h`, but
  `Font::init` scans the font sheet's alpha channel to measure glyph widths, and
  does it again on every `onGraphicsReset`. Anything named `font/*` therefore
  keeps its pixels — 64 KB of the 3.6 MB, for a 128x128 sheet. The test is on the
  resource name inside `assignTexture` rather than an argument at the call site,
  because `loadTexture` returns the cached id for a name it has already seen: the
  first caller would decide for everyone, and for the font sheet that is either
  `Font::init` or `Gui::renderToolBar` depending on when a graphics reset lands.
  The decision belongs to the resource. `Font::init` also survives a NULL `data`
  now, with default widths and a log line, instead of faulting.
* **1 MB stack reserve instead of 2** (`-Wl,--stack,0x100000`). The header value
  is what every thread created with `dwStackSize == 0` gets, RakNet's included,
  and it is reserved in the slot whether it is ever touched or not.
* **256 KB for the level-generation thread** (`CThread.cpp`), which is the only
  `CThread` in the codebase. Its deepest frames are the generator's, under 3 KB.
* **The `CommandServer` is not created** — see below.

The arithmetic that forced the first of those bullets came straight off the
device log. Once generation settled, each 16-chunk column cost a flat **1536 KB
of `virt free` — 96 KB per chunk** — and only 14,336 KB was free when
`selectLevel` started. 256 chunks × 96 KB is 24 MB, so `new` failed right after
`levelgen: 96/256 chunks`, with `slot largest free` down to 132 KB and 164 MB of
physical RAM still unused: not out of memory, out of *address space*. A chunk's
four arrays are 32768 + 3×16384 = 80 KB, so the other 16 KB per chunk was CE heap
header plus page rounding — 4 MB per level in padding alone.

Running at the full panel size costs something here, and it is worth naming:
800x480 RGB565 with a 16-bit depth buffer is ~1.5 MB of colour plus depth against
~375 KB at 400x240. Whether the driver takes that out of our 32 MB slot or its
own address space is not visible from this side of EGL — compare
`slot largest free` in the `[startup]` snapshot against the `[after app init]`
one to find out, since the surface is created between them.

## Allocating outside the 32 MB slot

`new` and `malloc` cannot help with the arithmetic above, because the 32 MB is not
a quota — it is a **window of virtual address space**, and every allocator that
CE gives a process hands back addresses inside it. The CE 5.x user-mode layout is

    0x00000000 - 0x01FFFFFF   slot 0: the running process        (32 MB)
    0x02000000 - 0x03FFFFFF   slot 1: XIP/ROM DLLs
    0x04000000 - 0x41FFFFFF   slots 2..32: one per process       (32 MB each)
    0x42000000 - 0x7FFFFFFF   memory-mapped views, shared        (~1 GB)

and the last row is the way out. A view created with `CreateFileMapping` +
`MapViewOfFile` is placed in that shared region rather than in the caller's slot.
Passing **`INVALID_HANDLE_VALUE`** as the file handle asks for a mapping with no
file behind it, backed by the system's RAM — so it is ordinary read/write memory
once mapped, with no commit call, no special access rules, and nothing stopping it
being handed to `memset` or to the level generator. This is the trick usually
filed under "large memory area" on CE.

`src/wince_bigalloc.cpp` wraps it as a **slab allocator, not a heap**:

* requests are rounded to a power-of-two size class from 4 KB to 64 KB and served
  from 1 MB views carved into equal slots, with a free list threaded through the
  free slots themselves. The two sizes this port asks for — 32768 for a chunk's
  block ids, 16384 for each of its three nibble layers — are exact powers of two,
  so a view divides with nothing left over and **no per-allocation header**. That
  is not incidental: the absent header is what also removes the 4 MB of CE heap
  padding counted above;
* anything larger than 64 KB gets a view of its own, rounded to CE's 64 KB view
  granularity, and unmapped when freed;
* anything smaller than 4 KB is not worth a slot and goes to the heap.

Four call sites in the shared sources are routed through it, all under
`#ifdef WINMOBILE`, and together they are every per-chunk array in a level:

    DataLayer::DataLayer                    3 × 16 KB per chunk   (12 MB/level)
    RandomLevelSource::getChunk             32 KB per chunk        (8 MB/level)
    PerformanceTestChunkSource::create      32 KB per chunk
    ExternalFileLevelStorage::load          32 KB per chunk (loading a saved world)

`LevelChunk::deleteBlockData` and `~DataLayer` are the matching frees, and they
are the only frees — `LevelChunk::~LevelChunk`'s `delete[]` is commented out
upstream, `setBlocks` copies rather than adopting, and `DataLayer`'s
buffer-adopting constructor is never called. What is left in the slot per chunk is
the `LevelChunk` object itself, two 256-byte maps and a handful of empty
containers: on the order of 1 KB against 96 KB before.

**Every failure mode degrades to the old behaviour rather than to a crash.** If
the mapping APIs refuse, if the shared region is exhausted, or if the 64-view
table fills, `wce_bigAlloc` returns `new[]` memory and logs why; `wce_bigFree`
range-checks against the view table and passes anything that belongs to no view
to `delete[]`. So the worst case is what this file was written to replace, with a
line in the log saying so — never a leak, and never a mismatched free even though
mapped and heap pointers are deliberately mixed.

One fact cannot be known off the device: the table above is the documented CE 5.0
layout, but the address a view actually gets is up to the ROM's kernel
configuration. The first successful mapping therefore reports itself:

    bigalloc: first view at 0x42010000, 1024K
    bigalloc: views land above the process slot, so chunk storage no longer
      competes with the 32 MB

An address below `0x02000000` means the view came out of our own slot, nothing was
gained, and the log says that instead — loudly, as a `WARNING`, because it is the
one outcome that would make world generation fail the same way it did before.

## The vibrator, and the thread that switches it off

Breaking a block asks for a 24 ms buzz — `GameMode::destroyBlock` calls
`AppPlatform::vibrate`, and `Options::destroyVibration` defaults to on. Windows
Mobile has no timed one-shot vibrate that CeGCC can link, so the motor is driven
through the notification-LED driver, where on this device it is LED 1:

    NledSettingsInfo info = { /* LedNum */ 1, /* OffOnBlink */ 1 };
    NLedSetDevice(NLED_SETTINGS_INFO_ID, &info);

Two things about that interface make it dangerous, and the first version of this
code got both of them wrong:

* **Blink mode (`OffOnBlink = 2`) does not stop.** `MetaCycleOn`/`MetaCycleOff`
  are not a count of cycles to run — they describe a pattern that *repeats*: N
  cycles blinking, M cycles dark, forever. The interface has no terminating count
  at all. Asking for "one meta cycle" asks for an endless one.
* **LED state belongs to the driver, not to the process that set it.** CE does
  not reset it on process exit, so closing the game switches nothing off.

Together those cost a reboot, which is exactly what happened on the third device
run: breaking one block started the motor, the motor outlived the game, and only
restarting the OS stopped it.

So the motor is now switched **on steadily and off explicitly**, and everything in
`wince_vibrate.cpp` exists to make the off unmissable:

* **A dedicated thread owns the off.** It waits on an auto-reset event, then holds
  the motor until the deadline and switches it off — independent of frame rate, of
  what the main thread is doing, and of how long a level takes to generate. The
  deadline is re-read every pass, so a second block broken mid-buzz extends the
  pulse instead of queueing another one. Its stack is requested as 16 KB rather
  than inheriting the image header's 1 MB, because a thread's stack reserve comes
  out of the 32 MB slot whether it is ever touched or not.
* **`wce_vibrateStop()` is called from every path that can end a buzz**: losing
  focus (`WM_ACTIVATE`), `WM_HIBERNATE`, `WM_CLOSE`, `WM_NCDESTROY`, the end of
  `main()`, `atexit`, and the out-of-memory handler — which matters because that
  handler ends in `TerminateProcess`, which runs no `atexit` handlers and no
  destructors. It takes no lock and allocates nothing, precisely so it is legal
  from all of those. A stop that can block is a stop that can be missed.
* **`wce_vibrateInit()` switches the motor off before it does anything else.**
  A kill that runs no cleanup at all is the one case nothing inside a process can
  cover; after this, the cure for a latched motor is starting the game once rather
  than rebooting the device.
* **If no stop can be guaranteed, there is no buzz.** Should the stopper thread
  fail to start, vibration disables itself and says so in the log. No haptics is a
  missing feature; a latched motor is a broken phone.

Requests are clamped to 250 ms, so no caller can ask the motor to run for a
minute and no computed deadline can be further out than that.

aygshell's `Vibrate()`, which the OS times out by itself, would be strictly safer
and is deliberately *not* the primary path: it is a Windows Mobile Standard API,
absent from CeGCC's `libaygshell.a`, and there is no way to know from here whether
this ROM has it or whether it obeys the profile's vibrate setting — while NLED 1
is proven to drive this device's motor, because the buzz worked from the first
device build. It is bound by name anyway and used if the thread is missing, and
`VibrateStop()` joins the off path when the ROM exports it. Binding by name is the
usual CE reason: an import the ROM does not export kills the process at load with
no message. Nothing here added an entry to the import table — the coredll count is
unchanged at 158, `NLedSetDevice` was already in it, and `Vibrate`/`VibrateStop`
appear nowhere in it by design.

## Deliberately stubbed or left out

* **Audio.** `SoundEngine.h` resolves to the header-only no-op `SoundSystem` on
  anything that is not Android or Apple, so audio is already stubbed with no
  source file to compile and no OpenAL to link. Nothing was mocked by hand.
* **`CommandServer`.** `Minecraft.cpp` builds one under `#ifdef WIN32`, and
  CeGCC predefines `WIN32`, so this port was getting it by accident: a TCP
  listener on port 4711, a `CameraEntity` mob, and an on-demand multi-megabyte
  `restoreBuffer`, none of it reachable from a phone with no console attached.
  It is now `#if (defined(WIN32) || defined(RPI)) && !defined(WINMOBILE)`.
  (`ConsoleScreen.cpp` is still compiled — the same `WIN32` branch instantiates
  it for the F3 debug overlay, which does work.)
* **`_RAKNET_SUPPORT_*` trimming.** Not done, because it is not needed: linking
  RakNet as an archive already keeps every unused plugin out. See the comment
  above `RAKNET_SRCS` in the Makefile.
* **Multitouch.** The engine handles up to twelve pointers (`Multitouch`,
  `MAX_POINTERS = 12`) and `TouchscreenInput::tick` iterates every active one, so
  a second finger would let the on-screen D-pad and a look-drag work at the same
  time. This port feeds one pointer, id 0, from `WM_LBUTTONDOWN` /
  `WM_MOUSEMOVE` / `WM_LBUTTONUP`, because that single stylus stream is all CE's
  message queue delivers. The HD2's digitizer can see two fingers; nothing in this
  SDK can ask it to. Everything touch-adjacent in CeGCC's import libraries was
  checked, and every one of them is single-pointer: `TouchCalibrate` (calibration),
  `SHRecognizeGesture` (the tap-and-hold recogniser, one point), and
  `GetMouseMovePoints`/`GetMouseMovePointsEx` (the *history* of one pointer, at
  finer time resolution — not a second finger). There is no `WM_TOUCH`, no
  `WM_GESTURE`, no `TKGetGestureInfo` and no HTC-specific entry point anywhere in
  the headers or the libraries, and the only firmware DLLs copied into `htchd2/`
  are the four GLES ones, which say nothing about touch. Feeding extra pointers is
  a three-line change in the window proc once the interface is known — identifying
  the interface is the whole job, and it starts with extracting the ROM's
  `coredll.dll`, `aygshell.dll` and touch driver the way the GLES DLLs were and
  reading their exports with `tools/pe_exports.py`.
* **ARMv7 / VFP.** The build stays at the toolchain's ARMv4 soft-float default.
  libstdc++, libgcc and libmingwex are all prebuilt that way, and CE 5.2's
  context switching around VFP is a known hazard. `make ARM_FLAGS=...` is there
  to try it once the game runs.

## Status

Loads on the device, rotates to landscape, brings up EGL on `libGLES_CM.dll`,
and got as far as rendering the menu on the first device run.

Fixed after the first device run:

* The dirt menu background rendered as vertical colour stripes.
  `AppPlatform_winmobile::loadTexture` was setting `TextureData::transparent`
  from whether the PNG had an alpha channel — but in this codebase that field is
  not "has transparency", it is the GL pixel-format selector, read only as
  `img.transparent ? GL_RGBA : GL_RGB`. An opaque texture therefore had its
  always-4-bytes-per-pixel buffer uploaded as 3-byte RGB, which walks the buffer
  at the wrong stride. `GL_RGBA` is also the only legal answer on ES 1.1, where
  `internalformat` must equal `format`.
* The game drawing into a quarter of the panel with mismatched touch — see
  *Display size, DPI and touch*. The HI_RES_AWARE resource turned out not to be
  enough on this ROM, so the size is measured at runtime instead.
* An empty `minecraft.log` in release builds — the `log.h` ordering, below.

Fixed after the second device run:

* **A crash on launch, immediately after the menu textures loaded.** WinCE
  reported `0xC0000005` at `Offset 000ac624`, which is an RVA relative to the
  0x10000 image base, so `0xbc624` in the unstripped link — 0xd0 into
  `Font::init(Options*)`. The texture-buffer optimisation above had freed the
  font sheet's pixels that `Font::init` reads, on the strength of a `grep` for
  `getTemporaryTextureData` that matched nothing in `Font.cpp`, because that file
  is ISO-8859 with CRLF line endings and `grep` here fails silently on it. Both
  halves are fixed: `font/*` keeps its buffer, and `Font::init` no longer
  dereferences a NULL one. (Use Python or `sed`, not `grep`, when auditing these
  sources.)

Confirmed on the third device run: **the runtime panel measurement works.** The
log reported `raw framebuffer 480x800, 16 bpp`, `DPI virtualisation is active`,
`creating a 800x480 window (OS screen 400x240)`, `EGL surface 800x480` and
`render size 800x480 (window 800x480, pointer input scaled by 2)`. Resolution and
touch mapping are done, and the engine picked `GuiScale = 3.0` as intended.

Addressed after the third device run:

* **World creation was running out of address space, not memory.** The same log
  made it exact: a flat 96 KB of slot per chunk, 14,336 KB free when
  `selectLevel` started, and `new` failing right after `levelgen: 96/256 chunks`
  with `slot largest free` at 132 KB while 164 MB of physical RAM sat idle. The
  level's ~20 MB of chunk arrays now come from mapped views outside the slot,
  which also removes the 4 MB of CE heap padding — see *Allocating outside the
  32 MB slot*. This also exonerates the accidental `CommandServer`, which had been
  the other suspect: it was the last thing world creation did before returning,
  and generation never got that far.

Confirmed on the fourth device run: **worlds generate and the game is playable.**
Creative and survival both hold a stable 30–40 fps. Since generation was failing
at chunk 96 of 256 before, that outcome is also the practical proof that the mapped
views land outside the slot — the log line that says so outright,
`bigalloc: first view at 0x…`, has not been read back yet, and `virt free` staying
roughly flat across the `levelgen` snapshots instead of falling 1536 KB per column
is the other half of the confirmation.

Addressed after the fourth device run:

* **The vibration motor ran forever and outlived the game.** Breaking a block
  latched it on, closing the game did not stop it, and only restarting the OS did.
  Both halves were mistakes about the notification-LED interface — blink mode
  repeats its meta cycles indefinitely, and LED state belongs to the driver rather
  than the process — see *The vibrator, and the thread that switches it off*. The
  motor is now switched on steadily with a dedicated thread owning the off, stopped
  from every path that can abandon a buzz, and switched off at startup, so a
  motor left latched by some earlier build needs the game started once instead of
  a reboot.

Untested on the device, and what to look for next: `vibrate: NLED 1, stopper thread
up` in the log, a buzz that stops on its own when a block breaks, and a motor that
is silent after the game is closed — including after closing it mid-buzz.

Also open, and unrelated to any of the above: `Missing category for tile N`
errors at startup. They happen on every platform built from this source and are
not a port problem.

One thing still worth doing if the game ever fails to load at all: the 158
coredll imports come from CeGCC's `libcoredll.a`, which was generated from a
CE 5.x SDK rather than from this phone. If a stock `coredll.dll` gets extracted
from the HD2 firmware the way the GLES DLLs were, `tools/pe_exports.py` will
list its exports and the two sets can be diffed; a name CeGCC believes in and
the ROM does not export fails at load time with no message. The four the
allocator added — `CreateFileMappingW`, `MapViewOfFile`, `UnmapViewOfFile`,
`InitializeCriticalSection` — are core CE 5.0 API and were the first candidates
worth checking, which is part of why the allocator's failure path is a log line
and a fallback rather than an assumption.
