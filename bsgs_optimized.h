#ifndef BSGS_OPTIMIZED_H
#define BSGS_OPTIMIZED_H

#include <stdint.h>
#include <immintrin.h>
#include "bloom/bloom.h"

#ifdef __cplusplus
extern "C" {
#endif

// Optimized batch size for bloom filter checking
// Should be a multiple of cache line size (64 bytes)
#define BLOOM_BATCH_SIZE 64

// Structure for batched point checking
struct PointBatch {
    unsigned char xpoints[BLOOM_BATCH_SIZE][32] __attribute__((aligned(64)));
    uint32_t indices[BLOOM_BATCH_SIZE];
    uint32_t count;
};

// Batched bloom filter check with prefetching
// Returns number of matches found
int bloom_check_batch(
    struct bloom *bloom_filters,  // Array of 256 bloom filters
    struct PointBatch *batch,
    uint32_t *match_indices,      // Output: indices of matching points
    uint32_t *match_count         // Output: number of matches
);

// Optimized point extraction with SIMD
// Extracts x-coordinates from points array in batches
void extract_xpoints_batch(
    void *points,                 // Array of Point objects
    int point_count,
    struct PointBatch *batches,
    int *batch_count
);

// Prefetch bloom filter data for upcoming checks
static inline void prefetch_bloom_filters(struct bloom *filters, uint8_t index) {
    _mm_prefetch((const char*)&filters[index], _MM_HINT_T0);
    _mm_prefetch((const char*)filters[index].bf, _MM_HINT_T0);
}

// Initialize a point batch
static inline void init_point_batch(struct PointBatch *batch) {
    batch->count = 0;
}

// Add a point to batch, returns 1 if batch is full
static inline int add_to_batch(struct PointBatch *batch, const unsigned char *xpoint, uint32_t index) {
    if (batch->count >= BLOOM_BATCH_SIZE) {
        return 1;  // Batch full
    }

    // Use SIMD for faster copy if available
    #ifdef __AVX2__
    __m256i data = _mm256_loadu_si256((const __m256i*)xpoint);
    _mm256_store_si256((__m256i*)batch->xpoints[batch->count], data);
    #else
    memcpy(batch->xpoints[batch->count], xpoint, 32);
    #endif

    batch->indices[batch->count] = index;
    batch->count++;

    return (batch->count >= BLOOM_BATCH_SIZE) ? 1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif // BSGS_OPTIMIZED_H
