/*
 * Platform abstraction layer - Main header
 *
 * Single include point for all platform-specific functionality.
 * Provides unified API for threading, mutexes, and timing operations
 * across Windows and POSIX-compliant systems.
 *
 * Usage:
 *   #include "platform/platform.h"
 *
 * This header replaces platform-specific includes:
 *   - Windows: windows.h
 *   - POSIX: pthread.h, time.h
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stddef.h>

/* Core platform types */
#include "platform_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Thread operations
 * ============================================================================ */

/**
 * @brief Create and start a new thread.
 * @param thread Output pointer receiving the thread handle.
 * @param func Thread entry point function.
 * @param arg User argument passed to the thread function.
 * @return 0 on success, non-zero error code on failure.
 */
int platform_thread_create(platform_thread_t *thread, platform_thread_func_t func, void *arg);

/**
 * @brief Wait for a thread to terminate and retrieve its exit value.
 * @param thread Thread handle to wait for.
 * @param retval Output pointer receiving the thread's return value (optional, may be NULL).
 * @return 0 on success, non-zero error code on failure.
 */
int platform_thread_join(platform_thread_t thread, platform_thread_return_t *retval);

/**
 * @brief Detach a thread, allowing it to run independently.
 * @param thread Thread handle to detach.
 * @return 0 on success, non-zero error code on failure.
 *
 * After detaching, the thread's resources are automatically cleaned up on exit.
 * Do not call platform_thread_join() on a detached thread.
 */
int platform_thread_detach(platform_thread_t thread);

/* ============================================================================
 * Mutex operations
 * ============================================================================ */

/**
 * @brief Initialize a mutex.
 * @param mutex Pointer to the mutex to initialize.
 * @return 0 on success, non-zero error code on failure.
 */
int platform_mutex_init(platform_mutex_t *mutex);

/**
 * @brief Acquire a mutex lock (blocking).
 * @param mutex Pointer to the mutex to lock.
 * @return 0 on success, non-zero error code on failure.
 */
int platform_mutex_lock(platform_mutex_t *mutex);

/**
 * @brief Release a mutex lock.
 * @param mutex Pointer to the mutex to unlock.
 * @return 0 on success, non-zero error code on failure.
 */
int platform_mutex_unlock(platform_mutex_t *mutex);

/**
 * @brief Destroy a mutex and release its resources.
 * @param mutex Pointer to the mutex to destroy.
 * @return 0 on success, non-zero error code on failure.
 *
 * Do not destroy a locked mutex. Behavior is undefined.
 */
int platform_mutex_destroy(platform_mutex_t *mutex);

/* ============================================================================
 * Time operations
 * ============================================================================ */

/**
 * @brief Get current monotonic time in nanoseconds.
 * @return Monotonic timestamp in nanoseconds.
 *
 * Uses CLOCK_MONOTONIC_RAW on Linux (not affected by NTP adjustments),
 * QueryPerformanceCounter on Windows.
 *
 * Suitable for performance measurements and elapsed time calculations.
 * Not suitable for wall-clock time or absolute timestamps.
 */
uint64_t platform_time_now_ns(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */
