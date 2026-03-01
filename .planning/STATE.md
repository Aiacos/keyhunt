---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: in-progress
last_updated: "2026-03-01T12:47:18Z"
progress:
  total_phases: 6
  completed_phases: 2
  total_plans: 15
  completed_plans: 15
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-02-28)

**Core value:** Every search result must be cryptographically correct -- correctness and stability under sustained load are non-negotiable.
**Current focus:** Phase 3 -- Config Migration

## Current Position

Phase: 3 of 6 (Config Migration) -- COMPLETE
Plan: 5 of 5 in current phase -- COMPLETE
Status: Phase 3 Complete
Last activity: 2026-03-01 -- Plan 03-05 complete: search_context.h externs eliminated, Phase 3 validated

Progress: [██████░░░░] 58% (15/26 estimated total plans)

## Performance Metrics

**Velocity:**
- Total plans completed: 12
- Average duration: ~13 min
- Total execution time: ~2.6 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1 - Test Baseline | 6/6 | ~98 min | ~16 min |
| 2 - Sanitizer Coverage | 4/4 | ~37 min | ~9 min |
| 3 - Config Migration | 5/5 | ~71 min | ~14 min |

**Recent Trend:**
- Last 5 plans: 03-01 (~10 min), 03-02 (~25 min), 03-03 (~3 min), 03-04 (~25 min), 03-05 (~8 min)
- Trend: Final cleanup plan fast -- most work done in prior plans

*Updated after each plan completion*

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

### Pending Todos

None yet.

### Blockers/Concerns

- ~~[Phase 1]: Root cause of 8 test_point and 11 test_intgroup failures is not yet analyzed~~ **RESOLVED** in 01-01: AVX2 ModMulK1 carry propagation bug
- ~~[Phase 1]: `make test` fails on clean build -- linker errors from test_search_rmd160.cpp referencing unlinked functions (from Plan 01-02). See deferred-items.md.~~ **PARTIALLY RESOLVED** in 01-04: Fixed TEST_SHARED_OBJS missing objects; coverage/sanitizer builds now link
- [Phase 6]: GPU correctness verification in CI requires either a hardware GPU runner or software emulation -- infrastructure decision unresolved

## Session Continuity

Last session: 2026-03-01
Stopped at: Completed 03-05-PLAN.md (Phase 3 complete)
Resume file: None
