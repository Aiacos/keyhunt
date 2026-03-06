---
phase: 04-monolith-decomposition
plan: 03
subsystem: core
tags: [refactoring, mode-extraction, thread-dispatch, vanity, minikeys]

# Dependency graph
requires:
  - phase: 04-monolith-decomposition/02
    provides: Mode dispatch table interface (mode_ops_t), src/modes/ directory and build integration
provides:
  - VANITY mode dispatcher (mode_vanity.cpp)
  - MINIKEYS mode dispatcher (mode_minikeys.cpp)
  - All non-BSGS modes now dispatched through mode_ops_t table
affects: [04-04, 04-05, 04-06]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Unconditional mode_dispatch() for all non-BSGS modes (no more inline fallback switch)"

key-files:
  created:
    - src/modes/mode_vanity.cpp
    - src/modes/mode_minikeys.cpp
  modified:
    - src/modes/modes.cpp
    - src/keyhunt.cpp
    - Makefile

key-decisions:
  - "Vanity/minikey init remains no-op stubs: state setup happens during CLI parsing in keyhunt.cpp before config bridge"
  - "Inline fallback switch eliminated entirely: mode_dispatch() now unconditional for all non-BSGS modes"

patterns-established:
  - "All 5 non-BSGS modes (ADDRESS, XPOINT, RMD160, VANITY, MINIKEYS) use dispatch table pattern"

requirements-completed: [STR-04]

# Metrics
duration: 4min
completed: 2026-03-06
---

# Phase 4 Plan 03: VANITY and MINIKEYS Mode Extraction Summary

**VANITY and MINIKEYS mode dispatchers extracted to src/modes/, completing non-BSGS mode dispatch table with unconditional mode_dispatch() call**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-06T07:21:22Z
- **Completed:** 2026-03-06T07:25:49Z
- **Tasks:** 2
- **Files modified:** 6 (2 created, 4 modified)

## Accomplishments
- Extracted VANITY mode thread creation from keyhunt.cpp inline switch to mode_vanity.cpp
- Extracted MINIKEYS mode thread creation from keyhunt.cpp inline switch to mode_minikeys.cpp
- Eliminated the inline fallback switch entirely -- keyhunt.cpp now uses unconditional mode_dispatch() for all non-BSGS modes
- All 5 non-BSGS modes verified working: ADDRESS (29 keys found), XPOINT, RMD160, VANITY, MINIKEYS

## Task Commits

Each task was committed atomically:

1. **Task 1: Extract mode_vanity.cpp** - `e681771` (feat)
2. **Task 2: Extract mode_minikeys.cpp** - `94f57bb` (feat)

## Files Created/Modified
- `src/modes/mode_vanity.cpp` - VANITY mode dispatcher: init (no-op) + run (thread_process_vanity creation) + cleanup
- `src/modes/mode_minikeys.cpp` - MINIKEYS mode dispatcher: init (no-op) + run (thread_process_minikeys creation) + cleanup
- `src/modes/modes.cpp` - Registered mode_vanity_ops and mode_minikeys_ops in dispatch table
- `src/keyhunt.cpp` - Removed inline fallback switch, simplified to unconditional mode_dispatch()
- `Makefile` - Added mode_vanity.o and mode_minikeys.o to MODES_OBJS

## Decisions Made
- **Vanity/minikey init remains no-op:** The vanity bloom filter and RMD target setup, and minikey base/coinbuffer parsing, all happen during CLI argument processing in keyhunt.cpp before the config bridge. Moving that logic into mode init would require restructuring the CLI parsing flow, which is out of scope for this extraction plan.
- **Inline fallback switch eliminated:** With MINIKEYS being the last mode in the fallback, its extraction allowed removing the conditional `if (mode_get_ops(...))` check entirely. Thread creation now always goes through `mode_dispatch()`.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All 5 non-BSGS modes now dispatch through the mode_ops_t table
- BSGS mode extraction is the next major target (04-04 or later)
- keyhunt.cpp thread creation path significantly simplified
- All unit tests pass (make test)

---
*Phase: 04-monolith-decomposition*
*Completed: 2026-03-06*
