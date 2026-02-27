/*
 * Platform abstraction layer - POSIX Compatibility Implementation
 *
 * Windows implementations of common POSIX functions.
 */

#include "platform_compat.h"

#if PLATFORM_WINDOWS

#include <windows.h>
#include <io.h>      /* _close */
#include <process.h> /* _getpid */
#include <ctype.h>   /* tolower */

/**
 * @brief Case-insensitive string comparison (Windows implementation).
 *
 * Compares two strings ignoring case differences.
 * Uses _stricmp on Windows, which is the native case-insensitive comparison.
 */
int strcasecmp(const char *s1, const char *s2)
{
    if (s1 == NULL || s2 == NULL) {
        /* Handle NULL pointers safely */
        if (s1 == s2) return 0;
        return (s1 == NULL) ? -1 : 1;
    }
    return _stricmp(s1, s2);
}

/**
 * @brief Case-insensitive string comparison with length limit (Windows implementation).
 *
 * Compares up to n characters of two strings ignoring case differences.
 * Uses _strnicmp on Windows.
 */
int strncasecmp(const char *s1, const char *s2, size_t n)
{
    if (s1 == NULL || s2 == NULL) {
        /* Handle NULL pointers safely */
        if (s1 == s2) return 0;
        return (s1 == NULL) ? -1 : 1;
    }
    return _strnicmp(s1, s2, n);
}

/**
 * @brief Close a file descriptor (Windows implementation).
 *
 * Wraps _close on Windows to match POSIX close() behavior.
 */
int platform_close(int fd)
{
    return _close(fd);
}

/**
 * @brief Get process ID (Windows implementation).
 *
 * Wraps _getpid on Windows to match POSIX getpid() behavior.
 */
int platform_getpid(void)
{
    return _getpid();
}

/**
 * @brief Suspend execution for microsecond intervals (Windows implementation).
 *
 * Uses Windows Sleep() function which takes milliseconds.
 * Converts microseconds to milliseconds with rounding.
 */
int platform_usleep(unsigned int usec)
{
    if (usec == 0) {
        /* Sleep(0) yields the time slice, which is correct behavior */
        Sleep(0);
        return 0;
    }

    /* Convert microseconds to milliseconds, rounding up */
    DWORD milliseconds = (usec + 999) / 1000;

    /* Ensure at least 1ms sleep for non-zero usec values */
    if (milliseconds == 0 && usec > 0) {
        milliseconds = 1;
    }

    Sleep(milliseconds);
    return 0;
}

#endif /* PLATFORM_WINDOWS */
