/*
 * Platform abstraction layer - POSIX Compatibility (platform_compat.h)
 *
 * This file provides Windows implementations of common POSIX functions
 * to enable clean cross-platform code without #ifdef blocks scattered throughout.
 *
 * Supports: Windows (64-bit), Linux, macOS (POSIX-compatible)
 */

#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#include "platform_types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Case-insensitive string comparison.
 *
 * Compares two strings ignoring case differences.
 * @param s1 First string to compare.
 * @param s2 Second string to compare.
 * @return Integer less than, equal to, or greater than zero if s1 is found,
 *         respectively, to be less than, to match, or be greater than s2.
 */
#if PLATFORM_WINDOWS
    int strcasecmp(const char *s1, const char *s2);
#else
    #include <strings.h>  /* strcasecmp on POSIX */
#endif

/**
 * @brief Case-insensitive string comparison with length limit.
 *
 * Compares up to n characters of two strings ignoring case differences.
 * @param s1 First string to compare.
 * @param s2 Second string to compare.
 * @param n Maximum number of characters to compare.
 * @return Integer less than, equal to, or greater than zero if s1 is found,
 *         respectively, to be less than, to match, or be greater than s2.
 */
#if PLATFORM_WINDOWS
    int strncasecmp(const char *s1, const char *s2, size_t n);
#else
    #include <strings.h>  /* strncasecmp on POSIX */
#endif

/**
 * @brief Close a file descriptor.
 *
 * Closes a file descriptor, so that it no longer refers to any file.
 * @param fd File descriptor to close.
 * @return 0 on success, -1 on error.
 */
#if PLATFORM_WINDOWS
    int platform_close(int fd);
    /* Provide macro for transparent usage */
    #ifndef close
        #define close platform_close
    #endif
#else
    #include <unistd.h>  /* close on POSIX */
#endif

/**
 * @brief Get process ID.
 *
 * Returns the process ID of the calling process.
 * @return Process ID.
 */
#if PLATFORM_WINDOWS
    int platform_getpid(void);
    /* Provide macro for transparent usage */
    #ifndef getpid
        #define getpid platform_getpid
    #endif
#else
    #include <unistd.h>  /* getpid on POSIX */
#endif

/**
 * @brief Suspend execution for microsecond intervals.
 *
 * Suspends execution of the calling thread for (at least) usec microseconds.
 * @param usec Number of microseconds to sleep.
 * @return 0 on success, -1 on error.
 */
#if PLATFORM_WINDOWS
    int platform_usleep(unsigned int usec);
    /* Provide macro for transparent usage */
    #ifndef usleep
        #define usleep platform_usleep
    #endif
#else
    #include <unistd.h>  /* usleep on POSIX */
#endif

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_COMPAT_H */
