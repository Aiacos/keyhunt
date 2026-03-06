# Phase 4: Monolith Decomposition - Research

**Researched:** 2026-03-06
**Domain:** C++ monolith decomposition, static analysis gates, compiler hardening
**Confidence:** HIGH

## Summary

Phase 4 transforms `src/keyhunt.cpp` (5501 lines) into a thin ~600-line orchestrator by extracting mode dispatchers into `src/modes/`, absorbing ~100+ global variables into `keyhunt_config_t`, completing io.cpp config wiring (CFG-07), and applying static analysis and hardening gates. The codebase is well-prepared: Phase 3 already wired all search modules through config, so mode extractors only need to move the initialization/setup/dispatch code that sits between CLI parsing and thread creation.

The main structural blocks in keyhunt.cpp are: CLI parsing (~1260 lines, 1588-2847), BSGS initialization (~1334 lines, 2847-4180), non-BSGS thread dispatch + monitoring loop (~881 lines, 4180-5060), and post-main utility functions (~442 lines, 5060-5501). The monitoring loop (status printing, progress tracking, rate calculation) is the trickiest extraction because it references both mode-specific and orchestrator-level state.

**Primary recommendation:** Extract modes in dependency order (address first, then xpoint/rmd160 which share code paths, then vanity/minikeys, then BSGS last as most complex). Move GPU dispatch and utility functions to their own modules before mode extraction to reduce coupling. Complete io.cpp wiring as part of the first mode extraction since all modes call writekey().

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- One file per mode in `src/modes/`: mode_address.cpp, mode_bsgs.cpp, mode_xpoint.cpp, mode_rmd160.cpp, mode_vanity.cpp, mode_minikeys.cpp
- `modes.h` provides dispatch table interface
- BSGS initialization (~700 lines of bloom/table setup) goes into mode_bsgs.cpp alongside dispatch logic (self-contained, ~800 lines)
- GPU dispatch functions (gpu_upload_*, gpu_run_*, gpu_found_callback) move to `src/gpu/` not into mode files
- Utility functions (thread_rand, sleep_ms, profiling, work queue) move to `src/util/`
- keyhunt.cpp retains: main(), CLI arg parsing, config building, mode_dispatch() call, thread wait, results printing, cleanup
- Target: ~400-600 lines
- Signal handling stays in orchestrator
- Full config wiring for io.cpp during decomposition (complete CFG-07)
- Change writekey() signature to accept `const keyhunt_config_t *config`
- Wire all 22 local externs through config parameter
- Absorb all ~100+ globals into keyhunt_config_t sub-structs
- Thread counters (THREADCOUNTER, FINISHED_THREADS_COUNTER, OLDFINISHED_ITEMS) as atomics in runtime_state_t
- Vanity state packed into vanity_state_t sub-struct
- Mode-specific flags absorbed into appropriate config sub-structs
- No globals.h fallback -- everything goes through config
- Static analysis gates applied during decomposition, not as a separate pass
- All code in src/modes/ must be clang-tidy clean
- Compiler hardening flags applied to entire build

