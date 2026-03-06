/*
 * monitoring.cpp - Monitoring loop and status reporting
 *
 * Extracted from keyhunt.cpp (Phase 4, Plan 08).
 * Contains the main monitoring loop, rate formatting, progress tracking,
 * GPU stats thread, and supporting utility functions.
 */

#include "monitoring.h"
#include "../output.h"
#include "../progress.h"
#include "../util/profiling.h"
#include "../util/thread_util.h"
#include "../util/work_queue.h"
#include "../gpu/gpu_dispatch.h"
#include "../gpu/gpu_backend.h"
#include "../gpu/gpu_multi_worker.h"
#include "../hybrid/adaptive_scheduler.h"
#include "../platform/platform_memory.h"
#include "../modes/bsgs_globals.h"
#include "../core/sysinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

/* ============================================================================
 * Extern references to keyhunt.cpp globals (temporary bridge)
 * ============================================================================ */

/* GPU atomic counters */
extern std::atomic<uint64_t> g_gpu_keys_checked;
extern std::atomic<uint64_t> g_gpu_keys_checked_cur;
extern std::atomic<int> g_gpu_should_stop;

/* Work pool (for GPU work-stealing mode) */
extern WorkPool g_work_pool;

/* Search flags read by utility functions */
extern int FLAGSEARCH;
extern int FLAGMODE;
extern int FLAGCRYPTO;
extern int FLAGENDOMORPHISM;
extern int FLAGGPU_FULL;
extern std::atomic<int> FLAGGPU_HYBRID;
extern int FLAGRANDOM;

/* Range variables (read by progress tracker) */
extern Int n_range_start;
extern platform_mutex_t write_random;

/* Shared Int constants */
extern Int ZERO;

/* ============================================================================
 * File-local state: Rate limits
 * ============================================================================ */

static const char *str_limits_prefixes[7] = {"Mkeys/s","Gkeys/s","Tkeys/s","Pkeys/s","Ekeys/s","Zkeys/s","Ykeys/s"};
static const char *str_limits_values[7] = {"1000000","1000000000","1000000000000","1000000000000000","1000000000000000000","1000000000000000000000","1000000000000000000000000"};
static Int int_limits[7];
static int rate_limits_initialized = 0;

void monitoring_initialize_rate_limits(void) {
    if (rate_limits_initialized) return;
    for (int j = 0; j < 7; j++) {
        int_limits[j].SetBase10((char*)str_limits_values[j]);
    }
    rate_limits_initialized = 1;
}

void monitoring_format_keys_per_second(Int &rate, char *out, size_t outSize) {
    if (outSize == 0) return;
    char *raw = rate.GetBase10();
    if (raw == NULL) {
        snprintf(out, outSize, "? keys/s");
        return;
    }

    if (rate.IsLower(&int_limits[0])) {
        snprintf(out, outSize, "%s keys/s", raw);
        free(raw);
        return;
    }

    int idx = 0;
    while (idx < 6 && !rate.IsLower(&int_limits[idx + 1])) {
        idx++;
    }

    Int scaled;
    scaled.Set(&rate);
    scaled.Div(&int_limits[idx]);
    char *scaledStr = scaled.GetBase10();
    if (scaledStr == NULL) {
        snprintf(out, outSize, "%s keys/s", raw);
        free(raw);
        return;
    }

    snprintf(out, outSize, "~%s %s (%s keys/s)", scaledStr, str_limits_prefixes[idx], raw);
    free(raw);
    free(scaledStr);
}

/* ============================================================================
 * File-local state: Range progress
 * ============================================================================ */

static bool g_rangeProgressEnabled = false;
static Int g_rangeProgressStart;
static Int g_rangeProgressEnd;
static Int g_rangeProgressSpan;

void monitoring_initialize_range_progress(Int &range_start, Int &range_end,
                                          int progress_bar_flag, int mode) {
    /* Always set range bounds for writekey validation */
    g_rangeProgressStart.Set(&range_start);
    g_rangeProgressEnd.Set(&range_end);

    if (!progress_bar_flag) {
        g_rangeProgressEnabled = false;
        g_rangeProgressSpan.SetInt32(0);
        return;
    }
    if (mode == MODE_BSGS) {
        g_rangeProgressEnabled = false;
        g_rangeProgressSpan.SetInt32(0);
        return;
    }
    if (range_start.IsGreaterOrEqual(&range_end)) {
        g_rangeProgressEnabled = false;
        g_rangeProgressSpan.SetInt32(0);
        return;
    }
    g_rangeProgressSpan.Set(&range_end);
    g_rangeProgressSpan.Sub(&range_start);
    g_rangeProgressEnabled = !g_rangeProgressSpan.IsZero();
}

