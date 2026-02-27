/*
 * Platform abstraction layer - Directory operations implementation
 *
 * Cross-platform directory creation, removal, existence checking, and iteration.
 * Provides unified API wrapping Windows FindFirstFile/FindNextFile and POSIX opendir/readdir.
 */

#include "platform_dir.h"
#include "platform_types.h"
#include <string.h>
#include <stdlib.h>

#if PLATFORM_WINDOWS
    #include <windows.h>
#else
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <dirent.h>
    #include <unistd.h>
    #include <errno.h>
#endif

/**
 * Create a directory.
 *
 * Windows implementation:
 *   - Uses CreateDirectoryA with NULL security (default permissions)
 *   - Creates single directory (parent must exist)
 *   - Returns FALSE if directory already exists
 *
 * POSIX implementation:
 *   - Uses mkdir with mode 0755 (rwxr-xr-x permissions)
 *   - Creates single directory (parent must exist)
 *   - Returns -1 with errno=EEXIST if directory already exists
 */
int platform_dir_create(const char *path)
{
    if (!path) {
        return -1;
    }

#if PLATFORM_WINDOWS
    if (!CreateDirectoryA(path, NULL)) {
        /* Check if failure is because directory already exists */
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            return -1;
        }
        return -1;
    }
    return 0;
#else
    /* Create directory with rwxr-xr-x permissions */
    if (mkdir(path, 0755) != 0) {
        return -1;
    }
    return 0;
#endif
}

/**
 * Remove a directory.
 *
 * Windows implementation:
 *   - Uses RemoveDirectoryA
 *   - Directory must be empty
 *   - Returns FALSE on failure
 *
 * POSIX implementation:
 *   - Uses rmdir
 *   - Directory must be empty
 *   - Returns -1 on failure with errno set
 */
int platform_dir_remove(const char *path)
{
    if (!path) {
        return -1;
    }

#if PLATFORM_WINDOWS
    if (!RemoveDirectoryA(path)) {
        return -1;
    }
    return 0;
#else
    if (rmdir(path) != 0) {
        return -1;
    }
    return 0;
#endif
}

/**
 * Check if a directory exists.
 *
 * Windows implementation:
 *   - Uses GetFileAttributesA to retrieve file attributes
 *   - Returns INVALID_FILE_ATTRIBUTES if file/directory doesn't exist
 *   - Checks FILE_ATTRIBUTE_DIRECTORY flag to verify it's a directory
 *
 * POSIX implementation:
 *   - Uses stat to get file information
 *   - Returns -1 if file/directory doesn't exist
 *   - Uses S_ISDIR macro to check if it's a directory
 */
int platform_dir_exists(const char *path)
{
    if (!path) {
        return 0;
    }

#if PLATFORM_WINDOWS
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return 0;  /* Does not exist */
    }
    /* Check if it's a directory */
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;  /* Does not exist */
    }
    /* Check if it's a directory */
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

/**
 * Open a directory for iteration.
 *
 * Windows implementation:
 *   - Appends "\*" to path for wildcard matching
 *   - Uses FindFirstFileA to start iteration
 *   - Allocates handle structure to track iteration state
 *   - first_entry flag ensures first entry is returned on first read
 *
 * POSIX implementation:
 *   - Uses opendir which returns DIR* handle
 *   - Handle is used directly for readdir calls
 */
platform_dir_handle_t platform_dir_open(const char *path)
{
    if (!path) {
        return NULL;
    }

#if PLATFORM_WINDOWS
    char search_path[MAX_PATH];
    platform_dir_handle_t handle;

    /* Construct search path: "path\*" */
    snprintf(search_path, sizeof(search_path), "%s\\*", path);

    /* Allocate handle structure */
    handle = (platform_dir_handle_t)malloc(sizeof(struct platform_dir_handle_s));
    if (!handle) {
        return NULL;
    }

    /* Start directory iteration */
    handle->handle = FindFirstFileA(search_path, &handle->find_data);
    if (handle->handle == INVALID_HANDLE_VALUE) {
        free(handle);
        return NULL;
    }

    handle->first_entry = 1;  /* First entry is already loaded */
    return handle;
#else
    /* opendir returns DIR* handle or NULL on error */
    return opendir(path);
#endif
}

/**
 * Read next directory entry.
 *
 * Windows implementation:
 *   - First call returns entry from FindFirstFileA
 *   - Subsequent calls use FindNextFileA
 *   - Skips "." and ".." entries automatically
 *   - Determines if entry is directory via dwFileAttributes
 *
 * POSIX implementation:
 *   - Uses readdir to get next entry
 *   - Returns NULL when no more entries
 *   - Skips "." and ".." entries automatically
 *   - Uses d_type to determine if entry is directory (DT_DIR)
 */
int platform_dir_read(platform_dir_handle_t handle, platform_dir_entry_t *entry)
{
    if (!handle || !entry) {
        return -1;
    }

#if PLATFORM_WINDOWS
    BOOL found = FALSE;

    /* Handle first entry (already loaded by FindFirstFileA) */
    if (handle->first_entry) {
        handle->first_entry = 0;
        found = TRUE;
    } else {
        /* Read next entry */
        found = FindNextFileA(handle->handle, &handle->find_data);
    }

    /* Loop until we find a valid entry or reach end */
    while (found) {
        /* Skip "." and ".." */
        if (strcmp(handle->find_data.cFileName, ".") == 0 ||
            strcmp(handle->find_data.cFileName, "..") == 0) {
            found = FindNextFileA(handle->handle, &handle->find_data);
            continue;
        }

        /* Copy entry information */
        strncpy(entry->name, handle->find_data.cFileName, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
        entry->is_directory = (handle->find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        return 1;  /* Entry found */
    }

    /* No more entries */
    return 0;
#else
    struct dirent *ent;

    /* Loop until we find a valid entry or reach end */
    while ((ent = readdir(handle)) != NULL) {
        /* Skip "." and ".." */
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        /* Copy entry information */
        strncpy(entry->name, ent->d_name, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';

        /* Determine if entry is a directory */
#ifdef _DIRENT_HAVE_D_TYPE
        entry->is_directory = (ent->d_type == DT_DIR) ? 1 : 0;
#else
        /* Fallback: if d_type is not available, assume it's a file */
        entry->is_directory = 0;
#endif
        return 1;  /* Entry found */
    }

    /* No more entries */
    return 0;
#endif
}

/**
 * Close directory handle.
 *
 * Windows implementation:
 *   - Calls FindClose to release Windows handle
 *   - Frees the allocated handle structure
 *
 * POSIX implementation:
 *   - Calls closedir to close DIR* handle
 *   - Returns -1 on error
 */
int platform_dir_close(platform_dir_handle_t handle)
{
    if (!handle) {
        return -1;
    }

#if PLATFORM_WINDOWS
    if (handle->handle != INVALID_HANDLE_VALUE) {
        FindClose(handle->handle);
    }
    free(handle);
    return 0;
#else
    if (closedir(handle) != 0) {
        return -1;
    }
    return 0;
#endif
}
