# Phase 3: Config Migration - Research

**Researched:** 2026-03-01
**Domain:** C/C++ global-to-struct configuration migration in a multi-threaded SIMD-optimized codebase
**Confidence:** HIGH

## Summary

Phase 3 eliminates all extern global variable declarations from `src/search/search_context.h` and the individual search module files by wiring `keyhunt_config_t*` through every search module's call chain. The config struct already exists (`src/config/config.h`) and is initialized in `main()` but not yet passed to any search thread. The migration guide (`MIGRATION_GUIDE.md`) documents all 55+ global-to-field mappings. The core technical challenge is threading the config pointer through the `tothread` struct into every thread function while preserving exact behavioral compatibility verified by the Phase 1 E2E tests.

There are 8 target files containing ~5,900 lines total that depend on ~94 extern declarations. The extern declarations come from two sources: `search_context.h` (the central extern header) and local `extern` declarations sprinkled inside individual `.cpp` files. Both must be eliminated. The variables fall into four categories: (1) read-only config flags already mapped to `keyhunt_config_t` fields, (2) shared mutable state (mutexes, bloom filters, data tables), (3) BSGS algorithm state (Int/Point values), and (4) infrastructure functions (`acquire_base_key`, `searchbinary`, `profile_set_thread`). Categories 1 and 2 map cleanly to existing config fields. Category 3 requires extending `keyhunt_config_t` with additional BSGS state fields or encapsulating them in a BSGS context struct passed alongside config. Category 4 requires function pointer or direct-include solutions.

**Primary recommendation:** Migrate module-by-module, starting with the simplest files (search_xpoint.cpp, search_rmd160.cpp -- already config-ready with no global access), then search_minikeys.cpp (isolated globals), then search_vanity.cpp, search_address.cpp (large, most globals), search_bsgs.cpp + search_bsgs_threads.cpp (BSGS-specific state), and finally io/io.cpp. After each module, run `make test && make test-e2e` to catch regressions immediately.

## Standard Stack

### Core

This phase does not introduce new libraries. It restructures existing code using only C/C++ language features.

| Component | Location | Purpose | Why Standard |
|-----------|----------|---------|--------------|
| `keyhunt_config_t` | `src/config/config.h` | Grouped configuration struct | Already designed and implemented; documented in MIGRATION_GUIDE.md |
| `thread_args` | `src/search/search_common.h:92-95` | Thread argument struct with config pointer | Already defined but unused |
| `kh_config_validate()` | `src/config/config.cpp:624` | Configuration validation | Already implemented with bounds checking |
| `kh_config_init()` | `src/config/config.cpp` | Zero-init with safe defaults | Already called in main() at line 1663 |

### Supporting

| Component | Location | Purpose | When to Use |
|-----------|----------|---------|-------------|
| `sort/sort.h` | `src/sort/sort.h` | `searchbinary()` declaration | Replace `extern int searchbinary(...)` in xpoint, rmd160, address modules |
| `bsgs/bsgs_sort.h` | `src/bsgs/bsgs_sort.h` | BSGS sort/search functions | Already included by search_bsgs.cpp |
| `io/io.h` | `src/io/io.h` | `writekey()`, `writekeyeth()` declarations | Already included by most modules |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Extending `keyhunt_config_t` with BSGS state | Separate `bsgs_context_t` struct | Keeps config struct read-only but adds a second pointer parameter; recommended for BSGS Int/Point state that is truly runtime-mutable |
| Passing config to every function | Using a thread-local config pointer | Avoids parameter threading but hides dependency; rejected -- explicit is better |
| Modifying `tothread` struct in-place | Creating new `thread_args` struct | `thread_args` already exists in search_common.h; use it and delete `tothread` |

## Architecture Patterns

### Recommended Migration Structure