bool monitoring_range_progress_enabled(void) { return g_rangeProgressEnabled; }
Int *monitoring_range_progress_start(void) { return &g_rangeProgressStart; }
Int *monitoring_range_progress_end(void) { return &g_rangeProgressEnd; }

bool monitoring_span_u64_from_range(Int &start, Int &end, uint64_t &out) {
    uint64_t d0 = end.bits64[0] - start.bits64[0];
    uint64_t borrow = (end.bits64[0] < start.bits64[0]) ? 1ULL : 0ULL;
    for (int i = 1; i < NB64BLOCK; i++) {
        const uint64_t ei = end.bits64[i];
        const uint64_t si = start.bits64[i];
        const uint64_t si_borrow = si + borrow;
        const uint64_t di = ei - si_borrow;
        if (di != 0) return false;
        borrow = (ei < si_borrow) ? 1ULL : 0ULL;
    }
    if (borrow) return false;
    out = d0;
    return true;
}

/* ============================================================================
 * Internal helpers: hex position, progress metrics, progress bar
 * ============================================================================ */

static void format_hex_position(Int &value, char *out, size_t outSize) {
    if (outSize == 0) return;
    char *hex = value.GetBase16();
    if (hex == NULL) {
        snprintf(out, outSize, "0x0");
        return;
    }
    size_t hexLen = strlen(hex);
    if (hexLen <= 12) {
        snprintf(out, outSize, "0x%s", hex);
    } else {
        snprintf(out, outSize, "0x%.6s..%s", hex, hex + (hexLen > 6 ? hexLen - 6 : 0));
    }
    free(hex);
}

static bool snapshot_range_next_key(Int &out) {
    if (!g_rangeProgressEnabled) return false;
    platform_mutex_lock(&write_random);
    out.Set(&n_range_start);
    platform_mutex_unlock(&write_random);
    return true;
}

static bool capture_progress_metrics(int nthreads, struct thread_counter *steps,
                                      int &permille, char *position, size_t positionSize) {
    if (!g_rangeProgressEnabled || positionSize == 0) return false;

    Int consumed;
    Int nextKey;

    const bool count_based_progress = (FLAGRANDOM || FLAGGPU_HYBRID || FLAGGPU_FULL);
    if (count_based_progress) {
        Int total_checked;
        total_checked.SetInt32(0);

        if (steps != NULL) {
            Int thread_total;
            for (int j = 0; j < nthreads; j++) {
                thread_total.Set(&BSGS_N);
                thread_total.Mult(steps[j].value);
                total_checked.Add(&thread_total);
            }
        }

        if (FLAGGPU_HYBRID || FLAGGPU_FULL) {
            uint64_t gpu_total_u64 = monitoring_gpu_keys_checked_total_u64();
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%" PRIu64, gpu_total_u64);
            Int gpu_total;
            gpu_total.SetBase10(tmp);
            total_checked.Add(&gpu_total);
        }

        if (FLAGENDOMORPHISM) {
            if (FLAGMODE == MODE_XPOINT) {
                total_checked.Mult(3);
            } else {
                total_checked.Mult(6);
            }
        }

        consumed.Set(&total_checked);

        if (g_rangeProgressSpan.IsZero()) {
            permille = 0;
        } else {
            Int scaled;
            scaled.Set(&consumed);
            scaled.Mult(1000);
            scaled.Div(&g_rangeProgressSpan);
            permille = static_cast<int>(scaled.GetInt32());
            if (permille > 1000) permille = 1000;
            else if (permille < 0) permille = 0;
        }

        char *hex_consumed = consumed.GetBase16();
        if (hex_consumed != NULL) {
            snprintf(position, positionSize, "~%s checked", hex_consumed);
            free(hex_consumed);
        } else {
            snprintf(position, positionSize, "checked");
        }
    } else {
        /* Sequential mode */
        if (!snapshot_range_next_key(nextKey)) return false;
        if (nextKey.IsLower(&g_rangeProgressStart)) {
            nextKey.Set(&g_rangeProgressStart);
        }
        consumed.Set(&nextKey);
        consumed.Sub(&g_rangeProgressStart);
        if (consumed.IsNegative()) consumed.SetInt32(0);
        if (consumed.IsGreater(&g_rangeProgressSpan)) {
            consumed.Set(&g_rangeProgressSpan);
            nextKey.Set(&g_rangeProgressEnd);
        }
        if (g_rangeProgressSpan.IsZero()) {
            permille = 1000;
        } else {
            Int scaled;
            scaled.Set(&consumed);
            scaled.Mult(1000);
            scaled.Div(&g_rangeProgressSpan);
            permille = static_cast<int>(scaled.GetInt32());
            if (permille > 1000) permille = 1000;
            else if (permille < 0) permille = 0;
        }
        format_hex_position(nextKey, position, positionSize);
    }

    return true;
}