### Claude's Discretion
- Exact extraction order (which mode to extract first)
- How to handle bPload thread externs (Phase 3 noted these run before bsgs_context_t exists)
- Work queue implementation details during extraction
- Exact clang-tidy check selection beyond bugprone-* and clang-analyzer-security.*

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| STR-01 | Extract ADDRESS mode dispatcher from keyhunt.cpp to src/modes/ | Lines 2750-2818 (file reading, GPU init, bloom setup) + 4578-4584 (thread dispatch) extracted to mode_address.cpp |
| STR-02 | Extract BSGS mode dispatcher from keyhunt.cpp to src/modes/ | Lines 2847-4180 (entire BSGS init block, ~1334 lines) extracted to mode_bsgs.cpp |
| STR-03 | Extract XPOINT and RMD160 mode dispatchers from keyhunt.cpp to src/modes/ | Share code path with ADDRESS (lines 4578-4584 switch cases); split into mode_xpoint.cpp and mode_rmd160.cpp |
| STR-04 | Extract VANITY and MINIKEYS mode dispatchers from keyhunt.cpp to src/modes/ | Lines 2685-2724 (minikey init), 2760-2765 (vanity file read), 4587-4597 (thread dispatch) |
| STR-05 | keyhunt.cpp reduced to thin dispatcher (~600 lines or less) | After extraction, orchestrator retains: main(), CLI parsing (getopt loop), config building, mode_dispatch(), monitoring loop, cleanup |
| STR-06 | Static analysis gate with clang-tidy (bugprone-* + clang-analyzer-security.*) | clang-tidy 21.1.8 available; 227 checks in selected categories; compilation database needed (bear installed) |
| STR-07 | Static analysis gate with cppcheck (error + warning level) | cppcheck 2.19.1 available; correct invocation is `cppcheck --enable=warning --error-exitcode=1` (not `--enable=error`) |
| STR-08 | OpenSSF compiler hardening flags | GCC 15.2.1 supports all flags: -D_FORTIFY_SOURCE=3, -fstack-protector-strong, -fcf-protection; verified compilation succeeds |
</phase_requirements>

## Standard Stack

### Core (Already in Project)
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| GCC | 15.2.1 | Compiler | Project compiler, supports all hardening flags |
| clang-tidy | 21.1.8 (LLVM) | Static analysis (bugprone, security) | Industry standard C++ linter |
| cppcheck | 2.19.1 | Static analysis (warnings, errors) | Complementary to clang-tidy, catches different bugs |
| bear | (installed) | Compilation database generator | Required for clang-tidy to understand build flags |
| GNU Make | existing | Build system | Current build system |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| bear | system | Generate compile_commands.json from Makefile | Run once: `bear -- make clean all` before clang-tidy |

### Alternatives Considered
None -- all tools are locked decisions from CONTEXT.md.

## Architecture Patterns

### Recommended Project Structure (Post-Decomposition)
```
src/
├── keyhunt.cpp              # Thin orchestrator (~400-600 lines)
├── modes/                   # Mode dispatchers (new)
│   ├── modes.h              # Dispatch table interface
│   ├── mode_address.cpp     # ADDRESS mode: file read, GPU init, thread dispatch
│   ├── mode_bsgs.cpp        # BSGS mode: bloom init, table setup, thread dispatch (~800 lines)
│   ├── mode_xpoint.cpp      # XPOINT mode dispatcher
│   ├── mode_rmd160.cpp      # RMD160 mode dispatcher
│   ├── mode_vanity.cpp      # VANITY mode dispatcher
│   └── mode_minikeys.cpp    # MINIKEYS mode dispatcher
├── gpu/                     # GPU dispatch (existing + new)
│   ├── gpu_dispatch.cpp     # Moved from keyhunt.cpp: gpu_upload_*, gpu_run_*, gpu_found_callback
│   └── ...                  # Existing GPU backend files
├── util/                    # Utility functions (existing + new)
│   ├── util.cpp             # Moved from keyhunt.cpp: thread_rand, sleep_ms
│   ├── profiling.cpp        # Moved from keyhunt.cpp: profile_*, append_profile_info
│   ├── work_queue.cpp       # Moved from keyhunt.cpp: configure_work_queue, shutdown_work_queue
│   └── mempool.c/h          # Existing
├── search/                  # Thread functions (existing, already config-wired)
├── config/                  # Config structs (existing)
└── io/                      # File I/O (existing, to be config-wired)
```

### Pattern 1: Mode Dispatch Table
**What:** A dispatch table mapping search_mode_t to mode entry functions
**When to use:** Replacing the switch(FLAGMODE) blocks in main()
**Example:**
```cpp
// src/modes/modes.h
#include "../config/config.h"

// Each mode entry function takes config, sets up mode-specific state,
// reads targets, dispatches threads, and returns thread handles
typedef struct {
    int (*init)(keyhunt_config_t *config);      // Read files, init bloom, GPU setup
    int (*run)(keyhunt_config_t *config);        // Create threads, return 0 on success
    void (*cleanup)(keyhunt_config_t *config);   // Mode-specific cleanup
} mode_ops_t;

// Mode registry
const mode_ops_t* mode_get_ops(search_mode_t mode);

// Convenience: init + run in one call
int mode_dispatch(keyhunt_config_t *config);
```