```
src/
├── config/
│   ├── config.h           # keyhunt_config_t (freeze schema first - CFG-01)
│   └── config.cpp         # init, validate, print
├── search/
│   ├── search_common.h    # thread_args struct, shared declarations
│   ├── search_context.h   # EMPTY after migration (CFG-08)
│   ├── search_address.cpp # thread_process() accepts thread_args*
│   ├── search_bsgs.cpp    # helper funcs accept bsgs_context_t*
│   ├── search_bsgs_threads.cpp # BSGS threads accept thread_args*
│   ├── search_vanity.cpp  # thread_process_vanity() accepts thread_args*
│   ├── search_minikeys.cpp# thread_process_minikeys() accepts thread_args*
│   ├── search_xpoint.cpp  # Already config-ready (no globals)
│   └── search_rmd160.cpp  # Already config-ready (no globals)
├── io/
│   └── io.cpp             # Functions accept config* or typed parameters
└── keyhunt.cpp            # Populates config, passes via thread_args
```

### Pattern 1: Thread Function Migration

**What:** Replace `tothread*` with `thread_args*` in all thread entry points
**When to use:** Every thread creation site in keyhunt.cpp (lines 4020, 4407-4422)

**Before (current):**
```cpp
// keyhunt.cpp - thread creation
struct tothread *tt = (tothread*)malloc(sizeof(struct tothread));
tt->nt = j;
s = platform_thread_create(&tid[j], thread_process, (void *)tt);

// search_address.cpp - thread entry
void *thread_process(void *vargp) {
    struct tothread *tt = (struct tothread *)vargp;
    int thread_number = tt->nt;
    free(tt);
    // ... reads FLAGMODE, FLAGSEARCH, etc. from globals
}
```

**After (target):**
```cpp
// keyhunt.cpp - thread creation
thread_args *args = new thread_args{ &config, j };
s = platform_thread_create(&tid[j], thread_process, (void *)args);

// search_address.cpp - thread entry
platform_thread_return_t PLATFORM_THREAD_CALL thread_process(void *vargp) {
    thread_args *args = (thread_args *)vargp;
    keyhunt_config_t *config = args->config;
    int thread_number = args->thread_id;
    delete args;
    // ... reads config->search.mode, config->search.key_format, etc.
}
```

### Pattern 2: Config Flag Replacement

**What:** Replace FLAG* global reads with config struct field reads
**When to use:** Every search module function body

**Mapping (complete list from search module usage):**
```cpp
FLAGMODE           -> config->search.mode           (search_mode_t)
FLAGSEARCH         -> config->search.key_format     (key_format_t, SEARCH_COMPRESS=0)
FLAGCRYPTO         -> config->search.crypto_type    (crypto_type_t)
FLAGENDOMORPHISM   -> config->search.endomorphism   (bool)
FLAGRANDOM         -> config->search.random_mode    (bool)
FLAGQUIET          -> config->search.quiet_mode     (bool)
FLAGMATRIX         -> config->search.matrix_mode    (bool)
FLAGDEBUG          -> config->search.debug_mode     (bool)
FLAGBASEMINIKEY    -> (derived: config->search.mode == MODE_MINIKEYS)
FLAGBSGSMODE       -> config->bsgs.bsgs_mode       (bsgs_mode_t)
FLAGSAVEREADFILE   -> config->bsgs.save_progress    (bool)
FLAGSKIPCHECKSUM   -> (new field needed or deprecate)
FLAGVANITY         -> (derived: config->search.mode == MODE_VANITY)
FLAGREADEDFILE1-4  -> (BSGS cache state, module-internal)
KFACTOR            -> config->bsgs.k_factor         (int)
MAXLENGTHADDRESS   -> (new field needed in search_config_t or runtime_state_t)
FLAGGPU            -> config->gpu.enabled           (int)
FLAGGPU_FULL       -> config->gpu.full_mode         (bool)
FLAGGPU_HYBRID     -> (new field or use gpu.hybrid_mode)
```

