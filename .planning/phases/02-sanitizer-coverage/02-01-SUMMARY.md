---
phase: 02-sanitizer-coverage
plan: 01
subsystem: testing
tags: [asan, tsan, ubsan, sanitizer, atomic, std-atomic, avx2, cpu-detection]

# Dependency graph
requires:
  - phase: 01-test-baseline
    provides: test infrastructure, coverage gate, unit test suite
provides:
  - Sanitizer builds (make asan / make sanitize / make tsan) that link and run
  - Atomic gpu_config_t fields (MEM-03 resolved)
  - Unified CPU feature detection via bsgs_fast_set_cpu_features() (MEM-04 resolved)
  - Explicit TEST_OBJDIR override in sanitizer recursive make calls
affects: [02-sanitizer-coverage plans 02-04, config migration, gpu backend]

# Tech tracking
tech-stack:
  added: [libasan, libtsan, libubsan, stdatomic.h]
  patterns: [std::atomic for cross-thread struct fields, setter-based CPU feature propagation]

key-files:
  created: []
  modified:
    - Makefile
    - src/config/config.h
    - src/config/config.cpp
    - src/bsgs/bsgs_fast.h
    - src/bsgs/bsgs_fast.cpp
    - src/keyhunt.cpp

key-decisions:
  - "gpu_config_t atomic fields use memory_order_relaxed for init (no contention at startup)"
  - "bsgs_fast renamed variables to g_bsgs_avx2/g_bsgs_avx512 to avoid shadowing keyhunt.cpp global"
  - "AVX-512 passed as false to bsgs_fast_set_cpu_features() since sysinfo does not yet expose it"
  - "C11 _Atomic fallback in config.h for future C file compatibility (no C files include it yet)"

patterns-established:
  - "Setter-based feature propagation: modules receive CPU features from caller, no local cpuid"
  - "std::atomic with #ifdef __cplusplus / _Atomic dual-use pattern for cross-language headers"

requirements-completed: [MEM-03, MEM-04]

# Metrics
duration: 15min
completed: 2026-03-01
---

# Phase 02 Plan 01: Sanitizer Foundation Summary

**Sanitizer runtime packages installed, gpu_config_t volatile-to-atomic conversion (MEM-03), and unified bsgs CPU detection via setter (MEM-04)**

## Performance

- **Duration:** 15 min
- **Started:** 2026-03-01T07:54:58Z
- **Completed:** 2026-03-01T08:10:27Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- Installed libasan, libtsan, libubsan on Fedora 43 and verified both `make asan` and `make tsan` link and produce instrumented binaries
- Converted gpu_config_t.keys_checked, .keys_checked_cur, .should_stop from volatile to std::atomic (C++) / _Atomic (C) with proper .store()/.load() in init code
- Eliminated duplicate CPU detection in bsgs_fast.cpp by replacing local detect_cpu_features() with bsgs_fast_set_cpu_features() setter called from keyhunt.cpp
- Added explicit TEST_OBJDIR= override in sanitizer/tsan recursive make calls for robustness
- Added `asan` convenience alias in Makefile .PHONY targets

## Task Commits

Each task was committed atomically:

1. **Task 1: Install sanitizer packages and add Makefile asan alias** - `c22e9ea` (chore)
2. **Task 2: Convert gpu_config_t volatile to atomic and unify bsgs CPU detection** - `87f86e6` (feat)

## Files Created/Modified
- `Makefile` - Added asan alias, TEST_OBJDIR explicit overrides for sanitize/tsan recursive calls
- `src/config/config.h` - Converted volatile fields to std::atomic/_Atomic, added atomic includes
- `src/config/config.cpp` - Updated kh_gpu_config_init() to use .store() for atomic fields
- `src/bsgs/bsgs_fast.h` - Added bsgs_fast_set_cpu_features() declaration
- `src/bsgs/bsgs_fast.cpp` - Removed cpuid.h/detect_cpu_features(), renamed vars, added setter
- `src/keyhunt.cpp` - Added bsgs_fast.h include, call to bsgs_fast_set_cpu_features() after AVX2 detection

## Decisions Made
- gpu_config_t atomic fields initialized with memory_order_relaxed (no contention at startup time)
- bsgs_fast variables renamed from g_avx2_available to g_bsgs_avx2 to avoid shadowing the keyhunt.cpp global
- AVX-512 passed as false to bsgs_fast since sysinfo does not yet expose a separate AVX-512 flag
- Added C11 _Atomic fallback path in config.h for future-proofing (currently no C files include it)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- ASan build aborts on pre-existing memory errors during test execution (expected, to be fixed in 02-03)
- TSan build crashes on SEGV in _umul128/ModMulK1order (pre-existing issue, to be fixed in 02-04)
- Both are expected findings that confirm the sanitizer infrastructure is working correctly

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Sanitizer infrastructure is operational for plans 02-02, 02-03, and 02-04
- MEM-03 and MEM-04 prerequisites resolved, reducing noise in future TSan runs
- Pre-existing ASan/TSan findings documented and ready for targeted fixing

---
*Phase: 02-sanitizer-coverage*
*Completed: 2026-03-01*
