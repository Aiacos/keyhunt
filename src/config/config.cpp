/*
 * config/config.cpp - Grouped configuration implementation
 *
 * Provides initialization, validation, and printing for all configuration
 * structures used by keyhunt.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
#include "../secp256k1/Int.h"
#endif

/* ============================================================================
 * String Constants for Mode Names
 * ============================================================================ */

static const char* search_mode_names[] = {
    "xpoint",    /* SEARCH_MODE_XPOINT */
    "address",   /* SEARCH_MODE_ADDRESS */
    "bsgs",      /* SEARCH_MODE_BSGS */
    "rmd160",    /* SEARCH_MODE_RMD160 */
    "pub2rmd",   /* SEARCH_MODE_PUB2RMD */
    "minikeys",  /* SEARCH_MODE_MINIKEYS */
    "vanity"     /* SEARCH_MODE_VANITY */
};

static const char* key_format_names[] = {
    "compressed",    /* KEYTYPE_COMPRESSED = 0 */
    "uncompressed",  /* KEYTYPE_UNCOMPRESSED = 1 */
    "both"           /* KEYTYPE_BOTH = 2 */
};

static const char* crypto_type_names[] = {
    "none",  /* CRYPTO_TYPE_NONE */
    "btc",   /* CRYPTO_TYPE_BTC */
    "eth",   /* CRYPTO_TYPE_ETH */
    "all"    /* CRYPTO_TYPE_ALL */
};

static const char* bsgs_mode_names[] = {
    "sequential",  /* BSGS_SEQUENTIAL */
    "backward",    /* BSGS_BACKWARD */
    "both",        /* BSGS_BOTH */
    "random",      /* BSGS_RANDOM */
    "dance"        /* BSGS_DANCE */
};

/* ============================================================================
 * Name Lookup Functions
 * ============================================================================ */

const char* kh_search_mode_name(search_mode_t mode) {
    if (mode >= 0 && mode <= SEARCH_MODE_VANITY) {
        return search_mode_names[mode];
    }
    return "unknown";
}

const char* kh_key_format_name(key_format_t fmt) {
    if (fmt >= 0 && fmt <= KEY_BOTH) {
        return key_format_names[fmt];
    }
    return "unknown";
}

const char* kh_crypto_type_name(crypto_type_t crypto) {
    if (crypto >= 0 && crypto <= CRYPTO_TYPE_ALL) {
        return crypto_type_names[crypto];
    }
    return "unknown";
}

const char* kh_bsgs_mode_name(bsgs_mode_t mode) {
    if (mode >= 0 && mode <= BSGS_DANCE) {
        return bsgs_mode_names[mode];
    }
    return "unknown";
}

/* ============================================================================
 * Search Config Initialization
 * ============================================================================ */

void kh_search_config_init(search_config_t *cfg) {
    if (!cfg) return;

    memset(cfg, 0, sizeof(search_config_t));

    /* Defaults */
    cfg->mode = SEARCH_MODE_ADDRESS;
    cfg->key_format = KEY_COMPRESSED;
    cfg->crypto_type = CRYPTO_TYPE_BTC;

    cfg->range_start[0] = '\0';
    cfg->range_end[0] = '\0';
    cfg->bit_range = 0;

    cfg->stride[0] = '\0';
    cfg->stride_enabled = false;

    cfg->target_file[0] = '\0';

    cfg->random_mode = false;
    cfg->endomorphism = false;
    cfg->quiet_mode = false;
    cfg->debug_mode = false;
    cfg->matrix_mode = false;
    cfg->progress_bar = false;
}

/* ============================================================================
 * BSGS Config Initialization
 * ============================================================================ */

void kh_bsgs_config_init(bsgs_config_t *cfg) {
    if (!cfg) return;

    memset(cfg, 0, sizeof(bsgs_config_t));

    /* Defaults */
    cfg->n_value = 0;  /* Will be auto-calculated */
    cfg->k_factor = 1;
    cfg->m_value = 4194304;  /* Default 2^22 */

    cfg->bloom_multiplier = 1;
    cfg->bloom_bp_bytes = 0;
    cfg->bloom_bp2_bytes = 0;
    cfg->bloom_bp3_bytes = 0;

    cfg->bsgs_mode = BSGS_SEQUENTIAL;

    cfg->save_progress = false;
    cfg->load_precalc = false;
    cfg->precalc_file[0] = '\0';

    cfg->m2_value = 0;
    cfg->m3_value = 0;
    cfg->aux_value = 0;
    cfg->point_number = 0;
}

