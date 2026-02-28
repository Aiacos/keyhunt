# Stack Research

**Domain:** Production hardening of a performance-critical C/C++ cryptographic tool
**Researched:** 2026-02-28
**Confidence:** HIGH (all major recommendations verified against official docs or recent releases)

---

## Context

This research answers one question: what tooling does a C++17/C11 cryptographic codebase (custom secp256k1, SHA256, RIPEMD160, SIMD-heavy, GPU backends) need to reach production quality in 2026? The codebase already has a Makefile + CMake build system, custom test framework, and partial sanitizer support. The goal is to harden, not rebuild.

---

## Recommended Stack

### Testing Framework

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| **Catch2** | v3.13.0 | Unit and integration test runner | Amalgamated single-header option keeps the zero-external-dep constraint. BDD-style `SECTION` blocks map naturally to the existing `tests/test_*.cpp` structure. CMake `Catch2::Catch2WithMain` target drops directly into the existing `CMakeLists.txt`. Active maintenance (released Feb 2025). |
| **Custom test_framework.h** | existing | Micro-tests where Catch2 is too heavy | Keep for internal hash correctness checks that run in < 1ms. Do NOT replace — it works and has zero overhead. |

**Why not GoogleTest:** Requires building a separate library, adds link-time complexity. The project's constraint is no-external-crypto-libs during normal build. GoogleTest introduces a cmake `FetchContent` step that breaks the offline build assumption for air-gapped machines where users often run this tool. GoogleTest is better for enterprise codebases with dedicated CI infrastructure.

**Why not doctest:** Catch2 v3's amalgamated distribution gives equivalent header-simplicity with a larger community, better CMake integration, and built-in benchmark support. doctest's community is too small for a security-critical project where you need well-audited test infrastructure.

### Sanitizers

Run as separate CI jobs. Never combine TSan with ASan/UBSan — they conflict at the instrumentation level.

| Sanitizer | Flags | When to Run | What It Catches |
|-----------|-------|-------------|-----------------|
| **ASan + UBSan** | `-fsanitize=address,undefined -fno-omit-frame-pointer` | Every PR, every commit to `refactor-pipeline` | Heap/stack overflows, use-after-free, integer overflow, NULL dereference, misaligned access. Critical for BSGS bloom filter I/O and GPU memory paths. |
| **TSan** | `-fsanitize=thread` | Nightly or on PRs that touch threading code | Data races in multi-threaded key search loops, config struct mutation races (the `volatile` issue in `gpu_config_t`). |
| **MSan** | `-fsanitize=memory` (Clang only) | Weekly or when uninitialized-read bugs are suspected | Uninitialized reads in crypto output buffers. Requires full recompilation of all deps — expensive but catches what ASan misses. |
| **LeakSanitizer** | Bundled with ASan on Linux | Same run as ASan | Memory leaks in long-running search loops, BSGS bloom allocation. |

**Why not Valgrind as primary:** ASan is 10-50x faster than Valgrind (real-world numbers: 2-3x slowdown vs 100-500x). Use ASan in CI. Reserve Valgrind for targeted debugging of uninitialized reads that MSan didn't catch, or when you need to debug a binary you cannot recompile. Valgrind also does not support AVX-512 instructions on all builds — a real issue for this codebase.

**Compiler for sanitizers:** Use Clang 18+ for sanitizers. GCC does not support MSan at all. Clang's sanitizer runtime is better maintained and produces more actionable stack traces. Both GCC and Clang support ASan/UBSan/TSan.

### Static Analysis

| Tool | Version | Purpose | Configuration |
|------|---------|---------|---------------|
| **clang-tidy** | 18+ (bundled with LLVM 18) | Lint, modernization, security checks | Start with a small check subset to avoid overwhelming existing codebase. See configuration below. |
| **cppcheck** | 2.19.0 | Bug detection (NULL deref, buffer overflows, uninitialized vars) | Run with `--enable=warning,performance,portability --error-exitcode=1`. Focus on `src/secp256k1/`, `src/hash/`, `src/bloom/`. |

