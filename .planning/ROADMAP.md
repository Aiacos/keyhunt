# Roadmap: KeyHunt Production Hardening

## Overview

Six phases harden an existing, feature-complete codebase to production quality. The order is dictated by a strict dependency chain: a green test suite must exist before sanitizer output is trustworthy; sanitizers must be clean before config migration creates new aliasing patterns for them to catch; config migration must complete before the monolith can be safely decomposed; all prior work must be stable before CI is wired as a gate; and fuzzing requires a clean sanitizer baseline per the Microsoft SDL protocol. Skipping or reordering any layer multiplies total effort by forcing rework.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [x] **Phase 1: Test Baseline** - Fix all 19 pre-existing test failures and verify all 6 search modes against known answers
- [x] **Phase 2: Sanitizer Coverage** - Achieve ASan, UBSan, and TSan clean builds with zero findings
- [x] **Phase 3: Config Migration** - Eliminate all extern globals by wiring keyhunt_config_t into every search module
- [ ] **Phase 4: Monolith Decomposition** - Reduce keyhunt.cpp from ~5300 lines to a ~600-line orchestrator
- [ ] **Phase 5: CI Pipeline** - Green GitHub Actions matrix on Linux, Windows, and macOS with regression detection
- [ ] **Phase 6: Fuzz and Advanced Verification** - libFuzzer harnesses for all untrusted inputs and GPU known-answer tests

## Phase Details

### Phase 1: Test Baseline
**Goal**: Every test passes and every search mode is verified against a known cryptographic answer
**Depends on**: Nothing (first phase)
**Requirements**: TEST-01, TEST-02, TEST-03, TEST-04, TEST-05, TEST-06, TEST-07, TEST-08, TEST-09, TEST-10, TEST-11, TEST-12, TEST-13
**Success Criteria** (what must be TRUE):
  1. Running `make test` exits 0 with no FAILED lines — the 19 pre-existing failures in test_point and test_intgroup are resolved or explicitly skipped with a documented root cause
  2. SHA256 and RIPEMD160 produce byte-for-byte correct output against published NIST/IETF test vectors
  3. secp256k1 point addition, doubling, and scalar multiplication match reference vectors from bitcoin-core/secp256k1
  4. Each of the 6 search modes (ADDRESS, BSGS, XPOINT, RMD160, VANITY, MINIKEYS) locates a known private key in a controlled test run
  5. gcovr reports at least 80% line coverage on src/secp256k1/ and src/hash/ paths
**Plans:** 6 plans

Plans:
- [x] 01-01-PLAN.md — Fix 36 pre-existing test failures and add SKIP_TEST macro
- [x] 01-02-PLAN.md — Add NIST/IETF/bitcoin-core reference crypto test vectors
- [x] 01-03-PLAN.md — E2E tests for ADDRESS, BSGS, XPOINT modes
- [x] 01-04-PLAN.md — E2E tests for RMD160, VANITY, MINIKEYS modes + coverage gate
- [x] 01-05-PLAN.md — Gap closure: Int.cpp and SECP256K1.cpp coverage tests
- [x] 01-06-PLAN.md — Gap closure: sha256_sse tests + raise coverage gate to 80%

### Phase 2: Sanitizer Coverage
**Goal**: The binary is free of memory errors, undefined behavior, and data races as confirmed by three independent sanitizer builds
**Depends on**: Phase 1
**Requirements**: MEM-01, MEM-02, MEM-03, MEM-04
**Success Criteria** (what must be TRUE):
  1. `make asan` build runs the full test suite with zero ASan or UBSan findings (not suppressed, actually fixed)
  2. `make tsan` build runs multi-threaded search tests with zero TSan data-race reports
  3. All GPU progress counters use std::atomic (no volatile fields remain in gpu_config_t lines 154-157)
  4. `grep -rn "g_avx2_available" src/` shows exactly one definition (in sysinfo.c or config), eliminating the BSGS/address dispatch divergence
**Plans:** 4 plans

Plans:
- [x] 02-01-PLAN.md — Install sanitizer packages, MEM-03 (volatile->atomic in gpu_config_t), MEM-04 (eliminate bsgs_fast.cpp duplicate detection), add make asan alias
- [x] 02-02-PLAN.md — Convert all remaining volatile cross-thread variables to atomics (THREADOUTPUT, bsgs_found, gpu_multi_worker, opencl, adaptive_scheduler)
- [x] 02-03-PLAN.md — ASan+UBSan clean build: run, catalog findings, fix to zero (MEM-01)
- [x] 02-04-PLAN.md — TSan clean build: create multi-threaded tests, run, fix to zero (MEM-02)

