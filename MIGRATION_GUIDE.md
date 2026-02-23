# Global Variables to Configuration Structures Migration Guide

## Overview

This guide documents the mapping of 50+ global variables to the new structured configuration system (`src/config/config.h`). The new system organizes configuration into logical groups for better maintainability, testability, and thread-safety.

## Why Migrate?

The legacy codebase uses global variables with inconsistent naming conventions:
- **ALLCAPS**: `FLAGDEBUG`, `NTHREADS`, `FLAGQUIET`
- **lowercase**: `bsgs_m`, `bitrange`
- **g_ prefix**: `g_avx2_available`, `g_rangeProgressEnabled`

This creates:
- ❌ Hidden dependencies between functions
- ❌ Difficult unit testing (can't isolate state)
- ❌ Thread-safety issues
- ❌ Hard to understand code flow

The new configuration system provides:
- ✅ Grouped, logical organization
- ✅ Explicit dependency injection
- ✅ Clear ownership and lifetime
- ✅ Self-documenting structure
- ✅ Type-safe enums instead of magic numbers

## Configuration Structure Hierarchy

```
keyhunt_config_t (top-level container)
├── search_config_t       - Search mode, range, flags
├── bsgs_config_t         - BSGS algorithm parameters
├── gpu_config_t          - GPU acceleration settings
├── autotune_config_t     - Auto-detected system info
└── runtime_state_t       - Mutable execution state
```

---

## Global Variable Mappings

### 1. Search Configuration (`search_config_t`)

Search parameters and operational modes.

| Legacy Global Variable | New Config Field | Type | Description |
|------------------------|------------------|------|-------------|
| `FLAGMODE` | `search.mode` | `search_mode_t` | Search algorithm mode (ADDRESS/BSGS/XPOINT/RMD160/VANITY/PUB2RMD/MINIKEYS) |
| `FLAGSEARCH` | `search.key_format` | `key_format_t` | Key compression type (COMPRESSED/UNCOMPRESSED/BOTH) |
| `FLAGCRYPTO` | `search.crypto_type` | `crypto_type_t` | Cryptocurrency type (BTC/ETH/ALL) |
| `range_start` | `search.range_start` | `char[128]` | Start of search range (hex string) |
| `range_end` | `search.range_end` | `char[128]` | End of search range (hex string) |
| `bitrange` | `search.bit_range` | `int` | Bit range for puzzle mode (0 = not set) |
| `FLAGBITRANGE` | `search.bit_range != 0` | derived | Whether bit range is specified |
| `str_stride` | `search.stride` | `char[128]` | Custom stride value (hex string) |
| `FLAGSTRIDE` | `search.stride_enabled` | `bool` | Use custom stride |
| `FLAGFILE` | `search.target_file[0] != 0` | derived | Target file was specified |
| `FLAGRANDOM` | `search.random_mode` | `bool` | Random key generation mode |
| `FLAGENDOMORPHISM` | `search.endomorphism` | `bool` | Use endomorphism optimization |
| `FLAGQUIET` | `search.quiet_mode` | `bool` | Minimal output |
| `FLAGDEBUG` | `search.debug_mode` | `bool` | Debug output |
| `FLAGMATRIX` | `search.matrix_mode` | `bool` | Matrix display mode |
| `FLAGPROGRESSBAR` | `search.progress_bar` | `bool` | Show progress bar |

**Example Migration:**
```cpp
// OLD
if (FLAGMODE == MODE_BSGS) {
    // BSGS-specific code
}
if (FLAGRANDOM) {
    // Random mode code
}

// NEW
if (config->search.mode == SEARCH_MODE_BSGS) {
    // BSGS-specific code
}
if (config->search.random_mode) {
    // Random mode code
}
```

---

### 2. BSGS Configuration (`bsgs_config_t`)

Baby Step Giant Step algorithm parameters.

| Legacy Global Variable | New Config Field | Type | Description |
|------------------------|------------------|------|-------------|
| `bsgs_m` | `bsgs.m_value` | `uint64_t` | M value (sqrt of N, baby steps count) |
| `bsgs_m2` | `bsgs.m2_value` | `uint64_t` | M / 32 (derived) |
| `bsgs_m3` | `bsgs.m3_value` | `uint64_t` | M2 / 32 (derived) |
| `str_N` | computed | derived | N value is calculated from m_value |
| `FLAG_N` | `bsgs.n_value != 0` | derived | N was explicitly set |
| `KFACTOR` | `bsgs.k_factor` | `int` | K multiplication factor |
| `FLAGBLOOMMULTIPLIER` | `bsgs.bloom_multiplier` | `int` | Bloom filter size multiplier |
| `bloom_bP_totalbytes` | `bsgs.bloom_bp_bytes` | `uint64_t` | Primary bloom filter bytes |
| `bloom_bP2_totalbytes` | `bsgs.bloom_bp2_bytes` | `uint64_t` | Secondary bloom filter bytes |
| `bloom_bP3_totalbytes` | `bsgs.bloom_bp3_bytes` | `uint64_t` | Tertiary bloom filter bytes |
| `FLAGBSGSMODE` | `bsgs.bsgs_mode` | `bsgs_mode_t` | BSGS search mode (SEQUENTIAL/BACKWARD/BOTH/RANDOM/DANCE) |
| `FLAGSAVEREADFILE` | `bsgs.save_progress` | `bool` | Save bloom/bP tables to disk |
| `FLAGPRECALCUTED_P_FILE` | `bsgs.load_precalc` | `bool` | Load precalculated files |
| `bsgs_aux` | `bsgs.aux_value` | `uint64_t` | Auxiliary computation value |
| `bsgs_point_number` | `bsgs.point_number` | `uint32_t` | Number of target points |

**Example Migration:**
```cpp
// OLD
if (bsgs_m > available_memory / BSGS_MULTIPLIER) {
    fprintf(stderr, "Error: Not enough memory\n");
}

// NEW
if (kh_bsgs_config_validate_memory(&config->bsgs, available_ram) != 0) {
    fprintf(stderr, "Error: Not enough memory\n");
}
```

---

### 3. GPU Configuration (`gpu_config_t`)

GPU acceleration settings.

| Legacy Global Variable | New Config Field | Type | Description |
|------------------------|------------------|------|-------------|
| `FLAGGPU` | `gpu.enabled` | `int` | GPU mode (0=off, 1=on, -1=auto) |
| `FLAGGPU_FULL` | `gpu.full_mode` | `bool` | Full ECC+hash+match on GPU |
| `FLAGGPU_HYBRID` | `gpu.hybrid_mode` | `bool` | Run GPU+CPU in parallel |
| N/A | `gpu.device_ids` | `int[8]` | Array of GPU device IDs to use |
| N/A | `gpu.device_count` | `int` | Number of selected GPUs |
| N/A | `gpu.range_percent` | `int` | GPU gets this % of range (default 80) |
| N/A | `gpu.blocks_per_sm` | `int` | CUDA blocks per SM |
| N/A | `gpu.threads_per_block` | `int` | CUDA threads per block |
| N/A | `gpu.keys_per_thread` | `int` | Keys processed per GPU thread |
| N/A | `gpu.keys_checked` | `volatile uint64_t` | Total GPU keys checked |
| N/A | `gpu.keys_checked_cur` | `volatile uint64_t` | Current block keys |
| N/A | `gpu.should_stop` | `volatile int` | Signal GPU to stop |
| N/A | `gpu.bloom_uploaded` | `bool` | GPU-side bloom filter ready |

**Example Migration:**
```cpp
// OLD
if (FLAGGPU && FLAGGPU_HYBRID) {
    // Start GPU in hybrid mode
}

// NEW
if (config->gpu.enabled && config->gpu.hybrid_mode) {
    // Start GPU in hybrid mode
}
```

---

### 4. Runtime State (`runtime_state_t`)

Mutable execution state (counters, handles, target data).

| Legacy Global Variable | New Config Field | Type | Description |
|------------------------|------------------|------|-------------|
| `NTHREADS` | `runtime.num_threads` | `int` | Number of CPU threads |
| `tid` | `runtime.thread_handles` | `void*` | Platform-specific thread handles |
| `FINISHED_THREADS_COUNTER` | `runtime.finished_threads` | `uint64_t` | Number of completed threads |
| `THREADCYCLES` | `runtime.thread_cycles` | `uint64_t` | Total thread cycles |
| `THREADCOUNTER` | `runtime.thread_counter` | `uint64_t` | Shared counter |
| `FINISHED_ITEMS` | `runtime.finished_items` | `uint64_t` | Keys processed (atomic) |
| `OLDFINISHED_ITEMS` | `runtime.old_finished_items` | `uint64_t` | Previous value for rate calculation |
| N/A | `runtime.start_time_ms` | `uint64_t` | Search start time (milliseconds) |
| N/A | `runtime.last_output_time` | `uint64_t` | Last status output time |
| `OUTPUTSECONDS` | `runtime.output_interval_sec` | `int` | Seconds between status updates |
| `addressTable` | `runtime.address_table` | `void*` | Sorted address table |
| N/A | `runtime.address_count` | `int64_t` | Number of target addresses |
| `bloom` | `runtime.bloom_filter` | `void*` | Main bloom filter |
| `vanity_rmd_targets` | `runtime.vanity_targets` | `int` | Number of vanity targets |
| `vanity_rmd_total` | `runtime.vanity_total` | `int` | Total vanity patterns |
| `vanity_bloom` | `runtime.vanity_bloom` | `void*` | Vanity-specific bloom filter |
| `bPtable` | `runtime.bsgs_bp_table` | `void*` | Baby step point table |
| `bloom_bP` | `runtime.bsgs_bloom_bp` | `void*` | BSGS bloom filter array |
| `bloom_bPx2nd` | `runtime.bsgs_bloom_bp2` | `void*` | Second level bloom |
| `bloom_bPx3rd` | `runtime.bsgs_bloom_bp3` | `void*` | Third level bloom |
| `write_keys` | `runtime.write_mutex` | `void*` | Mutex for file writes |
| `write_random` | `runtime.random_mutex` | `void*` | Mutex for random generation |
| `bsgs_thread` | `runtime.bsgs_mutex` | `void*` | BSGS-specific mutex |
| `g_workQueue` | `runtime.work_pool` | `void*` | Work-stealing pool handle |
| N/A | `runtime.work_pool_enabled` | `bool` | Work pool is active |
| N/A | `runtime.work_pool_exhausted` | `bool` | All work distributed |
| `bsgs_found` | `runtime.keys_found` | `int` | Total keys found this session |
| N/A | `runtime.output_file` | `char[512]` | Path to output file |

**Example Migration:**
```cpp
// OLD
#ifdef _WIN64
WaitForSingleObject(write_keys, INFINITE);
#else
pthread_mutex_lock(&write_keys);
#endif

// NEW
// Abstracted via config helper functions
kh_runtime_lock_write_mutex(&config->runtime);
// ... critical section
kh_runtime_unlock_write_mutex(&config->runtime);
```

---

### 5. Auto-Tune Configuration (`autotune_config_t`)

System-detected parameters (CPU, memory, optimal settings).

| Legacy Global Variable | New Config Field | Type | Description |
|------------------------|------------------|------|-------------|
| N/A (sysinfo) | `autotune.cpu_physical_cores` | `int` | Physical CPU cores |
| N/A (sysinfo) | `autotune.cpu_logical_cores` | `int` | Logical cores (with HT) |
| `g_avx2_available` | `autotune.has_avx2` | `bool` | AVX2 SIMD available |
| N/A (runtime detect) | `autotune.has_avx512` | `bool` | AVX-512 available |
| N/A (runtime detect) | `autotune.has_sha_ni` | `bool` | SHA-NI instructions |
| N/A (sysinfo) | `autotune.total_ram_bytes` | `uint64_t` | Total system RAM |
| N/A (sysinfo) | `autotune.available_ram_bytes` | `uint64_t` | Available RAM |
| N/A | `autotune.memory_limit_pct` | `int` | % of RAM to use (default 75) |
| `OPTIMAL_THREADS` | `autotune.optimal_threads` | `int` | Recommended thread count |
| `OPTIMAL_N` | `autotune.optimal_n` | `uint64_t` | Recommended N value |
| `OPTIMAL_KFACTOR` | `autotune.optimal_k_factor` | `int` | Recommended K factor |
| N/A | `autotune.optimal_batch_size` | `uint32_t` | Recommended batch size (1024) |

**Example Migration:**
```cpp
// OLD
if (g_avx2_available) {
    use_avx2_hash();
}

// NEW
if (config->autotune.has_avx2) {
    use_avx2_hash();
}
```

---

### 6. Additional Global Variables

Variables that need special handling or are being deprecated.

#### 6.1 Flags (Boolean States)

| Legacy Global Variable | New Location | Type | Notes |
|------------------------|--------------|------|-------|
| `FLAGSKIPCHECKSUM` | **DEPRECATED** | `int` | Checksum validation (consider removing) |
| `FLAGVANITY` | `search.mode == MODE_VANITY` | derived | Derived from mode |
| `FLAGBASEMINIKEY` | `search.mode == MODE_MINIKEYS` | derived | Derived from mode |
| `FLAGRANGE` | `search.range_start[0] != 0` | derived | Range was specified |
| `FLAGRAWDATA` | **DEPRECATED** | `int` | Raw data mode (consider removing) |
| `FLAGTHREADS` | `runtime.num_threads > 1` | derived | Multiple threads active |

#### 6.2 File I/O Flags (BSGS Caching)

| Legacy Global Variable | New Location | Type | Notes |
|------------------------|--------------|------|-------|
| `FLAGREADEDFILE1` | **Internal** | `int` | Bloom 1 file loaded (internal state) |
| `FLAGREADEDFILE2` | **Internal** | `int` | Bloom 2 file loaded (internal state) |
| `FLAGREADEDFILE3` | **Internal** | `int` | Bloom 3 file loaded (internal state) |
| `FLAGREADEDFILE4` | **Internal** | `int` | bP table file loaded (internal state) |
| `FLAGUPDATEFILE1` | **Internal** | `int` | Bloom 1 needs update (internal state) |

**Note:** These should become internal state in the BSGS module, not part of public config.

#### 6.3 Temporary/Computation Variables

These are NOT configuration and should remain as local or module-scoped variables:

| Legacy Global Variable | Recommendation | Reason |
|------------------------|----------------|--------|
| `BSGS_*` (Int/Point) | Module-scoped | Temporary computation values |
| `n_range_*` (Int) | Module-scoped | Range computation intermediates |
| `lambda`, `beta` | Module-scoped | Endomorphism constants |
| `secp` | Module-scoped | Secp256k1 context (singleton OK) |
| `Gn`, `GSn` (vectors) | Module-scoped | Precomputed point tables |
| `stride` (Int) | Module-scoped | Computed from string |
| `ONE`, `ZERO`, `MPZAUX` | Module-scoped | Math constants |

#### 6.4 Range Progress Tracking

| Legacy Global Variable | New Location | Type | Notes |
|------------------------|--------------|------|-------|
| `g_rangeProgressEnabled` | **Internal** | `bool` | Progress tracking enabled |
| `g_rangeProgressStart` | **Internal** | `Int` | Range start for progress |
| `g_rangeProgressEnd` | **Internal** | `Int` | Range end for progress |
| `g_rangeProgressSpan` | **Internal** | `Int` | Range span for percentage |

**Note:** These should be encapsulated in a progress tracking module.

#### 6.5 Vanity Mode Specific

| Legacy Global Variable | New Location | Type | Notes |
|------------------------|--------------|------|-------|
| `vanity_rmd_limits` | `runtime.*` | `int*` | Already in runtime_state_t |
| `vanity_rmd_limit_values_A/B` | `runtime.*` | `uint8_t***` | Complex vanity state |
| `vanity_rmd_minimun_bytes_check_length` | `runtime.*` | `int` | Vanity optimization |
| `vanity_address_targets` | `runtime.*` | `char**` | Vanity target list |

#### 6.6 Minikeys Mode Specific

| Legacy Global Variable | New Location | Type | Notes |
|------------------------|--------------|------|-------|
| `Ccoinbuffer` | **Module-scoped** | `char*` | Base58 character set |
| `str_baseminikey` | **Module-scoped** | `char*` | Minikey base string |
| `raw_baseminikey` | **Module-scoped** | `char*` | Minikey raw buffer |
| `minikeyN` | **Module-scoped** | `char*` | Minikey N value |
| `minikey_n_limit` | **Module-scoped** | `int` | Minikey N limit |

#### 6.7 Thread Management (Legacy)

| Legacy Global Variable | New Location | Type | Notes |
|------------------------|--------------|------|-------|
| `steps` | **Deprecated** | `struct thread_counter*` | Old thread work distribution |
| `ends` | **Deprecated** | `struct thread_flag*` | Old thread completion flags |
| `FINISHED_THREADS_BP` | `runtime.finished_threads` | derived | BSGS thread counter |
| `THREADOUTPUT` | **Deprecated** | `int` | Output control flag |

#### 6.8 Checksums and Buffers

| Legacy Global Variable | New Location | Type | Notes |
|------------------------|--------------|------|-------|
| `checksum` | **Internal** | `char[32]` | Bloom filter checksum (local) |
| `checksum_backup` | **Internal** | `char[32]` | Checksum backup (local) |
| `buffer_bloom_file` | **Internal** | `char[1024]` | File path buffer (local) |
| `bloom_bP_checksums` | **Internal** | `struct*` | Bloom checksums (internal) |
| `bloom_bPx2nd_checksums` | **Internal** | `struct*` | Bloom 2 checksums (internal) |
| `bloom_bPx3rd_checksums` | **Internal** | `struct*` | Bloom 3 checksums (internal) |

#### 6.9 BSGS Data Structures

| Legacy Global Variable | New Location | Type | Notes |
|------------------------|--------------|------|-------|
| `OriginalPointsBSGS` | **Module-scoped** | `std::vector<Point>` | Target points for BSGS |
| `OriginalPointsBSGScompressed` | **Module-scoped** | `bool*` | Compression flags |
| `oldbloom_bP` | **Internal** | `struct` | Old bloom implementation |
| `bloom_bP_mutex` | `runtime.*` | `pthread_mutex_t*` | Already abstracted |
| `bloom_bPx2nd_mutex` | `runtime.*` | `pthread_mutex_t*` | Already abstracted |
| `bloom_bPx3rd_mutex` | `runtime.*` | `pthread_mutex_t*` | Already abstracted |

#### 6.10 Constants and Limits

| Legacy Global Variable | Type | Notes |
|------------------------|------|-------|
| `THREADBPWORKLOAD` | `uint32_t` | Constant (1048576) - consider making configurable |
| `N_SEQUENTIAL_MAX` | `uint64_t` | Constant (0x100000000) - keep as constant |
| `DEBUGCOUNT` | `uint64_t` | Constant (0x400) - debug output frequency |
| `BSGS_XVALUE_RAM` | `uint64_t` | Constant (8 bytes) - memory calculation |
| `BSGS_BUFFERXPOINTLENGTH` | `uint64_t` | Constant (16 bytes) - buffer size |
| `BSGS_BUFFERREGISTERLENGTH` | `uint64_t` | Constant (36 bytes) - register size |
| `MAXLENGTHADDRESS` | `int` | Config or constant (-1 = unlimited) |

#### 6.11 String Arrays (Display)

| Legacy Global Variable | Type | Notes |
|------------------------|------|-------|
| `bsgs_modes[5]` | `const char*` | Use `kh_bsgs_mode_name()` helper |
| `modes[7]` | `const char*` | Use `kh_search_mode_name()` helper |
| `cryptos[3]` | `const char*` | Use `kh_crypto_type_name()` helper |
| `publicsearch[3]` | `const char*` | Use `kh_key_format_name()` helper |
| `str_limits_prefixs[7]` | `const char*` | Rate display prefixes |
| `str_limits[7]` | `const char*` | Rate display thresholds |
| `int_limits[7]` | `Int` | Computed rate thresholds |

#### 6.12 Miscellaneous

| Legacy Global Variable | New Location | Type | Notes |
|------------------------|--------------|------|-------|
| `bytes` | **Local** | `uint64_t` | Temporary variable |
| `u64range` | **Local** | `uint64_t` | Range calculation temporary |
| `byte_encode_crypto` | **Constant** | `uint8_t` | Bitcoin version byte (0x00) |
| `bit_range_str_min` | **Local/Derived** | `char*` | Display string |
| `bit_range_str_max` | **Local/Derived** | `char*` | Display string |
| `default_fileName` | **Constant** | `const char*` | Default "addresses.txt" |

---

## Migration Strategy

### Phase 1: Parallel Systems (CURRENT)
- ✅ New config system exists in `src/config/config.h`
- ✅ Old globals still exist for compatibility
- ⚠️ Both systems coexist during transition

### Phase 2: Gradual Conversion
1. **Update CLI parsing** (`src/cli.cpp`)
   - Parse arguments into `keyhunt_config_t` instead of globals
2. **Update main()** (`src/keyhunt.cpp`)
   - Initialize config structure
   - Pass config pointer to all major functions
3. **Update function signatures** (module by module)
   - Add `keyhunt_config_t *config` parameter
   - Replace global reads with `config->group.field`
4. **Update tests**
   - Create test configs instead of setting globals
   - Verify isolated state per test

### Phase 3: Remove Globals
- Delete all legacy global variable declarations
- Remove compatibility shims
- Update documentation

---

## Code Examples

### Example 1: Function Signature Migration

**Before:**
```cpp
void process_keys() {
    if (FLAGDEBUG) {
        fprintf(stderr, "Processing in mode %d\n", FLAGMODE);
    }
    for (int i = 0; i < NTHREADS; i++) {
        // start thread
    }
}
```

**After:**
```cpp
void process_keys(keyhunt_config_t *config) {
    if (config->search.debug_mode) {
        fprintf(stderr, "Processing in mode %s\n",
                kh_search_mode_name(config->search.mode));
    }
    for (int i = 0; i < config->runtime.num_threads; i++) {
        // start thread
    }
}
```

### Example 2: BSGS Memory Validation

**Before:**
```cpp
// Scattered throughout code
if (bsgs_m * KFACTOR * 3.5 > available_ram) {
    fprintf(stderr, "Error: insufficient memory\n");
    exit(1);
}
```

**After:**
```cpp
// Centralized validation
if (kh_bsgs_config_validate_memory(&config->bsgs, available_ram) != 0) {
    fprintf(stderr, "Error: insufficient memory\n");
    exit(1);
}
```

### Example 3: Config Initialization

**Before:**
```cpp
// Scattered initialization
NTHREADS = 4;
FLAGMODE = MODE_BSGS;
bsgs_m = 4194304;
KFACTOR = 1;
FLAGRANDOM = 0;
```

**After:**
```cpp
// Single initialization
keyhunt_config_t config;
kh_config_init(&config);

// Override specific values
config.runtime.num_threads = 4;
config.search.mode = SEARCH_MODE_BSGS;
config.bsgs.m_value = 4194304;
config.bsgs.k_factor = 1;
config.search.random_mode = false;

// Validate and apply auto-tuning
kh_config_validate(&config);
```

### Example 4: Thread Function Signature

**Before:**
```cpp
void *thread_process_bsgs(void *vargp) {
    // Access globals directly
    if (FLAGDEBUG) { ... }
    uint64_t m = bsgs_m;
    int threads = NTHREADS;

    // No clear dependency tracking
}
```

**After:**
```cpp
struct thread_params {
    keyhunt_config_t *config;
    int thread_id;
};

void *thread_process_bsgs(void *vargp) {
    struct thread_params *params = (struct thread_params*)vargp;
    keyhunt_config_t *config = params->config;

    // Explicit dependencies
    if (config->search.debug_mode) { ... }
    uint64_t m = config->bsgs.m_value;
    int threads = config->runtime.num_threads;
}
```

---

## Testing Strategy

### Unit Testing Benefits

With the new config system, you can now:

```cpp
void test_bsgs_with_different_configs() {
    keyhunt_config_t config1, config2;

    // Test case 1: Small N value
    kh_config_init(&config1);
    config1.bsgs.n_value = 1024;
    config1.bsgs.k_factor = 1;
    run_bsgs_test(&config1);

    // Test case 2: Large N value
    kh_config_init(&config2);
    config2.bsgs.n_value = 1048576;
    config2.bsgs.k_factor = 4;
    run_bsgs_test(&config2);

    // No global state pollution!
}
```

### Integration Testing

```cpp
int main() {
    // Test 1: Address mode
    keyhunt_config_t cfg1;
    kh_config_init(&cfg1);
    cfg1.search.mode = SEARCH_MODE_ADDRESS;
    assert(run_search(&cfg1) == 0);

    // Test 2: BSGS mode (independent of test 1)
    keyhunt_config_t cfg2;
    kh_config_init(&cfg2);
    cfg2.search.mode = SEARCH_MODE_BSGS;
    assert(run_search(&cfg2) == 0);
}
```

---

## API Reference

### Initialization Functions

```c
// Initialize entire config with defaults
void kh_config_init(keyhunt_config_t *cfg);

// Initialize individual sections
void kh_search_config_init(search_config_t *cfg);
void kh_bsgs_config_init(bsgs_config_t *cfg);
void kh_gpu_config_init(gpu_config_t *cfg);
void kh_runtime_state_init(runtime_state_t *state);
void kh_autotune_config_init(autotune_config_t *cfg);
```

### Validation Functions

```c
// Validate entire config, auto-correct invalid values
int kh_config_validate(keyhunt_config_t *cfg);

// Validate BSGS memory requirements
int kh_bsgs_config_validate_memory(bsgs_config_t *cfg, uint64_t available_ram);
```

### Helper Functions

```c
// Get human-readable names for enums
const char* kh_search_mode_name(search_mode_t mode);
const char* kh_key_format_name(key_format_t fmt);
const char* kh_crypto_type_name(crypto_type_t crypto);
const char* kh_bsgs_mode_name(bsgs_mode_t mode);

// Calculate BSGS memory requirements
uint64_t kh_bsgs_calc_memory(uint64_t n, int k,
                              uint64_t *bloom_out,
                              uint64_t *table_out);

// Apply auto-tuned values
void kh_config_apply_autotune(keyhunt_config_t *cfg);

// Print configuration (debugging)
void kh_config_print(const keyhunt_config_t *cfg);
void kh_search_config_print(const search_config_t *cfg);
void kh_bsgs_config_print(const bsgs_config_t *cfg);
void kh_gpu_config_print(const gpu_config_t *cfg);
```

### Cleanup Functions

```c
// Free dynamically allocated resources
void kh_runtime_state_cleanup(runtime_state_t *state);
```

---

## Compatibility Notes

### Type Safety Improvements

The new system uses **type-safe enums** instead of magic numbers:

**Before:**
```cpp
#define MODE_ADDRESS 1
#define MODE_BSGS 2
// ... easy to make mistakes with raw integers

if (mode == 1) { ... }  // What does 1 mean?
```

**After:**
```cpp
typedef enum {
    SEARCH_MODE_ADDRESS,
    SEARCH_MODE_BSGS,
    // ...
} search_mode_t;

if (config->search.mode == SEARCH_MODE_ADDRESS) { ... }  // Clear intent
```

### Backward Compatibility Macros

During transition, compatibility macros exist in `config.h`:

```c
#define SEARCH_MODE_ADDRESS   MODE_ADDRESS
#define SEARCH_MODE_BSGS      MODE_BSGS
// ... allows gradual migration
```

---

## Performance Considerations

### Memory Layout

The new config structure is designed for cache-friendliness:
- Related fields grouped together
- Hot path fields (runtime state) in separate struct
- Alignment-friendly layout

### Thread Safety

- **Config structs**: Read-only after initialization (thread-safe)
- **Runtime state**: Contains volatile atomics for counters
- **Mutexes**: Encapsulated in runtime_state_t

### Zero-Cost Abstraction

Passing `config` pointer has **no performance penalty**:
- Compiler optimizations inline field accesses
- No virtual function overhead
- Same as global variable access in optimized builds

---

## Troubleshooting

### Common Migration Issues

#### Issue 1: Missing Global Variable
**Symptom:** `error: 'NTHREADS' was not declared in this scope`

**Solution:** Update function to accept config parameter:
```cpp
// Before
void my_function() { use(NTHREADS); }

// After
void my_function(keyhunt_config_t *config) {
    use(config->runtime.num_threads);
}
```

#### Issue 2: Derived Flag Missing
**Symptom:** `FLAGBITRANGE` not in config

**Solution:** Compute derived value:
```cpp
// Before
if (FLAGBITRANGE) { ... }

// After
if (config->search.bit_range != 0) { ... }
```

#### Issue 3: Thread Access to Config
**Symptom:** Thread functions can't access config

**Solution:** Pass config via thread parameters:
```cpp
struct thread_params {
    keyhunt_config_t *config;
    int thread_id;
};

// In thread creation:
params.config = &config;
pthread_create(&tid, NULL, thread_func, &params);
```

---

## Summary Table: All 50+ Variables

| # | Legacy Variable | Config Field | Group |
|---|----------------|--------------|-------|
| 1 | `FLAGMODE` | `search.mode` | SearchConfig |
| 2 | `FLAGSEARCH` | `search.key_format` | SearchConfig |
| 3 | `FLAGCRYPTO` | `search.crypto_type` | SearchConfig |
| 4 | `range_start` | `search.range_start` | SearchConfig |
| 5 | `range_end` | `search.range_end` | SearchConfig |
| 6 | `bitrange` | `search.bit_range` | SearchConfig |
| 7 | `str_stride` | `search.stride` | SearchConfig |
| 8 | `FLAGSTRIDE` | `search.stride_enabled` | SearchConfig |
| 9 | `FLAGRANDOM` | `search.random_mode` | SearchConfig |
| 10 | `FLAGENDOMORPHISM` | `search.endomorphism` | SearchConfig |
| 11 | `FLAGQUIET` | `search.quiet_mode` | SearchConfig |
| 12 | `FLAGDEBUG` | `search.debug_mode` | SearchConfig |
| 13 | `FLAGMATRIX` | `search.matrix_mode` | SearchConfig |
| 14 | `FLAGPROGRESSBAR` | `search.progress_bar` | SearchConfig |
| 15 | `bsgs_m` | `bsgs.m_value` | BsgsConfig |
| 16 | `bsgs_m2` | `bsgs.m2_value` | BsgsConfig |
| 17 | `bsgs_m3` | `bsgs.m3_value` | BsgsConfig |
| 18 | `KFACTOR` | `bsgs.k_factor` | BsgsConfig |
| 19 | `FLAGBLOOMMULTIPLIER` | `bsgs.bloom_multiplier` | BsgsConfig |
| 20 | `bloom_bP_totalbytes` | `bsgs.bloom_bp_bytes` | BsgsConfig |
| 21 | `bloom_bP2_totalbytes` | `bsgs.bloom_bp2_bytes` | BsgsConfig |
| 22 | `bloom_bP3_totalbytes` | `bsgs.bloom_bp3_bytes` | BsgsConfig |
| 23 | `FLAGBSGSMODE` | `bsgs.bsgs_mode` | BsgsConfig |
| 24 | `FLAGSAVEREADFILE` | `bsgs.save_progress` | BsgsConfig |
| 25 | `FLAGPRECALCUTED_P_FILE` | `bsgs.load_precalc` | BsgsConfig |
| 26 | `bsgs_aux` | `bsgs.aux_value` | BsgsConfig |
| 27 | `bsgs_point_number` | `bsgs.point_number` | BsgsConfig |
| 28 | `FLAGGPU` | `gpu.enabled` | GpuConfig |
| 29 | `FLAGGPU_FULL` | `gpu.full_mode` | GpuConfig |
| 30 | `FLAGGPU_HYBRID` | `gpu.hybrid_mode` | GpuConfig |
| 31 | `NTHREADS` | `runtime.num_threads` | RuntimeState |
| 32 | `tid` | `runtime.thread_handles` | RuntimeState |
| 33 | `FINISHED_THREADS_COUNTER` | `runtime.finished_threads` | RuntimeState |
| 34 | `THREADCYCLES` | `runtime.thread_cycles` | RuntimeState |
| 35 | `THREADCOUNTER` | `runtime.thread_counter` | RuntimeState |
| 36 | `FINISHED_ITEMS` | `runtime.finished_items` | RuntimeState |
| 37 | `OLDFINISHED_ITEMS` | `runtime.old_finished_items` | RuntimeState |
| 38 | `OUTPUTSECONDS` | `runtime.output_interval_sec` | RuntimeState |
| 39 | `addressTable` | `runtime.address_table` | RuntimeState |
| 40 | `bloom` | `runtime.bloom_filter` | RuntimeState |
| 41 | `vanity_rmd_targets` | `runtime.vanity_targets` | RuntimeState |
| 42 | `vanity_rmd_total` | `runtime.vanity_total` | RuntimeState |
| 43 | `vanity_bloom` | `runtime.vanity_bloom` | RuntimeState |
| 44 | `bPtable` | `runtime.bsgs_bp_table` | RuntimeState |
| 45 | `bloom_bP` | `runtime.bsgs_bloom_bp` | RuntimeState |
| 46 | `bloom_bPx2nd` | `runtime.bsgs_bloom_bp2` | RuntimeState |
| 47 | `bloom_bPx3rd` | `runtime.bsgs_bloom_bp3` | RuntimeState |
| 48 | `write_keys` | `runtime.write_mutex` | RuntimeState |
| 49 | `write_random` | `runtime.random_mutex` | RuntimeState |
| 50 | `bsgs_thread` | `runtime.bsgs_mutex` | RuntimeState |
| 51 | `bsgs_found` | `runtime.keys_found` | RuntimeState |
| 52 | `g_avx2_available` | `autotune.has_avx2` | AutoTuneConfig |
| 53 | `OPTIMAL_THREADS` | `autotune.optimal_threads` | AutoTuneConfig |
| 54 | `OPTIMAL_N` | `autotune.optimal_n` | AutoTuneConfig |
| 55 | `OPTIMAL_KFACTOR` | `autotune.optimal_k_factor` | AutoTuneConfig |

**Total: 55 primary global variables mapped to structured configuration**

---

## Conclusion

This migration guide documents the complete mapping from legacy global variables to the new structured configuration system. The new system provides:

1. **Better organization**: Logical grouping of related configuration
2. **Type safety**: Enums instead of magic numbers
3. **Testability**: Isolated state per test case
4. **Thread safety**: Clear ownership and synchronization
5. **Self-documentation**: Structure names describe purpose
6. **Maintainability**: Easier to understand and modify

For questions or issues during migration, refer to:
- **config.h**: New configuration structure definitions
- **config.cpp**: Implementation of helper functions
- **AUTO-TUNING.md**: Auto-detection and validation system
- **PARAMETER_VALIDATION.md**: Parameter validation details

---

*Last Updated: 2026-02-23*
*Config Version: 1 (KH_CONFIG_VERSION)*
