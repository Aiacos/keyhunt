/*
 * Platform abstraction layer - Mutex operations implementation
 *
 * Cross-platform mutex initialization, locking, unlocking, and destruction.
 * Provides unified API wrapping Windows mutexes and POSIX pthread_mutex.
 */

#include "platform_mutex.h"
#include "platform_types.h"

#if PLATFORM_WINDOWS
    #include <windows.h>
#else
    #include <pthread.h>
    #include <errno.h>
#endif

/**
 * Initialize a mutex.
 *
 * Windows implementation:
 *   - Uses CreateMutex with NULL security, not initially owned, unnamed
 *   - Returns a HANDLE that can be used for WaitForSingleObject
 *   - Must be freed with CloseHandle when done
 *
 * POSIX implementation:
 *   - Uses pthread_mutex_init with default attributes (NULL)
 *   - Initializes the pthread_mutex_t structure in-place
 *   - Must be destroyed with pthread_mutex_destroy when done
 */
int platform_mutex_init(platform_mutex_t *mutex)
{
    if (!mutex) {
        return -1;
    }

#if PLATFORM_WINDOWS
    /* CreateMutex parameters:
     *   lpMutexAttributes: NULL (default security)
     *   bInitialOwner: FALSE (not initially owned by creating thread)
     *   lpName: NULL (unnamed mutex)
     */
    *mutex = CreateMutex(NULL, FALSE, NULL);

    if (*mutex == NULL) {
        return -1;
    }
    return 0;
#else
    /* pthread_mutex_init returns 0 on success, error code on failure */
    int result = pthread_mutex_init(mutex, NULL);
    return result;  /* 0 on success, errno on failure */
#endif
}

/**
 * Acquire a mutex lock (blocking).
 *
 * Windows implementation:
 *   - Uses WaitForSingleObject with INFINITE timeout
 *   - Blocks until mutex is signaled (available)
 *   - Automatically acquires ownership
 *
 * POSIX implementation:
 *   - Uses pthread_mutex_lock which blocks until mutex is available
 *   - Automatically acquires the lock
 */
int platform_mutex_lock(platform_mutex_t *mutex)
{
    if (!mutex) {
        return -1;
    }

#if PLATFORM_WINDOWS
    DWORD wait_result;

    /* Wait for mutex to become available (blocking) */
    wait_result = WaitForSingleObject(*mutex, INFINITE);

    if (wait_result != WAIT_OBJECT_0) {
        return -1;
    }
    return 0;
#else
    /* pthread_mutex_lock returns 0 on success, error code on failure */
    int result = pthread_mutex_lock(mutex);
    return result;
#endif
}

/**
 * Release a mutex lock.
 *
 * Windows implementation:
 *   - Uses ReleaseMutex to release ownership
 *   - Signals the mutex so waiting threads can acquire it
 *
 * POSIX implementation:
 *   - Uses pthread_mutex_unlock to release the lock
 *   - Allows other threads to acquire the mutex
 */
int platform_mutex_unlock(platform_mutex_t *mutex)
{
    if (!mutex) {
        return -1;
    }

#if PLATFORM_WINDOWS
    /* ReleaseMutex returns non-zero on success, 0 on failure */
    if (!ReleaseMutex(*mutex)) {
        return -1;
    }
    return 0;
#else
    /* pthread_mutex_unlock returns 0 on success, error code on failure */
    int result = pthread_mutex_unlock(mutex);
    return result;
#endif
}

/**
 * Destroy a mutex and release its resources.
 *
 * Windows implementation:
 *   - Uses CloseHandle to release the mutex object
 *   - Decrements the reference count and frees resources when it reaches zero
 *
 * POSIX implementation:
 *   - Uses pthread_mutex_destroy to release resources
 *   - The mutex must not be locked when destroyed
 *
 * After destruction, the mutex must be re-initialized before use.
 */
int platform_mutex_destroy(platform_mutex_t *mutex)
{
    if (!mutex) {
        return -1;
    }

#if PLATFORM_WINDOWS
    /* CloseHandle returns non-zero on success, 0 on failure */
    if (!CloseHandle(*mutex)) {
        return -1;
    }
    return 0;
#else
    /* pthread_mutex_destroy returns 0 on success, error code on failure */
    int result = pthread_mutex_destroy(mutex);
    return result;
#endif
}
