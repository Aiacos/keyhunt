/*
 * config/config.h - Grouped configuration structs for keyhunt
 *
 * This module provides structured configuration management with:
 *   - SearchConfig: Search mode, type, crypto settings
 *   - BsgsConfig: BSGS algorithm parameters (N, K, M, bloom)
 *   - GpuConfig: GPU settings (enabled, full mode, hybrid)
 *   - RuntimeState: Mutable runtime state (counters, handles)
 *   - KeyhuntConfig: Top-level container for all config groups
 *
 * This is the NEW configuration system (src/config/).
 * The legacy system in src/core/config.h handles INI file loading.
 */

#ifndef KEYHUNT_CONFIG_CONFIG_H
#define KEYHUNT_CONFIG_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Import shared type definitions from cli.h to avoid ODR violations */
#include "../cli.h"

#ifdef __cplusplus
#include <atomic>
/* Forward declaration of Int class for C++ compilation */
class Int;
extern "C" {
#else
#include <stdatomic.h>
#endif

/* ============================================================================
 * Compatibility Macros (map new names to cli.h names)
 * ============================================================================ */

/* Search mode aliases (cli.h uses MODE_*, new code can use SEARCH_MODE_*) */
#define SEARCH_MODE_ADDRESS   MODE_ADDRESS
#define SEARCH_MODE_BSGS      MODE_BSGS
#define SEARCH_MODE_XPOINT    MODE_XPOINT
#define SEARCH_MODE_RMD160    MODE_RMD160
#define SEARCH_MODE_VANITY    MODE_VANITY
#define SEARCH_MODE_PUB2RMD   MODE_PUB2RMD
#define SEARCH_MODE_MINIKEYS  MODE_MINIKEYS

/* Key compression type (use key_type_t from cli.h, alias for compatibility) */
typedef key_type_t key_format_t;
#define KEY_UNCOMPRESSED KEYTYPE_UNCOMPRESSED
#define KEY_COMPRESSED   KEYTYPE_COMPRESSED
#define KEY_BOTH         KEYTYPE_BOTH

/* Cryptocurrency type */
typedef enum {
    CRYPTO_TYPE_NONE = 0,  /* No specific crypto */
    CRYPTO_TYPE_BTC  = 1,  /* Bitcoin */
    CRYPTO_TYPE_ETH  = 2,  /* Ethereum */
    CRYPTO_TYPE_ALL  = 3   /* All supported cryptos */
} crypto_type_t;

/* ============================================================================
 * SearchConfig - Core search parameters
 * ============================================================================ */

typedef struct {
    /* Mode selection */
    search_mode_t mode;           /* Search algorithm mode */
    key_format_t  key_format;     /* Compressed/uncompressed/both */
    crypto_type_t crypto_type;    /* BTC/ETH/ALL */

    /* Range specification */
    char range_start[KH_RANGE_BUF_SIZE];  /* Start of search range (hex string) */
    char range_end[KH_RANGE_BUF_SIZE];    /* End of search range (hex string) */
    int  bit_range;               /* Bit range for puzzles (0 = not set) */

    /* Stride for sequential search */
    char stride[KH_RANGE_BUF_SIZE];       /* Custom stride value (hex string) */
    bool stride_enabled;          /* Use custom stride */

    /* Target file */
    char target_file[KH_PATH_BUF_SIZE];   /* Path to target addresses/hashes file */

    /* Flags */
    bool random_mode;             /* Random key generation */
    bool endomorphism;            /* Use endomorphism optimization */
    bool quiet_mode;              /* Minimal output */
    bool debug_mode;              /* Debug output */
    bool matrix_mode;             /* Matrix display */
    bool progress_bar;            /* Show progress bar */
    bool skip_checksum;           /* FLAGSKIPCHECKSUM (minikey validation bypass) */
} search_config_t;

/* ============================================================================
 * BsgsConfig - BSGS algorithm parameters
 * ============================================================================ */

typedef struct {
    /* Core BSGS parameters */
    uint64_t n_value;             /* N value (baby steps count) */
    int      k_factor;            /* K multiplication factor */
    uint64_t m_value;             /* M value (sqrt of N) */

    /* Extended precision parameters (for bit ranges > 64) */
#ifdef __cplusplus
    Int*     n_value_int;         /* N value as Int* (256-bit precision) */
    Int*     m_value_int;         /* M value as Int* (256-bit precision) */
#else
    void*    n_value_int;         /* N value as Int* (opaque in C) */
    void*    m_value_int;         /* M value as Int* (opaque in C) */
#endif

    /* Bloom filter parameters */
    int      bloom_multiplier;    /* Bloom filter size multiplier */
    uint64_t bloom_bp_bytes;      /* Primary bloom filter bytes */
    uint64_t bloom_bp2_bytes;     /* Secondary bloom filter bytes */
    uint64_t bloom_bp3_bytes;     /* Tertiary bloom filter bytes */

    /* BSGS search mode */
    bsgs_mode_t bsgs_mode;        /* Sequential/backward/both/random/dance */

    /* Caching */
    bool save_progress;           /* Save bloom/bP tables to disk */
    bool load_precalc;            /* Load precalculated files */
    char precalc_file[KH_PATH_BUF_SIZE]; /* Path to precalculated file */

    /* Derived values (computed at runtime) */
    uint64_t m2_value;            /* M / 32 */
    uint64_t m3_value;            /* M2 / 32 */
    uint64_t aux_value;           /* Auxiliary computation value */
    uint32_t point_number;        /* Number of target points */
} bsgs_config_t;

/* ============================================================================
 * GpuConfig - GPU acceleration settings
 * ============================================================================ */

#define GPU_MAX_DEVICES 16

typedef struct {
    /* GPU enable flags */
    int  enabled;                 /* 0=off, 1=on, -1=auto */
    bool full_mode;               /* Full ECC+hash+match on GPU */
    bool hybrid_mode;             /* Run GPU+CPU in parallel */
    bool multi_gpu_enabled;       /* Enable multiple GPU devices */

    /* Device selection */
    int  device_ids[GPU_MAX_DEVICES];  /* Array of GPU device IDs */
    int  device_count;            /* Number of selected devices */

    /* Hybrid mode parameters */
    int  range_percent;           /* GPU gets this % of range (default 80) */

    /* Performance tuning */
    int  blocks_per_sm;           /* Blocks per streaming multiprocessor */
    int  threads_per_block;       /* Threads per CUDA block */
    int  keys_per_thread;         /* Keys processed per GPU thread */

    /* Runtime stats (atomic for cross-thread visibility) */
#ifdef __cplusplus
    std::atomic<uint64_t> keys_checked;      /* Total GPU keys checked */
    std::atomic<uint64_t> keys_checked_cur;  /* Current block keys */
    std::atomic<int>      should_stop;       /* Signal GPU to stop */
#else
    _Atomic uint64_t keys_checked;      /* Total GPU keys checked */
    _Atomic uint64_t keys_checked_cur;  /* Current block keys */
    _Atomic int      should_stop;       /* Signal GPU to stop */
#endif

    /* Bloom filter state */
    bool bloom_uploaded;          /* GPU-side bloom filter ready */
} gpu_config_t;

/* ============================================================================
 * RuntimeState - Mutable state during execution
 * ============================================================================ */

typedef struct {
    /* Thread management */
    int      num_threads;         /* Number of CPU threads */
    void    *thread_handles;      /* Platform-specific thread handles */

    /* Progress counters (atomic in implementation) */
    uint64_t finished_threads;    /* Number of completed threads */
    uint64_t thread_cycles;       /* Total thread cycles */
    uint64_t thread_counter;      /* Shared counter */
    uint64_t finished_items;      /* Keys processed (atomic) */
    uint64_t old_finished_items;  /* Previous value for rate calculation */

    /* Timing */
    uint64_t start_time_ms;       /* Search start time (milliseconds) */
    uint64_t last_output_time;    /* Last status output time */
    int      output_interval_sec; /* Seconds between status updates */

    /* Target data */
    void    *address_table;       /* Sorted address table */
    int64_t  address_count;       /* Number of target addresses */
    void    *bloom_filter;        /* Main bloom filter */

    /* Vanity mode state */
    int      vanity_targets;      /* Number of vanity targets */
    int      vanity_total;        /* Total vanity patterns */
    void    *vanity_bloom;        /* Vanity-specific bloom filter */

    /* BSGS runtime state */
    void    *bsgs_bp_table;       /* Baby step point table */
    void    *bsgs_bloom_bp;       /* BSGS bloom filter array */
    void    *bsgs_bloom_bp2;      /* Second level bloom */
    void    *bsgs_bloom_bp3;      /* Third level bloom */

    /* Synchronization primitives */
    void    *write_mutex;         /* Mutex for file writes */
    void    *random_mutex;        /* Mutex for random generation */
    void    *bsgs_mutex;          /* BSGS-specific mutex */

    /* Work pool state */
    void    *work_pool;           /* Work-stealing pool handle */
    bool     work_pool_enabled;   /* Work pool is active */
    bool     work_pool_exhausted; /* All work distributed */

    /* Found keys */
    int      keys_found;          /* Total keys found this session */
    char     output_file[KH_PATH_BUF_SIZE]; /* Path to output file */

    /* ------------------------------------------------------------------ */
    /*  Fields below added for Phase 3 Config Migration (CFG-01)          */
    /* ------------------------------------------------------------------ */

    /* Secp256k1 curve instance */
    void    *secp;                /* Secp256K1* (set once at init, read-only from threads) */

    /* Thread progress arrays (allocated per-thread) */
    void    *thread_counters;     /* struct thread_counter* (cache-padded counters) */
    void    *thread_flags;        /* struct thread_flag* (thread completion flags) */
    void    *thread_output;       /* std::atomic<int>* (output synchronization) */

    /* Generator points (set once at init) */
    void    *generator_points;    /* std::vector<Point>* Gn (precomputed points) */
    void    *generator_point_2;   /* Point* _2Gn (doubled generator) */

    /* Endomorphism constants (set once at init, read-only) */
    void    *endo_lambda;         /* Int* lambda */
    void    *endo_lambda2;        /* Int* lambda2 */
    void    *endo_beta;           /* Int* beta */
    void    *endo_beta2;          /* Int* beta2 */

    /* Range parameters (set once, read-only from threads) */
    void    *range_start;         /* Int* n_range_start */
    void    *range_end;           /* Int* n_range_end */
    void    *stride;              /* Int* stride value */

    /* Minikey mode state (set once at init) */
    void    *minikey_coinbuffer;  /* char* Ccoinbuffer (base58 lookup) */
    void    *minikey_raw_base;    /* char* raw_baseminikey */
    void    *minikey_n;           /* char* minikeyN */
    int      minikey_n_limit;     /* int minikey_n_limit */

    /* Vanity mode extended state */
    void    *vanity_limits;       /* int* vanity_rmd_limits */
    void    *vanity_values_a;     /* uint8_t*** vanity_rmd_limit_values_A */
    void    *vanity_values_b;     /* uint8_t*** vanity_rmd_limit_values_B */
    int      vanity_min_check_len;/* int vanity_rmd_minimun_bytes_check_length */
    void    *vanity_addresses;    /* char** vanity_address_targets */

    /* BSGS context (allocated only in BSGS mode) */
    void    *bsgs_context;        /* bsgs_context_t* (see search_bsgs.cpp) */

    /* BSGS generator points (set once at init) */
    void    *bsgs_generator_points;  /* std::vector<Point>* GSn */
    void    *bsgs_generator_point_2; /* Point* _2GSn */

    /* Original range bounds (immutable after init, for writekey validation) */
    void    *range_progress_start; /* Int* g_rangeProgressStart (original range start) */
    void    *range_progress_end;   /* Int* g_rangeProgressEnd (original range end) */

    /* Max address/hash length for bloom checks */
    int      max_address_length;  /* MAXLENGTHADDRESS */

    /* Sequential iteration limit */
    uint64_t sequential_max;      /* N_SEQUENTIAL_MAX */
} runtime_state_t;

/* ============================================================================
 * SCHEMA FROZEN -- Phase 3 Config Migration (CFG-01)
 *
 * All fields above are committed. During Phase 3 migration:
 * - DO NOT remove or rename existing fields
 * - Adding new fields is acceptable if discovered missing
 * - Document any additions in this section
 *
 * Schema version: 3 (bumped from 2)
 * ============================================================================ */

/* ============================================================================
 * AutoTuneConfig - Auto-detected system parameters
 * ============================================================================ */

typedef struct {
    /* CPU info */
    int      cpu_physical_cores;  /* Physical CPU cores */
    int      cpu_logical_cores;   /* Logical cores (with HT) */
    bool     has_avx2;            /* AVX2 SIMD available */
    bool     has_avx512;          /* AVX-512 available */
    bool     has_sha_ni;          /* SHA-NI instructions */

    /* Memory info */
    uint64_t total_ram_bytes;     /* Total system RAM */
    uint64_t available_ram_bytes; /* Available RAM */
    int      memory_limit_pct;    /* % of RAM to use (default 75) */

    /* Recommended values */
    int      optimal_threads;     /* Recommended thread count */
    uint64_t optimal_n;           /* Recommended N value */
    int      optimal_k_factor;    /* Recommended K factor */
    uint32_t optimal_batch_size;  /* Recommended batch size */
} autotune_config_t;

/* ============================================================================
 * KeyhuntConfig - Top-level configuration container
 * ============================================================================ */

typedef struct {
    /* Version for compatibility checks */
    uint32_t version;             /* Config structure version */

    /* Grouped configurations */
    search_config_t   search;     /* Search parameters */
    bsgs_config_t     bsgs;       /* BSGS algorithm settings */
    gpu_config_t      gpu;        /* GPU acceleration settings */
    autotune_config_t autotune;   /* Auto-detected parameters */

    /* Runtime state (mutable) */
    runtime_state_t   runtime;    /* Execution state */

    /* Legacy INI config path */
    char ini_config_path[KH_PATH_BUF_SIZE]; /* Path to keyhunt.conf if loaded */
    bool ini_loaded;              /* INI file was loaded */

    /* Flags indicating what was explicitly set */
    struct {
        bool mode;
        bool range;
        bool bit_range;
        bool threads;
        bool n_value;
        bool k_factor;
        bool gpu;
        bool stride;
        bool target_file;
    } explicitly_set;
} keyhunt_config_t;

/* ============================================================================
 * Configuration Functions
 * ============================================================================ */

/**
 * Initialize configuration with default values
 *
 * @param cfg Pointer to configuration structure
 */
void kh_config_init(keyhunt_config_t *cfg);

/**
 * Initialize only the search config section
 *
 * @param cfg Pointer to search config
 */
void kh_search_config_init(search_config_t *cfg);

/**
 * Initialize only the BSGS config section
 *
 * @param cfg Pointer to BSGS config
 */
void kh_bsgs_config_init(bsgs_config_t *cfg);

/**
 * Initialize only the GPU config section
 *
 * @param cfg Pointer to GPU config
 */
void kh_gpu_config_init(gpu_config_t *cfg);

/**
 * Initialize only the runtime state section
 *
 * @param state Pointer to runtime state
 */
void kh_runtime_state_init(runtime_state_t *state);

/**
 * Initialize autotune config from system detection
 *
 * @param cfg Pointer to autotune config
 */
void kh_autotune_config_init(autotune_config_t *cfg);

/**
 * Validate configuration and auto-correct invalid values
 *
 * @param cfg Pointer to configuration
 * @return 0 on success, negative on error
 */
int kh_config_validate(keyhunt_config_t *cfg);

/**
 * Validate BSGS configuration against available memory
 *
 * @param cfg Pointer to BSGS config
 * @param available_ram Available RAM in bytes
 * @return 0 on success, -1 if would exceed memory
 */
int kh_bsgs_config_validate_memory(bsgs_config_t *cfg, uint64_t available_ram);

/**
 * Print current configuration to stderr
 *
 * @param cfg Pointer to configuration
 */
void kh_config_print(const keyhunt_config_t *cfg);

/**
 * Print search config section
 *
 * @param cfg Pointer to search config
 */
void kh_search_config_print(const search_config_t *cfg);

/**
 * Print BSGS config section
 *
 * @param cfg Pointer to BSGS config
 */
void kh_bsgs_config_print(const bsgs_config_t *cfg);

/**
 * Print GPU config section
 *
 * @param cfg Pointer to GPU config
 */
void kh_gpu_config_print(const gpu_config_t *cfg);

/**
 * Get string name for search mode
 *
 * @param mode Search mode enum value
 * @return Static string name
 */
const char* kh_search_mode_name(search_mode_t mode);

/**
 * Get string name for key format
 *
 * @param fmt Key format enum value
 * @return Static string name
 */
const char* kh_key_format_name(key_format_t fmt);

/**
 * Get string name for crypto type
 *
 * @param crypto Crypto type enum value
 * @return Static string name
 */
const char* kh_crypto_type_name(crypto_type_t crypto);

/**
 * Get string name for BSGS mode
 *
 * @param mode BSGS mode enum value
 * @return Static string name
 */
const char* kh_bsgs_mode_name(bsgs_mode_t mode);

/**
 * Calculate BSGS memory requirements
 *
 * @param n N value
 * @param k K factor
 * @param bloom_out Output: bloom filter bytes needed
 * @param table_out Output: bP table bytes needed
 * @return Total bytes needed
 */
uint64_t kh_bsgs_calc_memory(uint64_t n, int k, uint64_t *bloom_out, uint64_t *table_out);

#ifdef __cplusplus
/**
 * Calculate BSGS memory requirements using Int (256-bit) arithmetic
 *
 * This version supports N values beyond uint64_t range by using Int arithmetic.
 * Calculates M = sqrt(N) and total memory = bloom + bP table.
 *
 * @param n_int Pointer to Int representing N value
 * @param k K factor (multiplication factor)
 * @param m_int_out Optional output pointer to store calculated M value (caller must free)
 * @param bloom_out Optional output pointer for bloom filter bytes
 * @param table_out Optional output pointer for bP table bytes
 * @return Total bytes needed (UINT64_MAX if overflow occurs)
 */
uint64_t kh_bsgs_calc_memory_int(Int *n_int, int k, Int **m_int_out, uint64_t *bloom_out, uint64_t *table_out);
#endif

/**
 * Apply autotune settings to main config
 *
 * @param cfg Pointer to main config
 */
void kh_config_apply_autotune(keyhunt_config_t *cfg);

/**
 * Free any dynamically allocated resources in runtime state
 *
 * @param state Pointer to runtime state
 */
void kh_runtime_state_cleanup(runtime_state_t *state);

/* ============================================================================
 * Environment Variable Overrides
 *
 * These env vars provide runtime tuning that is applied before CLI args.
 * See docs/ENV_VARIABLES.md for full documentation and examples.
 * ============================================================================ */

typedef struct {
    /* Profiling and debugging */
    bool profile_enabled;         /* KEYHUNT_PROFILE          (default: false) */
    bool skip_sysinfo;            /* KEYHUNT_SKIP_SYSINFO     (default: false) */
    bool debug_distributed;       /* KEYHUNT_DEBUG            (default: false) */
    bool debug_subprocess;        /* KEYHUNT_DEBUG_SUBPROCESS  (default: false) */

    /* GPU tuning */
    bool gpu_selftest;            /* KEYHUNT_GPU_SELFTEST     (default: false) */
    int  hybrid_gpu_percent;      /* KEYHUNT_HYBRID_GPU_PERCENT (0=auto, 1-99) */
    bool hybrid_work_steal;       /* KEYHUNT_HYBRID_WORK_STEAL (default: false) */
    uint64_t hybrid_block_size;   /* KEYHUNT_HYBRID_BLOCK_SIZE (default: 0x100000000) */

    /* CPU tuning */
    int  cpu_use_y;               /* KEYHUNT_CPU_USE_Y         (default: 1) */
    int  hybrid_cpu_use_y;        /* KEYHUNT_HYBRID_CPU_USE_Y  (default: 1) */
    int64_t cpu_n_override;       /* KEYHUNT_CPU_N             (0=auto) */
    int64_t hybrid_cpu_n_override;/* KEYHUNT_HYBRID_CPU_N      (0=auto) */
} env_overrides_t;

/**
 * Read all KEYHUNT_* environment variables into the overrides struct.
 *
 * @param env  Pointer to env_overrides_t to populate
 */
void kh_env_overrides_init(env_overrides_t *env);

/* ============================================================================
 * Configuration Version
 * ============================================================================ */

#define KH_CONFIG_VERSION 3

#ifdef __cplusplus
}

/* ============================================================================
 * Helper Functions for Int/uint64_t Conversion (C++ only)
 * ============================================================================ */

/**
 * Convert a uint64_t value to an Int* (256-bit integer).
 *
 * @param value  The 64-bit unsigned integer to convert
 * @return       Newly allocated Int* (caller must free)
 */
Int* uint64_to_int(uint64_t value);

/**
 * Safely convert an Int* to uint64_t, checking for overflow.
 *
 * @param value     The Int* to convert (can be NULL)
 * @param overflow  Output flag set to true if value doesn't fit in 64 bits (can be NULL)
 * @return          The lower 64 bits of the Int value (0 if value is NULL)
 *
 * Note: Returns the lower 64 bits even on overflow. Check the overflow flag
 *       to determine if data was lost.
 */
uint64_t int_to_uint64_safe(Int* value, bool* overflow);

#endif

#endif /* KEYHUNT_CONFIG_CONFIG_H */