### Pattern 2: Global Absorption into Config
**What:** Move each global variable into the appropriate keyhunt_config_t sub-struct
**When to use:** During extraction, replace `extern int FLAGFOO` with `config->search.foo`
**Example:**
```cpp
// Before (keyhunt.cpp global):
int FLAGENDOMORPHISM = 0;
// Used in mode: if (FLAGENDOMORPHISM) { ... }

// After (in config sub-struct):
// search_config_t already has: bool endomorphism;
// Mode function receives config pointer:
void mode_address_init(keyhunt_config_t *config) {
    bool endomorphism = config->search.endomorphism;
    // ...
}
```

### Pattern 3: io.cpp Config Wiring
**What:** Change writekey/writekeyeth signatures to accept config, eliminating 22 extern globals
**When to use:** First mode extraction (all modes call writekey)
**Example:**
```cpp
// Before:
void writekey(bool compressed, Int *key);
// Uses extern secp, FLAGMODE, FLAGCRYPTO, write_keys, etc.

// After:
void writekey(const keyhunt_config_t *config, bool compressed, Int *key);
// Extracts secp from config->runtime.secp
// Extracts mode from config->search.mode
// Extracts mutex from config->runtime.write_mutex
```

### Pattern 4: Monitoring Loop Stays in Orchestrator
**What:** The status printing / progress tracking loop (lines 4607-5016) stays in keyhunt.cpp
**When to use:** This loop checks thread completion flags and prints status -- it is mode-agnostic
**Key insight:** The monitoring loop only reads from `steps[]`, `ends[]`, and atomic counters. These are already in `runtime_state_t`. The loop does not need to know which mode is running.

### Anti-Patterns to Avoid
- **Premature abstraction in mode files:** Each mode file should be a straightforward extraction of the code from keyhunt.cpp. Do not refactor the algorithm logic. Move first, clean later.
- **Leaving half-extracted globals:** Every global that a mode file needs must be absorbed into config before the mode can compile independently. Do not leave `extern` declarations as a bridge.
- **Shared monitoring code in modes:** The monitoring loop belongs in the orchestrator. Modes only do init + thread creation + return.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Compilation database | Manual compile_commands.json | `bear -- make clean all` | bear intercepts compiler calls accurately |
| Static analysis config | Per-file clang-tidy invocations | `.clang-tidy` config file + Makefile target | Reproducible, consistent across all files |
| Suppression management | Inline NOLINT everywhere | Targeted NOLINT with reason + `.clang-tidy` WarningsAsErrors | Auditable, minimal suppressions |
| Thread-safe global replacement | Custom atomic wrappers | std::atomic (already used in codebase) | Standard, well-tested |

## Common Pitfalls

### Pitfall 1: _FORTIFY_SOURCE Requires Optimization
**What goes wrong:** `-D_FORTIFY_SOURCE=3` has no effect at `-O0` and causes warnings/errors
**Why it happens:** FORTIFY_SOURCE requires at least `-O1` to activate; GCC silently ignores or warns
**How to avoid:** The project already uses `-O2` in OPT_FLAGS. Verify hardening flags are added alongside OPT_FLAGS, not to debug/sanitizer builds that may use `-O0`
**Warning signs:** `#warning _FORTIFY_SOURCE requires compiling with optimization (-O)`

### Pitfall 2: clang-tidy bugprone-easily-swappable-parameters on SIMD Functions
**What goes wrong:** Functions like `aligned_calloc(size_t alignment, size_t nmemb, size_t size)` trigger noisy warnings
**Why it happens:** This check flags adjacent parameters of the same type -- very common in low-level code
**How to avoid:** Suppress `bugprone-easily-swappable-parameters` globally in `.clang-tidy` or for specific functions with NOLINT. This check produces too many false positives in systems code.
**Warning signs:** Hundreds of warnings from hash/secp256k1 headers

