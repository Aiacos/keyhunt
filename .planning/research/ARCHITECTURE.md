# Architecture Research

**Domain:** C/C++ production hardening of a high-performance cryptographic search tool
**Researched:** 2026-02-28
**Confidence:** HIGH (domain-specific patterns, official toolchain docs, verified against OpenSSF/LLVM/CMake sources)

## Standard Architecture for C/C++ Production Hardening

### System Overview

Production hardening of a C/C++ codebase is not a single action — it is a layered sequence where each layer creates the preconditions for the next. The dependency chain is strict: you cannot trust sanitizer output if tests are broken; you cannot safely migrate globals if the monolith is not yet decomposed; you cannot meaningfully run CI if sanitizers are not wired in.

```
┌─────────────────────────────────────────────────────────────┐
│              Layer 5: CI Pipeline (Green on All Platforms)  │
│  Linux x86_64  │  Windows x64  │  macOS  │  Sanitizer Jobs │
├─────────────────────────────────────────────────────────────┤
│              Layer 4: Monolith Decomposition                │
│   Extract mode dispatchers  │  Wire config into modules    │
├─────────────────────────────────────────────────────────────┤
│              Layer 3: Config Migration                      │
│  Eliminate extern globals  │  Dependency injection pattern  │
├─────────────────────────────────────────────────────────────┤
│              Layer 2: Sanitizer Coverage                    │
│    ASan  │  UBSan  │  TSan (separate build)  │  Valgrind   │
├─────────────────────────────────────────────────────────────┤
│              Layer 1: Test Foundation (MUST BE GREEN FIRST) │
│  Fix existing failures  │  Add mode coverage  │  Crypto tests│
└─────────────────────────────────────────────────────────────┘
```

### Component Boundaries (What Can Be Hardened Independently)

These components have clean external interfaces and can be hardened in isolation without touching keyhunt.cpp:

| Component | Boundary | Can Harden Independently? | Dependency Direction |
|-----------|----------|--------------------------|---------------------|
| `src/secp256k1/` | Public: `SECP256k1.h`, `Int.h`, `Point.h` | YES — self-contained, no extern globals | Used by search modules |
| `src/hash/` | Public: `sha256.h`, `ripemd160.h` | YES — pure functions, no state | Used by secp256k1, search |
| `src/bloom/` | Public: `bloom.h`, `bloom_wrapper.h` | YES — no external deps | Used by search, bsgs |
| `src/platform/` | Public: `platform.h` | YES — zero external deps | Used by all threaded code |
| `src/base58/`, `src/bech32/` | Public: C API headers | YES — pure encoding | Used by io, crypto |
| `src/config/` | Public: `config.h` | YES — struct definitions only | Used by everything |
| `src/core/sysinfo.c` | Public: `sysinfo.h` | YES — reads /proc, no globals | Used by config init |
| `src/core/parameter_validator.c` | Public: `parameter_validator.h` | YES — takes config ptr | Used by main |
| `src/gpu/` | Public: `gpu_backend.h` | PARTIAL — CUDA/OpenCL optional | Used by main, hybrid |
| `src/search/` | Implicit: `search_context.h` externs | NO — tangled via extern globals | Depends on keyhunt.cpp globals |
| `src/keyhunt.cpp` | Entry point only | NO — 5300-line monolith | Owns all global state |
| `src/distributed/` | `distributed.h` | PARTIAL — large, self-contained | Depends on platform, config |

### Dependency Chain Direction (Strict Build Order)

The hardening dependency chain is a DAG with a single critical path. Violating the order creates rework.

```
Test Foundation
      |
      v
Sanitizer Coverage  <-- requires green tests to interpret output
      |
      v
Config Migration    <-- requires sanitizers to catch use-after-free during migration
      |
      v
Monolith Decomposition  <-- requires config injection to be complete first
      |
      v
CI Pipeline         <-- requires all above to not regress on Linux/Windows/macOS
```

**Why this exact order:**

1. **Tests first**: Broken tests produce noise that obscures real sanitizer findings. Fix the 8 test_point failures and 11 test_intgroup failures before adding ASan — otherwise every run is ambiguous. (Source: OpenSSF hardening guide: "establish a green baseline before adding checks")

2. **Sanitizers before config migration**: Moving 50+ globals into a config struct creates exactly the memory access patterns that ASan catches — use-after-free when a module holds a raw pointer to a config struct that gets reused or freed. Run sanitizers first to establish a clean baseline, then migration violations show up clearly.

