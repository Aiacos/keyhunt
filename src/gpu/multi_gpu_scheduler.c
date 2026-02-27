/*
 * multi_gpu_scheduler.c - Multi-GPU Work Distribution
 *
 * Implements dynamic work scheduling across multiple GPUs.
 */

#include "multi_gpu_scheduler.h"
#include "gpu_backend.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Internal scheduler structure */
struct multi_gpu_scheduler_s {
    multi_gpu_config_t config;
    multi_gpu_state_t state;

    /* Work distribution */
    uint64_t total_start;
    uint64_t total_end;
    uint64_t next_chunk_start;
    uint64_t base_chunk_size;

    /* Synchronization */
    platform_mutex_t lock;
    int running;
};

/* Get current time in milliseconds */
static uint64_t get_time_ms(void) {
    return platform_time_now_ns() / 1000000ULL;
}

int multi_gpu_available(void) {
    gpu_backend_info_t info;
    if (gpu_backend_init(&info) == 0) {
        return info.gpu_count;
    }
    return 0;
}

multi_gpu_scheduler_t* multi_gpu_init(const multi_gpu_config_t *config) {
    multi_gpu_scheduler_t *sched = (multi_gpu_scheduler_t*)calloc(1, sizeof(*sched));
    if (!sched) return NULL;

    platform_mutex_init(&sched->lock);

    /* Copy or default config */
    if (config) {
        sched->config = *config;
    } else {
        sched->config.device_count = -1;  /* Auto-detect */
        sched->config.adaptive_balancing = true;
        sched->config.rebalance_interval_keys = 100000000ULL;  /* 100M keys */
    }

    /* Detect GPUs */
    gpu_backend_info_t info;
    if (gpu_backend_init(&info) != 0 || info.gpu_count == 0) {
        free(sched);
        return NULL;
    }

    int count = (sched->config.device_count < 0) ?
                info.gpu_count : sched->config.device_count;
    if (count > MULTI_GPU_MAX_DEVICES) count = MULTI_GPU_MAX_DEVICES;

    sched->state.active_count = count;

    /* Initialize device info */
    for (int i = 0; i < count; i++) {
        multi_gpu_device_t *dev = &sched->state.devices[i];
        dev->device_id = (sched->config.device_count < 0) ? i : sched->config.device_ids[i];
        strncpy(dev->name, info.name, sizeof(dev->name) - 1);
        dev->vram_mb = info.vram_mb;
        dev->performance_score = 1.0;
        dev->current_allocation = 1.0 / count;
    }

    sched->base_chunk_size = 1ULL << 24;  /* 16M keys base chunk */
    sched->running = 1;

    printf("[Multi-GPU] Initialized scheduler with %d GPUs\n", count);
    return sched;
}

void multi_gpu_get_state(const multi_gpu_scheduler_t *sched, multi_gpu_state_t *state) {
    if (!sched || !state) return;
    platform_mutex_lock((platform_mutex_t*)&((multi_gpu_scheduler_t*)sched)->lock);
    *state = sched->state;
    platform_mutex_unlock((platform_mutex_t*)&((multi_gpu_scheduler_t*)sched)->lock);
}

void multi_gpu_set_range(multi_gpu_scheduler_t *sched, uint64_t total_start, uint64_t total_end) {
    if (!sched) return;
    platform_mutex_lock(&sched->lock);
    sched->total_start = total_start;
    sched->total_end = total_end;
    sched->next_chunk_start = total_start;
    platform_mutex_unlock(&sched->lock);
}

bool multi_gpu_get_work(multi_gpu_scheduler_t *sched, int device_id,
                        uint64_t *range_start, uint64_t *range_end) {
    if (!sched || !range_start || !range_end) return false;

    platform_mutex_lock(&sched->lock);

    if (sched->next_chunk_start >= sched->total_end || !sched->running) {
        platform_mutex_unlock(&sched->lock);
        return false;
    }

    /* Find device and calculate chunk size based on performance */
    double allocation = 1.0 / sched->state.active_count;
    for (int i = 0; i < sched->state.active_count; i++) {
        if (sched->state.devices[i].device_id == device_id) {
            allocation = sched->state.devices[i].current_allocation;
            break;
        }
    }

    uint64_t chunk_size = (uint64_t)(sched->base_chunk_size * allocation * sched->state.active_count);
    if (chunk_size < sched->base_chunk_size / 4) chunk_size = sched->base_chunk_size / 4;

    *range_start = sched->next_chunk_start;
    *range_end = sched->next_chunk_start + chunk_size;
    if (*range_end > sched->total_end) *range_end = sched->total_end;

    sched->next_chunk_start = *range_end;

    platform_mutex_unlock(&sched->lock);
    return true;
}

void multi_gpu_report_work(multi_gpu_scheduler_t *sched, int device_id,
                           uint64_t keys_processed, uint64_t elapsed_ms) {
    if (!sched || elapsed_ms == 0) return;

    platform_mutex_lock(&sched->lock);

    double mkeys = (double)keys_processed / 1000000.0;
    double throughput = mkeys / ((double)elapsed_ms / 1000.0);

    for (int i = 0; i < sched->state.active_count; i++) {
        if (sched->state.devices[i].device_id == device_id) {
            multi_gpu_device_t *dev = &sched->state.devices[i];
            dev->keys_processed += keys_processed;
            /* Exponential moving average */
            dev->avg_throughput = (dev->avg_throughput * 0.7) + (throughput * 0.3);
            break;
        }
    }

    sched->state.total_keys_processed += keys_processed;

    /* Recalculate total throughput */
    double total = 0;
    for (int i = 0; i < sched->state.active_count; i++) {
        total += sched->state.devices[i].avg_throughput;
    }
    sched->state.total_throughput = total;

    /* Rebalance if adaptive */
    if (sched->config.adaptive_balancing && total > 0) {
        for (int i = 0; i < sched->state.active_count; i++) {
            sched->state.devices[i].current_allocation =
                sched->state.devices[i].avg_throughput / total;
        }
    }

    platform_mutex_unlock(&sched->lock);
}

void multi_gpu_rebalance(multi_gpu_scheduler_t *sched) {
    if (!sched) return;
    /* Rebalancing happens automatically in report_work when adaptive */
    platform_mutex_lock(&sched->lock);
    sched->state.last_rebalance_time = get_time_ms();
    platform_mutex_unlock(&sched->lock);
}

void multi_gpu_shutdown(multi_gpu_scheduler_t *sched) {
    if (!sched) return;

    platform_mutex_lock(&sched->lock);
    sched->running = 0;
    platform_mutex_unlock(&sched->lock);

    platform_mutex_destroy(&sched->lock);
    free(sched);

    printf("[Multi-GPU] Scheduler shutdown\n");
}
