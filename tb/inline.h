#pragma once

// #define NO_INLINE_INDEX

#if defined(NO_INLINE_INDEX) || !defined(NDEBUG)
	#define __FORCE_INLINE __attribute__((noinline))
#else
	#define __FORCE_INLINE __attribute__((always_inline)) inline
#endif
