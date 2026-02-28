/*
 * Optimized BSGS Operations Module
 * High-performance batch operations for Baby Step Giant Step algorithm
 * C interface with C++ implementation for elliptic curve operations
 */

#include <immintrin.h>
#include "bsgs_ops.h"
#include "../bloom/bloom.h"
#include "../secp256k1/Int.h"
#include "../secp256k1/Point.h"
#include "../secp256k1/IntGroup.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif

// CPU_GRP_SIZE must match the value in search_context.h
#ifndef CPU_GRP_SIZE
#define CPU_GRP_SIZE 1024
#endif

// Check AVX2 availability
static bool check_avx2() {
#if defined(__GNUC__) || defined(__clang__)
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        return (ebx & (1 << 5)) != 0;
    }
#endif
    return false;
}

// Check AVX-512 availability
static bool check_avx512() {
#if defined(__GNUC__) || defined(__clang__)
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
        ptr = nullptr;
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

    // Allocate point array (CPU_GRP_SIZE points, cache-aligned)
    ctx->pts = (Point*)aligned_alloc_helper(CACHE_LINE_SIZE, CPU_GRP_SIZE * sizeof(Point));
    if (!ctx->pts) goto fail;

    // Allocate dx array for batch modular inversion (CPU_GRP_SIZE/2 + 1)
    ctx->dx = (Int*)aligned_alloc_helper(CACHE_LINE_SIZE, (CPU_GRP_SIZE / 2 + 1) * sizeof(Int));
    if (!ctx->dx) goto fail;

    // Create IntGroup for batch modular inversion
    ctx->grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
    if (!ctx->grp) goto fail;

    // Set the dx array in the IntGroup
    ((IntGroup*)ctx->grp)->Set(ctx->dx);

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

    if (ctx->pts) aligned_free_helper(ctx->pts);
    if (ctx->dx) aligned_free_helper(ctx->dx);
    if (ctx->grp) delete (IntGroup*)ctx->grp;
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
// This implements the core BSGS point computation algorithm:
// - Computes dx values (x-coordinate differences)
// - Performs batch modular inversion using Montgomery's trick
// - Calculates points in both positive and negative directions
//
// Cache optimization strategy:
// - Aggressive prefetching with _mm_prefetch intrinsics
// - PREFETCH_DISTANCE (8) chosen for ~100 cycle latency hiding
// - _MM_HINT_T0: High temporal locality (L1 cache) for frequently accessed data
// - _MM_HINT_T1: Moderate temporal locality (L2 cache) for write-back buffers
// - Loop unrolling by 4 for instruction-level parallelism
// - Cache-aligned allocations (CACHE_LINE_SIZE = 64 bytes)
void bsgs_batch_compute_points(
    bsgs_batch_ctx_t *ctx,
    Point *startP,
    Point *GSn,
    Point *_2GSn,
    int hLength)
{
    // Validate parameters
    if (!ctx || !ctx->initialized) {
        return;
    }
    if (!startP || !GSn || !_2GSn) {
        return;
    }
    if (hLength < 0 || hLength >= CPU_GRP_SIZE / 2) {
        return;
    }

    Int *dx = ctx->dx;
    Point *pts = ctx->pts;
    IntGroup *grp = (IntGroup*)ctx->grp;

    // Temporary variables for point arithmetic
    Int dy, dyn, _s, _p;
    Point pp, pn;

    // Step 1: Compute dx values (differences in x-coordinates)
    // This computes: dx[i] = GSn[i].x - startP.x (mod p)
    // Loop unrolling with aggressive prefetching for better performance
    int i = 0;

    // Unroll by 4 for better instruction-level parallelism
    for (; i + 3 < hLength; i += 4) {
        // Prefetch future GSn points (read, high temporal locality)
        if (i + PREFETCH_DISTANCE < hLength) {
            _mm_prefetch((const char*)&GSn[i + PREFETCH_DISTANCE], _MM_HINT_T0);
        }
        // Prefetch dx write locations (write, moderate temporal locality)
        if (i + PREFETCH_DISTANCE < hLength) {
            _mm_prefetch((const char*)&dx[i + PREFETCH_DISTANCE], _MM_HINT_T1);
        }

        dx[i].ModSub(&GSn[i].x, &startP->x);
        dx[i + 1].ModSub(&GSn[i + 1].x, &startP->x);
        dx[i + 2].ModSub(&GSn[i + 2].x, &startP->x);
        dx[i + 3].ModSub(&GSn[i + 3].x, &startP->x);
    }

    // Handle remaining elements
    for (; i < hLength; i++) {
        dx[i].ModSub(&GSn[i].x, &startP->x);
    }

    // Compute dx for the center point
    dx[hLength].ModSub(&GSn[hLength].x, &startP->x);

    // Compute dx for the next center point (2*GSn)
    dx[hLength + 1].ModSub(&_2GSn->x, &startP->x);

    // Step 2: Batch modular inversion using Montgomery's trick
    // This is the expensive operation that benefits from batching
    // Complexity: O(n) modular multiplications + 1 modular inversion
    // instead of n modular inversions
    grp->Set(dx);
    grp->ModInvOptimized();

    // Step 3: Compute points using the batch-inverted dx values
    // We compute points symmetrically: P ± i*G for i = 1..hLength
    // This exploits the fact that P+iG and P-iG share the same dx inverse

    // Set center point
    pts[CPU_GRP_SIZE / 2] = *startP;

    // Compute positive and negative points with loop unrolling
    for (i = 0; i < hLength; i++) {
        // Prefetch upcoming GSn points for read (high temporal locality)
        if (i + PREFETCH_DISTANCE < hLength) {
            _mm_prefetch((const char*)&GSn[i + PREFETCH_DISTANCE], _MM_HINT_T0);
        }
        // Prefetch upcoming dx values (already computed, high temporal locality)
        if (i + PREFETCH_DISTANCE < hLength) {
            _mm_prefetch((const char*)&dx[i + PREFETCH_DISTANCE], _MM_HINT_T0);
        }
        // Prefetch pts write locations (moderate temporal locality)
        int pts_idx_p = CPU_GRP_SIZE / 2 + (i + PREFETCH_DISTANCE + 1);
        int pts_idx_n = CPU_GRP_SIZE / 2 - (i + PREFETCH_DISTANCE + 1);
        if (i + PREFETCH_DISTANCE < hLength) {
            _mm_prefetch((const char*)&pts[pts_idx_p], _MM_HINT_T1);
            _mm_prefetch((const char*)&pts[pts_idx_n], _MM_HINT_T1);
        }

        pp = *startP;
        pn = *startP;

        // Compute P = startP + i*G (positive direction)
        dy.ModSub(&GSn[i].y, &pp.y);

        // s = (p2.y - p1.y) * inverse(p2.x - p1.x)
        _s.ModMulK1(&dy, &dx[i]);

        // _p = s^2
        _p.ModSquareK1(&_s);

        // rx = s^2 - p1.x - p2.x
        pp.x.ModNeg();
        pp.x.ModAdd(&_p);
        pp.x.ModSub(&GSn[i].x);

        // ry = -p2.y - s*(rx - p2.x)
        pp.y.ModSub(&GSn[i].x, &pp.x);
        pp.y.ModMulK1(&_s);
        pp.y.ModSub(&GSn[i].y);

        // Compute P = startP - i*G (negative direction)
        dyn.Set(&GSn[i].y);
        dyn.ModNeg();
        dyn.ModSub(&pn.y);

        // s = (p2.y - p1.y) * inverse(p2.x - p1.x)
        _s.ModMulK1(&dyn, &dx[i]);

        // _p = s^2
        _p.ModSquareK1(&_s);

        // rx = s^2 - p1.x - p2.x
        pn.x.ModNeg();
        pn.x.ModAdd(&_p);
        pn.x.ModSub(&GSn[i].x);

        // ry = -p2.y - s*(rx - p2.x)
        pn.y.ModSub(&GSn[i].x, &pn.x);
        pn.y.ModMulK1(&_s);
        pn.y.ModSub(&dyn);

        // Store points symmetrically around center
        pts[CPU_GRP_SIZE / 2 + (i + 1)] = pp;
        pts[CPU_GRP_SIZE / 2 - (i + 1)] = pn;
    }

    // Compute the next center point using the last dx
    // This advances to the next batch: startP + (CPU_GRP_SIZE/2)*G
    dy.ModSub(&_2GSn->y, &startP->y);
    _s.ModMulK1(&dy, &dx[hLength + 1]);
    _p.ModSquareK1(&_s);

    pp.x.Set(&startP->x);
    pp.x.ModNeg();
    pp.x.ModAdd(&_p);
    pp.x.ModSub(&_2GSn->x);

    pp.y.ModSub(&_2GSn->x, &pp.x);
    pp.y.ModMulK1(&_s);
    pp.y.ModSub(&_2GSn->y);

    pts[0] = pp;
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
