# Phase 2: Sanitizer Coverage - Research

**Researched:** 2026-03-01
**Domain:** C/C++ sanitizer instrumentation (ASan, UBSan, TSan), memory safety, data race elimination
**Confidence:** HIGH

## Summary

Phase 2 requires achieving zero findings from three independent sanitizer builds (ASan+UBSan, TSan) across the full test suite. The existing Makefile already has `sanitize` and `tsan` targets with correct flags, but they currently fail to link because the sanitizer runtime libraries (`libasan`, `libtsan`, `libubsan`) are not installed on the build system. Once the libraries are present, the real work begins: fixing actual memory errors, undefined behavior, and data races that the sanitizers will discover.

Four specific requirements must be addressed: MEM-01 (ASan+UBSan clean), MEM-02 (TSan clean), MEM-03 (replace `volatile` with `std::atomic` in `gpu_config_t`), and MEM-04 (eliminate duplicate `g_avx2_available` in `bsgs_fast.cpp`). The codebase has extensive `volatile` usage patterns that TSan will flag as data races, a duplicate CPU feature detection variable that creates dispatch divergence, and no multi-threaded test cases for TSan to exercise.

**Primary recommendation:** Install sanitizer runtime packages, fix the four known issues (volatile->atomic, duplicate g_avx2_available, remaining volatile in search context), then iteratively build and fix until both `make sanitize` and `make tsan` run the full test suite with zero findings.

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| MEM-01 | ASan + UBSan clean build with zero findings on full test suite | Makefile `sanitize` target exists with correct flags (`-fsanitize=address -fsanitize=undefined`). Currently fails to link -- needs `libasan`, `libubsan` packages. SIMD code may produce false positives requiring targeted `__attribute__((no_sanitize))` annotations. |
| MEM-02 | TSan clean build with zero findings on multi-threaded search tests | Makefile `tsan` target exists with correct flags (`-fsanitize=thread`). Currently fails to link -- needs `libtsan` package. No multi-threaded tests exist currently; need to create minimal threaded test cases. Multiple `volatile` variables will trigger TSan race reports until converted to `std::atomic`. |
| MEM-03 | Replace volatile with std::atomic for GPU counters in config.h (lines 154-157) | Three volatile fields identified: `keys_checked` (uint64_t), `keys_checked_cur` (uint64_t), `should_stop` (int). Must become `std::atomic` in C++ context. The header is C/C++ dual-use via `#ifdef __cplusplus` -- requires conditional compilation for atomic types. `gpu_backend.h` already uses `std::atomic` in C++ mode with `volatile` fallback in C mode -- this pattern should be followed. |
| MEM-04 | Fix duplicate g_avx2_available in bsgs_fast.cpp (unified SIMD dispatch) | `bsgs_fast.cpp:20` has `static bool g_avx2_available` with its own `detect_cpu_features()` via `__get_cpuid_count`. `keyhunt.cpp:161` has global `bool g_avx2_available` set via `ripemd160_avx2_available()`. `search_address.cpp:52` and `search_vanity.cpp:43` extern the keyhunt.cpp version. These can diverge. Solution: unify to use `sysinfo.c` detection results, pass through config or a single global, delete the bsgs_fast.cpp local copy. |
</phase_requirements>

## Standard Stack

### Core

| Tool | Version | Purpose | Why Standard |
|------|---------|---------|--------------|
| GCC | 15.2.1 (system) | Primary compiler with ASan/UBSan/TSan support | Already used by the project; GCC 15 has mature sanitizer support |
| libasan | 15.2.1-7.fc43 | AddressSanitizer runtime library | Required by GCC's `-fsanitize=address`; detects buffer overflows, use-after-free, memory leaks |
| libubsan | 15.2.1-7.fc43 | UndefinedBehaviorSanitizer runtime library | Required by GCC's `-fsanitize=undefined`; detects signed overflow, null deref, alignment violations, shift errors |
| libtsan | 15.2.1-7.fc43 | ThreadSanitizer runtime library | Required by GCC's `-fsanitize=thread`; detects data races in multi-threaded code |

### Supporting

