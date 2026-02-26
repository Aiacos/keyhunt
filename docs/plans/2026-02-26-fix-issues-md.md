# Fix All 8 ISSUES.md Audit Items — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Resolve all 8 issues identified in the comprehensive codebase audit (ISSUES.md), from quick bug fixes to architectural refactors.

**Architecture:** Work in dependency order — quick wins first (no risk), then safety improvements, then networking, then core refactors, then build system. Each phase is independently testable. All changes happen on the `refactor-pipeline` branch.

**Tech Stack:** C/C++17, CUDA, POSIX (poll), CMake 3.18+, std::atomic, intrinsics

---

## Phase 1: Quick Wins (No Risk)

### Task 1: Fix DEBUGCOUNT=0 — CPU throughput shows 0 keys/s (Issue 7)

**Files:**
- Modify: `src/keyhunt.cpp:637` (declaration)
- Modify: `src/keyhunt.cpp:2348-2349` (where BSGS_N is set from DEBUGCOUNT)

**Step 1: Fix DEBUGCOUNT initialization for non-BSGS modes**

In `src/keyhunt.cpp`, at line 2348, DEBUGCOUNT is used to set BSGS_N which is the multiplier for CPU throughput reporting. Currently DEBUGCOUNT=0 so CPU always reports 0 keys/s.

The fix: set DEBUGCOUNT to 1024 (the batch size, matching CPU_GRP_SIZE) for non-BSGS modes, right before it's used.

```cpp
// src/keyhunt.cpp:2348 — REPLACE this block:
if(FLAGMODE != MODE_BSGS && FLAGMODE != MODE_MINIKEYS)	{
    BSGS_N.SetInt32(DEBUGCOUNT);

// WITH:
if(FLAGMODE != MODE_BSGS && FLAGMODE != MODE_MINIKEYS)	{
    if(DEBUGCOUNT == 0) DEBUGCOUNT = 1024;
    BSGS_N.SetInt32(DEBUGCOUNT);
```

**Step 2: Verify the reporting loop uses BSGS_N correctly**

Check `src/keyhunt.cpp:1163-1167` — the reporting loop multiplies `steps[j].value * BSGS_N`. With DEBUGCOUNT=1024, each step now represents 1024 keys (matching the actual batch size in thread_process).

**Step 3: Build and test**

Run: `make clean && make -j$(nproc)`
Expected: Compiles without errors.

Run: `./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 2 -q -s 5`
Expected: CPU speed > 0 keys/s in output.

**Step 4: Commit**

```bash
git add src/keyhunt.cpp
git commit -m "fix: set DEBUGCOUNT=1024 for non-BSGS modes to fix CPU 0 keys/s reporting"
```

---

### Task 2: Replace volatile with std::atomic in gpu_config_t (Issue 2)

**Files:**
- Modify: `src/config/config.h:142-145`
- Modify: `src/config/config.cpp` (initialization)
- Modify: `src/gpu/gpu_backend_cuda.cu` (all read/write sites for these fields)
- Modify: `src/gpu/gpu_backend.h` (pointer types in gpu_search_config_t)

**Step 1: Update config.h — replace volatile with std::atomic**

```cpp
// src/config/config.h — REPLACE lines 142-145:
    /* Runtime stats (volatile, updated during search) */
    volatile uint64_t keys_checked;      /* Total GPU keys checked */
    volatile uint64_t keys_checked_cur;  /* Current block keys */
    volatile int      should_stop;       /* Signal GPU to stop */

// WITH:
    /* Runtime stats (atomic, updated during search) */
    std::atomic<uint64_t> keys_checked{0};      /* Total GPU keys checked */
    std::atomic<uint64_t> keys_checked_cur{0};  /* Current block keys */
    std::atomic<int>      should_stop{0};       /* Signal GPU to stop */
```

Add `#include <atomic>` at top of config.h if not already present. Note: config.h is C++ (included from .cpp files), so std::atomic is valid.

**Step 2: Update config.cpp initialization**

