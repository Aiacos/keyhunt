---
phase: 03-config-migration
plan: 02
subsystem: config
tags: [keyhunt_config_t, thread_args, minikeys, vanity, extern-elimination]

# Dependency graph
requires:
  - phase: 03-01
    provides: frozen keyhunt_config_t schema with runtime_state_t fields and config bridge in keyhunt.cpp
provides:
  - Config-wired search_minikeys.cpp (zero extern globals, thread_args entry)
  - Config-wired search_vanity.cpp (zero extern globals, thread_args entry)
  - thread_args thread creation for MODE_MINIKEYS and MODE_VANITY in keyhunt.cpp
  - Validated migration pattern for helper function parameter threading
affects: [03-03, 03-04, 03-05]

# Tech tracking
tech-stack:
  added: []
  patterns: [config-extraction-at-thread-entry, helper-function-parameter-threading, local-extern-for-pre-thread-mutation, legacy-flag-derivation-from-enum]

key-files:
  created: []
  modified:
    - src/search/search_minikeys.cpp
    - src/search/search_vanity.cpp
    - src/search/search_common.h
    - src/keyhunt.cpp

key-decisions:
  - "Helper functions (set_minikey, increment_minikey_index, increment_minikey_N) received coinbuffer/minikeyN as parameters instead of reading extern globals"
  - "addvanity() uses local extern declarations inside function body for pre-thread global mutation rather than config reads"
  - "Derived legacy-compatible FLAGSEARCH/FLAGCRYPTO/FLAGENDOMORPHISM ints from config enums to avoid changing all comparison sites"
  - "vanityrmdmatch() and writevanitykey() receive keyhunt_config_t* parameter for vanity state and secp access"
  - "CPU_GRP_SIZE defined locally in search_vanity.cpp after removing search_context.h dependency"

patterns-established:
  - "Config extraction: Extract all config reads as local variables at thread entry, before main loop"
  - "Helper function threading: When helper functions read globals, add parameters rather than keeping externs"
  - "Local extern pattern: For functions called before thread creation (addvanity), use local extern declarations inside function body"
  - "Legacy flag derivation: Convert config enums to legacy int values for minimal code churn in comparison sites"

requirements-completed: [CFG-04, CFG-05]

# Metrics
duration: 25min
completed: 2026-03-01
---

# Phase 03 Plan 02: Minikeys + Vanity Config Migration Summary

**Eliminated all extern globals from search_minikeys.cpp and search_vanity.cpp by wiring keyhunt_config_t via thread_args, with helper function parameter threading and local extern pattern for pre-thread mutation**

## Performance

- **Duration:** ~25 min
- **Started:** 2026-03-01T11:50:00Z
- **Completed:** 2026-03-01T12:18:06Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- search_minikeys.cpp: zero extern globals (was 16), reads all state from thread_args->config
- search_vanity.cpp: zero config-mapped extern globals (was ~20), reads all state from thread_args->config; only infrastructure externs remain (sysinfo, workpool, profiling)
- keyhunt.cpp: MODE_MINIKEYS and MODE_VANITY thread creation uses thread_args with config pointer
- Helper functions updated with explicit parameters instead of extern global reads
- All E2E tests pass (5/5 pass, 0 fail)

## Task Commits

Each task was committed atomically:

1. **Task 1: Wire config into search_minikeys.cpp** - `3829d11` (feat)
2. **Task 2: Wire config into search_vanity.cpp** - `5ab863c` (feat)

## Files Created/Modified
- `src/search/search_minikeys.cpp` - Rewrote thread entry to use thread_args->config, removed 16 externs, added parameters to helper functions
- `src/search/search_vanity.cpp` - Rewrote thread entry, vanityrmdmatch, writevanitykey to use config; addvanity uses local externs; removed search_context.h dependency
- `src/search/search_common.h` - Updated minikey helper function signatures with new parameters (coinbuffer, minikeyN)
- `src/keyhunt.cpp` - Changed MODE_MINIKEYS and MODE_VANITY to create thread_args instead of passing tothread

## Decisions Made

1. **Helper function parameter threading (Task 1):** The plan stated set_minikey, increment_minikey_index, and increment_minikey_N "don't read globals" but they actually read Ccoinbuffer, minikeyN, and minikey_n_limit as extern globals. Added these as explicit parameters and updated search_common.h declarations. This is the correct approach -- parameters are explicit and testable.

2. **addvanity() local extern pattern (Task 2):** addvanity() is called from keyhunt.cpp before threads start, mutating global vanity state that later gets copied into config. Rather than passing config (which isn't populated yet at call time), used local extern declarations inside the function body. The results flow through the config bridge.

3. **Legacy flag derivation (Task 2):** Vanity code uses `FLAGSEARCH == SEARCH_COMPRESS` comparisons extensively. Since `SEARCH_COMPRESS=1` but `KEYTYPE_COMPRESSED=0` (values swapped), deriving a local `int FLAGSEARCH` from the config enum avoids touching dozens of comparison sites while maintaining correctness.

4. **CPU_GRP_SIZE local definition (Task 2):** After removing search_context.h from search_vanity.cpp, CPU_GRP_SIZE (1024) was undefined. Added a guarded `#ifndef/#define` block. This constant could later be moved to search_common.h.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added parameters to minikey helper functions**
- **Found during:** Task 1 (search_minikeys.cpp migration)
- **Issue:** Plan stated helper functions "don't read globals" but set_minikey, increment_minikey_index, increment_minikey_N all accessed Ccoinbuffer/minikeyN/minikey_n_limit as extern globals
- **Fix:** Added explicit parameters to all three functions and updated search_common.h declarations
- **Files modified:** src/search/search_minikeys.cpp, src/search/search_common.h
- **Verification:** Build succeeds, E2E tests pass
- **Committed in:** 3829d11 (Task 1 commit)

**2. [Rule 3 - Blocking] Added CPU_GRP_SIZE definition after removing search_context.h**
- **Found during:** Task 2 (search_vanity.cpp migration)
- **Issue:** CPU_GRP_SIZE macro was provided by search_context.h which was removed during migration
- **Fix:** Added `#ifndef CPU_GRP_SIZE / #define CPU_GRP_SIZE 1024 / #endif` block
- **Files modified:** src/search/search_vanity.cpp
- **Verification:** Build succeeds, VANITY E2E test passes
- **Committed in:** 5ab863c (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (2 blocking)
**Impact on plan:** Both fixes necessary for compilation. No scope creep. Pattern is sound -- future migrations should check for macro dependencies from removed headers.

## Issues Encountered
- Git stash contamination: A prior git stash pop leaked partial search_address.cpp migration changes into the working tree. Resolved by reverting search_address.cpp with `git checkout` before committing Task 2 changes.
- GPU config bridge leak: Stash also introduced GPU/hardware detection bridge lines in keyhunt.cpp. Removed as out-of-scope for this plan since neither minikeys nor vanity reads config->gpu or config->autotune.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- search_address.cpp (MODE_ADDRESS, MODE_XPOINT, MODE_RMD160) is the next migration target (Plan 03-03)
- search_address.cpp still uses search_context.h and tothread; migration will follow the same pattern validated here
- The local extern pattern for addvanity() can be replicated for any init-time functions in other modules
- CPU_GRP_SIZE should be added to search_common.h in a future cleanup to avoid per-file definitions

---
*Phase: 03-config-migration*
*Completed: 2026-03-01*
