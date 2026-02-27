/*
 * gpu_multi_worker.c - Multi-GPU Worker Thread Management
 *
 * Manages worker threads for each GPU, coordinating with the scheduler
 * to fetch work units, execute GPU kernels, and report results.
 */

#include "gpu_multi_worker.h"
#include "gpu_backend.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per-worker context (one per GPU) */
typedef struct {
    int device_id;
    platform_thread_t thread;
    worker_stats_t stats;
    gpu_multi_worker_t *manager;  /* Back-reference to manager */
    bool thread_started;
} worker_context_t;

/* Internal worker manager structure */
struct gpu_multi_worker_s {
    worker_config_t config;
    worker_context_t workers[MULTI_GPU_MAX_DEVICES];
    int worker_count;

    /* Synchronization */
    platform_mutex_t stats_lock;
    volatile bool should_stop;
    volatile bool paused;
    volatile bool has_result;

    /* Status tracking */
    worker_status_t global_status;
};

/* Get current time in milliseconds */
static uint64_t get_time_ms(void) {
    return platform_time_now_ns() / 1000000ULL;
}

gpu_multi_worker_t* gpu_worker_init(const worker_config_t *config) {
    if (!config || !config->scheduler) {
        fprintf(stderr, "[Worker] Error: Invalid configuration (NULL config or scheduler)\n");
        return NULL;
    }

    if (config->device_count <= 0 || config->device_count > MULTI_GPU_MAX_DEVICES) {
        fprintf(stderr, "[Worker] Error: Invalid device_count %d (must be 1-%d)\n",
                config->device_count, MULTI_GPU_MAX_DEVICES);
        return NULL;
    }

    /* Allocate worker manager */
    gpu_multi_worker_t *worker = (gpu_multi_worker_t*)calloc(1, sizeof(*worker));
    if (!worker) {
        fprintf(stderr, "[Worker] Error: Failed to allocate worker manager\n");
        return NULL;
    }

    /* Initialize mutex */
    if (platform_mutex_init(&worker->stats_lock) != 0) {
        fprintf(stderr, "[Worker] Error: Failed to initialize mutex\n");
        free(worker);
        return NULL;
    }

    /* Copy configuration */
    worker->config = *config;
    worker->worker_count = config->device_count;
    worker->should_stop = false;
    worker->paused = false;
    worker->has_result = false;
    worker->global_status = WORKER_IDLE;

    /* Initialize per-GPU worker contexts */
    for (int i = 0; i < worker->worker_count; i++) {
        worker_context_t *ctx = &worker->workers[i];

        /* Set device ID */
        ctx->device_id = config->device_ids[i];
        ctx->manager = worker;
        ctx->thread_started = false;

        /* Initialize worker statistics */
        ctx->stats.device_id = ctx->device_id;
        ctx->stats.status = WORKER_IDLE;
        ctx->stats.keys_processed = 0;
        ctx->stats.total_elapsed_ms = 0;
        ctx->stats.current_throughput = 0.0;
        ctx->stats.peak_throughput = 0.0;
        ctx->stats.work_units_completed = 0;
        ctx->stats.error_count = 0;
        ctx->stats.last_error[0] = '\0';
    }

    printf("[Worker] Initialized with %d GPU workers\n", worker->worker_count);
    for (int i = 0; i < worker->worker_count; i++) {
        printf("[Worker]   Worker %d: GPU device %d\n", i, worker->workers[i].device_id);
    }

    return worker;
}

worker_config_t gpu_worker_default_config(multi_gpu_scheduler_t *scheduler,
                                          int device_count) {
    worker_config_t config;
    memset(&config, 0, sizeof(config));

    config.scheduler = scheduler;
    config.device_count = device_count;
    config.batch_size = 1048576;  /* 1M keys default */
    config.priority = 0;
    config.auto_restart = false;

    /* Get device IDs from scheduler if device_count == -1 (auto-detect) */
    if (device_count < 0 && scheduler) {
        multi_gpu_state_t state;
        multi_gpu_get_state(scheduler, &state);
        config.device_count = state.active_count;
        for (int i = 0; i < state.active_count && i < MULTI_GPU_MAX_DEVICES; i++) {
            config.device_ids[i] = state.devices[i].device_id;
        }
    } else {
        /* Use sequential device IDs by default */
        for (int i = 0; i < device_count && i < MULTI_GPU_MAX_DEVICES; i++) {
            config.device_ids[i] = i;
        }
    }

    return config;
}