**IMPORTANT TYPE MISMATCH:** The legacy globals use `int` (0/1) for boolean flags, but config uses `bool`. Migration must handle the comparison patterns:
- `if (FLAGRANDOM)` works with both int and bool -- safe
- `if (FLAGSEARCH == SEARCH_COMPRESS)` requires enum comparison -- config uses `key_format_t` enum, but SEARCH_COMPRESS=1 while KEYTYPE_COMPRESSED=0. **This is a critical mismatch that must be reconciled.**

### Pattern 3: Shared Mutable State via Config Runtime

**What:** Replace extern mutable globals with `config->runtime.*` fields
**When to use:** Mutexes, bloom filters, address tables, thread counters

**Key mappings:**
```cpp
secp              -> config->runtime.secp (need to add Secp256K1* field)
bloom             -> (bloom_extended_t*)(config->runtime.bloom_filter)
addressTable      -> (address_value*)(config->runtime.address_table)
N                 -> config->runtime.address_count (int64_t)
write_keys        -> (platform_mutex_t*)(config->runtime.write_mutex)
write_random      -> (platform_mutex_t*)(config->runtime.random_mutex)
steps             -> (thread_counter*)(config->runtime.thread_counters) [need to add]
ends              -> (thread_flag*)(config->runtime.thread_flags) [need to add]
THREADOUTPUT      -> (std::atomic<int>*)(config->runtime.thread_output) [need to add]
```

**Problem:** `runtime_state_t` stores most pointers as `void*`, requiring casts everywhere. The alternative is adding typed fields directly, but that requires `#ifdef __cplusplus` guards since config.h is C-compatible.

### Pattern 4: BSGS Context Struct (New)

**What:** Encapsulate BSGS algorithm state in a dedicated context struct
**When to use:** search_bsgs.cpp and search_bsgs_threads.cpp

The BSGS modules use ~30 additional globals (Int types, Point types, bloom filter arrays) that don't fit cleanly into `keyhunt_config_t`. These are **algorithm state**, not configuration. Creating a `bsgs_context_t` struct keeps the config struct focused on user-facing parameters.

```cpp
typedef struct {
    // Int parameters (C++ only)
    Int BSGS_CURRENT, BSGS_R, BSGS_AUX;
    Int BSGS_N, BSGS_N_double;
    Int BSGS_M, BSGS_M_double;
    Int BSGS_M2, BSGS_M2_double;
    Int BSGS_M3, BSGS_M3_double;

    // Point parameters
    Point BSGS_MP_double, BSGS_MP2_double, BSGS_MP3_double;
    std::vector<Point> BSGS_AMP2, BSGS_AMP3;
    std::vector<Point> OriginalPointsBSGS;
    bool *OriginalPointsBSGScompressed;
    std::vector<Point> GSn;
    Point _2GSn;

    // Found state
    std::atomic<int> *bsgs_found;

    // Bloom filters
    bloom_extended_t *bloom_bP;
    bloom_extended_t *bloom_bPx2nd;
    bloom_extended_t *bloom_bPx3rd;

    // Tables
    bsgs_xvalue *bPtable;

    // Mutexes
    platform_mutex_t *bloom_bP_mutex;
    platform_mutex_t *bloom_bPx2nd_mutex;
    platform_mutex_t *bloom_bPx3rd_mutex;
    platform_mutex_t *bPload_mutex;

    // Scalar parameters
    uint64_t bsgs_m, bsgs_m2, bsgs_m3, bsgs_aux;
    uint32_t bsgs_point_number;
    uint64_t BSGS_BUFFERXPOINTLENGTH;

    // Cache loading state
    int FLAGREADEDFILE1, FLAGREADEDFILE2, FLAGREADEDFILE3, FLAGREADEDFILE4;
} bsgs_context_t;
```

This struct would be stored in `keyhunt_config_t.runtime` as `void *bsgs_context` or added as a C++-only member.

### Pattern 5: Function-Level Dependencies (acquire_base_key, profile_set_thread)