3. **Config migration before monolith decomposition**: You cannot cleanly extract `thread_process_bsgs()` from keyhunt.cpp if it still reaches out to extern globals. The extraction point must be the config struct boundary. Migration must be complete or the extracted file just re-imports the same externs.

4. **All the above before CI**: CI is a gate, not a foundation. Wiring CI before the above are stable means continuous false positive management.

## Recommended Hardening Structure for keyhunt

### Phase 1: Test Foundation

**Goal:** Every test that should pass does pass. No pre-existing failures obscure new work.

**Components to harden:**
- `tests/test_point.cpp` — fix 8 failures (secp256k1 arithmetic edge cases)
- `tests/test_intgroup.cpp` — fix 11 failures (batch modular inversion)
- `tests/test_hash.cpp` — fix SHA256 input size mismatches (uint32_t[8] vs uint32_t[16])
- Add crypto coverage: bloom filter false positive rate, RIPEMD160 known vectors
- Add search mode integration tests (end-to-end ADDRESS, XPOINT, RMD160 correctness)

**Independent of everything else.** Fix tests against current code — no refactoring yet.

**Why this boundary is clean:** `src/secp256k1/` and `src/hash/` have no extern globals. Tests for these components are purely unit tests that link against isolated translation units.

### Phase 2: Sanitizer Coverage

**Goal:** All tests pass clean under ASan + UBSan. ThreadSanitizer as a separate build.

**Standard setup for this codebase:**

```cmake
# CMakePresets.json or cmake -DCMAKE_BUILD_TYPE=Asan
set(ASAN_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1")
# Separate TSan build (cannot mix with ASan)
set(TSAN_FLAGS "-fsanitize=thread -fno-omit-frame-pointer -g -O1")
```

**Three separate sanitizer jobs** (not combinable):
- Job 1: ASan + UBSan + LeakSanitizer (finds memory corruption, UB, leaks)
- Job 2: ThreadSanitizer (finds data races — critical for multithreaded search loops)
- Job 3: Valgrind memcheck (slower, catches what ASan misses in edge cases)

**Conflict with keyhunt.cpp:** `_FORTIFY_SOURCE` must be explicitly disabled for sanitizer builds. The existing `-D_FORTIFY_SOURCE=2` in Makefile must be removed from ASAN_BUILD target.

**GPU backends:** Skip CUDA/OpenCL in sanitizer builds — GPU runtime and sanitizer runtime have ABI conflicts. Use `#ifdef HAVE_CUDA_BACKEND` guards. This is correct and expected behavior, not a limitation.

**Components most likely to surface issues:**
- `src/secp256k1/Int.cpp` — 256-bit integer with manual memory layout (UBSan/alignment)
- `src/hash/ripemd160_avx2.cpp` — SIMD with `__m256i` casts (strict aliasing UB)
- `src/bloom/bloom.cpp` — large allocations via `mmap`/`calloc` (ASan)
- `src/search/search_bsgs_threads.cpp` — multithreaded access to shared bloom filters (TSan)
- `src/distributed/distributed.c` — 169KB network code (all of the above)

**SIMD + UBSan interaction:** AVX2 code commonly triggers `-fsanitize=alignment` because `__m256i*` casts from `char*` are technically UB under strict C++ aliasing rules. The correct fix is `__attribute__((may_alias))` or `memcpy`-based loads, not disabling the sanitizer. Flag each real UBSan finding individually rather than blanket-suppressing.

### Phase 3: Config Migration

**Goal:** Eliminate all `extern` declarations in `search_context.h`. All search modules receive `keyhunt_config_t*` as a parameter, not via extern globals.

**The pattern:** This is standard C dependency injection for multi-TU C codebases.

```c
/* BEFORE: search_address.cpp reaches out to extern globals */
extern int NTHREADS;
extern bloom_t *bloom_bP;
void thread_process(void *arg) { ... uses NTHREADS, bloom_bP ... }

/* AFTER: receives config pointer via thread arg */
typedef struct { keyhunt_config_t *config; int thread_id; } thread_args_t;
void thread_process(void *arg) {
    thread_args_t *args = (thread_args_t*)arg;
    int nthreads = args->config->runtime.thread_count;
    bloom_t *bloom = args->config->runtime.bloom_filter;
    ...
}
```

**Migration order within Phase 3** (minimizes risk):

1. `src/search/search_address.cpp` — most-used path, highest value
2. `src/search/search_xpoint.cpp`, `search_rmd160.cpp` — similar pattern to address
3. `src/search/search_vanity.cpp`, `search_minikeys.cpp` — smaller, simpler
4. `src/search/search_bsgs.cpp`, `search_bsgs_threads.cpp` — largest, most complex (last)

