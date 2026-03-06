---
phase: 04-monolith-decomposition
plan: 02
subsystem: core
tags: [refactoring, dispatch-table, mode-extraction, thread-dispatch]

# Dependency graph
requires:
  - phase: 04-monolith-decomposition/01
    provides: Config-wired writekey/writekeyeth, shared util headers, work queue module
provides:
  - Mode dispatch table interface (mode_ops_t, mode_get_ops, mode_dispatch)
  - ADDRESS mode dispatcher (mode_address.cpp)
  - XPOINT mode dispatcher (mode_xpoint.cpp)
  - RMD160 mode dispatcher (mode_rmd160.cpp)
affects: [04-03, 04-04, 04-05, 04-06]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Mode dispatch table pattern: mode_ops_t with init/run/cleanup function pointers"
    - "Extern const linkage pattern for C++ dispatch table entries (avoids internal linkage of const)"

key-files:
  created:
    - src/modes/modes.h
    - src/modes/modes.cpp
    - src/modes/mode_address.cpp
    - src/modes/mode_xpoint.cpp
    - src/modes/mode_rmd160.cpp
  modified:
    - src/keyhunt.cpp
    - Makefile

key-decisions:
  - "Crypto defaults (BTC) stay in keyhunt.cpp before file reading since readFileAddress depends on FLAGCRYPTO"
  - "Mode init functions are currently no-ops; thread creation is the extracted logic"
  - "extern const pattern required for C++ dispatch table entries (const has internal linkage by default)"
  - "MINIKEYS and VANITY modes remain in keyhunt.cpp inline switch until future extraction plans"

patterns-established:
  - "Mode dispatch: mode_get_ops(mode) returns ops struct, mode_dispatch(config, tids, count) calls init+run"
  - "Mode file structure: static init/run/cleanup functions, extern const mode_*_ops export"

requirements-completed: [STR-01, STR-03]

# Metrics
duration: 11min
completed: 2026-03-06
---

# Phase 4 Plan 02: Mode Dispatch Table and ADDRESS/XPOINT/RMD160 Extraction Summary

**Dispatch table interface (mode_ops_t) with ADDRESS, XPOINT, and RMD160 thread creation extracted to src/modes/**

## Performance

- **Duration:** 11 min
- **Started:** 2026-03-06T07:06:20Z
- **Completed:** 2026-03-06T07:17:28Z
- **Tasks:** 2
- **Files modified:** 7 (5 created, 2 modified)

## Accomplishments
- Created mode_ops_t dispatch table interface with init/run/cleanup function pointers
- Extracted ADDRESS, XPOINT, and RMD160 thread creation from keyhunt.cpp inline switch to dedicated mode files
- keyhunt.cpp thread creation loop now uses mode_dispatch() for registered modes, with fallback for MINIKEYS/VANITY
- All three modes verified working: ADDRESS finds 29 keys, XPOINT ~15 Mkeys/s, RMD160 ~48 Mkeys/s

## Task Commits

Each task was committed atomically:

1. **Task 1: Create modes.h dispatch interface and mode_address.cpp** - `e9e4a71` (feat)
2. **Task 2: Extract mode_xpoint.cpp and mode_rmd160.cpp** - `b2eacb5` (feat)

## Files Created/Modified
- `src/modes/modes.h` - Dispatch table interface (mode_ops_t, mode_get_ops, mode_dispatch)
- `src/modes/modes.cpp` - Dispatch table implementation mapping MODE_ADDRESS/XPOINT/RMD160 to ops
- `src/modes/mode_address.cpp` - ADDRESS mode: init (no-op) + run (thread_process creation)
- `src/modes/mode_xpoint.cpp` - XPOINT mode: init (no-op) + run (thread_process creation)
- `src/modes/mode_rmd160.cpp` - RMD160 mode: init (no-op) + run (thread_process creation)
- `src/keyhunt.cpp` - Uses mode_dispatch() for ADDRESS/XPOINT/RMD160, inline switch for MINIKEYS/VANITY
- `Makefile` - Added MODES_OBJS, obj/modes/ directory

## Decisions Made
- **Crypto defaults stay in keyhunt.cpp:** readFileAddress checks FLAGCRYPTO to determine address format (BTC vs ETH). The default-to-BTC logic for ADDRESS and RMD160 modes must execute BEFORE file reading, so it stays in keyhunt.cpp rather than moving to mode init functions.
- **Mode init is no-op for now:** The shared initialization (file reading, sorting, config bridge, GPU upload) is deeply intertwined in keyhunt.cpp. Mode init functions are stubs; the extracted logic is thread creation (the run function).
- **extern const for dispatch table entries:** In C++, `const` at file scope has internal linkage by default. The `extern const mode_*_ops` forward declaration is required to give the ops structs external linkage so the dispatch table in modes.cpp can reference them.
- **MINIKEYS/VANITY stay inline:** These modes will be extracted in future plans (04-04). The dispatch table gracefully falls back to inline creation for unregistered modes.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] C++ const internal linkage for dispatch table entries**
- **Found during:** Task 1 (Build after creating mode_address.cpp)
- **Issue:** `const mode_ops_t mode_address_ops` had internal linkage in C++ (const implies static), causing undefined reference at link time with LTO
- **Fix:** Added `extern const mode_ops_t mode_address_ops;` forward declaration before definition to force external linkage
- **Files modified:** src/modes/mode_address.cpp (same pattern applied to xpoint and rmd160)
- **Committed in:** e9e4a71

---

**Total deviations:** 1 auto-fixed (Rule 3 - blocking build issue)
**Impact on plan:** Fix necessary for correct C++ linkage. No scope creep.

## Issues Encountered
None beyond the auto-fixed linkage issue.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Dispatch table pattern established for remaining mode extractions (VANITY, MINIKEYS in 04-04; BSGS in 04-03)
- src/modes/ directory created and integrated into build system
- Mode files compile independently without including keyhunt.cpp
- All unit tests pass (make test)

---
*Phase: 04-monolith-decomposition*
*Completed: 2026-03-06*
