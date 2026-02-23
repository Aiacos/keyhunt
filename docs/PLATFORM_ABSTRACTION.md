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
└── platform_time.h/c       # High-resolution monotonic time
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
   - Unified file path handling (Windows `\` vs POSIX `/`)

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