**What:** Functions defined in keyhunt.cpp that search modules call via `extern`
**When to use:** search_address.cpp, search_vanity.cpp, search_minikeys.cpp, search_bsgs_threads.cpp

Three functions from keyhunt.cpp are called by search modules:
1. `acquire_base_key(Int &key)` -- used by search_address.cpp, search_vanity.cpp
2. `profile_set_thread(int idx)` -- used by all thread entry points
3. `searchbinary()` -- already in sort/sort.h (just include it)

**Options for acquire_base_key:**
- **Option A (recommended):** Move `acquire_base_key` to a shared file (e.g., `src/core/work_dispatch.cpp`) and pass the globals it reads (n_range_start, n_range_end, N_SEQUENTIAL_MAX, FLAGRANDOM, g_work_pool, g_workQueue) as parameters or through config.
- **Option B:** Pass as a function pointer in `thread_args`.
- **Option C:** Leave as extern declaration but move from search_context.h to a dedicated `work_dispatch.h` header.

**For profile_set_thread:**
- Move to a shared profiling header (`src/profiling.h`) or pass as function pointer in `thread_args`.

### Anti-Patterns to Avoid

- **Partial migration with dual reads:** Never read the same parameter from both a global AND config in the same code path. This creates subtle bugs where one gets updated but the other doesn't.
- **Config writes from search modules:** The config struct should be read-only from threads. Only `main()` and initialization code should write config fields.
- **Casting void* everywhere:** If a runtime field is always the same type, add a properly typed field to avoid cast errors.
- **Migrating definitions before declarations:** Always migrate the write site (keyhunt.cpp global definition -> config field population) BEFORE migrating the read site (search module extern -> config field read). Otherwise the config field has default/zero values.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Thread argument passing | Custom void* casting | `thread_args` struct (already defined) | Type-safe, includes config pointer |
| Config validation | Per-module bounds checking | `kh_config_validate()` (already implemented) | Centralized, tested, consistent |
| Enum-to-string conversion | sprintf with magic numbers | `kh_search_mode_name()` etc. (already implemented) | Type-safe, DRY |
| Binary search for addresses | extern declaration per file | `#include "sort/sort.h"` | Proper header already exists |
| BSGS sort functions | Duplicate extern declarations | `#include "bsgs/bsgs_sort.h"` | Proper header already exists |

**Key insight:** Most of the infrastructure for the migration already exists. The `keyhunt_config_t` struct, `thread_args` struct, init/validate functions, and name helpers are all implemented. The work is wiring them together and removing the old globals.

## Common Pitfalls

### Pitfall 1: SEARCH_COMPRESS vs KEYTYPE_COMPRESSED Enum Value Mismatch

**What goes wrong:** Legacy code uses `SEARCH_COMPRESS=1`, `SEARCH_UNCOMPRESS=0`, `SEARCH_BOTH=2`. The new `key_type_t` enum uses `KEYTYPE_COMPRESSED=0`, `KEYTYPE_UNCOMPRESSED=1`, `KEYTYPE_BOTH=2`. The meaning of 0 and 1 is **swapped**.

**Why it happens:** The enums were defined independently in cli.h (new) and search_common.h (legacy).

**How to avoid:** When migrating `FLAGSEARCH` to `config->search.key_format`:
1. Audit EVERY comparison: `if (FLAGSEARCH == SEARCH_COMPRESS)` must become `if (config->search.key_format == KEY_COMPRESSED)` -- NOT a direct value substitution.
2. The keyhunt.cpp code that sets `FLAGSEARCH` from CLI args must also populate `config->search.key_format` with the correct enum value.
3. Add a compile-time assertion that SEARCH_COMPRESS != KEYTYPE_COMPRESSED to make this mismatch visible.

**Warning signs:** Tests pass for "both" mode (value 2 is same) but fail for compressed-only or uncompressed-only modes.

### Pitfall 2: Write Site Before Read Site