/* ============================================================================
 * GPU Config Initialization
 * ============================================================================ */

void kh_gpu_config_init(gpu_config_t *cfg) {
    if (!cfg) return;

    /* NOTE: gpu_config_t fields use volatile qualifiers for cross-thread visibility.
     * All fields are explicitly initialized below. */

    /* Defaults */
    cfg->enabled = 0;  /* Off by default */
    cfg->full_mode = false;
    cfg->hybrid_mode = false;

    for (int i = 0; i < GPU_MAX_DEVICES; i++) {
        cfg->device_ids[i] = -1;
    }
    cfg->device_count = 0;

    cfg->range_percent = 80;  /* GPU gets 80% of range in hybrid mode */

    cfg->blocks_per_sm = 4;
    cfg->threads_per_block = 256;
    cfg->keys_per_thread = 256;

    cfg->keys_checked = 0;
    cfg->keys_checked_cur = 0;
    cfg->should_stop = 0;

    cfg->bloom_uploaded = false;
}

/* ============================================================================
 * Runtime State Initialization
 * ============================================================================ */

void kh_runtime_state_init(runtime_state_t *state) {
    if (!state) return;

    memset(state, 0, sizeof(runtime_state_t));

    state->num_threads = 1;
    state->thread_handles = NULL;

    state->finished_threads = 0;
    state->thread_cycles = 0;
    state->thread_counter = 0;
    state->finished_items = 0;
    state->old_finished_items = (uint64_t)-1;

    state->start_time_ms = 0;
    state->last_output_time = 0;
    state->output_interval_sec = 10;

    state->address_table = NULL;
    state->address_count = 0;
    state->bloom_filter = NULL;

    state->vanity_targets = 0;
    state->vanity_total = 0;
    state->vanity_bloom = NULL;

    state->bsgs_bp_table = NULL;
    state->bsgs_bloom_bp = NULL;
    state->bsgs_bloom_bp2 = NULL;
    state->bsgs_bloom_bp3 = NULL;

    state->write_mutex = NULL;
    state->random_mutex = NULL;
    state->bsgs_mutex = NULL;

    state->work_pool = NULL;
    state->work_pool_enabled = false;
    state->work_pool_exhausted = false;

    state->keys_found = 0;
    state->output_file[0] = '\0';
}

/* ============================================================================
 * Autotune Config Initialization
 * ============================================================================ */

void kh_autotune_config_init(autotune_config_t *cfg) {
    if (!cfg) return;

    memset(cfg, 0, sizeof(autotune_config_t));

    /* These will be populated by system detection */
    cfg->cpu_physical_cores = 1;
    cfg->cpu_logical_cores = 1;
    cfg->has_avx2 = false;
    cfg->has_avx512 = false;
    cfg->has_sha_ni = false;

    cfg->total_ram_bytes = 0;
    cfg->available_ram_bytes = 0;
    cfg->memory_limit_pct = 75;

    cfg->optimal_threads = 1;
    cfg->optimal_n = 0;
    cfg->optimal_k_factor = 1;
    cfg->optimal_batch_size = 1024;
}

/* ============================================================================
 * Main Config Initialization
 * ============================================================================ */

void kh_config_init(keyhunt_config_t *cfg) {
    if (!cfg) return;

    /* NOTE: gpu_config_t uses volatile fields for cross-thread visibility.
     * Zero each section explicitly for clarity. */

    cfg->version = KH_CONFIG_VERSION;

    /* Initialize sub-configs */
    kh_search_config_init(&cfg->search);
    kh_bsgs_config_init(&cfg->bsgs);
    kh_gpu_config_init(&cfg->gpu);
    kh_runtime_state_init(&cfg->runtime);
    kh_autotune_config_init(&cfg->autotune);

    cfg->ini_config_path[0] = '\0';
    cfg->ini_loaded = false;

    /* Nothing explicitly set */
    cfg->explicitly_set.mode = false;
    cfg->explicitly_set.range = false;
    cfg->explicitly_set.bit_range = false;
    cfg->explicitly_set.threads = false;
    cfg->explicitly_set.n_value = false;
    cfg->explicitly_set.k_factor = false;
    cfg->explicitly_set.gpu = false;
    cfg->explicitly_set.stride = false;
    cfg->explicitly_set.target_file = false;
}

