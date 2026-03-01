---
phase: 03-config-migration
plan: 04
subsystem: search
tags: [bsgs, config-migration, thread-args, context-struct, secp256k1]

# Dependency graph
requires:
  - phase: 03-01
    provides: "keyhunt_config_t schema with runtime_state_t.bsgs_context void* field"
provides:
  - "bsgs_context_t struct encapsulating 30+ BSGS algorithm state variables"
  - "Config-wired BSGS thread implementations (5 variants)"
  - "Config-wired BSGS helper functions (secondcheck, thirdcheck, calcualteindex)"
  - "thread_args-based BSGS thread creation in keyhunt.cpp"
affects: [03-05, phase-4]

# Tech tracking
tech-stack:
  added: []
  patterns: [bsgs-context-struct, pointer-to-globals-pattern, local-shadow-variables]

key-files:
  created: []
  modified:
    - src/search/search_common.h
    - src/search/search_bsgs_threads.cpp
    - src/search/search_bsgs.cpp
    - src/search/search_context.h
    - src/keyhunt.cpp

key-decisions:
  - "bsgs_context_t stores pointers to globals (not copies) since Int/Point are 256-bit and threads share read-only"
  - "bsgs_ctx declared at main() scope to ensure lifetime outlives all BSGS threads (segfault fix)"
  - "bPload threads keep local extern declarations since they run during init before bsgs_context is populated"
  - "extern Secp256K1 *secp retained in search_bsgs.cpp as infrastructure extern (not BSGS state)"

patterns-established:
  - "BSGS context pattern: algorithm state in bsgs_context_t, stored as void* in config->runtime.bsgs_context, cast back in thread"
  - "Scope-safe context: declare context structs at main() scope, populate later in mode-specific block"

requirements-completed: [CFG-03]

# Metrics
duration: 25min
completed: 2026-03-01
---

# Phase 3 Plan 4: BSGS Config Migration Summary

**bsgs_context_t struct encapsulating 30+ BSGS algorithm state variables, wired into all 5 BSGS thread variants and 3 helper functions via config->runtime.bsgs_context**

## Performance

- **Duration:** ~25 min (including segfault debugging across sessions)
- **Started:** 2026-03-01T12:20:00Z
- **Completed:** 2026-03-01T12:47:18Z
- **Tasks:** 2 (committed atomically since compilation requires both)
- **Files modified:** 5

## Accomplishments
- Defined bsgs_context_t struct with 30+ fields covering all BSGS Int/Point/bloom/mutex/scalar state
- Migrated all 5 BSGS thread variants (sequential, backward, both, random, dance) from tothread+externs to thread_args+config
- Migrated all 3 BSGS helper functions to accept bsgs_context_t* parameter instead of reading extern globals
- Eliminated all file-scope BSGS extern declarations from search_bsgs_threads.cpp (only infrastructure externs remain)
- BSGS E2E test passes: puzzle #21 privkey 0x1BA534 found correctly

## Task Commits

Tasks 1 and 2 were committed together since they cannot compile independently (thread functions call helper functions with new signatures):

1. **Task 1+2: Define bsgs_context_t, migrate BSGS threads and helpers** - `5d6d137` (feat)

**Plan metadata:** TBD (docs: complete plan)

## Files Created/Modified
- `src/search/search_common.h` - Added bsgs_context_t struct definition (~60 lines) and updated function declarations
- `src/search/search_bsgs_threads.cpp` - Migrated 5 BSGS thread functions + validate_bsgs_nm_values to use thread_args/bsgs_context (+457/-226 lines)
- `src/search/search_bsgs.cpp` - Migrated 3 helper functions to accept bsgs_context_t* parameter
- `src/search/search_context.h` - Updated function declarations, added forward declaration
- `src/keyhunt.cpp` - Added bsgs_context_t population (~40 lines), thread_args creation for BSGS, moved bsgs_ctx to main() scope

## Decisions Made
1. **Pointer-based context struct**: bsgs_context_t stores pointers to Int/Point globals (not copies) because: Int/Point are 256-bit large objects, globals already exist in keyhunt.cpp, multiple threads share read-only values, BSGS_CURRENT needs mutex-protected writes via existing pattern.
2. **main() scope for bsgs_ctx**: Declaring bsgs_ctx inside the BSGS mode block caused a use-after-free segfault because the stack variable was destroyed before threads finished. Moving to main() scope ensures the context outlives all threads.
3. **bPload threads keep local externs**: thread_bPload and thread_bPload_2blooms run during initialization (before bsgs_context exists) and use struct bPload (not thread_args), so they retain local extern declarations inside function bodies.
4. **Single atomic commit**: Tasks 1 and 2 are inseparable for compilation (thread functions call helper functions with new signatures), so they were committed together.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed bsgs_ctx scope causing use-after-free segfault**
- **Found during:** Task 1 (BSGS thread migration)
- **Issue:** bsgs_ctx declared on stack inside BSGS mode block scope. Block ends at line 4172, but threads continue running and accessing the dead bsgs_ctx via config->runtime.bsgs_context. This caused a segfault in the BSGS E2E test.
- **Fix:** Moved bsgs_ctx declaration to main() scope (line 1681, same level as `config`), zero-initialized with memset. Population code remains in BSGS mode block.
- **Files modified:** src/keyhunt.cpp
- **Verification:** BSGS E2E test passes, all other E2E tests pass
- **Committed in:** 5d6d137

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** Essential fix for correctness -- plan warned about this exact issue but the initial implementation placed bsgs_ctx in the wrong scope.

## Issues Encountered
- **bsgs_xvalue struct redefinition**: search_common.h and bsgs_sort.h both defined the struct. Fixed by including bsgs_sort.h FIRST so its BSGS_SORT_H guard prevents the conditional definition in search_common.h.
- **Include order sensitivity**: search_bsgs.cpp and search_bsgs_threads.cpp must include bsgs_sort.h before search_common.h to ensure bsgs_searchbinary() is declared and bsgs_xvalue is not redefined.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Plan 03-05 (eliminate search_context.h externs) can now proceed
- All search modules are now config-wired: search_xpoint, search_rmd160, search_address, search_vanity, search_minikeys, search_bsgs, search_bsgs_threads
- Remaining work: audit and remove extern declarations from search_context.h, validate all modules compile without it

---
*Phase: 03-config-migration*
*Completed: 2026-03-01*
