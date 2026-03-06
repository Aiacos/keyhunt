# Phase 4: Monolith Decomposition - Context

**Gathered:** 2026-03-06
**Status:** Ready for planning

<domain>
## Phase Boundary

Reduce keyhunt.cpp from ~5500 lines to a ~600-line thin orchestrator by extracting mode dispatchers into `src/modes/`, absorbing remaining globals into `keyhunt_config_t`, completing io.cpp config wiring (CFG-07), and applying static analysis gates. No new features or search algorithms.

</domain>

<decisions>
## Implementation Decisions

### Extraction granularity
- One file per mode in `src/modes/`: mode_address.cpp, mode_bsgs.cpp, mode_xpoint.cpp, mode_rmd160.cpp, mode_vanity.cpp, mode_minikeys.cpp
- `modes.h` provides dispatch table interface
- BSGS initialization (~700 lines of bloom/table setup) goes into mode_bsgs.cpp alongside dispatch logic (self-contained, ~800 lines)
- GPU dispatch functions (gpu_upload_*, gpu_run_*, gpu_found_callback) move to `src/gpu/` not into mode files
- Utility functions (thread_rand, sleep_ms, profiling, work queue) move to `src/util/`

### Orchestrator scope
- keyhunt.cpp retains: main(), CLI arg parsing, config building, mode_dispatch() call, thread wait, results printing, cleanup
- Target: ~400-600 lines
- Signal handling stays in orchestrator

### io.cpp cleanup
- Full config wiring during decomposition (complete CFG-07)
- Change writekey() signature to accept `const keyhunt_config_t *config`
- Wire all 22 local externs through config parameter
- Natural timing: call sites are moving to mode files anyway, so signature changes are absorbed during extraction

### Global variable strategy
- Absorb all ~100+ globals into keyhunt_config_t sub-structs
- Thread counters (THREADCOUNTER, FINISHED_THREADS_COUNTER, OLDFINISHED_ITEMS) as atomics in runtime_state_t
- Vanity state (vanity_rmd_targets, vanity_rmd_limits, vanity_rmd_limit_values_A/B) packed into vanity_state_t sub-struct
- Mode-specific flags (FLAGENDOMORPHISM, FLAGBSGSMODE, etc.) absorbed into appropriate config sub-structs
- No globals.h fallback -- everything goes through config

### Static analysis gates
- Apply clang-tidy (bugprone-*, clang-analyzer-security.*) and cppcheck during decomposition, not as a separate pass
- All code in src/modes/ must be clang-tidy clean -- fix pre-existing issues when moving code
- Compiler hardening flags applied to entire build: -D_FORTIFY_SOURCE=3, -fstack-protector-strong, -fcf-protection (STR-08)
- Hardening applies to all CXXFLAGS, not just new files

### Claude's Discretion
- Exact extraction order (which mode to extract first)
- How to handle bPload thread externs (Phase 3 noted these run before bsgs_context_t exists)
- Work queue implementation details during extraction
- Exact clang-tidy check selection beyond bugprone-* and clang-analyzer-security.*

</decisions>

<specifics>
## Specific Ideas

- Phase 3 decision [03-03]: io.cpp writekey signature change was explicitly deferred to Phase 4 because of 10+ call sites
- Phase 3 decision [03-05]: io.cpp 22 local externs are Phase 4 cleanup targets
- Phase 3 decision [03-04]: bPload threads keep local extern declarations since they run during init before bsgs_context_t exists -- needs resolution during BSGS extraction
- Existing diagnostic warnings in keyhunt.cpp (misleading indentation, unused includes, anonymous typedef) should be fixed during decomposition

</specifics>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/search/` (7 files): Already config-wired from Phase 3 -- mode dispatchers call these thread functions
- `src/config/config.h`: keyhunt_config_t with search_config_t, bsgs_config_t, gpu_config_t, autotune_config_t, runtime_state_t
- `src/platform/`: Thread creation, mutex, timing abstractions
- `src/cli.h`: Type-safe enums (search_mode_t, key_type_t, gpu_mode_t, bsgs_mode_t)

### Established Patterns
- Config bridge pattern (keyhunt.cpp populates config from globals after CLI parsing)
- Secondary config bridge (refreshes runtime-allocated state before thread creation)
- thread_args struct passed to search threads with config pointer
- Local shadow variables pattern: extract config values into locals matching old global names at function entry

### Integration Points
- `src/modes/` (new) called from main() after config is built
- `src/modes/*.cpp` call `src/search/*.cpp` thread functions
- `src/modes/*.cpp` call `src/io/io.cpp` for key writing (after signature change)
- `src/gpu/gpu_dispatch.cpp` (new) called from mode files that support GPU
- Makefile needs new src/modes/*.o and src/util/*.o object lists

</code_context>

<deferred>
## Deferred Ideas

None -- discussion stayed within phase scope

</deferred>

---

*Phase: 04-monolith-decomposition*
*Context gathered: 2026-03-06*
