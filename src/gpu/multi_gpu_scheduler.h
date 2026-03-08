/*
 * multi_gpu_scheduler.h - Multi-GPU Work Distribution
 *
 * Dynamically schedules work across multiple GPUs based on their
 * relative performance, with support for heterogeneous GPU setups.
 */

#ifndef MULTI_GPU_SCHEDULER_H
#define MULTI_GPU_SCHEDULER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum supported GPUs — kept in sync with GPU_MAX_DEVICES from config.h */
#ifdef GPU_MAX_DEVICES
#define MULTI_GPU_MAX_DEVICES GPU_MAX_DEVICES
#else
#define MULTI_GPU_MAX_DEVICES 16
#endif

/* Per-GPU statistics */
typedef struct {
    int device_id;
    char name[128];
    uint64_t vram_mb;
    double performance_score;    /* Relative performance (1.0 = baseline) */
    uint64_t keys_processed;
    double avg_throughput;       /* Mkeys/s */
    double current_allocation;   /* Fraction of work assigned (0.0 - 1.0) */
} multi_gpu_device_t;

/* Scheduler configuration */
typedef struct {
    int device_count;            /* Number of GPUs to use (-1 = auto-detect all) */
    int device_ids[MULTI_GPU_MAX_DEVICES];  /* Specific device IDs to use */
    bool adaptive_balancing;     /* Dynamically adjust based on throughput */
    uint64_t rebalance_interval_keys;  /* Keys between rebalancing (0 = never) */
} multi_gpu_config_t;

/* Scheduler state */
typedef struct {
    multi_gpu_device_t devices[MULTI_GPU_MAX_DEVICES];
    int active_count;
    uint64_t total_keys_processed;
    double total_throughput;     /* Combined Mkeys/s */
    uint64_t last_rebalance_time;
} multi_gpu_state_t;

/* Opaque scheduler handle */
typedef struct multi_gpu_scheduler_s multi_gpu_scheduler_t;

/**
 * Initialize multi-GPU scheduler
 * @param config Configuration (NULL for auto-detection of all GPUs)
 * @return Scheduler handle, or NULL on failure
 */
multi_gpu_scheduler_t* multi_gpu_init(const multi_gpu_config_t *config);

/**
 * Get current scheduler state
 * @param sched Scheduler handle
 * @param state Output state structure
 */
void multi_gpu_get_state(multi_gpu_scheduler_t *sched,
                         multi_gpu_state_t *state);

/**
 * Allocate work range for a specific GPU
 * @param sched Scheduler handle
 * @param device_id GPU device ID
 * @param range_start Output: start of assigned range
 * @param range_end Output: end of assigned range (exclusive)
 * @return true if work assigned, false if no more work
 */
bool multi_gpu_get_work(multi_gpu_scheduler_t *sched, int device_id,
                        uint64_t *range_start, uint64_t *range_end);

/**
 * Report completed work from a GPU
 * @param sched Scheduler handle
 * @param device_id GPU device ID
 * @param keys_processed Number of keys processed
 * @param elapsed_ms Time taken in milliseconds
 */
void multi_gpu_report_work(multi_gpu_scheduler_t *sched, int device_id,
                           uint64_t keys_processed, uint64_t elapsed_ms);

/**
 * Set the total work range to distribute
 * @param sched Scheduler handle
 * @param total_start Start of total range
 * @param total_end End of total range (exclusive)
 */
void multi_gpu_set_range(multi_gpu_scheduler_t *sched,
                         uint64_t total_start, uint64_t total_end);

/**
 * Trigger rebalancing of work allocation
 * @param sched Scheduler handle
 */
void multi_gpu_rebalance(multi_gpu_scheduler_t *sched);

/**
 * Shutdown scheduler and release resources
 * @param sched Scheduler handle
 */
void multi_gpu_shutdown(multi_gpu_scheduler_t *sched);

/**
 * Check if multi-GPU support is available
 * @return Number of GPUs available, 0 if none
 */
int multi_gpu_available(void);

#ifdef __cplusplus
}
#endif

#endif /* MULTI_GPU_SCHEDULER_H */
