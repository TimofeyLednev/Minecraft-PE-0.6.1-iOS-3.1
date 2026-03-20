#include "LocklessTypes.h"

#if defined(__APPLE__)
// iOS ARMv6 doesn't reliably provide GCC __sync_fetch_and_add_* runtime symbols.
// Use Apple's OSAtomic operations instead.
#include <libkern/OSAtomic.h>
#endif

using namespace RakNet;

LocklessUint32_t::LocklessUint32_t()
{
	value=0;
}
LocklessUint32_t::LocklessUint32_t(uint32_t initial)
{
	value=initial;
}
uint32_t LocklessUint32_t::Increment(void)
{
#ifdef _WIN32
	return (uint32_t) InterlockedIncrement(&value);
#elif defined(ANDROID) || defined(__S3E__)
	uint32_t v;
	mutex.Lock();
	++value;
	v=value;
	mutex.Unlock();
	return v;
#elif defined(__APPLE__)
	// Returns the value AFTER increment.
	return (uint32_t)OSAtomicIncrement32Barrier((volatile int32_t*)&value);
#else
	return __sync_fetch_and_add (&value, (uint32_t) 1);
#endif
}
uint32_t LocklessUint32_t::Decrement(void)
{
#ifdef _WIN32
	return (uint32_t) InterlockedDecrement(&value);
#elif defined(ANDROID) || defined(__S3E__)
	uint32_t v;
	mutex.Lock();
	--value;
	v=value;
	mutex.Unlock();
	return v;
#elif defined(__APPLE__)
	// Returns the value AFTER decrement.
	return (uint32_t)OSAtomicDecrement32Barrier((volatile int32_t*)&value);
#else
	return __sync_fetch_and_add (&value, (uint32_t) -1);
#endif
}
