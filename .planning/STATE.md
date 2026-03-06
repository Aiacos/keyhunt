---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: in-progress
stopped_at: Completed 04-10-PLAN.md
last_updated: "2026-03-06T12:29:53.167Z"
last_activity: "2026-03-06 -- Plan 04-09 complete: Globals extraction from keyhunt.cpp to globals.h/cpp"
progress:
  total_phases: 6
  completed_phases: 4
  total_plans: 26
  completed_plans: 26
  percent: 100
---

---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: in-progress
stopped_at: Completed 04-09-PLAN.md
last_updated: "2026-03-06T12:02:00Z"
last_activity: "2026-03-06 -- Plan 04-09 complete: Globals extraction from keyhunt.cpp to globals.h/cpp"
progress:
  [██████████] 100%
  completed_phases: 4
  total_plans: 26
  completed_plans: 25
  percent: 96
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-02-28)

**Core value:** Every search result must be cryptographically correct -- correctness and stability under sustained load are non-negotiable.
**Current focus:** Phase 4 -- Monolith Decomposition

## Current Position

Phase: 4 of 6 (Monolith Decomposition) -- Gap Closure
Plan: 9 of 10 in current phase (gap closure plans 09-10)
Status: In Progress
Last activity: 2026-03-06 -- Plan 04-09 complete: Globals extraction from keyhunt.cpp to globals.h/cpp

Progress: [█████████░] 96% (25/26 total plans)

## Performance Metrics

**Velocity:**
- Total plans completed: 25
- Average duration: ~15 min
- Total execution time: ~5.4 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1 - Test Baseline | 6/6 | ~98 min | ~16 min |
| 2 - Sanitizer Coverage | 4/4 | ~37 min | ~9 min |
| 3 - Config Migration | 6/6 | ~76 min | ~13 min |
| 4 - Monolith Decomposition | 9/10 | ~207 min | ~23 min |

**Recent Trend:**
- Last 5 plans: 04-06 (~69 min), 04-07 (~45 min), 04-08 (~20 min), 04-09 (~11 min)
- Trend: Gap closure plans executing quickly; globals extraction straightforward

