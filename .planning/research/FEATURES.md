# Feature Research

**Domain:** Production hardening of a high-performance C/C++ cryptographic tool
**Researched:** 2026-02-28
**Confidence:** HIGH (for test/tooling standards), MEDIUM (for specific targets)

---

## Context: What "Production Hardened" Means Here

Keyhunt already has working features. This research answers: *what quality infrastructure does a production-ready C/C++ cryptographic tool have that keyhunt currently lacks?*

The reference bar is set by tools like wolfSSL, libsecp256k1, hashcat, and OpenSSL — projects that ship to security researchers and are trusted for correctness. Keyhunt's core value statement from PROJECT.md: "Every search result must be cryptographically correct — a single false negative wastes compute time, a single false positive wastes human time."

---

## Feature Landscape

### Table Stakes (Users Expect These)

Features that any credible C/C++ cryptographic tool must have. Missing these signals that the tool is a prototype, not a production artifact.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| All existing tests passing | Pre-existing failures (test_point: 8, test_intgroup: 11, SHA256 size mismatches) signal broken mathematical foundations to any auditor | LOW | Fixes are scoped: projective coordinate bugs and SHA256 buffer alignment |
| Unit tests for all crypto primitives with known-good vectors | ISO/IETF test vectors are the standard proof of correctness; any crypto library ships with them | MEDIUM | RIPEMD160, SHA256, SHA512 need full vector coverage; secp256k1 point ops need correctness proof |
| AddressSanitizer clean on all test paths | ASan is the industry baseline for C/C++ memory safety; Google deploys ASan on hundreds of millions of lines; finding bugs = expected on first run | MEDIUM | `make sanitize` target exists; must reach zero findings, not just "run with ASan" |
| ThreadSanitizer clean on threaded code | Multi-threaded crypto tools with shared state (globals, bloom filters, progress counters) are data race magnets | MEDIUM | `make tsan` target exists; current global variable coexistence with config creates race conditions |
| UndefinedBehaviorSanitizer (UBSan) clean | Signed integer overflow and pointer arithmetic UB are common in bit-manipulation crypto code | MEDIUM | Not yet a separate make target; must be added and pass cleanly |
| CI pipeline on at least Linux x86_64 | Every production open-source C++ tool runs CI; absence means regressions ship silently | MEDIUM | GitHub Actions is the standard; must build, run tests, and run ASan on each PR |
| Static analysis gate (clang-tidy or cppcheck) | Static analysis at PR time catches obvious bugs before runtime; industry standard since ~2018 | MEDIUM | Neither is currently wired into CI; cppcheck is lower friction to start |
| Compiler warnings as errors (-Wall -Wextra -Werror) | Warnings-as-errors prevents accumulation of masked bugs; required by OpenSSF Compiler Hardening Guide | LOW | Current Makefile likely has -Wall but not -Werror; adding it will surface latent issues |
| Compiler hardening flags in release builds | OpenSSF guide: -D_FORTIFY_SOURCE=3, -fstack-protector-strong, -fPIE -pie, -Wl,-z,relro -Wl,-z,now | LOW | These are linker/compiler flags; add to Makefile release target; no code changes needed |
| End-to-end correctness tests for all 6 search modes | Tool correctness is verified by finding known keys; if a known puzzle key is not found, mode is broken | MEDIUM | Shell-script E2E tests exist; must become part of CI and test all 6 modes with known answers |
| Deterministic builds on same platform | Required for reproducibility and supply chain trust | LOW | Currently non-deterministic (time-based seeds in some paths); flag in tests |
| Clean Valgrind output (no errors, controlled leaks) | wolfSSL, libsecp256k1, OpenSSL all run valgrind regularly; it's the baseline dynamic analysis tool | MEDIUM | `valgrind --leak-check=full ./run_tests` must produce zero errors |

### Differentiators (Competitive Advantage)

