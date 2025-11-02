/*
 * Optimized BSGS main loop with batched bloom checking
 * Replaces the serial bloom check in thread_process_bsgs
 *
 * Key optimizations:
 * 1. Batch bloom filter checks (64 points at a time)
 * 2. Prefetch bloom filter data before access
 * 3. Better cache locality with aligned data structures
 * 4. Reduced function call overhead
 */

// Include bsgs_optimized.h first to avoid macro conflicts
#include "bsgs_optimized.h"
#include "secp256k1/Point.h"
#include "secp256k1/Int.h"
#include "secp256k1/IntGroup.h"
#include "bloom/bloom.h"

// Optimized version of the point checking loop
// Returns 1 if key found, 0 otherwise
int bsgs_check_points_batched(
    Point *pts,                    // Array of CPU_GRP_SIZE points
    int cpu_grp_size,              // CPU_GRP_SIZE value
    struct bloom *bloom_bP,        // Array of 256 bloom filters
    Int *base_key,                 // Base key for this iteration
    uint64_t cycle_offset,         // j * 1024
    int (*secondcheck_func)(Int*, uint32_t, Int*),  // Callback to bsgs_secondcheck
    Int *keyfound,                 // Output: found key
    int *bsgs_found                // Global found flag
) {
    struct PointBatch batch;
    unsigned char xpoint_raw[32] __attribute__((aligned(32)));

    uint32_t match_indices[BLOOM_BATCH_SIZE];
    uint32_t match_count = 0;

    init_point_batch(&batch);

    // Extract points into batch with prefetching
    for (int i = 0; i < cpu_grp_size && *bsgs_found == 0; i++) {
        // Extract x-coordinate
        pts[i].x.Get32Bytes((unsigned char*)xpoint_raw);

        // Prefetch next point's data
        if (i + 1 < cpu_grp_size) {
            _mm_prefetch((const char*)&pts[i + 1], _MM_HINT_T0);
        }

        // Add to batch
        int batch_full = add_to_batch(&batch, xpoint_raw, i);

        // Process batch when full or at end
        if (batch_full || i == cpu_grp_size - 1) {
            // Check entire batch at once
            bloom_check_batch(bloom_bP, &batch, match_indices, &match_count);

            // Process matches
            for (uint32_t m = 0; m < match_count && *bsgs_found == 0; m++) {
                uint32_t idx = match_indices[m];
                uint32_t absolute_idx = cycle_offset + idx;

                // Do second check
                int found = secondcheck_func(base_key, absolute_idx, keyfound);
                if (found) {
                    *bsgs_found = 1;
                    return 1;
                }
            }

            // Reset batch for next iteration
            init_point_batch(&batch);
        }
    }

    return 0;
}

// Optimized bPload loop with better memory access patterns
void bPload_generate_batch_optimized(
    Point *pts,                     // Output: generated points
    int cpu_grp_size,               // CPU_GRP_SIZE value
    Point *startP,                  // Starting point
    Point *Gn,                      // Precomputed G multiples
    Point *_2Gn,                    // 2 * (CPU_GRP_SIZE/2) * G
    IntGroup *grp,                  // IntGroup for batch inversion
    Int *dx                         // Temporary array for inversions
) {
    int hLength = (cpu_grp_size / 2 - 1);
    Int dy, dyn, _s, _p;
    Point pp, pn;

    // Compute all dx values first (better for cache)
    for (int i = 0; i < hLength; i++) {
        dx[i].ModSub(&Gn[i].x, &startP->x);
    }
    dx[hLength].ModSub(&Gn[hLength].x, &startP->x);
    dx[hLength + 1].ModSub(&_2Gn->x, &startP->x);

    // Batch ModInv (most expensive operation)
    grp->ModInv();

    // Center point
    pts[cpu_grp_size / 2] = *startP;

    // Generate all points using precomputed inversions
    // This loop is optimized for better instruction-level parallelism
    for (int i = 0; i < hLength; i++) {
        pp = *startP;
        pn = *startP;

        // Positive direction: P = startP + i*G
        dy.ModSub(&Gn[i].y, &pp.y);
        _s.ModMulK1(&dy, &dx[i]);
        _p.ModSquareK1(&_s);
        pp.x.ModNeg();
        pp.x.ModAdd(&_p);
        pp.x.ModSub(&Gn[i].x);

        // Negative direction: P = startP - i*G
        dyn.Set(&Gn[i].y);
        dyn.ModNeg();
        dyn.ModSub(&pn.y);
        _s.ModMulK1(&dyn, &dx[i]);
        _p.ModSquareK1(&_s);
        pn.x.ModNeg();
        pn.x.ModAdd(&_p);
        pn.x.ModSub(&Gn[i].x);

        // Store both points
        pts[cpu_grp_size / 2 + (i + 1)] = pp;
        pts[cpu_grp_size / 2 - (i + 1)] = pn;
    }

    // First point (startP - (GRP_SIZE/2)*G)
    pn = *startP;
    dyn.Set(&Gn[hLength].y);
    dyn.ModNeg();
    dyn.ModSub(&pn.y);
    _s.ModMulK1(&dyn, &dx[hLength]);
    _p.ModSquareK1(&_s);
    pn.x.ModNeg();
    pn.x.ModAdd(&_p);
    pn.x.ModSub(&Gn[hLength].x);
    pts[0] = pn;
}
