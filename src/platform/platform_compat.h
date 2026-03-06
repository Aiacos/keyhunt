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

/**
 * @brief Get the platform-specific path separator character.
 *
 * Returns the path separator used by the current platform.
 * @return '\\' on Windows, '/' on POSIX systems.
 */
char platform_get_path_separator(void);

/**
 * @brief Normalize path separators for the current platform.
 *
 * Converts all path separators in the given path to the platform-specific format.
 * On Windows: converts '/' to '\\'
 * On POSIX: converts '\\' to '/'
 * Modifies the path in-place.
 *
 * @param path Path string to normalize (modified in-place).
 * @return Pointer to the normalized path (same as input).
 */
char* platform_normalize_path(char *path);

/**
 * @brief Thread-safe version of localtime.
 *
 * Converts a time_t value to a broken-down local time.
 * On POSIX: wraps localtime_r directly.
 * On Windows: wraps localtime_s (which has reversed parameter order).
 *
 * @param timep Pointer to time_t value to convert.
 * @param result Pointer to struct tm to store the result.
 * @return Pointer to result on success, NULL on error.
 */
#if PLATFORM_WINDOWS
    #include <time.h>
    static inline struct tm *platform_localtime_r(const time_t *timep, struct tm *result) {
        return (localtime_s(result, timep) == 0) ? result : NULL;
    }
    #ifndef localtime_r
        #define localtime_r platform_localtime_r
    #endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_COMPAT_H */
