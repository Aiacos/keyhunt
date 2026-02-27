// src/database/anonymize.h
#ifndef ANONYMIZE_H
#define ANONYMIZE_H

#include <stdint.h>
#include <stdbool.h>
#include "perfdb.h"
#include "core/sysinfo.h"

#ifdef __cplusplus
extern "C" {
#endif

// Anonymization levels
typedef enum {
    ANONYMIZE_NONE = 0,     // No anonymization (local use only)
    ANONYMIZE_MINIMAL = 1,  // Hash identifying strings, keep precise numbers
    ANONYMIZE_FULL = 2      // Hash identifiers + quantize numbers for max privacy
} anonymize_level_t;

// Anonymized benchmark result (safe for community submission)
typedef struct {
    // Search configuration (unchanged)
    char mode[32];              // 'address', 'bsgs', 'xpoint', etc.
    int bits;                   // Puzzle bit size
    char key_type[16];          // 'compressed', 'uncompressed', 'both'

    // Performance metrics (unchanged - not identifying)
    double cpu_speed_mkeys;
    double gpu_speed_mkeys;
    double hybrid_speed_mkeys;
    double efficiency_ratio;

    // Benchmark metadata (sanitized)
    int benchmark_duration_seconds;
    char keyhunt_version[32];   // Kept for compatibility analysis

    // Anonymized hardware context
    char cpu_family[64];        // e.g., "intel_core_i7_gen12" instead of exact model
    int cpu_physical_cores;     // Kept (not identifying)
    int cpu_logical_cores;      // Kept
    bool has_avx2;              // Feature flags kept (not identifying)
    bool has_avx512;
    bool has_avx512f;
    bool has_sha_ni;

    // Quantized values (FULL mode only)
    int ram_gb_bucket;          // e.g., 16, 32, 64 instead of exact MB
    int cache_l3_mb_bucket;     // e.g., 8, 16, 32 instead of exact KB

    // GPU info (anonymized)
    char gpu_family[64];        // e.g., "nvidia_ampere_ga102" instead of "RTX 3080"
    int gpu_sm_count;           // Kept (architectural detail, not serial)
    int gpu_compute_capability; // e.g., 86 for sm_86

    // Privacy-preserving hardware fingerprint
    char anonymized_hash[65];   // Different from hardware_hash, groups similar configs

    // Timestamp handling (no exact time)
    char submission_date[11];   // YYYY-MM-DD only (no time component)
    int days_since_first_run;   // Relative timeline instead of absolute timestamps
} anonymized_benchmark_t;

// Anonymize a benchmark result for community submission
// level: MINIMAL (hash models) or FULL (hash + quantize)
// Returns: 0 on success, -1 on error
int anonymize_benchmark_result(const perfdb_benchmark_t *original,
                               anonymized_benchmark_t *anonymized,
                               anonymize_level_t level);

// Create CPU family string from exact model name
// Input: "Intel(R) Core(TM) i7-12700K CPU @ 3.60GHz"
// Output: "intel_core_i7_gen12" (MINIMAL) or "intel_core_i7" (FULL)
void anonymize_cpu_model(const char *cpu_model, char *cpu_family,
                        size_t family_size, anonymize_level_t level);

// Create GPU family string from exact model name
// Input: "NVIDIA GeForce RTX 3080"
// Output: "nvidia_ampere_ga102" (MINIMAL) or "nvidia_ampere" (FULL)
void anonymize_gpu_model(const char *gpu_name, int compute_capability,
                        char *gpu_family, size_t family_size,
                        anonymize_level_t level);

// Compute anonymized hardware hash (groups similar configurations)
// Uses CPU family + quantized specs, NOT exact model strings
void anonymize_compute_hardware_hash(const anonymized_benchmark_t *anon,
                                    char *hash_out, size_t hash_size);

// Quantize RAM to power-of-2 buckets for privacy
// 15000 MB → 16 GB bucket, 31000 MB → 32 GB bucket
int anonymize_quantize_ram(uint64_t ram_mb);

// Quantize L3 cache to standard buckets for privacy
// 12 MB → 12 MB bucket, 20 MB → 16 MB bucket
int anonymize_quantize_cache_l3(uint64_t cache_kb);

// Validate anonymized result doesn't leak identifying info
// Returns: true if safe for submission, false if privacy risk detected
bool anonymize_validate_privacy(const anonymized_benchmark_t *anon);

// Export anonymized result to JSON (for community submission file)
// Returns: 0 on success, -1 on error
int anonymize_export_json(const anonymized_benchmark_t *anon,
                          char *json_out, size_t json_size);

#ifdef __cplusplus
}
#endif

#endif // ANONYMIZE_H