/* ============================================================================
 * BSGS Memory Calculation
 * ============================================================================ */

uint64_t kh_bsgs_calc_memory(uint64_t n, int k, uint64_t *bloom_out, uint64_t *table_out) {
    /*
     * BSGS Memory Formula:
     *   M = sqrt(N)
     *   Total RAM = (M * K * 3.5) + (M * K * 3.5 / 32) + (M * K * 3.5 / 1024) + (M / 32 * K * 16)
     *
     * Where:
     *   - First term: Main bloom filter (bloom1)
     *   - Second term: Secondary bloom (bloom2, 1/32 size)
     *   - Third term: Tertiary bloom (bloom3, 1/1024 size)
     *   - Fourth term: bP table (16 bytes per entry)
     */
    if (n == 0 || k <= 0) {
        if (bloom_out) *bloom_out = 0;
        if (table_out) *table_out = 0;
        return 0;
    }

    double m = sqrt((double)n);
    double mk = m * (double)k;

    /* Bloom filter sizes (3.5 bytes per element) */
    double bloom1 = mk * 3.5;
    double bloom2 = bloom1 / 32.0;
    double bloom3 = bloom1 / 1024.0;
    double total_bloom = bloom1 + bloom2 + bloom3;

    /* bP table size (16 bytes per entry, M/32 entries per K) */
    double table = (m / 32.0) * (double)k * 16.0;

    uint64_t bloom_bytes = (uint64_t)(total_bloom + 0.5);
    uint64_t table_bytes = (uint64_t)(table + 0.5);

    if (bloom_out) *bloom_out = bloom_bytes;
    if (table_out) *table_out = table_bytes;

    return bloom_bytes + table_bytes;
}

/* ============================================================================
 * BSGS Memory Validation
 * ============================================================================ */

int kh_bsgs_config_validate_memory(bsgs_config_t *cfg, uint64_t available_ram) {
    if (!cfg) return -1;

    uint64_t needed = kh_bsgs_calc_memory(cfg->n_value, cfg->k_factor, NULL, NULL);

    if (needed > available_ram) {
        /* Would exceed available memory */
        return -1;
    }

    return 0;
}

/* ============================================================================
 * Configuration Validation
 * ============================================================================ */

int kh_config_validate(keyhunt_config_t *cfg) {
    if (!cfg) return -1;

    /* Validate threads */
    if (cfg->runtime.num_threads < 1) {
        cfg->runtime.num_threads = 1;
    }
    if (cfg->runtime.num_threads > 256) {
        cfg->runtime.num_threads = 256;
    }

    /* Validate BSGS K factor */
    if (cfg->bsgs.k_factor < 1) {
        cfg->bsgs.k_factor = 1;
    }

    /* Validate bloom multiplier */
    if (cfg->bsgs.bloom_multiplier < 1) {
        cfg->bsgs.bloom_multiplier = 1;
    }

    /* Validate GPU range percent */
    if (cfg->gpu.range_percent < 1) {
        cfg->gpu.range_percent = 1;
    }
    if (cfg->gpu.range_percent > 99) {
        cfg->gpu.range_percent = 99;
    }

    /* Validate GPU tuning parameters */
    if (cfg->gpu.blocks_per_sm < 1) {
        cfg->gpu.blocks_per_sm = 4;
    }
    if (cfg->gpu.threads_per_block < 32) {
        cfg->gpu.threads_per_block = 256;
    }

    /* Validate output interval */
    if (cfg->runtime.output_interval_sec < 1) {
        cfg->runtime.output_interval_sec = 10;
    }

    /* Validate autotune memory limit */
    if (cfg->autotune.memory_limit_pct < 10) {
        cfg->autotune.memory_limit_pct = 10;
    }
    if (cfg->autotune.memory_limit_pct > 95) {
        cfg->autotune.memory_limit_pct = 95;
    }

    return 0;
}

