/**
    Entry point for cross compilation.

    This is ugly, yes. But it will change in the "half near" future
    to a more correct system of solving the cross-platform entry
    point "problem" */

#define _SECURE_SCL 0

#if defined(WIN32) && !defined(WINMOBILE)
	#include "vld.h"
#endif

#include "platform/log.h"

#ifdef WIN32
#endif
#ifdef ANDROID
#endif


#include "NinecraftApp.h"
#define MAIN_CLASS NinecraftApp

#ifdef _WIN32
	// WINMOBILE must be tested first: CeGCC predefines both WIN32 and _WIN32,
	// so the desktop entry point is the one that would otherwise get compiled.
	#ifdef WINMOBILE
		#include "main_winmobile.h"
	#else
		#include "main_win32.h"
	#endif
#endif
#ifdef ANDROID
    #ifdef PRE_ANDROID23
        #include "main_android_java.h"
    #else
        #include "main_android.h"
    #endif
#endif

#ifdef RPI
    #include "main_rpi.h"
#endif


