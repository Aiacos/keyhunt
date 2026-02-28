# Project Research Summary

**Project:** keyhunt — Production Hardening
**Domain:** C/C++ cryptographic tool hardening (secp256k1, SHA256, RIPEMD160, SIMD, GPU)
**Researched:** 2026-02-28
**Confidence:** HIGH

## Executive Summary

Keyhunt is a high-performance Bitcoin/secp256k1 private key search tool with a working feature set but an unverified mathematical foundation: 19 pre-existing test failures (8 in test_point, 11 in test_intgroup) mean the EC arithmetic and batch modular inversion subsystems have never been proven correct. The consensus from reference implementations in this domain (libsecp256k1, wolfSSL, hashcat) is clear: no quality claim is credible while tests are failing, and no sanitizer or CI output is trustworthy until the test suite is green. The entire hardening effort depends on fixing this baseline first — every subsequent layer (ASan, TSan, config migration, monolith decomposition, CI) creates noise without it.

The recommended hardening approach follows a strict five-layer dependency chain: test foundation → sanitizer coverage → config migration → monolith decomposition → CI pipeline. This order is not arbitrary: sanitizers produce uninterpretable output on broken tests; config migration creates exactly the memory patterns (pointer aliasing, shared mutable state) that sanitizers need to be clean before they can catch; monolith decomposition cannot cleanly extract modules that still depend on extern globals; and CI is a gate, not a foundation. Skipping or reordering any layer multiplies total effort by forcing rework. The recommended toolchain is Clang 18 with ASan/UBSan/TSan as separate CI jobs, Catch2 v3 (amalgamated) for test infrastructure, cppcheck + clang-tidy for static analysis, and GitHub Actions for CI — all chosen to preserve the zero-external-dependency constraint of the main build.

The primary risks are: (1) config migration creating silent mode mismatches if write sites are migrated after read sites; (2) duplicate `g_avx2_available` variables in BSGS vs. address paths causing split dispatch behavior; (3) GPU kernels silently producing wrong cryptographic results with no error codes; and (4) `volatile` fields in gpu_config_t providing no TSan compatibility or memory ordering guarantees. All four risks are preventable with specific migration ordering disciplines and known-answer test coverage for GPU paths.

## Key Findings

### Recommended Stack

The hardening toolchain for this C++17/C11 codebase prioritizes tools that work with the zero-external-dependency constraint and support the SIMD-heavy, GPU-optional build matrix. Sanitizers must run as three separate CI jobs (ASan+UBSan+LeakSanitizer, TSan, Valgrind) — they cannot be combined. Clang 18 is required for MSan and produces better sanitizer stack traces than GCC; GCC 13 is better for coverage builds (gcov integration). The existing `test_framework.h` should be retained for micro-tests; Catch2 v3 amalgamated is additive for mode-level integration tests.

**Core technologies:**
- **Catch2 v3.13.0**: Test runner — amalgamated single-header preserves zero-external-dep build constraint; BDD-style sections map to existing test file structure
- **ASan + UBSan (Clang 18)**: Memory safety and UB detection — industry baseline; catches heap overflows, UB in 256-bit integer arithmetic, and SIMD alignment violations
- **TSan (Clang 18, separate build)**: Thread safety — required for multithreaded search loops and bloom filter shared state; incompatible with ASan, must run in isolation
- **cppcheck 2.19.0 + clang-tidy 18**: Static analysis — covers same ground as Coverity at zero cost; phased check adoption prevents overwhelming existing warnings
- **libFuzzer (Clang 18) + AFL++ 4.x**: Fuzzing — in-process libFuzzer for hash functions (5-20x faster); AFL++ for input parsers with magic-byte mutations
- **nanobench v4.3.11**: Microbenchmarks — single-header, C++17, `batch()` API ideal for per-SIMD-lane throughput tracking
- **Google Benchmark v1.9.5**: Macro-benchmarks — JSON output enables automated >5% regression detection in CI
- **GitHub Actions**: CI platform — matrix across Linux/Windows/macOS with separate sanitizer jobs
- **OpenSSF Compiler Hardening Flags**: Production build hardening — `-D_FORTIFY_SOURCE=3`, `-fstack-protector-strong`, `-fstack-clash-protection`, RELRO linker flags
- **gcovr 7.x**: Coverage reports — local HTML + Cobertura; avoids cloud dependency of codecov.io

