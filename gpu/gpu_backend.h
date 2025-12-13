/*
 * GPU backend interface for keyhunt.
 *
 * Supports two modes:
 * 1. Hash-only mode: CPU generates points, GPU computes hash160
 * 2. Full GPU mode: GPU does ECC + hash160 + matching (BitCrack style)
 */

#ifndef GPU_BACKEND_H
#define GPU_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int gpu_count;
    uint64_t vram_mb;
    char name[128];
    int compute_major;
    int compute_minor;
    int multiprocessors;
    int max_threads_per_block;
} gpu_backend_info_t;

// Callback for found keys
typedef void (*gpu_found_callback_t)(const uint8_t *privkey, int compressed, void *userdata);

// Search configuration
typedef struct {
    uint8_t start_key[32];      // Starting private key (big-endian)
    uint8_t end_key[32];        // End of range (big-endian)
    uint8_t stride[32];         // Stride between keys (usually 1)

    const uint8_t *targets;     // Target hashes (20 bytes each, concatenated)
    size_t target_count;        // Number of targets

    int compressed_only;        // 1 = only compressed, 0 = both parities
    int use_bloom;              // 1 = use bloom filter for N>1

    gpu_found_callback_t callback;
    void *callback_userdata;

    // Statistics output (updated by GPU)
    volatile uint64_t *keys_checked;
    volatile int *should_stop;

    // If non-zero, suppress periodic GPU progress output (still updates keys_checked).
    int quiet;
} gpu_search_config_t;

// Initialize backend and populate info (if non-NULL). Returns 0 on success.
int gpu_backend_init(gpu_backend_info_t *info);

// Returns 1 if a CUDA backend is built and usable.
int gpu_backend_available(void);

// Shutdown backend (safe to call even if unavailable).
void gpu_backend_shutdown(void);

// ============================================================================
// Mode 1: Hash-only (existing API)
// ============================================================================

// Compute HASH160 (RIPEMD160(SHA256([prefix||x32]))) for BTC compressed pubkeys.
// - x32_be: concatenated big-endian X coordinates, count*32 bytes.
// - out02/out03: count*20 output bytes for prefix 0x02 and 0x03.
// Returns 0 on success, non-zero on error.
int gpu_hash160_fromX_batch(const uint8_t *x32_be, size_t count,
                            uint8_t *out02, uint8_t *out03);

// ============================================================================
// Mode 2: Full GPU search (ECC + hash160 + matching)
// ============================================================================

// Upload G table to GPU (call once at init after secp256k1 init)
// gtable: precomputed G table from CPU (256*32 points, each point = 64 bytes X||Y)
int gpu_upload_gtable(const uint8_t *gtable, size_t point_count);

// Upload targets to GPU (for matching)
int gpu_upload_targets(const uint8_t *targets, size_t count);

// Upload bloom filter to GPU
int gpu_upload_bloom(const uint8_t *bloom_data, size_t bloom_size, int num_hashes);

// Run full GPU search
// Returns: number of keys found (matches are reported via callback)
// The search runs until end_key is reached or should_stop is set
int gpu_full_search(const gpu_search_config_t *config);

// Get optimal batch size for this GPU
size_t gpu_get_optimal_batch_size(void);

// Benchmark GPU performance (returns keys/second)
double gpu_benchmark(size_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif // GPU_BACKEND_H
