/*
 * secure_file.h - Secure file opening with restrictive permissions
 *
 * Provides fopen_secure_append() which creates files with 0600 permissions
 * (owner read/write only) instead of the default umask-dependent permissions.
 * Used for writing sensitive data like found private keys.
 */

#ifndef SECURE_FILE_H
#define SECURE_FILE_H

#include <stdio.h>
#include "platform/platform_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open a file for appending with secure permissions (0600).
 * Creates the file if it doesn't exist.
 * Returns FILE* on success, NULL on failure.
 */
#if PLATFORM_WINDOWS
static inline FILE *fopen_secure_append(const char *path) {
    return fopen(path, "a");
}
#else
#include <fcntl.h>
#include <unistd.h>
static inline FILE *fopen_secure_append(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, "a");
    if (!f) close(fd);
    return f;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* SECURE_FILE_H */
