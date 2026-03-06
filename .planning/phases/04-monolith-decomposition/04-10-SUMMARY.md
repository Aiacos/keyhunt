---
phase: 04-monolith-decomposition
plan: 10
subsystem: core
tags: [refactor, monolith-decomposition, cli, gpu, config-bridge, orchestration]

# Dependency graph
requires:
  - phase: 04-09
    provides: globals.h/cpp shared global module
provides:
  - keyhunt.cpp reduced to 638 lines (thin orchestrator)
  - parse_cli_args() in cli.cpp for CLI argument processing
  - setup_search_range() in cli.cpp for range resolution
  - resolve_gpu_mode(), run_gpu_full_search_mode(), run_gpu_hybrid_setup() in gpu_dispatch.cpp
  - kh_config_bridge_from_globals() in config/config.cpp
  - init_monitoring_params() in monitoring.cpp
affects: [future-refactoring, testing, gpu-optimization]

# Tech tracking
tech-stack:
  added: []
  patterns: [function-extraction, config-bridge-pattern, monitoring-params-init]

key-files:
  created: []
  modified:
    - src/keyhunt.cpp
    - src/cli.cpp
    - src/cli.h
    - src/gpu/gpu_dispatch.cpp
    - src/gpu/gpu_dispatch.h
    - src/config/config.cpp
    - src/config/config.h
    - src/monitoring/monitoring.cpp
    - src/monitoring/monitoring.h
    - src/globals.h
    - src/globals.cpp

key-decisions:
  - "Range setup extracted to cli.cpp (logical extension of argument processing)"
  - "Config bridge extracted to config/config.cpp (keeps config wiring centralized)"
  - "Monitoring params init extracted to monitoring.cpp (keeps monitoring self-contained)"
  - "GPU orchestration functions use local sigint handler to avoid dependency on keyhunt.cpp statics"

patterns-established:
  - "Thin orchestrator: main() is a call sequence, not inline logic"
  - "Config bridge pattern: kh_config_bridge_from_globals() centralizes global-to-config translation"

requirements-completed: [STR-01, STR-03, STR-04, STR-05, STR-06, STR-07, STR-08]

# Metrics
duration: 45min
completed: 2026-03-06
---

# Phase 04 Plan 10: Main() Slimming Summary

**Extracted CLI parsing, GPU orchestration, range setup, config bridge, and monitoring init from keyhunt.cpp main(), reducing it from 1334 to 638 lines**

## Performance

- **Duration:** ~45 min
- **Started:** 2026-03-06T12:30:00Z
- **Completed:** 2026-03-06T13:15:00Z
- **Tasks:** 2
- **Files modified:** 11

## Accomplishments
- keyhunt.cpp reduced from 1334 to 638 lines (52% reduction, under 650-line target)
- CLI getopt loop + early checks + config file handling + parameter validation extracted to parse_cli_args() in cli.cpp (~363 lines)
- GPU mode resolution, full search orchestration, and hybrid setup extracted to gpu_dispatch.cpp (~243 lines)
- Range setup extracted to setup_search_range() in cli.cpp
- Config bridge extracted to kh_config_bridge_from_globals() in config/config.cpp
- Monitoring params initialization extracted to init_monitoring_params() in monitoring.cpp
- All smoke tests pass: ADDRESS, BSGS, XPOINT modes

## Task Commits

Each task was committed atomically:

1. **Task 1: Extract CLI parsing to parse_cli_args()** - `64c22d4` (refactor)
2. **Task 2: Extract GPU orchestration + additional extractions** - `82d3bea` (refactor)

