/*
 * Platform abstraction layer - Directory operations
 *
 * Cross-platform directory creation, removal, existence checking, and iteration.
 * Provides unified API wrapping Windows FindFirstFile/FindNextFile and POSIX opendir/readdir.
 */

#ifndef PLATFORM_DIR_H
#define PLATFORM_DIR_H

#include "platform_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque directory handle type.
 *
 * Wraps HANDLE (FindFirstFile) on Windows, DIR* on POSIX.
 */
#if PLATFORM_WINDOWS
    typedef struct platform_dir_handle_s {
        HANDLE handle;
        WIN32_FIND_DATAA find_data;
        int first_entry;  /* Flag to track if we've read the first entry */
    } platform_dir_handle_t;
#else
    #include <dirent.h>
    typedef DIR* platform_dir_handle_t;
#endif

/**
 * @brief Directory entry structure.
 *
 * Contains information about a single directory entry.
 */
typedef struct {
    char name[256];      /* Entry name (file or directory) */
    int is_directory;    /* 1 if directory, 0 if file */
} platform_dir_entry_t;

/**
 * @brief Create a directory.
 *
 * Windows implementation:
 *   - Uses CreateDirectoryA with NULL security attributes
 *   - Creates single directory (not recursive)
 *
 * POSIX implementation:
 *   - Uses mkdir with mode 0755 (rwxr-xr-x)
 *   - Creates single directory (not recursive)
 *
 * @param path Path to the directory to create.
 * @return 0 on success, -1 on failure (directory already exists or permission denied).
 */
int platform_dir_create(const char *path);

/**
 * @brief Remove a directory.
 *
 * Windows implementation:
 *   - Uses RemoveDirectoryA
 *   - Directory must be empty
 *
 * POSIX implementation:
 *   - Uses rmdir
 *   - Directory must be empty
 *
 * @param path Path to the directory to remove.
 * @return 0 on success, -1 on failure (directory not empty or does not exist).
 */
int platform_dir_remove(const char *path);

/**
 * @brief Check if a directory exists.
 *
 * Windows implementation:
 *   - Uses GetFileAttributesA
 *   - Checks FILE_ATTRIBUTE_DIRECTORY flag
 *
 * POSIX implementation:
 *   - Uses stat
 *   - Checks S_ISDIR macro
 *
 * @param path Path to check.
 * @return 1 if directory exists, 0 if not exists or is a file.
 */
int platform_dir_exists(const char *path);

/**
 * @brief Open a directory for iteration.
 *
 * Windows implementation:
 *   - Uses FindFirstFileA with "path\*" pattern
 *   - Allocates handle structure
 *
 * POSIX implementation:
 *   - Uses opendir
 *   - Returns DIR* handle
 *
 * @param path Path to the directory to open.
 * @return Directory handle on success, NULL on failure.
 *
 * Must be closed with platform_dir_close() after use.
 */
platform_dir_handle_t platform_dir_open(const char *path);

/**
 * @brief Read next directory entry.
 *
 * Windows implementation:
 *   - First call returns result from FindFirstFileA
 *   - Subsequent calls use FindNextFileA
 *   - Skips "." and ".." entries automatically
 *
 * POSIX implementation:
 *   - Uses readdir
 *   - Skips "." and ".." entries automatically
 *
 * @param handle Directory handle from platform_dir_open().
 * @param entry Output pointer receiving directory entry information.
 * @return 1 if entry was read, 0 if end of directory, -1 on error.
 */
int platform_dir_read(platform_dir_handle_t handle, platform_dir_entry_t *entry);

/**
 * @brief Close directory handle.
 *
 * Windows implementation:
 *   - Calls FindClose on the handle
 *   - Frees the handle structure
 *
 * POSIX implementation:
 *   - Calls closedir
 *
 * @param handle Directory handle to close.
 * @return 0 on success, -1 on failure.
 */
int platform_dir_close(platform_dir_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_DIR_H */
