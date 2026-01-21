/*
 * adaptive_scheduler.c - Dynamic work distribution for hybrid CPU+GPU mode
 */

#include "adaptive_scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* Global scheduler instance */
adaptive_scheduler_t g_adaptive_scheduler = {0};

/* Get current time in milliseconds */
uint64_t adaptive_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

void adaptive_init(float initial_cpu_ratio, uint64_t range_start, uint64_t range_end) {
    adaptive_scheduler_t *s = &g_adaptive_scheduler;

    pthread_mutex_init(&s->lock, NULL);

    memset(s->stats, 0, sizeof(s->stats));

    /* Clamp initial ratio */
    if (initial_cpu_ratio < ADAPTIVE_MIN_CPU_RATIO) {
        initial_cpu_ratio = ADAPTIVE_MIN_CPU_RATIO;
    }
    if (initial_cpu_ratio > (1.0f - ADAPTIVE_MIN_GPU_RATIO)) {
        initial_cpu_ratio = 1.0f - ADAPTIVE_MIN_GPU_RATIO;
    }

    s->cpu_ratio = initial_cpu_ratio;
    s->gpu_ratio = 1.0f - initial_cpu_ratio;

    s->total_range_start = range_start;
    s->total_range_end = range_end;
    s->next_chunk_start = range_start;

    /* Set initial chunk sizes based on ratios */
    /* GPU gets larger chunks since it's typically faster */
    s->stats[WORKER_CPU].chunk_size = ADAPTIVE_BASE_CHUNK_SIZE;
    s->stats[WORKER_GPU].chunk_size = ADAPTIVE_BASE_CHUNK_SIZE * 4;  /* 4x for GPU */

    s->start_time_ms = adaptive_time_ms();
    s->last_update_time_ms = s->start_time_ms;

    s->initialized = true;
    s->enabled = true;  /* Adaptive by default */

    fprintf(stderr, "[Adaptive] Initialized: CPU=%.1f%%, GPU=%.1f%%\n",
            s->cpu_ratio * 100.0f, s->gpu_ratio * 100.0f);
}

void adaptive_set_enabled(bool enabled) {
    g_adaptive_scheduler.enabled = enabled;
    fprintf(stderr, "[Adaptive] Mode: %s\n", enabled ? "adaptive" : "static");
}

bool adaptive_get_chunk(worker_type_t worker_type, uint64_t *out_start, uint64_t *out_end) {
    adaptive_scheduler_t *s = &g_adaptive_scheduler;

    if (!s->initialized) {
        return false;
    }

    pthread_mutex_lock(&s->lock);

    /* Check if range exhausted */
    if (s->next_chunk_start >= s->total_range_end) {
        pthread_mutex_unlock(&s->lock);
        return false;
    }

    /* Get chunk size for this worker type */
    uint64_t chunk_size = s->stats[worker_type].chunk_size;

    /* Assign chunk */
    *out_start = s->next_chunk_start;
    uint64_t chunk_end = s->next_chunk_start + chunk_size;

    /* Clamp to range end */
    if (chunk_end > s->total_range_end) {
        chunk_end = s->total_range_end;
    }

    *out_end = chunk_end;
    s->next_chunk_start = chunk_end;

    pthread_mutex_unlock(&s->lock);

    return true;
}

void adaptive_report_work(worker_type_t worker_type, uint64_t keys_checked, uint64_t elapsed_ms) {
    adaptive_scheduler_t *s = &g_adaptive_scheduler;

    if (!s->initialized || worker_type >= WORKER_TYPE_COUNT) {
        return;
    }

    pthread_mutex_lock(&s->lock);

    worker_stats_t *ws = &s->stats[worker_type];

    ws->keys_checked += keys_checked;
    ws->chunks_completed++;

    /* Calculate throughput for this chunk */
    if (elapsed_ms > 0) {
        double mkeys = (double)keys_checked / 1000000.0;
        double seconds = (double)elapsed_ms / 1000.0;
        double mkeys_sec = mkeys / seconds;

        /* Update rolling average (exponential moving average) */
        if (ws->avg_mkeys_sec == 0.0) {
            ws->avg_mkeys_sec = mkeys_sec;
        } else {
            /* Alpha = 0.3 for responsiveness */
            ws->avg_mkeys_sec = 0.3 * mkeys_sec + 0.7 * ws->avg_mkeys_sec;
        }
        ws->last_mkeys_sec = mkeys_sec;
    }

    /* Check if it's time to update ratios */
    uint64_t now = adaptive_time_ms();
    if (s->enabled && (now - s->last_update_time_ms) >= ADAPTIVE_UPDATE_INTERVAL_MS) {
        s->last_update_time_ms = now;
        /* Call update without lock (we'll release it first) */
        pthread_mutex_unlock(&s->lock);
        adaptive_update_ratios();
        return;
    }

    pthread_mutex_unlock(&s->lock);
}

