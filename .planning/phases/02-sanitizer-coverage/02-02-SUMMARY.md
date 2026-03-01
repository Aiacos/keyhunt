---
phase: 02-sanitizer-coverage
plan: 02
subsystem: concurrency
tags: [atomic, volatile, tsan, memory-ordering, secp256k1, gpu]

# Dependency graph
requires:
  - phase: 02-sanitizer-coverage/01
    provides: "Atomic gpu_config_t fields, MEM-03/MEM-04 foundation"
provides:
  - "All cross-thread volatile variables converted to std::atomic (C++) or __atomic builtins (C)"
  - "Zero cross-thread volatile remaining in src/ (only signal handlers, asm, compiler hints, third-party)"
  - "Correct memory ordering: relaxed for counters, acquire/release for signals and stop flags"
affects: [02-sanitizer-coverage/03, 02-sanitizer-coverage/04]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "std::atomic with explicit memory_order for C++ cross-thread variables"
    - "__atomic builtins (not _Atomic) for C cross-thread variables (TSan-compatible, no ABI issues)"
    - "memory_order_relaxed for tight-loop reads (bsgs_found), acquire/release for signals (THREADOUTPUT, should_stop)"

key-files:
  created: []
  modified:
    - "src/keyhunt.cpp"
    - "src/search/search_context.h"
    - "src/search/search_bsgs_threads.cpp"
    - "src/search/search_address.cpp"
    - "src/search/search_vanity.cpp"
    - "src/gpu/gpu_multi_worker.c"
    - "src/gpu/gpu_backend_opencl.c"
    - "src/hybrid/adaptive_scheduler.h"

key-decisions:
  - "Used memory_order_relaxed for bsgs_found tight-loop reads to preserve performance (delayed visibility acceptable)"
  - "Used __atomic builtins (not _Atomic) in C files for consistency with existing adaptive_scheduler.c pattern and to avoid ABI issues at C/C++ boundary"
  - "Kept volatile int* cast in gpu_multi_worker.c for gpu_search_config_t compatibility (C mode struct uses volatile pointers)"
  - "bsgs_found allocated with new std::atomic<int>[N]{} instead of calloc -- zero-initialized by default"

patterns-established:
  - "C++ cross-thread: std::atomic<T> with explicit memory ordering"
  - "C cross-thread: plain fields + __atomic_load_n/__atomic_store_n/__atomic_fetch_add"
  - "Tight-loop reads: memory_order_relaxed (thread does extra iterations, acceptable)"
  - "Signal/flag writes: memory_order_release; corresponding reads: memory_order_acquire"

requirements-completed: [MEM-01, MEM-02]

# Metrics
duration: 6min
completed: 2026-03-01
---

# Phase 2 Plan 2: Volatile-to-Atomic Conversion Summary

**All cross-thread volatile variables converted to std::atomic (C++) and __atomic builtins (C) with correct memory ordering for TSan-clean execution**

## Performance

- **Duration:** 6 min
- **Started:** 2026-03-01T08:13:10Z
- **Completed:** 2026-03-01T08:19:50Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- Converted THREADOUTPUT from volatile int to std::atomic<int> across 5 C++ files with acquire/release ordering
- Converted bsgs_found from volatile int* to std::atomic<int>* with relaxed ordering for tight-loop reads, release for writes, acquire for accumulation
- Converted gpu_multi_worker.c should_stop/paused/has_result from volatile to __atomic builtins
- Converted gpu_backend_opencl.c keys_processed from volatile to __atomic builtins
- Removed redundant volatile from adaptive_scheduler.h update_in_progress (already used __atomic)
- Removed redundant volatile extern declarations from search_bsgs_threads.cpp and search_address.cpp

## Task Commits

Each task was committed atomically:

1. **Task 1: Convert THREADOUTPUT and bsgs_found to std::atomic** - `1d3a533` (feat)
2. **Task 2: Convert remaining volatile in GPU and scheduler C code** - `3c56359` (feat)

**Plan metadata:** [pending] (docs: complete plan)

## Files Created/Modified
- `src/keyhunt.cpp` - THREADOUTPUT and bsgs_found definitions changed to std::atomic, all use sites updated
- `src/search/search_context.h` - Added #include <atomic>, extern declarations updated to std::atomic types
- `src/search/search_bsgs_threads.cpp` - Removed redundant volatile externs, ~30 bsgs_found sites and 5 THREADOUTPUT sites updated with atomic operations
- `src/search/search_address.cpp` - Removed local volatile extern, THREADOUTPUT write updated
- `src/search/search_vanity.cpp` - THREADOUTPUT write updated
- `src/gpu/gpu_multi_worker.c` - should_stop/paused/has_result use __atomic builtins
- `src/gpu/gpu_backend_opencl.c` - keys_processed uses __atomic builtins, C-mode should_stop read updated
- `src/hybrid/adaptive_scheduler.h` - Removed redundant volatile from update_in_progress

## Decisions Made
- Used memory_order_relaxed for bsgs_found tight-loop reads -- thread just does extra iterations if visibility is delayed, preserving performance
- Used __atomic builtins (not _Atomic) in C files for consistency with existing adaptive_scheduler.c pattern and to avoid ABI issues at C/C++ boundary
- Kept volatile int* cast in gpu_multi_worker.c line 215 for gpu_search_config_t compatibility -- the C-mode struct definition in gpu_backend.h uses volatile pointers
- bsgs_found allocated with new std::atomic<int>[N]{} instead of calloc -- value initialization provides zero-init

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All cross-thread volatile variables eliminated from src/ (only acceptable patterns remain: sig_atomic_t, asm volatile, IntMod.cpp compiler hints, third-party code)
- TSan should no longer report data races on these variables
- Ready for plan 02-03 (ASan/UBSan integration) and 02-04 (TSan testing)

---
*Phase: 02-sanitizer-coverage*
*Completed: 2026-03-01*