**Critical version/compatibility notes:**
- ASan/TSan: Linux x86_64 only; TSan not supported on macOS x86_64
- MSan: Never in CI; SIMD false positive rate makes it unreliable for this codebase
- `-D_FORTIFY_SOURCE=3` requires `-O1+`; current `-O2` is compatible
- GPU backends must be disabled in all sanitizer builds (`-DHAVE_CUDA_BACKEND=0`)

### Expected Features

The quality bar is set by libsecp256k1, wolfSSL, and hashcat. Keyhunt currently lacks all of their common hardening features. The three-phase MVP prioritizes correctness first, automation second, and breadth third.

**Must have (table stakes):**
- Fix test_point 8 failures and test_intgroup 11 failures — broken EC arithmetic tests mean the mathematical foundation is unverified
- Fix SHA256 test input size mismatches (uint32_t[8] vs uint32_t[16]) — hash correctness untested for 256-bit inputs
- ASan-clean on all test paths — zero memory violations; this is the industry minimum for any C/C++ tool claiming production quality
- Compiler warnings as errors (-Wall -Wextra -Werror) — prevents accumulation of masked bugs
- GitHub Actions CI on Linux x86_64 — build + test + ASan on every PR
- End-to-end correctness tests for all 6 search modes — known puzzle key found = mode is correct
- Compiler hardening flags in release build — OpenSSF minimum: FORTIFY_SOURCE=3, stack-protector-strong, PIE, RELRO

**Should have (competitive):**
- ThreadSanitizer clean — multi-threaded search loops with shared bloom filters and progress counters require race-free verification
- UBSan clean — signed integer overflow and pointer arithmetic UB common in bit-manipulation crypto code
- Code coverage gate (80%+ on src/secp256k1/ and src/hash/) — crypto primitives must have tracked coverage
- Cross-platform CI matrix (Linux + Windows MinGW + macOS) — PROJECT.md targets all three; CI is the only guarantee
- cppcheck static analysis in CI — structural bug detection at PR time
- Performance regression CI — keyhunt's core value is speed; >5% throughput regression detection required

**Defer (v2+):**
- Formal constant-time analysis — timing side-channels are low risk for a search tool (not a signing tool); defer
- SBOM generation — main build has zero external deps; trivial but low urgency
- Sanitizer coverage for GPU paths — requires GPU hardware in CI; provision only after CPU hardening is complete
- OSS-Fuzz integration — 90-day public disclosure window is inappropriate for a security research tool

**Anti-features to avoid:**
- 100% code coverage requirement — drives gaming, not correctness; set 80% floor on crypto paths instead
- MSan in CI — SIMD false positive rate makes it unreliable; run weekly at most
- Prometheus metrics endpoint — adds network listener to air-gapped tool; structured stdout JSON is sufficient
- Replacing custom secp256k1 with Rust — out of scope; sanitizer-clean C++ achieves same safety goals

### Architecture Approach

Production hardening of this codebase follows a strict five-layer dependency chain where each layer creates preconditions for the next. Violating the order creates rework: sanitizers on broken tests produce noise; config migration during active sanitizer work creates ambiguous findings; monolith decomposition of modules still tied to extern globals just moves the problem. The architecture research identifies clean component boundaries that can be hardened in isolation (src/secp256k1/, src/hash/, src/bloom/, src/platform/, src/config/) and tangled components that require the migration work first (src/search/ modules, keyhunt.cpp).

