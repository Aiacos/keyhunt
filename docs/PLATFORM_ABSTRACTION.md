# Platform Abstraction Layer Documentation

## Overview

The platform abstraction layer provides a unified, cross-platform API for system-level operations, enabling seamless compilation on Windows (64-bit) and POSIX-compliant systems (Linux, macOS) without scattered `#ifdef` blocks throughout the codebase.

**Key Features:**
- Single include point: `#include "platform/platform.h"`
- Opaque type wrappers for platform-specific handles
- Zero external dependencies (uses native OS APIs only)
- Runtime CPU feature detection
- Thread-safe operations
- Clean error handling (0 = success, non-zero = error)

## Supported Platforms

| Platform | Detection | Requirements |
|----------|-----------|--------------|
| **Windows** | `_WIN64` defined | Windows 7+ (64-bit), Windows API |
| **Linux** | POSIX detected | glibc 2.17+, pthread, clock_gettime |
| **macOS** | POSIX detected | macOS 10.12+, pthread, clock_gettime |

**Note:** Cygwin on Windows is treated as POSIX, not Windows.

## Quick Start

### Basic Usage

```c
#include "platform/platform.h"

int main(void) {
    /* Thread example */
    platform_thread_t thread;
    platform_thread_create(&thread, my_worker, user_data);
    platform_thread_join(thread, NULL);

    /* Mutex example */
    platform_mutex_t mutex;
    platform_mutex_init(&mutex);
    platform_mutex_lock(&mutex);
    /* ... critical section ... */
    platform_mutex_unlock(&mutex);
    platform_mutex_destroy(&mutex);

    /* Timing example */
    uint64_t start = platform_time_now_ns();
    /* ... do work ... */
    uint64_t elapsed_ns = platform_time_now_ns() - start;
    double elapsed_sec = elapsed_ns / 1e9;

    /* Directory example */
    if (!platform_dir_exists("output")) {
        platform_dir_create("output");
    }

    /* POSIX compatibility example */
    char path[] = "output/data/file.txt";
    platform_normalize_path(path);  /* Converts to platform format */

    return 0;
}
```

### Build Instructions

#### Linux / macOS

Standard build (automatic detection):
```bash
make clean
make
```

The Makefile automatically detects the platform and links required libraries:
- Linux: `-lpthread -lrt`
- macOS: `-lpthread` (no `-lrt` needed)

#### Windows (64-bit)

Using MinGW-w64:
```bash
# Install MinGW-w64
# Ubuntu/Debian: apt-get install mingw-w64
# macOS: brew install mingw-w64

# Build
make clean
make CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++
```

Using MSVC:
```cmd
cl /O2 /I. /Fekeyhunt.exe keyhunt.cpp src/platform/*.c /link /SUBSYSTEM:CONSOLE
```

#### Cross-Platform Build Matrix

| Host OS | Target OS | Compiler | Command |
|---------|-----------|----------|---------|
| Linux | Linux | GCC/Clang | `make` |
| macOS | macOS | Clang | `make` |
| Linux | Windows | MinGW-w64 | `make CC=x86_64-w64-mingw32-gcc` |
| Windows | Windows | MSVC | `cl /Fe:keyhunt.exe keyhunt.cpp src/platform/*.c` |

## Architecture

### File Structure

```
src/platform/
├── platform.h              # Main entry point (includes all sub-headers)
├── platform_types.h        # Opaque type definitions, platform detection
├── platform_thread.h/c     # Thread operations (create, join, detach)
├── platform_mutex.h/c      # Mutex operations (init, lock, unlock, destroy)
├── platform_time.h/c       # High-resolution monotonic time
├── platform_dir.h/c        # Directory operations (create, remove, iterate)
└── platform_compat.h/c     # POSIX compatibility (strcasecmp, usleep, paths)
```

### Platform Detection (platform_types.h)

Compile-time platform detection using preprocessor macros:

```c
#if defined(_WIN64) && !defined(__CYGWIN__)
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_POSIX 0
#else
    #define PLATFORM_WINDOWS 0
    #define PLATFORM_POSIX 1
#endif
```

### Opaque Types

