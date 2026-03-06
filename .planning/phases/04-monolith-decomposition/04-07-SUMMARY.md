---
phase: 04-monolith-decomposition
plan: 07
subsystem: architecture
tags: [bsgs, config-wiring, extern-elimination, global-ownership]

# Dependency graph
requires:
  - phase: 04-04
    provides: "config struct with runtime_state_t fields"
  - phase: 04-05
    provides: "GPU dispatch extraction pattern"
provides:
  - "bsgs_globals.h header declaring BSGS-specific state"
  - "mode_bsgs.cpp owns BSGS globals (no extern bridge to keyhunt.cpp)"
  - "io.cpp readFile functions accept keyhunt_config_t parameter (zero externs)"
affects: [04-08, future-bsgs-refactoring]

# Tech tracking
tech-stack:
  added: []
  patterns: ["config bridge: globals -> config fields -> function parameters -> write-back to globals", "checksumsha256 include guard pattern"]

key-files:
  created: ["src/modes/bsgs_globals.h"]
  modified: ["src/modes/mode_bsgs.cpp", "src/keyhunt.cpp", "src/io/io.cpp", "src/io/io.h", "src/config/config.h", "src/search/search_context.h"]

key-decisions:
  - "BSGS globals owned by mode_bsgs.cpp with bsgs_globals.h header for cross-TU access"
  - "49 shared-state externs retained in mode_bsgs.cpp (flags, counters, secp, ranges) -- these are NOT BSGS-specific"
  - "io.cpp config bridge: pre-call wiring + post-call sync pattern for gradual migration"
  - "Vanity state wired into config BEFORE readFileVanity call to fix timing bug"

patterns-established:
  - "Config bridge pattern: wire globals -> config before call, sync config -> globals after return"
  - "Include guard for shared structs: #ifndef STRUCTNAME_DEFINED / #define STRUCTNAME_DEFINED"

requirements-completed: [STR-02, STR-05]

# Metrics
duration: 45min
completed: 2026-03-06
---

# Phase 04 Plan 07: BSGS Globals Ownership + io.cpp Config Wiring Summary

**BSGS globals moved from keyhunt.cpp to mode_bsgs.cpp ownership via bsgs_globals.h; io.cpp readFile functions fully config-wired with zero extern declarations**

## Performance

- **Duration:** ~45 min (across context continuations)
- **Started:** 2026-03-06T09:30:00Z
- **Completed:** 2026-03-06T10:19:00Z
- **Tasks:** 2/2
- **Files modified:** 7

## Accomplishments
- Moved ~60 BSGS-specific global variables from keyhunt.cpp to mode_bsgs.cpp ownership
- Created bsgs_globals.h as authoritative declaration header for BSGS state
- Eliminated all 43 extern declarations from io.cpp (readFile/writeFile functions)
- All readFile/writeFile functions now accept keyhunt_config_t* parameter
- ADDRESS smoke test passes: 29 keys found in 1to32.txt range

## Task Commits

Each task was committed atomically:

1. **Task 1: Move BSGS globals from keyhunt.cpp to mode_bsgs.cpp** - `a8aa596` (feat)
2. **Task 2: Wire io.cpp readFile functions through config** - `b5607a5` (feat)

## Files Created/Modified
- `src/modes/bsgs_globals.h` - New header declaring all BSGS-specific extern globals (~135 lines)
- `src/modes/mode_bsgs.cpp` - Now DEFINES all BSGS globals; replaced 108 extern declarations with include + definitions
- `src/keyhunt.cpp` - Removed BSGS variable definitions; added config bridge for readFile calls; added cleanup_all_resources wrapper
- `src/io/io.cpp` - Complete rewrite: zero extern declarations, all functions config-parameterized
- `src/io/io.h` - Updated all function signatures to accept keyhunt_config_t* parameter
- `src/config/config.h` - Added io_read_cached and thread_count fields to runtime_state_t
- `src/search/search_context.h` - Added CHECKSUMSHA256_DEFINED include guard

## Decisions Made
- **BSGS ownership boundary:** Only BSGS-specific variables (Int/Point BSGS_*, bloom_bP*, bPtable, checksums, mutexes) moved. Shared state (secp, flags, counters, ranges) remains as extern in mode_bsgs.cpp -- these are used by non-BSGS code paths too.
- **Config bridge pattern for io.cpp:** Pre-call: wire globals into config fields. Post-call: sync config output back to globals. This allows gradual migration without breaking the monitoring loop that still reads globals.
- **Vanity timing fix:** Vanity state (vanity_rmd_targets, vanity_bloom, etc.) must be wired into config BEFORE readFileVanity is called, not after (as the original config bridge did at line 2762).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed output_key_found_box -> output_key_found**
- **Found during:** Task 2 (io.cpp rewrite compilation)
- **Issue:** Previous io.cpp rewrite introduced non-existent function name `output_key_found_box`
- **Fix:** Replaced with correct `output_key_found` from output.h
- **Files modified:** src/io/io.cpp
- **Committed in:** b5607a5

**2. [Rule 1 - Bug] Fixed generate_eth_address_from_pubkey -> generate_binaddress_eth**
- **Found during:** Task 2 (io.cpp rewrite compilation)
- **Issue:** Previous io.cpp rewrite introduced non-existent function `generate_eth_address_from_pubkey`
- **Fix:** Replaced with correct `generate_binaddress_eth` + `tohex_dst` pattern from original code
- **Files modified:** src/io/io.cpp
- **Committed in:** b5607a5

**3. [Rule 3 - Blocking] Added CPU_GRP_SIZE extern to mode_bsgs.cpp**
- **Found during:** Task 1 (mode_bsgs.cpp compilation)
- **Issue:** Removing extern block lost CPU_GRP_SIZE declaration needed by BSGS init
- **Fix:** Added `extern uint32_t CPU_GRP_SIZE;` to shared globals extern section
- **Files modified:** src/modes/mode_bsgs.cpp
- **Committed in:** a8aa596

**4. [Rule 3 - Blocking] Added checksumsha256 struct include guard**
- **Found during:** Task 1 (mode_bsgs.cpp compilation)
- **Issue:** checksumsha256 struct defined in both bsgs_globals.h and search_context.h causing redefinition
- **Fix:** Added `#ifndef CHECKSUMSHA256_DEFINED` guard in both headers
- **Files modified:** src/modes/bsgs_globals.h, src/search/search_context.h
- **Committed in:** a8aa596

---

**Total deviations:** 4 auto-fixed (2 bugs, 2 blocking)
**Impact on plan:** All auto-fixes necessary for compilation. No scope creep.

## Issues Encountered
- io.cpp rewrite from previous context had two non-existent function calls (introduced during the context break). Fixed during Task 2 build verification.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- mode_bsgs.cpp extern count reduced from 108 to 49 (shared-state only)
- io.cpp has zero extern declarations
- keyhunt.cpp reduced by ~100 lines of BSGS variable definitions
- Ready for 04-08 (CLI parser extraction or further keyhunt.cpp decomposition)

---
## Self-Check: PASSED

- All 7 files exist on disk
- Both task commits (a8aa596, b5607a5) found in git log
- io.cpp has 0 extern declarations (excluding comments)
- keyhunt binary builds successfully
- ADDRESS smoke test finds 29 keys

---
*Phase: 04-monolith-decomposition*
*Completed: 2026-03-06*
