/*
 * Platform abstraction layer - Time operations
 *
 * Provides unified monotonic clock API across Windows and POSIX systems.
 * Wraps QueryPerformanceCounter/clock_gettime for performance measurements.
 *
 * This header is included by platform.h and should not be included directly.
 */

#ifndef PLATFORM_TIME_H
#define PLATFORM_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get current monotonic time in nanoseconds.
 * @return Monotonic timestamp in nanoseconds (uint64_t).
 *
 * Windows: Wraps QueryPerformanceCounter + QueryPerformanceFrequency.
 *          Converts high-resolution performance counter to nanoseconds.
 * POSIX: Wraps clock_gettime with CLOCK_MONOTONIC_RAW (preferred) or CLOCK_MONOTONIC.
 *        Provides nanosecond resolution timestamp.
 *
 * Monotonic clocks are not affected by system time changes (NTP, manual adjustments).
 * Suitable for measuring elapsed time, performance profiling, and timeouts.
 * NOT suitable for wall-clock time or absolute timestamps.
 *
 * The returned value is only meaningful for calculating time differences:
 *   uint64_t start = platform_time_now_ns();
 *   // ... do work ...
 *   uint64_t elapsed = platform_time_now_ns() - start;
 */
uint64_t platform_time_now_ns(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_TIME_H */