**Major components and hardening strategy:**
1. **Test Foundation (keyhunt.cpp, tests/)** — fix 19 pre-existing failures before touching any other component; establish green CI baseline
2. **Sanitizer Layer (build system)** — three separate CMake presets: `asan` (ASan+UBSan+LSan), `tsan` (TSan), `valgrind`; GPU disabled in all three
3. **Config Migration (src/config/, src/search/)** — eliminate all `extern` declarations in search_context.h; pass `keyhunt_config_t*` via thread arg structs; freeze struct schema before migration begins; migrate write sites before read sites
4. **Monolith Decomposition (keyhunt.cpp → src/search/)** — reduce from ~5300 lines to ~600-line orchestrator; extract in order: MINIKEYS → VANITY → XPOINT/RMD160 → ADDRESS → BSGS; boundary rule: search/ may not include keyhunt.cpp symbols
5. **CI Pipeline (GitHub Actions)** — matrix Linux/Windows/macOS; sanitizer jobs Linux-only; all jobs green before first merge to main

**Key patterns:**
- Config struct dependency injection: thread arg bundles pass `keyhunt_config_t*` as the sole external dependency
- Strangler Fig extraction: forward-declare the extracted function in keyhunt.cpp, linker verifies; delete stub after verification
- Sanitizer build types as named CMake presets alongside Debug/Release: `asan`, `tsan`, `ubsan`, `coverage`, `fuzz`
- Freeze-then-migrate: lock `keyhunt_config_t` schema before any module migration begins; never change struct shape during migration

### Critical Pitfalls

1. **Duplicate g_avx2_available causing BSGS/address dispatch divergence** — bsgs_fast.cpp has a `static bool g_avx2_available` independent of the keyhunt.cpp global; they can diverge, silently causing one mode to use scalar and another to use AVX2. Prevention: detect CPU features exactly once in sysinfo.c, store in `config->autotune`, delete all module-local copies. Verify with `grep -rn "g_avx2_available" src/` showing exactly one definition.

2. **Pre-existing test failures masking new regressions** — 19 known failures mean CI stays red regardless of new bugs introduced. Prevention: fix or SKIP-with-documented-reason every pre-existing failure before Phase 1 is declared done. CI must start from a fully green baseline; never add a CI job that is knowingly red.

3. **Config migration creating silent mode mismatches** — if a reader migrates to `config->search.mode` before the writer migrates from `FLAGMODE =`, the config field is zero-initialized (MODE_ADDRESS) and every `-m bsgs` run silently uses the wrong algorithm. Prevention: always migrate write sites first; add `keyhunt_config_validate()` that asserts non-zero required fields; run this in every test.

4. **volatile instead of std::atomic for GPU progress counters** — `volatile uint64_t` in gpu_config_t provides no memory ordering and causes TSan races. Prevention: replace every `volatile T` counter with `std::atomic<T>` using `memory_order_relaxed`; use `memory_order_seq_cst` for the `should_stop` flag. Zero performance cost.

5. **GPU kernels silently producing wrong cryptographic results** — SHA256 padding or RIPEMD160 byte-order bugs in CUDA/OpenCL kernels produce plausible-looking but wrong hashes; the search returns zero results, indistinguishable from "key not in range." Prevention: known-answer tests that compare GPU kernel output byte-for-byte against CPU reference; add `--self-test` flag that runs before any `-G` mode search.

## Implications for Roadmap

Based on the combined research, the strict dependency chain from the architecture research defines the phase order. Deviating from this order multiplies effort.

### Phase 1: Test Baseline
**Rationale:** The entire hardening effort rests on a green test suite. Sanitizers, CI, and any correctness claims are meaningless while 19 tests are failing. This is the single highest-leverage action: fix it once and all subsequent phases work from a trustworthy baseline.
**Delivers:** Zero pre-existing test failures; all 6 search modes verified against known answers; SHA256/RIPEMD160 hash correctness against IETF vectors
**Features addressed:** Fix test_point 8 failures, fix test_intgroup 11 failures, fix SHA256 size mismatches, end-to-end correctness tests for all 6 modes
**Pitfalls avoided:** Pre-existing failures masking new regressions (Pitfall 2); prevents false confidence in Phase 2 sanitizer output
**Research flag:** Standard patterns — well-documented secp256k1 and SHA256 test vectors available from NIST/IETF; no additional research needed

