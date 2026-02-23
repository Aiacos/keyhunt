/*
 * Platform abstraction layer - Mutex operations
 *
 * Provides unified mutex API across Windows and POSIX systems.
 * Wraps CreateMutex/pthread_mutex for thread synchronization.
 *
 * This header is included by platform.h and should not be included directly.
 */

#ifndef PLATFORM_MUTEX_H
#define PLATFORM_MUTEX_H

#include "platform_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize a mutex.
 * @param mutex Pointer to the mutex to initialize.
 * @return 0 on success, non-zero error code on failure.
 *
 * Windows: Creates a new mutex object with CreateMutex (unnamed, not initially owned).
 * POSIX: Calls pthread_mutex_init with default attributes (NULL).
 *
 * Must call platform_mutex_destroy() when done to release resources.
 */
int platform_mutex_init(platform_mutex_t *mutex);

/**
 * @brief Acquire a mutex lock (blocking).
 * @param mutex Pointer to the mutex to lock.
 * @return 0 on success, non-zero error code on failure.
 *
 * Windows: Wraps WaitForSingleObject with INFINITE timeout.
 * POSIX: Wraps pthread_mutex_lock.
 *
 * Blocks until the mutex becomes available.
 * Always pair with platform_mutex_unlock().
 */
int platform_mutex_lock(platform_mutex_t *mutex);

/**
 * @brief Release a mutex lock.
 * @param mutex Pointer to the mutex to unlock.
 * @return 0 on success, non-zero error code on failure.
 *
 * Windows: Wraps ReleaseMutex.
 * POSIX: Wraps pthread_mutex_unlock.
 *
 * Must be called by the same thread that locked the mutex.
 */
int platform_mutex_unlock(platform_mutex_t *mutex);

/**
 * @brief Destroy a mutex and release its resources.
 * @param mutex Pointer to the mutex to destroy.
 * @return 0 on success, non-zero error code on failure.
 *
 * Windows: Calls CloseHandle to release the mutex object.
 * POSIX: Calls pthread_mutex_destroy.
 *
 * Do not destroy a locked mutex. Behavior is undefined.
 * After destruction, the mutex must be re-initialized before use.
 */
int platform_mutex_destroy(platform_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_MUTEX_H */
