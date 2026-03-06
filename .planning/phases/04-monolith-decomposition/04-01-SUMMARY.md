---
phase: 04-monolith-decomposition
plan: 01
subsystem: core
tags: [config-migration, refactoring, io, profiling, thread-util, work-queue]

# Dependency graph
requires:
  - phase: 03-config-migration
    provides: keyhunt_config_t with runtime_state_t fields, search modules config-wired via thread_args
provides:
  - Config-wired writekey/writekeyeth (no extern globals in writekey path)
  - Shared profiling header (src/util/profiling.h) with profile_counters_t and KH_PROF_* macros
  - Thread utility module (src/util/thread_util.h) with aligned_calloc, thread_rand, sleep_ms
  - Work queue module (src/util/work_queue.h) with extern g_work_pool
affects: [04-02, 04-03, 04-04, 04-05, 04-06]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Config parameter injection for IO functions (writekey receives const keyhunt_config_t*)"
    - "Shared profiling header eliminates duplicated profile_counters_t across modules"
    - "Function-local extern declarations for init-time globals (file-reading functions)"

key-files:
  created:
    - src/util/thread_util.h
    - src/util/thread_util.cpp
    - src/util/profiling.h
    - src/util/profiling.cpp
    - src/util/work_queue.h
    - src/util/work_queue.cpp
  modified:
    - src/io/io.h
    - src/io/io.cpp
    - src/config/config.h
    - src/keyhunt.cpp
    - src/search/search_address.cpp
    - src/search/search_vanity.cpp
    - src/search/search_minikeys.cpp
    - src/search/search_bsgs_threads.cpp
    - src/search/search_common.h
    - Makefile

key-decisions:
  - "File-reading functions keep function-local externs (init-time only, not worth parameterizing yet)"
  - "g_kh_config_ptr file-scope pointer used for GPU found callback to access keyhunt_config_t"
  - "range_progress_start/end fields added to runtime_state_t for writekey range validation"
  - "acquire_base_key stays in keyhunt.cpp (deeply coupled to 8+ globals, future extraction target)"
  - "profiling.h uses platform_time_now_ns() instead of duplicated kh_profile_now_ns() inline"

patterns-established:
  - "Config injection pattern: writekey(config, compressed, key) replaces global extern access"
  - "Shared util headers: search modules include util/profiling.h instead of duplicating struct defs"

requirements-completed: [STR-05]

# Metrics
duration: 16min
completed: 2026-03-06
---

# Phase 4 Plan 01: Config-Wire IO and Extract Utilities Summary

