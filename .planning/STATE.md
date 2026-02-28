# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-02-28)

**Core value:** Every search result must be cryptographically correct — correctness and stability under sustained load are non-negotiable.
**Current focus:** Phase 1 — Test Baseline

## Current Position

Phase: 1 of 6 (Test Baseline)
Plan: 3 of 4 in current phase
Status: Executing
Last activity: 2026-03-01 — Plan 01-03 complete: E2E subprocess tests for ADDRESS, BSGS, XPOINT modes

Progress: [███░░░░░░░] 12% (3/24 estimated total plans)

## Performance Metrics

**Velocity:**
- Total plans completed: 3
- Average duration: ~24 min
- Total execution time: ~1.2 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1 - Test Baseline | 3/4 | ~71 min | ~24 min |

**Recent Trend:**
- Last 5 plans: 01-01 (~45 min), 01-02 (~13 min), 01-03 (~13 min)
- Trend: accelerating (simpler test-only work)

*Updated after each plan completion*

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Roadmap]: Fix existing tests before adding new ones — can't trust new tests if foundation is broken
- [Roadmap]: Complete config migration before decomposing keyhunt.cpp — config wiring enables clean extraction
- [Roadmap]: Sanitizer work precedes config migration — migration creates aliasing patterns sanitizers catch
- [01-01]: Fixed AVX2 ModMulK1 multiplication bug rather than working around it in tests -- production code was genuinely broken
- [01-01]: imm_umul carry chain pattern is the correct approach for 256x256 multiplication in AVX2 path
- [01-01]: EC() and Add2/AddDirect require affine coordinates -- always Reduce() before calling
- [01-02]: 2G Y-coordinate verified via Python -- plan had incorrect reference value
- [01-02]: SHA256 SIMD checksum uses SSE-vs-AVX2 cross-validation (Transform2 has internal scalar/SIMD differences)
- [01-02]: Batch-vs-single ModInv avoids exact field boundary values due to representation differences
- [01-03]: Used Bitcoin puzzle #21 for BSGS E2E test -- BSGS min parameter constraints make small ranges unusable
- [01-03]: tests/1to32.txt contains puzzle addresses, NOT sequential private keys 1-32

### Pending Todos

None yet.

### Blockers/Concerns

- ~~[Phase 1]: Root cause of 8 test_point and 11 test_intgroup failures is not yet analyzed~~ **RESOLVED** in 01-01: AVX2 ModMulK1 carry propagation bug
- [Phase 1]: `make test` fails on clean build -- linker errors from test_search_rmd160.cpp referencing unlinked functions (from Plan 01-02). See deferred-items.md.
- [Phase 6]: GPU correctness verification in CI requires either a hardware GPU runner or software emulation — infrastructure decision unresolved

## Session Continuity

Last session: 2026-03-01
Stopped at: Completed 01-03-PLAN.md
Resume file: None
