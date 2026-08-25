/*
 * errno.h -- replacement for the CeGCC sysroot's non-functional <errno.h>.
 *
 * The sysroot ships a header that reads
 *
 *     #ifdef __COREDLL__
 *     # include_next <errno.h>
 *
 * and __COREDLL__ *is* predefined by arm-mingw32ce-gcc, so every
 * #include <errno.h> becomes "error: no include path in which to search for
 * errno.h".  The header it is deferring to was meant to come from a CE SDK that
 * this toolchain does not ship.  (conio.h, direct.h, locale.h, signal.h and
 * varargs.h are broken the same way; float.h escapes because GCC's own copy is
 * found first.)
 *
 * There is no errno on Windows CE to forward to: coredll has no _errno, no
 * __errno_location, nothing.  So this provides a real one.  wce_compat.cpp holds
 * the storage; see the note there about it being process-wide rather than
 * per-thread.
 *
 * Deliberately *not* mapped onto GetLastError().  The two numbering schemes are
 * unrelated -- ERROR_FILE_NOT_FOUND is 2 and so is ENOENT, but that coincidence
 * stops immediately -- and GetLastError() is clobbered by every intervening API
 * call, so code that sets errno and reads it two lines later would see whatever
 * the C library did in between.  A private int does what the callers actually
 * expect.
 */
#ifndef WINMOBILE_COMPAT_ERRNO_H__
#define WINMOBILE_COMPAT_ERRNO_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Same shape as mingw's: the lvalue form is required, because rdlmalloc.c and
   friends assign to errno (MALLOC_FAILURE_ACTION is "errno = ENOMEM"). */
int* _errno(void);

#ifdef __cplusplus
}
#endif

#define errno (*_errno())

/* The MSVC/mingw values, so that a number that escapes into a log means the
   same thing it would on the desktop build. */
#define EPERM		1
#define ENOENT		2
#define ESRCH		3
#define EINTR		4
#define EIO		5
#define ENXIO		6
#define E2BIG		7
#define ENOEXEC		8
#define EBADF		9
#define ECHILD		10
#define EAGAIN		11
#define ENOMEM		12
#define EACCES		13
#define EFAULT		14
#define EBUSY		16
#define EEXIST		17
#define EXDEV		18
#define ENODEV		19
#define ENOTDIR		20
#define EISDIR		21
#define EINVAL		22
#define ENFILE		23
#define EMFILE		24
#define ENOTTY		25
#define EFBIG		27
#define ENOSPC		28
#define ESPIPE		29
#define EROFS		30
#define EMLINK		31
#define EPIPE		32
#define EDOM		33
#define ERANGE		34
#define EDEADLK		36
#define ENAMETOOLONG	38
#define ENOLCK		39
#define ENOSYS		40
#define ENOTEMPTY	41
#define EILSEQ		42

#define EDEADLOCK	EDEADLK

/* Guarded: winsock2.h does not define these in this SDK, but a future header
   might, and a redefinition with a different value is a hard error. */
#ifndef EWOULDBLOCK
#define EWOULDBLOCK	140
#endif
#ifndef EINPROGRESS
#define EINPROGRESS	112
#endif

#endif /* WINMOBILE_COMPAT_ERRNO_H__ */