static void append_progress_info(int nthreads, struct thread_counter *steps,
                                  char *buffer, size_t bufferSize, int elapsed_seconds = -1) {
    if (!g_rangeProgressEnabled || bufferSize < 4) return;
    int permille = 0;
    char position[48];
    if (!capture_progress_metrics(nthreads, steps, permille, position, sizeof(position))) return;

    const int segments = 20;
    int filled = (permille * segments) / 1000;
    char bar[segments * 3 + 1];
    int pos = 0;
    for (int i = 0; i < segments; ++i) {
        const char *ch;
        if (i < filled) {
            ch = "\xe2\x96\x88"; // full block
        } else if (i == filled && filled < segments && (permille * segments) % 1000 > 0) {
            ch = "\xe2\x96\x93"; // dark shade
        } else {
            ch = "\xe2\x96\x91"; // light shade
        }
        bar[pos++] = ch[0];
        bar[pos++] = ch[1];
        bar[pos++] = ch[2];
    }
    bar[pos] = '\0';
    int percent = permille / 10;
    int tenths = permille % 10;

    char eta_part[48] = "";
    if (elapsed_seconds > 0 && permille > 0) {
        double total_estimate = elapsed_seconds / (permille / 1000.0);
        double remaining = (1000.0 - permille) / 1000.0;
        int eta_secs = (int)(total_estimate * remaining);
        if (eta_secs < 3600) {
            snprintf(eta_part, sizeof(eta_part), " | ETA: %dm %ds", eta_secs / 60, eta_secs % 60);
        } else if (eta_secs < 86400) {
            snprintf(eta_part, sizeof(eta_part), " | ETA: %dh %dm", eta_secs / 3600, (eta_secs % 3600) / 60);
        } else {
            snprintf(eta_part, sizeof(eta_part), " | ETA: %dd %dh", eta_secs / 86400, (eta_secs % 86400) / 3600);
        }
    }

    char addition[256];
    snprintf(addition, sizeof(addition), " | %s %d.%d%% @ %s%s", bar, percent, tenths, position, eta_part);
    size_t len = strlen(buffer);
    char tail = 0;
    if (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
        tail = buffer[len - 1];
        buffer[len - 1] = '\0';
        len--;
    }
    size_t rem = (len < bufferSize) ? bufferSize - len : 0;
    if (rem > 1) {
        strncat(buffer, addition, rem - 1);
        len = strlen(buffer);
    }
    if (tail != 0 && len + 1 < bufferSize) {
        buffer[len] = tail;
        buffer[len + 1] = '\0';
    }
}

/* ============================================================================
 * Adaptive info helper
 * ============================================================================ */

