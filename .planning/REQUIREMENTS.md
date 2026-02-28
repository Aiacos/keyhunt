# Requirements: KeyHunt Production Hardening

**Defined:** 2026-02-28
**Core Value:** Every search result must be cryptographically correct — correctness and stability under sustained load are non-negotiable.

## v1 Requirements

Requirements for production-ready release. Each maps to roadmap phases.

### Testing Foundation

- [x] **TEST-01**: All 19 pre-existing test failures fixed (test_point x8, test_intgroup x11) -- completed in 01-01, also fixed AVX2 ModMulK1 bug
- [x] **TEST-02**: SHA256 test input size mismatches corrected (uint32_t[8] -> uint32_t[16]) -- completed in 01-01
- [x] **TEST-03**: NIST/IETF test vectors for SHA256 (FIPS 180-4 examples)
- [x] **TEST-04**: NIST/IETF test vectors for RIPEMD160
- [x] **TEST-05**: Test vectors for secp256k1 point operations (addition, doubling, scalar multiplication)
- [x] **TEST-06**: Test vectors for secp256k1 batch modular inversion (IntGroup)
- [ ] **TEST-07**: End-to-end correctness test for ADDRESS mode (known privkey → known address)
- [ ] **TEST-08**: End-to-end correctness test for BSGS mode (known pubkey in known range)
- [ ] **TEST-09**: End-to-end correctness test for XPOINT mode (known X-coordinate)
- [ ] **TEST-10**: End-to-end correctness test for RMD160 mode (known RIPEMD160 hash)
- [ ] **TEST-11**: End-to-end correctness test for VANITY mode (known prefix match)
- [ ] **TEST-12**: End-to-end correctness test for MINIKEYS mode
- [ ] **TEST-13**: Code coverage gate at 80% on crypto paths (secp256k1, hash, bloom) via gcovr

### Memory Safety

- [ ] **MEM-01**: ASan + UBSan clean build with zero findings on full test suite
- [ ] **MEM-02**: TSan clean build with zero findings on multi-threaded search tests
- [ ] **MEM-03**: Replace volatile with std::atomic for GPU counters in config.h (lines 154-157)
- [ ] **MEM-04**: Fix duplicate g_avx2_available in bsgs_fast.cpp (unified SIMD dispatch)
- [ ] **MEM-05**: libFuzzer harness for secp256k1 point operations
- [ ] **MEM-06**: libFuzzer harness for RIPEMD160 and SHA256 hash functions
- [ ] **MEM-07**: libFuzzer harness for bloom filter deserialization (untrusted file input)
- [ ] **MEM-08**: libFuzzer harness for distributed JSON parser (untrusted network input)
- [ ] **MEM-09**: libFuzzer harness for address/key file parser (untrusted file input)

### Config Migration

- [ ] **CFG-01**: Freeze keyhunt_config_t struct schema (no mid-migration shape changes)
- [ ] **CFG-02**: Wire config parameter into search_address.cpp (eliminate extern globals)
- [ ] **CFG-03**: Wire config parameter into search_bsgs.cpp and search_bsgs_threads.cpp
- [ ] **CFG-04**: Wire config parameter into search_vanity.cpp
- [ ] **CFG-05**: Wire config parameter into search_minikeys.cpp
- [ ] **CFG-06**: Wire config parameter into search_xpoint.cpp and search_rmd160.cpp
- [ ] **CFG-07**: Wire config parameter into io/io.cpp
- [ ] **CFG-08**: Eliminate search_context.h extern declarations (target: 0 remaining)
- [ ] **CFG-09**: All search modules accept keyhunt_config_t* as parameter, no global reads

### Code Structure

- [ ] **STR-01**: Extract ADDRESS mode dispatcher from keyhunt.cpp to src/modes/
- [ ] **STR-02**: Extract BSGS mode dispatcher from keyhunt.cpp to src/modes/
- [ ] **STR-03**: Extract XPOINT and RMD160 mode dispatchers from keyhunt.cpp to src/modes/
- [ ] **STR-04**: Extract VANITY and MINIKEYS mode dispatchers from keyhunt.cpp to src/modes/
- [ ] **STR-05**: keyhunt.cpp reduced to thin dispatcher (~600 lines or less)
- [ ] **STR-06**: Static analysis gate with clang-tidy (bugprone-* + clang-analyzer-security.*)
- [ ] **STR-07**: Static analysis gate with cppcheck (error + warning level)
- [ ] **STR-08**: OpenSSF compiler hardening flags applied (-D_FORTIFY_SOURCE=3, -fstack-protector-strong, -fcf-protection)

### CI Pipeline