Platform-specific handles are wrapped in unified types:

| Abstract Type | Windows Type | POSIX Type |
|---------------|--------------|------------|
| `platform_thread_t` | `HANDLE` | `pthread_t` |
| `platform_mutex_t` | `HANDLE` (mutex object) | `pthread_mutex_t` |
| `platform_thread_func_t` | `DWORD (WINAPI *)(void*)` | `void* (*)(void*)` |
| `platform_thread_return_t` | `DWORD` | `void*` |
| `platform_dir_handle_t` | `struct { HANDLE, WIN32_FIND_DATAA }` | `DIR*` |

## API Reference

### Thread Operations

#### `platform_thread_create`

Create and start a new thread.

```c
int platform_thread_create(
    platform_thread_t *thread,      // Output: thread handle
    platform_thread_func_t func,    // Thread entry point
    void *arg                       // User argument passed to func
);
```

**Returns:** 0 on success, non-zero error code on failure.

**Implementation:**
- **Windows:** `CreateThread` with default stack size, immediate start
- **POSIX:** `pthread_create` with default attributes

**Example:**
```c
platform_thread_return_t PLATFORM_THREAD_FUNC my_worker(void *arg) {
    int *data = (int*)arg;
    printf("Worker thread received: %d\n", *data);
    return 0;
}

int main(void) {
    platform_thread_t thread;
    int data = 42;

    if (platform_thread_create(&thread, my_worker, &data) != 0) {
        fprintf(stderr, "Failed to create thread\n");
        return 1;
    }

    platform_thread_join(thread, NULL);
    return 0;
}
```

#### `platform_thread_join`

Wait for a thread to terminate and retrieve its exit value.

```c
int platform_thread_join(
    platform_thread_t thread,              // Thread to wait for
    platform_thread_return_t *retval       // Output: exit value (optional, may be NULL)
);
```

**Returns:** 0 on success, non-zero error code on failure.

**Implementation:**
- **Windows:** `WaitForSingleObject(INFINITE)` + `GetExitCodeThread`
- **POSIX:** `pthread_join`

**Important:** Do NOT call on detached threads.

#### `platform_thread_detach`

Detach a thread, allowing it to run independently.

```c
int platform_thread_detach(platform_thread_t thread);
```

**Returns:** 0 on success, non-zero error code on failure.

**Implementation:**
- **Windows:** `CloseHandle` (decrements reference count)
- **POSIX:** `pthread_detach`

**After detaching:**
- Thread resources are automatically cleaned up on exit
- Do NOT call `platform_thread_join()` on a detached thread

### Mutex Operations

#### `platform_mutex_init`

Initialize a mutex.

```c
int platform_mutex_init(platform_mutex_t *mutex);
```

**Returns:** 0 on success, non-zero error code on failure.

**Implementation:**
- **Windows:** `CreateMutex` (unnamed, not initially owned)
- **POSIX:** `pthread_mutex_init` with default attributes

**Must call `platform_mutex_destroy()` when done.**

#### `platform_mutex_lock`

Acquire a mutex lock (blocking).

```c
int platform_mutex_lock(platform_mutex_t *mutex);
```

**Returns:** 0 on success, non-zero error code on failure.

**Implementation:**
- **Windows:** `WaitForSingleObject(INFINITE)`
- **POSIX:** `pthread_mutex_lock`

**Blocks until the mutex becomes available. Always pair with `platform_mutex_unlock()`.**

#### `platform_mutex_unlock`

Release a mutex lock.

```c
int platform_mutex_unlock(platform_mutex_t *mutex);
```

**Returns:** 0 on success, non-zero error code on failure.

**Implementation:**
- **Windows:** `ReleaseMutex`
- **POSIX:** `pthread_mutex_unlock`

**Must be called by the same thread that locked the mutex.**

#### `platform_mutex_destroy`

Destroy a mutex and release its resources.

```c
int platform_mutex_destroy(platform_mutex_t *mutex);
```

**Returns:** 0 on success, non-zero error code on failure.

**Implementation:**
- **Windows:** `CloseHandle`
- **POSIX:** `pthread_mutex_destroy`

