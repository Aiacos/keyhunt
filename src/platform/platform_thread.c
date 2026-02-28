/*
 * Platform abstraction layer - Thread operations implementation
 *
 * Cross-platform thread creation, joining, and detaching.
 * Provides unified API wrapping Windows CreateThread and POSIX pthread.
 */

#include "platform_thread.h"
#include "platform_types.h"

#if PLATFORM_WINDOWS
    #include <windows.h>
#else
    #include <pthread.h>
    #include <errno.h>
#endif

/**
 * Create and start a new thread.
 *
 * Windows implementation:
 *   - Uses CreateThread with NULL security, default stack size, immediate start
 *   - Returns thread handle in 'thread' parameter
 *   - Thread ID is discarded (not needed for our use case)
 *
 * POSIX implementation:
 *   - Uses pthread_create with default attributes (NULL)
 *   - Returns pthread_t handle in 'thread' parameter
 */
int platform_thread_create(platform_thread_t *thread, platform_thread_func_t func, void *arg)
{
    if (!thread || !func) {
        return -1;
    }

#if PLATFORM_WINDOWS
    DWORD thread_id;  /* Thread ID (unused but required by CreateThread) */
    *thread = CreateThread(
        NULL,           /* lpThreadAttributes: default security */
        0,              /* dwStackSize: default stack size */
        func,           /* lpStartAddress: thread function */
        arg,            /* lpParameter: user argument */
        0,              /* dwCreationFlags: start immediately */
        &thread_id      /* lpThreadId: receives thread ID */
    );

    if (*thread == NULL) {
        return -1;
    }
    return 0;
#else
    /* pthread_create returns 0 on success, error code on failure */
    int result = pthread_create(
        thread,         /* thread: output handle */
        NULL,           /* attr: default attributes */
        func,           /* start_routine: thread function */
        arg             /* arg: user argument */
    );

    return result;  /* 0 on success, errno on failure */
#endif
}

/**
 * Wait for a thread to terminate and retrieve its exit value.
 *
 * Windows implementation:
 *   - Uses WaitForSingleObject with INFINITE timeout (blocking wait)
 *   - Optionally retrieves exit code with GetExitCodeThread
 *   - Does NOT close the handle (caller must do that or use detach)
 *
 * POSIX implementation:
 *   - Uses pthread_join which blocks until thread terminates
 *   - Retrieves return value if retval is not NULL
 */
int platform_thread_join(platform_thread_t thread, platform_thread_return_t *retval)
{
#if PLATFORM_WINDOWS
    DWORD wait_result;
    DWORD exit_code;

    /* Wait for thread to terminate (blocking) */
    wait_result = WaitForSingleObject(thread, INFINITE);
    if (wait_result != WAIT_OBJECT_0) {
        CloseHandle(thread);
        return -1;
    }

    /* Retrieve exit code if requested */
    if (retval != NULL) {
        if (!GetExitCodeThread(thread, &exit_code)) {
            CloseHandle(thread);
            return -1;
        }
        *retval = exit_code;
    }

    CloseHandle(thread);
    return 0;
#else
    void *thread_retval;
    int result;

    /* pthread_join blocks until thread terminates and retrieves return value */
    result = pthread_join(thread, &thread_retval);
    if (result != 0) {
        return result;
    }

    /* Store return value if requested */
    if (retval != NULL) {
        *retval = thread_retval;
    }

    return 0;
#endif
}

/**
 * Detach a thread, allowing it to run independently.
 *
 * Windows implementation:
 *   - Closes the thread handle (decrements reference count)
 *   - Thread resources are cleaned up automatically when thread exits
 *
 * POSIX implementation:
 *   - Calls pthread_detach
 *   - Thread resources are reclaimed automatically when thread exits
 *
 * After detaching, do NOT call platform_thread_join() on this thread.
 */
int platform_thread_detach(platform_thread_t thread)
{
#if PLATFORM_WINDOWS
    /* CloseHandle decrements the thread handle reference count */
    if (!CloseHandle(thread)) {
        return -1;
    }
    return 0;
#else
    /* pthread_detach returns 0 on success, error code on failure */
    int result = pthread_detach(thread);
    return result;
#endif
}
