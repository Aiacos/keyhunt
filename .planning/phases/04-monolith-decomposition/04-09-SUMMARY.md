---
phase: 04-monolith-decomposition
plan: 09
subsystem: architecture
tags: [globals, extern-elimination, cpp-modules, dependency-injection]

# Dependency graph
requires:
  - phase: 04-monolith-decomposition (plans 01-08)
    provides: extracted modes, monitoring, GPU dispatch from keyhunt.cpp
provides:
  - src/globals.h with struct definitions and extern declarations for all shared globals
  - src/globals.cpp with variable definitions previously in keyhunt.cpp
  - mode_bsgs.cpp zero-extern architecture (only ops table linkage remains)
  - gpu_dispatch.cpp and monitoring.cpp using globals.h instead of inline externs
affects: [04-10-main-slimming, phase-05]

# Tech tracking
tech-stack:
  added: []
  patterns: [centralized-globals-module, include-over-extern]

key-files:
  created:
    - src/globals.h
    - src/globals.cpp
  modified:
    - src/keyhunt.cpp
    - src/modes/mode_bsgs.cpp
    - src/gpu/gpu_dispatch.cpp
    - src/monitoring/monitoring.cpp
    - src/search/search_common.h
    - src/search/search_context.h
    - Makefile

key-decisions:
  - "Shared globals centralized in globals.h/cpp; file-local statics stay in keyhunt.cpp"
  - "MODE_* macros removed from search_common.h and search_context.h to avoid conflict with cli.h enum values"
  - "search_context.h struct definitions replaced with #include globals.h"

patterns-established:
  - "Include globals.h for shared state: all new code should use #include globals.h instead of inline extern declarations"
  - "No MODE_* macros: use search_mode_t enum values from cli.h (via globals.h)"

requirements-completed: [STR-02, STR-05]

# Metrics
duration: 11min
completed: 2026-03-06
---

# Phase 4 Plan 09: Globals Extraction Summary

**Centralized ~123 shared globals into src/globals.h/cpp, eliminating 51 extern declarations from mode_bsgs.cpp and all inline externs from gpu_dispatch.cpp/monitoring.cpp**

## Performance

- **Duration:** 11 min
- **Started:** 2026-03-06T11:51:29Z
- **Completed:** 2026-03-06T12:02:28Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- Created src/globals.h (265 lines) with all struct definitions + extern declarations for shared globals
- Created src/globals.cpp (175 lines) with variable definitions moved from keyhunt.cpp
- Reduced keyhunt.cpp from 1504 to 1334 lines (170 lines removed)
- mode_bsgs.cpp externs: 51 -> 1 (only ops table linkage remains)
- gpu_dispatch.cpp externs: 12 -> 0
- monitoring.cpp externs: 13 -> 0
- Resolved MODE_* macro/enum conflict between search_context.h and cli.h

## Task Commits

Each task was committed atomically:

1. **Task 1: Create globals.h and globals.cpp, move all shared globals from keyhunt.cpp** - `1b8a658` (refactor)
2. **Task 2: Replace inline extern declarations in mode_bsgs.cpp, gpu_dispatch.cpp, monitoring.cpp, search_common.h** - `3ada473` (refactor)

## Files Created/Modified
- `src/globals.h` - Header declaring all shared globals (structs + extern declarations)
- `src/globals.cpp` - Definitions of all shared global variables previously in keyhunt.cpp
- `src/keyhunt.cpp` - Removed global definitions, now includes globals.h
- `src/modes/mode_bsgs.cpp` - Replaced 51 extern declarations with #include globals.h
- `src/gpu/gpu_dispatch.cpp` - Replaced 12 extern declarations with #include globals.h
- `src/monitoring/monitoring.cpp` - Replaced 13 extern declarations with #include globals.h
- `src/search/search_common.h` - Removed duplicate extern declarations, includes globals.h
- `src/search/search_context.h` - Removed duplicate struct definitions, includes globals.h
- `Makefile` - Added globals.o to COMMON_OBJS

## Decisions Made
- Shared globals centralized in globals.h/cpp; file-local statics (g_config, g_workQueue, g_sigint_received, etc.) stay in keyhunt.cpp
- MODE_* macros removed from search_common.h and search_context.h to avoid conflict with cli.h search_mode_t enum values (enum is canonical source)
- search_context.h struct definitions (address_value, bPload, publickey, thread_counter, thread_flag) replaced with #include globals.h, using THREAD_COUNTER_DEFINED include guard
- cleanup_general_resources() and cleanup_all_resources() kept in keyhunt.cpp (they reference both static and shared globals)
- BSGS-specific externs in search_common.h retained (they point to mode_bsgs.cpp/bsgs_globals.h, not keyhunt.cpp)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed MODE_* macro/enum conflict**
- **Found during:** Task 2 (replacing extern declarations)
- **Issue:** search_context.h and search_common.h defined MODE_* as macros BEFORE including globals.h, which transitively includes cli.h with search_mode_t enum using the same names. The macro expansion inside enum caused compile errors.
- **Fix:** Moved globals.h include before macro definitions and removed MODE_* macros entirely (enum values from cli.h are the canonical source)
- **Files modified:** src/search/search_context.h, src/search/search_common.h
- **Verification:** Build compiles without errors
- **Committed in:** 3ada473 (Task 2 commit)

**2. [Rule 3 - Blocking] Removed duplicate struct definitions from search_context.h**
- **Found during:** Task 2 (replacing extern declarations)
- **Issue:** search_context.h defined address_value, bPload, publickey, thread_counter, thread_flag structs that were now also in globals.h (included via search_common.h -> globals.h), causing redefinition errors
- **Fix:** Replaced struct definitions in search_context.h with #include globals.h
- **Files modified:** src/search/search_context.h
- **Verification:** Build compiles without redefinition errors
- **Committed in:** 3ada473 (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (2 blocking issues)
**Impact on plan:** Both auto-fixes necessary for compilation. No scope creep.

## Issues Encountered
None beyond the auto-fixed deviations above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- globals.h provides centralized access to all shared state
- keyhunt.cpp at 1334 lines (down from 1504), ready for further main() slimming (plan 04-10)
- mode_bsgs.cpp has only 1 extern (ops table), no longer depends on keyhunt.cpp symbols

---
*Phase: 04-monolith-decomposition*
*Completed: 2026-03-06*