Remove explicit zero-initialization since std::atomic has in-class initializer:
```cpp
// Remove these lines from kh_gpu_config_init():
    cfg->keys_checked = 0;
    cfg->keys_checked_cur = 0;
    cfg->should_stop = 0;
// REPLACE with:
    cfg->keys_checked.store(0, std::memory_order_relaxed);
    cfg->keys_checked_cur.store(0, std::memory_order_relaxed);
    cfg->should_stop.store(0, std::memory_order_relaxed);
```

**Step 3: Update gpu_backend.h pointer types**

```cpp
// In gpu_search_config_t, change volatile pointers to atomic pointers:
// REPLACE:
    volatile uint64_t *keys_checked;
    volatile int *should_stop;
// WITH:
    std::atomic<uint64_t> *keys_checked;
    std::atomic<int> *should_stop;
```

**Step 4: Update gpu_backend_cuda.cu read/write sites**

All direct dereferences need `.load()` / `.store()`:
- Line 2530: `*(config->keys_checked) = total_keys;` → `config->keys_checked->store(total_keys, std::memory_order_release);`
- Line 2435: `int should_stop_val = *(config->should_stop);` → `int should_stop_val = config->should_stop->load(std::memory_order_acquire);`
- Line 2382: `while (!*(config->should_stop) && ...)` → `while (!config->should_stop->load(std::memory_order_acquire) && ...)`
- Line 1534: `while (keys_remaining > 0 && !(*should_stop))` — if `should_stop` is passed as raw `volatile int*` to inner functions, those function signatures need updating too. Check each call site.

**Step 5: Remove keys_checked_cur if truly unused**

Research shows `keys_checked_cur` is declared but never read. Remove the field entirely or keep as placeholder with a TODO comment.

**Step 6: Build and test**

Run: `make clean && make -j$(nproc)`
Run: `make test`
Expected: All tests pass. No warnings about volatile/atomic mismatch.

**Step 7: Commit**

```bash
git add src/config/config.h src/config/config.cpp src/gpu/gpu_backend.h src/gpu/gpu_backend_cuda.cu
git commit -m "fix: replace volatile with std::atomic in gpu_config_t for proper thread safety"
```

---

## Phase 2: Safety Improvements (Low Risk)

### Task 3: Add CUDA_CHECK macro and wrap all CUDA calls (Issue 3)

**Files:**
- Create: `src/gpu/cuda_check.h`
- Modify: `src/gpu/gpu_backend_cuda.cu` (wrap 20 unprotected calls)

**Step 1: Create CUDA_CHECK macro header**

```cpp
// src/gpu/cuda_check.h
#ifndef CUDA_CHECK_H
#define CUDA_CHECK_H

#ifdef HAVE_CUDA_BACKEND

#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>

#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = (call); \
        if (err != cudaSuccess) { \
            fprintf(stderr, "[CUDA ERROR] %s:%d: %s returned %s (%d)\n", \
                    __FILE__, __LINE__, #call, cudaGetErrorString(err), (int)err); \
            return -1; \
        } \
    } while (0)

// Variant for void functions (no return value)
#define CUDA_CHECK_VOID(call) \
    do { \
        cudaError_t err = (call); \
        if (err != cudaSuccess) { \
            fprintf(stderr, "[CUDA ERROR] %s:%d: %s returned %s (%d)\n", \
                    __FILE__, __LINE__, #call, cudaGetErrorString(err), (int)err); \
        } \
    } while (0)

// Variant for cleanup paths (log but continue)
#define CUDA_CHECK_WARN(call) \
    do { \
        cudaError_t err = (call); \
        if (err != cudaSuccess) { \
            fprintf(stderr, "[CUDA WARN] %s:%d: %s returned %s (%d)\n", \
                    __FILE__, __LINE__, #call, cudaGetErrorString(err), (int)err); \
        } \
    } while (0)

#endif // HAVE_CUDA_BACKEND
#endif // CUDA_CHECK_H
```

**Step 2: Include cuda_check.h in gpu_backend_cuda.cu**

Add `#include "cuda_check.h"` near the top includes.

