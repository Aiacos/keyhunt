/*
 * Optimized BSGS operations with batching and prefetching
 * Significantly improves cache utilization and reduces memory latency
 */

#include "bsgs_optimized.h"
#include "bloom/bloom.h"
#include <string.h>
#include <immintrin.h>

// Batched bloom filter check with prefetching and cache optimization
int bloom_check_batch(
    struct bloom *bloom_filters,
    struct PointBatch *batch,
    uint32_t *match_indices,
    uint32_t *match_count
) {
    if (!batch || batch->count == 0) {
        *match_count = 0;
        return 0;
    }

    uint32_t matches = 0;

    // Process batch with prefetching
    for (uint32_t i = 0; i < batch->count; i++) {
        // Prefetch next bloom filter while processing current
        if (i + 1 < batch->count) {
            uint8_t next_index = batch->xpoints[i + 1][0];
            prefetch_bloom_filters(bloom_filters, next_index);
        }

        // Get bloom filter index from first byte
        uint8_t filter_idx = batch->xpoints[i][0];

        // Check current point
        int result = bloom_check(&bloom_filters[filter_idx],
                                (char*)batch->xpoints[i], 32);

        if (result) {
            match_indices[matches] = batch->indices[i];
            matches++;
        }
    }

    *match_count = matches;
    return matches > 0 ? 1 : 0;
}

// Extract x-coordinates from Point array in optimized batches
// Note: This is a placeholder function for future use
void extract_xpoints_batch(
    void *points_raw __attribute__((unused)),
    int point_count __attribute__((unused)),
    struct PointBatch *batches,
    int *batch_count
) {
    // This function is currently a placeholder for future optimization
    // When integrated, it will extract x-coordinates from Point objects
    // in optimized batches using SIMD operations

    *batch_count = 0;
    if (batches) {
        init_point_batch(&batches[0]);
    }

    // TODO: Implement actual Point extraction when integrated with
    // the main BSGS loop. For now, extraction is done inline in
    // bsgs_check_points_batched() for better integration.
}

// Optimized bloom check with software prefetching for better cache performance
int bloom_check_optimized(struct bloom *bloom_filter, const char *data, size_t len) {
    // Prefetch the bloom filter data structure
    _mm_prefetch((const char*)bloom_filter, _MM_HINT_T0);
    _mm_prefetch((const char*)bloom_filter->bf, _MM_HINT_T0);

    // Use the standard bloom check after prefetching
    return bloom_check(bloom_filter, data, len);
}
