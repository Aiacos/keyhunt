# Pitfalls Research

**Domain:** C/C++ cryptographic tool production hardening (SIMD, GPU, global-to-config migration, monolith decomposition, cross-platform CI)
**Researched:** 2026-02-28
**Confidence:** HIGH (codebase-verified for code-specific pitfalls, MEDIUM for general process pitfalls cross-referenced with multiple web sources)

---

## Critical Pitfalls

### Pitfall 1: Duplicate CPU Feature Detection Variables with Different Values

**What goes wrong:**
`bsgs_fast.cpp` defines a `static bool g_avx2_available` that is populated by its own `detect_cpu_features()` via `__get_cpuid_count`. `keyhunt.cpp` defines a non-static `bool g_avx2_available` populated by `ripemd160_avx2_available()`. `search_address.cpp` and `search_vanity.cpp` then `extern` the keyhunt.cpp version. The BSGS code uses its local static, the address/vanity code use the global from keyhunt.cpp. These two variables can diverge — for example, if `bsgs_fast.cpp` is initialized before main's detection runs, or if the detection methods return different results on a given CPU.

**Why it happens:**
The codebase was grown incrementally. The original monolith had one global; as modules were extracted they each grew their own copies rather than sharing through a central config.

**How to avoid:**
Detect CPU features exactly once, in `sysinfo.c::detect_cpu_features()`, store results in `keyhunt_config_t::autotune`, and pass config everywhere. Delete all module-local `static bool g_avx2_available` variables. Remove `extern bool g_avx2_available` from all search files. Enforce with a linker check or `static_assert` that no translation unit defines a local copy.

**Warning signs:**
- Address mode uses AVX2 path but BSGS falls back to scalar (or vice versa) on the same binary run.
- `grep -rn "g_avx2_available" src/` shows definitions in more than one `.cpp` file.
- Throughput drops silently after config migration work touches one module but not another.

**Phase to address:**
Config migration phase (eliminating extern globals and wiring `keyhunt_config_t` into all search modules). This is a prerequisite for monolith decomposition — do not split `keyhunt.cpp` before this is resolved or each extracted module inherits its own stale copy.

---

### Pitfall 2: Pre-Existing Test Failures Mask New Regressions

**What goes wrong:**
The test suite has 19 known pre-existing failures (8 in `test_point.cpp`, 11 in `test_intgroup.cpp`) and size-mismatch TODOs in `test_hash.cpp`. If hardening work adds a new regression in point arithmetic or hash code, the CI output stays red and no one notices the new failure among the existing noise. The refactor gets blamed on the pre-existing count, not on the new bug.

**Why it happens:**
Refactors proceed assuming "these tests have always failed, so we can ignore them." The distinction between "was failing before my change" and "is now failing because of my change" gets lost when the baseline is not documented.

**How to avoid:**
Before any other hardening work, fix or explicitly skip every pre-existing failure with a documented reason. Create a `KNOWN_FAILURES.md` or use test framework skip macros so each failure has an owner and a resolution ticket. CI must start from a green baseline. Any new red failure is then immediately attributable. Separate investigation of `test_point` failures from code changes — understand whether they're testing deprecated behavior or actual bugs before touching the test.

**Warning signs:**
- CI shows non-zero failures that are not counted against the merge gate.
- Developers say "those tests have never passed" as justification for ignoring CI output.
- A change to `Int.cpp` or `IntGroup.cpp` that introduces a real regression goes unreviewed because the failure count didn't increase.

**Phase to address:**
Must be Phase 1 (test baseline establishment) before any other phase proceeds. No other phase should begin while test_point and test_intgroup are failing.

---

### Pitfall 3: Sanitizers Produce False Positives on SIMD and Aligned Memory

**What goes wrong:**
ASan inserts shadow memory checks that assume 8-byte-aligned allocations and sequential access patterns. AVX-512 code requires 64-byte-aligned buffers; AVX2 requires 32-byte. When ASan instruments stack variables that have `__attribute__((aligned(64)))`, the shadow bytes may not be correctly poisoned/unpoisoned at the required alignment boundary, causing legitimate SIMD loads to trip ASan. The build system's sanitizer target (`make sanitize`) disables GPU backends but still compiles all AVX2 hash files; the first `ripemd160avx2_32()` call may report a stack-buffer-overflow on a legally aligned local array.