**What goes wrong:** A search module reads `config->search.mode` but keyhunt.cpp hasn't populated it yet (still using `FLAGMODE`). The config field has the default value (MODE_ADDRESS=1), causing silent mode mismatches.

**Why it happens:** Migration is done module-by-module, and developers start with read sites (easier) instead of write sites (requires understanding keyhunt.cpp).

**How to avoid:** For EVERY config field migration:
1. First: Add the write site in keyhunt.cpp (e.g., `config.search.mode = (search_mode_t)FLAGMODE;`)
2. Second: Run `kh_config_validate(&config)` after all writes
3. Third: Update read sites in search modules
4. Run E2E tests after each field migration

**Warning signs:** E2E tests find wrong private key or no key at all; ADDRESS mode test passes but BSGS fails.

### Pitfall 3: Thread Lifetime of Config and Arguments

**What goes wrong:** `thread_args` is allocated on the stack or freed too early; thread reads garbage.

**Why it happens:** Current pattern allocates `tothread` with malloc and thread frees it immediately after copying. If `thread_args.config` points to a stack-local config, all threads share the same valid pointer. But if config is freed before threads finish, undefined behavior.

**How to avoid:**
1. `keyhunt_config_t config` must be declared in `main()` scope (or static), NOT in a sub-function
2. `thread_args` must be heap-allocated (one per thread) since threads free it after extracting the config pointer
3. The `config` pointer itself is NOT freed by threads -- they just copy it

**Warning signs:** TSan reports, sporadic crashes, different results across runs.

### Pitfall 4: Mutable State in "Configuration" Struct

**What goes wrong:** Multiple threads write to `config->runtime.*` fields (counters, flags) without synchronization. Data races appear.

**Why it happens:** `runtime_state_t` contains mutable counters alongside read-only config. Moving a global into config doesn't automatically make it thread-safe.

**How to avoid:**
1. Fields that are truly read-only after init (flags, mode, parameters) -> put in config
2. Fields that are written by multiple threads (counters, found flags) -> keep as externally-managed atomics or protected by mutexes
3. Run `make tsan` after migration to verify

**Warning signs:** TSan findings that didn't exist before migration.

### Pitfall 5: search_minikeys.cpp Include Conflict

**What goes wrong:** search_minikeys.cpp already notes it "cannot include search_context.h because its #define MODE_* macros conflict with the search_mode_t enum in cli.h". It uses local extern declarations instead.

**Why it happens:** `search_context.h` uses `#define MODE_XPOINT 0` etc., which conflicts with the `typedef enum { MODE_XPOINT = 0 ... } search_mode_t` in cli.h that config.h transitively includes.

**How to avoid:** After migration, search_context.h will be empty (CFG-08), so this conflict disappears naturally. But during migration, search_minikeys.cpp should continue using its local extern pattern and migrate directly to config reads without ever including search_context.h.

**Warning signs:** Compile errors about macro/enum conflicts when including both headers.

### Pitfall 6: void* Casting for Typed Data in runtime_state_t

**What goes wrong:** `runtime_state_t` stores bloom filter, address table, and mutexes as `void*`. Every access requires a cast like `(bloom_extended_t*)(config->runtime.bloom_filter)`. A wrong cast type silently corrupts data.

**Why it happens:** `runtime_state_t` was designed for C compatibility, so it uses `void*` instead of C++-specific types.

**How to avoid:**
1. Add typed accessor functions: `bloom_extended_t* kh_get_bloom(keyhunt_config_t *config)`
2. Or use `#ifdef __cplusplus` to provide typed fields in the struct
3. Or accept the casts but use inline helpers to centralize them

**Warning signs:** Silent data corruption; crashes deep in bloom filter code.

## Code Examples

### Example 1: Complete Module Migration (search_minikeys.cpp)

This is the simplest non-trivial migration because minikeys has few globals and no search_context.h include.