void monitoring_append_adaptive_info(char *buffer, size_t bufferSize) {
    if (bufferSize < 4) return;
    if (!g_adaptive_scheduler.initialized) return;

    double cpu_mkeys = 0.0, gpu_mkeys = 0.0;
    int cpu_pct = 0, gpu_pct = 0;
    adaptive_get_stats(&cpu_mkeys, &gpu_mkeys, &cpu_pct, &gpu_pct);

    if (cpu_mkeys < 0.1 && gpu_mkeys < 0.1) return;

    char addition[128];
    snprintf(addition, sizeof(addition),
             " | adapt CPU=%.1fMk/s(%d%%) GPU=%.1fMk/s(%d%%)",
             cpu_mkeys, cpu_pct, gpu_mkeys, gpu_pct);

    size_t len = strlen(buffer);
    char tail = 0;
    if (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
        tail = buffer[len - 1];
        buffer[len - 1] = '\0';
        len--;
    }
    size_t rem = (len < bufferSize) ? bufferSize - len : 0;
    if (rem > 1) {
        strncat(buffer, addition, rem - 1);
        len = strlen(buffer);
    }
    if (tail != 0 && len + 1 < bufferSize) {
        buffer[len] = tail;
        buffer[len + 1] = '\0';
    }
}

/* ============================================================================
 * GPU keys checked total (handles work-pool aggregation)
 * ============================================================================ */

uint64_t monitoring_gpu_keys_checked_total_u64(void) {
    if (!g_work_pool.enabled.load(std::memory_order_acquire)) {
        return g_gpu_keys_checked.load(std::memory_order_acquire);
    }
    uint64_t cur = g_gpu_keys_checked_cur.load(std::memory_order_acquire);
    uint64_t base = g_gpu_keys_checked.load(std::memory_order_acquire);
    return base + cur;
}

/* ============================================================================
 * CPU Y-parity optimization check
 * ============================================================================ */

bool monitoring_cpu_use_y_parity_for_compressed_btc(void) {
    if (!((FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160) &&
          FLAGCRYPTO == CRYPTO_BTC &&
          !FLAGENDOMORPHISM &&
          FLAGSEARCH == SEARCH_COMPRESS)) {
        return false;
    }

    if (FLAGGPU_HYBRID && FLAGGPU_FULL == 1) {
        const char *env = getenv("KEYHUNT_HYBRID_CPU_USE_Y");
        if (env && *env) return atoi(env) != 0;
        return true;
    }

    const char *env = getenv("KEYHUNT_CPU_USE_Y");
    if (env && *env) return atoi(env) != 0;
    return true;
}

/* ============================================================================
 * GPU full-search stats thread
 * ============================================================================ */

#ifndef _WIN64
platform_thread_return_t PLATFORM_THREAD_CALL gpu_full_stats_thread(void *arg) {
    gpu_full_stats_args_t *args = (gpu_full_stats_args_t *)arg;
    if (args == NULL || args->stop_flag == NULL) return (platform_thread_return_t)0;

    const int period = args->period_seconds;
    if (period <= 0) return (platform_thread_return_t)0;

    uint64_t prev_total = 0;
    uint64_t seconds = 0;
    while (!args->stop_flag->load(std::memory_order_acquire)) {
        sleep_ms(1000);
        seconds++;
        if (args->stop_flag->load(std::memory_order_acquire)) break;
        if ((seconds % (uint64_t)period) != 0) continue;

        uint64_t total_u64 = g_gpu_keys_checked.load(std::memory_order_relaxed);
        uint64_t delta_u64 = total_u64 - prev_total;

        Int total_i, delta_i, rate_i;
        {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%" PRIu64, total_u64);
            total_i.SetBase10(tmp);
            snprintf(tmp, sizeof(tmp), "%" PRIu64, delta_u64);
            delta_i.SetBase10(tmp);
        }

        rate_i.Set(&delta_i);
        Int period_i;
        period_i.SetInt32(period);
        rate_i.Div(&period_i);

        char gpu_rate_str[128];
        monitoring_format_keys_per_second(rate_i, gpu_rate_str, sizeof(gpu_rate_str));

        char *str_total = total_i.GetBase10();
        char seconds_buf[32];
        snprintf(seconds_buf, sizeof(seconds_buf), "%" PRIu64, seconds);

        char buffer[512];
        snprintf(buffer, sizeof(buffer),
                 "[+] Total %s keys in %s seconds (last %d s): CPU 0 keys/s | GPU %s | TOTAL %s\n",
                 str_total ? str_total : "?", seconds_buf, period, gpu_rate_str, gpu_rate_str);
        /* Note: append_progress_info needs thread data; in GPU-full-only mode there are no CPU threads */
        monitoring_append_adaptive_info(buffer, sizeof(buffer));
        append_profile_info(buffer, sizeof(buffer));
        printf("%s", buffer);
        fflush(stdout);

        if (str_total) free(str_total);
        prev_total = total_u64;
    }

    return (platform_thread_return_t)0;
}
#endif

