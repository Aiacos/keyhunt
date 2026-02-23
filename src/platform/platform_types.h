/*
 * Platform abstraction layer - Type definitions
 *
 * This file defines opaque types for platform-specific handles (threads, mutexes)
 * to enable clean cross-platform code without #ifdef blocks scattered throughout.
 *
 * Supports: Windows (64-bit), Linux, macOS (POSIX-compatible)
 */

#ifndef PLATFORM_TYPES_H
#define PLATFORM_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Platform detection */
#if defined(_WIN64) && !defined(__CYGWIN__)
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_POSIX 0
#else
    #define PLATFORM_WINDOWS 0
    #define PLATFORM_POSIX 1
#endif

/* Include platform-specific headers */
#if PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <pthread.h>
    #include <time.h>
#endif

/**
 * @brief Opaque thread handle type.
 *
 * Wraps HANDLE on Windows, pthread_t on POSIX.
 */
#if PLATFORM_WINDOWS
    typedef HANDLE platform_thread_t;
#else
    typedef pthread_t platform_thread_t;
#endif

/**
 * @brief Opaque mutex handle type.
 *
 * Wraps HANDLE on Windows, pthread_mutex_t on POSIX.
 */
#if PLATFORM_WINDOWS
    typedef HANDLE platform_mutex_t;
#else
    typedef pthread_mutex_t platform_mutex_t;
#endif

/**
 * @brief Thread function signature.
 *
 * Unified signature for thread entry points across platforms.
 * @param arg User-provided argument passed to the thread.
 * @return Thread exit value (platform-specific interpretation).
 */
#if PLATFORM_WINDOWS
    typedef DWORD (WINAPI *platform_thread_func_t)(void *arg);
#else
    typedef void* (*platform_thread_func_t)(void *arg);
#endif

/**
 * @brief Thread return value type.
 *
 * Wraps DWORD on Windows, void* on POSIX.
 */
#if PLATFORM_WINDOWS
    typedef DWORD platform_thread_return_t;
#else
    typedef void* platform_thread_return_t;
#endif

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_TYPES_H */