**Important:**
- Do NOT destroy a locked mutex (undefined behavior)
- After destruction, the mutex must be re-initialized before reuse

### Time Operations

#### `platform_time_now_ns`

Get current monotonic time in nanoseconds.

```c
uint64_t platform_time_now_ns(void);
```

**Returns:** Monotonic timestamp in nanoseconds (uint64_t).

**Implementation:**
- **Windows:** `QueryPerformanceCounter` + `QueryPerformanceFrequency`
  - High-resolution performance counter (typically 10 MHz)
  - Converts ticks to nanoseconds with overflow protection
  - Two-step conversion: ticks → microseconds → nanoseconds
- **POSIX:** `clock_gettime`
  - Prefers `CLOCK_MONOTONIC_RAW` (not affected by NTP)
  - Falls back to `CLOCK_MONOTONIC` if unavailable
  - Direct nanosecond resolution (1e-9 seconds)

**Properties:**
- **Monotonic:** Not affected by system time changes (NTP, manual adjustments)
- **High-resolution:** Sub-microsecond precision on most systems
- **Thread-safe:** Can be called from multiple threads concurrently

**Suitable for:**
- Performance measurements and benchmarking
- Elapsed time calculations
- Timeout implementations

**NOT suitable for:**
- Wall-clock time
- Absolute timestamps
- Time-of-day displays

**Example:**
```c
uint64_t start = platform_time_now_ns();

/* Perform operation */
for (int i = 0; i < 1000000; i++) {
    /* ... computation ... */
}

uint64_t end = platform_time_now_ns();
uint64_t elapsed_ns = end - start;
double elapsed_ms = elapsed_ns / 1e6;
double elapsed_sec = elapsed_ns / 1e9;

printf("Operation took: %.3f ms (%.6f sec)\n", elapsed_ms, elapsed_sec);
```

### Directory Operations

#### `platform_dir_create`

Create a new directory.

```c
int platform_dir_create(const char *path);
```

**Returns:** 0 on success, -1 on failure (directory already exists or permission denied).

**Implementation:**
- **Windows:** `CreateDirectoryA` with NULL security attributes
- **POSIX:** `mkdir` with mode 0755 (rwxr-xr-x)

**Note:** Creates a single directory, not recursive (parent must exist).

**Example:**
```c
if (platform_dir_create("output") != 0) {
    fprintf(stderr, "Failed to create directory\n");
    return 1;
}
```

#### `platform_dir_remove`

Remove an empty directory.

```c
int platform_dir_remove(const char *path);
```

**Returns:** 0 on success, -1 on failure (directory not empty or does not exist).

**Implementation:**
- **Windows:** `RemoveDirectoryA`
- **POSIX:** `rmdir`

**Important:** Directory must be empty before removal.

#### `platform_dir_exists`

Check if a directory exists.

```c
int platform_dir_exists(const char *path);
```

**Returns:** 1 if directory exists, 0 if not exists or is a file.

**Implementation:**
- **Windows:** `GetFileAttributesA` with `FILE_ATTRIBUTE_DIRECTORY` check
- **POSIX:** `stat` with `S_ISDIR` macro

**Example:**
```c
if (!platform_dir_exists("data")) {
    platform_dir_create("data");
}
```

#### `platform_dir_open`

Open a directory for iteration.

```c
platform_dir_handle_t platform_dir_open(const char *path);
```

**Returns:** Directory handle on success, NULL on failure.

**Implementation:**
- **Windows:** `FindFirstFileA` with "path\\*" pattern
- **POSIX:** `opendir`

**Must be closed with `platform_dir_close()` after use.**

#### `platform_dir_read`

Read the next directory entry.

```c
int platform_dir_read(
    platform_dir_handle_t handle,
    platform_dir_entry_t *entry
);
```

**Returns:** 1 if entry was read, 0 if end of directory, -1 on error.

**Implementation:**
- **Windows:** First call uses cached `FindFirstFileA` result, subsequent calls use `FindNextFileA`
- **POSIX:** `readdir`

**Automatically skips "." and ".." entries.**

