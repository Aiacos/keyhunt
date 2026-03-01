---
phase: 02-sanitizer-coverage
verified: 2026-03-01T10:30:00Z
status: passed
score: 4/4 must-haves verified
re_verification: false
---

# Phase 02: Sanitizer Coverage Verification Report

**Phase Goal:** The binary is free of memory errors, undefined behavior, and data races as confirmed by three independent sanitizer builds
**Verified:** 2026-03-01T10:30:00Z
**Status:** PASSED
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths (from ROADMAP.md Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `make asan` runs full test suite with zero ASan or UBSan findings (not suppressed, actually fixed) | VERIFIED | `run_tests_asan` exits 0; grep for `ERROR:\|runtime error:` returns 0; no `no_sanitize` in src/ (only sqlite3 third-party); no `detect_leaks=0` globally |
| 2 | `make tsan` runs multi-threaded search tests with zero TSan data-race reports | VERIFIED | `run_tests_tsan` exits 0; grep for `WARNING: ThreadSanitizer` returns 0; `nm run_tests_tsan | grep -c __tsan_func_entry` = 1 (instrumented) |
| 3 | All GPU progress counters use std::atomic (no volatile fields remain in gpu_config_t lines 154-157) | VERIFIED | `src/config/config.h` lines 159-161: `std::atomic<uint64_t> keys_checked`, `std::atomic<uint64_t> keys_checked_cur`, `std::atomic<int> should_stop`; config.cpp uses `.store()` not memset for these fields |
| 4 | `grep -rn "g_avx2_available" src/` shows exactly one definition, eliminating the BSGS/address dispatch divergence | VERIFIED | One definition at `src/keyhunt.cpp:162`; `src/bsgs/` contains zero matches; `src/bsgs/bsgs_fast.cpp` uses `g_bsgs_avx2` (renamed) fed via `bsgs_fast_set_cpu_features()` setter |

**Score:** 4/4 truths verified

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/config/config.h` | Atomic gpu_config_t fields with `#ifdef __cplusplus` dual-use | VERIFIED | Lines 159-165: `std::atomic<uint64_t> keys_checked/keys_checked_cur`, `std::atomic<int> should_stop` (C++) + `_Atomic` (C) fallback |
| `src/bsgs/bsgs_fast.h` | `bsgs_fast_set_cpu_features()` declaration | VERIFIED | Line 46: `void bsgs_fast_set_cpu_features(bool has_avx2, bool has_avx512);` |
| `src/bsgs/bsgs_fast.cpp` | No `detect_cpu_features()` or `g_avx2_available`; uses `g_bsgs_avx2` via setter | VERIFIED | Only `g_bsgs_avx2`/`g_bsgs_avx512` present; setter implemented at line 29; no cpuid.h include |
| `Makefile` | `asan: sanitize` alias; explicit `TEST_OBJDIR=` in sanitize/tsan recursive calls | VERIFIED | Line 532: `asan: sanitize`; line 516: `TEST_OBJDIR=$(SANITIZE_OBJDIR)/tests`; line 539: `TEST_OBJDIR=$(TSAN_OBJDIR)/tests`; both in `.PHONY` (line 633) |
| `src/search/search_context.h` | `std::atomic<int> THREADOUTPUT` and `std::atomic<int> *bsgs_found` externs | VERIFIED | Line 156: `extern std::atomic<int> THREADOUTPUT;`; line 223: `extern std::atomic<int> *bsgs_found;` |
| `src/keyhunt.cpp` | Atomic THREADOUTPUT/bsgs_found definitions; `bsgs_fast_set_cpu_features()` call | VERIFIED | Line 607: `std::atomic<int> THREADOUTPUT{0};`; line 836: `std::atomic<int> *bsgs_found;`; line 1704: `bsgs_fast_set_cpu_features(g_avx2_available, false);` |
| `src/gpu/gpu_multi_worker.c` | `should_stop`/`paused`/`has_result` use `__atomic` builtins (not volatile) | VERIFIED | Lines 32-34: plain `int`/`bool` fields; all accesses via `__atomic_store_n`/`__atomic_load_n` with `__ATOMIC_RELEASE`/`__ATOMIC_ACQUIRE` |
| `src/gpu/gpu_backend_opencl.c` | `keys_processed` uses `__atomic` builtins | VERIFIED | Line 128: `uint64_t keys_processed;` (no volatile); accesses at lines 509, 679, 1288, 1379, 1411 use `__atomic_store_n`/`__atomic_load_n`/`__atomic_fetch_add` |
| `src/hybrid/adaptive_scheduler.h` | `update_in_progress` has no `volatile` keyword | VERIFIED | Line 61: `int update_in_progress; /* Accessed via __atomic builtins for thread safety */` |
| `tests/test_threading.cpp` | 3 multi-threaded tests using `platform_thread_create`; zero volatile | VERIFIED | 191 lines; 3 tests: `threading_atomic_counter_no_race`, `threading_per_element_atomic_array`, `threading_mutex_shared_data`; `grep -c volatile` = 0 |
| `run_tests_asan` | ASan+UBSan instrumented binary that exits 0 | VERIFIED | Binary at 33MB; `nm | grep -c __asan_init` = 1; runs ALL TESTS PASSED, exit code 0 |
| `run_tests_tsan` | TSan instrumented binary that exits 0 | VERIFIED | Binary at 14MB; `nm | grep -c __tsan_func_entry` = 1; runs ALL TESTS PASSED, exit code 0 |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `src/keyhunt.cpp` | `src/bsgs/bsgs_fast.cpp` | `bsgs_fast_set_cpu_features()` called after sysinfo detection | WIRED | keyhunt.cpp:1704 calls `bsgs_fast_set_cpu_features(g_avx2_available, false)` after line 1696 AVX2 detection |
| `src/config/config.h` | `src/config/config.cpp` | `.store()` init pattern for atomic fields | WIRED | config.cpp lines 174-176 use `.store(0, memory_order_relaxed)` — no memset on atomic struct |
| `Makefile sanitize/tsan targets` | `run_tests_asan`/`run_tests_tsan` binaries | `TEST_OBJDIR` override ensures test objects built in instrumented obj dir | WIRED | Makefile line 516/539 both pass explicit `TEST_OBJDIR=...; nm` confirms instrumentation |
| `tests/test_threading.cpp` | `src/platform/platform.h` | Uses `platform_thread_create`/`join`, `platform_mutex_*` | WIRED | File includes `platform/platform.h`; uses `platform_thread_create`, `platform_thread_join`, `platform_mutex_init/lock/unlock/destroy` |
| `tests/run_tests.cpp` | `tests/test_threading.cpp` | `run_threading_tests()` declared and called | WIRED | run_tests.cpp line 52: `int run_threading_tests(void);`; line 211: `total_failures += run_threading_tests();` |
| `src/keyhunt.cpp` | `src/search/search_context.h` | `THREADOUTPUT` atomic extern declaration | WIRED | search_context.h line 156: `extern std::atomic<int> THREADOUTPUT;`; keyhunt.cpp:607 defines it; all write sites use `.store(memory_order_release)`, reads use `.load(memory_order_acquire)` |

---

### Requirements Coverage

| Requirement | Source Plan(s) | Description | Status | Evidence |
|-------------|---------------|-------------|--------|----------|
| MEM-01 | 02-03 | ASan + UBSan clean build with zero findings on full test suite | SATISFIED | `run_tests_asan` exits 0; 0 matches for `ERROR:`; 0 matches for `runtime error:`; 7 actual code fixes (Int.h bounds check, IntMod.cpp unsigned casts, ripemd160.cpp alignment, test buffer sizes); no `detect_leaks=0`; no `no_sanitize` annotations outside sqlite3 third-party code |
| MEM-02 | 02-04 | TSan clean build with zero findings on multi-threaded search tests | SATISFIED | `run_tests_tsan` exits 0; 0 matches for `WARNING: ThreadSanitizer`; 3 threaded tests exercise concurrent patterns (counter, per-element array, mutex); `nm` confirms instrumentation |
| MEM-03 | 02-01 | Replace volatile with std::atomic for GPU counters in config.h (lines 154-157) | SATISFIED | Lines 159-161 in config.h: `std::atomic<uint64_t> keys_checked`, `std::atomic<uint64_t> keys_checked_cur`, `std::atomic<int> should_stop`; C fallback with `_Atomic`; config.cpp initializes with `.store()` |
| MEM-04 | 02-01 | Fix duplicate g_avx2_available in bsgs_fast.cpp (unified SIMD dispatch) | SATISFIED | `grep -rn "g_avx2_available" src/bsgs/` returns 0 matches; bsgs_fast.cpp uses `g_bsgs_avx2` (renamed) fed via `bsgs_fast_set_cpu_features()` setter; keyhunt.cpp:1704 calls the setter |

**REQUIREMENTS.md cross-reference:** All four requirements marked `[x]` complete at lines 28-31 and Phase 2 column shows `Complete` at lines 123-126. No orphaned requirements detected.

---

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `src/gpu/gpu_backend.h` | 74-75 | `volatile uint64_t *keys_checked` and `volatile int *should_stop` in C-mode struct | Info | These are legacy C-mode pointer fields in `gpu_search_config_t` (used when included as C), not the primary `gpu_config_t` atomic fields. The receiving C code uses `__atomic` builtins at call sites. This is an acknowledged compatibility boundary (noted in 02-02 decisions). Not a blocker. |
| `src/gpu/gpu_multi_worker.c` | 215 | `(volatile int*)&manager->should_stop` cast | Info | Legacy cast for `gpu_search_config_t` pointer field. `__atomic` builtins provide actual synchronization. Acknowledged as technical debt in 02-02 decisions. Not a data race — cast only establishes pointer type, not access mode. |
| `src/wizard/wizard.h` | 433 | `volatile int *stop_flag` parameter type | Info | Wizard code is out of scope for this phase per 02-02 plan exclusion pattern (`grep` filter). Not a blocker for the sanitizer goal. |

No Blockers found. All anti-patterns are Info-level legacy compatibility artifacts explicitly acknowledged in plan decisions.

---

### Human Verification Required

None. All success criteria are programmatically verifiable and have been verified:
- Binary exits confirmed via actual binary execution
- Sanitizer finding counts confirmed via grep on binary output
- Instrumentation confirmed via `nm` symbol lookup
- Atomic types confirmed via grep on source files
- Commit existence confirmed via git log

---

### Volatile Elimination Summary

Cross-thread volatile eliminated from:
- `src/config/config.h` — gpu_config_t fields (MEM-03)
- `src/keyhunt.cpp` — THREADOUTPUT, bsgs_found
- `src/search/search_context.h` — THREADOUTPUT, bsgs_found externs
- `src/search/search_bsgs_threads.cpp` — redundant externs removed
- `src/search/search_address.cpp` — local extern removed
- `src/search/search_vanity.cpp` — THREADOUTPUT write updated
- `src/gpu/gpu_multi_worker.c` — should_stop, paused, has_result
- `src/gpu/gpu_backend_opencl.c` — keys_processed
- `src/hybrid/adaptive_scheduler.h` — update_in_progress

Remaining `volatile` in src/ is all acceptable:
- `volatile sig_atomic_t` in signal handlers (keyhunt.cpp:697)
- `volatile uint64_t *` / `volatile int *` pointer types in C-mode struct fields (gpu_backend.h — legacy C interface)
- `(volatile int*)` cast at gpu_multi_worker.c:215 (acknowledged compatibility cast)
- `volatile int *stop_flag` in wizard.h (out of scope for this phase)
- `__asm__ volatile` in hash SIMD files (inline assembly — always acceptable)

---

## ASan/UBSan Findings Fixed (MEM-01)

7 actual code fixes, zero suppressions:

1. `src/secp256k1/Int.h` — getBits/setBits bounds check (return 0 for out-of-range indices)
2. `src/secp256k1/IntMod.cpp` — Newton iteration uint64_t (prevents signed overflow)
3. `src/secp256k1/IntMod.cpp` — SWAP_ADD/SWAP_SUB unsigned casts (prevents signed overflow UB)
4. `src/hash/ripemd160.cpp` — aligned uint32_t state[5] buffer + memcpy (prevents misaligned access)
5. `tests/test_bsgs_ops.cpp` — GSn arrays sized hLength+1 (was hLength, off-by-one overflow)
6. `tests/test_fused_hash.cpp` — 2-block SHA256 inputs sized [8][32] (was [8][16])
7. `tests/test_search_mocks.cpp` — searchbinary mock uses 20-byte comparison (was 32, read past RMD160 buffer)

---

## TSan Findings Fixed (MEM-02)

2 auto-fixed issues beyond the volatile conversion:

1. `tests/test_search_xpoint.cpp` — Added `init_secp256k1_field()` with static lifetime for InitK1/SetupField args (prevented SEGV/use-after-return under TSan -O1)
2. `src/gpu/gpu_backend_none.cpp` — Changed to return -1 from `gpu_backend_init` (GPU tests now SKIP correctly on headless machines instead of failing)

---

_Verified: 2026-03-01T10:30:00Z_
_Verifier: Claude (gsd-verifier)_