**Recommended clang-tidy check subset for this codebase** (phased adoption — don't enable all at once):

```
# Phase 1: Critical correctness
-checks=-*,clang-analyzer-*,clang-analyzer-security.*,bugprone-*

# Phase 2: After phase 1 is clean
-checks=-*,clang-analyzer-*,bugprone-*,performance-*,portability-simd-intrinsics

# Phase 3: Full modernization
-checks=-*,clang-analyzer-*,bugprone-*,performance-*,portability-*,modernize-use-nullptr,modernize-use-override
```

**Why not SonarCloud or Coverity:** SonarCloud is excellent but requires cloud connectivity and adds a SAAS dependency. The tool's users include people running on isolated networks. Cppcheck + clang-tidy covers 90% of what Coverity catches for free. Add SonarCloud only if the project goes open-source with GitHub hosting (it is free for public repos).

**Why not PVS-Studio:** Proprietary license, no CI-friendly free tier for this use case.

### Fuzzing

| Tool | Version | Purpose | Priority |
|------|---------|---------|----------|
| **libFuzzer** | Bundled with Clang 18+ | Primary fuzzer — in-process, fast, integrates with ASan/UBSan | High |
| **AFL++** | 4.x (latest stable) | Secondary fuzzer — better at magic-byte exploration, grammar mutations | Medium |

**Fuzz targets to write (ordered by risk):**

1. `fuzz_secp256k1_point_add` — Feed random byte pairs as X,Y coordinates into point addition. The pre-existing test_point failures suggest brittleness here.
2. `fuzz_ripemd160` — Feed variable-length byte sequences. RIPEMD160 padding bugs are a classic source of correctness failures.
3. `fuzz_sha256` — Validate the SHA256 test input-size mismatch issue that exists in `test_hash.cpp`.
4. `fuzz_bloom_deserialize` — Feed corrupted/random BSGS cache files. The bloom filter file integrity concern in CONCERNS.md is exactly the attack surface fuzzing finds.
5. `fuzz_address_decode` — Feed malformed Bitcoin address strings to parser.

**Cryptographic fuzzing caveat (HIGH confidence):** libFuzzer has limited coverage inside cryptographic hash functions because the internal state is opaque to coverage instrumentation. The harness must fuzz the *inputs and outputs*, not try to explore internal hash state. This is validated behavior — see the LLVM libFuzzer documentation and Botan's fuzzing guide.

**Build flag for libFuzzer:**
```bash
clang++ -fsanitize=fuzzer,address,undefined -O1 -fno-omit-frame-pointer \
        fuzz_target.cpp -o fuzz_target
```

**Why libFuzzer first:** In-process means 5-20x faster than AFL++ for pure compute targets like hash functions. Harnesses written for libFuzzer are compatible with AFL++ (`afl-clang-fast++` as drop-in compiler) — write once, run both.

### Benchmarking

| Tool | Version | Purpose | Why |
|------|---------|---------|-----|
| **nanobench** | v4.3.11 | Microbenchmarks for SIMD hash paths | Single-header, C++17, 80x faster to compile than Google Benchmark. `batch()` API directly reports "ops/second" for N-wide SIMD lanes — ideal for `GetHash160_AVX2()` regression tracking. No external dependencies. |
| **Google Benchmark** | v1.9.5 | Macro-benchmarks for end-to-end search throughput | JSON output enables automated regression detection in CI. Use for the high-level "keys/sec" metric that users care about. |

**Use nanobench for:** Individual hash function performance (`sha256`, `ripemd160`, AVX2 vs SSE2 vs scalar comparisons). These need sub-microsecond precision.

**Use Google Benchmark for:** Full search loop throughput (ADDRESS mode keys/sec), BSGS initialization time, bloom filter lookup latency. These run 10-60 seconds and JSON output feeds a trend chart.

**Why not just use the existing `--benchmark` mode:** The existing benchmark is for users, not for developers catching regressions. A developer benchmark must be deterministic, output machine-readable results, and fail CI if performance drops >5%. The existing mode does none of these.

### CI/CD

| Tool | Purpose | Configuration |
|------|---------|---------------|
| **GitHub Actions** | Primary CI platform | Matrix across Linux/Windows/macOS + compiler variants |
| **CMake presets** | Reproducible build configurations | Encode sanitizer/coverage/fuzz build types as named presets |

**Recommended GitHub Actions matrix:**

```yaml
strategy:
  matrix:
    include:
      # Standard builds
      - os: ubuntu-24.04, compiler: gcc-13, build_type: Release
      - os: ubuntu-24.04, compiler: clang-18, build_type: Release
      - os: macos-14, compiler: clang, build_type: Release
      - os: windows-2022, compiler: msvc, build_type: Release
      # Sanitizer builds (Linux Clang only — MSan requires full recompile)
      - os: ubuntu-24.04, compiler: clang-18, build_type: ASan
      - os: ubuntu-24.04, compiler: clang-18, build_type: TSan
      - os: ubuntu-24.04, compiler: clang-18, build_type: UBSan
```

**Why separate sanitizer jobs:** ASan + TSan cannot be combined. TSan is 5-15x slower — isolating it prevents it from blocking the fast feedback loop. UBSan can run with ASan but keeping it separate isolates failures.

**Workflow triggers:**
- Every push to `refactor-pipeline`: run standard matrix + ASan/UBSan
- PR to `main`: run full matrix including TSan
- Nightly: run MSan, fuzzing (30-minute corpus runs), benchmark regression check

### Compiler Hardening Flags (Production Build)

Based on the OpenSSF Compiler Hardening Guide (HIGH confidence — official source):

```makefile
# Add to Makefile HARDENED_FLAGS or CMake release profile
HARDEN_FLAGS := \
  -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 \
  -D_GLIBCXX_ASSERTIONS \
  -fstack-protector-strong \
  -fstack-clash-protection \
  -fcf-protection=full \
  -Wformat=2 -Werror=format-security \
  -fno-delete-null-pointer-checks \
  -fno-strict-overflow \
  -ftrivial-auto-var-init=zero

HARDEN_LDFLAGS := \
  -Wl,-z,relro -Wl,-z,now \
  -Wl,-z,noexecstack \
  -Wl,--as-needed
```

**Important constraint:** `-D_FORTIFY_SOURCE=3` requires `-O1` or higher. The project already uses `-O2`, so this is safe. Do NOT use `-D_FORTIFY_SOURCE=2` — version 3 adds checking for dynamic sizes (glibc 2.35+, which Ubuntu 22.04+ has).

**Do not add `-Wconversion` globally:** The SIMD intrinsic code makes extensive use of implicit integer conversions between `__m256i`, `uint32_t`, and `int`. `-Wconversion` will produce hundreds of false positives in `src/hash/*_avx2.cpp`. Apply it only to non-SIMD source files if desired.

### Code Coverage

| Tool | Version | Purpose | When to Run |
|------|---------|---------|-------------|
| **gcovr** | 7.x | HTML + Cobertura coverage reports | PR coverage gate, nightly report |
| **lcov/genhtml** | 2.x | Alternative HTML report | Local developer use |

**Coverage build flags:**
```bash
-fprofile-arcs -ftest-coverage -O0 -g
```

**Target coverage thresholds (realistic for this codebase):**
- `src/secp256k1/` — 90%+ line coverage (cryptographic core, must be tested)
- `src/hash/` — 85%+ line coverage (hash correctness is non-negotiable)
- `src/bloom/` — 80%+ line coverage
- `src/gpu/` — Exclude from coverage (GPU kernels cannot be measured by gcov)
- `src/distributed/` — 60%+ (complex stateful code, accept lower initially)

**Why gcovr over codecov.io:** gcovr runs locally, produces HTML reports, and has a GitHub Actions action (`threeal/gcovr-action`). codecov.io adds a cloud dependency and requires uploading coverage data to an external service — inappropriate for a tool used in security research contexts.

---

## Installation

```bash
# Ubuntu 24.04 — install all hardening tools
sudo apt-get update
sudo apt-get install -y \
  clang-18 clang-tidy-18 llvm-18 \
  gcc-13 g++-13 \
  cppcheck \
  valgrind \
  gcovr lcov \
  afl++

# Catch2 v3 — vendored amalgamation (preserves zero-external-dep build)
# Download catch_amalgamated.hpp and catch_amalgamated.cpp from:
# https://github.com/catchorg/Catch2/releases/tag/v3.13.0
# Place in tests/catch2/

# nanobench v4.3.11 — single header
# Download nanobench.h from:
# https://github.com/martinus/nanobench/releases/tag/v4.3.11
# Place in tests/nanobench/

# Google Benchmark v1.9.5 — via CMake FetchContent (benchmark builds only)
# Already declared optional in STACK.md — use only in benchmark/ subdir
```

---

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| Catch2 v3 | GoogleTest | If the project gains a dedicated CI team and wants IDE integration (CLion/VS) with mock framework (gMock). GoogleTest is the right call for teams > 5 engineers. |
| nanobench | Criterion (C) | If the codebase were pure C. Criterion is excellent for C but has no C++ API. |
| libFuzzer + AFL++ | Honggfuzz | Honggfuzz has excellent multi-process fuzzing and hardware coverage. Consider it if the project gets a dedicated fuzzing infrastructure. |
| clang-tidy + cppcheck | PVS-Studio | PVS-Studio finds more real bugs than cppcheck on average, but requires a commercial license. Use it if the project moves to a commercial model. |
| gcovr | Codecov.io | If the project is public on GitHub, Codecov.io is free, provides PR comments with diff coverage, and is worth adding on top of gcovr. |
| GitHub Actions | Jenkins | If users need to run CI on-premises with air-gapped machines. Jenkins is more complex but fully self-hosted. |

---

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| **Boost.Test** | Adds Boost as a dependency. The project explicitly avoids external dependencies in its main build. Boost.Test provides no advantage over Catch2 for this use case. | Catch2 v3 amalgamated |
| **-Ofast** | Already documented in CLAUDE.md — causes Ubuntu system freezes due to `-ffast-math` breaking floating point semantics. Confirmed in the codebase history. | `-O2` with explicit vectorization flags |
| **MSan in CI on every commit** | MSan requires recompiling every dependency with MSan instrumentation, including libc. On this codebase (custom secp256k1, custom hashes, no stdlib crypto), it is feasible but expensive. False positives from uninstrumented deps will block CI. | Run MSan weekly on a dedicated schedule. |
| **-fsanitize=address with CUDA code** | ASan and CUDA runtime are incompatible. `cudaMalloc` and `cuMemcpy` will produce false positives. | Compile GPU backend with `KEYHUNT_NO_GPU=1` for sanitizer runs. |
| **Thread sanitizer + AVX-512** | TSan's memory model does not correctly track SIMD vector register accesses in some GCC/Clang versions. Can produce false positives with `__m512i` loads/stores. | Use TSan with AVX2 builds only (`-mno-avx512f` for TSan runs). |
| **Coverity (cloud)** | Requires uploading source code to Coverity's servers. Unacceptable for a security tool whose users may have confidentiality requirements. | cppcheck + clang-tidy (local, open source) |
| **gcov on GPU kernels** | gcov instruments CPU code only. OpenCL kernels (.cl files) and CUDA kernels (.cu files) are not instrumented. Coverage reports for GPU code are meaningless. | AMD ROCm provides rocprofiler for GPU kernel coverage; NVIDIA provides Nsight. These are separate tools for dedicated GPU testing phases. |

---

## Stack Patterns by Build Type

**Standard development build:**
- GCC 13 or Clang 18, `-O2 -g`, no sanitizers
- `make` or `cmake --build . --config Debug`

**CI sanitizer build (ASan + UBSan):**
- Clang 18 required
- `-fsanitize=address,undefined -fno-omit-frame-pointer -O1`
- Exclude GPU backends: `CFLAGS+="-DKEYHUNT_NO_GPU=1"` or CMake `-DENABLE_GPU=OFF`
- Run all tests, expect < 3x slowdown

**CI sanitizer build (TSan):**
- Clang 18 required
- `-fsanitize=thread -O2`
- Exclude GPU backends
- Run multi-threaded search tests only (single-threaded tests waste TSan budget)
- Disable AVX-512: `-mno-avx512f`

**Fuzzing build:**
- Clang 18 required (libFuzzer is Clang-only)
- `-fsanitize=fuzzer,address,undefined -O1 -g`
- Separate `fuzz/` directory with harness files
- Each harness is its own binary (linked against the core lib)

**Coverage build:**
- GCC 13 (gcov integrates tightly with GCC)
- `-fprofile-arcs -ftest-coverage -O0 -g`
- Run `gcovr --html-details -o coverage/`

**Hardened production build:**
- GCC 13 or Clang 18
- `-O2` + all HARDEN_FLAGS listed above
- Strip debug symbols for release: `strip keyhunt`

---

## Version Compatibility

| Package | Compatible With | Notes |
|---------|-----------------|-------|
| Catch2 v3.13.0 | C++14, C++17, C++20 | Requires CMake 3.16+ for `Catch2::Catch2WithMain` target. Compatible with the project's CMake 3.18+ requirement. |
| nanobench v4.3.11 | C++11/14/17/20 | Single header, no CMake required. Works with both Makefile and CMake builds. |
| Google Benchmark v1.9.5 | C++14+ | CMake `FetchContent` recommended. Requires `cmake .. -DBENCHMARK_ENABLE_TESTING=OFF` to avoid GoogleTest dependency. |
| ASan (Clang 18) | Linux x86_64, macOS | Not available on Windows MinGW. MSVC has its own `/fsanitize:address` which is less capable. |
| TSan (Clang 18) | Linux x86_64 only | Not supported on macOS for x86_64 (Apple Silicon only). Not available on Windows. |
| MSan (Clang 18) | Linux x86_64 only | Not available on macOS or Windows at all. |
| cppcheck 2.19.0 | C++17, C11 | Works on Linux, Windows, macOS. Supports `compile_commands.json` from CMake. |
| clang-tidy 18 | C++17 | Requires `compile_commands.json`: `cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. |
| gcovr 7.x | GCC 13+ | Clang coverage via `-fprofile-instr-generate` requires `llvm-profdata`/`llvm-cov` instead — a different toolchain. Standardize on GCC for coverage builds. |
| AFL++ 4.x | Linux x86_64, macOS | Windows support is experimental. Run fuzzing CI on Linux only. |

---

## Sources

- [Catch2 Releases — GitHub](https://github.com/catchorg/catch2/releases) — v3.13.0 confirmed February 2025, HIGH confidence
- [nanobench Releases — GitHub](https://github.com/martinus/nanobench/releases) — v4.3.11 confirmed, HIGH confidence
- [Google Benchmark Releases — GitHub](https://github.com/google/benchmark/releases) — v1.9.5 confirmed, HIGH confidence
- [cppcheck Releases — GitHub](https://github.com/danmar/cppcheck/releases) — v2.19.0 December 2024, HIGH confidence
- [OpenSSF Compiler Hardening Guide](https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html) — official, HIGH confidence
- [LLVM libFuzzer Documentation](https://llvm.org/docs/LibFuzzer.html) — official, HIGH confidence
- [AFL++ Fuzzing in Depth](https://aflplus.plus/docs/fuzzing_in_depth/) — official, HIGH confidence
- [Red Hat: Memory error checking in C/C++](https://developers.redhat.com/blog/2021/05/05/memory-error-checking-in-c-and-c-comparing-sanitizers-and-valgrind) — ASan vs Valgrind comparison, MEDIUM confidence (2021 article, still accurate for tool selection rationale)
- [Conan Sanitizer CI Workflow](https://blog.conan.io/sanitizers/toolchain/tools/conan/2025/11/25/How-to-use-sanitizers-in-your-conan-workflow.html) — sanitizer CI integration patterns, MEDIUM confidence
- [GitHub Actions cmake-multi-platform starter workflow](https://github.com/actions/starter-workflows/blob/main/ci/cmake-multi-platform.yml) — official GitHub, HIGH confidence
- [Clang-Tidy checks list](https://clang.llvm.org/extra/clang-tidy/checks/list.html) — official LLVM, HIGH confidence
- [gcovr-action GitHub Marketplace](https://github.com/marketplace/actions/gcovr-action) — CI integration pattern, HIGH confidence
- [Botan Fuzzing Guide](https://botan.randombit.net/handbook/dev_ref/fuzzing.html) — cryptographic fuzzing caveats (checksum bypass), HIGH confidence

---
*Stack research for: Production hardening of C/C++ cryptographic key search tool (keyhunt)*
*Researched: 2026-02-28*