| Tool | Version | Purpose | When to Use |
|------|---------|---------|-------------|
| Clang | 21.1.8 (system) | Alternative compiler for sanitizer cross-validation | If GCC ASan produces false positives on SIMD; needs `compiler-rt` 21 package installed |
| Valgrind | N/A | Alternative memory checker for SIMD-heavy code | As a fallback if ASan produces intractable false positives on AVX2/AVX-512 aligned stack variables |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| GCC sanitizers | Clang sanitizers (via compiler-rt) | Clang's sanitizer implementation is often considered more mature (Google authors). However, Clang 21 on this system also lacks the runtime (`compiler-rt` package not installed for v21). GCC 15 is equally capable and is the project's primary compiler. Use GCC unless specific false positives force a switch. |
| `std::atomic` for volatile replacement | GCC `__atomic` builtins | `__atomic_*` builtins are already used in `bsgs_fast.cpp` and `adaptive_scheduler.c`. For C-only files, `__atomic` builtins are the correct choice. For C++ files and the C/C++ dual-use header `config.h`, `std::atomic` is cleaner and TSan understands it natively. Use `std::atomic` in C++ code, `__atomic` builtins in pure C code. |

**Installation (prerequisite):**
```bash
sudo dnf install libasan libtsan libubsan
```

## Architecture Patterns

### Current Sanitizer Build Structure

The Makefile already separates sanitizer builds into isolated object directories to avoid cross-contamination with production builds:

```
obj/           # Production build objects
obj_asan/      # ASan+UBSan build objects
obj_tsan/      # TSan build objects
obj_coverage/  # Coverage build objects
```

Each sanitizer target disables LTO (`LTO_FLAGS=""`), GPU backends (`GPU_OBJS` uses `gpu_backend_none.o`), and GPU compile flags (`GPU_CXXFLAGS=""`). This is correct -- LTO can interfere with sanitizer instrumentation, and GPU code cannot be sanitized without CUDA/OpenCL sanitizer support.

### Pattern 1: Volatile-to-Atomic Migration in C/C++ Dual-Use Headers

**What:** The `gpu_config_t` struct in `config/config.h` uses `volatile` for three fields that are shared between threads. This header is included by both C and C++ translation units.

**When to use:** Any struct field that is read/written from multiple threads in a C/C++ dual-use header.

**Example (following the existing pattern from `gpu_backend.h:70-76`):**
```c
/* In config/config.h */
typedef struct {
    /* ... other fields ... */

    /* Runtime stats (thread-safe via atomics) */
#ifdef __cplusplus
    std::atomic<uint64_t> keys_checked;
    std::atomic<uint64_t> keys_checked_cur;
    std::atomic<int>      should_stop;
#else
    /* C compilation path: use _Atomic if C11+, volatile fallback */
    _Atomic uint64_t keys_checked;
    _Atomic uint64_t keys_checked_cur;
    _Atomic int      should_stop;
#endif
    /* ... */
} gpu_config_t;
```

**Critical constraint:** `std::atomic<T>` and `_Atomic T` have the same layout and alignment on all major compilers (x86-64). However, `sizeof(gpu_config_t)` may change if alignment differs. Since the struct is never serialized or shared across ABI boundaries in this codebase, this is safe.

**Alternative approach (simpler, if C code never directly accesses these fields):**
Since `gpu_config_t` fields `keys_checked`, `keys_checked_cur`, and `should_stop` are primarily accessed from C++ code (keyhunt.cpp, search modules), and the C code paths in gpu_backend use their own `volatile` local copies or atomic builtins, it may be simpler to conditionally use `std::atomic` in C++ and keep `volatile` only for C, as `gpu_backend.h` already does.

### Pattern 2: Eliminating Duplicate SIMD Detection

**What:** Replace module-local CPU feature detection with a single source of truth.

**Current state:**
- `src/bsgs/bsgs_fast.cpp:20`: `static bool g_avx2_available` with own `detect_cpu_features()` via `__get_cpuid_count`
- `src/keyhunt.cpp:161`: `bool g_avx2_available` set via `ripemd160_avx2_available()`
- `src/keyhunt.cpp:1695`: `g_avx2_available = ripemd160_avx2_available();`
- `src/search/search_address.cpp:52`: `extern bool g_avx2_available;`
- `src/search/search_vanity.cpp:43`: `extern bool g_avx2_available;`
- `src/config/config.h:223`: `autotune_config_t.has_avx2` (already exists but not wired)
- `src/core/sysinfo.c`: Already detects AVX2 via `/proc/cpuinfo` parsing