### Phase 2: Sanitizer Coverage
**Rationale:** Green tests are the prerequisite. With a clean baseline, every sanitizer finding is a real bug. Sanitizer work before config migration is critical because migration creates new aliasing and shared-state access patterns that sanitizers will catch.
**Delivers:** ASan-clean, UBSan-clean, TSan-clean binaries; `make sanitize`/`make tsan` targets passing in CI
**Features addressed:** ASan clean on all test paths, ThreadSanitizer clean, UBSan clean
**Uses from stack:** Clang 18 for all sanitizer builds; ASan+UBSan+LSan as one job; TSan as separate job; Valgrind as third job
**Pitfalls avoided:** SIMD false positives (Pitfall 3) — handle with per-function `no_sanitize` annotations, not file-level suppression; volatile/atomic issue (Pitfall 4) discovered here and fixed before config migration
**Research flag:** Needs attention — SIMD + sanitizer interaction requires careful annotation strategy; AVX2/AVX-512 aligned-stack false positives documented but need per-file audit

### Phase 3: Config Migration
**Rationale:** Sanitizers must be clean before migration because migration creates new pointer-passing patterns. The extern global problem (94 externs across 6 files) must be resolved before any module extraction — extracted modules that still reference globals just move the problem.
**Delivers:** Zero `extern` declarations in search_context.h; all search modules receive `keyhunt_config_t*` via thread arg; `volatile` replaced with `std::atomic`; single `g_avx2_available` source
**Features addressed:** Compiler warnings as errors (externs surfaced by -Wextern-initializer); ThreadSanitizer clean (volatile → atomic)
**Architecture component:** Config struct dependency injection pattern; freeze struct schema before migration begins
**Pitfalls avoided:** Duplicate CPU feature variables (Pitfall 1); config migration mode mismatches (Pitfall 7); volatile/atomic races (Pitfall 4)
**Research flag:** Needs attention — migration ordering discipline (write-before-read) is critical and requires per-module verification; `keyhunt_config_validate()` must be added before migration begins

### Phase 4: Monolith Decomposition
**Rationale:** Config migration must complete before extraction. Extracting a module that still uses extern globals moves the problem, not solves it. With config injection complete, each module has a clean dependency boundary and can be extracted atomically.
**Delivers:** keyhunt.cpp reduced from ~5300 to ~600 lines (orchestrator only); each search mode self-contained in src/search/; boundary enforced: search/ never includes keyhunt.cpp symbols
**Extraction order:** MINIKEYS → VANITY → XPOINT/RMD160 → ADDRESS → BSGS (least coupled first)
**Architecture component:** Strangler Fig pattern; compile-time verification via forward declarations in keyhunt.cpp
**Pitfalls avoided:** Decomposing before config migration (Anti-Pattern 3 in ARCHITECTURE.md); build system drift — resolve Makefile vs. CMake authority before adding new source files (Pitfall 5)
**Research flag:** Standard patterns — Strangler Fig is well-documented; no additional research needed; dual build system sync requires one-time decision on CI authority

### Phase 5: CI Pipeline and Hardening
**Rationale:** CI is a gate, not a foundation. All prior phases must be stable before wiring CI — otherwise CI manages "known failures" and teams stop treating red as a signal. This phase adds the automation that prevents regressions on all three platforms.
**Delivers:** GitHub Actions matrix (Linux/Windows/macOS); sanitizer jobs Linux-only; cppcheck + clang-tidy in CI; compiler hardening flags in release build; performance regression detection (>5%); code coverage gate (80% on src/secp256k1/ and src/hash/)
**Uses from stack:** GitHub Actions matrix strategy; cppcheck 2.19.0; clang-tidy 18 (phased check adoption); OpenSSF HARDEN_FLAGS; Google Benchmark JSON output for regression detection; gcovr 7.x for coverage
**Pitfalls avoided:** Dual build system drift (Pitfall 5) — CI job that diffs source file lists between Makefile and CMakeLists.txt; CI added before tests green (Anti-Pattern 5 in ARCHITECTURE.md)
**Research flag:** Standard patterns — GitHub Actions matrix is well-documented; Windows MinGW build may surface platform/ layer gaps; no deep research needed