*Updated after each plan completion*
| Phase 04 P10 | 45min | 2 tasks | 11 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Roadmap]: Fix existing tests before adding new ones -- can't trust new tests if foundation is broken
- [Roadmap]: Complete config migration before decomposing keyhunt.cpp -- config wiring enables clean extraction
- [Roadmap]: Sanitizer work precedes config migration -- migration creates aliasing patterns sanitizers catch
- [01-01]: Fixed AVX2 ModMulK1 multiplication bug rather than working around it in tests -- production code was genuinely broken
- [01-01]: imm_umul carry chain pattern is the correct approach for 256x256 multiplication in AVX2 path
- [01-01]: EC() and Add2/AddDirect require affine coordinates -- always Reduce() before calling
- [01-02]: 2G Y-coordinate verified via Python -- plan had incorrect reference value
- [01-02]: SHA256 SIMD checksum uses SSE-vs-AVX2 cross-validation (Transform2 has internal scalar/SIMD differences)
- [01-02]: Batch-vs-single ModInv avoids exact field boundary values due to representation differences
- [01-03]: Used Bitcoin puzzle #21 for BSGS E2E test -- BSGS min parameter constraints make small ranges unusable
- [01-03]: tests/1to32.txt contains puzzle addresses, NOT sequential private keys 1-32
- [01-04]: Coverage threshold 70% not 80% -- AVX-512/SHA-NI/Random excluded as hardware-dependent, 72.8% achieved
- [01-04]: VANITY mode writes to VANITYKEYFOUND.txt (not KEYFOUNDKEYFOUND.txt like other modes)
- [01-04]: Fixed TEST_SHARED_OBJS and cli.cpp linkage -- benefits sanitizer/coverage builds
- [01-05]: GetLowestBit/ShiftL32BitAndSub are private -- tested indirectly via GCD/Div
- [01-05]: SECP256K1.cpp 57% vs 60% target acceptable -- remaining gaps are AVX2/AVX-512 variants
- [01-05]: Cross-validation (SSE 4-point vs single-point) is the testing pattern for batch hash functions
- [01-06]: sha256_sse.cpp standalone tests use NIST abc vector for 1B and cross-validate 2B against scalar
- [01-06]: AVX2 GetHash160 tests cross-validate 8-way output against 4-way SSE batches (efficient cross-validation)
- [01-06]: Coverage gate raised from 70% to 80% -- actual coverage 85.3%, TEST-13 fully satisfied
- [02-01]: gpu_config_t atomic fields use memory_order_relaxed for init (no contention at startup)
- [02-01]: bsgs_fast variables renamed to g_bsgs_avx2/g_bsgs_avx512 to avoid shadowing keyhunt.cpp global
- [02-01]: AVX-512 passed as false to bsgs_fast_set_cpu_features() since sysinfo does not yet expose it
- [02-01]: C11 _Atomic fallback in config.h for future C file compatibility
- [02-02]: bsgs_found tight-loop reads use memory_order_relaxed for performance (delayed visibility acceptable)
- [02-02]: __atomic builtins (not _Atomic) in C files for consistency with adaptive_scheduler.c and ABI safety
- [02-02]: bsgs_found allocated with new std::atomic<int>[N]{} instead of calloc for proper zero-init
- [02-03]: getBits/setBits bounds check returns 0 for out-of-range (Knuth convention for implicit zero digits)
- [02-03]: Newton iteration uses uint64_t (only low 64 bits matter for Montgomery constant)
- [02-03]: SWAP_ADD/SWAP_SUB use unsigned casts: bit-level result identical but avoids signed overflow UB
- [02-03]: ripemd160_32 uses aligned state[5] buffer then memcpy to digest (avoids misaligned uint32_t)
- [02-03]: sqlite3 did not produce UBSan findings, no exemption needed
- [02-03]: searchbinary mock uses 20-byte comparison matching production cmp_hash20 behavior
- [02-04]: Thread tests use nanosleep(10ms) not volatile spin -- volatile contradicts MEM-03 and TSan flags it
- [02-04]: GPU none-backend returns -1 from init to signal no GPU (tests SKIP properly on headless machines)
- [02-04]: XPoint endomorphism test InitK1/SetupField args must be static (store raw pointers)
- [03-01]: Config bridge placed after CLI parsing, secondary bridge before each thread creation path for runtime-allocated state
- [03-01]: flagsearch_to_key_format() handles SEARCH_COMPRESS(1)/KEYTYPE_COMPRESSED(0) value swap between legacy and new enums
- [03-01]: All runtime_state_t new fields are void* for C compatibility with comments documenting actual C++ types
- [03-01]: sequential_max default 0x100000000ULL; max_address_length default 20
- [03-01]: Schema freeze comment block prevents field removal during Phase 3
- [03-02]: Helper functions receive globals as explicit parameters rather than keeping extern declarations
- [03-02]: addvanity() uses local extern declarations inside function body for pre-thread global mutation
- [03-02]: Legacy FLAGSEARCH/FLAGCRYPTO/FLAGENDOMORPHISM ints derived from config enums at thread entry to minimize code churn
- [03-02]: CPU_GRP_SIZE defined locally when search_context.h removed; should move to search_common.h later
- [03-03]: Local shadow variables pattern: extract config values into locals matching old global names at function entry
- [03-03]: FLAGSEARCH conversion at thread_process entry: KEYTYPE_COMPRESSED(0)->SEARCH_COMPRESS(1) via ternary
- [03-03]: cpu_use_y_parity_for_compressed_btc parameterized (6 explicit args instead of 6 externs)
- [03-03]: io.cpp writekey signature change deferred to Phase 4 (10+ call sites would break)
- [03-04]: bsgs_context_t stores pointers to globals (not copies) -- Int/Point are 256-bit, threads share read-only
- [03-04]: bsgs_ctx declared at main() scope to ensure lifetime outlives all BSGS threads (fixed segfault)
- [03-04]: bPload threads keep local extern declarations since they run during init before bsgs_context exists
- [03-04]: Tasks 1+2 committed atomically since BSGS thread functions call helper functions with new signatures