/* ============================================================================
 * Apply Autotune Settings
 * ============================================================================ */

void kh_config_apply_autotune(keyhunt_config_t *cfg) {
    if (!cfg) return;

    /* Apply optimal thread count if not explicitly set */
    if (!cfg->explicitly_set.threads && cfg->autotune.optimal_threads > 0) {
        cfg->runtime.num_threads = cfg->autotune.optimal_threads;
    }

    /* Apply optimal N if not explicitly set (BSGS mode) */
    if (!cfg->explicitly_set.n_value && cfg->autotune.optimal_n > 0) {
        cfg->bsgs.n_value = cfg->autotune.optimal_n;
    }

    /* Apply optimal K if not explicitly set */
    if (!cfg->explicitly_set.k_factor && cfg->autotune.optimal_k_factor > 0) {
        cfg->bsgs.k_factor = cfg->autotune.optimal_k_factor;
    }
}

/* ============================================================================
 * Search Config Print
 * ============================================================================ */

void kh_search_config_print(const search_config_t *cfg) {
    if (!cfg) return;

    fprintf(stderr, "[Search Config]\n");
    fprintf(stderr, "  Mode: %s\n", kh_search_mode_name(cfg->mode));
    fprintf(stderr, "  Key format: %s\n", kh_key_format_name(cfg->key_format));
    fprintf(stderr, "  Crypto: %s\n", kh_crypto_type_name(cfg->crypto_type));

    if (cfg->range_start[0] && cfg->range_end[0]) {
        fprintf(stderr, "  Range: %s - %s\n", cfg->range_start, cfg->range_end);
    }
    if (cfg->bit_range > 0) {
        fprintf(stderr, "  Bit range: %d\n", cfg->bit_range);
    }
    if (cfg->stride_enabled) {
        fprintf(stderr, "  Stride: %s\n", cfg->stride);
    }
    if (cfg->target_file[0]) {
        fprintf(stderr, "  Target file: %s\n", cfg->target_file);
    }

    fprintf(stderr, "  Flags: %s%s%s%s%s%s\n",
            cfg->random_mode ? "random " : "",
            cfg->endomorphism ? "endomorphism " : "",
            cfg->quiet_mode ? "quiet " : "",
            cfg->debug_mode ? "debug " : "",
            cfg->matrix_mode ? "matrix " : "",
            cfg->progress_bar ? "progress " : "");
}

/* ============================================================================
 * BSGS Config Print
 * ============================================================================ */

void kh_bsgs_config_print(const bsgs_config_t *cfg) {
    if (!cfg) return;

    fprintf(stderr, "[BSGS Config]\n");
    fprintf(stderr, "  N value: %lu\n", (unsigned long)cfg->n_value);
    fprintf(stderr, "  K factor: %d\n", cfg->k_factor);
    fprintf(stderr, "  M value: %lu\n", (unsigned long)cfg->m_value);
    fprintf(stderr, "  Mode: %s\n", kh_bsgs_mode_name(cfg->bsgs_mode));
    fprintf(stderr, "  Bloom multiplier: %d\n", cfg->bloom_multiplier);

    if (cfg->bloom_bp_bytes > 0) {
        fprintf(stderr, "  Bloom sizes: %lu / %lu / %lu bytes\n",
                (unsigned long)cfg->bloom_bp_bytes,
                (unsigned long)cfg->bloom_bp2_bytes,
                (unsigned long)cfg->bloom_bp3_bytes);
    }

    fprintf(stderr, "  Save progress: %s\n", cfg->save_progress ? "yes" : "no");
    if (cfg->load_precalc && cfg->precalc_file[0]) {
        fprintf(stderr, "  Precalc file: %s\n", cfg->precalc_file);
    }
}

/* ============================================================================
 * GPU Config Print
 * ============================================================================ */