### Pitfall 3: Reserved Identifier Warnings (_swap, _sort, etc.)
**What goes wrong:** clang-tidy flags `_swap`, `_sort`, `_introsort`, `_insertionsort`, `_partition` in sort/sort.h
**Why it happens:** Identifiers starting with underscore are reserved in the global namespace (C++ standard)
**How to avoid:** Rename these functions during decomposition (e.g., `kh_swap`, `kh_sort`). Already flagged by clang-tidy `bugprone-reserved-identifier`.
**Warning signs:** 5+ warnings per translation unit from sort.h

### Pitfall 4: cppcheck dangerousTypeCast in secp256k1/Int.h
**What goes wrong:** cppcheck flags _umul128 / _addcarry_u64 intrinsics in Int.h with dangerousTypeCast
**Why it happens:** These are MSVC intrinsic emulations with old-style C casts; cppcheck 2.19 is stricter
**How to avoid:** These are in vendored secp256k1 code. Suppress with inline comment or `--suppress=dangerousTypeCast:src/secp256k1/Int.h`. Do NOT modify the crypto math.
**Warning signs:** Warnings appear for every file that includes Int.h

### Pitfall 5: compile_commands.json Must Include All Build Variants
**What goes wrong:** clang-tidy cannot analyze files not in compile_commands.json
**Why it happens:** bear only captures what the Makefile actually compiles. CUDA (.cu) and OpenCL (.cl) files are not captured.
**How to avoid:** Run `bear -- make clean all` for the standard build. For GPU files, add manual entries or skip them from clang-tidy analysis (they use different compilers anyway).
**Warning signs:** clang-tidy reports "file not found in compilation database"

### Pitfall 6: Circular Dependencies During Extraction
**What goes wrong:** mode_address.cpp needs a function still defined in keyhunt.cpp, which needs a function being moved
**Why it happens:** The monolith has implicit bidirectional dependencies through globals
**How to avoid:** Extract utilities (thread_rand, sleep_ms, profiling) FIRST to src/util/ before extracting modes. Then modes depend on util, not on keyhunt.cpp.
**Warning signs:** Linker errors (undefined reference) during incremental extraction

### Pitfall 7: BSGS bPload Threads Running Before bsgs_context_t Exists
**What goes wrong:** bPload/bPload_2blooms threads run during BSGS init, before bsgs_context_t is populated
**Why it happens:** Phase 3 decision [03-04] noted these threads keep local extern declarations
**How to avoid:** In mode_bsgs.cpp, construct bsgs_context_t early (before bPload threads) and pass it to bPload threads. This requires reordering the init sequence slightly: allocate bloom arrays + bsgs_context first, then spawn bPload threads with context pointer.
**Warning signs:** Segfault during BSGS init if bsgs_context_t fields are accessed before population

### Pitfall 8: cppcheck --enable=error is Invalid
**What goes wrong:** `cppcheck --enable=warning,error` fails with "unknown name 'error'"
**Why it happens:** cppcheck 2.19 does not accept `error` as an `--enable` category. Errors are always enabled.
**How to avoid:** Use `cppcheck --enable=warning --error-exitcode=1` for CI-style gating
**Warning signs:** cppcheck exits immediately with usage error

## Code Examples

### Generating compile_commands.json
```bash
# Run from project root
bear -- make clean all
# Produces compile_commands.json in project root
# clang-tidy automatically picks this up
```

### .clang-tidy Configuration File
```yaml
# .clang-tidy
Checks: >
  -*,
  bugprone-*,
  clang-analyzer-security.*,
  -bugprone-easily-swappable-parameters,
  -bugprone-exception-escape

# Only apply to project headers, not system/vendored
HeaderFilterRegex: 'src/(modes|util|io|search|config|gpu|platform)/.*'

WarningsAsErrors: ''

# Suppress known false positives in vendored code
# (secp256k1 is vendored crypto, do not modify)
```

### clang-tidy Invocation
```bash
# Single file
clang-tidy -p . src/modes/mode_address.cpp

# All src/modes/ files
find src/modes/ -name '*.cpp' | xargs -P4 clang-tidy -p .

# All src/ tree (for STR-06 gate)
find src/ -name '*.cpp' -not -path 'src/secp256k1/*' -not -path 'src/gmp256k1/*' \
  | xargs -P4 clang-tidy -p . 2>&1 | tee clang-tidy-report.txt
```

