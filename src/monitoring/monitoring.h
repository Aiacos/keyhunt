/*
 * monitoring.h - Monitoring loop and status reporting
 *
 * Extracted from keyhunt.cpp (Phase 4, Plan 08) to reduce the monolith.
 *
 * Contains:
 *   - run_monitoring_loop(): Main loop that prints status, tracks progress
 *   - GPU full-search stats thread
 *   - Rate formatting and progress bar helpers
 *   - Range progress tracking utilities
 */

#ifndef KEYHUNT_MONITORING_H
#define KEYHUNT_MONITORING_H

#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include "../config/config.h"
#include "../platform/platform.h"
#include "../secp256k1/Int.h"
#include "../search/search_common.h"

/* ============================================================================
 * Rate formatting and limits
 * ============================================================================ */

/* Initialize rate limit constants (int_limits[7] array for Mkeys/Gkeys/etc.) */
void monitoring_initialize_rate_limits(void);

/* Format keys-per-second Int value into human-readable string like "~42 Gkeys/s (42000000000 keys/s)" */
void monitoring_format_keys_per_second(Int &rate, char *out, size_t outSize);

/* ============================================================================
 * Range progress tracking
 * ============================================================================ */

/* Initialize progress tracker from range start/end. Call after range is set. */
void monitoring_initialize_range_progress(Int &range_start, Int &range_end,
                                          int progress_bar_flag, int mode);

/* Get whether range progress is enabled */
bool monitoring_range_progress_enabled(void);

/* Get range progress start/end pointers (for writekey validation) */
Int *monitoring_range_progress_start(void);
Int *monitoring_range_progress_end(void);

/* Compute (end - start) into uint64_t. Returns false if result doesn't fit. */
bool monitoring_span_u64_from_range(Int &start, Int &end, uint64_t &out);

/* ============================================================================
 * GPU full-search stats thread
 * ============================================================================ */

#ifndef _WIN64
typedef struct {
    int period_seconds;
    std::atomic<int> *stop_flag;
} gpu_full_stats_args_t;

platform_thread_return_t PLATFORM_THREAD_CALL gpu_full_stats_thread(void *arg);
#endif

/* ============================================================================
 * Monitoring loop parameters
 * ============================================================================ */

typedef struct {
    /* Thread state arrays (owned by keyhunt.cpp) */
    struct thread_counter *steps;
    struct thread_flag *ends;
    int num_threads;

    /* Mode flags */
    int flagmode;
    int flagmatrix;
    int flagquiet;
    int flagvisual;
    int flaggpu_hybrid;
    int flaggpu_full;
    int flagrandom;
    int flagendomorphism;

    /* GPU hybrid state */
    int gpu_hybrid_started;
    platform_thread_t gpu_thread_id;
    void *gpu_hybrid_args;  /* gpu_hybrid_args_t* */

    /* Output control */
    Int *output_seconds;
    std::atomic<int> *thread_output;

    /* Mutexes */
    platform_mutex_t *bsgs_mutex;

    /* Config pointer */
    keyhunt_config_t *config;

    /* Progress state */
    void *progress_state;  /* progress_state_t* */
    bool progress_enabled;

    /* System info (for visual mode) */
    void *sysinfo;  /* system_info_t* */

    /* Multi-GPU workers pointer (for per-GPU stats display) */
    void *multi_gpu_workers;  /* gpu_multi_worker_t* */
} monitoring_params_t;

/*
 * Run the main monitoring loop.
 *
 * Sleeps 1s per iteration, checks thread completion, prints status at
 * configured intervals. Returns when all threads are done.
 *
 * After returning, the caller handles GPU hybrid join and final cleanup.
 */
void run_monitoring_loop(monitoring_params_t *params);

/* ============================================================================
 * Utility: Total GPU keys checked (handles work-pool aggregation)
 * ============================================================================ */

uint64_t monitoring_gpu_keys_checked_total_u64(void);

/* ============================================================================
 * Utility: CPU Y-parity optimization check
 * ============================================================================ */

bool monitoring_cpu_use_y_parity_for_compressed_btc(void);

/* ============================================================================
 * Utility: Append adaptive scheduler info to status buffer
 * ============================================================================ */

void monitoring_append_adaptive_info(char *buffer, size_t bufferSize);

#endif /* KEYHUNT_MONITORING_H */