Additionally, MemorySanitizer (MSan) will always produce false positives on SIMD widening operations that read a 32-byte register but only fully initialize part of it — this is legal because the unread lanes are discarded, but MSan sees an uninitialized read.

**Why it happens:**
ASan's shadow memory granularity is 8 bytes; it cannot express 32- or 64-byte "partial poisoning" for wider SIMD. GCC 13 on Ubuntu has a known variant of this where 64-byte-aligned data members trigger ASan warnings on valid code (Launchpad bug #2023424).

**How to avoid:**
- Run sanitizer builds with `ASAN_OPTIONS=detect_stack_use_after_return=0` for the SIMD code paths while keeping it enabled for everything else.
- Use `__attribute__((no_sanitize("address")))` only on the specific SIMD inner loops that use aligned stack buffers, not on entire files.
- Do not add MSan to the CI matrix — the false positive rate on SIMD code makes it unreliable. Valgrind with `--partial-loads-ok=yes` is more practical.
- For each sanitizer false positive, add a regression comment in the source explaining why the suppression is correct, so future developers don't remove it thinking it hides a real bug.

**Warning signs:**
- `make sanitize` fails immediately on hash functions with `stack-buffer-overflow` on an `__attribute__((aligned(32)))` local.
- The sanitizer report points to a line that reads a `__m256i` from a stack variable.
- Adding `-DSANITIZE=1` to a build causes a crash that does not reproduce without the flag.

**Phase to address:**
Sanitizer integration phase. Set up suppressions and `no_sanitize` annotations before enabling sanitizers in CI. Never run MSan in CI against SIMD code — it will always red-flag.

---

### Pitfall 4: `volatile` Does Not Provide Atomicity or Memory Ordering

**What goes wrong:**
`src/config/config.h` marks GPU progress counters as `volatile uint64_t` (`keys_checked`, `keys_checked_cur`, `should_stop`). On x86-64 this often "works" because x86 has strong memory ordering and 64-bit aligned loads/stores are atomic on that platform. On ARM (if ever targeted), Clang with `-O2`, or with ThreadSanitizer enabled, these `volatile` fields will produce data races, torn reads, or be optimized away entirely. TSan will report races on every access to these fields even on x86 because `volatile` provides no TSan annotation that prevents the report.

**Why it happens:**
`volatile` was historically used in C for memory-mapped I/O and cross-thread signaling. It prevents the compiler from caching the value in a register but does not generate memory barriers. Developers unfamiliar with the C++11 memory model substitute `volatile` for `std::atomic`.

**How to avoid:**
Replace all `volatile T` fields in `gpu_config_t` with `std::atomic<T>` using `memory_order_relaxed` for counters (where losing a few counts is acceptable) and `memory_order_seq_cst` for the `should_stop` flag. Mark `keyhunt_config_t` as `const` after initialization and before thread spawn to make the remaining config fields safe to read without locks.

**Warning signs:**
- TSan reports data races on `keys_checked` or `should_stop` fields.
- On a debug build, progress counters read zero despite the search running.
- Any line in `gpu_config_t` that has `volatile` keyword.

**Phase to address:**
Config migration phase, specifically when wiring config into GPU modules. Do not expose `volatile` fields in any new API introduced during monolith decomposition.

---

### Pitfall 5: Two Build Systems (Makefile + CMake) Drifting Out of Sync

**What goes wrong:**
A CMake build was added alongside the existing Makefile during the refactor. If a new source file is added to `Makefile` but not `CMakeLists.txt` (or vice versa), one build system silently omits a translation unit. For cryptographic code, a missing `.cpp` file means a fallback path that the developer expected to be dead code becomes the live path — for example, the scalar RIPEMD160 path being used when AVX2 was expected.

**Why it happens:**
Parallel build systems require synchronized manual updates. No enforcement mechanism prevents forgetting to update both.