**Current state (extern declarations in search_minikeys.cpp lines 88-103):**
```cpp
extern Secp256K1 *secp;
extern int FLAGBASEMINIKEY;
extern int FLAGRANDOM;
extern int FLAGMATRIX;
extern int FLAGQUIET;
extern uint64_t N;
extern uint64_t N_SEQUENTIAL_MAX;
extern struct thread_counter *steps;
extern struct address_value *addressTable;
extern bloom_extended_t bloom;
extern platform_mutex_t write_keys;
extern platform_mutex_t write_random;
extern char *raw_baseminikey;
extern char *Ccoinbuffer;
extern char *minikeyN;
extern int minikey_n_limit;
```

**After migration:**
```cpp
#include "search_common.h"
// #include "search_context.h"  -- REMOVED
#include "../config/config.h"

platform_thread_return_t PLATFORM_THREAD_CALL thread_process_minikeys(void *vargp) {
    thread_args *args = (thread_args *)vargp;
    keyhunt_config_t *config = args->config;
    int thread_number = args->thread_id;
    delete args;

    // Config reads (immutable after init)
    bool is_base = (config->search.mode == MODE_MINIKEYS);  // was FLAGBASEMINIKEY
    bool is_random = config->search.random_mode;              // was FLAGRANDOM
    bool is_quiet = config->search.quiet_mode;                // was FLAGQUIET
    bool is_matrix = config->search.matrix_mode;              // was FLAGMATRIX

    // Runtime state reads (set once at init, read-only from threads)
    Secp256K1 *secp = (Secp256K1 *)config->runtime.secp;    // need to add field
    bloom_extended_t *bloom = (bloom_extended_t *)config->runtime.bloom_filter;
    address_value *addressTable = (address_value *)config->runtime.address_table;
    int64_t N = config->runtime.address_count;
    platform_mutex_t *write_keys = (platform_mutex_t *)config->runtime.write_mutex;
    platform_mutex_t *write_random = (platform_mutex_t *)config->runtime.random_mutex;
    // Minikey-specific state (need to add to runtime_state_t)
    // ...
}
```

### Example 2: keyhunt.cpp Write Site Population

**Before (current -- config is initialized but not populated):**
```cpp
keyhunt_config_t config;
kh_config_init(&config);
// ... config sits unused while FLAGMODE, FLAGSEARCH, etc. are set from CLI args
```

**After (config populated from parsed globals):**
```cpp
keyhunt_config_t config;
kh_config_init(&config);
// ... parse CLI args into globals (existing code, unchanged) ...

// Populate config from globals (bridge layer)
config.search.mode = (search_mode_t)FLAGMODE;
config.search.key_format = flagsearch_to_key_format(FLAGSEARCH);  // handles value swap
config.search.crypto_type = (crypto_type_t)FLAGCRYPTO;
config.search.endomorphism = (FLAGENDOMORPHISM != 0);
config.search.random_mode = (FLAGRANDOM != 0);
config.search.quiet_mode = (FLAGQUIET != 0);
config.search.matrix_mode = (FLAGMATRIX != 0);
config.search.debug_mode = (FLAGDEBUG != 0);
config.bsgs.k_factor = KFACTOR;
config.bsgs.bsgs_mode = (bsgs_mode_t)FLAGBSGSMODE;
config.bsgs.save_progress = (FLAGSAVEREADFILE != 0);
config.runtime.num_threads = NTHREADS;

// Runtime mutable state (pointers set after allocation)
config.runtime.bloom_filter = &bloom;
config.runtime.address_table = addressTable;
config.runtime.address_count = (int64_t)N;
config.runtime.write_mutex = &write_keys;
config.runtime.random_mutex = &write_random;

kh_config_validate(&config);  // CFG-09 success criterion
```

### Example 3: FLAGSEARCH to key_format_t Conversion