void kh_gpu_config_print(const gpu_config_t *cfg) {
    if (!cfg) return;

    fprintf(stderr, "[GPU Config]\n");
    fprintf(stderr, "  Enabled: %s\n",
            cfg->enabled < 0 ? "auto" : (cfg->enabled > 0 ? "yes" : "no"));
    fprintf(stderr, "  Full mode: %s\n", cfg->full_mode ? "yes" : "no");
    fprintf(stderr, "  Hybrid mode: %s\n", cfg->hybrid_mode ? "yes" : "no");

    if (cfg->device_count > 0) {
        fprintf(stderr, "  Devices: ");
        for (int i = 0; i < cfg->device_count; i++) {
            if (i > 0) fprintf(stderr, ", ");
            fprintf(stderr, "%d", cfg->device_ids[i]);
        }
        fprintf(stderr, "\n");
    }

    if (cfg->hybrid_mode) {
        fprintf(stderr, "  GPU range: %d%%\n", cfg->range_percent);
    }

    fprintf(stderr, "  Tuning: %d blocks/SM, %d threads/block, %d keys/thread\n",
            cfg->blocks_per_sm, cfg->threads_per_block, cfg->keys_per_thread);
}

/* ============================================================================
 * Main Config Print
 * ============================================================================ */

void kh_config_print(const keyhunt_config_t *cfg) {
    if (!cfg) return;

    fprintf(stderr, "\n========== Keyhunt Configuration ==========\n");
    fprintf(stderr, "Config version: %u\n", cfg->version);

    if (cfg->ini_loaded) {
        fprintf(stderr, "Loaded from: %s\n", cfg->ini_config_path);
    }

    fprintf(stderr, "\n");
    kh_search_config_print(&cfg->search);

    if (cfg->search.mode == SEARCH_MODE_BSGS) {
        fprintf(stderr, "\n");
        kh_bsgs_config_print(&cfg->bsgs);
    }

    fprintf(stderr, "\n");
    kh_gpu_config_print(&cfg->gpu);

    fprintf(stderr, "\n[Runtime]\n");
    fprintf(stderr, "  Threads: %d\n", cfg->runtime.num_threads);
    fprintf(stderr, "  Output interval: %d sec\n", cfg->runtime.output_interval_sec);

    if (cfg->autotune.total_ram_bytes > 0) {
        fprintf(stderr, "\n[Autotune]\n");
        fprintf(stderr, "  CPU cores: %d physical, %d logical\n",
                cfg->autotune.cpu_physical_cores, cfg->autotune.cpu_logical_cores);
        fprintf(stderr, "  SIMD: %s%s%s\n",
                cfg->autotune.has_avx2 ? "AVX2 " : "",
                cfg->autotune.has_avx512 ? "AVX-512 " : "",
                cfg->autotune.has_sha_ni ? "SHA-NI " : "");
        fprintf(stderr, "  RAM: %lu MB total, %lu MB available\n",
                (unsigned long)(cfg->autotune.total_ram_bytes / (1024 * 1024)),
                (unsigned long)(cfg->autotune.available_ram_bytes / (1024 * 1024)));
        fprintf(stderr, "  Memory limit: %d%%\n", cfg->autotune.memory_limit_pct);
    }

    fprintf(stderr, "============================================\n\n");
}

/* ============================================================================
 * Runtime State Cleanup
 * ============================================================================ */

void kh_runtime_state_cleanup(runtime_state_t *state) {
    if (!state) return;

    /*
     * Note: This function only resets state pointers.
     * Actual deallocation of bloom filters, tables, etc.
     * must be handled by the caller with knowledge of what was allocated.
     */

    state->thread_handles = NULL;
    state->address_table = NULL;
    state->bloom_filter = NULL;
    state->vanity_bloom = NULL;
    state->bsgs_bp_table = NULL;
    state->bsgs_bloom_bp = NULL;
    state->bsgs_bloom_bp2 = NULL;
    state->bsgs_bloom_bp3 = NULL;
    state->write_mutex = NULL;
    state->random_mutex = NULL;
    state->bsgs_mutex = NULL;
    state->work_pool = NULL;
}

/* ============================================================================
 * Environment Variable Overrides
 * ============================================================================ */