## Files Created/Modified
- `src/keyhunt.cpp` - Thin orchestrator: main() is now a call sequence (638 lines)
- `src/cli.cpp` - Added parse_cli_args() and setup_search_range() functions
- `src/cli.h` - Declared parse_cli_args() and setup_search_range()
- `src/gpu/gpu_dispatch.cpp` - Added resolve_gpu_mode(), run_gpu_full_search_mode(), run_gpu_hybrid_setup()
- `src/gpu/gpu_dispatch.h` - Declared GPU orchestration functions + gpu_multi_worker.h include
- `src/config/config.cpp` - Added kh_config_bridge_from_globals()
- `src/config/config.h` - Declared kh_config_bridge_from_globals()
- `src/monitoring/monitoring.cpp` - Added init_monitoring_params()
- `src/monitoring/monitoring.h` - Declared init_monitoring_params()
- `src/globals.h` - Added g_ini_config, g_ini_config_loaded, g_save_config_path, g_fileName declarations
- `src/globals.cpp` - Added definitions for new globals

## Decisions Made
- Extracted range setup to cli.cpp rather than a separate module, since it logically follows argument parsing
- Config bridge function placed in config/config.cpp to keep config translation centralized
- GPU orchestration uses a local sigint handler and local multi-GPU worker pointer, avoiding dependency on keyhunt.cpp file-local statics
- Moved g_ini_config, g_save_config_path, and g_fileName from file-local statics to shared globals to enable cross-TU access from cli.cpp

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Forward declaration conflict for keyhunt_ini_config_t**
- **Found during:** Task 1 (CLI extraction)
- **Issue:** `struct keyhunt_ini_config_t;` forward declaration in globals.h conflicted with `typedef struct { ... } keyhunt_ini_config_t;` in core/config.h (C++ typedef/struct name mismatch)
- **Fix:** Replaced forward declaration with `#include "core/config.h"` in globals.h
- **Files modified:** src/globals.h
- **Verification:** Build passes cleanly
- **Committed in:** 64c22d4 (Task 1 commit)

**2. [Rule 3 - Blocking] Missing gpu_multi_worker_t type in gpu_dispatch.h**
- **Found during:** Task 2 (GPU extraction)
- **Issue:** run_gpu_full_search_mode() parameter used gpu_multi_worker_t** but the type wasn't declared
- **Fix:** Added `#include "gpu_multi_worker.h"` to gpu_dispatch.h
- **Files modified:** src/gpu/gpu_dispatch.h
- **Verification:** Build passes cleanly
- **Committed in:** 82d3bea (Task 2 commit)

**3. [Rule 2 - Missing Critical] Additional extractions needed for 650-line target**
- **Found during:** Task 2 (after GPU extraction)
- **Issue:** After CLI + GPU extraction, keyhunt.cpp was 732 lines (82 over target)
- **Fix:** Extracted setup_search_range() to cli.cpp, kh_config_bridge_from_globals() to config.cpp, init_monitoring_params() to monitoring.cpp (-94 lines net)
- **Files modified:** src/cli.cpp, src/config/config.cpp, src/monitoring/monitoring.cpp, src/keyhunt.cpp
- **Verification:** wc -l src/keyhunt.cpp = 638 (under 650 target)
- **Committed in:** 82d3bea (Task 2 commit)

---

**Total deviations:** 3 auto-fixed (2 blocking, 1 missing critical)
**Impact on plan:** All auto-fixes necessary for correctness. The additional extractions were anticipated by the plan text ("Additional small extractions if needed to reach 600").

## Issues Encountered
- None beyond the deviations documented above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- keyhunt.cpp is now a 638-line thin orchestrator
- All monolith decomposition goals achieved: mode dispatchers, search modules, I/O, monitoring, GPU dispatch, CLI parsing, globals, and config bridge are all in dedicated modules
- mode_bsgs.cpp still has shared-state externs but these are through globals.h (not raw externs)
- Ready for any future optimization or testing phases

## Self-Check: PASSED

- All 7 key files exist on disk
- Both task commits (64c22d4, 82d3bea) found in git log
- keyhunt.cpp line count: 638 (target: <=650)

---
*Phase: 04-monolith-decomposition*
*Completed: 2026-03-06*