```cpp
// Conversion helper (handles the value swap)
static key_format_t flagsearch_to_key_format(int flagsearch) {
    switch (flagsearch) {
        case 0: return KEYTYPE_UNCOMPRESSED;  // SEARCH_UNCOMPRESS=0 -> KEYTYPE_UNCOMPRESSED=1
        case 1: return KEYTYPE_COMPRESSED;    // SEARCH_COMPRESS=1 -> KEYTYPE_COMPRESSED=0
        case 2: return KEYTYPE_BOTH;          // SEARCH_BOTH=2 -> KEYTYPE_BOTH=2
        default: return KEYTYPE_BOTH;
    }
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| 50+ scattered globals | `keyhunt_config_t` struct | Feb 2026 (designed) | Struct exists but is not wired into search modules |
| `tothread` struct (nt, rs, rpt) | `thread_args` struct (config*, thread_id) | Feb 2026 (designed) | Defined in search_common.h:92 but unused |
| `#include "search_context.h"` for all externs | Direct `#include` of config.h | This phase | search_context.h becomes empty |
| `int` flags (0/1) | `bool` / typed enums | This phase | Type safety improvement |

**Deprecated after this phase:**
- `search_context.h` extern declarations (CFG-08: zero remaining)
- `struct tothread` (replaced by `thread_args`)
- SEARCH_COMPRESS/SEARCH_UNCOMPRESS macros (replaced by key_type_t enum)

## Open Questions

1. **FLAGSEARCH enum value mismatch**
   - What we know: SEARCH_COMPRESS=1 but KEYTYPE_COMPRESSED=0. Values are swapped.
   - What's unclear: Whether to fix the cli.h enum to match legacy values (breaking change) or add a conversion function (extra complexity).
   - Recommendation: Add a conversion function (`flagsearch_to_key_format`) -- changing cli.h would affect the wizard, benchmark, and other modules that already use the new enum.

2. **BSGS state: extend config or separate context?**
   - What we know: ~30 BSGS-specific globals (Int/Point types) need to be passed to search_bsgs*.cpp. They don't fit conceptually in "configuration".
   - What's unclear: Whether to add a `bsgs_context_t*` pointer to `thread_args` or to `runtime_state_t.bsgs_context`.
   - Recommendation: Add a `void *bsgs_context` field to `runtime_state_t` -- keeps the config struct focused and allows BSGS context to be allocated only when BSGS mode is active.

3. **acquire_base_key location**
   - What we know: Defined in keyhunt.cpp, used by search_address.cpp and search_vanity.cpp. Reads n_range_start, n_range_end, FLAGRANDOM, g_work_pool, g_workQueue.
   - What's unclear: Whether to move it to a shared file or keep it in keyhunt.cpp with an extern declaration.
   - Recommendation: Keep in keyhunt.cpp for now (Phase 4 will decompose keyhunt.cpp). For Phase 3, declare in a header (`src/core/work_dispatch.h`) and include that header instead of using bare extern. The function itself can read from config internally.

4. **Fields missing from keyhunt_config_t**
   - `secp` (Secp256K1*) -- not in runtime_state_t
   - `steps` (thread_counter*) -- not in runtime_state_t
   - `ends` (thread_flag*) -- not in runtime_state_t
   - `THREADOUTPUT` (atomic<int>) -- not in runtime_state_t
   - `g_avx2_available` (bool) -- should map to autotune.has_avx2
   - `N_SEQUENTIAL_MAX` (uint64_t) -- constant, could be a #define
   - `MAXLENGTHADDRESS` (int) -- not in any config struct
   - `FLAGSKIPCHECKSUM` (int) -- not mapped
   - `Gn`, `_2Gn` (vector<Point>, Point) -- generator points, not in config
   - `lambda`, `lambda2`, `beta`, `beta2` (Int) -- endomorphism constants
   - Minikey state: `Ccoinbuffer`, `raw_baseminikey`, `minikeyN`, `minikey_n_limit`
   - Vanity state: `vanity_rmd_*` variables
   - Recommendation: Add missing fields to `runtime_state_t` using `void*` with typed accessor macros. Constants like `N_SEQUENTIAL_MAX` should be `#define` in a shared header.