**Config-wired writekey/writekeyeth with zero file-scope externs, extracted thread_util/profiling/work_queue to src/util/**

## Performance

- **Duration:** 16 min
- **Started:** 2026-03-06T06:46:23Z
- **Completed:** 2026-03-06T07:02:43Z
- **Tasks:** 2
- **Files modified:** 16 (6 created, 10 modified)

## Accomplishments
- writekey() and writekeyeth() now accept const keyhunt_config_t* parameter, eliminating 4 file-scope externs (secp, write_keys, g_rangeProgressStart/End)
- Remaining file-reading externs moved to function-local scope (22 file-scope externs -> 0)
- Profiling infrastructure shared across search modules via single header (eliminates 3 copies of profile_counters_t)
- ~250 lines of utility code extracted from keyhunt.cpp to independently compilable modules

## Task Commits

Each task was committed atomically:

1. **Task 1: Wire io.cpp through config and update all call sites** - `70a8168` (feat)
2. **Task 2: Extract utility functions from keyhunt.cpp to src/util/** - `e93d6c4` (feat)

## Files Created/Modified
- `src/util/thread_util.h/cpp` - aligned_calloc, aligned_free, thread_rand, thread_rand_n, sleep_ms
- `src/util/profiling.h/cpp` - profile_counters_t, profile_init_threads, profile_set_thread, profile_aggregate, append_profile_info, KH_PROF_* macros
- `src/util/work_queue.h/cpp` - extern WorkPool g_work_pool (struct in core/workpool.h)
- `src/io/io.h` - Updated writekey/writekeyeth signatures with config parameter
- `src/io/io.cpp` - Config-wired writekey/writekeyeth, function-local externs for file-reading
- `src/config/config.h` - Added range_progress_start/range_progress_end to runtime_state_t
- `src/keyhunt.cpp` - Removed ~250 lines of extracted code, added includes, g_kh_config_ptr
- `src/search/search_address.cpp` - Updated 22 writekey/writekeyeth call sites, replaced profiling duplication
- `src/search/search_vanity.cpp` - Replaced profiling duplication with shared header
- `src/search/search_minikeys.cpp` - Replaced extern profile_set_thread with profiling.h include
- `src/search/search_bsgs_threads.cpp` - Replaced extern profile_set_thread with profiling.h include
- `src/search/search_common.h` - Updated writekey signature, replaced thread_rand with thread_util.h include
- `Makefile` - Added thread_util.o, profiling.o, work_queue.o to UTIL_OBJS

## Decisions Made
- **File-reading externs stay function-local:** readFileAddress, forceReadFileAddress, etc. run at init time and mutate globals directly. Converting to config parameters would require a much larger refactor (not in scope). Function-local externs satisfy the "zero file-scope extern" goal.
- **g_kh_config_ptr for GPU callback:** The gpu_found_callback function is a C-style callback with void* userdata. Rather than threading keyhunt_config_t through the GPU backend API, a file-scope pointer set by main() is simpler and safe (set once before any GPU launch).
- **acquire_base_key stays:** It depends on g_work_pool, g_workQueue, n_range_start, n_range_end, write_random, FLAGRANDOM, N_SEQUENTIAL_MAX, and cpu_cached_block_* thread-locals. Extracting it would require either a large context struct or significant API changes to the work queue system.
- **Reuse core/workpool.h:** The WorkPool struct was already extracted to core/workpool.h in a prior phase. util/work_queue.h just wraps the include and adds the extern g_work_pool declaration.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] C/C++ linkage conflict for thread_rand**
- **Found during:** Task 2 (Build after extraction)
- **Issue:** thread_util.h declared thread_rand with extern "C" linkage, but search_common.h declared it with C++ linkage
- **Fix:** Removed extern "C" wrapper from thread_util.h (all consumers are C++ files). Replaced search_common.h declarations with #include of thread_util.h
- **Files modified:** src/util/thread_util.h, src/search/search_common.h
- **Committed in:** e93d6c4

**2. [Rule 3 - Blocking] kh_profile_now_ns not declared after profiling extraction**
- **Found during:** Task 2 (Build after extraction)
- **Issue:** search_address.cpp used kh_profile_now_ns() which was a local inline removed during profiling extraction
- **Fix:** Replaced with platform_time_now_ns() (available via profiling.h -> platform_time.h)
- **Files modified:** src/search/search_address.cpp
- **Committed in:** e93d6c4

**3. [Rule 3 - Blocking] Duplicate WorkPool struct definition**
- **Found during:** Task 2 (Creating work_queue.h)
- **Issue:** core/workpool.h already had WorkPool struct; creating another in util/work_queue.h would cause ODR violation
- **Fix:** util/work_queue.h includes core/workpool.h and only adds extern g_work_pool declaration
- **Files modified:** src/util/work_queue.h
- **Committed in:** e93d6c4

---

**Total deviations:** 3 auto-fixed (all Rule 3 - blocking build issues)
**Impact on plan:** All fixes necessary to compile. No scope creep.

## Issues Encountered
None beyond the auto-fixed build issues.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- io.cpp writekey path is fully config-wired (CFG-07 complete)
- src/util/ provides shared infrastructure for mode extraction plans (04-02 through 04-06)
- Search modules can now import profiling/work_queue headers instead of duplicating code
- acquire_base_key remains in keyhunt.cpp as a known dependency for future extraction

---
*Phase: 04-monolith-decomposition*
*Completed: 2026-03-06*
