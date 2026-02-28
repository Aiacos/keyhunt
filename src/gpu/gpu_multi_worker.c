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
    gpu_worker_stats_t stats;
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
    volatile int should_stop;
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

/* Worker thread main function */
static platform_thread_return_t gpu_worker_thread(void *arg) {
    worker_context_t *ctx = (worker_context_t*)arg;
    gpu_multi_worker_t *manager = ctx->manager;
    int device_id = ctx->device_id;

    /* Update status to running */
    platform_mutex_lock(&manager->stats_lock);
    ctx->stats.status = WORKER_RUNNING;
    platform_mutex_unlock(&manager->stats_lock);

    printf("[Worker %d] Thread started for GPU device %d\n", device_id, device_id);

    /* Main work loop */
    while (!manager->should_stop && !manager->has_result) {
        /* Check if paused */
        if (manager->paused) {
            platform_mutex_lock(&manager->stats_lock);
            ctx->stats.status = WORKER_IDLE;
            platform_mutex_unlock(&manager->stats_lock);

            /* Sleep briefly while paused */
            uint64_t pause_start = get_time_ms();
            while (manager->paused && !manager->should_stop) {
                /* Sleep for 100ms */
                uint64_t now = get_time_ms();
                if (now - pause_start > 100) break;
            }

            platform_mutex_lock(&manager->stats_lock);
            ctx->stats.status = WORKER_RUNNING;
            platform_mutex_unlock(&manager->stats_lock);
            continue;
        }

        /* Get work unit from scheduler */
        uint64_t range_start, range_end;
        if (!multi_gpu_get_work(manager->config.scheduler, device_id,
                                &range_start, &range_end)) {
            /* No more work available */
            break;
        }

        /* Calculate work unit size */
        uint64_t keys_in_batch = range_end - range_start;

        /* Configure GPU search parameters */
        gpu_search_config_t search_config;
        memset(&search_config, 0, sizeof(search_config));

        /* Set key range (convert uint64_t to 32-byte big-endian) */
        memset(search_config.start_key, 0, 32);
        for (int i = 0; i < 8; i++) {
            search_config.start_key[31 - i] = (range_start >> (i * 8)) & 0xFF;
        }

        memset(search_config.end_key, 0, 32);
        for (int i = 0; i < 8; i++) {
            search_config.end_key[31 - i] = (range_end >> (i * 8)) & 0xFF;
        }

        /* Set stride to 1 */
        memset(search_config.stride, 0, 32);
        search_config.stride[31] = 1;

        /* Use targets and config from manager config */
        search_config.targets = manager->config.scheduler ? NULL : NULL;  /* TODO: Get from config */
        search_config.target_count = 0;  /* TODO: Get from config */
        search_config.search_compressed = 1;
        search_config.search_uncompressed = 0;
        search_config.use_bloom = 0;

        /* Set callback (will be provided by manager config in future) */
        search_config.callback = NULL;  /* TODO: Get from config */
        search_config.callback_userdata = ctx;

        /* Statistics tracking */
        search_config.keys_checked = NULL;  /* Worker tracks separately */
        search_config.should_stop = (volatile int*)&manager->should_stop;
        search_config.quiet = 1;  /* Suppress GPU progress output */

        /* Execute GPU search */
        uint64_t work_start = get_time_ms();
        int found_count = gpu_full_search(&search_config);
        uint64_t work_elapsed = get_time_ms() - work_start;

        /* Check for errors */
        if (found_count < 0) {
            platform_mutex_lock(&manager->stats_lock);
            ctx->stats.error_count++;
            snprintf(ctx->stats.last_error, sizeof(ctx->stats.last_error),
                    "GPU search failed with code %d", found_count);
            platform_mutex_unlock(&manager->stats_lock);

            fprintf(stderr, "[Worker %d] GPU search failed: %s\n",
                   device_id, ctx->stats.last_error);

            /* Auto-restart if configured */
            if (!manager->config.auto_restart) {
                break;
            }
            continue;
        }

        /* Update worker statistics */
        platform_mutex_lock(&manager->stats_lock);

        ctx->stats.keys_processed += keys_in_batch;
        ctx->stats.total_elapsed_ms += work_elapsed;
        ctx->stats.work_units_completed++;

        /* Calculate throughput (Mkeys/s) */
        if (work_elapsed > 0) {
            double mkeys = (double)keys_in_batch / 1000000.0;
            double seconds = (double)work_elapsed / 1000.0;
            double throughput = mkeys / seconds;

            /* Exponential moving average */
            if (ctx->stats.current_throughput == 0.0) {
                ctx->stats.current_throughput = throughput;
            } else {
                ctx->stats.current_throughput =
                    (ctx->stats.current_throughput * 0.7) + (throughput * 0.3);
            }

            /* Update peak */
            if (throughput > ctx->stats.peak_throughput) {
                ctx->stats.peak_throughput = throughput;
            }
        }

        platform_mutex_unlock(&manager->stats_lock);

        /* Report work completion to scheduler */
        multi_gpu_report_work(manager->config.scheduler, device_id,
                             keys_in_batch, work_elapsed);

        /* Check if key was found */
        if (found_count > 0) {
            manager->has_result = true;
            printf("[Worker %d] Found %d key(s)! Signaling completion.\n",
                   device_id, found_count);
            break;
        }
    }

    /* Update status to stopped */
    platform_mutex_lock(&manager->stats_lock);
    ctx->stats.status = WORKER_STOPPED;
    platform_mutex_unlock(&manager->stats_lock);

    printf("[Worker %d] Thread stopped. Processed %llu keys in %llu ms (%.2f Mkeys/s avg)\n",
           device_id,
           (unsigned long long)ctx->stats.keys_processed,
           (unsigned long long)ctx->stats.total_elapsed_ms,
           ctx->stats.current_throughput);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

bool gpu_worker_start(gpu_multi_worker_t *worker) {
    if (!worker) {
        fprintf(stderr, "[Worker] Error: NULL worker handle\n");
        return false;
    }

    /* Check if already running */
    if (worker->global_status == WORKER_RUNNING) {
        fprintf(stderr, "[Worker] Error: Workers already started\n");
        return false;
    }

    printf("[Worker] Starting %d GPU worker threads...\n", worker->worker_count);

    /* Spawn one thread per GPU */
    int started_count = 0;
    for (int i = 0; i < worker->worker_count; i++) {
        worker_context_t *ctx = &worker->workers[i];

        /* Create and start thread */
        int result = platform_thread_create(&ctx->thread, gpu_worker_thread, ctx);
        if (result != 0) {
            fprintf(stderr, "[Worker] Error: Failed to create thread for GPU %d (error %d)\n",
                    ctx->device_id, result);

            /* Stop already-started threads before returning */
            if (started_count > 0) {
                fprintf(stderr, "[Worker] Stopping %d already-started workers...\n", started_count);
                worker->should_stop = true;

                /* Wait for threads to stop */
                for (int j = 0; j < i; j++) {
                    if (worker->workers[j].thread_started) {
                        platform_thread_join(worker->workers[j].thread, NULL);
                        worker->workers[j].thread_started = false;
                    }
                }
            }

            return false;
        }

        /* Mark thread as started */
        ctx->thread_started = true;
        started_count++;

        printf("[Worker]   Started worker thread for GPU %d\n", ctx->device_id);
    }

    /* Update global status */
    worker->global_status = WORKER_RUNNING;

    printf("[Worker] All %d worker threads started successfully\n", worker->worker_count);
    return true;
}

bool gpu_worker_stop(gpu_multi_worker_t *worker, uint64_t timeout_ms) {
    if (!worker) {
        fprintf(stderr, "[Worker] Error: NULL worker handle\n");
        return false;
    }

    /* Check if already stopped */
    if (worker->global_status == WORKER_STOPPED ||
        worker->global_status == WORKER_IDLE) {
        printf("[Worker] Workers already stopped\n");
        return true;
    }

    printf("[Worker] Stopping %d worker threads...\n", worker->worker_count);

    /* Signal all workers to stop */
    worker->should_stop = true;
    worker->global_status = WORKER_STOPPING;

    /* Wait for all threads to finish */
    uint64_t stop_start = get_time_ms();
    bool all_stopped = true;

    for (int i = 0; i < worker->worker_count; i++) {
        worker_context_t *ctx = &worker->workers[i];

        /* Skip if thread was never started */
        if (!ctx->thread_started) {
            continue;
        }

        /* Check timeout if specified */
        if (timeout_ms > 0) {
            uint64_t elapsed = get_time_ms() - stop_start;
            if (elapsed >= timeout_ms) {
                fprintf(stderr, "[Worker] Timeout waiting for worker %d to stop\n",
                        ctx->device_id);
                all_stopped = false;
                continue;
            }
        }

        /* Wait for thread to terminate */
        int result = platform_thread_join(ctx->thread, NULL);
        if (result != 0) {
            fprintf(stderr, "[Worker] Error joining thread for GPU %d (error %d)\n",
                    ctx->device_id, result);
            all_stopped = false;
        } else {
            ctx->thread_started = false;
            printf("[Worker]   Stopped worker thread for GPU %d\n", ctx->device_id);
        }
    }

    /* Update global status */
    worker->global_status = WORKER_STOPPED;

    if (all_stopped) {
        printf("[Worker] All worker threads stopped successfully\n");
    } else {
        fprintf(stderr, "[Worker] Some worker threads failed to stop cleanly\n");
    }

    return all_stopped;
}

void gpu_worker_shutdown(gpu_multi_worker_t *worker) {
    if (!worker) {
        return;
    }

    printf("[Worker] Shutting down worker manager...\n");

    /* Stop workers if still running */
    if (worker->global_status == WORKER_RUNNING) {
        gpu_worker_stop(worker, 5000);  /* 5 second timeout */
    }

    /* Destroy synchronization primitives */
    platform_mutex_destroy(&worker->stats_lock);

    /* Free worker manager memory */
    free(worker);

    printf("[Worker] Worker manager shutdown complete\n");
}

bool gpu_worker_has_result(const gpu_multi_worker_t *worker) {
    if (!worker) {
        return false;
    }
    return worker->has_result;
}

void gpu_worker_get_stats(const gpu_multi_worker_t *worker,
                          multi_gpu_worker_stats_t *stats) {
    if (!worker || !stats) {
        return;
    }

    /* Clear output structure */
    memset(stats, 0, sizeof(*stats));

    /* Lock for thread-safe access */
    platform_mutex_lock((platform_mutex_t*)&((gpu_multi_worker_t*)worker)->stats_lock);

    /* Copy aggregate statistics */
    stats->active_workers = worker->worker_count;
    stats->total_keys_processed = 0;
    stats->combined_throughput = 0.0;

    /* Copy per-worker statistics and calculate totals */
    for (int i = 0; i < worker->worker_count; i++) {
        stats->workers[i] = worker->workers[i].stats;
        stats->total_keys_processed += worker->workers[i].stats.keys_processed;
        stats->combined_throughput += worker->workers[i].stats.current_throughput;
    }

    platform_mutex_unlock((platform_mutex_t*)&((gpu_multi_worker_t*)worker)->stats_lock);
}

bool gpu_worker_get_device_stats(const gpu_multi_worker_t *worker,
                                 int device_id,
                                 gpu_worker_stats_t *stats) {
    if (!worker || !stats) {
        return false;
    }

    platform_mutex_lock((platform_mutex_t*)&((gpu_multi_worker_t*)worker)->stats_lock);

    /* Find worker with matching device_id */
    bool found = false;
    for (int i = 0; i < worker->worker_count; i++) {
        if (worker->workers[i].device_id == device_id) {
            *stats = worker->workers[i].stats;
            found = true;
            break;
        }
    }

    platform_mutex_unlock((platform_mutex_t*)&((gpu_multi_worker_t*)worker)->stats_lock);

    return found;
}

bool gpu_worker_pause(gpu_multi_worker_t *worker) {
    if (!worker) {
        fprintf(stderr, "[Worker] Error: NULL worker handle\n");
        return false;
    }

    /* Check if already paused */
    if (worker->paused) {
        printf("[Worker] Workers already paused\n");
        return true;
    }

    /* Check if workers are running */
    if (worker->global_status != WORKER_RUNNING) {
        fprintf(stderr, "[Worker] Error: Cannot pause - workers not running\n");
        return false;
    }

    printf("[Worker] Pausing %d worker threads...\n", worker->worker_count);

    /* Signal workers to pause */
    worker->paused = true;

    /* Wait briefly for workers to enter paused state */
    uint64_t pause_start = get_time_ms();
    bool all_paused = false;

    while (get_time_ms() - pause_start < 1000) {  /* 1 second timeout */
        platform_mutex_lock(&worker->stats_lock);

        /* Check if all workers are idle (paused) */
        all_paused = true;
        for (int i = 0; i < worker->worker_count; i++) {
            if (worker->workers[i].stats.status != WORKER_IDLE) {
                all_paused = false;
                break;
            }
        }

        platform_mutex_unlock(&worker->stats_lock);

        if (all_paused) {
            break;
        }

        /* Sleep briefly before rechecking */
        uint64_t now = get_time_ms();
        if (now - pause_start > 50) break;  /* Check every 50ms */
    }

    if (all_paused) {
        printf("[Worker] All worker threads paused\n");
    } else {
        printf("[Worker] Workers pausing (may take a moment to complete current work)\n");
    }

    return true;
}

bool gpu_worker_resume(gpu_multi_worker_t *worker) {
    if (!worker) {
        fprintf(stderr, "[Worker] Error: NULL worker handle\n");
        return false;
    }

    /* Check if actually paused */
    if (!worker->paused) {
        printf("[Worker] Workers already running\n");
        return true;
    }

    /* Check if workers are in a runnable state */
    if (worker->global_status != WORKER_RUNNING) {
        fprintf(stderr, "[Worker] Error: Cannot resume - workers not in running state\n");
        return false;
    }

    printf("[Worker] Resuming %d worker threads...\n", worker->worker_count);

    /* Clear pause flag */
    worker->paused = false;

    printf("[Worker] Worker threads resumed\n");
    return true;
}