/* Internal: parse a boolean env var (truthy = anything except 0/false/no/empty). */
static bool env_bool(const char *name) {
    const char *v = getenv(name);
    if (!v || !*v) return false;
    if (v[0] == '0' && v[1] == '\0') return false;
    if ((v[0] == 'f' || v[0] == 'F') && (v[1] == 'a' || v[1] == 'A')) return false;
    if ((v[0] == 'n' || v[0] == 'N') && (v[1] == 'o' || v[1] == 'O')) return false;
    return true;
}

/* Internal: parse an int env var, return fallback if unset or invalid. */
static int64_t env_int(const char *name, int64_t fallback) {
    const char *v = getenv(name);
    if (!v || !*v) return fallback;
    char *end = NULL;
    long long val = strtoll(v, &end, 0);  /* auto-detect hex with 0x prefix */
    if (end == v) return fallback;
    return (int64_t)val;
}

void kh_env_overrides_init(env_overrides_t *env) {
    if (!env) return;
    memset(env, 0, sizeof(*env));

    /* Profiling and debugging */
    env->profile_enabled     = env_bool("KEYHUNT_PROFILE");
    env->skip_sysinfo        = env_bool("KEYHUNT_SKIP_SYSINFO");
    env->debug_distributed   = env_bool("KEYHUNT_DEBUG");
    env->debug_subprocess    = env_bool("KEYHUNT_DEBUG_SUBPROCESS");

    /* GPU tuning */
    env->gpu_selftest        = env_bool("KEYHUNT_GPU_SELFTEST");
    env->hybrid_gpu_percent  = (int)env_int("KEYHUNT_HYBRID_GPU_PERCENT", 0);
    env->hybrid_work_steal   = env_bool("KEYHUNT_HYBRID_WORK_STEAL");
    env->hybrid_block_size   = (uint64_t)env_int("KEYHUNT_HYBRID_BLOCK_SIZE",
                                                   0x100000000LL);

    /* CPU tuning */
    env->cpu_use_y              = (int)env_int("KEYHUNT_CPU_USE_Y", 1);
    env->hybrid_cpu_use_y       = (int)env_int("KEYHUNT_HYBRID_CPU_USE_Y", 1);
    env->cpu_n_override         = env_int("KEYHUNT_CPU_N", 0);
    env->hybrid_cpu_n_override  = env_int("KEYHUNT_HYBRID_CPU_N", 0);
}

/* ============================================================================
 * Helper Functions for Int/uint64_t Conversion (C++ only)
 * ============================================================================ */

#ifdef __cplusplus

/**
 * Convert a uint64_t value to an Int* (256-bit integer).
 *
 * Creates a new Int object on the heap initialized with the given 64-bit value.
 * The caller is responsible for freeing the returned Int*.
 *
 * @param value  The 64-bit unsigned integer to convert
 * @return       Newly allocated Int* (caller must free with delete)
 */
Int* uint64_to_int(uint64_t value) {
    return new Int(value);
}

/**
 * Safely convert an Int* to uint64_t, checking for overflow.
 *
 * Extracts the lower 64 bits from an Int value and checks if the higher
 * bits are non-zero (indicating overflow). Returns 0 if the input is NULL.
 *
 * @param value     The Int* to convert (can be NULL)
 * @param overflow  Output flag set to true if value doesn't fit in 64 bits (can be NULL)
 * @return          The lower 64 bits of the Int value (0 if value is NULL)
 *
 * Note: Returns the lower 64 bits even on overflow. Check the overflow flag
 *       to determine if data was lost.
 */
uint64_t int_to_uint64_safe(Int* value, bool* overflow) {
    /* Handle NULL input */
    if (!value) {
        if (overflow) *overflow = true;
        return 0;
    }

    /* Check if higher bits (beyond bits64[0]) are non-zero */
    bool has_overflow = false;
    for (int i = 1; i < NB64BLOCK; i++) {
        if (value->bits64[i] != 0) {
            has_overflow = true;
            break;
        }
    }

    /* Set overflow flag if requested */
    if (overflow) *overflow = has_overflow;

    /* Return lower 64 bits using Int's built-in getter */
    return value->GetInt64();
}

#endif /* __cplusplus */