- [03-05]: io.cpp local externs (22 globals) are Phase 4 cleanup targets, not Phase 3 scope
- [03-05]: THREADOUTPUT accessed via config->runtime.thread_output std::atomic<int>& reference
- [03-05]: search_common.h BSGS externs retained for bPload threads running before bsgs_context_t exists
- [03-06]: is_base_minikey check uses raw_baseminikey != NULL instead of mode check -- semantically correct for -C flag detection
- [03-06]: CFG-07 marked Partial not Complete -- io.cpp 22 local externs deferred to Phase 4
- [04-01]: File-reading functions keep function-local externs (init-time only, not worth parameterizing yet)
- [04-01]: g_kh_config_ptr file-scope pointer used for GPU found callback to access keyhunt_config_t
- [04-01]: range_progress_start/end fields added to runtime_state_t for writekey range validation
- [04-01]: acquire_base_key stays in keyhunt.cpp (deeply coupled to 8+ globals, future extraction target)
- [04-01]: Reuse core/workpool.h for WorkPool struct; util/work_queue.h wraps include + extern g_work_pool
- [04-02]: Crypto defaults (BTC) stay in keyhunt.cpp before file reading since readFileAddress depends on FLAGCRYPTO
- [04-02]: extern const required for C++ dispatch table entries (const at file scope has internal linkage)
- [04-02]: Mode init functions are stubs; extracted logic is thread creation (run function)
- [04-02]: MINIKEYS/VANITY remain inline in keyhunt.cpp until future extraction plans
- [04-03]: Vanity/minikey mode init stubs are no-ops; state setup in CLI parsing before config bridge
- [04-03]: Inline fallback switch eliminated; mode_dispatch() now unconditional for all non-BSGS modes
- [04-04]: BSGS globals remain in keyhunt.cpp with extern access from mode_bsgs.cpp (move first, clean later)
- [04-04]: mode_bsgs_run ignores tids/thread_count parameters since init allocates its own global arrays
- [04-04]: shutdown_work_queue made non-static for mode_bsgs.cpp cross-file access
- [04-04]: config.search.target_file wired in config bridge to pass fileName to mode init functions
- [04-05]: GPU dispatch uses extern globals (not config-parameterized) as temporary bridge pattern
- [04-05]: g_kh_config_ptr and check_sigint_cleanup made non-static for gpu_dispatch.cpp cross-file access
- [04-05]: 600-line keyhunt.cpp target deferred -- requires CLI/monitoring/GPU-resolution extraction as separate plans
- [04-06]: Suppressed noisy clang-tidy checks (macro-parentheses, narrowing-conversions, widening-multiplication) as false positives for SIMD-heavy codebase
- [04-06]: cppcheck --check-level=normal with vendored code exclusion to keep analysis tractable (full analysis times out)
- [04-06]: Reserved identifiers in sort.h renamed to kh_ prefix per C++ standard
- [04-07]: BSGS globals owned by mode_bsgs.cpp; bsgs_globals.h header for cross-TU access
- [04-07]: 49 shared-state externs retained in mode_bsgs.cpp (secp, flags, counters, ranges are NOT BSGS-specific)
- [04-07]: io.cpp config bridge: pre-call wiring + post-call sync for gradual global elimination
- [04-07]: Vanity state wired into config BEFORE readFileVanity (not after like original config bridge)
- [04-08]: monitoring_params_t struct used for dependency injection instead of adding more extern declarations
- [04-08]: Globals remain in keyhunt.cpp (extern bridge) since moving them would break 100+ extern references
- [04-08]: 600-line target not achievable without global elimination; 1504 lines achieved (55% reduction from 3374)
- [04-08]: Cleaned up 13 unused variables from main() after extraction
- [04-09]: Shared globals centralized in globals.h/cpp; file-local statics stay in keyhunt.cpp
- [04-09]: MODE_* macros removed from search_common.h/search_context.h to avoid conflict with cli.h enum values
- [04-09]: search_context.h struct definitions replaced with #include globals.h
- [Phase 04]: Range setup extracted to cli.cpp; config bridge to config.cpp; monitoring params to monitoring.cpp for 638-line keyhunt.cpp

### Pending Todos

None yet.

### Blockers/Concerns

- ~~[Phase 1]: Root cause of 8 test_point and 11 test_intgroup failures is not yet analyzed~~ **RESOLVED** in 01-01: AVX2 ModMulK1 carry propagation bug
- ~~[Phase 1]: `make test` fails on clean build -- linker errors from test_search_rmd160.cpp referencing unlinked functions (from Plan 01-02). See deferred-items.md.~~ **PARTIALLY RESOLVED** in 01-04: Fixed TEST_SHARED_OBJS missing objects; coverage/sanitizer builds now link
- [Phase 6]: GPU correctness verification in CI requires either a hardware GPU runner or software emulation -- infrastructure decision unresolved

## Session Continuity

Last session: 2026-03-06T12:29:53.164Z
Stopped at: Completed 04-10-PLAN.md
Resume file: None
