/*
 * Platform abstraction layer - Time operations implementation
 *
 * Cross-platform monotonic clock implementation for performance measurements.
 * Provides unified API wrapping Windows QueryPerformanceCounter and POSIX clock_gettime.
 */

#include "platform_time.h"
#include "platform_types.h"

#if PLATFORM_WINDOWS
    #include <windows.h>
#else
    #include <time.h>
    #include <stdint.h>
#endif

/**
 * Get current monotonic time in nanoseconds.
 *
 * Windows implementation:
 *   - Uses QueryPerformanceCounter to get high-resolution timestamp
 *   - Uses QueryPerformanceFrequency to get timer frequency (ticks per second)
 *   - Converts ticks to nanoseconds: (ticks * 1000000000) / frequency
 *   - Frequency is cached on first call for performance
 *
 * POSIX implementation:
 *   - Uses clock_gettime with CLOCK_MONOTONIC_RAW (preferred, not affected by NTP)
 *   - Falls back to CLOCK_MONOTONIC if CLOCK_MONOTONIC_RAW is not available
 *   - Converts timespec (seconds + nanoseconds) to total nanoseconds
 *   - Provides nanosecond resolution (1e-9 seconds)
 *
 * The returned value is only meaningful for calculating time differences.
 * It does not represent wall-clock time or any absolute timestamp.
 */
uint64_t platform_time_now_ns(void)
{
#if PLATFORM_WINDOWS
    static LARGE_INTEGER frequency = {0};
    LARGE_INTEGER counter;

    /* Cache the frequency on first call (it never changes) */
    if (frequency.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&frequency)) {
            /* Fallback if QueryPerformanceFrequency fails (should never happen on modern Windows) */
            return 0;
        }
    }

    /* Get current counter value */
    if (!QueryPerformanceCounter(&counter)) {
        return 0;
    }

    /*
     * Convert counter ticks to nanoseconds:
     * nanoseconds = (ticks * 1,000,000,000) / frequency
     *
     * To avoid overflow on 32-bit systems and improve precision:
     * 1. Convert to microseconds first: (ticks * 1,000,000) / frequency
     * 2. Then multiply by 1,000 to get nanoseconds
     */
    uint64_t microseconds = (counter.QuadPart * 1000000ULL) / frequency.QuadPart;
    return microseconds * 1000ULL;

#else
    struct timespec ts;

#ifdef CLOCK_MONOTONIC_RAW
    /* CLOCK_MONOTONIC_RAW is preferred: not subject to NTP adjustments */
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
#endif

    /* Fallback to CLOCK_MONOTONIC (subject to NTP adjustments but still monotonic) */
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }

    /* Ultimate fallback if both fail (should never happen on modern POSIX systems) */
    return 0;
#endif
}