**Solution:** In `bsgs_fast.cpp`, remove the `static bool g_avx2_available` and `detect_cpu_features()`. Replace with either:
1. A setter function `bsgs_fast_set_avx2(bool avail)` called from `main()` after sysinfo detection, or
2. An extern reference to the keyhunt.cpp global (short-term, will be cleaned in Phase 3 config migration)

Option 1 is preferred because it maintains the encapsulation of bsgs_fast.cpp while eliminating the duplicate detection.

### Pattern 3: TSan-Compatible Volatile Replacement for bsgs_found

**What:** The `volatile int *bsgs_found` array is allocated in `keyhunt.cpp:2789` and accessed from multiple BSGS threads. Each element is per-target (not shared between threads for the same index), but multiple threads read all elements to compute `salir &= bsgs_found[l]`.

**Solution:** Replace with `std::atomic<int> *bsgs_found` allocated via `new std::atomic<int>[N]{}`. Read with `bsgs_found[k].load(std::memory_order_relaxed)`, write with `bsgs_found[k].store(1, std::memory_order_release)`. The `salir` accumulation loop should use `memory_order_acquire` for reads.

### Pattern 4: THREADOUTPUT Volatile Replacement

**What:** `volatile int THREADOUTPUT` is a cross-thread signal flag (set to 1 by workers, read/reset to 0 by main thread).

**Solution:** Replace with `std::atomic<int> THREADOUTPUT{0}`. All reads become `.load(std::memory_order_acquire)`, all writes become `.store(value, std::memory_order_release)`. The `volatile` keyword is removed from the declaration and all `extern volatile int THREADOUTPUT` declarations.

### Anti-Patterns to Avoid

- **Suppressing sanitizer findings instead of fixing them:** The success criterion explicitly states "not suppressed, actually fixed." Only use `__attribute__((no_sanitize))` for confirmed false positives with documented justification.
- **Adding `-fno-sanitize=X` to individual files:** This hides real bugs. Instead, annotate specific functions.
- **Running sanitizers with `-O0`:** The Makefile already uses `-O1` which is correct -- `-O0` can produce different behavior than production code and miss some UBSan findings. `-O1` preserves most optimizations sanitizers need to see.
- **Mixing ASan and TSan in the same build:** These two sanitizers are mutually exclusive and cannot be combined. The Makefile correctly uses separate targets.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Cross-thread counters | Custom lock+increment | `std::atomic<uint64_t>` with `memory_order_relaxed` | `std::atomic` generates optimal hardware-level atomic instructions (lock xadd on x86); manual locks add contention |
| Cross-thread stop signal | `volatile int` flag | `std::atomic<int>` with `memory_order_seq_cst` | The stop signal is a "happens-before" relationship; `volatile` provides no ordering guarantees; TSan will flag it |
| CPU feature detection | Per-module `__get_cpuid_count` | `sysinfo.c::detect_cpu_features()` centralized | One source of truth eliminates divergence; already exists and populates `system_info_t` |
| Sanitizer suppression files | Custom awk/sed to filter output | GCC `__attribute__((no_sanitize("address")))` per-function | Build-time annotations are more maintainable than runtime suppression files; they stay with the code |

**Key insight:** The primary work in this phase is NOT building new infrastructure -- it's fixing existing code that has latent memory errors, UB, and data races. The sanitizers will find the bugs; the work is in understanding and fixing each one correctly.

## Common Pitfalls

### Pitfall 1: Sanitizer Runtime Libraries Not Installed

**What goes wrong:** `make sanitize` and `make tsan` compile all objects successfully but fail at link time with "cannot find libasan.so.8.0.0" (Italian: "impossibile trovare").
**Why it happens:** GCC 15 on Fedora 43 installs the compiler but not the sanitizer runtime libraries. The GCC library directory has a `libasan.so` linker script that points to `/usr/lib64/libasan.so.8.0.0`, which doesn't exist until the `libasan` package is installed.
**How to avoid:** Install packages before attempting sanitizer builds: `sudo dnf install libasan libtsan libubsan`
**Warning signs:** Link errors mentioning `libasan.so`, `libtsan.so`, or `libubsan.so`. Build compiles all `.o` files successfully but fails at the final link step.
**Confidence:** HIGH (verified by direct build attempt on this system)

