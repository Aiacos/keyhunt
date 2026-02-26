/*
 * Optimized BSGS Operations Module
 * High-performance batch operations for Baby Step Giant Step algorithm
 * C interface only - avoids conflicts with immintrin.h and Int.h macros
 */

#include <immintrin.h>
#include "bsgs_ops.h"
#include "../bloom/bloom.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __linux__
#include <cpuid.h>
#endif

// Check AVX2 availability
static bool check_avx2() {
#ifdef __linux__
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        return (ebx & (1 << 5)) != 0;
    }
#endif
    return false;
}

// Check AVX-512 availability
static bool check_avx512() {
#ifdef __linux__
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        return (ebx & (1 << 16)) != 0;
    }
#endif
    return false;
}

// Aligned allocation helper
static void* aligned_alloc_helper(size_t alignment, size_t size) {
    void *ptr = nullptr;
#ifdef _WIN64
    ptr = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) {
        ptr = malloc(size);
    }
#endif
    return ptr;
}

static void aligned_free_helper(void *ptr) {
#ifdef _WIN64
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// ============================================================================
// C Interface Implementation
// ============================================================================

extern "C" {

int bsgs_batch_init(bsgs_batch_ctx_t *ctx, int batch_size) {
    if (!ctx || batch_size < 64) return -1;

    memset(ctx, 0, sizeof(bsgs_batch_ctx_t));
    ctx->batch_size = batch_size;
    ctx->half_batch = batch_size / 2;

    // Allocate bloom results
    ctx->bloom_results = (uint8_t*)aligned_alloc_helper(CACHE_LINE_SIZE, batch_size);
    if (!ctx->bloom_results) goto fail;

    // Allocate xpoint raw data
    ctx->xpoint_raw = (uint8_t*)aligned_alloc_helper(CACHE_LINE_SIZE, batch_size * 32);
    if (!ctx->xpoint_raw) goto fail;

    ctx->initialized = true;
    return 0;

fail:
    bsgs_batch_free(ctx);
    return -1;
}

void bsgs_batch_free(bsgs_batch_ctx_t *ctx) {
    if (!ctx) return;

    if (ctx->bloom_results) aligned_free_helper(ctx->bloom_results);
    if (ctx->xpoint_raw) aligned_free_helper(ctx->xpoint_raw);

    memset(ctx, 0, sizeof(bsgs_batch_ctx_t));
}

void bsgs_batch_extract_xpoints(bsgs_batch_ctx_t *ctx, int num_points) {
    (void)ctx;
    (void)num_points;
    // This would be called from keyhunt.cpp with actual Point data
}

int bsgs_batch_bloom_check(bsgs_batch_ctx_t *ctx, void *bloom_array, int num_points) {
    if (!ctx || !ctx->initialized || !bloom_array) return 0;

    struct bloom *blooms = (struct bloom*)bloom_array;
    int hits = 0;

    // Reset results
    memset(ctx->bloom_results, 0, num_points);

    // Check each point against bloom filter with prefetching
    for (int i = 0; i < num_points; i++) {
        // Prefetch next xpoint
        if (i + PREFETCH_DISTANCE < num_points) {
            _mm_prefetch((const char*)(ctx->xpoint_raw + (i + PREFETCH_DISTANCE) * 32), _MM_HINT_T0);
        }

        uint8_t *xpoint = ctx->xpoint_raw + i * 32;
        uint8_t bloom_idx = xpoint[0];

        // Prefetch bloom filter
        _mm_prefetch((const char*)blooms[bloom_idx].bf, _MM_HINT_T0);

        if (bloom_check(&blooms[bloom_idx], (char*)xpoint, 32) == 1) {
            ctx->bloom_results[i] = 1;
            hits++;
        }
    }

    return hits;
}

// Batch compute points with optimized memory access
void bsgs_batch_compute_points(
    bsgs_batch_ctx_t *ctx,
    Point *startP,
    Point *GSn,
    Point *_2GSn,
    int hLength)
{
    // Stub implementation - validates parameters
    if (!ctx || !ctx->initialized) {
        return;
    }
    if (!startP || !GSn || !_2GSn) {
        return;
    }
    if (hLength < 0) {
        return;
    }

    // TODO: Implement actual batch point computation
    // This is a placeholder for the actual BSGS point computation logic
}

// Check if SIMD is available
int bsgs_ops_simd_available(void) {
    return check_avx2() ? (check_avx512() ? 2 : 1) : 0;
}

// Print SIMD capabilities
void bsgs_ops_print_caps(void) {
    printf("BSGS Ops Capabilities:\n");
    printf("  AVX2:    %s\n", check_avx2() ? "YES" : "NO");
    printf("  AVX-512: %s\n", check_avx512() ? "YES" : "NO");
}

} // extern "C"