**Entry structure:**
```c
typedef struct {
    char name[256];      /* Entry name (file or directory) */
    int is_directory;    /* 1 if directory, 0 if file */
} platform_dir_entry_t;
```

**Example:**
```c
platform_dir_handle_t dir = platform_dir_open("data");
if (dir == NULL) {
    fprintf(stderr, "Failed to open directory\n");
    return 1;
}

platform_dir_entry_t entry;
while (platform_dir_read(dir, &entry) == 1) {
    printf("%s %s\n",
           entry.is_directory ? "[DIR]" : "[FILE]",
           entry.name);
}

platform_dir_close(dir);
```

#### `platform_dir_close`

Close a directory handle.

```c
int platform_dir_close(platform_dir_handle_t handle);
```

**Returns:** 0 on success, -1 on failure.

**Implementation:**
- **Windows:** `FindClose` and frees handle structure
- **POSIX:** `closedir`

### POSIX Compatibility Functions

The platform compatibility layer provides Windows implementations of common POSIX functions, enabling clean cross-platform code without scattered `#ifdef` blocks.

#### `strcasecmp` / `strncasecmp`

Case-insensitive string comparison.

```c
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
```

**Returns:** Integer less than, equal to, or greater than zero if s1 is found to be less than, to match, or be greater than s2.

**Implementation:**
- **Windows:** Custom implementation using `_stricmp` / `_strnicmp`
- **POSIX:** Native `strcasecmp` / `strncasecmp` from `<strings.h>`

**Example:**
```c
if (strcasecmp(mode, "BSGS") == 0) {
    /* Mode is bsgs (case-insensitive) */
}

if (strncasecmp(prefix, "0x", 2) == 0) {
    /* String starts with hex prefix */
}
```

#### `close`

Close a file descriptor.

```c
int close(int fd);  /* Aliased to platform_close on Windows */
```

**Returns:** 0 on success, -1 on error.

**Implementation:**
- **Windows:** `_close` from CRT
- **POSIX:** Native `close` from `<unistd.h>`

**Note:** On Windows, this is provided via macro to `platform_close`.

#### `getpid`

Get process ID.

```c
int getpid(void);  /* Aliased to platform_getpid on Windows */
```

**Returns:** Process ID of the calling process.

**Implementation:**
- **Windows:** `GetCurrentProcessId`
- **POSIX:** Native `getpid` from `<unistd.h>`

**Example:**
```c
printf("Current process ID: %d\n", getpid());
```

#### `usleep`

Suspend execution for microsecond intervals.

```c
int usleep(unsigned int usec);  /* Aliased to platform_usleep on Windows */
```

**Returns:** 0 on success, -1 on error.

**Implementation:**
- **Windows:** `Sleep(usec / 1000)` with millisecond rounding
- **POSIX:** Native `usleep` from `<unistd.h>`

**Note:** Windows implementation has millisecond resolution (rounds up).

**Example:**
```c
/* Sleep for 100 microseconds */
usleep(100);

/* Sleep for 1 millisecond */
usleep(1000);
```

#### `platform_get_path_separator`

Get the platform-specific path separator character.

```c
char platform_get_path_separator(void);
```

**Returns:** '\\' on Windows, '/' on POSIX systems.

**Example:**
```c
char sep = platform_get_path_separator();
sprintf(path, "data%cconfig.txt", sep);
/* Windows: "data\config.txt" */
/* POSIX: "data/config.txt" */
```

#### `platform_normalize_path`

Normalize path separators for the current platform.

```c
char* platform_normalize_path(char *path);
```

**Returns:** Pointer to the normalized path (same as input).

**Behavior:**
- **Windows:** Converts '/' to '\\'
- **POSIX:** Converts '\\' to '/'
- Modifies the path in-place

**Example:**
```c
char path[] = "data/subdir\\file.txt";
platform_normalize_path(path);
/* Windows: "data\subdir\file.txt" */
/* POSIX: "data/subdir/file.txt" */
```

## Advanced Usage

### Producer-Consumer Pattern