### cppcheck Invocation
```bash
# Single file
cppcheck --enable=warning --error-exitcode=1 -Isrc src/modes/mode_address.cpp

# Full src/ tree (for STR-07 gate)
cppcheck --enable=warning --error-exitcode=1 \
  --suppress=dangerousTypeCast:src/secp256k1/Int.h \
  --suppress=dangerousTypeCast:src/gmp256k1/Int.h \
  -Isrc src/ 2>&1 | tee cppcheck-report.txt
```

### Hardening Flags in Makefile
```makefile
# Add to existing OPT_FLAGS or as separate HARDEN_FLAGS
HARDEN_FLAGS := -D_FORTIFY_SOURCE=3 -fstack-protector-strong -fcf-protection

# Apply to both C and C++
CXXFLAGS += $(HARDEN_FLAGS)
CFLAGS += $(HARDEN_FLAGS)
```

### Mode Dispatch Table Implementation
```cpp
// src/modes/modes.h
#ifndef MODES_H
#define MODES_H

#include "../config/config.h"
#include "../platform/platform.h"

// Mode operations structure
typedef struct {
    int (*init)(keyhunt_config_t *config);
    int (*run)(keyhunt_config_t *config,
               platform_thread_t *tids, int *thread_count);
    void (*cleanup)(keyhunt_config_t *config);
} mode_ops_t;

// Get operations for a mode
const mode_ops_t* mode_get_ops(search_mode_t mode);

// Convenience dispatcher
int mode_dispatch(keyhunt_config_t *config);

#endif
```

