---
phase: 02-sanitizer-coverage
plan: 04
subsystem: testing
tags: [tsan, threading, atomic, mutex, concurrency, sanitizer]

# Dependency graph
requires:
  - phase: 02-sanitizer-coverage/01
    provides: "Sanitizer build infrastructure (make tsan target), atomic gpu_config_t"
  - phase: 02-sanitizer-coverage/02
    provides: "All cross-thread volatile converted to std::atomic/builtins"
provides:
  - "Multi-threaded test suite (test_threading.cpp) exercising concurrent atomic, array, and mutex patterns"
  - "make tsan exits 0 with zero ThreadSanitizer findings"
  - "MEM-02 complete: TSan clean build with zero findings on multi-threaded search tests"
affects: [03-config-migration, phase-2-completion]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Threading tests use platform_thread_create/join + std::atomic for TSan-visible concurrency"
    - "nanosleep for timed waits in tests (no volatile spin loops)"
    - "GPU none-backend returns -1 from init so GPU tests properly SKIP on headless machines"

key-files:
  created:
    - "tests/test_threading.cpp"
  modified:
    - "tests/run_tests.cpp"
    - "Makefile"
    - "tests/test_search_xpoint.cpp"
    - "tests/test_gpu_backend.cpp"
    - "src/gpu/gpu_backend_none.cpp"

key-decisions:
  - "Thread tests use nanosleep(10ms) not volatile spin -- volatile contradicts phase MEM-03 goals and TSan may flag it"
  - "GPU none-backend returns -1 from gpu_backend_init to signal no GPU available (GPU tests properly SKIP)"
  - "XPoint endomorphism tests need static Int for InitK1/SetupField since they store raw pointers"

patterns-established:
  - "Multi-threaded test pattern: use std::atomic with explicit memory_order + platform_thread_create/join"
  - "GPU test skip pattern: check both init return code AND gpu_count before asserting GPU presence"

requirements-completed: [MEM-02]

# Metrics
duration: 16min
completed: 2026-03-01
---

# Phase 2 Plan 4: TSan Threading Tests and Zero-Finding Gate Summary

**Multi-threaded test suite with 3 concurrent tests (atomic counter, per-element array, mutex) achieving zero TSan data-race findings via `make tsan`**

## Performance

- **Duration:** 16 min
- **Started:** 2026-03-01T08:23:52Z
- **Completed:** 2026-03-01T08:40:51Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- Created test_threading.cpp with 3 multi-threaded tests: atomic counter (THREADOUTPUT pattern), per-element atomic array (bsgs_found pattern), mutex-protected shared data (stats_lock pattern)
- `make tsan` exits 0 with zero `WARNING: ThreadSanitizer` findings across the entire test suite
- `nm run_tests_tsan | grep __tsan_func_entry` confirms TSan instrumentation is present
- No volatile in test code, no suppression files, all fixes are actual code fixes
- MEM-02 satisfied: TSan clean build with zero findings on multi-threaded tests

## Task Commits

Each task was committed atomically:

1. **Task 1: Create multi-threaded test file and wire into build** - `f9c96da` (feat)
2. **Task 2: Fix pre-existing test crashes blocking TSan clean run** - `1400dca` (fix)
3. **Task 2 (follow-up): Make InitK1/SetupField args static** - `70ff804` (fix)

## Files Created/Modified
- `tests/test_threading.cpp` - 3 multi-threaded tests using platform_thread_create/join, std::atomic, platform_mutex_t
- `tests/run_tests.cpp` - Added run_threading_tests() declaration and call in main()
- `Makefile` - Added TEST_THREADING_OBJ to TEST_OBJS with build rule
- `tests/test_search_xpoint.cpp` - Added secp256k1 InitK1/SetupField initialization (static lifetime)
- `tests/test_gpu_backend.cpp` - Fixed gpu_backend_init_basic to check gpu_count before asserting
- `src/gpu/gpu_backend_none.cpp` - Return -1 from gpu_backend_init (no GPU available)

## Decisions Made
- Thread tests use `nanosleep(10ms)` for timed waits, not volatile spin loops (volatile contradicts MEM-03 and TSan may flag it)
- GPU none-backend changed to return -1 from `gpu_backend_init` to properly signal "no GPU hardware" so tests SKIP gracefully
- XPoint test's `Int P` and `Int order` variables made static since `InitK1`/`SetupField` store raw pointers that must outlive the function scope

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed SEGV in xpoint endomorphism tests under TSan**
- **Found during:** Task 2 (TSan build)
- **Issue:** `test_search_xpoint.cpp` called `ModMulK1order` without calling `InitK1` first, causing SEGV on NULL `_O` pointer. Crash was hidden in normal build but reliably reproduced under TSan's `-O1` compilation.
- **Fix:** Added `init_secp256k1_field()` function that calls `Int::SetupField(&P)` and `Int::InitK1(&order)` with proper static lifetime.
- **Files modified:** `tests/test_search_xpoint.cpp`
- **Verification:** `make test` and `make tsan` both pass including endomorphism tests
- **Committed in:** `1400dca`, `70ff804`

**2. [Rule 3 - Blocking] Fixed GPU tests failing on machines without GPU hardware**
- **Found during:** Task 2 (TSan build)
- **Issue:** `gpu_backend_none.cpp` returned 0 from `gpu_backend_init` (success) with gpu_count=0. Tests expected -1 for no-GPU and proceeded past SKIP checks, then failed on `ASSERT_TRUE(result == 0 || result == -1)` because stub functions returned 1.
- **Fix:** Changed `gpu_backend_none.cpp` to return -1 from init. Updated `test_gpu_backend.cpp` to also check gpu_count.
- **Files modified:** `src/gpu/gpu_backend_none.cpp`, `tests/test_gpu_backend.cpp`
- **Verification:** GPU tests now SKIP (not FAIL) on headless machines. `make tsan` exits 0.
- **Committed in:** `1400dca`

---

**Total deviations:** 2 auto-fixed (1 bug, 1 blocking)
**Impact on plan:** Both auto-fixes necessary for `make tsan` to exit 0. Without the xpoint fix, TSan build crashed on SEGV. Without the GPU fix, TSan build exited 1 due to GPU test failures. No scope creep.

## Issues Encountered
- Pre-existing uncommitted changes from 02-03 (ASan plan) found in working tree (bsgs_ops.h, Int.h, test_bsgs_ops.cpp) -- left untouched as they are out of scope for this plan

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Phase 02 (Sanitizer Coverage) is fully complete: ASan, TSan, UBSan, and coverage all operational
- MEM-01 through MEM-04 requirements satisfied
- Ready for Phase 03 (Config Migration)

---
*Phase: 02-sanitizer-coverage*
*Completed: 2026-03-01*