**Step 3: Wrap all 20 unprotected CUDA calls**

Replace each bare call with the appropriate macro:

For functions returning int (error code):
- Lines 2037-2039: `cudaMalloc()` × 3 → `CUDA_CHECK(cudaMalloc(...))`
- Line 2042: `cudaStreamCreate()` → `CUDA_CHECK(cudaStreamCreate(...))`
- Lines 2050, 2058-2059: `cudaMemcpyAsync()` × 3 → `CUDA_CHECK(cudaMemcpyAsync(...))`
- Line 2061: `cudaStreamSynchronize()` → `CUDA_CHECK(cudaStreamSynchronize(...))`
- Lines 2151, 2154: `cudaMemcpyToSymbol()` × 2 → `CUDA_CHECK(cudaMemcpyToSymbol(...))`
- Line 2231: `cudaMemcpy()` → `CUDA_CHECK(cudaMemcpy(...))`
- Line 2330: `cudaMemcpyToSymbol()` → `CUDA_CHECK(cudaMemcpyToSymbol(...))`
- Lines 2436-2437: `cudaMemcpyAsync()` × 2 → `CUDA_CHECK_WARN(...)` (non-critical path)
- Lines 2496, 2500: `cudaMemcpyFromSymbol()` × 2 → `CUDA_CHECK(cudaMemcpyFromSymbol(...))`
- Line 2524: `cudaMemcpyToSymbol()` → `CUDA_CHECK(cudaMemcpyToSymbol(...))`
- Lines 2584, 2588: `cudaMemcpyFromSymbol()` × 2 → `CUDA_CHECK(cudaMemcpyFromSymbol(...))`

For cleanup paths:
- Line 1800: `cudaStreamSynchronize()` → `CUDA_CHECK_WARN(...)` (in cleanup)
- Line 2579: `cudaStreamSynchronize()` → `CUDA_CHECK_WARN(...)` (final cleanup)

For pinned memory:
- Lines 2014-2016: `cudaMallocHost()` × 3 → `CUDA_CHECK(cudaMallocHost(...))`

**Step 4: Remove now-redundant manual error checks**

Where the existing code already does `if (err != cudaSuccess)` followed by cleanup, replace with CUDA_CHECK since the macro does the same thing more consistently. Keep the cleanup logic (free, etc.) but wrap it differently:

```cpp
// BEFORE:
cudaError_t err = cudaMalloc(&ptr, size);
if (err != cudaSuccess) {
    fprintf(stderr, "...");
    return -1;
}
// AFTER:
CUDA_CHECK(cudaMalloc(&ptr, size));
```

**Step 5: Build and test**

Run: `make clean && make -j$(nproc)`
Run: `make test`

**Step 6: Commit**

```bash
git add src/gpu/cuda_check.h src/gpu/gpu_backend_cuda.cu
git commit -m "fix: add CUDA_CHECK macro and wrap all 31 CUDA API calls for systematic error detection"
```

---

### Task 4: Remove oldbloom redundancy (Issue 5)

**Files:**
- Modify: `src/search/search_context.h` (remove oldbloom include)
- Modify: `src/crypto/bloom_init.h` (remove oldbloom include)
- Modify: `src/keyhunt.cpp` (remove oldbloom_bP variable)
- Modify: `src/keyhunt_legacy.cpp` (update file loading to use bloom struct directly)
- Modify: `src/bsgsd.cpp` (update file loading to use bloom struct directly)
- Modify: `Makefile` (remove oldbloom from BLOOM_OBJS and OBJ_DIRS)
- Delete: `src/oldbloom/` directory

**Step 1: Understand the migration bridge**

The oldbloom struct has extra fields (checksum[32], checksum_backup[32], mutex) that new bloom doesn't have. The file loading code reads the old format, then memcpys the first sizeof(struct bloom) bytes. We need to keep the file format reading code but remove the oldbloom dependency.

Create a minimal struct for reading old-format files:

```cpp
// Add to src/bloom/bloom.h or a new header:
// Legacy bloom filter struct for reading old BSGS cache files
struct bloom_legacy_header {
    uint64_t entries, bits, bytes;
    uint8_t hashes;
    long double error;
    uint8_t ready, major, minor;
    double bpe;
    uint8_t checksum[32];
    uint8_t checksum_backup[32];
    // bf pointer and mutex follow but we only read the fixed fields
};
```

**Step 2: Update file loading in keyhunt_legacy.cpp**

Replace `fread(&oldbloom_bP, sizeof(struct oldbloom), ...)` with reading a `bloom_legacy_header`.

**Step 3: Update file loading in bsgsd.cpp**

Same changes as keyhunt_legacy.cpp.

**Step 4: Remove oldbloom_bP declaration from keyhunt.cpp**

Remove `struct oldbloom oldbloom_bP;` at line 777 and any includes.

**Step 5: Remove oldbloom include from search_context.h and bloom_init.h**

**Step 6: Update Makefile**

```makefile
# REPLACE:
BLOOM_OBJS := $(OBJDIR)/oldbloom/bloom.o $(OBJDIR)/bloom/bloom.o $(OBJDIR)/bloom/bloom_simd.o
# WITH:
BLOOM_OBJS := $(OBJDIR)/bloom/bloom.o $(OBJDIR)/bloom/bloom_simd.o
```

Remove `$(OBJDIR)/oldbloom` from `OBJ_DIRS`.

**Step 7: Delete oldbloom directory**

```bash
rm -rf src/oldbloom/
```

**Step 8: Build and test**

Run: `make clean && make -j$(nproc)`
Run: `make test`

**Step 9: Commit**

```bash
git add -A
git commit -m "refactor: remove oldbloom module, use bloom_legacy_header for old file format compatibility"
```

---

## Phase 3: Networking (Medium Risk)

### Task 5: Replace select() with poll() (Issue 4)

**Files:**
- Modify: `src/distributed/distributed.c` (replace select with poll)

**Step 1: Replace select() with poll() in dist_coordinator_process**

The current code only monitors one socket (listen_socket) with select(). Replace with poll():

```c
// REPLACE the entire select() block (lines 1309-1323) with:
#include <poll.h>

// In dist_coordinator_process():
    struct pollfd pfd;
    pfd.fd = coord->listen_socket;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ready = poll(&pfd, 1, timeout_ms);
    if (ready < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }

    if (ready > 0 && (pfd.revents & POLLIN)) {
        // ... existing accept() and handling code stays the same ...
    }
```

Remove `#include <sys/select.h>` if present, ensure `#include <poll.h>` is added.

**Step 2: Remove fd_set variables**

Remove `fd_set readfds;`, `FD_ZERO`, `FD_SET`, `FD_ISSET` — none needed with poll().
Remove `int maxfd` variable.
Remove `struct timeval tv` — poll takes timeout directly in ms.

**Step 3: Build and test**

Run: `make clean && make -j$(nproc)`
Run: `make test`

**Step 4: Commit**

```bash
git add src/distributed/distributed.c
git commit -m "refactor: replace select() with poll() to remove FD_SETSIZE limitation"
```

---

## Phase 4: Core Refactors (High Risk)

### Task 6: Remove Int class union — fix strict aliasing UB (Issue 1)

**Files:**
- Modify: `src/secp256k1/Int.h` (remove union, add accessors)
- Modify: `src/secp256k1/Int.cpp` (10 usages of .bits[])
- Modify: `src/secp256k1/IntMod.cpp` (5 usages of .bits[])
- Modify: `src/secp256k1/SECP256K1.cpp` (26 usages in KEYBUFFCOMP/KEYBUFFUNCOMP macros)
- Modify: `Makefile` (remove -fno-strict-aliasing from LTO_FLAGS)

**Step 1: Add inline accessors to Int.h**

Replace the union with single storage + accessors:

```cpp
// REPLACE the union (lines 180-183) with:
    uint64_t bits64[NB64BLOCK];

    // 32-bit accessor for backwards compatibility (replaces union .bits[])
    inline uint32_t getBits(int i) const {
        return static_cast<uint32_t>(bits64[i >> 1] >> ((i & 1) * 32));
    }
    inline void setBits(int i, uint32_t val) {
        int idx = i >> 1;
        int shift = (i & 1) * 32;
        bits64[idx] = (bits64[idx] & ~(0xFFFFFFFFULL << shift)) | ((uint64_t)val << shift);
    }
```

**Step 2: Replace all .bits[] in Int.cpp (10 usages)**

Each `x.bits[i]` read → `x.getBits(i)`
Each `x.bits[i] = val` write → `x.setBits(i, val)`

Example replacements:
- Line 580: `while(i>=0 && t.bits[i]==0) i--;` → `while(i>=0 && t.getBits(i)==0) i--;`
- Line 390: `b.bits[i] = ~a->bits[i-n];` → `b.setBits(i, ~a->getBits(i-n));`
- Line 392: `b.bits[i] = 0xFFFFFFFF;` → `b.setBits(i, 0xFFFFFFFF);`

**Step 3: Replace all .bits[] in IntMod.cpp (5 usages)**

All are read-only:
- Line 218: `int Q = _P.bits[0] & 3;` → `int Q = _P.getBits(0) & 3;`
- etc.

**Step 4: Replace all .bits[] in SECP256K1.cpp (26 usages in macros)**

Rewrite KEYBUFFCOMP and KEYBUFFUNCOMP macros to use getBits():

```cpp
#define KEYBUFFCOMP(buff,p) \
(buff)[0] = ((p).x.getBits(7) >> 8) | ((uint32_t)(0x2 + (p).y.IsOdd()) << 24); \
(buff)[1] = ((p).x.getBits(6) >> 8) | ((p).x.getBits(7) <<24); \
// ... same pattern for all 9 lines
```

Similarly for KEYBUFFUNCOMP (17 usages).

**Step 5: Remove -fno-strict-aliasing from Makefile**

```makefile
# REPLACE:
LTO_FLAGS ?= -flto=auto -fno-strict-aliasing
# WITH:
LTO_FLAGS ?= -flto=auto
```

Update the comment above to explain the union was removed.

**Step 6: Build and run full test suite**

Run: `make clean && make -j$(nproc)`
Run: `make test`
Expected: ALL tests pass (especially test_int, test_point, test_intgroup, test_hash).

**Step 7: Run functional test**

Run: `./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -q -s 10`
Expected: Finds keys correctly (same as before).

**Step 8: Run BSGS test**

Run: `./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10 -R`
Expected: BSGS mode works correctly.

**Step 9: Commit**

```bash
git add src/secp256k1/Int.h src/secp256k1/Int.cpp src/secp256k1/IntMod.cpp src/secp256k1/SECP256K1.cpp Makefile
git commit -m "fix: remove Int class union UB, add getBits/setBits accessors, enable strict aliasing"
```

---

### Task 7: Improve hybrid mode scheduler (Issue 6)

**Files:**
- Modify: `src/hybrid/adaptive_scheduler.c` (replace volatile with atomic, improve sync)

**Step 1: Replace volatile int with atomic in adaptive_scheduler**

The scheduler already uses `__sync_bool_compare_and_swap` for `update_in_progress`, which is essentially an atomic CAS. But `volatile int update_in_progress` should be `_Atomic int` (C11) or use `stdatomic.h`:

```c
// REPLACE:
volatile int update_in_progress;
// WITH:
#include <stdatomic.h>
atomic_int update_in_progress;
```

Update CAS calls:
```c
// REPLACE:
if (__sync_bool_compare_and_swap(&s->update_in_progress, 0, 1)) {
// WITH:
int expected = 0;
if (atomic_compare_exchange_strong(&s->update_in_progress, &expected, 1)) {
```

```c
// REPLACE:
__sync_synchronize();
s->update_in_progress = 0;
// WITH:
atomic_store(&s->update_in_progress, 0);
```

**Step 2: Build and test**

Run: `make clean && make -j$(nproc)`
Run: `make test`

**Step 3: Commit**