5. **CFG-01: Schema freeze boundary**
   - What we know: CFG-01 says "freeze keyhunt_config_t struct schema" before migration begins.
   - What's unclear: How strict the freeze is -- can we add new fields during migration?
   - Recommendation: Freeze means no removing/renaming existing fields. Adding new fields (for missing globals) is acceptable and necessary. Document all additions.

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| CFG-01 | Freeze keyhunt_config_t struct schema (no mid-migration shape changes) | Schema is documented in config.h; ~15 fields need to be added for globals not yet mapped (secp, steps, ends, etc.). Add all missing fields FIRST, then freeze. |
| CFG-02 | Wire config into search_address.cpp (eliminate extern globals) | search_address.cpp has ~14 extern declarations; mapping documented in Pattern 2 and Example 1. Most complex file (1130 lines, uses acquire_base_key, GPU flags, profiling). |
| CFG-03 | Wire config into search_bsgs.cpp and search_bsgs_threads.cpp | search_bsgs.cpp has 0 local externs (uses search_context.h); search_bsgs_threads.cpp has ~20 local externs. BSGS context pattern (Pattern 4) needed for ~30 Int/Point state variables. |
| CFG-04 | Wire config into search_vanity.cpp | search_vanity.cpp has ~9 local externs plus search_context.h. Moderate complexity; uses vanity-specific state that needs runtime_state_t fields. |
| CFG-05 | Wire config into search_minikeys.cpp | search_minikeys.cpp has ~16 local externs, no search_context.h. Simplest non-trivial migration; good starting point. See Example 1. |
| CFG-06 | Wire config into search_xpoint.cpp and search_rmd160.cpp | Both files are ALREADY config-ready -- they accept all data as function parameters and only declare `extern searchbinary()`. Replace extern with `#include "sort/sort.h"`. Trivial. |
| CFG-07 | Wire config into io/io.cpp | io.cpp has 2 local externs (g_rangeProgressStart/End) plus reads from search_context.h globals (FLAGMODE, FLAGCRYPTO, FLAGSAVEREADFILE, etc.). Functions need config parameter or should receive typed parameters. |
| CFG-08 | Eliminate search_context.h extern declarations (target: 0 remaining) | search_context.h has ~94 extern declarations. After all modules are migrated, delete all extern lines. File can remain with struct definitions and includes only. |
| CFG-09 | All search modules accept keyhunt_config_t* as parameter, no global reads | Verified by: (1) compiling without search_context.h, (2) kh_config_validate() call in test setup, (3) E2E tests still pass. |
</phase_requirements>

## Sources

### Primary (HIGH confidence)

- `src/config/config.h` -- full keyhunt_config_t struct definition, 517 lines, version 2
- `src/config/config.cpp` -- init, validate, print implementations, kh_config_validate at line 624
- `src/search/search_context.h` -- all 94 extern declarations that must be eliminated
- `src/search/search_common.h` -- thread_args struct already defined at line 92
- `MIGRATION_GUIDE.md` -- complete 55-variable mapping from globals to config fields
- Each search module file (examined for exact extern declarations and usage patterns)

### Secondary (MEDIUM confidence)

- STATE.md accumulated decisions (FLAGSEARCH enum mismatch inferred from code analysis, not explicitly documented as a concern)
- Memory model assumptions for runtime_state_t thread safety (based on Phase 2 TSan work)

### Tertiary (LOW confidence)

- Performance impact of config pointer indirection vs global access (claimed zero-cost in MIGRATION_GUIDE.md but not benchmarked)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all components already exist in the codebase, just need wiring
- Architecture: HIGH -- migration patterns are mechanical and well-documented in MIGRATION_GUIDE.md
- Pitfalls: HIGH -- identified from direct code analysis of enum mismatches, include conflicts, and void* typing

**Research date:** 2026-03-01
**Valid until:** 2026-04-01 (stable codebase, no external dependencies)