/* ============================================================================
 * Visual progress display helper
 * ============================================================================ */

static void show_visual_progress(monitoring_params_t *params,
                                  int permille, double speed_mkeys,
                                  uint64_t keys_checked, int elapsed_secs) {
    double percent = permille / 10.0;
    double remaining_ratio = (1000.0 - permille) / 1000.0;
    double total_estimate = (permille > 0) ? (elapsed_secs / (permille / 1000.0)) : 0;
    int eta_seconds = (int)(total_estimate * remaining_ratio);

    if (params->progress_enabled) {
        progress_state_t *ps = (progress_state_t *)params->progress_state;
        speed_history_add_sample(&ps->speed_history, speed_mkeys * 1000000.0);
    }

    if (params->flagvisual) {
        uint64_t memory_used_mb = platform_memory_usage_mb();
        system_info_t *si = (system_info_t *)params->sysinfo;
        uint64_t memory_total_mb = si ? si->ram_total : 0;

        double speed_samples[SPEED_HISTORY_MAX_SAMPLES];
        int sample_count = 0;
        if (params->progress_enabled) {
            progress_state_t *ps = (progress_state_t *)params->progress_state;
            if (ps->speed_history.count > 0) {
                const speed_history_t *hist = &ps->speed_history;
                sample_count = hist->count;
                int start_idx = (hist->write_index - hist->count + SPEED_HISTORY_MAX_SAMPLES) % SPEED_HISTORY_MAX_SAMPLES;
                for (int i = 0; i < hist->count; i++) {
                    int idx = (start_idx + i) % SPEED_HISTORY_MAX_SAMPLES;
                    speed_samples[i] = hist->samples[idx].keys_per_second / 1000000.0;
                }
            }
        }

        output_progress_detailed(percent, speed_mkeys, keys_checked, eta_seconds,
                                 memory_used_mb, memory_total_mb,
                                 sample_count > 0 ? speed_samples : NULL, sample_count,
                                 params->num_threads);
    } else {
        output_progress(percent, speed_mkeys, keys_checked, eta_seconds);
    }
    printf("\n");
}

/* ============================================================================
 * Display per-GPU stats helper
 * ============================================================================ */

static void display_multi_gpu_stats(void *workers_ptr) {
    if (workers_ptr == NULL) return;
    gpu_multi_worker_t *workers = (gpu_multi_worker_t *)workers_ptr;

    multi_gpu_worker_stats_t gpu_stats;
    gpu_worker_get_stats(workers, &gpu_stats);

    if (gpu_stats.active_workers > 0) {
        int device_ids[MULTI_GPU_MAX_DEVICES];
        uint64_t keys_processed[MULTI_GPU_MAX_DEVICES];
        double throughput_mkeys[MULTI_GPU_MAX_DEVICES];
        const char *device_names[MULTI_GPU_MAX_DEVICES];

        for (int i = 0; i < gpu_stats.active_workers; i++) {
            device_ids[i] = gpu_stats.workers[i].device_id;
            keys_processed[i] = gpu_stats.workers[i].keys_processed;
            throughput_mkeys[i] = gpu_stats.workers[i].current_throughput;
            device_names[i] = NULL;
        }

        output_gpu_stats(gpu_stats.active_workers, device_ids,
                         keys_processed, throughput_mkeys, device_names);
    }
}

/* ============================================================================
 * Main monitoring loop
 * ============================================================================ */