- [ ] **CI-01**: GitHub Actions workflow builds and tests on Linux x86_64 (GCC 12+)
- [ ] **CI-02**: GitHub Actions workflow builds and tests on Windows x64 (MinGW-w64)
- [ ] **CI-03**: GitHub Actions workflow builds and tests on macOS (Clang 14+)
- [ ] **CI-04**: ASan + UBSan CI job (Clang 18, GPU disabled, runs on every push)
- [ ] **CI-05**: TSan CI job (Clang 18, separate build, runs on PRs)
- [ ] **CI-06**: Performance regression benchmark CI with JSON output and threshold detection
- [ ] **CI-07**: GPU known-answer correctness test for CUDA backend
- [ ] **CI-08**: GPU known-answer correctness test for OpenCL backend

## v2 Requirements

Deferred to future release. Tracked but not in current roadmap.

### Advanced Testing

- **ADV-01**: MSan (MemorySanitizer) clean build — requires full dependency recompilation
- **ADV-02**: P2SH and BECH32 address mode implementation and testing
- **ADV-03**: Distributed mode integration tests (server + client coordination)

### Build System

- **BLD-01**: Resolve Makefile/CMake drift (single source of truth or sync enforcement)
- **BLD-02**: Windows MSVC native build support (not just MinGW)

### Documentation

- **DOC-01**: API documentation for all public functions
- **DOC-02**: Architecture decision records (ADRs) for major design choices

## Out of Scope

| Feature | Reason |
|---------|--------|
| New search algorithms | Hardening existing, not adding new capabilities |
| ARM64/mobile support | Primary user base is x86_64 desktop/server |
| GUI or web interface | CLI tool — out of project scope |
| Rewrite of keyhunt_legacy.cpp | Deprecate, don't rewrite — not worth the effort |
| Rewrite of bsgsd.cpp | Deprecate, don't rewrite — separate concern |
| OSS-Fuzz integration | Rejected by libsecp256k1 for documented reasons that apply here |
| 100% code coverage target | Drives gaming rather than correctness — 80% on crypto paths is sufficient |
| Replacing custom test framework | Pure churn — extend, don't replace test_framework.h |
| MISRA C compliance | Scope mismatch — designed for safety-critical embedded, not performance tools |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| TEST-01 | Phase 1 | Pending |
| TEST-02 | Phase 1 | Pending |
| TEST-03 | Phase 1 | Complete |
| TEST-04 | Phase 1 | Complete |
| TEST-05 | Phase 1 | Complete |
| TEST-06 | Phase 1 | Complete |
| TEST-07 | Phase 1 | Pending |
| TEST-08 | Phase 1 | Pending |
| TEST-09 | Phase 1 | Pending |
| TEST-10 | Phase 1 | Pending |
| TEST-11 | Phase 1 | Pending |
| TEST-12 | Phase 1 | Pending |
| TEST-13 | Phase 1 | Pending |
| MEM-01 | Phase 2 | Pending |
| MEM-02 | Phase 2 | Pending |
| MEM-03 | Phase 2 | Pending |
| MEM-04 | Phase 2 | Pending |
| MEM-05 | Phase 6 | Pending |
| MEM-06 | Phase 6 | Pending |
| MEM-07 | Phase 6 | Pending |
| MEM-08 | Phase 6 | Pending |
| MEM-09 | Phase 6 | Pending |
| CFG-01 | Phase 3 | Pending |
| CFG-02 | Phase 3 | Pending |
| CFG-03 | Phase 3 | Pending |
| CFG-04 | Phase 3 | Pending |
| CFG-05 | Phase 3 | Pending |
| CFG-06 | Phase 3 | Pending |
| CFG-07 | Phase 3 | Pending |
| CFG-08 | Phase 3 | Pending |
| CFG-09 | Phase 3 | Pending |
| STR-01 | Phase 4 | Pending |
| STR-02 | Phase 4 | Pending |
| STR-03 | Phase 4 | Pending |
| STR-04 | Phase 4 | Pending |
| STR-05 | Phase 4 | Pending |
| STR-06 | Phase 4 | Pending |
| STR-07 | Phase 4 | Pending |
| STR-08 | Phase 4 | Pending |
| CI-01 | Phase 5 | Pending |
| CI-02 | Phase 5 | Pending |
| CI-03 | Phase 5 | Pending |
| CI-04 | Phase 5 | Pending |
| CI-05 | Phase 5 | Pending |
| CI-06 | Phase 5 | Pending |
| CI-07 | Phase 5 | Pending |
| CI-08 | Phase 5 | Pending |

**Coverage:**
- v1 requirements: 43 total
- Mapped to phases: 43
- Unmapped: 0

---
*Requirements defined: 2026-02-28*
*Last updated: 2026-02-28 after roadmap creation*