**Each module migrates atomically:** add parameter, update all call sites in keyhunt.cpp, run tests, run sanitizers, commit. Never have half-migrated modules at end-of-day.

**Risk:** `keyhunt_config_t` must be stable before migration begins. Changing the struct shape mid-migration breaks all in-progress files. Freeze the struct schema at Phase 3 start.

### Phase 4: Monolith Decomposition

**Goal:** `keyhunt.cpp` drops from ~5300 lines to a thin orchestrator (~500-800 lines). Each mode dispatcher lives in `src/search/`.

**Strangler Fig pattern** applied to a monolith (Fowler/InfoQ verified pattern): extract one mode at a time, keep the monolith callable through the same interface, delete dead code after extraction is verified.

**Extraction order** (least coupled first):

1. **MINIKEYS mode** — `thread_process_minikeys()` has fewest dependencies
2. **VANITY mode** — already partially in `search_vanity.cpp`
3. **XPOINT + RMD160** — share infrastructure with ADDRESS
4. **ADDRESS mode** — largest user-facing mode, extract last from CPU path
5. **BSGS mode** — most complex, most internal state; extract as final phase

**What stays in keyhunt.cpp after decomposition:**
- `main()` — argument parsing, config init
- Thread lifecycle management (create, join, signal handling)
- High-level mode dispatch (`switch(config->search.mode)`)
- GPU backend init/teardown
- Progress display and statistics

**Boundary rule:** A file in `src/search/` may only `#include` from `src/config/`, `src/secp256k1/`, `src/hash/`, `src/bloom/`, `src/platform/`, `src/core/`. It must never `#include "keyhunt.cpp"` or depend on a symbol defined in `main()`.

### Phase 5: CI Pipeline

**Goal:** GitHub Actions workflow green on Linux x86_64, Windows x64, macOS. Sanitizer jobs run on Linux only (CUDA-free).

**Matrix strategy (GitHub Actions):**

```yaml
strategy:
  matrix:
    include:
      - os: ubuntu-22.04, compiler: gcc-12, build: Release
      - os: ubuntu-22.04, compiler: clang-16, build: Asan
      - os: ubuntu-22.04, compiler: clang-16, build: Tsan
      - os: windows-2022,  compiler: msvc,   build: Release
      - os: macos-14,      compiler: clang,  build: Release
```

**Job isolation rules:**
- Sanitizer jobs: Linux only, no CUDA/OpenCL headers, `KEYHUNT_SKIP_SYSINFO=1` to avoid /proc parsing failures in containers
- Release jobs: All platforms, GPU backends gated by header availability
- Each job must build from scratch (`make clean` or separate build dir)

**CI gate: fail on any sanitizer report.** Use `ASAN_OPTIONS=halt_on_error=1:detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Do not suppress reports — fix them.

## Architectural Patterns to Follow

### Pattern 1: Config Struct Dependency Injection

**What:** Pass `keyhunt_config_t*` down the call stack instead of reading extern globals.
**When to use:** Every function that currently reads a global variable.
**Trade-offs:** Adds one parameter to thread function signatures; enables unit testing of any function in isolation.

```c
/* Thread argument bundle — standard C pattern for multithreaded config passing */
typedef struct {
    keyhunt_config_t *config;   /* shared, read-only after init */
    int              thread_id; /* per-thread index */
    volatile int    *stop_flag; /* shared, written by signal handler */
} search_thread_args_t;