void run_monitoring_loop(monitoring_params_t *params) {
    char buffer[2048];
    Int total, pretotal, debugcount_mpz, seconds, div_pretotal;
    char *str_seconds = NULL;
    char *str_total = NULL;
    char *str_pretotal = NULL;
    char *str_divpretotal = NULL;
    int continue_flag, check_flag, j;
    uint64_t i;
    int salir;

    monitoring_initialize_rate_limits();

    continue_flag = 1;
    total.SetInt32(0);
    pretotal.SetInt32(0);
    debugcount_mpz.Set(&BSGS_N);
    seconds.SetInt32(0);
    Int prev_cpu_total;
    prev_cpu_total.SetInt32(0);
    uint64_t prev_gpu_total_u64 = 0;

    Int output_seconds_local;
    output_seconds_local.Set(params->output_seconds);

    do {
        sleep_ms(1000);
        seconds.AddOne();
        check_flag = 1;
        for (j = 0; j < params->num_threads && check_flag; j++) {
            check_flag &= params->ends[j].value;
        }
        if (check_flag) {
            continue_flag = 0;
        }
        if (output_seconds_local.IsGreater(&ZERO)) {
            Int mpzaux;
            mpzaux.Set(&seconds);
            mpzaux.Mod(&output_seconds_local);
            if (mpzaux.IsZero()) {
                platform_mutex_lock(params->bsgs_mutex);
                if (params->flaggpu_hybrid && params->gpu_hybrid_started) {
                    /* ===== GPU Hybrid stats path ===== */
                    Int cpu_total;
                    cpu_total.SetInt32(0);
                    for (j = 0; j < params->num_threads; j++) {
                        pretotal.Set(&debugcount_mpz);
                        pretotal.Mult(params->steps[j].value);
                        cpu_total.Add(&pretotal);
                    }

                    uint64_t gpu_total_u64 = monitoring_gpu_keys_checked_total_u64();
                    Int gpu_total;
                    {
                        char tmp[64];
                        snprintf(tmp, sizeof(tmp), "%" PRIu64, gpu_total_u64);
                        gpu_total.SetBase10(tmp);
                    }

                    Int cpu_delta;
                    cpu_delta.Set(&cpu_total);
                    cpu_delta.Sub(&prev_cpu_total);
                    uint64_t gpu_delta_u64 = gpu_total_u64 - prev_gpu_total_u64;
                    Int gpu_delta;
                    {
                        char tmp[64];
                        snprintf(tmp, sizeof(tmp), "%" PRIu64, gpu_delta_u64);
                        gpu_delta.SetBase10(tmp);
                    }

                    Int period;
                    period.Set(&output_seconds_local);
                    if (period.IsZero()) period.SetInt32(1);

                    Int overall_total;
                    overall_total.Set(&cpu_total);
                    overall_total.Add(&gpu_total);

                    /* Report to adaptive scheduler */
                    {
                        uint64_t cpu_delta_u64 = cpu_delta.IsPositive() ?
                            strtoull(cpu_delta.GetBase10(), NULL, 10) : 0;
                        uint64_t period_ms = period.IsPositive() ?
                            strtoull(period.GetBase10(), NULL, 10) * 1000 : 1000;

                        if (cpu_delta_u64 > 0) {
                            adaptive_report_work(WORKER_CPU, cpu_delta_u64, period_ms);
                        }
                        if (gpu_delta_u64 > 0 && !g_work_pool.enabled) {
                            adaptive_report_work(WORKER_GPU, gpu_delta_u64, period_ms);
                        }
                    }

                    Int cpu_rate, gpu_rate, overall_rate;
                    cpu_rate.Set(&cpu_delta);
                    cpu_rate.Div(&period);
                    gpu_rate.Set(&gpu_delta);
                    gpu_rate.Div(&period);
                    overall_rate.Set(&cpu_delta);
                    overall_rate.Add(&gpu_delta);
                    overall_rate.Div(&period);

                    char cpu_rate_str[128], gpu_rate_str[128], overall_rate_str[128];
                    monitoring_format_keys_per_second(cpu_rate, cpu_rate_str, sizeof(cpu_rate_str));
                    monitoring_format_keys_per_second(gpu_rate, gpu_rate_str, sizeof(gpu_rate_str));
                    monitoring_format_keys_per_second(overall_rate, overall_rate_str, sizeof(overall_rate_str));

                    str_seconds = seconds.GetBase10();
                    str_total = overall_total.GetBase10();
                    char *str_period = period.GetBase10();

                    const bool line_mode = (params->flagmatrix || params->flagquiet || params->flaggpu_hybrid);
                    if (line_mode) {
                        snprintf(buffer, sizeof(buffer),
                            "[+] Total %s keys in %s seconds (last %s s): CPU %s | GPU %s | TOTAL %s\n",
                            str_total ? str_total : "?", str_seconds ? str_seconds : "?",
                            str_period ? str_period : "?",
                            cpu_rate_str, gpu_rate_str, overall_rate_str);
                    } else {
                        snprintf(buffer, sizeof(buffer),
                            "\r[+] Total %s keys in %s seconds (last %s s): CPU %s | GPU %s | TOTAL %s\r",
                            str_total ? str_total : "?", str_seconds ? str_seconds : "?",
                            str_period ? str_period : "?",
                            cpu_rate_str, gpu_rate_str, overall_rate_str);
                    }

                    int elapsed_secs = atoi(str_seconds ? str_seconds : "0");
                    append_progress_info(params->num_threads, params->steps, buffer, sizeof(buffer), elapsed_secs);
                    append_profile_info(buffer, sizeof(buffer));
                    monitoring_append_adaptive_info(buffer, sizeof(buffer));
                    printf("%s", buffer);
                    fflush(stdout);
                    params->thread_output->store(0, std::memory_order_release);

                    display_multi_gpu_stats(params->multi_gpu_workers);

                    /* Visual progress for hybrid mode */
                    if (g_rangeProgressEnabled && !line_mode) {
                        int permille = 0;
                        char pos[48];
                        if (capture_progress_metrics(params->num_threads, params->steps, permille, pos, sizeof(pos))) {
                            char *rate_str = overall_rate.GetBase10();
                            double speed_mkeys = strtod(rate_str ? rate_str : "0", NULL) / 1000000.0;
                            if (rate_str) free(rate_str);
                            uint64_t keys_checked = strtoull(str_total ? str_total : "0", NULL, 10);
                            show_visual_progress(params, permille, speed_mkeys, keys_checked, elapsed_secs);
                        }
                    } else if (g_rangeProgressEnabled && line_mode) {
                        int permille = 0;
                        char pos[48];
                        if (capture_progress_metrics(params->num_threads, params->steps, permille, pos, sizeof(pos))) {
                            if (params->progress_enabled) {
                                char *rate_str = overall_rate.GetBase10();
                                double speed_raw = strtod(rate_str ? rate_str : "0", NULL);
                                if (rate_str) free(rate_str);
                                progress_state_t *ps = (progress_state_t *)params->progress_state;
                                speed_history_add_sample(&ps->speed_history, speed_raw);
                            }
                        }
                    }

                    /* Update progress tracking */
                    if (params->progress_enabled) {
                        static bool progress_save_warned = false;
                        char *current_hex = n_range_start.GetBase16();
                        progress_state_t *ps = (progress_state_t *)params->progress_state;
                        int update_result = progress_update(ps, current_hex, strtoull(str_total ? str_total : "0", NULL, 10));
                        if (update_result != 0 && !progress_save_warned) {
                            output_warning("Failed to save progress. Check disk space and permissions.\n");
                            progress_save_warned = true;
                        }
                        if (current_hex) free(current_hex);
                    }

                    prev_cpu_total.Set(&cpu_total);
                    prev_gpu_total_u64 = gpu_total_u64;

                    if (str_seconds) free(str_seconds);
                    if (str_total) free(str_total);
                    if (str_period) free(str_period);

                } else {
                    /* ===== CPU-only stats path ===== */
                    total.SetInt32(0);
                    for (j = 0; j < params->num_threads; j++) {
                        pretotal.Set(&debugcount_mpz);
                        pretotal.Mult(params->steps[j].value);
                        total.Add(&pretotal);
                    }

                    if (params->flagendomorphism) {
                        if (params->flagmode == MODE_XPOINT) {
                            total.Mult(3);
                        } else {
                            total.Mult(6);
                        }
                    }

                    pretotal.Set(&total);
                    pretotal.Div(&seconds);
                    str_seconds = seconds.GetBase10();
                    str_pretotal = pretotal.GetBase10();
                    str_total = total.GetBase10();

                    const bool line_mode = (params->flagmatrix || params->flagquiet || params->flaggpu_hybrid);
                    if (pretotal.IsLower(&int_limits[0])) {
                        if (line_mode) {
                            snprintf(buffer, sizeof(buffer), "[+] Total %s keys in %s seconds: %s keys/s\n",
                                     str_total, str_seconds, str_pretotal);
                        } else {
                            snprintf(buffer, sizeof(buffer), "\r[+] Total %s keys in %s seconds: %s keys/s\r",
                                     str_total, str_seconds, str_pretotal);
                        }
                    } else {
                        i = 0;
                        salir = 0;
                        while (i < 6 && !salir) {
                            if (pretotal.IsLower(&int_limits[i + 1])) {
                                salir = 1;
                            } else {
                                i++;
                            }
                        }

                        div_pretotal.Set(&pretotal);
                        div_pretotal.Div(&int_limits[salir ? i : i - 1]);
                        str_divpretotal = div_pretotal.GetBase10();
                        if (line_mode) {
                            snprintf(buffer, sizeof(buffer),
                                     "[+] Total %s keys in %s seconds: ~%s %s (%s keys/s)\n",
                                     str_total, str_seconds, str_divpretotal,
                                     str_limits_prefixes[salir ? i : i - 1], str_pretotal);
                        } else {
                            if (params->thread_output->load(std::memory_order_acquire) == 1) {
                                snprintf(buffer, sizeof(buffer),
                                         "\r[+] Total %s keys in %s seconds: ~%s %s (%s keys/s)\r",
                                         str_total, str_seconds, str_divpretotal,
                                         str_limits_prefixes[salir ? i : i - 1], str_pretotal);
                            } else {
                                snprintf(buffer, sizeof(buffer),
                                         "\r[+] Total %s keys in %s seconds: ~%s %s (%s keys/s)\r",
                                         str_total, str_seconds, str_divpretotal,
                                         str_limits_prefixes[salir ? i : i - 1], str_pretotal);
                            }
                        }
                        free(str_divpretotal);
                    }
                    int elapsed_secs_cpu = atoi(str_seconds ? str_seconds : "0");
                    append_progress_info(params->num_threads, params->steps, buffer, sizeof(buffer), elapsed_secs_cpu);
                    append_profile_info(buffer, sizeof(buffer));
                    monitoring_append_adaptive_info(buffer, sizeof(buffer));
                    printf("%s", buffer);
                    fflush(stdout);
                    params->thread_output->store(0, std::memory_order_release);

                    display_multi_gpu_stats(params->multi_gpu_workers);

                    /* Visual progress */
                    if (g_rangeProgressEnabled && !line_mode) {
                        int permille = 0;
                        char pos[48];
                        if (capture_progress_metrics(params->num_threads, params->steps, permille, pos, sizeof(pos))) {
                            double speed_mkeys = strtod(str_pretotal ? str_pretotal : "0", NULL) / 1000000.0;
                            uint64_t keys_checked = strtoull(str_total ? str_total : "0", NULL, 10);
                            show_visual_progress(params, permille, speed_mkeys, keys_checked, elapsed_secs_cpu);
                        }
                    } else if (g_rangeProgressEnabled && line_mode) {
                        int permille = 0;
                        char pos[48];
                        if (capture_progress_metrics(params->num_threads, params->steps, permille, pos, sizeof(pos))) {
                            if (params->progress_enabled) {
                                double speed_raw = strtod(str_pretotal ? str_pretotal : "0", NULL);
                                progress_state_t *ps = (progress_state_t *)params->progress_state;
                                speed_history_add_sample(&ps->speed_history, speed_raw);
                            }
                        }
                    }

                    /* Update progress tracking */
                    if (params->progress_enabled) {
                        static bool progress_save_warned_cpu = false;
                        char *current_hex = n_range_start.GetBase16();
                        progress_state_t *ps = (progress_state_t *)params->progress_state;
                        int update_result = progress_update(ps, current_hex, strtoull(str_total ? str_total : "0", NULL, 10));
                        if (update_result != 0 && !progress_save_warned_cpu) {
                            output_warning("Failed to save progress. Check disk space and permissions.\n");
                            progress_save_warned_cpu = true;
                        }
                        if (current_hex) free(current_hex);
                    }

                    free(str_seconds);
                    free(str_pretotal);
                    free(str_total);
                }
                platform_mutex_unlock(params->bsgs_mutex);
            }
        }
    } while (continue_flag);
}
