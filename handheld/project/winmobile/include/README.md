# `project/winmobile/include`

Headers that only the Windows Mobile / Windows CE build sees. Put this
directory on the include path *before* the CeGCC sysroot.

## `GLES/gl.h`, `EGL/egl.h`

Generated — do not edit. Run

    python3 ../tools/gen_gl_headers.py \
        --khronos ../../../lib/include \
        --dll     <path to stock libGLES_CM.dll> \
        --out     .

Signatures are lifted from the Khronos reference headers in
`handheld/lib/include`, then re-emitted with a platform layer written for
CeGCC/WinCE ARM and filtered against the export table of the real device
driver. Anything the driver does not export lands in an `#if 0` block, so
calling it is a compile error rather than a link error.

The stock HD2 (Adreno 200) `libGLES_CM.dll` covers 145/145 GL and 34/34 EGL
entry points, and exports the `egl*` symbols itself — `-lGLES_CM` alone
satisfies both.

## `Windows.h`, `WinSock2.h`, `Ws2tcpip.h`

Case-only forwarding shims. The sources — mostly RakNet — spell these
headers the way the Windows SDK does, but the CeGCC sysroot ships them
lowercase, and Linux has a case-sensitive filesystem. Rather than patch
upstream files, these forward to the real lowercase headers.

Safe because the lookup is case-sensitive: `#include <windows.h>` from
inside `Windows.h` does not find a lowercase `windows.h` here, so it falls
through to the sysroot.

## `errno.h`, `direct.h`

Replacements for two sysroot headers that cannot be used as shipped. Both
consist of nothing but

    #ifdef __COREDLL__
    # include_next <errno.h>

and `arm-mingw32ce-gcc` *does* predefine `__COREDLL__`, so both expand to
`error: no include path in which to search for errno.h`. They were written
to defer to a Windows CE SDK that CeGCC 4.4.0 does not ship.

`conio.h`, `locale.h`/`clocale`, `signal.h`/`csignal` and `varargs.h` are
broken the same way but are not shimmed, because nothing in the build needs
them — the only `conio.h` users are RakNet's `Getche.cpp`/`Kbhit.h`, which
the Makefile's source list leaves out. (`float.h`/`cfloat` look like they
should be affected and are not: GCC's own copy is found first.)

* **`errno.h`** — Windows CE has no `errno` of any kind, so this declares
  `int* _errno(void)` and defines `errno` as `(*_errno())`, with the
  MSVC/mingw `E*` values. The storage lives in `../src/wince_compat.cpp`.
  Needed by RakNet's `rdlmalloc` (`MALLOC_FAILURE_ACTION` is
  `errno = ENOMEM`), `FileOperations.cpp` and `FolderMethods.cpp`.
  Deliberately *not* mapped onto `GetLastError()` — see the header comment.
* **`direct.h`** — forwards to `<io.h>`, which on this toolchain is where
  `_mkdir`/`_access`/`_unlink` are actually declared. Nothing is lost: the
  sysroot version added only the drive-letter calls, which are meaningless
  on CE.
