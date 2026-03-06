/*
 * gpu_dispatch.h - GPU dispatch function declarations
 *
 * Extracted from keyhunt.cpp (Phase 4, Plan 05) to decouple GPU search
 * orchestration from the main orchestrator.
 *
 * These functions handle:
 *   - Uploading precomputed G table to GPU
 *   - Uploading target addresses to GPU
 *   - Building and uploading GPU-side bloom filters
 *   - GPU found-key callback (writes discovered keys)
 *   - Full GPU search execution
 *   - Hybrid GPU thread (work-stealing or static-range)
 */

#ifndef GPU_DISPATCH_H
#define GPU_DISPATCH_H

#include <stdint.h>
#include <atomic>
#include "../config/config.h"
#include "../platform/platform.h"
#include "../secp256k1/Int.h"
#include "gpu_multi_worker.h"

/* Thread calling convention (matches search/search_common.h) */
#ifndef PLATFORM_THREAD_CALL
#if defined(_WIN64) && !defined(__CYGWIN__)
    #define PLATFORM_THREAD_CALL WINAPI
#else
    #define PLATFORM_THREAD_CALL
#endif
#endif

/* ============================================================================
 * GPU Hybrid Thread Arguments
 * ============================================================================ */

typedef struct {
    Int start_key;
    Int end_key;
    Int stride;
    int64_t target_count;
    std::atomic<int> result{0};
    std::atomic<int> completed{0};
} gpu_hybrid_args_t;

/* ============================================================================
 * GPU Dispatch Functions
 * ============================================================================ */

/*
 * Upload precomputed G table (256*32 = 8192 points) to GPU.
 * Uses the secp256k1 instance from keyhunt.cpp (extern Secp256K1 *secp).
 *
 * Returns 0 on success, non-zero on failure.
 */
int gpu_dispatch_upload_gtable(void);

/*
 * Upload target address hashes to GPU for matching.
 *
 * @param addressTable  Pointer to struct address_value array
 * @param count         Number of target addresses
 * Returns 0 on success, non-zero on failure.
 */
int gpu_dispatch_upload_targets(void *addressTable, int64_t count);

/*
 * Build and upload a GPU-side bloom filter from the address table.
 * Reduces expensive binary-search matching when target count is large.
 *
 * @param addressTable  Pointer to struct address_value array
 * @param count         Number of target addresses
 * Returns 0 on success, non-zero on failure.
 */
int gpu_dispatch_upload_bloom(void *addressTable, int64_t count);

/*
 * Callback invoked by GPU search when a matching key is found.
 * Converts big-endian private key to Int and calls writekey().
 *
 * @param privkey_be   32-byte big-endian private key
 * @param compressed   1 if key was found via compressed pubkey, 0 otherwise
 * @param userdata     Opaque pointer (keyhunt_config_t*)
 */
void gpu_dispatch_found_callback(const uint8_t *privkey_be, int compressed, void *userdata);

/*
 * Run full GPU search over a key range.
 *
 * @param config_ptr    Pointer to keyhunt_config_t (for FLAGSEARCH, quiet, etc.)
 * @param start_key     Start of search range (inclusive)
 * @param end_key       End of search range (exclusive)
 * @param stride_val    Key increment stride
 * @param target_count  Number of target addresses
 * Returns number of keys found (>=0), or negative on error.
 */
int gpu_dispatch_run_full_search(keyhunt_config_t *config_ptr,
                                 Int *start_key, Int *end_key,
                                 Int *stride_val, int64_t target_count);

/*
 * GPU hybrid thread function (work-stealing or static-range).
 * Designed to run as a platform thread alongside CPU threads.
 *
 * @param arg  Pointer to gpu_hybrid_args_t
 * Returns 0 (platform_thread_return_t).
 */
platform_thread_return_t PLATFORM_THREAD_CALL gpu_dispatch_hybrid_thread(void *arg);

/*
 * Check/get whether the GPU-side bloom was uploaded.
 * File-local state managed by gpu_dispatch.cpp.
 */
int gpu_dispatch_bloom_uploaded(void);

/*
 * GPU self-test: verify GPU hash160 against CPU reference for 16 test points.
 * Returns true if GPU matches CPU output, false on mismatch or error.
 */
bool gpu_selftest_hash160_fromX(void);

/*
 * Calculate default GPU range percent for hybrid mode based on SM/CU count vs CPU threads.
 * Returns percentage (50-99) of range to assign to GPU.
 */
int hybrid_get_gpu_range_percent_default(int cpu_threads);

/* ============================================================================
 * High-level GPU orchestration functions (extracted from keyhunt.cpp main())
 * ============================================================================ */

/*
 * Resolve GPU mode flags based on availability, mode support, and user request.
 * Modifies FLAGGPU, FLAGGPU_FULL, FLAGGPU_HYBRID, NTHREADS globals.
 * Must be called after CLI parsing and sysinfo detection.
 */
void resolve_gpu_mode(void);

/*
 * Run GPU full search mode (multi-GPU or single-GPU).
 * Handles stats thread, multi-GPU scheduler, and GPU lifecycle.
 *
 * @param config       Pointer to keyhunt_config_t
 * @param multi_gpu_workers  Pointer to g_multi_gpu_workers (may be set by this function)
 * Returns: 0 on success (caller should exit), -1 on failure (fall back to CPU)
 */
int run_gpu_full_search_mode(keyhunt_config_t *config, gpu_multi_worker_t **multi_gpu_workers);

/*
 * Set up GPU hybrid mode (work-stealing or static split).
 * Starts GPU thread and optionally adjusts CPU range.
 *
 * @param config              Pointer to keyhunt_config_t
 * @param gpu_thread_id       Output: thread handle for GPU thread
 * @param gpu_hybrid_args     Output: populated hybrid args struct
 * @param gpu_hybrid_started  Output: 1 if GPU thread was started, 0 otherwise
 * Returns: 0 on success, -1 on failure
 */
int run_gpu_hybrid_setup(keyhunt_config_t *config,
                         platform_thread_t *gpu_thread_id,
                         gpu_hybrid_args_t *gpu_hybrid_args,
                         int *gpu_hybrid_started);

#endif /* GPU_DISPATCH_H */
