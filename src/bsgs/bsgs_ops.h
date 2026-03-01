/*
 * Optimized BSGS Operations Module
 * High-performance batch operations for Baby Step Giant Step algorithm
 *
 * Key optimizations:
 * - SIMD bloom filter integration
 * - Batched point calculations with prefetching
 * - Cache-aligned data structures
 * - Loop unrolling for ILP (Instruction Level Parallelism)
 */

#ifndef BSGS_OPS_H
#define BSGS_OPS_H

#include <stdint.h>
#include <stdbool.h>
#include "../secp256k1/Int.h"
#include "../secp256k1/Point.h"
#include "../secp256k1/IntGroup.h"

#ifdef __cplusplus
extern "C" {
#endif

// Batch size for BSGS operations (must be power of 2, aligned to cache)
#define BSGS_BATCH_SIZE 1024
#define BSGS_HALF_BATCH (BSGS_BATCH_SIZE / 2)

// Cache line size
#define CACHE_LINE_SIZE 64

// Prefetch distance for optimal memory access
#define PREFETCH_DISTANCE 8

/*
 * BSGS Batch Context
 * Pre-allocated buffers for batch operations
 */
typedef struct bsgs_batch_ctx {
    // Pre-allocated point arrays (cache-aligned)
    Point *pts;              // CPU_GRP_SIZE points
    Int *dx;                 // Differences for batch ModInv

    // IntGroup for batch modular inversion
    void *grp;               // IntGroup pointer (opaque for C compatibility)

    // Bloom filter check results
    uint8_t *bloom_results;  // Results array

    // X-point raw data for bloom checks
    uint8_t *xpoint_raw;     // 32 * batch_size bytes

    // Configuration
    int batch_size;
    int half_batch;
    bool initialized;
} bsgs_batch_ctx_t;

/*
 * Initialize BSGS batch context
 * Allocates aligned memory for all batch operations
 *
 * @param ctx       Context to initialize
 * @param batch_size Number of points per batch (default BSGS_BATCH_SIZE)
 * @return          0 on success, -1 on failure
 */
int bsgs_batch_init(bsgs_batch_ctx_t *ctx, int batch_size);

/*
 * Free BSGS batch context
 */
void bsgs_batch_free(bsgs_batch_ctx_t *ctx);

/*
 * Batch compute points with optimized memory access
 * Calculates CPU_GRP_SIZE points from startP using GSn table
 *
 * @param ctx       Batch context
 * @param startP    Starting point
 * @param GSn       Pre-computed G*n table (must have at least hLength+1 elements)
 * @param _2GSn     Pre-computed 2*G*step point
 * @param hLength   Half length (CPU_GRP_SIZE/2 - 1)
 */
void bsgs_batch_compute_points(
    bsgs_batch_ctx_t *ctx,
    Point *startP,
    Point *GSn,
    Point *_2GSn,
    int hLength);

/*
 * Batch bloom filter check with SIMD optimization
 * Checks all points in batch against bloom filter
 *
 * @param ctx           Batch context
 * @param bloom_array   Array of bloom filters (256 filters indexed by first byte)
 * @param num_points    Number of points to check
 * @return              Number of potential matches found
 */
int bsgs_batch_bloom_check(
    bsgs_batch_ctx_t *ctx,
    void *bloom_array,
    int num_points);

/*
 * Extract X-coordinates from points in batch
 * Optimized with prefetching and SIMD where possible
 *
 * @param ctx        Batch context
 * @param num_points Number of points
 */
void bsgs_batch_extract_xpoints(
    bsgs_batch_ctx_t *ctx,
    int num_points);

/*
 * Advance startP to next batch position
 * startP = startP + (bsSize * GRP_SIZE) * G
 *
 * @param ctx       Batch context
 * @param startP    Point to advance (modified in place)
 * @param _2GSn     Pre-computed advancement point
 * @param dx_last   Last dx value from batch
 */
void bsgs_batch_advance_startP(
    bsgs_batch_ctx_t *ctx,
    Point *startP,
    Point *_2GSn,
    Int *dx_last);

#ifdef __cplusplus
}
#endif

#endif // BSGS_OPS_H