### io.cpp writekey Signature Change
```cpp
// New signature (io.h)
void writekey(const keyhunt_config_t *config, bool compressed, Int *key);
void writekeyeth(const keyhunt_config_t *config, Int *key);

// Implementation pattern (io.cpp) - replace extern globals with config reads
void writekey(const keyhunt_config_t *config, bool compressed, Int *key) {
    Secp256K1 *secp = (Secp256K1 *)config->runtime.secp;
    int mode = (int)config->search.mode;
    int crypto = (int)config->search.crypto_type;
    platform_mutex_t *mutex = (platform_mutex_t *)config->runtime.write_mutex;
    // ... rest of function uses these locals instead of externs
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| All globals in keyhunt.cpp | keyhunt_config_t sub-structs | Phase 3 (2026-02) | search modules config-wired; globals still exist for orchestrator |
| search_context.h externs | thread_args with config pointer | Phase 3 (2026-02) | Search threads no longer depend on keyhunt.cpp symbols |
| Monolithic mode dispatch | Mode files in src/modes/ | Phase 4 (this phase) | Each mode compiles independently |

**Pre-existing issues to fix during decomposition:**
- Reserved identifiers in sort.h: `_swap`, `_sort`, `_introsort`, `_insertionsort`, `_partition`
- io.cpp has 22 local extern declarations (documented Phase 4 targets)
- bPload threads use local externs (Phase 3 decision [03-04])
- Diagnostic warnings in keyhunt.cpp: misleading indentation, unused includes, anonymous typedef

## Extraction Size Estimates

| Target File | Source Lines | Content |
|-------------|-------------|---------|
| mode_address.cpp | ~200 | File reading, bloom setup, GPU init for address, thread dispatch |
| mode_bsgs.cpp | ~800 | Entire BSGS initialization (bloom alloc, file parse, table compute, bPload threads, bsgs_ctx populate, thread dispatch) |
| mode_xpoint.cpp | ~100 | File reading (xpoint format), thread dispatch (shares ADDRESS path) |
| mode_rmd160.cpp | ~100 | File reading (rmd160 format), thread dispatch (shares ADDRESS path) |
| mode_vanity.cpp | ~150 | Vanity file reading, vanity bloom setup, thread dispatch |
| mode_minikeys.cpp | ~100 | Minikey init (base key, coinbuffer), thread dispatch |
| gpu_dispatch.cpp | ~250 | gpu_upload_gtable, gpu_upload_targets, gpu_build_bloom, gpu_found_callback, gpu_run_full_search, gpu_hybrid_thread |
| util/profiling.cpp | ~200 | profile_init_threads, profile_set_thread, append_profile_info, append_adaptive_info |
| util/thread_util.cpp | ~100 | thread_rand, sleep_ms, aligned_calloc, aligned_free |
| util/work_queue.cpp | ~50 | configure_work_queue, shutdown_work_queue |
| **Total extracted** | **~2050** | |
| **keyhunt.cpp remaining** | **~3450 -> ~500** | After removing extracted code + absorbing globals (many globals become zero-line once moved to config init) |

## Global Variable Categorization

### Already in config sub-structs (just need removal from keyhunt.cpp)
These globals have config equivalents from Phase 3 bridge code:
- `FLAGMODE` -> `config->search.mode`
- `FLAGSEARCH` -> `config->search.key_format`
- `FLAGCRYPTO` -> `config->search.crypto_type`
- `FLAGENDOMORPHISM` -> `config->search.endomorphism`
- `FLAGQUIET` -> `config->search.quiet_mode`
- `FLAGDEBUG` -> `config->search.debug_mode`
- `FLAGSKIPCHECKSUM` -> `config->search.skip_checksum`
- `NTHREADS` -> `config->runtime.num_threads`
- `KFACTOR` -> `config->bsgs.k_factor`
- `FLAGGPU` -> `config->gpu.enabled`
- `FLAGGPU_FULL` -> `config->gpu.full_mode`
- `FLAGGPU_HYBRID` -> (needs new atomic field or use gpu.hybrid_mode)

### Need new config fields
- `FLAGBSGSMODE` -> `config->bsgs.bsgs_mode` (already exists)
- `FLAGRANGE`, `FLAGBITRANGE`, `FLAGFILE`, `FLAG_N` -> new `explicitly_set` flags
- `FLAGMATRIX`, `FLAGPROGRESSBAR`, `FLAGVISUAL` -> new display config fields
- `FLAGVANITY`, `FLAGBASEMINIKEY` -> mode-specific state
- `FLAGSAVEREADFILE`, `FLAGREADEDFILE1-4`, `FLAGUPDATEFILE1` -> file state tracking
- `FLAGSTRIDE` -> `config->search.stride_enabled` (already exists)
- `FLAGRANDOM` -> `config->search.random_mode` (already exists)
- `FLAGPRECALCUTED_P_FILE` -> `config->bsgs.load_precalc` (already exists)
- `byte_encode_crypto` -> derive from `config->search.crypto_type`
- `vanity_*` globals -> `config->runtime.vanity_*` (partially mapped)
- `THREADCOUNTER`, `FINISHED_THREADS_COUNTER`, `OLDFINISHED_ITEMS` -> atomics in runtime_state_t
- `FINISHED_ITEMS` -> already std::atomic, map to runtime_state
- `g_gpu_keys_checked*` -> `config->gpu.keys_checked*` (already atomic in gpu_config_t)

### Globals that become local to modes
- `N` (target count) -> local in mode init, stored in `config->runtime.address_count`
- `addressTable` -> allocated in mode init, stored in `config->runtime.address_table`
- `bloom` -> allocated in mode init, stored in `config->runtime.bloom_filter`
- `steps[]`, `ends[]` -> allocated in mode init, stored in `config->runtime.thread_counters/flags`
- `tid[]` -> allocated in orchestrator (thread handles)
- BSGS-specific (BSGS_N, BSGS_M, bloom_bP, bPtable, etc.) -> local to mode_bsgs.cpp
- Generator points (Gn, _2Gn, GSn, _2GSn) -> allocated in init, stored in config->runtime

## Open Questions

1. **Monitoring loop placement**
   - What we know: The monitoring loop (lines 4607-5016) reads thread counters, prints status, handles GPU hybrid stats. It is ~400 lines.
   - What's unclear: Should it stay in keyhunt.cpp or become a shared utility?
   - Recommendation: Keep in keyhunt.cpp. It is orchestrator logic (wait for threads, report status). This is consistent with "keyhunt.cpp retains thread wait, results printing."

2. **How many globals can be eliminated vs. need new config fields?**
   - What we know: ~40 of the ~100+ globals already map to existing config fields. ~20 need new fields. ~40 become local to mode files.
   - What's unclear: Exact count depends on what the monitoring loop needs
   - Recommendation: Add display config fields (matrix, visual, progressbar) and file-state tracking fields to config. The rest become mode-local.

3. **bPload thread extern resolution**
   - What we know: bPload/bPload_2blooms threads run during BSGS init before bsgs_context_t is fully populated
   - What's unclear: Exact minimum fields these threads need from context
   - Recommendation: In mode_bsgs.cpp, populate bsgs_context_t bloom fields BEFORE spawning bPload threads. The threads only need bloom arrays and bP table pointers, which are allocated before the threads start.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Custom test_framework.h + functional tests |
| Config file | Makefile test targets |
| Quick run command | `make clean all && ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF` |
| Full suite command | `make test` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| STR-01 | ADDRESS mode works after extraction | smoke | `./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF` | N/A (runtime test) |
| STR-02 | BSGS mode works after extraction | smoke | `./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10 -R` | N/A (runtime test) |
| STR-03 | XPOINT/RMD160 modes work after extraction | smoke | `./keyhunt -m xpoint -f tests/120.txt -t 4 -b 125 -R -q` | N/A (runtime test) |
| STR-04 | VANITY/MINIKEYS modes work after extraction | smoke | `./keyhunt -m vanity -f tests/vanity.txt -r 1:FFFFFFFF -s 5` | N/A (runtime test) |
| STR-05 | keyhunt.cpp <= 600 lines | metric | `wc -l src/keyhunt.cpp` | N/A (metric check) |
| STR-06 | clang-tidy clean on src/ | static-analysis | `clang-tidy -p . src/modes/*.cpp` | Wave 0: .clang-tidy config |
| STR-07 | cppcheck clean on src/ | static-analysis | `cppcheck --enable=warning --error-exitcode=1 -Isrc src/` | Wave 0: cppcheck suppression list |
| STR-08 | Hardening flags compile clean | build | `make CXXFLAGS+=-D_FORTIFY_SOURCE=3...` | N/A (Makefile change) |

### Sampling Rate
- **Per task commit:** `make clean all && ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF`
- **Per wave merge:** Full smoke test of all modes + clang-tidy + cppcheck on changed files
- **Phase gate:** `wc -l src/keyhunt.cpp` <= 600 AND clang-tidy clean AND cppcheck clean AND all mode smoke tests pass

### Wave 0 Gaps
- [ ] `.clang-tidy` -- config file with check selection and suppressions
- [ ] `compile_commands.json` -- via `bear -- make clean all`
- [ ] `src/modes/` directory -- does not exist yet
- [ ] Verify cppcheck suppressions for vendored secp256k1/Int.h

## Sources

### Primary (HIGH confidence)
- Direct codebase analysis: src/keyhunt.cpp (5501 lines), src/config/config.h, src/io/io.cpp
- Tool version verification: clang-tidy 21.1.8, cppcheck 2.19.1, GCC 15.2.1
- Compiler flag test: `-D_FORTIFY_SOURCE=3 -fstack-protector-strong -fcf-protection` verified

### Secondary (MEDIUM confidence)
- cppcheck `--enable=error` invalidity: confirmed by direct invocation failure
- bear availability: confirmed installed and working

### Tertiary (LOW confidence)
- None

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - all tools verified locally, versions confirmed
- Architecture: HIGH - based on direct analysis of 5501-line source with line-level mapping
- Pitfalls: HIGH - confirmed by running tools against actual codebase (clang-tidy, cppcheck outputs examined)
- Extraction estimates: MEDIUM - line counts are approximate; actual extraction may vary +-20% depending on how monitoring loop is handled

**Research date:** 2026-03-06
**Valid until:** 2026-04-06 (stable -- no external dependency changes expected)