**How to avoid:**
Pick one build system as authoritative for CI. If CMake is the future direction, CI should build only with CMake. The Makefile can remain for developer convenience but should not be the CI gate. Add a CI job that compares the list of `.cpp` and `.cu` files referenced in each build system and fails if they diverge. Alternatively, use a `glob` in `CMakeLists.txt` for source directories and rely on the Makefile pattern rules — they will naturally stay in sync if both use the same directory structure.

**Warning signs:**
- Adding a new `.cpp` to the Makefile with a specific per-file flag (`-mavx2`) does not result in a corresponding line in `CMakeLists.txt`.
- A function is linked in a Makefile build but produces "undefined reference" in CMake.
- Performance degrades after adding a new optimized file because CMake omits it.

**Phase to address:**
CI setup phase. Resolve build system authority before adding new source files.

---

### Pitfall 6: GPU Kernels Silently Produce Wrong Cryptographic Results

**What goes wrong:**
CUDA and OpenCL kernels implement SHA256 and RIPEMD160 in vendor-specific compute shader languages. A subtle bug in byte-order handling, padding logic, or modular arithmetic produces hashes that are plausible-looking but wrong — for example, off-by-one in SHA256 padding, or wrong endian swap in RIPEMD160. The kernel runs to completion with no error code. The search reports zero keys found, which is indistinguishable from "the target key is not in this range." A user running a 72-hour GPU puzzle search gets a false negative with no indication that results were corrupt.

**Why it happens:**
GPU kernels are harder to unit test than CPU code. The typical pattern is to run the GPU and see if it "finds something," not to compare GPU hash outputs against reference implementations on a known-answer test vector.

**How to avoid:**
For every GPU kernel, add a correctness test that runs the kernel on known inputs and compares output byte-for-byte against the CPU reference implementation. These tests must run in CI (which currently has no GPU runner — use software emulation or add a real GPU runner). Add a `--self-test` flag to the binary that runs 1000 random keys through both the CPU and GPU paths and reports any divergence before beginning a real search. This self-test should run automatically on first use of `-G` mode.

**Warning signs:**
- GPU mode reports 0 keys/s improvement over CPU-only mode (kernel not actually running).
- A key known to be in the target range is not found by GPU mode but is found by CPU mode.
- GPU mode "finds" a key that does not verify when re-checked with CPU mode.

**Phase to address:**
GPU correctness verification phase, which must precede any GPU performance optimization phase. Performance is meaningless if results are wrong.

---

### Pitfall 7: Config Migration Creates a Period of Mixed Old/New Code Paths

**What goes wrong:**
During the migration from `extern` globals to `keyhunt_config_t`, some functions read `config->search.mode` while others still read the global `FLAGMODE`. If a search function is migrated first but the caller that sets the value is migrated last, the function reads a `config` field that was never populated — it is zero-initialized (usually `MODE_ADDRESS`), which can silently cause the wrong search algorithm to run regardless of the user's `-m` flag.

**Why it happens:**
Partial migrations are inherently inconsistent. The compiler does not warn when a migrated function reads a config field that the un-migrated writer never sets.

**How to avoid:**
Migrate in write-then-read order: migrate the code that writes a value (`FLAGMODE = MODE_BSGS`) to `config->search.mode = MODE_BSGS` before migrating any reader. As a validation step, add a `keyhunt_config_validate()` function that asserts all required fields are non-zero after argument parsing and before any threads start. Run this validator in every test. Add a CI check that runs all 6 search modes briefly against known test inputs to catch silent mode mismatches.

**Warning signs:**
- Switching from `-m address` to `-m bsgs` with the same binary produces identical output.
- A search that should use the BSGS algorithm runs at address-search throughput.
- `config->search.mode` is `0` (MODE_ADDRESS default) regardless of the `-m` flag passed.

**Phase to address:**
Config migration phase. Include mode-correctness smoke tests at the end of every individual migration step, not just at the end of the overall migration.

---

## Technical Debt Patterns

