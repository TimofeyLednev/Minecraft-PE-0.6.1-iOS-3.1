/*
 *  CThread.cpp
 *  oxeye
 *
 *  Created by aegzorz on 2007-02-09.
 *  Copyright 2007 Mojang AB. All rights reserved.
 *
 */

#include "CThread.h"



	CThread::CThread( pthread_fn threadFunc, void* threadParam )
	{
	#ifdef WIN32
		mp_threadFunc = (LPTHREAD_START_ROUTINE) threadFunc;

	#ifdef WINMOBILE
		/* An explicit stack, unlike every other platform here, because on
		 * Windows CE a thread's stack is *reserved inside the process's 32 MB
		 * slot* -- the same slot the level's 21 MB of chunks has to fit in.
		 * dwStackSize == 0 means "use the value in the module header", which is
		 * 1 MB after the Makefile's -Wl,--stack, and this thread does not need
		 * anything like that: it runs Minecraft::prepareLevel, whose deepest
		 * frames are the level generator's at under 3 KB each and nowhere near
		 * recursive enough to need 256 KB.  CE commits stack pages on demand, so
		 * the number below costs address space and not memory.
		 *
		 * (CE honours dwStackSize from 5.0 onwards; on 4.x it was ignored and
		 * the header value used, which is the harmless outcome anyway.) */
		const DWORD stackSize = 256 * 1024;
	#else
		const DWORD stackSize = 0;
	#endif

		m_threadHandle = CreateThread(
			NULL,				// pointer to security attributes
			stackSize,          // initial thread stack size
			mp_threadFunc,		// pointer to thread function
			threadParam,        // argument for new thread
			NULL,               // creation flags
			&m_threadID        // pointer to receive thread ID
		);
	#endif
	#if defined(LINUX) || defined(ANDROID) || defined(__APPLE__) || defined(POSIX)
		mp_threadFunc = (pthread_fn)threadFunc;

		pthread_attr_init(&m_attributes);
		pthread_attr_setdetachstate( &m_attributes, PTHREAD_CREATE_DETACHED );
		/*int error =*/ pthread_create(&m_thread, &m_attributes, mp_threadFunc,threadParam);
	#endif
	#ifdef MACOSX
		mp_threadFunc = (TaskProc) threadFunc;
	
		MPCreateTask(
			mp_threadFunc,		// pointer to thread function
			threadParam,		// argument for new thread
			0,					// initial thread stack size
			NULL,				// queue id
			NULL,				// termination param 1
			NULL,				// termination param 2
			0,					// task options
			&m_threadID			// pointer to receive task ID
		);
	#endif
	}
	
	void CThread::sleep( const unsigned int millis )
	{
		#ifdef WIN32
			Sleep( millis );
		#endif
		#if defined(LINUX) || defined(ANDROID) || defined(__APPLE__) || defined(POSIX)
			usleep(millis * 1000);
		#endif
	}

	CThread::~CThread()
	{
	#ifdef WIN32
		TerminateThread(m_threadHandle, 0);
	#endif
	#if defined(LINUX) || defined(ANDROID) || defined(__APPLE__) || defined(POSIX)
		pthread_join(m_thread, NULL);
		pthread_attr_destroy(&m_attributes);
	#endif
	}