```c
#include "platform/platform.h"
#include <stdio.h>

typedef struct {
    int data;
    platform_mutex_t mutex;
    int ready;
} shared_data_t;

platform_thread_return_t PLATFORM_THREAD_FUNC producer(void *arg) {
    shared_data_t *shared = (shared_data_t*)arg;

    /* Produce data */
    platform_mutex_lock(&shared->mutex);
    shared->data = 42;
    shared->ready = 1;
    platform_mutex_unlock(&shared->mutex);

    return 0;
}

platform_thread_return_t PLATFORM_THREAD_FUNC consumer(void *arg) {
    shared_data_t *shared = (shared_data_t*)arg;

    /* Wait for data */
    while (1) {
        platform_mutex_lock(&shared->mutex);
        if (shared->ready) {
            printf("Consumed: %d\n", shared->data);
            platform_mutex_unlock(&shared->mutex);
            break;
        }
        platform_mutex_unlock(&shared->mutex);
    }

    return 0;
}

int main(void) {
    shared_data_t shared = {0};
    platform_mutex_init(&shared.mutex);

    platform_thread_t prod, cons;
    platform_thread_create(&prod, producer, &shared);
    platform_thread_create(&cons, consumer, &shared);

    platform_thread_join(prod, NULL);
    platform_thread_join(cons, NULL);

    platform_mutex_destroy(&shared.mutex);
    return 0;
}
```

### Thread Pool Pattern

```c
#include "platform/platform.h"
#include <stdlib.h>

#define NUM_WORKERS 4

typedef struct {
    int worker_id;
    uint64_t operations;
} worker_data_t;

platform_thread_return_t PLATFORM_THREAD_FUNC worker(void *arg) {
    worker_data_t *data = (worker_data_t*)arg;
    uint64_t start = platform_time_now_ns();

    /* Simulate work */
    for (int i = 0; i < 1000000; i++) {
        data->operations++;
    }

    uint64_t elapsed = platform_time_now_ns() - start;
    printf("Worker %d: %llu ops in %.3f ms\n",
           data->worker_id,
           (unsigned long long)data->operations,
           elapsed / 1e6);

    return 0;
}

int main(void) {
    platform_thread_t threads[NUM_WORKERS];
    worker_data_t workers[NUM_WORKERS] = {0};

    /* Start worker threads */
    for (int i = 0; i < NUM_WORKERS; i++) {
        workers[i].worker_id = i;
        platform_thread_create(&threads[i], worker, &workers[i]);
    }

    /* Wait for completion */
    for (int i = 0; i < NUM_WORKERS; i++) {
        platform_thread_join(threads[i], NULL);
    }

    return 0;
}
```

### Benchmark Pattern

```c
#include "platform/platform.h"
#include <stdio.h>

void benchmark_function(const char *name, void (*func)(void)) {
    const int iterations = 1000;
    uint64_t total = 0;

    for (int i = 0; i < iterations; i++) {
        uint64_t start = platform_time_now_ns();
        func();
        uint64_t elapsed = platform_time_now_ns() - start;
        total += elapsed;
    }

    double avg_ns = (double)total / iterations;
    double avg_us = avg_ns / 1e3;
    double avg_ms = avg_ns / 1e6;

    printf("%-30s: %.3f ms (%.3f µs, %.0f ns)\n",
           name, avg_ms, avg_us, avg_ns);
}

void my_algorithm(void) {
    /* ... implementation ... */
}

int main(void) {
    benchmark_function("My Algorithm", my_algorithm);
    return 0;
}
```

### Directory Traversal Pattern

```c
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

void process_directory(const char *path) {
    platform_dir_handle_t dir = platform_dir_open(path);
    if (dir == NULL) {
        fprintf(stderr, "Failed to open directory: %s\n", path);
        return;
    }

    platform_dir_entry_t entry;
    int count_files = 0, count_dirs = 0;

    while (platform_dir_read(dir, &entry) == 1) {
        if (entry.is_directory) {
            printf("[DIR]  %s\n", entry.name);
            count_dirs++;
        } else {
            printf("[FILE] %s\n", entry.name);
            count_files++;
        }
    }

    platform_dir_close(dir);
    printf("Total: %d files, %d directories\n", count_files, count_dirs);
}

int main(void) {
    /* Ensure directory exists */
    if (!platform_dir_exists("output")) {
        if (platform_dir_create("output") != 0) {
            fprintf(stderr, "Failed to create output directory\n");
            return 1;
        }
    }

    /* Process directory contents */
    process_directory("output");

    return 0;
}
```