Shortcuts that seem reasonable but create long-term problems.

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Keep `extern` globals alongside new config during migration | Avoids big-bang rewrite | Config drift, untestable modules, race conditions | Only during a bounded migration window with a hard deadline; never indefinite |
| `volatile` for GPU progress counters instead of `std::atomic` | Simpler to write; "works" on x86 | TSan false positives, races on non-x86, optimizer can eliminate | Never; `std::atomic<uint64_t>` with `memory_order_relaxed` costs nothing |
| One CI matrix entry (Ubuntu x86_64 GCC) for cross-platform claims | Fast CI | Windows MinGW and macOS diverge silently | Only as a starting point; add Windows CI within the same milestone |
| Skip sanitizer run for SIMD files entirely | Avoids false positives | Real bugs in non-SIMD surrounding code go undetected | Use per-function `no_sanitize` instead of excluding whole files |
| Comment `// TODO: fix test` instead of fixing pre-existing failures | Defers work | Masks new regressions; makes CI unreliable | Never in a hardening milestone; fix or skip with a tracking issue |
| Using `void *write_mutex` in config structs instead of typed mutex | C-compatible headers | Type unsafe, no RAII, easy to use wrong lock | Only where C/C++ ABI boundary is mandatory; wrap in typed accessor in C++ code |

---

## Integration Gotchas

Common mistakes when connecting components during hardening.

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| Passing `keyhunt_config_t *` to search modules | Functions keep the `extern` fallback and the `config` param both, causing dual-reads from inconsistent sources | Remove the `extern` declaration at the same commit that adds the `config` param; break the build if old name remains |
| Wiring GPU backends into the config system | GPU backend reads `FLAGGPU_FULL` global while the config-migration branch sets `config->gpu.mode`; they diverge silently | Audit every `FLAGGPU_*` read site before merging the GPU module changes |
| Cross-platform thread abstraction (`platform_thread.h`) | Code under test calls `pthread_create` directly instead of `platform_thread_create`, so TSan can't instrument it | Enforce the abstraction layer via a CI lint step that greps for raw `pthread_` or `CreateThread` calls outside `platform_thread.c` |
| Bloom filter file format versioning | Adding a field to the on-disk struct without incrementing the magic number causes old caches to be read with the new layout, corrupting BSGS state silently | Always bump the format version constant when changing the layout; add a compile-time `static_assert` on the struct size |
| Distributed coordinator JSON from untrusted workers | `atoi()`/`sscanf()` on `range_start` fields from client messages can be fed malformed values that wrap or go negative | Use `strtoull()` with errno check; validate min/max bounds before use; treat all coordinator inputs as untrusted |

---

## Performance Traps

Patterns that work at small scale but fail under sustained load.

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Detecting CPU features on every thread start instead of once at main | Throughput degrades as thread count increases due to serialized CPUID calls | Detect once in `main()` before thread creation; pass result through config | Threads > 8 |
| Marking config fields `volatile` to share across GPU threads | Forces every read to go to memory; defeats store-to-load forwarding in tight loops | Use `std::atomic` with `relaxed` ordering for counters; use event or fence for stop signals | Always; measurable even with 1 thread |
| Using `select()`-based I/O for distributed coordinator with 1000 workers | Worker heartbeat timeout overhead grows O(n) per loop iteration | Use `epoll` on Linux, `IOCP` on Windows, or use libuv/libev | >64 concurrent connections on Windows (select fd limit); >500 on Linux |
| Loading BSGS bloom filter files with single-threaded fread | 500 GB cache load takes 30+ minutes on NVMe that supports 7 GB/s | Use mmap + parallel read threads or pread with thread pool | Cache files > 10 GB |
| Batching bloom filter checks 64 at a time without prefetching | L2 cache miss on every bloom check; throughput plateau at ~200 MKeys/s | Explicit `_mm_prefetch()` 200-400 cache lines ahead; verified in `bsgs_optimized.h` | Single-threaded address search above 100 MKeys/s |

---

## Security Mistakes

Domain-specific security issues in this cryptographic tool.