### Phase 6: Fuzz and Advanced Verification (Post-Foundation)
**Rationale:** Fuzzing requires a clean ASan + UBSan baseline (Microsoft SDL protocol). This phase is additive after the foundation is solid.
**Delivers:** libFuzzer harnesses for 5 priority targets (secp256k1 point add, ripemd160, sha256, bloom deserialize, address decode); GPU known-answer tests; Valgrind-clean report
**Uses from stack:** libFuzzer (Clang 18); AFL++ 4.x as secondary fuzzer; compute-sanitizer for CUDA paths
**Pitfalls avoided:** GPU kernels silently wrong (Pitfall 6) — known-answer tests added in this phase; bloom filter cache corruption (PITFALLS.md security section) — HMAC-SHA256 over file content
**Research flag:** Needs attention — GPU correctness verification requires GPU hardware or software emulation in CI; this is the only phase with unresolved infrastructure requirement

### Phase Ordering Rationale

The order is dictated by the dependency DAG identified in ARCHITECTURE.md:
- Tests must be green before sanitizers — otherwise sanitizer output is noise
- Sanitizers must be clean before config migration — migration creates aliasing patterns sanitizers catch
- Config migration must be complete before decomposition — extracted modules with extern globals just move the problem
- All of the above must be stable before CI — CI with known failures normalizes red
- Fuzzing requires clean sanitizer baseline per Microsoft SDL

This ordering also minimizes total effort: doing Phase 2 before Phase 1 doubles sanitizer noise; doing Phase 4 before Phase 3 means extraction work is done twice.

### Research Flags

Phases needing deeper research or careful execution attention:
- **Phase 2 (Sanitizer Coverage):** SIMD + ASan/UBSan interaction requires file-by-file audit; AVX2/AVX-512 aligned-stack false positives are documented but each requires individual triage. Per-function `no_sanitize` annotations must be documented with explanatory comments.
- **Phase 3 (Config Migration):** Write-before-read ordering discipline is critical; `keyhunt_config_validate()` infrastructure must be in place before migration starts; ~94 externs is a large surface area requiring systematic tracking.
- **Phase 6 (Fuzzing/GPU Verification):** GPU correctness verification has an unresolved CI infrastructure requirement; fuzzing harnesses for cryptographic code require understanding the coverage boundary limitation documented in STACK.md.

Phases with well-documented, standard patterns (no additional research needed):
- **Phase 1 (Test Baseline):** NIST/IETF test vectors are public; secp256k1 test vectors available from bitcoin-core/secp256k1
- **Phase 4 (Monolith Decomposition):** Strangler Fig pattern is well-documented; extraction order is clear from dependency analysis
- **Phase 5 (CI Pipeline):** GitHub Actions matrix strategy is standard; OpenSSF hardening flags are prescriptive

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | All tool recommendations verified against official releases (Catch2 v3.13.0, nanobench v4.3.11, Google Benchmark v1.9.5, cppcheck 2.19.0). Sanitizer compatibility matrix verified against official Clang/GCC docs. |
| Features | HIGH | Quality standards drawn from wolfSSL, libsecp256k1, OpenSSF official documentation. Coverage percentages (80% floor) are MEDIUM — pragmatic industry practice, not a hard standard. |
| Architecture | HIGH | Five-layer dependency chain verified against OpenSSF hardening guide, LLVM safety-at-scale documentation, and Martin Fowler's Strangler Fig pattern. Specific line counts (keyhunt.cpp ~5300) are HIGH confidence from codebase audit. |
| Pitfalls | HIGH (code-specific) / MEDIUM (process) | Code-specific pitfalls (duplicate g_avx2_available, volatile in gpu_config_t, pre-existing failures) verified by direct codebase inspection. Process pitfalls cross-referenced with multiple sources. |