void adaptive_update_ratios(void) {
    adaptive_scheduler_t *s = &g_adaptive_scheduler;

    if (!s->initialized || !s->enabled) {
        return;
    }

    pthread_mutex_lock(&s->lock);

    double cpu_throughput = s->stats[WORKER_CPU].avg_mkeys_sec;
    double gpu_throughput = s->stats[WORKER_GPU].avg_mkeys_sec;
    double total = cpu_throughput + gpu_throughput;

    if (total > 0.0) {
        /* Calculate new ratios based on measured throughput */
        float new_gpu_ratio = (float)(gpu_throughput / total);
        float new_cpu_ratio = 1.0f - new_gpu_ratio;

        /* Clamp to minimum ratios */
        if (new_cpu_ratio < ADAPTIVE_MIN_CPU_RATIO) {
            new_cpu_ratio = ADAPTIVE_MIN_CPU_RATIO;
            new_gpu_ratio = 1.0f - new_cpu_ratio;
        }
        if (new_gpu_ratio < ADAPTIVE_MIN_GPU_RATIO) {
            new_gpu_ratio = ADAPTIVE_MIN_GPU_RATIO;
            new_cpu_ratio = 1.0f - new_gpu_ratio;
        }

        /* Smooth transition (don't change too fast) */
        s->cpu_ratio = 0.7f * s->cpu_ratio + 0.3f * new_cpu_ratio;
        s->gpu_ratio = 1.0f - s->cpu_ratio;

        /* Adjust chunk sizes based on throughput */
        /* Faster workers get larger chunks to reduce overhead */
        if (cpu_throughput > 0.0) {
            double cpu_factor = 1.0 + (cpu_throughput / 100.0);  /* Scale with speed */
            s->stats[WORKER_CPU].chunk_size = (uint64_t)(ADAPTIVE_BASE_CHUNK_SIZE * cpu_factor);
            /* Clamp chunk size */
            if (s->stats[WORKER_CPU].chunk_size > 0x10000000ULL) {
                s->stats[WORKER_CPU].chunk_size = 0x10000000ULL;  /* Max 256M */
            }
        }

        if (gpu_throughput > 0.0) {
            double gpu_factor = 1.0 + (gpu_throughput / 100.0);
            s->stats[WORKER_GPU].chunk_size = (uint64_t)(ADAPTIVE_BASE_CHUNK_SIZE * 4 * gpu_factor);
            if (s->stats[WORKER_GPU].chunk_size > 0x100000000ULL) {
                s->stats[WORKER_GPU].chunk_size = 0x100000000ULL;  /* Max 4G */
            }
        }
    }

    pthread_mutex_unlock(&s->lock);
}

void adaptive_get_stats(double *cpu_mkeys, double *gpu_mkeys,
                        int *cpu_percent, int *gpu_percent) {
    adaptive_scheduler_t *s = &g_adaptive_scheduler;

    pthread_mutex_lock(&s->lock);

    if (cpu_mkeys) *cpu_mkeys = s->stats[WORKER_CPU].avg_mkeys_sec;
    if (gpu_mkeys) *gpu_mkeys = s->stats[WORKER_GPU].avg_mkeys_sec;
    if (cpu_percent) *cpu_percent = (int)(s->cpu_ratio * 100.0f);
    if (gpu_percent) *gpu_percent = (int)(s->gpu_ratio * 100.0f);

    pthread_mutex_unlock(&s->lock);
}

double adaptive_get_progress(void) {
    adaptive_scheduler_t *s = &g_adaptive_scheduler;

    if (!s->initialized) return 0.0;

    pthread_mutex_lock(&s->lock);

    uint64_t total_range = s->total_range_end - s->total_range_start;
    uint64_t completed = s->next_chunk_start - s->total_range_start;

    pthread_mutex_unlock(&s->lock);

    if (total_range == 0) return 1.0;
    return (double)completed / (double)total_range;
}

double adaptive_get_total_throughput(void) {
    adaptive_scheduler_t *s = &g_adaptive_scheduler;

    pthread_mutex_lock(&s->lock);
    double total = s->stats[WORKER_CPU].avg_mkeys_sec + s->stats[WORKER_GPU].avg_mkeys_sec;
    pthread_mutex_unlock(&s->lock);

    return total;
}

void adaptive_cleanup(void) {
    adaptive_scheduler_t *s = &g_adaptive_scheduler;

    if (s->initialized) {
        pthread_mutex_destroy(&s->lock);
        s->initialized = false;
    }
}
