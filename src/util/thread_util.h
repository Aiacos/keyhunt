/*
 * thread_util.h - Thread utility functions extracted from keyhunt.cpp
 *
 * Provides:
 * - Aligned memory allocation (aligned_calloc/aligned_free)
 * - Thread-safe random number generation (thread_rand/thread_rand_n)
 * - Cross-platform sleep (sleep_ms)
 *
 * These functions have no dependency on keyhunt.cpp globals.
 */

#ifndef KEYHUNT_THREAD_UTIL_H
#define KEYHUNT_THREAD_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * Aligned Memory Allocation
 * ============================================================================ */

/*
 * Allocate zero-initialized memory with specified alignment.
 * Uses _aligned_malloc on Windows, aligned_alloc on POSIX.
 *
 * Parameters:
 *   alignment - Required alignment (must be power of 2)
 *   count     - Number of elements
 *   elem_size - Size of each element in bytes
 *
 * Returns:
 *   Pointer to aligned, zero-initialized memory, or NULL on failure
 */
void* aligned_calloc(size_t alignment, size_t count, size_t elem_size);

/*
 * Free memory allocated by aligned_calloc.
 * Uses _aligned_free on Windows, free on POSIX.
 */
void aligned_free(void *ptr);

/* ============================================================================
 * Thread-Safe Random Number Generation
 * ============================================================================ */

/*
 * Thread-safe replacement for rand().
 * Uses thread-local LCG state, auto-initializes on first call.
 *
 * Returns:
 *   Random integer in range [0, 32767]
 */
int thread_rand(void);

/*
 * Thread-safe random in range [0, n).
 *
 * Parameters:
 *   n - Upper bound (exclusive). If n <= 0, returns 0.
 *
 * Returns:
 *   Random integer in range [0, n)
 */
int thread_rand_n(int n);

/* ============================================================================
 * Cross-Platform Sleep
 * ============================================================================ */

/*
 * Sleep for the specified number of milliseconds.
 * Uses Sleep() on Windows, nanosleep() on POSIX.
 *
 * Parameters:
 *   milliseconds - Duration to sleep
 */
void sleep_ms(int milliseconds);

#endif /* KEYHUNT_THREAD_UTIL_H */