**Overall confidence:** HIGH

### Gaps to Address

- **GPU CI infrastructure:** Phase 6 fuzzing and GPU known-answer tests require either a GPU runner in CI or software emulation. No decision has been made on provisioning. This is the only unresolved infrastructure requirement. Handle during Phase 5 planning by deciding: GPU hardware runner, software emulation (CUDA CPU emulator), or deferring GPU correctness tests to manual validation.
- **Specific secp256k1 test vector failures:** The 8 test_point and 11 test_intgroup failures are documented as pre-existing but the root cause is not analyzed in the research. Are they testing deprecated behavior, or actual arithmetic bugs? This needs a targeted investigation before Phase 1 begins. Low effort: run failing tests in a debugger, identify the expected vs. actual values.
- **Distributed mode TLS:** PITFALLS.md flags that distributed mode has no TLS by default. This is a security issue but is not blocked by the hardening phases. Flag for tracking; address in Phase 5 (hardening) or post-v1 depending on priority.
- **Coverage percentages as CI gate thresholds:** The 80% floor on src/secp256k1/ and src/hash/ is a reasonable starting point but actual current coverage is unknown. Measure baseline coverage in Phase 1 before setting enforcement thresholds.

## Sources

### Primary (HIGH confidence)
- [OpenSSF Compiler Hardening Guide for C/C++](https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html) — compiler flags, FORTIFY_SOURCE, stack protection
- [LLVM libFuzzer Documentation](https://llvm.org/docs/LibFuzzer.html) — in-process fuzzing, SIMD coverage boundary
- [LLVM AddressSanitizer Documentation](https://clang.llvm.org/docs/AddressSanitizer.html) — flag combinations, suppression patterns
- [LLVM UndefinedBehaviorSanitizer Documentation](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) — UB categories, halt_on_error
- [GitHub Starter Workflows: CMake multi-platform](https://github.com/actions/starter-workflows/blob/main/ci/cmake-multi-platform.yml) — matrix strategy
- [Catch2 Releases v3.13.0](https://github.com/catchorg/catch2/releases) — version confirmation
- [wolfSSL Testing Overview](https://www.wolfssl.com/overview-of-testing-in-wolfssl/) — reference quality bar
- [Martin Fowler: Strangler Fig Application](https://martinfowler.com/bliki/StranglerFigApplication.html) — incremental monolith extraction
- Codebase direct inspection: src/config/config.h, src/bsgs/bsgs_fast.cpp, src/search/search_context.h, keyhunt.cpp — codebase-verified pitfalls

### Secondary (MEDIUM confidence)
- [wolfSSL: Multi-tool static analysis (Coverity + Infer + clang scan-build)](https://www.wolfssl.com/overview-of-testing-in-wolfssl/) — feature comparison benchmark
- [GCC 13 Ubuntu: ASan/AVX-512 alignment false positive — Launchpad #2023424](https://bugs.launchpad.net/ubuntu/+source/gcc-13/+bug/2023424) — SIMD sanitizer pitfall validation
- [libsecp256k1 on OSS-Fuzz: deliberate rejection](https://github.com/google/oss-fuzz/issues/4708) — anti-feature rationale for OSS-Fuzz
- [Botan Fuzzing Guide](https://botan.randombit.net/handbook/dev_ref/fuzzing.html) — cryptographic fuzzing coverage boundary
- [Red Hat: Memory error checking in C/C++ — ASan vs Valgrind comparison](https://developers.redhat.com/blog/2021/05/05/memory-error-checking-in-c-and-c-comparing-sanitizers-and-valgrind) — tool selection rationale

### Tertiary (LOW confidence)
- [GCC AVX2 target attribute optimization bug — Codeforces 2024](https://codeforces.com/blog/entry/149099) — community report, single source
- [Wrong results from OpenCL driver — NVIDIA Developer Forums](https://forums.developer.nvidia.com/t/wrong-results-from-the-opencl-driver/14803) — GPU silent wrong results pattern

---
*Research completed: 2026-02-28*
*Ready for roadmap: yes*
