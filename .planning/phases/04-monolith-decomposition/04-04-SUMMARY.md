---
phase: 04-monolith-decomposition
plan: 04
subsystem: core
tags: [refactoring, mode-extraction, bsgs, thread-dispatch, bloom-filter]

# Dependency graph
requires:
  - phase: 04-monolith-decomposition/03
    provides: Mode dispatch table with all non-BSGS modes registered
provides:
  - BSGS mode dispatcher (mode_bsgs.cpp) with init/run/cleanup
  - All 6 search modes now dispatched through mode_ops_t table
  - keyhunt.cpp reduced from 5213 to 3885 lines
affects: [04-05, 04-06]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "BSGS mode uses global tid/steps/ends allocated in init, ignoring dispatch table tids parameter"
    - "config.search.target_file wired for mode files to access input file path"

key-files:
  created:
    - src/modes/mode_bsgs.cpp
  modified:
    - src/modes/modes.cpp
    - src/keyhunt.cpp
    - Makefile

key-decisions:
  - "BSGS globals remain in keyhunt.cpp with extern access from mode_bsgs.cpp (move first, clean later pattern)"
  - "mode_bsgs_run ignores tids/thread_count parameters since init allocates its own global arrays"
  - "shutdown_work_queue made non-static for mode_bsgs.cpp cross-file access"
  - "config.search.target_file wired in config bridge to pass fileName to mode init functions"

patterns-established:
  - "All 6 modes (ADDRESS, XPOINT, RMD160, VANITY, MINIKEYS, BSGS) dispatched through mode_ops_t table"

requirements-completed: [STR-02]

# Metrics
duration: 17min
completed: 2026-03-06
---

# Phase 4 Plan 04: BSGS Mode Extraction Summary

**BSGS mode initialization (1334 lines of bloom filter allocation, bPload threads, bP table computation, thread dispatch) extracted from keyhunt.cpp to src/modes/mode_bsgs.cpp**

## Performance

- **Duration:** 17 min
- **Started:** 2026-03-06T07:31:56Z
- **Completed:** 2026-03-06T07:49:21Z
- **Tasks:** 2
- **Files modified:** 5 (1 created, 4 modified)

## Accomplishments
- Extracted the largest single code block from keyhunt.cpp: BSGS initialization (file reading, parameter calculation, memory validation, 3-tier bloom filter allocation, bPload thread spawning, generator point computation, bsgs_context_t population, cache file I/O, search thread dispatch)
- Registered mode_bsgs_ops in dispatch table -- all 6 search modes now route through mode_dispatch()
- keyhunt.cpp reduced from 5213 to 3885 lines (-1328), removed unused BSGS-only local variables from main()
- BSGS mode verified working: processes bP points, searches at ~3 Pkeys/s on 125-bit puzzle

## Task Commits

Each task was committed atomically:

1. **Task 1: Extract BSGS initialization and dispatch to mode_bsgs.cpp** - `f8d53e6` (feat)
2. **Task 2: Verify BSGS mode end-to-end and fix regressions** - No code changes needed; all tests passed

## Files Created/Modified
- `src/modes/mode_bsgs.cpp` - BSGS mode dispatcher: init (1334-line initialization block), run (5-mode thread dispatch), cleanup (no-op, atexit handles it)
- `src/modes/modes.cpp` - Registered mode_bsgs_ops in dispatch table
- `src/keyhunt.cpp` - Removed entire BSGS inline block, replaced with mode_dispatch call; wired config.search.target_file; removed unused local variables
- `Makefile` - Added mode_bsgs.o to MODES_OBJS

## Decisions Made
- **BSGS globals remain extern:** The 50+ BSGS globals (Int/Point/bloom/bPtable) stay defined in keyhunt.cpp with extern access from mode_bsgs.cpp. This follows the "move first, clean later" principle -- absorbing all globals into config would be a separate, orthogonal change.
- **mode_bsgs_run ignores dispatch table tids parameter:** BSGS init allocates its own global tid/steps/ends arrays (used by the monitoring loop). The run function uses these globals directly rather than the tids passed through mode_dispatch.
- **shutdown_work_queue non-static:** Made accessible to mode_bsgs.cpp for BSGS path work queue teardown.
- **config.search.target_file wired:** Added fileName -> config bridge so mode init functions can access the input file path without it being a function parameter.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- Python heredoc in replacement script injected literal `\n` instead of newline in string -- fixed immediately with manual edit.
- `bsgs_xvalue` struct redefinition: bsgs_sort.h must be included before search_common.h to trigger the `#ifndef BSGS_SORT_H` guard -- same pattern used in search_bsgs.cpp.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All 6 search modes now dispatch through mode_ops_t table
- keyhunt.cpp at 3885 lines (target: ~600 lines); remaining extraction targets: io.cpp config wiring, monitoring loop, GPU dispatch, utility functions, global variable absorption
- BSGS globals still in keyhunt.cpp are future cleanup targets
- All unit tests pass (make test)

## Self-Check: PASSED

- [x] src/modes/mode_bsgs.cpp exists
- [x] Commit f8d53e6 exists
- [x] 04-04-SUMMARY.md exists

---
*Phase: 04-monolith-decomposition*
*Completed: 2026-03-06*
