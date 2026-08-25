#!/usr/bin/env python3
"""Generate self-contained OpenGL ES 1.1 / EGL headers for the WinCE port.

Signatures are extracted from the official Khronos headers shipped in
handheld/lib/include (authoritative), then re-emitted with a platform layer
written for CeGCC / Windows CE ARM:

  * no dependency on KHR/khrplatform.h, which on this target both emits
    __stdcall (when _WIN32_WCE is not yet defined) and typedefs
    khronos_int32_t to the MSVC-only __int32.
  * fixed-width types spelled out for 32-bit ARM.
  * GL_APIENTRY / EGLAPIENTRY empty: the ARM ABI has a single calling
    convention, so __stdcall is meaningless here.
  * plain "extern" rather than __declspec(dllimport), so the generated
    import libraries in ../lib resolve by name.

Every prototype is cross-checked against the real device DLL export table.
Functions the driver does not export are emitted inside an #if 0 block and
listed in a summary comment, so a call to one is a compile error rather than
a link error or a silent stub.

Usage:
    gen_gl_headers.py --khronos <dir> --dll <libGLES_CM.dll> --out <include-dir>
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_exports import PE  # noqa: E402

# khronos_* -> concrete 32-bit ARM spelling
KHR_TYPES = {
    'khronos_int8_t': 'signed char',
    'khronos_uint8_t': 'unsigned char',
    'khronos_int16_t': 'short',
    'khronos_uint16_t': 'unsigned short',
    'khronos_int32_t': 'int',
    'khronos_uint32_t': 'unsigned int',
    'khronos_int64_t': 'long long',
    'khronos_uint64_t': 'unsigned long long',
    'khronos_float_t': 'float',
    'khronos_intptr_t': 'int',
    'khronos_uintptr_t': 'unsigned int',
    'khronos_ssize_t': 'int',
    'khronos_usize_t': 'unsigned int',
    'khronos_utime_nanoseconds_t': 'unsigned long long',
    'khronos_stime_nanoseconds_t': 'long long',
}

PROTO_RE = {
    'gl': re.compile(r'^GL_API\s+(.+?)\s+GL_APIENTRY\s+(\w+)\s*\((.*)\)\s*;$'),
    'egl': re.compile(r'^EGLAPI\s+(.+?)\s+EGLAPIENTRY\s+(\w+)\s*\((.*)\)\s*;$'),
}
PROTO_START = {'gl': 'GL_API', 'egl': 'EGLAPI'}
DEFINE_RE = re.compile(r'^#\s*define\s+((?:GL|EGL)_\w+)\s+(\S.*?)\s*$')
TYPEDEF_RE = re.compile(r'^typedef\s+(.+?)\s+(\*?\w+)\s*;')


def subst_khr(text):
    for k, v in KHR_TYPES.items():
        text = re.sub(r'\b%s\b' % k, v, text)
    return text


def parse(path, kind):
    """Pull defines, typedefs and prototypes out of a Khronos header.

    Prototypes may span several physical lines (most of egl.h's do), so they
    are accumulated into one logical line, up to the terminating semicolon,
    before matching.
    """
    defines, typedefs, protos = [], [], []
    proto_re = PROTO_RE[kind]
    start = PROTO_START[kind]
    pending = None

    with open(path, 'r') as f:
        for raw in f:
            line = raw.strip()

            # continuation of a prototype spanning multiple lines
            if pending is not None:
                pending = pending + ' ' + line
                if ';' not in pending:
                    continue
                line, pending = ' '.join(pending.split()), None
            elif line.startswith(start):
                if ';' not in line:
                    pending = line
                    continue

            m = proto_re.match(line)
            if m:
                ret, name, args = m.group(1), m.group(2), m.group(3)
                protos.append((name, subst_khr(ret.strip()),
                               subst_khr(' '.join(args.split())) or 'void'))
                continue
            if line.startswith(start):
                sys.stderr.write('warning: unparsed prototype: %s\n' % line)
                continue

            m = DEFINE_RE.match(line)
            if m:
                defines.append((m.group(1), m.group(2)))
                continue
            m = TYPEDEF_RE.match(line)
            if m:
                base, name = subst_khr(m.group(1)), m.group(2)
                if 'khronos' not in base:
                    typedefs.append((base, name))
    return defines, typedefs, protos


HEADER_PREAMBLE = """\
/*
 * %(outname)s -- OpenGL ES 1.1 / EGL for Windows Mobile (Windows CE, ARM)
 *
 * GENERATED FILE -- do not edit by hand.
 *   generator : handheld/project/winmobile/tools/gen_gl_headers.py
 *   driver    : %(dllname)s  (%(nexp)d exports)
 *
 * Signatures come from the Khronos reference headers; the platform layer is
 * written for CeGCC (arm-mingw32ce-gcc 4.4.0) targeting Windows CE 5.2:
 *
 *   - self-contained: does NOT include KHR/khrplatform.h.  On this target
 *     that header expands KHRONOS_APIENTRY to __stdcall (because _WIN32_WCE
 *     is not defined until a windows header has been seen) and typedefs
 *     khronos_int32_t to __int32, which GCC 4.4 does not accept.
 *   - %(entry)s is empty: ARM has one calling convention, so
 *     __stdcall / __cdecl are meaningless and only provoke warnings.
 *   - functions are declared plain extern, matching the by-name import
 *     libraries generated from the device DLLs (see the .def files in ../lib).
 *
 * Functions absent from the device driver are wrapped in #if 0 so that using
 * one fails at compile time with a clear message instead of failing to link.
 */
"""


def emit_gl(defines, typedefs, protos, exports, dllname, out):
    present = [p for p in protos if p[0] in exports]
    missing = [p for p in protos if p[0] not in exports]

    L = []
    L.append(HEADER_PREAMBLE % dict(outname='GLES/gl.h', dllname=dllname,
                                    nexp=len(exports), entry='GL_APIENTRY'))
    L.append('#ifndef __gles_gl_h_')
    L.append('#define __gles_gl_h_')
    L.append('')
    L.append('#ifdef __cplusplus')
    L.append('extern "C" {')
    L.append('#endif')
    L.append('')
    L.append('/* ---- platform ---- */')
    L.append('#ifndef GL_API')
    L.append('#define GL_API      extern')
    L.append('#endif')
    L.append('#ifndef GL_APIENTRY')
    L.append('#define GL_APIENTRY')
    L.append('#endif')
    L.append('/* Some sources spell these the desktop way. */')
    L.append('#ifndef GLAPI')
    L.append('#define GLAPI       GL_API')
    L.append('#endif')
    L.append('#ifndef APIENTRY')
    L.append('#define APIENTRY    GL_APIENTRY')
    L.append('#endif')
    L.append('')
    L.append('/* ---- types ---- */')
    for base, name in typedefs:
        L.append('typedef %-24s %s;' % (base, name))
    L.append('')
    L.append('/* ---- enums ---- */')
    for name, val in defines:
        L.append('#define %-40s %s' % (name, val))
    L.append('')
    L.append('/* ---- entry points (%d of %d present in %s) ---- */'
             % (len(present), len(protos), dllname))
    for name, ret, args in present:
        L.append('GL_API %s GL_APIENTRY %s (%s);' % (ret, name, args))
    L.append('')
    if missing:
        L.append('/*')
        L.append(' * Not exported by %s (%d):' % (dllname, len(missing)))
        for name, ret, args in missing:
            L.append(' *   %s' % name)
        L.append(' */')
        L.append('#if 0')
        for name, ret, args in missing:
            L.append('GL_API %s GL_APIENTRY %s (%s);' % (ret, name, args))
        L.append('#endif')
        L.append('')
    L.append('#ifdef __cplusplus')
    L.append('}')
    L.append('#endif')
    L.append('')
    L.append('#endif /* __gles_gl_h_ */')
    write(out, '\n'.join(L) + '\n')
    return present, missing


def emit_egl(defines, typedefs, protos, exports, dllname, out):
    present = [p for p in protos if p[0] in exports]
    missing = [p for p in protos if p[0] not in exports]

    L = []
    L.append(HEADER_PREAMBLE % dict(outname='EGL/egl.h', dllname=dllname,
                                    nexp=len(exports), entry='EGLAPIENTRY'))
    L.append('#ifndef __egl_h_')
    L.append('#define __egl_h_')
    L.append('')
    L.append('/* EGLNativeDisplayType is HDC on Windows CE: pass GetDC(hwnd)')
    L.append(' * to eglGetDisplay, and the HWND to eglCreateWindowSurface. */')
    L.append('#ifndef WIN32_LEAN_AND_MEAN')
    L.append('#define WIN32_LEAN_AND_MEAN 1')
    L.append('#endif')
    L.append('#include <windows.h>')
    L.append('')
    L.append('#ifdef __cplusplus')
    L.append('extern "C" {')
    L.append('#endif')
    L.append('')
    L.append('/* ---- platform ---- */')
    L.append('#ifndef EGLAPI')
    L.append('#define EGLAPI      extern')
    L.append('#endif')
    L.append('#ifndef EGLAPIENTRY')
    L.append('#define EGLAPIENTRY')
    L.append('#endif')
    L.append('#define EGLAPIENTRYP EGLAPIENTRY *')
    L.append('')
    L.append('typedef HDC     EGLNativeDisplayType;')
    L.append('typedef HBITMAP EGLNativePixmapType;')
    L.append('typedef HWND    EGLNativeWindowType;')
    L.append('')
    L.append('/* pre-EGL-1.3 spellings, still used by older code */')
    L.append('typedef EGLNativeDisplayType NativeDisplayType;')
    L.append('typedef EGLNativePixmapType  NativePixmapType;')
    L.append('typedef EGLNativeWindowType  NativeWindowType;')
    L.append('')
    L.append('/* EGLint lives in eglplatform.h upstream, and the return type of')
    L.append(' * eglGetProcAddress is declared with function-pointer syntax; both')
    L.append(' * are fixed by the EGL spec for a 32-bit target, so spell them out. */')
    L.append('typedef int EGLint;')
    L.append('typedef void (*__eglMustCastToProperFunctionPointerType)(void);')
    L.append('')
    L.append('/* ---- types ---- */')
    for base, name in typedefs:
        if name in ('EGLNativeDisplayType', 'EGLNativePixmapType',
                    'EGLNativeWindowType', 'NativeDisplayType',
                    'NativePixmapType', 'NativeWindowType'):
            continue
        L.append('typedef %-28s %s;' % (base, name))
    L.append('')
    L.append('/* ---- enums ---- */')
    for name, val in defines:
        L.append('#define %-36s %s' % (name, val))
    L.append('')
    L.append('/* ---- entry points (%d of %d present in %s) ---- */'
             % (len(present), len(protos), dllname))
    for name, ret, args in present:
        L.append('EGLAPI %s EGLAPIENTRY %s (%s);' % (ret, name, args))
    L.append('')
    if missing:
        L.append('/*')
        L.append(' * Not exported by %s (%d):' % (dllname, len(missing)))
        for name, ret, args in missing:
            L.append(' *   %s' % name)
        L.append(' */')
        L.append('#if 0')
        for name, ret, args in missing:
            L.append('EGLAPI %s EGLAPIENTRY %s (%s);' % (ret, name, args))
        L.append('#endif')
        L.append('')
    L.append('#ifdef __cplusplus')
    L.append('}')
    L.append('#endif')
    L.append('')
    L.append('#endif /* __egl_h_ */')
    write(out, '\n'.join(L) + '\n')
    return present, missing


def write(path, text):
    d = os.path.dirname(path)
    if d and not os.path.isdir(d):
        os.makedirs(d)
    with open(path, 'w') as f:
        f.write(text)


def arg(argv, flag, default=None):
    return argv[argv.index(flag) + 1] if flag in argv else default


def main():
    argv = sys.argv[1:]
    khronos = arg(argv, '--khronos')
    dll = arg(argv, '--dll')
    outdir = arg(argv, '--out')
    if not (khronos and dll and outdir):
        sys.stderr.write(__doc__)
        return 2

    with open(dll, 'rb') as f:
        _, exps = PE(f.read()).exports()
    exports = set(n for (n, o, fw) in exps if n)
    dllname = os.path.basename(dll)

    gd, gt, gp = parse(os.path.join(khronos, 'GLES', 'gl.h'), 'gl')
    ed, et, ep = parse(os.path.join(khronos, 'EGL', 'egl.h'), 'egl')

    gpres, gmiss = emit_gl(gd, gt, gp, exports, dllname,
                           os.path.join(outdir, 'GLES', 'gl.h'))
    epres, emiss = emit_egl(ed, et, ep, exports, dllname,
                            os.path.join(outdir, 'EGL', 'egl.h'))

    print('driver %s: %d exports' % (dllname, len(exports)))
    print('GLES/gl.h : %3d/%3d prototypes present, %d absent'
          % (len(gpres), len(gp), len(gmiss)))
    for n, r, a in gmiss:
        print('    absent: %s' % n)
    print('EGL/egl.h : %3d/%3d prototypes present, %d absent'
          % (len(epres), len(ep), len(emiss)))
    for n, r, a in emiss:
        print('    absent: %s' % n)
    return 0


if __name__ == '__main__':
    sys.exit(main())
