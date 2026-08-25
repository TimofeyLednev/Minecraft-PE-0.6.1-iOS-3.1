#ifndef LOG_H__
#define LOG_H__

#ifdef __cplusplus
	#include <cstdio>
#else
	#include <stdio.h>
#endif

#define __LOG_PUBLISH(...) do { __VA_ARGS__; } while(0)

#ifdef ANDROID
	#include <android/log.h>
	#ifdef ANDROID_PUBLISH
		#define LOGV(fmt, ...) __LOG_PUBLISH(__VA_ARGS__)
		#define LOGI(fmt, ...) __LOG_PUBLISH(__VA_ARGS__)
		#define LOGW(fmt, ...) __LOG_PUBLISH(__VA_ARGS__)
		#define LOGE(fmt, ...) __LOG_PUBLISH(__VA_ARGS__)
	#else
		// @todo @fix; Obiously the tag shouldn't be hardcoded in here..
		#define LOGV(...) ((void)__android_log_print( ANDROID_LOG_VERBOSE, "MinecraftPE", __VA_ARGS__ ))
		#define LOGI(...) ((void)__android_log_print( ANDROID_LOG_INFO,  "MinecraftPE", __VA_ARGS__ ))
		#define LOGW(...) ((void)__android_log_print( ANDROID_LOG_WARN,  "MinecraftPE", __VA_ARGS__ ))
		#define LOGE(...) ((void)__android_log_print( ANDROID_LOG_ERROR, "MinecraftPE", __VA_ARGS__ ))
		#define printf LOGI
	#endif
#else
#ifdef WINMOBILE
	// A GUI process on Windows CE has no stdout. printf() still links and still
	// succeeds -- it just writes nowhere -- so the generic branch below would
	// silently discard every line, including the ones explaining a failed boot.
	// wce_logPrintf writes to <exeDir>\minecraft.log and mirrors to the
	// debugger. Included rather than declared so this works even in a TU built
	// without the Makefile's -include wince_compat.h.
	//
	// Tested BEFORE PUBLISH, unlike every other platform here, and deliberately:
	// `make release` passes -DPUBLISH, and with the usual ordering that turns
	// LOGI/LOGW/LOGE into do{;}while(0) -- which on a device with no debugger,
	// no stdout and no crash dialog leaves nothing at all to diagnose from. The
	// file is the only channel this port has, so it stays on in release. It
	// costs one fprintf+fflush per line and nothing on the frame path, since
	// nothing in the render loop logs.
	#include "wince_compat.h"
	#define LOGV(...) (wce_logPrintf(__VA_ARGS__))
	#define LOGI(...) (wce_logPrintf(__VA_ARGS__))
	#define LOGW(...) (wce_logPrintf(__VA_ARGS__))
	#define LOGE(...) (wce_logPrintf(__VA_ARGS__))
#elif defined(PUBLISH)
    #define LOGV(fmt, ...) __LOG_PUBLISH(__VA_ARGS__)
    #define LOGI(fmt, ...) __LOG_PUBLISH(__VA_ARGS__)
    #define LOGW(fmt, ...) __LOG_PUBLISH(__VA_ARGS__)
    #define LOGE(fmt, ...) __LOG_PUBLISH(__VA_ARGS__)
#else
	#define LOGV(...) (printf(__VA_ARGS__))
	#define LOGI(...) (printf(__VA_ARGS__))
	#define LOGW(...) (printf(__VA_ARGS__))
	#define LOGE(...) (printf(__VA_ARGS__))
#endif
#endif

#ifdef _DEBUG
	#define LOGVV LOGV
#else
	#define LOGVV(fmt, ...) __LOG_PUBLISH(__VA_ARGS__)
#endif

#endif /*LOG_H__*/
