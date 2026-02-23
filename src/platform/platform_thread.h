/*
 * Platform abstraction layer - Thread operations
 *
 * Provides unified thread API across Windows and POSIX systems.
 * Wraps CreateThread/pthread_create, thread joining, and detaching.
 *
 * This header is included by platform.h and should not be included directly.
 */

#ifndef PLATFORM_THREAD_H
#define PLATFORM_THREAD_H

#include "platform_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create and start a new thread.
 * @param thread Output pointer receiving the thread handle.
 * @param func Thread entry point function.
 * @param arg User argument passed to the thread function.
 * @return 0 on success, non-zero error code on failure.
 *
 * Windows: Wraps CreateThread with default stack size and immediate start.
 * POSIX: Wraps pthread_create with default attributes.
 */
int platform_thread_create(platform_thread_t *thread, platform_thread_func_t func, void *arg);

/**
 * @brief Wait for a thread to terminate and retrieve its exit value.
 * @param thread Thread handle to wait for.
 * @param retval Output pointer receiving the thread's return value (optional, may be NULL).
 * @return 0 on success, non-zero error code on failure.
 *
 * Windows: Wraps WaitForSingleObject(INFINITE) + GetExitCodeThread.
 * POSIX: Wraps pthread_join.
 *
 * Blocks until the specified thread terminates.
 * Do not call on detached threads.
 */
int platform_thread_join(platform_thread_t thread, platform_thread_return_t *retval);

/**
 * @brief Detach a thread, allowing it to run independently.
 * @param thread Thread handle to detach.
 * @return 0 on success, non-zero error code on failure.
 *
 * Windows: Closes the thread handle (decrements ref count).
 * POSIX: Calls pthread_detach.
 *
 * After detaching, the thread's resources are automatically cleaned up on exit.
 * Do not call platform_thread_join() on a detached thread.
 */
int platform_thread_detach(platform_thread_t thread);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_THREAD_H */