### Phase 3: Config Migration
**Goal**: Every search module receives all runtime state through keyhunt_config_t* and reads no extern globals
**Depends on**: Phase 2
**Requirements**: CFG-01, CFG-02, CFG-03, CFG-04, CFG-05, CFG-06, CFG-07, CFG-08, CFG-09
**Success Criteria** (what must be TRUE):
  1. `grep -rn "extern" src/search/search_context.h` returns zero matches — the extern declaration file is empty or deleted
  2. All 6 search module files (search_address.cpp, search_bsgs.cpp, search_bsgs_threads.cpp, search_vanity.cpp, search_minikeys.cpp, search_xpoint.cpp, search_rmd160.cpp, io/io.cpp) compile without warnings when search_context.h is excluded from their include path
  3. A call to keyhunt_config_validate() succeeds before every test run, proving write sites were migrated before read sites
  4. Re-running the Phase 1 end-to-end mode tests still produces correct results, proving no silent mode mismatches were introduced
**Plans:** 6/6 plans complete

Plans:
- [x] 03-01-PLAN.md — Freeze config schema + populate write-sites + migrate xpoint/rmd160 (trivial)
- [x] 03-02-PLAN.md — Wire config into search_minikeys.cpp and search_vanity.cpp
- [x] 03-03-PLAN.md — Wire config into search_address.cpp and io.cpp
- [x] 03-04-PLAN.md — Wire config into search_bsgs.cpp and search_bsgs_threads.cpp (bsgs_context_t)
- [x] 03-05-PLAN.md — Eliminate search_context.h externs + final validation

### Phase 4: Monolith Decomposition
**Goal**: keyhunt.cpp is a thin orchestrator; each search mode is self-contained in src/modes/ with no dependency on keyhunt.cpp symbols
**Depends on**: Phase 3
**Requirements**: STR-01, STR-02, STR-03, STR-04, STR-05, STR-06, STR-07, STR-08
**Success Criteria** (what must be TRUE):
  1. `wc -l keyhunt.cpp` reports 600 lines or fewer
  2. Each file in src/modes/ compiles in isolation without including keyhunt.cpp or any symbol defined only in it
  3. `clang-tidy` with bugprone-* and clang-analyzer-security.* checks exits 0 on the entire src/ tree
  4. `cppcheck --enable=warning,error` exits 0 on the entire src/ tree
  5. The release build compiles with -D_FORTIFY_SOURCE=3, -fstack-protector-strong, and -fcf-protection without warnings
**Plans:** 4/6 plans executed

Plans:
- [ ] 04-01-PLAN.md — Wire io.cpp through config (CFG-07) + extract utilities to src/util/
- [ ] 04-02-PLAN.md — Create modes.h dispatch + extract ADDRESS, XPOINT, RMD160 modes
- [ ] 04-03-PLAN.md — Extract VANITY and MINIKEYS modes
- [ ] 04-04-PLAN.md — Extract BSGS mode (largest extraction)
- [ ] 04-05-PLAN.md — Extract GPU dispatch + slim keyhunt.cpp to orchestrator
- [ ] 04-06-PLAN.md — Static analysis gates (clang-tidy, cppcheck) + hardening flags

### Phase 5: CI Pipeline
**Goal**: Every push and pull request is automatically built and tested on all three target platforms, with regressions caught before merge
**Depends on**: Phase 4
**Requirements**: CI-01, CI-02, CI-03, CI-04, CI-05, CI-06, CI-07, CI-08
**Success Criteria** (what must be TRUE):
  1. A pull request to main shows green check marks for Linux (GCC 12+), Windows (MinGW-w64), and macOS (Clang 14+) build-and-test jobs
  2. The ASan + UBSan CI job runs on every push and fails the PR if any sanitizer finding appears
  3. A 5% or greater throughput regression in the benchmark job blocks merge with a clearly labeled failure message
  4. GPU known-answer correctness jobs for CUDA and OpenCL backends produce a pass or a documented skip (no hardware available) — never a silent green
**Plans**: TBD

### Phase 6: Fuzz and Advanced Verification
**Goal**: All untrusted inputs are covered by fuzz harnesses and GPU cryptographic output is verified byte-for-byte against CPU reference
**Depends on**: Phase 5
**Requirements**: MEM-05, MEM-06, MEM-07, MEM-08, MEM-09
**Success Criteria** (what must be TRUE):
  1. libFuzzer harnesses exist and build for all five targets: secp256k1 point operations, RIPEMD160, SHA256, bloom filter deserialization, and the distributed JSON parser and address/key file parser
  2. Each harness runs for at least 60 seconds under ASan without crashing or finding a bug (corpus seed run)
  3. GPU CUDA and OpenCL kernels produce SHA256 and RIPEMD160 output that matches the CPU reference implementation byte-for-byte on a suite of 1000 known inputs
**Plans**: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 -> 2 -> 3 -> 4 -> 5 -> 6

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Test Baseline | 6/6 | Complete | - |
| 2. Sanitizer Coverage | 4/4 | Complete | - |
| 3. Config Migration | 6/6 | Complete   | 2026-03-06 |
| 4. Monolith Decomposition | 4/6 | In Progress|  |
| 5. CI Pipeline | 0/TBD | Not started | - |
| 6. Fuzz and Advanced Verification | 0/TBD | Not started | - |