| Mistake | Risk | Prevention |
|---------|------|------------|
| Bloom filter cache files validated only with CRC32, not cryptographic MAC | A malicious or corrupted `.blm` file can cause systematic false negatives — valid keys are silently skipped during a long search | Add HMAC-SHA256 over file content, key derived from a user-supplied or machine-unique secret; fail hard on MAC mismatch |
| Distributed mode accepts `range_start`/`range_end` from workers without bounds check | A rogue client can report that it searched a range it did not actually search, causing coverage gaps | Server must assign ranges, not accept them from clients; workers report only found keys and completion confirmations |
| `getenv("KEYHUNT_DEBUG")` check in 50+ places in `distributed.c` without caching | `getenv()` is not async-signal-safe and on some platforms takes a lock; calling it in tight network loops causes contention | Cache the result once at startup: `static bool debug = getenv("KEYHUNT_DEBUG") != NULL;` |
| No TLS by default in distributed mode | Worker-to-coordinator traffic (including found keys) is cleartext on the network | Default to TLS-required; make plaintext opt-in with an explicit `--allow-plaintext` flag and a loud warning |
| Custom secp256k1 implementation without cross-validation against a reference | A subtle bug in point addition or modular inversion could cause all keys in a range to be computed incorrectly, producing systematic false negatives | Add a CI test that generates 10,000 random keys, computes their addresses via the custom secp256k1, and verifies against a known-good reference (Python `coincurve` or `bitcoin-core/secp256k1`) |

---

## "Looks Done But Isn't" Checklist

Things that appear complete but are missing critical pieces.

- [ ] **Config migration:** Externs removed from search module headers — verify with `grep -rn "extern.*FLAGMODE\|extern.*NTHREADS" src/search/` returns zero hits.
- [ ] **Test baseline:** CI is green — verify the count of PASS/SKIP/FAIL in CI output, not just "tests ran."
- [ ] **Sanitizer integration:** `make sanitize` passes — verify it actually runs all test cases, not just compiles. An empty test run passes vacuously.
- [ ] **GPU correctness:** GPU mode enabled in CI — verify the CI matrix actually includes a GPU runner or software emulation that exercises the CUDA/OpenCL code paths.
- [ ] **Cross-platform CI:** Windows CI passes — verify MinGW build compiles AND runs the test suite, not just produces a binary.
- [ ] **Monolith decomposition:** `keyhunt.cpp` under 1000 lines — verify the extracted mode files do not re-introduce globals that were supposed to be eliminated.
- [ ] **P2SH/BECH32 implementation:** "Not implemented" placeholder removed — verify by running `./keyhunt -m address -l p2sh -f tests/bech32_addresses.txt` and checking for non-zero output count.
- [ ] **SIMD dispatch unified:** Single `g_avx2_available` source — verify with `grep -rn "g_avx2_available" src/` shows exactly one definition (in `config.cpp` or `sysinfo.c`) and all others are `config->autotune.has_avx2`.

---

## Recovery Strategies

When pitfalls occur despite prevention, how to recover.

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Duplicate CPU feature variables diverge in production | HIGH | Bisect with `git bisect` to find which merge introduced the divergence; add `assert(g_avx2_available == config->autotune.has_avx2)` as temporary diagnostic; then proceed with full config migration |
| Pre-existing test failures mask a new regression | MEDIUM | Document all known failures in `KNOWN_FAILURES.md` with explicit `SKIP` annotations; re-run full test suite with `--failures-only` filter to isolate new failures from known ones |
| Sanitizer false positive blocks CI | LOW | Add targeted `__attribute__((no_sanitize("address")))` to the specific function; add a comment explaining the annotation; do not suppress entire sanitizer for the binary |
| Config migration breaks a search mode silently | MEDIUM | Roll back to the pre-migration baseline for that module; add end-to-end test that verifies the mode before re-attempting migration |
| GPU kernel produces wrong results discovered late | HIGH | Immediately add known-answer tests for all GPU kernel outputs; run the failing hash inputs through CPU reference to identify which kernel function is wrong; suspend GPU mode in production binary until fixed |
| Windows CI breaks due to MinGW API differences | LOW | Create a minimal reproducer on Windows runner; check `src/platform/` for the missing abstraction; add the Windows-specific path inside the platform layer rather than in business logic |

---

## Pitfall-to-Phase Mapping

