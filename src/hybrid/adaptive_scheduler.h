/*
 * adaptive_scheduler.h - Dynamic work distribution for hybrid CPU+GPU mode
 *
 * Monitors throughput of CPU and GPU workers and dynamically adjusts
 * chunk sizes to maximize overall throughput.
 */

#ifndef ADAPTIVE_SCHEDULER_H
#define ADAPTIVE_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration constants */
#define ADAPTIVE_UPDATE_INTERVAL_MS  1000   /* Update ratios every 1 second */
#define ADAPTIVE_MIN_CPU_RATIO       0.05f  /* Minimum 5% work to CPU */
#define ADAPTIVE_MIN_GPU_RATIO       0.05f  /* Minimum 5% work to GPU */
#define ADAPTIVE_BASE_CHUNK_SIZE     0x100000ULL  /* 1M keys base chunk */

/* Worker types */
typedef enum {
    WORKER_CPU = 0,
    WORKER_GPU = 1,
    WORKER_TYPE_COUNT
} worker_type_t;

/* Per-worker statistics */
typedef struct {
    uint64_t keys_checked;      /* Total keys checked by this worker */
    uint64_t chunks_completed;  /* Number of chunks completed */
    double last_mkeys_sec;      /* Last measured throughput (Mkeys/s) */
    double avg_mkeys_sec;       /* Rolling average throughput */
    uint64_t chunk_size;        /* Current chunk size for this worker */
} worker_stats_t;

/* Main scheduler structure */
typedef struct {
    /* Statistics per worker type */
    worker_stats_t stats[WORKER_TYPE_COUNT];

    /* Computed ratios */
    float cpu_ratio;            /* Fraction of work for CPU (0.0-1.0) */
    float gpu_ratio;            /* Fraction of work for GPU (0.0-1.0) */

    /* Work distribution */
    uint64_t total_range_start;
    uint64_t total_range_end;
    uint64_t next_chunk_start;  /* Next chunk to assign */

    /* Timing */
    uint64_t last_update_time_ms;
    uint64_t start_time_ms;

    /* Thread safety */
    pthread_mutex_t lock;
    volatile int update_in_progress;  /* Atomic flag to prevent concurrent updates */

    /* State */
    bool initialized;
    bool enabled;               /* False = static split, True = adaptive */
} adaptive_scheduler_t;

/* Global scheduler instance */
extern adaptive_scheduler_t g_adaptive_scheduler;

/**
 * Initialize the adaptive scheduler
 * @param initial_cpu_ratio Initial CPU work ratio (from sysinfo scores)
 * @param range_start Start of search range
 * @param range_end End of search range
 */
void adaptive_init(float initial_cpu_ratio, uint64_t range_start, uint64_t range_end);

/**
 * Enable/disable adaptive mode
 * When disabled, uses static split based on initial ratios
 */
void adaptive_set_enabled(bool enabled);

/**
 * Request next work chunk for a worker
 * @param worker_type WORKER_CPU or WORKER_GPU
 * @param out_start Output: chunk start
 * @param out_end Output: chunk end
 * @return true if work available, false if range exhausted
 */
bool adaptive_get_chunk(worker_type_t worker_type, uint64_t *out_start, uint64_t *out_end);

/**
 * Report completed work from a worker
 * @param worker_type WORKER_CPU or WORKER_GPU
 * @param keys_checked Number of keys checked in this chunk
 * @param elapsed_ms Time taken for this chunk (milliseconds)
 */
void adaptive_report_work(worker_type_t worker_type, uint64_t keys_checked, uint64_t elapsed_ms);

/**
 * Update ratios based on measured throughput
 * Called periodically (every ADAPTIVE_UPDATE_INTERVAL_MS)
 */
void adaptive_update_ratios(void);

/**
 * Get current statistics for display
 * @param cpu_mkeys Output: CPU throughput in Mkeys/s
 * @param gpu_mkeys Output: GPU throughput in Mkeys/s
 * @param cpu_percent Output: CPU work percentage
 * @param gpu_percent Output: GPU work percentage
 */
void adaptive_get_stats(double *cpu_mkeys, double *gpu_mkeys,
                        int *cpu_percent, int *gpu_percent);

/**
 * Get total progress
 * @return Fraction of range completed (0.0-1.0)
 */
double adaptive_get_progress(void);

/**
 * Get total throughput
 * @return Combined CPU+GPU throughput in Mkeys/s
 */
double adaptive_get_total_throughput(void);

/**
 * Cleanup scheduler resources
 */
void adaptive_cleanup(void);

/**
 * Get current time in milliseconds
 */
uint64_t adaptive_time_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTIVE_SCHEDULER_H */