Features that go beyond the typical open-source C++ tool in this domain. Valuable but not universally expected.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Fuzz testing for input parsers | Microsoft SDL requires fuzzing at every untrusted interface; catches bugs static analysis misses; libsecp256k1 runs its own fuzzer | HIGH | Target: address file parser, bloom filter file loader, distributed protocol JSON parser — three untrusted input surfaces |
| Performance regression CI (automated throughput benchmarks) | Prevents silent performance regressions when refactoring SIMD paths; keyhunt's value is speed — regressions kill users silently | HIGH | Google Benchmark + Bencher or simple baseline file comparison; detect >5% regression on address mode throughput |
| Cross-platform CI matrix (Linux + Windows + macOS) | PROJECT.md targets all three; without CI, cross-platform support is a claim not a guarantee | MEDIUM | GitHub Actions matrix strategy; Windows needs MinGW-w64 or MSVC; macOS needs Clang |
| Code coverage gate with tracked trend | Not a magic number, but tracking coverage prevents it from silently dropping; crypto code needs high coverage on critical paths | MEDIUM | lcov/gcov already available via `make coverage`; enforce minimum on crypto primitives (hash, EC math); 80%+ on src/secp256k1/ and src/hash/ |
| Sanitizer-clean GPU test paths | CUDA and OpenCL backends have no sanitizer coverage; GPU memory bugs are silent corruption | HIGH | CUDA-memcheck / compute-sanitizer for CUDA; limited tooling for OpenCL; at minimum test CPU-only paths under ASan |
| Structured error handling with machine-parseable output | Current code has inconsistent fprintf/exit/return-code error paths; library consumers (future) need predictable error surface | MEDIUM | Define keyhunt_error_t; standardize return codes; enables future embedding as library |
| SBOM (Software Bill of Materials) | Supply chain security standard; tracks all dependencies; required for many enterprise and government users | LOW | The main build has zero external dependencies (by design); document this explicitly; SPDX format |
| Formal constant-time verification for key-comparison paths | libsecp256k1 specifically designs for constant-time to prevent timing side-channels; keyhunt compares hashes against bloom filters — timing leaks could theoretically reveal search progress to co-located processes | HIGH | Use valgrind's `--tool=memcheck --track-origins=yes` combined with ctgrind or dudect for timing analysis; flag for future research, not blocking |

### Anti-Features (Commonly Requested, Often Problematic)

