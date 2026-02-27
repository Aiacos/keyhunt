// src/database/perfdb.h
#ifndef PERFDB_H
#define PERFDB_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "core/sysinfo.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PERFDB_DIR "~/.keyhunt"
#define PERFDB_FILENAME "performance.db"

// Benchmark result structure for database storage
typedef struct {
    // Search configuration
    char mode[32];              // 'address', 'bsgs', 'xpoint', etc.
    int bits;                   // Puzzle bit size (0 if not applicable)
    char key_type[16];          // 'compressed', 'uncompressed', 'both'

    // Performance metrics (Mkeys/s)
    double cpu_speed_mkeys;
    double gpu_speed_mkeys;
    double hybrid_speed_mkeys;
    double efficiency_ratio;    // hybrid / (cpu + gpu)

    // Benchmark metadata
    int benchmark_duration_seconds;
    char keyhunt_version[32];
    char notes[256];

    // Hardware context (copied from system_info_t)
    system_info_t hardware;

    // Computed hash for hardware fingerprint
    char hardware_hash[65];     // SHA256 hex string (64 chars + null)
} perfdb_benchmark_t;

// Query result structure
typedef struct {
    int id;
    time_t timestamp;
    char mode[32];
    double cpu_speed_mkeys;
    double gpu_speed_mkeys;
    double hybrid_speed_mkeys;
    char cpu_model[128];
    char gpu_name[128];
} perfdb_result_t;

// Trend statistics structure
typedef struct {
    int count;                  // Number of benchmarks
    double avg_cpu_speed;
    double avg_gpu_speed;
    double avg_hybrid_speed;
    double min_cpu_speed;
    double max_cpu_speed;
    double recent_cpu_speed;    // Most recent
    double historical_avg;      // Average of older results
    double change_percent;      // Recent vs historical
} perfdb_stats_t;

// Initialize database (creates directory and database file if needed)
int perfdb_init(void);

// Save benchmark result to database
int perfdb_save_benchmark(const perfdb_benchmark_t *benchmark);

// Query recent benchmarks (most recent N results)
// Returns number of results found, fills results array (max_results size)
int perfdb_query_recent(perfdb_result_t *results, int max_results, const char *mode);

// Get statistics for a specific mode and hardware
// Returns 0 on success, -1 on error
int perfdb_get_stats(perfdb_stats_t *stats, const char *mode, const char *hardware_hash);

// Detect performance regression
// Returns: 1 if regression detected, 0 if no regression, -1 on error
// threshold_percent: regression threshold (e.g., -10.0 for 10% slowdown)
int perfdb_detect_regression(const char *mode, const char *hardware_hash,
                             double threshold_percent, double *actual_change);

// Get database file path
void perfdb_get_filepath(char *path, size_t path_size);

// Check if database exists
bool perfdb_exists(void);

// Compute hardware fingerprint (SHA256 hash of hardware characteristics)
void perfdb_compute_hardware_hash(const system_info_t *hardware, char *hash_out, size_t hash_size);

// Execute schema.sql to create tables (called by perfdb_init)
int perfdb_create_schema(void);

// List all benchmarks (for debugging/CLI display)
// Returns number of results
int perfdb_list_all(perfdb_result_t *results, int max_results);

// Delete old benchmarks (keep only last N days)
int perfdb_cleanup_old(int keep_days);

#ifdef __cplusplus
}
#endif

#endif // PERFDB_H