void thread_process_address(void *arg) {
    search_thread_args_t *a = (search_thread_args_t*)arg;
    /* a->config->search.mode, a->config->runtime.thread_count, etc. */
}
```

### Pattern 2: Sanitizer Build Types (Separate CMake Presets)

**What:** Define `Asan`, `Tsan`, `Ubsan` as distinct CMake build types alongside `Debug`/`Release`.
**When to use:** CI and developer local verification before commit.
**Trade-offs:** Longer CI time (3 extra build jobs); catches bugs that make it to production otherwise.

```cmake
# CMakePresets.json excerpt
{
  "name": "asan",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE": "Debug",
    "CMAKE_C_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer",
    "CMAKE_CXX_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer",
    "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=address,undefined"
  }
}
```

### Pattern 3: Incremental Extraction with Compile-Time Verification

**What:** When extracting a function from `keyhunt.cpp` to `src/search/`, use a forward declaration stub in keyhunt.cpp that calls the extracted function. The linker verifies correctness.
**When to use:** During monolith decomposition in Phase 4.
**Trade-offs:** Creates a two-step extraction; eliminates "it compiles but is wrong" class of errors.

```cpp
/* keyhunt.cpp — stub after extraction */
/* thread_process_minikeys() now lives in src/search/search_minikeys.cpp */
/* keyhunt.cpp just declares and calls it */
extern void thread_process_minikeys(void *arg);  /* linker verifies */
```

### Pattern 4: Test-After-Fix Before Test-Expand

**What:** For each failing test, fix the root cause in the production code, verify the specific test passes, then expand coverage. Never expand a broken test suite.
**When to use:** Phase 1, sequentially per test file.
**Trade-offs:** Slower apparent progress; guarantees no false green coverage inflation.

## Anti-Patterns to Avoid

### Anti-Pattern 1: Sanitizer + Global Suppression

**What people do:** Run ASan, see 200 reports, add `__attribute__((no_sanitize("address")))` or `LSAN_OPTIONS=suppressions=...` to silence them.
**Why it's wrong:** Suppressions hide real bugs. The 200th report may be the actual memory corruption causing incorrect search results.
**Do this instead:** Fix Phase 1 (green tests) first. Run sanitizers on the clean test suite. Each report is a real bug — fix it in the production code. If a SIMD intrinsic genuinely requires `may_alias`, document it explicitly with a comment.

### Anti-Pattern 2: Config Migration With Struct Shape Changes Mid-Flight

**What people do:** Start migrating `search_address.cpp`, then realize `keyhunt_config_t` is missing a field, add it, now `search_bsgs.cpp` (partially migrated) fails to compile.
**Why it's wrong:** Partial migrations in a broken state block all other work. The monorepo has no way to "merge around" a compilation failure.
**Do this instead:** Freeze `keyhunt_config_t` schema at Phase 3 start. Any missing field is added to the struct and to `config.cpp` initialization in a single preparatory commit before any module migration begins.

### Anti-Pattern 3: Decomposing Before Config Migration

**What people do:** Extract `thread_process_bsgs()` from `keyhunt.cpp` into `search_bsgs.cpp` while it still uses extern globals.
**Why it's wrong:** You've moved the problem, not solved it. The extracted file now has the same extern dependency graph — you've done the physical move work twice.
**Do this instead:** Complete Phase 3 config migration for a module first, then extract it in Phase 4. Migration + extraction is a single logical unit per module.

### Anti-Pattern 4: Mixing TSan With Other Sanitizers

**What people do:** Enable `-fsanitize=address,thread` in a single build to "do everything at once."
**Why it's wrong:** TSan and ASan have incompatible shadow memory layouts. The resulting binary produces false positives and often segfaults in the sanitizer runtime before the actual bug.
**Do this instead:** Always run TSan in a completely separate build. Three separate CI jobs: `asan+ubsan`, `tsan`, `valgrind`.

### Anti-Pattern 5: CI Added Before Tests Are Green

**What people do:** Wire GitHub Actions matrix before fixing test_point and test_intgroup failures, then spend time managing "known failures" exceptions in CI.
**Why it's wrong:** CI with known-failing tests normalizes red. Teams stop treating red as a signal. New failures get buried.
**Do this instead:** Tests must be green on the developer machine before CI is added. CI's first commit must be a fully green run.

## Data Flow During Hardening Phases

### Phase Transition Flow

```
Phase 1: Fix Tests
   test_point.cpp       -> green
   test_intgroup.cpp    -> green
   test_hash.cpp        -> green
   test_address_mode    -> green (new end-to-end)
         |
         v
Phase 2: Sanitizer Coverage
   Build: cmake -preset asan
   Run:   ctest (all tests clean under ASan/UBSan)
   Build: cmake -preset tsan
   Run:   ctest (search_bsgs_threads clean under TSan)
         |
         v