### Cross-Platform Path Handling

```c
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

void build_cross_platform_path(void) {
    char sep = platform_get_path_separator();

    /* Build path using platform separator */
    char path[256];
    snprintf(path, sizeof(path), "data%cconfig%csettings.ini", sep, sep);
    printf("Platform-specific path: %s\n", path);
    /* Windows: "data\config\settings.ini" */
    /* POSIX: "data/config/settings.ini" */

    /* Normalize mixed separators */
    char mixed[] = "data/logs\\2024/errors.log";
    platform_normalize_path(mixed);
    printf("Normalized path: %s\n", mixed);
    /* Windows: "data\logs\2024\errors.log" */
    /* POSIX: "data/logs/2024/errors.log" */
}

int main(void) {
    build_cross_platform_path();
    return 0;
}
```

### Safe Directory Creation with Parents

```c
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

int create_directory_recursive(const char *path) {
    char temp[256];
    char *p = NULL;
    size_t len;

    snprintf(temp, sizeof(temp), "%s", path);
    platform_normalize_path(temp);
    len = strlen(temp);

    /* Remove trailing separator */
    if (temp[len - 1] == platform_get_path_separator()) {
        temp[len - 1] = '\0';
    }

    /* Create each parent directory */
    for (p = temp + 1; *p; p++) {
        if (*p == platform_get_path_separator()) {
            *p = '\0';
            if (!platform_dir_exists(temp)) {
                if (platform_dir_create(temp) != 0) {
                    return -1;
                }
            }
            *p = platform_get_path_separator();
        }
    }

    /* Create final directory */
    if (!platform_dir_exists(temp)) {
        return platform_dir_create(temp);
    }

    return 0;
}

int main(void) {
    /* Create nested directory structure */
    if (create_directory_recursive("output/data/2024/logs") == 0) {
        printf("Successfully created directory structure\n");
    } else {
        fprintf(stderr, "Failed to create directories\n");
        return 1;
    }

    return 0;
}
```

## Design Principles

### 1. Single Include Point

All platform functionality is accessed through one header:
```c
#include "platform/platform.h"
```

This replaces scattered platform-specific includes:
```c
/* OLD: Platform-specific includes scattered everywhere */
#ifdef _WIN32
    #include <windows.h>
#else
    #include <pthread.h>
    #include <time.h>
#endif

/* NEW: Single unified include */
#include "platform/platform.h"
```

### 2. Opaque Types

Platform-specific handles are wrapped in unified types, hiding implementation details:

```c
/* Application code sees abstract types */
platform_thread_t thread;
platform_mutex_t mutex;

/* Implementation details hidden behind the abstraction */
#if PLATFORM_WINDOWS
    typedef HANDLE platform_thread_t;  /* Windows */
#else
    typedef pthread_t platform_thread_t;  /* POSIX */
#endif
```

### 3. Zero External Dependencies

Uses only native OS APIs:
- **Windows:** Windows API (kernel32.dll, no CRT dependencies beyond standard library)
- **POSIX:** pthread (libpthread), clock_gettime (libc/librt)

No third-party libraries required.

### 4. Runtime Detection

Platform features are detected at compile-time via preprocessor macros:

```c
#if PLATFORM_WINDOWS
    /* Windows implementation */
#else
    /* POSIX implementation */
#endif
```

CPU features (AVX2, AVX-512) are detected at runtime by the main application.

### 5. Clean Error Handling

All functions follow a consistent error convention:
- **Return 0:** Success
- **Return non-zero:** Error code (platform-specific)

```c
if (platform_thread_create(&thread, worker, data) != 0) {
    fprintf(stderr, "Failed to create thread\n");
    return 1;
}
```

## Integration Notes

### Replacing Direct Platform APIs

The platform abstraction layer eliminates the need for direct platform API usage:

| Old Code | New Code |
|----------|----------|
| `#include <pthread.h>` | `#include "platform/platform.h"` |
| `pthread_create(...)` | `platform_thread_create(...)` |
| `pthread_join(...)` | `platform_thread_join(...)` |
| `pthread_mutex_lock(...)` | `platform_mutex_lock(...)` |
| `clock_gettime(...)` | `platform_time_now_ns()` |
| `mkdir(...) / CreateDirectory(...)` | `platform_dir_create(...)` |
| `opendir(...) / FindFirstFile(...)` | `platform_dir_open(...)` |
| `strcasecmp(...) / _stricmp(...)` | `strcasecmp(...)` (auto-aliased) |
| `usleep(...) / Sleep(...)` | `usleep(...)` (auto-aliased) |
| `#ifdef _WIN32 ... #endif` | (No conditional compilation needed) |

### Thread Safety

All platform abstraction functions are thread-safe and can be called concurrently from multiple threads:
- Multiple threads can create other threads simultaneously
- Multiple threads can acquire different mutexes concurrently
- `platform_time_now_ns()` is fully reentrant

### Memory Management

- **Thread handles:** No explicit cleanup required after `platform_thread_join()` or `platform_thread_detach()`
- **Mutex objects:** Must call `platform_mutex_destroy()` to release resources
- **No heap allocations:** All operations use stack-allocated types or OS-managed resources

### Performance Characteristics

| Operation | Windows | POSIX | Typical Cost |
|-----------|---------|-------|--------------|
| Thread create | CreateThread | pthread_create | 10-50 µs |
| Thread join | WaitForSingleObject | pthread_join | <1 µs (if already terminated) |
| Mutex lock (uncontended) | WaitForSingleObject | pthread_mutex_lock | 10-50 ns |
| Mutex unlock | ReleaseMutex | pthread_mutex_unlock | 10-50 ns |
| Time query | QueryPerformanceCounter | clock_gettime | 50-200 ns |

## Troubleshooting

### Compilation Issues

#### Windows: Missing `windows.h`

**Error:**
```
fatal error: windows.h: No such file or directory
```

**Solution:**
Install MinGW-w64 or Windows SDK:
```bash
# Ubuntu/Debian
sudo apt-get install mingw-w64

# macOS
brew install mingw-w64
```

#### Linux: Undefined reference to `pthread_create`

**Error:**
```
undefined reference to `pthread_create`
```

**Solution:**
Link with pthread library:
```bash
gcc -o keyhunt keyhunt.c src/platform/*.c -lpthread -lrt
```

The Makefile handles this automatically.

#### Linux: Undefined reference to `clock_gettime`

**Error:**
```
undefined reference to `clock_gettime`
```

**Solution:**
Link with rt library (glibc < 2.17):
```bash
gcc -o keyhunt keyhunt.c src/platform/*.c -lpthread -lrt
```

For glibc ≥ 2.17, `-lrt` is not needed but harmless.

### Runtime Issues

#### Thread Creation Fails

**Symptoms:**
- `platform_thread_create()` returns non-zero
- Application fails to spawn workers

**Possible causes:**
1. Resource limits (ulimit -u on Linux)
2. Out of memory
3. Invalid function pointer

**Solution:**
```bash
# Check thread limit (Linux)
ulimit -u

# Increase if needed
ulimit -u 4096
```

#### Deadlock on Mutex

**Symptoms:**
- Application hangs indefinitely
- No CPU usage

**Common causes:**
1. Forgot to call `platform_mutex_unlock()`
2. Locking same mutex twice from same thread (undefined behavior)
3. Lock ordering violation (thread A locks mutex1→mutex2, thread B locks mutex2→mutex1)

**Prevention:**
- Always pair `lock()` with `unlock()`
- Use consistent lock ordering across all threads
- Consider RAII wrappers in C++ code

#### Time Measurements Incorrect

**Symptoms:**
- Negative elapsed time
- Extremely large elapsed time

**Causes:**
- Using `platform_time_now_ns()` for wall-clock time (wrong usage)
- Comparing timestamps from different runs (monotonic time is not absolute)

