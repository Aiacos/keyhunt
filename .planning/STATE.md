---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: in-progress
last_updated: "2026-03-01T08:40:51Z"
progress:
  total_phases: 6
  completed_phases: 1
  total_plans: 10
  completed_plans: 10
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-02-28)

**Core value:** Every search result must be cryptographically correct -- correctness and stability under sustained load are non-negotiable.
**Current focus:** Phase 2 -- Sanitizer Coverage

## Current Position

Phase: 2 of 6 (Sanitizer Coverage)
Plan: 4 of 4 in current phase -- COMPLETE
Status: In Progress
Last activity: 2026-03-01 -- Plan 02-04 complete: TSan threading tests with zero data-race findings

Progress: [████░░░░░░] 38% (10/26 estimated total plans)

## Performance Metrics

**Velocity:**
- Total plans completed: 10
- Average duration: ~14 min
- Total execution time: ~2.3 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1 - Test Baseline | 6/6 | ~98 min | ~16 min |
| 2 - Sanitizer Coverage | 4/4 | ~37 min | ~9 min |

**Recent Trend:**
- Last 5 plans: 01-06 (~5 min), 02-01 (~15 min), 02-02 (~6 min), 02-04 (~16 min)
- Trend: stable velocity

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
- [02-04]: Thread tests use nanosleep(10ms) not volatile spin -- volatile contradicts MEM-03 and TSan flags it
- [02-04]: GPU none-backend returns -1 from init to signal no GPU (tests SKIP properly on headless machines)
- [02-04]: XPoint endomorphism test InitK1/SetupField args must be static (store raw pointers)

### Pending Todos

None yet.

### Blockers/Concerns

- ~~[Phase 1]: Root cause of 8 test_point and 11 test_intgroup failures is not yet analyzed~~ **RESOLVED** in 01-01: AVX2 ModMulK1 carry propagation bug
- ~~[Phase 1]: `make test` fails on clean build -- linker errors from test_search_rmd160.cpp referencing unlinked functions (from Plan 01-02). See deferred-items.md.~~ **PARTIALLY RESOLVED** in 01-04: Fixed TEST_SHARED_OBJS missing objects; coverage/sanitizer builds now link
- [Phase 6]: GPU correctness verification in CI requires either a hardware GPU runner or software emulation -- infrastructure decision unresolved

## Session Continuity

Last session: 2026-03-01
Stopped at: Completed 02-04-PLAN.md (TSan threading tests with zero data-race findings -- Phase 02 complete)
Resume file: None
