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

/* POSIX compatibility layer */
#include "platform_compat.h"

/* Directory operations */
#include "platform_dir.h"

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

/* ============================================================================
 * Terminal operations
 * ============================================================================ */

/**
 * @brief Get terminal width in columns.
 * @return Terminal width in columns, or 80 if detection fails.
 *
 * Uses GetConsoleScreenBufferInfo on Windows, ioctl(TIOCGWINSZ) on POSIX.
 *
 * Returns 80 as a safe default if terminal size cannot be detected or
 * output is redirected to a pipe/file.
 *
 * Suitable for responsive output formatting that adapts to terminal size.
 */
int platform_terminal_width(void);

/* ============================================================================
 * Directory operations
 * ============================================================================ */

/**
 * @brief Create a directory.
 * @param path Path to the directory to create.
 * @return 0 on success, -1 on failure.
 */
int platform_dir_create(const char *path);

/**
 * @brief Remove a directory.
 * @param path Path to the directory to remove (must be empty).
 * @return 0 on success, -1 on failure.
 */
int platform_dir_remove(const char *path);

/**
 * @brief Check if a directory exists.
 * @param path Path to check.
 * @return 1 if directory exists, 0 otherwise.
 */
int platform_dir_exists(const char *path);

/**
 * @brief Open a directory for iteration.
 * @param path Path to the directory to open.
 * @return Directory handle on success, NULL on failure.
 *
 * Must be closed with platform_dir_close() after use.
 */
platform_dir_handle_t platform_dir_open(const char *path);

/**
 * @brief Read next directory entry.
 * @param handle Directory handle from platform_dir_open().
 * @param entry Output pointer receiving directory entry information.
 * @return 1 if entry was read, 0 if end of directory, -1 on error.
 */
int platform_dir_read(platform_dir_handle_t handle, platform_dir_entry_t *entry);

/**
 * @brief Close directory handle.
 * @param handle Directory handle to close.
 * @return 0 on success, -1 on failure.
 */
int platform_dir_close(platform_dir_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */
