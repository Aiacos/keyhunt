/*
 * gpu_multi_worker.h - Multi-GPU Worker Thread Management
 *
 * Manages worker threads for each GPU, coordinating with the scheduler
 * to fetch work units, execute GPU kernels, and report results.
 */

#ifndef GPU_MULTI_WORKER_H
#define GPU_MULTI_WORKER_H

#include <stdint.h>
#include <stdbool.h>
#include "multi_gpu_scheduler.h"
#include "../platform/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Worker status enumeration */
typedef enum {
    WORKER_IDLE,        /* Worker initialized but not started */
    WORKER_RUNNING,     /* Worker actively processing work */
    WORKER_STOPPING,    /* Worker shutdown requested */
    WORKER_STOPPED,     /* Worker thread terminated */
    WORKER_ERROR        /* Worker encountered fatal error */
} worker_status_t;

/* Per-worker statistics */
typedef struct {
    int device_id;
    worker_status_t status;
    uint64_t keys_processed;
    uint64_t total_elapsed_ms;
    double current_throughput;   /* Mkeys/s (moving average) */
    double peak_throughput;      /* Mkeys/s (max observed) */
    uint64_t work_units_completed;
    uint64_t error_count;
    char last_error[256];
} worker_stats_t;

/* Aggregate multi-worker statistics */
typedef struct {
    int active_workers;
    uint64_t total_keys_processed;
    double combined_throughput;  /* Mkeys/s across all workers */
    worker_stats_t workers[MULTI_GPU_MAX_DEVICES];
} multi_worker_stats_t;

/* Worker configuration */
typedef struct {
    multi_gpu_scheduler_t *scheduler;  /* Scheduler to fetch work from */
    int device_ids[MULTI_GPU_MAX_DEVICES];  /* GPU device IDs to use */
    int device_count;                  /* Number of GPUs to spawn workers for */
    uint64_t batch_size;               /* Keys per work unit (default: 1048576) */
    int priority;                      /* Thread priority (0=normal) */
    bool auto_restart;                 /* Restart worker on non-fatal errors */
} worker_config_t;

/* Opaque worker manager handle */
typedef struct gpu_multi_worker_s gpu_multi_worker_t;

/**
 * Initialize multi-worker manager
 * @param config Worker configuration
 * @return Worker manager handle, or NULL on failure
 *
 * Allocates worker structures and prepares threads, but does not start them.
 * Call gpu_worker_start() to begin processing.
 */
gpu_multi_worker_t* gpu_worker_init(const worker_config_t *config);

/**
 * Start all worker threads
 * @param worker Worker manager handle
 * @return true on success, false if any worker failed to start
 *
 * Spawns a thread for each configured GPU. Threads begin fetching work
 * from the scheduler immediately.
 */
bool gpu_worker_start(gpu_multi_worker_t *worker);

/**
 * Stop all worker threads gracefully
 * @param worker Worker manager handle
 * @param timeout_ms Maximum time to wait for workers to stop (0 = wait indefinitely)
 * @return true if all workers stopped cleanly, false if timeout or errors
 *
 * Signals all workers to stop and waits for them to complete current work.
 * Does not cancel in-progress GPU kernels; workers finish current batch.
 */
bool gpu_worker_stop(gpu_multi_worker_t *worker, uint64_t timeout_ms);

/**
 * Get current statistics from all workers
 * @param worker Worker manager handle
 * @param stats Output statistics structure
 *
 * Thread-safe snapshot of current worker states and performance metrics.
 */
void gpu_worker_get_stats(const gpu_multi_worker_t *worker,
                          multi_worker_stats_t *stats);

/**
 * Get statistics for a specific worker
 * @param worker Worker manager handle
 * @param device_id GPU device ID
 * @param stats Output statistics structure
 * @return true if device_id is valid, false otherwise
 */
bool gpu_worker_get_device_stats(const gpu_multi_worker_t *worker,
                                 int device_id,
                                 worker_stats_t *stats);

/**
 * Pause all workers (suspend work fetching)
 * @param worker Worker manager handle
 * @return true on success, false on failure
 *
 * Workers complete current work unit but do not fetch new work.
 * Call gpu_worker_resume() to continue processing.
 */
bool gpu_worker_pause(gpu_multi_worker_t *worker);

/**
 * Resume paused workers
 * @param worker Worker manager handle
 * @return true on success, false on failure
 */
bool gpu_worker_resume(gpu_multi_worker_t *worker);

/**
 * Check if any worker has found a result
 * @param worker Worker manager handle
 * @return true if a key was found by any worker
 *
 * Used for early termination when a solution is discovered.
 */
bool gpu_worker_has_result(const gpu_multi_worker_t *worker);

/**
 * Shutdown worker manager and release all resources
 * @param worker Worker manager handle
 *
 * Stops all workers (if running), joins threads, and frees memory.
 * Handle becomes invalid after this call.
 */
void gpu_worker_shutdown(gpu_multi_worker_t *worker);

/**
 * Create default worker configuration
 * @param scheduler Scheduler instance to use
 * @param device_count Number of GPUs (-1 = use all from scheduler)
 * @return Default configuration structure
 */
worker_config_t gpu_worker_default_config(multi_gpu_scheduler_t *scheduler,
                                          int device_count);

#ifdef __cplusplus
}
#endif

#endif /* GPU_MULTI_WORKER_H */