How roadmap phases should address these pitfalls.

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| Duplicate CPU feature detection variables | Config migration (eliminate all extern globals) | `grep -rn "g_avx2_available" src/` shows exactly one non-`extern` definition |
| Pre-existing tests masking new regressions | Test baseline (Phase 1, before everything else) | CI shows zero unexplained failures; all known failures have `SKIP` annotations |
| Sanitizer false positives on SIMD | Sanitizer integration phase | `make sanitize` passes with no suppression of test cases; per-function annotations documented |
| `volatile` instead of `std::atomic` | Config migration phase (atomicity audit) | `grep -rn "volatile" src/config/` returns zero results |
| Dual build systems drifting | CI setup phase | A CI job diffs source file lists between Makefile and CMakeLists.txt and fails on divergence |
| GPU kernels silently wrong | GPU correctness verification phase | Known-answer tests for SHA256+RIPEMD160 pass on both CUDA and OpenCL paths in CI |
| Config migration mixed old/new paths | Config migration phase (write-then-read order discipline) | `keyhunt_config_validate()` runs in every test and asserts non-zero required fields |
| Two `g_avx2_available` causing BSGS/address dispatch divergence | Config migration phase | Test that explicitly runs both ADDRESS and BSGS mode and compares dispatch log output |
| Cache file corruption going undetected | Security hardening sub-phase of test coverage | `test_stale_cache.c` extended to test HMAC validation; corrupted file causes hard failure |
| Bloom filter false negatives from wrong hash function selection | Test coverage phase | Property test that inserts N items and verifies zero false negatives, then measures actual false positive rate against theoretical |

---

## Sources

- Codebase audit: `/home/aiacos/workspace/keyhunt/.planning/codebase/CONCERNS.md` (HIGH confidence, first-party)
- Codebase direct inspection: `src/bsgs/bsgs_fast.cpp`, `src/keyhunt.cpp`, `src/search/search_address.cpp`, `src/config/config.h` (HIGH confidence, first-party)
- [Understanding AddressSanitizer: Better Memory Safety for Your Code — Trail of Bits, May 2024](https://blog.trailofbits.com/2024/05/16/understanding-addresssanitizer-better-memory-safety-for-your-code/) (MEDIUM confidence)
- [GCC 13 on Ubuntu: ASan/AVX-512 alignment false positive — Launchpad bug #2023424](https://bugs.launchpad.net/ubuntu/+source/gcc-13/+bug/2023424) (MEDIUM confidence)
- [AddressSanitizer — Clang documentation](https://clang.llvm.org/docs/AddressSanitizer.html) (HIGH confidence, official)
- [ThreadSanitizer false positives with condition variables — Google Sanitizers issue #1259](https://github.com/google/sanitizers/issues/1259) (MEDIUM confidence)
- [UBSan left-shift undefined behavior — Clang UBSan documentation](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) (HIGH confidence, official)
- [Mixing C and C++ Code in the Same Program — Oracle Technical Resources](https://www.oracle.com/technical-resources/articles/it-infrastructure/mixing-c-and-cplusplus.html) (MEDIUM confidence)
- [Do not call a function with mismatched language linkage — SEI CERT C++](https://wiki.sei.cmu.edu/confluence/display/cplusplus/EXP56-CPP.+Do+not+call+a+function+with+a+mismatched+language+linkage) (HIGH confidence, authoritative)
- [GCC AVX2 target attribute optimization bug — Codeforces, 2024](https://codeforces.com/blog/entry/149099) (LOW confidence, community report)
- [GitHub Actions cache stale artifacts discussion — GitHub community](https://github.com/orgs/community/discussions/169413) (MEDIUM confidence)
- [Wrong results from OpenCL driver — NVIDIA Developer Forums](https://forums.developer.nvidia.com/t/wrong-results-from-the-opencl-driver/14803) (MEDIUM confidence)
- [Analysis of False Negative Rates for Recycling Bloom Filters — ACM SIGMETRICS 2024](https://dl.acm.org/doi/10.1145/3656005) (MEDIUM confidence)
- Thread safety migration pitfalls — multiple cppreference.com and Bartosz Milewski sources (MEDIUM confidence)

---
*Pitfalls research for: C/C++ cryptographic tool production hardening (keyhunt)*
*Researched: 2026-02-28*
