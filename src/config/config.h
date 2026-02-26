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

#ifdef __cplusplus
#include <atomic>
#endif

/* Import shared type definitions from cli.h to avoid ODR violations */
#include "../cli.h"

#ifdef __cplusplus
extern "C" {
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
    char range_start[128];        /* Start of search range (hex string) */
    char range_end[128];          /* End of search range (hex string) */
    int  bit_range;               /* Bit range for puzzles (0 = not set) */

    /* Stride for sequential search */
    char stride[128];             /* Custom stride value (hex string) */
    bool stride_enabled;          /* Use custom stride */

    /* Target file */
    char target_file[512];        /* Path to target addresses/hashes file */

    /* Flags */
    bool random_mode;             /* Random key generation */
    bool endomorphism;            /* Use endomorphism optimization */
    bool quiet_mode;              /* Minimal output */
    bool debug_mode;              /* Debug output */
    bool matrix_mode;             /* Matrix display */
    bool progress_bar;            /* Show progress bar */
} search_config_t;

/* ============================================================================
 * BsgsConfig - BSGS algorithm parameters
 * ============================================================================ */

typedef struct {
    /* Core BSGS parameters */
    uint64_t n_value;             /* N value (baby steps count) */
    int      k_factor;            /* K multiplication factor */
    uint64_t m_value;             /* M value (sqrt of N) */

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
    char precalc_file[512];       /* Path to precalculated file */

    /* Derived values (computed at runtime) */
    uint64_t m2_value;            /* M / 32 */
    uint64_t m3_value;            /* M2 / 32 */
    uint64_t aux_value;           /* Auxiliary computation value */
    uint32_t point_number;        /* Number of target points */
} bsgs_config_t;

/* ============================================================================
 * GpuConfig - GPU acceleration settings
 * ============================================================================ */

#define GPU_MAX_DEVICES 8

typedef struct {
    /* GPU enable flags */
    int  enabled;                 /* 0=off, 1=on, -1=auto */
    bool full_mode;               /* Full ECC+hash+match on GPU */
    bool hybrid_mode;             /* Run GPU+CPU in parallel */

    /* Device selection */
    int  device_ids[GPU_MAX_DEVICES];  /* Array of GPU device IDs */
    int  device_count;            /* Number of selected devices */

    /* Hybrid mode parameters */
    int  range_percent;           /* GPU gets this % of range (default 80) */

    /* Performance tuning */
    int  blocks_per_sm;           /* Blocks per streaming multiprocessor */
    int  threads_per_block;       /* Threads per CUDA block */
    int  keys_per_thread;         /* Keys processed per GPU thread */

#ifdef __cplusplus
    /* Runtime stats (atomic, thread-safe GPU counters) */
    std::atomic<uint64_t> keys_checked{0};      /* Total GPU keys checked */
    std::atomic<uint64_t> keys_checked_cur{0};  /* Current block keys */
    std::atomic<int>      should_stop{0};       /* Signal GPU to stop */
#else
    /* Runtime stats (volatile fallback for C) */
    volatile uint64_t keys_checked;      /* Total GPU keys checked */
    volatile uint64_t keys_checked_cur;  /* Current block keys */
    volatile int      should_stop;       /* Signal GPU to stop */
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
    char     output_file[512];    /* Path to output file */
} runtime_state_t;

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
    char ini_config_path[512];    /* Path to keyhunt.conf if loaded */
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

#define KH_CONFIG_VERSION 2

#ifdef __cplusplus
}
#endif

#endif /* KEYHUNT_CONFIG_CONFIG_H */
