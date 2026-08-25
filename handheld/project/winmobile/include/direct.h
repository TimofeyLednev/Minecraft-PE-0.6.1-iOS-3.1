/*
 * direct.h -- replacement for the CeGCC sysroot's non-functional <direct.h>.
 *
 * Broken in exactly the way <errno.h> is: an #include_next guarded by
 * __COREDLL__, which is predefined, deferring to a CE SDK header the toolchain
 * does not ship.  See errno.h in this directory for the full story.
 *
 * Nothing is lost by forwarding to <io.h>: on this toolchain io.h is where
 * _mkdir, _rmdir, _chdir, _getcwd, _access and _unlink are actually declared,
 * and the sysroot's own direct.h did nothing but include it and add the
 * drive-letter calls (_getdrive, _chdrive, _getdiskfree) -- which are
 * meaningless on CE, where the whole device is one namespace rooted at '\'.
 *
 * Note that only _mkdir, _access, _unlink, _stat and _findfirst are *defined*
 * in libmingwex; _rmdir, _chdir and _getcwd are declared but resolve to
 * nothing, so calling them is a link error rather than a compile error.  That
 * is correct behaviour for CE: there is no current working directory to change
 * or query.
 */
#ifndef WINMOBILE_COMPAT_DIRECT_H__
#define WINMOBILE_COMPAT_DIRECT_H__

#include <io.h>

#endif /* WINMOBILE_COMPAT_DIRECT_H__ */