Phase 3: Config Migration
   search_context.h externs -> 0 remaining
   Each search/*.cpp receives keyhunt_config_t* via thread arg
   No extern globals remain in search/ directory
         |
         v
Phase 4: Monolith Decomposition
   keyhunt.cpp: ~5300 lines -> ~600 lines (orchestrator only)
   src/search/: each mode self-contained
   Boundary: search/ may not include keyhunt.cpp symbols
         |
         v
Phase 5: CI Pipeline
   GitHub Actions: Linux/Windows/macOS matrix
   Sanitizer jobs: Linux only (no GPU)
   All jobs: green before merge to main
```

### State Management During Migration

**Critical invariant:** At any commit boundary, `make` must produce a working binary. The hardening work is always shippable-intermediate — never in a state where the tool produces wrong answers.

This means:
- Phase 2 (sanitizers): Fix bugs found in sanitizer builds before committing sanitizer CI job. Never add a CI job that is knowingly red.
- Phase 3 (config migration): Migrate one module at a time. After each module, run tests and the binary produces correct output on `tests/1to32.txt`.
- Phase 4 (decomposition): Extract one thread function at a time. After each extraction, confirm the binary still searches correctly.

## Integration Points

### Sanitizer Integration Boundaries

| Build Path | Sanitizers | GPU | Notes |
|------------|-----------|-----|-------|
| Debug (Makefile) | None | Optional | Normal development |
| Asan preset | ASan + UBSan + LSan | Disabled | `-DHAVE_CUDA_BACKEND=0` |
| Tsan preset | TSan | Disabled | Search thread race detection |
| Release (Makefile) | None | Optional | Production use |
| CI Linux job | All sanitizer presets | Disabled | Per-sanitizer jobs |
| CI Windows/macOS | None (Release only) | Disabled | Compilation correctness |

### Module Extraction Boundaries

When a module is extracted from keyhunt.cpp into src/search/, its boundary contract is:

| Boundary Direction | Allowed | Forbidden |
|-------------------|---------|-----------|
| search/ -> config/ | YES | - |
| search/ -> secp256k1/ | YES | - |
| search/ -> hash/ | YES | - |
| search/ -> bloom/ | YES | - |
| search/ -> platform/ | YES | - |
| search/ -> core/ | YES | - |
| search/ -> keyhunt.cpp symbols | NO | extern from main TU |
| search/ -> search/ (cross-mode) | NO | Each mode is independent |

## Scaling Considerations for Hardening Work

| Phase | Effort Estimate | Risk if Skipped |
|-------|----------------|----------------|
| Test Foundation | Medium — 8+11+SHA256 fixes are bounded | High: no confidence in correctness |
| Sanitizer Coverage | Medium — mostly finding + fixing bugs | High: silent memory corruption in prod |
| Config Migration | High — ~94 externs across 6 files | Medium: blocks clean decomposition |
| Monolith Decomposition | High — keyhunt.cpp is 5300 lines | Medium: maintainability debt accumulates |
| CI Pipeline | Low — matrix YAML + scripts | Medium: regressions reach main undetected |

**Optimal sequencing reduces total effort.** Doing Phase 2 before Phase 1 is actively harmful (doubles sanitizer noise). Doing Phase 4 before Phase 3 means extraction is done twice.

## Sources

- [OpenSSF Compiler Hardening Guide for C/C++](https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html) — staged enabling approach, FORTIFY_SOURCE conflict with sanitizers (HIGH confidence)
- [GitHub Starter Workflows: CMake multi-platform](https://github.com/actions/starter-workflows/blob/main/ci/cmake-multi-platform.yml) — matrix strategy for Linux/Windows/macOS (HIGH confidence)
- [Sanitizers in continuous integration — CodeInE](https://ngathanasiou.wordpress.com/2022/07/04/sanitizers-in-continuous-integration/) — sanitizer isolation requirement, halt_on_error settings (MEDIUM confidence)
- [LLVM Safety at Scale — AsiaLLVM 2025](https://llvm.org/devmtg/2025-06/slides/technical-talk/yasuda-safety.pdf) — green baseline requirement, large-scale cleanup process (HIGH confidence)
- [Martin Fowler: Break Monolith into Microservices](https://martinfowler.com/articles/break-monolith-into-microservices.html) — Strangler Fig pattern for incremental extraction (HIGH confidence)
- [Refactoring 024: Replace Global Variables with Dependency Injection](https://dev.to/mcsee/refactoring-024-replace-global-variables-with-dependency-injection-2h51) — config struct injection pattern (MEDIUM confidence)
- [sanitizers-cmake](https://github.com/arsenm/sanitizers-cmake) — CMake separate build type approach (MEDIUM confidence)
- [JetBrains CLion: Refactoring in C++ 2024](https://blog.jetbrains.com/clion/2024/12/refactoring-in-cpp/) — incremental refactoring best practices (MEDIUM confidence)
- [OWASP C-Based Toolchain Hardening](https://cheatsheetseries.owasp.org/cheatsheets/C-Based_Toolchain_Hardening_Cheat_Sheet.html) — compiler flag recommendations (HIGH confidence)

---
*Architecture research for: keyhunt C/C++ production hardening — component boundaries, dependency chains, build order*
*Researched: 2026-02-28*