```bash
git add src/hybrid/adaptive_scheduler.c
git commit -m "fix: replace volatile+__sync builtins with C11 atomics in adaptive scheduler"
```

---

## Phase 5: Build System

### Task 8: Add CMake build system (Issue 8)

**Files:**
- Create: `CMakeLists.txt` (root)
- Keep: `Makefile` (as fallback — do NOT delete)

**Step 1: Create root CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.18)
project(keyhunt LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CPU architecture flags
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-march=native" HAS_MARCH_NATIVE)
if(HAS_MARCH_NATIVE)
    add_compile_options(-march=native -mtune=native -mssse3)
endif()

# Optimization
add_compile_options(-O3 -ftree-vectorize -funroll-loops -pipe -DNDEBUG)
add_compile_options(-Wall -Wextra)

# LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED)
if(LTO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# SIMD detection
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <immintrin.h>
int main() { __m256i a = _mm256_setzero_si256(); return 0; }
" HAS_AVX2)

check_cxx_source_compiles("
#include <immintrin.h>
int main() { __m512i a = _mm512_setzero_si512(); return 0; }
" HAS_AVX512)

# Source files by module
file(GLOB SECP256K1_SRCS src/secp256k1/*.cpp)
file(GLOB HASH_SRCS src/hash/ripemd160.cpp src/hash/ripemd160_sse.cpp
                    src/hash/sha256.cpp src/hash/sha256_sse.cpp src/hash/sha512.cpp)
file(GLOB BLOOM_SRCS src/bloom/bloom.cpp)
file(GLOB PLATFORM_SRCS src/platform/*.c)
file(GLOB BSGS_SRCS src/bsgs/*.cpp)
file(GLOB SEARCH_SRCS src/search/*.cpp)

# AVX2 sources (compile with -mavx2)
set(AVX2_SRCS
    src/hash/ripemd160_avx2.cpp
    src/hash/sha256_avx2.cpp
    src/hash/sha512_avx2.cpp
    src/bloom/bloom_simd.cpp
    src/bsgs/bsgs_ops.cpp
    src/bsgs/bsgs_fast.cpp
)

# AVX-512 sources
set(AVX512_SRCS
    src/hash/ripemd160_avx512.cpp
    src/hash/sha512_avx512.cpp
)

# SHA-NI sources
set(SHANI_SRCS src/hash/sha256_shani.cpp)

# Other modules
set(OTHER_SRCS
    src/base58/base58.cpp
    src/rmd160/rmd160.cpp
    src/xxhash/xxhash.c
    src/core/util.c src/core/sysinfo.c src/core/parameter_validator.c src/core/config.c
    src/config/config.cpp
    src/hybrid/adaptive_scheduler.c
    src/util/mempool.cpp
    src/distributed/distributed.c
    src/output.cpp src/progress.cpp src/benchmark.cpp src/cli.cpp
    src/sort/sort.cpp
    src/crypto/address_util.cpp src/crypto/bloom_init.cpp
    src/io/io.cpp
    src/sha3/sha3.c src/sha3/keccak.c
)

set(WIZARD_SRCS
    src/wizard/wizard.c src/wizard/wizard_config.c src/wizard/wizard_ui.c
    src/wizard/wizard_community.c src/wizard/wizard_server.c src/wizard/wizard_client.c
)

# Main executable
add_executable(keyhunt
    src/keyhunt.cpp
    ${SECP256K1_SRCS} ${HASH_SRCS} ${BLOOM_SRCS} ${PLATFORM_SRCS}
    ${BSGS_SRCS} ${SEARCH_SRCS} ${OTHER_SRCS} ${WIZARD_SRCS}
    ${AVX2_SRCS} ${AVX512_SRCS} ${SHANI_SRCS}
)

target_include_directories(keyhunt PRIVATE src)

# Set SIMD flags per source file
foreach(src ${AVX2_SRCS})
    set_source_files_properties(${src} PROPERTIES COMPILE_FLAGS "-mavx2")
endforeach()
foreach(src ${AVX512_SRCS})
    set_source_files_properties(${src} PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512dq")
endforeach()
foreach(src ${SHANI_SRCS})
    set_source_files_properties(${src} PROPERTIES COMPILE_FLAGS "-msha -msse4.1")
endforeach()

# C files that need C++ compilation
set_source_files_properties(
    src/core/util.c src/core/hashing.c src/sha3/sha3.c src/sha3/keccak.c
    PROPERTIES LANGUAGE CXX
)

# Link libraries
target_link_libraries(keyhunt PRIVATE m pthread dl)

# Optional CUDA
include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    target_sources(keyhunt PRIVATE src/gpu/gpu_backend_cuda.cu)
    target_compile_definitions(keyhunt PRIVATE HAVE_CUDA_BACKEND=1)
    set_target_properties(keyhunt PROPERTIES CUDA_ARCHITECTURES "75;86;89")
    target_link_libraries(keyhunt PRIVATE CUDA::cudart)
else()
    target_sources(keyhunt PRIVATE
        src/gpu/gpu_backend_none.cpp
        src/gpu/gpu_autotune.c
        src/gpu/multi_gpu_scheduler.c
        src/gpu/async_pipeline.c
    )
endif()

# Optional TLS
option(ENABLE_TLS "Enable TLS support" OFF)
if(ENABLE_TLS)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(keyhunt PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(keyhunt PRIVATE HAVE_OPENSSL)
endif()

# Tests
enable_testing()
file(GLOB TEST_SRCS tests/test_*.cpp)
add_executable(run_tests tests/run_tests.cpp ${TEST_SRCS}
    ${SECP256K1_SRCS} ${HASH_SRCS} ${BLOOM_SRCS} ${PLATFORM_SRCS}
    ${BSGS_SRCS} ${OTHER_SRCS} ${AVX2_SRCS} ${AVX512_SRCS} ${SHANI_SRCS}
)
target_include_directories(run_tests PRIVATE src)
target_link_libraries(run_tests PRIVATE m pthread dl)

# Apply same SIMD flags to test build
foreach(src ${AVX2_SRCS})
    set_source_files_properties(${src} PROPERTIES COMPILE_FLAGS "-mavx2")
endforeach()

add_test(NAME unit_tests COMMAND run_tests)
```

**Step 2: Test CMake build**

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest --output-on-failure
```

**Step 3: Verify both build systems work**

```bash
# CMake
cd build && cmake --build . -j$(nproc) && ctest
# Makefile (from project root)
cd .. && make clean && make -j$(nproc) && make test
```

**Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat: add CMake build system alongside Makefile for better CUDA/GCC compatibility"
```

---

## Final Verification

### Task 9: Full regression test and cleanup

**Step 1: Clean build from scratch**

```bash
make clean && make -j$(nproc)
```

**Step 2: Run all unit tests**

```bash
make test
```

**Step 3: Run functional tests**

```bash
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -q -s 5
./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10 -R
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -l compress -R -q -s 5
./keyhunt -m xpoint -f tests/120.txt -t 4 -b 125 -R -q -s 5
```

**Step 4: Update ISSUES.md with resolution status**

Mark each issue as RESOLVED with the commit hash.

**Step 5: Final commit**

```bash
git add ISSUES.md
git commit -m "docs: mark all 8 audit issues as resolved in ISSUES.md"
```

---

## Execution Order Summary

| Task | Issue | Risk | Dependencies |
|------|-------|------|--------------|
| 1 | DEBUGCOUNT=0 (#7) | None | — |
| 2 | volatile→atomic (#2) | Low | — |
| 3 | CUDA_CHECK (#3) | Low | — |
| 4 | Remove oldbloom (#5) | Low | — |
| 5 | select→poll (#4) | Medium | — |
| 6 | Int union removal (#1) | **HIGH** | — |
| 7 | Hybrid scheduler (#6) | Medium | Task 2 |
| 8 | CMake (#8) | Low | Tasks 4, 6 (file changes) |
| 9 | Final verification | — | All |

Tasks 1-5 can be parallelized. Task 6 is highest risk and should be done carefully with full test verification. Task 8 should be done last since it references the final file layout.