### Pitfall 2: ASan False Positives on SIMD-Aligned Stack Buffers

**What goes wrong:** ASan instruments stack variables and checks shadow memory at 8-byte granularity. AVX2 code uses 32-byte aligned stack buffers (`__attribute__((aligned(32)))`). When ASan's shadow bytes don't perfectly match the wider alignment, legitimate SIMD loads may trigger `stack-buffer-overflow` reports on correctly aligned arrays.
**Why it happens:** ASan's shadow memory architecture was designed for scalar code. It can express 1-8 byte alignment but not 32-byte or 64-byte. This is a known limitation.
**How to avoid:** For each ASan false positive on a SIMD function, verify it's false by checking the alignment manually, then add `__attribute__((no_sanitize("address")))` to ONLY that specific function with a comment explaining why. Do not suppress entire files.
**Warning signs:** ASan reports `stack-buffer-overflow` pointing to a line that does `_mm256_load_si256` from a stack variable.
**Confidence:** HIGH (documented in PITFALLS.md, confirmed by Clang ASan documentation and GCC Launchpad bug #2023424)

### Pitfall 3: TSan Reports on Volatile Variables That "Work" on x86

**What goes wrong:** TSan reports data races on `volatile uint64_t keys_checked` and `volatile int should_stop` in `gpu_config_t`, and on `volatile int THREADOUTPUT`, and on `volatile int *bsgs_found`. These reads/writes are atomic on x86 due to hardware guarantees, but TSan doesn't know that -- it only recognizes `std::atomic`, `__atomic_*` builtins, or proper mutex synchronization.
**Why it happens:** `volatile` tells the compiler not to cache the value but provides no atomicity or memory ordering. TSan instruments every memory access and flags any access to a location that is also written from another thread without proper synchronization.
**How to avoid:** Replace all cross-thread `volatile` with `std::atomic`. This is not optional for TSan compliance.
**Warning signs:** TSan output floods with hundreds of race reports on the same few variables.
**Confidence:** HIGH (verified by codebase inspection; every `volatile` in cross-thread context will produce a TSan finding)

### Pitfall 4: No Multi-Threaded Tests for TSan to Exercise

**What goes wrong:** The current test suite is entirely single-threaded (unit tests). TSan only detects races on code paths that are actually executed concurrently. Without multi-threaded tests, TSan passes vacuously -- zero findings because zero concurrent accesses occurred.
**Why it happens:** The test suite was built for functional correctness, not concurrency testing.
**How to avoid:** Create minimal multi-threaded test cases that exercise the patterns TSan needs to see: (1) main thread + worker thread sharing `steps[]` counters, (2) multiple BSGS threads sharing `bsgs_found[]`, (3) any code path that reads/writes `THREADOUTPUT`. These don't need to be full search operations -- just enough to trigger the concurrent access patterns.
**Warning signs:** `make tsan` passes on first try with zero findings and zero threads created beyond main.
**Confidence:** HIGH (verified by inspecting the test suite -- no `platform_thread_create` or `pthread_create` calls in test files)

### Pitfall 5: UBSan Flags Signed Left Shift in Third-Party Code (sqlite3)

**What goes wrong:** The bundled `sqlite3.c` (200K+ lines) may contain UBSan-flagged patterns like signed integer overflow, left shift of negative value, etc. These are in third-party code that we don't control.
**Why it happens:** SQLite is compiled as an amalgamation; we cannot patch it without forking.
**How to avoid:** Compile `sqlite3.o` with `-fno-sanitize=undefined` or add a specific UBSan suppression for the sqlite3 file. The Makefile already has a special rule for sqlite3 (`-Wno-implicit-fallthrough`); extend it with UBSan exemption. Document that sqlite3 is third-party and excluded from UBSan.
**Warning signs:** UBSan reports pointing to `src/database/sqlite3.c` line numbers.
**Confidence:** MEDIUM (sqlite3 is known to have UBSan-flagged patterns in various builds, but not verified on this specific version)

### Pitfall 6: Memory Order Semantics for bsgs_found Stop Condition

**What goes wrong:** The BSGS threads use `bsgs_found[k]` as a per-target "found" flag. One thread writes `bsgs_found[k] = 1`, and all threads read it in their tight loop condition `while(... && bsgs_found[k] == 0)`. With `memory_order_relaxed`, a thread might not see the write immediately, causing it to do extra unnecessary work (but not produce wrong results). With `memory_order_seq_cst`, there's unnecessary overhead in the tight inner loop.
**Why it happens:** Choosing the right memory order requires understanding the correctness requirements.
**How to avoid:** Use `memory_order_relaxed` for the read in the loop condition (delayed visibility is acceptable -- the thread just does a few extra iterations). Use `memory_order_release` for the write (ensures the found key data is visible before the flag). Use `memory_order_acquire` for the final `salir &= bsgs_found[l]` accumulation after the loop (ensures all found keys are visible).
**Warning signs:** Performance regression in BSGS mode after adding atomics (means `seq_cst` was used everywhere instead of `relaxed`).
**Confidence:** HIGH (verified by reading the bsgs_found usage pattern in search_bsgs_threads.cpp)

## Code Examples

### Example 1: gpu_config_t volatile-to-atomic (MEM-03)

```c
/* Before (config/config.h lines 154-157) */
typedef struct {
    /* ... */
    volatile uint64_t keys_checked;
    volatile uint64_t keys_checked_cur;
    volatile int      should_stop;
    /* ... */
} gpu_config_t;

/* After */
typedef struct {
    /* ... */
    /* Runtime stats (atomic for cross-thread visibility) */
#ifdef __cplusplus
    std::atomic<uint64_t> keys_checked;
    std::atomic<uint64_t> keys_checked_cur;
    std::atomic<int>      should_stop;
#else
    _Atomic uint64_t keys_checked;
    _Atomic uint64_t keys_checked_cur;
    _Atomic int      should_stop;
#endif
    /* ... */
} gpu_config_t;
```

Note: The `#include <atomic>` is already present at the top of `gpu_backend.h` inside `#ifdef __cplusplus`. It must also be added to `config/config.h` inside the C++ guard, or `<stdatomic.h>` added for C11.

### Example 2: Eliminating bsgs_fast.cpp Duplicate (MEM-04)

```cpp
/* Before (bsgs_fast.cpp:20-31) */
static bool g_avx2_available = false;
static bool g_avx512_available = false;

static void detect_cpu_features() {
#ifdef __linux__
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        g_avx2_available = (ebx & (1 << 5)) != 0;
        g_avx512_available = (ebx & (1 << 16)) != 0;
    }
#endif
}

/* After: Use setter function called from main after sysinfo detection */
static bool g_bsgs_avx2 = false;
static bool g_bsgs_avx512 = false;

extern "C" {
void bsgs_fast_set_cpu_features(bool avx2, bool avx512) {
    g_bsgs_avx2 = avx2;
    g_bsgs_avx512 = avx512;
}

int bsgs_fast_init(void) {
    if (g_initialized) return 0;
    /* No more local detect_cpu_features() -- caller sets features */
    memset(&g_stats, 0, sizeof(g_stats));
    g_initialized = true;
    return 0;
}
```

Then in keyhunt.cpp, after sysinfo detection:
```cpp
g_avx2_available = ripemd160_avx2_available();
bsgs_fast_set_cpu_features(g_avx2_available, g_sysinfo.has_avx512);
```

### Example 3: THREADOUTPUT atomic replacement

```cpp
/* Before */
volatile int THREADOUTPUT = 0;          /* keyhunt.cpp:606 */
extern volatile int THREADOUTPUT;       /* search_context.h:155 */

/* After */
std::atomic<int> THREADOUTPUT{0};       /* keyhunt.cpp:606 */
extern std::atomic<int> THREADOUTPUT;   /* search_context.h:155 */

/* Usage (search_bsgs_threads.cpp) */
/* Before: */ THREADOUTPUT = 1;
/* After:  */ THREADOUTPUT.store(1, std::memory_order_release);
/* Before: */ if(THREADOUTPUT == 1)
/* After:  */ if(THREADOUTPUT.load(std::memory_order_acquire) == 1)
```

### Example 4: bsgs_found atomic replacement

```cpp
/* Before (keyhunt.cpp:835, 2789) */
volatile int *bsgs_found;
bsgs_found = (int*) calloc(N, sizeof(int));

/* After */
std::atomic<int> *bsgs_found;
bsgs_found = new std::atomic<int>[N]{};  /* Zero-initialized */

/* Usage in search_bsgs_threads.cpp tight loops: */
/* Before: */ while(j < cycles && bsgs_found[k] == 0)
/* After:  */ while(j < cycles && bsgs_found[k].load(std::memory_order_relaxed) == 0)

/* Write: */
/* Before: */ bsgs_found[k] = 1;
/* After:  */ bsgs_found[k].store(1, std::memory_order_release);

/* Accumulation: */
/* Before: */ salir &= bsgs_found[l];
/* After:  */ salir &= bsgs_found[l].load(std::memory_order_acquire);

/* Cleanup */
/* Before: */ free(bsgs_found);
/* After:  */ delete[] bsgs_found;
```

### Example 5: Minimal Multi-Threaded TSan Test

```cpp
/* tests/test_threading.cpp -- minimal test for TSan validation */
#include "test_framework.h"
#include "platform/platform.h"
#include <atomic>

static std::atomic<int> shared_counter{0};
static std::atomic<int> stop_flag{0};

static PLATFORM_THREAD_RETURN_TYPE PLATFORM_THREAD_CALL worker_func(void *arg) {
    (void)arg;
    while (stop_flag.load(std::memory_order_acquire) == 0) {
        shared_counter.fetch_add(1, std::memory_order_relaxed);
    }
    return (PLATFORM_THREAD_RETURN_TYPE)0;
}

TEST(threading_atomic_counter_no_race) {
    platform_thread_t t1, t2;
    stop_flag.store(0, std::memory_order_release);
    shared_counter.store(0, std::memory_order_release);

    platform_thread_create(&t1, worker_func, NULL);
    platform_thread_create(&t2, worker_func, NULL);

    /* Let threads run briefly */
    platform_time_now_ns(); /* Touch the clock */
    for (volatile int i = 0; i < 100000; i++) {} /* Spin briefly */

    stop_flag.store(1, std::memory_order_release);
    platform_thread_join(t1, NULL);
    platform_thread_join(t2, NULL);

    ASSERT_TRUE(shared_counter.load() > 0);
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `volatile` for cross-thread visibility | `std::atomic` with explicit memory ordering | C++11 (2011), widespread adoption ~2015 | TSan recognizes atomics; `volatile` is only for memory-mapped I/O |
| GCC ASan without UBSan | Combined `-fsanitize=address,undefined` | GCC 5+ (2015) | Catches both memory errors AND undefined behavior in one pass |
| Separate ASan and TSan CI jobs | Still separate (mutually exclusive) | Always | ASan and TSan cannot be combined; they use incompatible shadow memory layouts |
| MSan for uninitialized reads | Avoid MSan for SIMD code; use Valgrind instead | N/A | MSan produces false positives on SIMD widening operations; explicitly out of scope (ADV-01) |

**Deprecated/outdated:**
- **`volatile` for thread synchronization:** Never correct per C++11 standard; provides no atomicity or ordering. The project already uses `std::atomic` in newer code (keyhunt.cpp:627, 685, 689-691) but retains `volatile` in older code paths.
- **MSan in CI:** Explicitly deferred to v2 (ADV-01) because it requires full dependency recompilation and produces false positives on SIMD.

## Open Questions

1. **ASan heap overflow in hash padding**
   - What we know: `sha256_33`/`sha256_65` write padding in-place; buffers must be 64/128 bytes per MEMORY.md. If any caller passes a shorter buffer, ASan will catch it.
   - What's unclear: Whether all callers allocate sufficient buffer sizes. ASan will reveal this.
   - Recommendation: Run ASan build, catalog all findings, fix each one. This is the discovery process.

2. **sqlite3 UBSan compatibility**
   - What we know: sqlite3 is a 220K-line third-party amalgamation compiled with special flags already.
   - What's unclear: Whether the bundled version triggers UBSan findings.
   - Recommendation: Compile sqlite3.o without `-fsanitize=undefined` in the sanitizer target. If it's clean, remove the exemption. This is pragmatic -- we cannot fix sqlite3.

3. **UBSan findings in IntMod.cpp carry chain code**
   - What we know: `IntMod.cpp:875` and `:954` use `volatile unsigned char c` for older GCC compatibility (before 7.3). With GCC 15, the `volatile` is not needed.
   - What's unclear: Whether the `_addcarry_u64` intrinsic chain has any UBSan-visible undefined behavior (e.g., signed overflow in intermediate values).
   - Recommendation: These `volatile unsigned char c` are not cross-thread -- they're compiler hints for carry chain register allocation. They will not trigger TSan. UBSan may or may not flag them; run and see.

4. **Makefile target naming: `sanitize` vs `asan`**
   - What we know: The ROADMAP success criterion says "`make asan`" but the Makefile target is `sanitize`. Similarly for `make tsan` which exists.
   - What's unclear: Whether to rename the Makefile target or update the ROADMAP.
   - Recommendation: Add `asan` as an alias for `sanitize` in the Makefile (one line: `asan: sanitize`). This satisfies the ROADMAP wording without breaking existing scripts.

5. **Scope of volatile replacement beyond gpu_config_t**
   - What we know: MEM-03 specifically calls out `gpu_config_t` lines 154-157. But there are additional `volatile` patterns that TSan will flag: `THREADOUTPUT`, `bsgs_found`, `should_stop` in `gpu_multi_worker.c`, `update_in_progress` in `adaptive_scheduler.h`.
   - What's unclear: Whether the scope of MEM-01/MEM-02 (zero findings) implicitly requires fixing ALL volatile-related TSan reports, or just the ones in gpu_config_t.
   - Recommendation: MEM-01 and MEM-02 require ZERO findings. Any volatile that causes a TSan race report must be fixed to achieve zero. MEM-03 calls out the specific lines, but MEM-02's zero-findings requirement captures everything else.

## Inventory of Volatile Cross-Thread Variables Requiring Conversion

This inventory is critical for planning -- each item must be converted to achieve MEM-02 (zero TSan findings).

### In scope (src/ non-legacy, non-backup, non-third-party):

| Variable | File | Declaration | Used By | Proposed Change |
|----------|------|-------------|---------|----------------|
| `gpu_config_t.keys_checked` | config/config.h:155 | `volatile uint64_t` | keyhunt.cpp, GPU backends | `std::atomic<uint64_t>` (MEM-03) |
| `gpu_config_t.keys_checked_cur` | config/config.h:156 | `volatile uint64_t` | keyhunt.cpp, GPU backends | `std::atomic<uint64_t>` (MEM-03) |
| `gpu_config_t.should_stop` | config/config.h:157 | `volatile int` | keyhunt.cpp, GPU backends | `std::atomic<int>` (MEM-03) |
| `THREADOUTPUT` | keyhunt.cpp:606 | `volatile int` | search_context.h, search_bsgs_threads.cpp, search_address.cpp, search_vanity.cpp | `std::atomic<int>` |
| `bsgs_found` | keyhunt.cpp:835 | `volatile int *` | search_context.h, search_bsgs_threads.cpp (5 thread functions) | `std::atomic<int> *` |
| `should_stop` | gpu_multi_worker.c:32 | `volatile int` | GPU worker threads | `_Atomic int` or `__atomic` builtins (C file) |
| `paused` | gpu_multi_worker.c:33 | `volatile bool` | GPU worker threads | `_Atomic bool` or `__atomic` builtins |
| `has_result` | gpu_multi_worker.c:34 | `volatile bool` | GPU worker threads | `_Atomic bool` or `__atomic` builtins |
| `update_in_progress` | adaptive_scheduler.h:61 | `volatile int` | Already uses `__atomic` builtins for access | Remove `volatile` keyword; `__atomic` builtins are sufficient |
| `keys_processed` | gpu_backend_opencl.c:128 | `volatile uint64_t` | OpenCL worker | `_Atomic uint64_t` |
| `g_sigint_received` | keyhunt.cpp:697 | `volatile sig_atomic_t` | Signal handler | KEEP: `volatile sig_atomic_t` is the correct type for signal handlers per POSIX |

### Out of scope (legacy/third-party/inline-asm):

| Variable | File | Reason for exclusion |
|----------|------|---------------------|
| Various in `sqlite3.c` | database/sqlite3.c | Third-party code; do not modify |
| Various in `keyhunt_legacy.cpp` | keyhunt_legacy.cpp | Legacy binary; not in scope for hardening |
| `__asm__ volatile` in hash/*.cpp | hash/ | Inline assembly `volatile` prevents reordering; not a data race |
| `sha3_explicit_memset_impl` | sha3/sha3.c | Anti-optimization pattern for secure memory wiping; correct use of volatile |
| Various in wizard_client.c/wizard_server.c | wizard/ | Signal handler patterns; `volatile sig_atomic_t` is correct |
| `g_reconnect_thread_running` | distributed/distributed.c | Single-writer pattern; but may need conversion if TSan flags it |

### Borderline (may or may not trigger TSan depending on test coverage):

| Variable | File | Notes |
|----------|------|-------|
| `g_client_running` | wizard/wizard_client.c:66 | `volatile sig_atomic_t` -- POSIX-correct for signal handlers |
| `g_keys_since_heartbeat` | wizard/wizard_client.c:81 | `volatile uint64_t` -- cross-thread, but wizard code is not exercised by unit tests |
| `g_reconnect_thread_running` | distributed/distributed.c:363 | `volatile int` -- cross-thread, but distributed code not exercised by unit tests |

## Estimated Effort Breakdown

| Task | Estimated Size | Complexity | Risk |
|------|---------------|------------|------|
| Install sanitizer packages | Trivial | None | None |
| Add `make asan` alias | Trivial | None | None |
| Replace volatile in gpu_config_t (MEM-03) | Small | Low (3 fields, follow gpu_backend.h pattern) | Low |
| Eliminate bsgs_fast.cpp g_avx2_available (MEM-04) | Small | Low (remove local, add setter) | Low |
| Replace volatile THREADOUTPUT | Small | Low (one global, 3 extern sites, ~10 use sites) | Low |
| Replace volatile bsgs_found | Medium | Medium (pointer to dynamic array, 30+ use sites in tight loops) | Medium (performance) |
| Replace volatile in gpu_multi_worker.c | Small | Low (3 fields, pure C file uses __atomic builtins) | Low |
| Fix volatile in adaptive_scheduler.h | Trivial | None (already uses __atomic; just remove keyword) | None |
| Fix ASan findings (unknown count) | Unknown | Unknown | Medium (may discover real bugs) |
| Fix UBSan findings (unknown count) | Unknown | Unknown | Low (usually shifts/overflow) |
| Create minimal multi-threaded TSan tests | Medium | Medium (need to exercise actual concurrent patterns) | Low |
| sqlite3 UBSan exemption | Trivial | None | None |

## Sources

### Primary (HIGH confidence)
- Direct codebase inspection: `src/config/config.h`, `src/bsgs/bsgs_fast.cpp`, `src/keyhunt.cpp`, `src/search/search_context.h`, `src/gpu/gpu_backend.h`, `src/gpu/gpu_multi_worker.c`, `src/hybrid/adaptive_scheduler.h` -- all volatile and atomic patterns verified
- Direct build attempt: `make sanitize` output showing link failure (libasan.so.8.0.0 not found), package availability via `dnf list`
- Existing project research: `.planning/research/PITFALLS.md` (HIGH confidence, authored by previous research phase)
- Existing project REQUIREMENTS.md and ROADMAP.md -- requirement definitions for MEM-01 through MEM-04

### Secondary (MEDIUM confidence)
- GCC documentation on `-fsanitize=address`, `-fsanitize=undefined`, `-fsanitize=thread` -- standard compiler documentation
- LLVM/Clang ASan documentation: https://clang.llvm.org/docs/AddressSanitizer.html
- LLVM/Clang TSan documentation: https://clang.llvm.org/docs/ThreadSanitizer.html
- C++11 `std::atomic` semantics -- well-established standard behavior
- GCC Launchpad bug #2023424 (ASan/AVX-512 alignment false positive) -- cited in PITFALLS.md

### Tertiary (LOW confidence)
- Prediction that sqlite3 will trigger UBSan findings -- based on general knowledge of sqlite3 builds, not verified on this specific amalgamation version
- Estimate of ASan/UBSan finding count -- fundamentally unknown until first build succeeds

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - GCC 15 sanitizers are mature; packages identified and available
- Architecture: HIGH - Makefile targets already exist; volatile/atomic patterns thoroughly inventoried
- Pitfalls: HIGH (codebase-specific) / MEDIUM (false positive prediction) - All volatile uses mapped; SIMD false positive risk documented but not yet observed

**Research date:** 2026-03-01
**Valid until:** 2026-04-01 (stable domain; sanitizer behavior doesn't change rapidly)
