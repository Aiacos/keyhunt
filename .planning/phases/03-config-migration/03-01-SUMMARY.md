---
phase: 03-config-migration
plan: 01
subsystem: config
tags: [keyhunt_config_t, runtime_state_t, schema-freeze, config-bridge, extern-removal]

# Dependency graph
requires:
  - phase: 02-sanitizer-coverage
    provides: Clean sanitizer baseline (no UB/data races) for safe config aliasing
provides:
  - Frozen keyhunt_config_t schema (v3) with all fields search modules need
  - Config write-site bridge in keyhunt.cpp (globals to config struct)
  - Zero-extern search_xpoint.cpp and search_rmd160.cpp
affects: [03-02, 03-03, 03-04, 03-05]

# Tech tracking
tech-stack:
  added: []
  patterns: [config-bridge-pattern, flagsearch-to-key-format-conversion, sort-header-include-over-extern]

key-files:
  created: []
  modified:
    - src/config/config.h
    - src/config/config.cpp
    - src/keyhunt.cpp
    - src/search/search_xpoint.cpp
    - src/search/search_rmd160.cpp

key-decisions:
  - "Config bridge placed after CLI parsing and before thread creation, with secondary bridge before each thread creation path for runtime-allocated state"
  - "flagsearch_to_key_format() handles SEARCH_COMPRESS(1)/KEYTYPE_COMPRESSED(0) value swap between legacy and new enums"
  - "All new runtime_state_t fields are void* for C compatibility, with comments documenting the actual C++ types"
  - "sequential_max default is 0x100000000ULL (4GB) matching N_SEQUENTIAL_MAX pattern"
  - "Schema freeze comment block added to prevent field removal during Phase 3 migration"

patterns-established:
  - "Config bridge pattern: populate config from globals after CLI parsing, secondary bridge before thread creation for runtime-allocated state"
  - "Extern elimination: replace extern function declarations with proper #include of header that declares the function"

requirements-completed: [CFG-01, CFG-06]

# Metrics
duration: 10min
completed: 2026-03-01
---

# Phase 3 Plan 1: Config Schema Freeze + Write-Site Bridge + xpoint/rmd160 Migration Summary

**Frozen keyhunt_config_t schema (v3) with 30+ new runtime_state_t fields, config bridge populating from globals in keyhunt.cpp, and zero-extern search_xpoint/search_rmd160 using sort.h include**

## Performance

- **Duration:** 10 min
- **Started:** 2026-03-01T11:46:42Z
- **Completed:** 2026-03-01T11:57:00Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments
- Froze keyhunt_config_t schema at version 3 with all 30+ fields search modules need (secp, thread arrays, generator points, endomorphism constants, range params, minikey/vanity/BSGS state)
- Added config bridge in keyhunt.cpp that populates all config fields from globals after CLI parsing, with secondary bridges before both BSGS and non-BSGS thread creation paths
- Migrated search_xpoint.cpp and search_rmd160.cpp to zero extern declarations by replacing extern searchbinary with #include "../sort/sort.h"
- All tests pass: make test (all passed), make test-e2e (5 pass, 0 fail, 1 expected skip)

## Task Commits

Each task was committed atomically:

1. **Task 1: Add missing fields to runtime_state_t and freeze config schema (CFG-01)** - `1397928` (feat)
2. **Task 2: Populate config write-sites in keyhunt.cpp and migrate xpoint/rmd160 (CFG-06)** - `826e006` (feat)

## Files Created/Modified
- `src/config/config.h` - Added 26 void* fields, 4 scalar fields to runtime_state_t; added skip_checksum to search_config_t; bumped KH_CONFIG_VERSION to 3; added SCHEMA FROZEN comment
- `src/config/config.cpp` - Updated kh_runtime_state_init() with defaults for all new fields; updated kh_runtime_state_cleanup() to reset new pointer fields
- `src/keyhunt.cpp` - Added flagsearch_to_key_format() helper; added primary config bridge after CLI parsing; added secondary bridges before BSGS and non-BSGS thread creation; added kh_config_validate() call
- `src/search/search_xpoint.cpp` - Replaced extern searchbinary with #include "../sort/sort.h"
- `src/search/search_rmd160.cpp` - Replaced extern searchbinary with #include "../sort/sort.h"

## Decisions Made
- **Config bridge placement:** Primary bridge after all CLI parsing (line ~2583) for flags/constants known at that point. Secondary bridges right before thread creation for runtime-allocated state (bloom, addressTable, steps, ends). This two-stage approach avoids forward-referencing uninitialized state.
- **FLAGSEARCH value swap:** Created flagsearch_to_key_format() because legacy SEARCH_COMPRESS=1 maps to new KEYTYPE_COMPRESSED=0 (values are swapped). The explicit conversion function prevents subtle bugs.
- **void* for all new fields:** All C++ types (Secp256K1*, Int*, std::vector<Point>*, std::atomic<int>*) stored as void* to maintain C compatibility of the config header. Comments document the actual types.
- **sequential_max default:** Set to 0x100000000ULL in kh_runtime_state_init() matching the large-scale default, while keyhunt.cpp initializes N_SEQUENTIAL_MAX to 4096 (overridden during parameter validation). The bridge copies the runtime value.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Config schema is frozen (v3) with all fields needed by remaining search modules
- Write-site bridge in keyhunt.cpp populates config from globals, enabling read-site migration in plans 03-02 through 03-05
- search_xpoint.cpp and search_rmd160.cpp are fully migrated (zero externs)
- Ready for plan 03-02 (minikeys + vanity config wiring)

## Self-Check: PASSED

All files exist. Both task commits verified in git log.

---
*Phase: 03-config-migration*
*Completed: 2026-03-01*