**Correct usage:**
```c
/* ✓ CORRECT: Measure elapsed time within same run */
uint64_t start = platform_time_now_ns();
do_work();
uint64_t elapsed = platform_time_now_ns() - start;

/* ✗ WRONG: Comparing timestamps from different runs */
uint64_t time1 = platform_time_now_ns();
/* (restart application) */
uint64_t time2 = platform_time_now_ns();
uint64_t diff = time2 - time1;  /* Meaningless! */
```

### Platform-Specific Issues

#### Windows: Access Denied on Mutex

**Error:**
Windows error code 5 (ERROR_ACCESS_DENIED) from mutex operations.

**Solution:**
Ensure mutex is not being accessed after destruction or from wrong process.

#### Linux: CLOCK_MONOTONIC_RAW Not Available

**Symptoms:**
`clock_gettime` fails with EINVAL on older kernels.

**Solution:**
The implementation automatically falls back to `CLOCK_MONOTONIC` if `CLOCK_MONOTONIC_RAW` is unavailable (Linux < 2.6.28).

## Testing

### Unit Tests

Test platform abstraction independently:

```bash
# Compile test program
gcc -o test_platform tests/test_platform.c src/platform/*.c -lpthread -lrt

# Run tests
./test_platform
```

### Cross-Platform Testing

Verify functionality on all target platforms:

```bash
# Linux native
make clean && make
./keyhunt -m address -f tests/1to32.txt -t 4

# Windows cross-compile (from Linux)
make clean
make CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++
wine ./keyhunt.exe -m address -f tests/1to32.txt -t 4

# macOS native
make clean && make
./keyhunt -m address -f tests/1to32.txt -t 4
```

### Performance Testing

Measure overhead of platform abstraction:

```bash
# Build with profiling
make clean
CFLAGS="-pg" make

# Run workload
./keyhunt -m address -f tests/66.txt -b 66 -t 8

# Analyze profile
gprof keyhunt gmon.out | grep platform_
```

Expected overhead: <1% (abstraction layer is thin wrapper around native APIs).

## Future Enhancements

Potential additions to the platform abstraction layer:

1. **Condition Variables:**
   - `platform_cond_init()`, `platform_cond_wait()`, `platform_cond_signal()`
   - Enables efficient thread synchronization patterns

2. **Read-Write Locks:**
   - `platform_rwlock_init()`, `platform_rwlock_rdlock()`, `platform_rwlock_wrlock()`
   - Better performance for read-heavy workloads

3. **Thread-Local Storage:**
   - `platform_tls_create()`, `platform_tls_get()`, `platform_tls_set()`
   - Per-thread data without explicit passing

4. **Atomic Operations:**
   - `platform_atomic_add()`, `platform_atomic_cas()`
   - Lock-free data structures

5. **Process Operations:**
   - `platform_process_create()`, `platform_process_wait()`
   - Multi-process parallelism

6. **File I/O Abstraction:**
   - `platform_file_open()`, `platform_file_read()`, `platform_file_write()`
   - ✅ **Partially implemented:** Path handling via `platform_normalize_path()` and `platform_get_path_separator()`

7. **Network Sockets:**
   - `platform_socket_create()`, `platform_socket_connect()`
   - Distributed computing support

## See Also

- **README.md:** User documentation and usage examples
- **CLAUDE.md:** Platform abstraction architecture overview (section: Platform Abstraction Layer)
- **Makefile:** Build configuration for automatic platform detection
- **src/platform/*.h:** Header file API documentation

## References

### Windows API

- [CreateThread](https://docs.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread)
- [WaitForSingleObject](https://docs.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject)
- [CreateMutex](https://docs.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexw)
- [QueryPerformanceCounter](https://docs.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter)

### POSIX API

- [pthread_create](https://man7.org/linux/man-pages/man3/pthread_create.3.html)
- [pthread_join](https://man7.org/linux/man-pages/man3/pthread_join.3.html)
- [pthread_mutex_lock](https://man7.org/linux/man-pages/man3/pthread_mutex_lock.3p.html)
- [clock_gettime](https://man7.org/linux/man-pages/man3/clock_gettime.3.html)