Features that seem like good ideas during hardening but create more problems than they solve.

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| 100% code coverage requirement | "More coverage = more quality" | Drives test-writing toward coverage gaming, not correctness; dead code in GPU shims would require impractical mocking; diminishing returns above 80% on tested paths | Set 80% floor on core crypto paths (secp256k1/, hash/), track trend rather than absolute |
| Replace custom test framework with Google Test or Catch2 | External frameworks have better matchers, fixtures, parameterized tests | Adds external dependency; current framework is sufficient; migration is churn with no correctness benefit during hardening | Extend the existing test_framework.h with missing assertion types as needed |
| Full MISRA C:2025 compliance | MISRA is the gold standard for safety-critical C | MISRA was designed for embedded safety-critical systems (automotive, medical); keyhunt is a research/CTF tool; full compliance would require rewriting idiomatic C++ patterns and would block AVX intrinsics usage | Apply MISRA-inspired rules selectively: no undefined behavior, no implicit type conversions in critical paths |
| Integrate OSS-Fuzz (Google's continuous fuzzing service) | Free continuous fuzzing with vulnerability disclosure | OSS-Fuzz has a 90-day public disclosure window; libsecp256k1 explicitly rejected OSS-Fuzz for this reason; for a security research tool, premature public disclosure of found bugs creates more risk than benefit | Run local LibFuzzer targets in CI; disclose on maintainer's schedule |
| Rewrite memory-unsafe components in Rust | Memory safety by construction; Rust is gaining in crypto tools | Out of scope per PROJECT.md; would fragment the codebase; the C++ sanitizer approach (ASan + UBSan + TSan) achieves the same correctness goals with less disruption; Rust FFI boundary would add new failure modes | Sanitizer-clean C++ with hardening flags achieves adequate safety for this use case |
| Windows-specific MSVC-only sanitizers | MSVC's `/sdl` and `/analyze` are thorough | Creates two separate static analysis configurations; clang-tidy and cppcheck work cross-platform; MSVC `/analyze` is additive after cross-platform tools pass | Use cross-platform tools first; add MSVC-specific checks only if cross-platform tools miss Windows-specific bugs |
| Prometheus metrics endpoint | Real-time monitoring dashboard sounds compelling | Adds a network listener to a tool that users run in secure, air-gapped environments; creates new attack surface; progress is already output to stdout | Structured stdout output with machine-parseable JSON progress format is sufficient for scripting; Prometheus is overkill for a CLI tool |
| Automated performance tuning via ML/autotuning | Sounds innovative | Current SIMD dispatch is already hardware-adaptive; adding ML-based tuning adds massive complexity with unclear benefit over manual tuning; maintain PGO build as the performance optimization path | PGO (Profile-Guided Optimization) via `make pgo-use` already provides 5-15% improvement with deterministic results |

---

## Feature Dependencies

```
[ASan/UBSan/TSan clean]
    └──requires──> [All tests passing] (can't sanitizer-run broken tests)
                       └──requires──> [test_point fixes]
                       └──requires──> [test_intgroup fixes]
                       └──requires──> [SHA256 test input fixes]

[CI pipeline (GitHub Actions)]
    └──requires──> [Compiler warnings as errors] (CI must fail on warnings)
    └──requires──> [All tests passing] (CI can't tolerate known failures)
    └──enables──> [Cross-platform CI matrix] (matrix is just config once base CI works)
    └──enables──> [Static analysis gate] (add as CI job after base CI passes)
    └──enables──> [Performance regression CI] (add as separate workflow)

[Static analysis gate]
    └──requires──> [Compiler hardening flags] (analysis is more accurate with -O2 + hardening)

[Code coverage gate]
    └──requires──> [All tests passing] (coverage on broken tests is meaningless)
    └──enables──> [Coverage trend tracking] (historical coverage only meaningful when baseline is green)

[Fuzz testing]
    └──requires──> [ASan clean] (Microsoft SDL: run sanitizers before fuzzing)
    └──requires──> [UBSan clean] (fuzzer findings need clean sanitizer baseline to distinguish new bugs)
    └──enhances──> [Static analysis gate] (fuzzer finds runtime bugs static analysis misses)

[Performance regression CI]
    └──requires──> [CI pipeline] (needs baseline storage and comparison infrastructure)
    └──conflicts──> [Sanitizer builds] (sanitizers 2x slowdown; benchmark only against -O2 release builds)
```

### Dependency Notes

- **All tests passing requires test_point/test_intgroup/SHA256 fixes:** CI cannot be declared green while known test failures exist. Fix tests first, then wire CI.
- **ASan clean requires all tests passing:** Running ASan on a test suite with pre-existing failures produces mixed signal. Sanitizer findings are only actionable when the test suite itself is correct.
- **Fuzz testing requires ASan + UBSan clean:** Microsoft SDL protocol and libFuzzer documentation both specify this ordering. Fuzzing before sanitizer cleanup produces a flood of findings that obscures new ones.
- **Performance regression CI conflicts with sanitizer builds:** Benchmarking must run against `-O2` release builds. Never benchmark sanitizer builds (ASan adds ~2x overhead, masking real regressions).
- **Static analysis gate enhances but does not replace sanitizers:** Static analysis (clang-tidy, cppcheck) finds structural issues at compile time. Sanitizers find runtime behavior. Both are required — they catch different classes of bugs.

---

## MVP Definition

This is a hardening milestone, not a product launch. "MVP" here means: minimum state to call keyhunt production-hardened rather than a research prototype.

### Phase 1: Foundation — Fix What's Broken (v1 of hardening)

Must-have before any quality claims are credible.

- [ ] **Fix test_point 8 failures** — broken EC point tests mean the mathematical foundation is unverified
- [ ] **Fix test_intgroup 11 failures** — broken batch modular inverse tests means BSGS correctness is unverified
- [ ] **Fix SHA256 test input size mismatches** — hash correctness untested for 256-bit inputs
- [ ] **Reach ASan-clean on all test paths** — zero memory safety violations; this is the minimum bar for any C/C++ tool claiming production quality
- [ ] **Compiler warnings as errors** (-Wall -Wextra -Werror) — latent bugs surfaced before runtime

### Phase 2: Automation — CI Pipeline (v1.1)

Required before any merges to main can be trusted.

- [ ] **GitHub Actions CI on Linux x86_64** — builds, tests, and ASan run on every PR
- [ ] **cppcheck static analysis in CI** — catches dead code, null dereferences, suspicious patterns at PR time
- [ ] **End-to-end correctness tests for all 6 modes in CI** — known puzzle key found = mode is correct
- [ ] **Compiler hardening flags in release build** — OpenSSF minimum: -D_FORTIFY_SOURCE=3, -fstack-protector-strong, -fPIE, RELRO

### Phase 3: Breadth — Coverage and Cross-Platform (v1.2)

Required to justify "production-ready on Linux + Windows + macOS" claim.

- [ ] **ThreadSanitizer clean** — multi-threaded code data race free
- [ ] **UBSan clean** — no undefined behavior in hot paths
- [ ] **Code coverage gate: 80%+ on src/secp256k1/ and src/hash/** — crypto primitives covered
- [ ] **Cross-platform CI matrix** — Linux x86_64, Windows x64 (MinGW), macOS

### Add After Foundation is Solid (v1.x)

- [ ] **Performance regression CI** — detect >5% throughput regression in address mode; trigger: after config migration and keyhunt.cpp decomposition complete (structural changes can affect performance)
- [ ] **Fuzz testing for untrusted input parsers** — address file parser, bloom filter cache loader, distributed JSON; trigger: after ASan + UBSan clean baseline established
- [ ] **Valgrind clean (no errors)** — full dynamic analysis; lower priority than ASan since ASan catches the same issues faster

### Future Consideration (v2+)

- [ ] **Formal constant-time analysis** — timing side-channel verification; deferred because keyhunt is not a key-generation or signing tool, just a search tool; timing leaks don't directly expose private keys
- [ ] **SBOM generation** — supply chain documentation; deferred because the main build has zero external dependencies, making SBOM trivial but not urgent
- [ ] **Sanitizer coverage for GPU paths** — CUDA compute-sanitizer requires GPU hardware in CI; expensive to provision; defer until GPU bugs are reported

---

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| Fix test_point / test_intgroup failures | HIGH (correctness) | MEDIUM | P1 |
| ASan clean | HIGH (safety) | MEDIUM | P1 |
| Compiler -Werror | HIGH (prevents regression) | LOW | P1 |
| GitHub Actions CI (Linux) | HIGH (automation) | MEDIUM | P1 |
| E2E correctness tests all 6 modes | HIGH (correctness) | MEDIUM | P1 |
| Compiler hardening flags | HIGH (security) | LOW | P1 |
| cppcheck in CI | MEDIUM (catches structural bugs) | LOW | P2 |
| ThreadSanitizer clean | HIGH (thread safety) | MEDIUM | P2 |
| UBSan clean | HIGH (UB elimination) | MEDIUM | P2 |
| Code coverage gate (crypto paths) | MEDIUM (coverage tracking) | LOW | P2 |
| Cross-platform CI matrix | MEDIUM (platform guarantee) | MEDIUM | P2 |
| Performance regression CI | HIGH (keyhunt's core value is speed) | HIGH | P2 |
| Fuzz testing untrusted inputs | HIGH (security) | HIGH | P2 |
| Valgrind clean | MEDIUM (overlaps ASan) | MEDIUM | P3 |
| Constant-time analysis | LOW (not a signing tool) | HIGH | P3 |
| SBOM generation | LOW (no external deps) | LOW | P3 |

---

## Competitor Feature Analysis

Reference implementations in the same domain:

| Quality Feature | libsecp256k1 | wolfSSL | hashcat | keyhunt (current) |
|---------|--------------|---------|---------|-------------------|
| All tests passing | Yes (exhaustive + fuzz) | Yes | Assumed | No (19 failures) |
| ASan in CI | Yes | Yes | Unknown | Makefile target only, not CI |
| Static analysis (multi-tool) | Yes | Coverity + clang scan-build + Facebook Infer | Unknown | None in CI |
| Fuzz testing | Yes (own fuzzers, declined OSS-Fuzz) | Yes (in-memory + network fuzzers) | Unknown | None |
| Performance benchmarks in CI | Yes (`--enable-benchmark`) | Yes | Yes (OpenBenchmarking.org) | `make pgo-train` only |
| Multi-compiler validation | Yes (gcc, clang, MSVC) | Yes (6+ compilers) | Partial | Not automated |
| Known-vector test coverage | Yes (NIST + RFC vectors) | Yes (NIST) | N/A | Yes (hash tests) |
| Valgrind in CI | Yes | Yes (nightly) | Unknown | Not in CI |
| Cross-platform CI | Yes | Yes | Partial | None |

Key observation: wolfSSL uses *multiple* static analysis tools (Coverity, clang scan-build, Facebook Infer) in combination. No single tool catches everything. For keyhunt, using cppcheck + clang-tidy covers the same ground at zero cost.

---

## Sources

- [OpenSSF Compiler Hardening Guide for C and C++](https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html) — HIGH confidence, official OpenSSF publication
- [NISTIR 8397 / Microsoft Build Reliable and Secure C++ Programs](https://learn.microsoft.com/en-us/cpp/code-quality/build-reliable-secure-programs?view=msvc-170) — HIGH confidence, Microsoft official documentation updated 2025-04-25
- [wolfSSL Testing Overview](https://www.wolfssl.com/overview-of-testing-in-wolfssl/) — HIGH confidence, official wolfSSL documentation
- [google/sanitizers — ASan, TSan, MSan](https://github.com/google/sanitizers) — HIGH confidence, official Google repository
- [libsecp256k1 on OSS-Fuzz](https://github.com/google/oss-fuzz/issues/4708) — MEDIUM confidence, GitHub issue showing libsecp256k1's deliberate decision to not use OSS-Fuzz
- [C/C++ Code Linter GitHub Action (clang-tidy + cppcheck)](https://github.com/marketplace/actions/c-c-code-linter-clang-tidy-clang-format-and-cppcheck) — MEDIUM confidence, GitHub Marketplace
- [Performance Regression Testing in CI/CD: C++ Benchmark Automation](https://markaicode.com/performance-regression-testing-cicd/) — MEDIUM confidence, technical blog verified against multiple sources
- [Qt Quality Assurance: Is 70-80-90-100% Code Coverage Good Enough?](https://www.qt.io/quality-assurance/blog/is-70-80-90-or-100-code-coverage-good-enough) — MEDIUM confidence, from a major C++ tooling vendor
- [Standard library hardening C++26 — WG21 P3471R4](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3471r4.html) — HIGH confidence, official WG21 paper
- [MISRA C:2025](https://www.perforce.com/resources/qac/misra-c-cpp) — MEDIUM confidence (marketing page, but MISRA C:2025 publication in March 2025 is confirmed)
- PROJECT.md, TESTING.md, CONCERNS.md — HIGH confidence, authoritative project context

---

## Confidence Notes

- **Testing standards (ASan, TSan, static analysis, CI):** HIGH confidence. These are settled industry practice documented by Google, Microsoft, OpenSSF, and wolfSSL. Not opinion.
- **Specific coverage percentages (80% for crypto paths):** MEDIUM confidence. The 80% floor is pragmatic industry practice; the specific split (higher for crypto primitives vs. platform code) is a judgment call informed by the critical path analysis in CONCERNS.md.
- **Constant-time analysis as future-only:** MEDIUM confidence. Keyhunt is a search tool, not a signing tool. The argument that timing leaks don't directly expose private keys is sound but not formally verified.
- **Anti-feature rationale for OSS-Fuzz:** HIGH confidence. The libsecp256k1 maintainers explicitly documented this decision; it is not speculation.

---
*Feature research for: keyhunt production hardening*
*Researched: 2026-02-28*
