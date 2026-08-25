/*
 * WinSock2.h -- case-only forwarding shim for the CeGCC cross build.
 *
 * RakNet and a couple of our own headers spell this the way the Windows SDK
 * does; the CeGCC sysroot ships it lowercase, and Linux is case-sensitive.
 * The include below cannot recurse into this file for the same reason.
 */
#ifndef WINMOBILE_COMPAT_WINSOCK2_H__
#define WINMOBILE_COMPAT_WINSOCK2_H__

#include <winsock2.h>

#endif /* WINMOBILE_COMPAT_WINSOCK2_H__ */
