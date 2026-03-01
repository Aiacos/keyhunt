/*
 * Fast BSGS - Optimized Baby Step Giant Step Implementation
 * C interface only - avoids macro conflicts with Int.h
 *
 * CPU feature detection is handled by the caller (keyhunt.cpp) via
 * bsgs_fast_set_cpu_features() -- no local cpuid detection here.
 */

#include <immintrin.h>
#include "bsgs_fast.h"
#include "../bloom/bloom.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Global performance stats
static bsgs_perf_stats_t g_stats;
static bool g_initialized = false;
static bool g_bsgs_avx2 = false;
static bool g_bsgs_avx512 = false;

// ============================================================================
// C Interface
// ============================================================================

extern "C" {

void bsgs_fast_set_cpu_features(bool has_avx2, bool has_avx512) {
    g_bsgs_avx2 = has_avx2;
    g_bsgs_avx512 = has_avx512;
}

int bsgs_fast_init(void) {
    if (g_initialized) return 0;

    memset(&g_stats, 0, sizeof(g_stats));
    g_initialized = true;

    return 0;
}

void bsgs_fast_cleanup(void) {
    g_initialized = false;
}

void bsgs_fast_get_stats(bsgs_perf_stats_t *stats) {
    if (stats) {
        *stats = g_stats;
    }
}

void bsgs_fast_reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
}

int bsgs_fast_simd_available(void) {
    return g_bsgs_avx2 ? (g_bsgs_avx512 ? 2 : 1) : 0;
}

void bsgs_fast_print_caps(void) {
    printf("BSGS Fast Capabilities:\n");
    printf("  AVX2:    %s\n", g_bsgs_avx2 ? "YES" : "NO");
    printf("  AVX-512: %s\n", g_bsgs_avx512 ? "YES" : "NO");
}

// Increment bloom hits counter (thread-safe)
void bsgs_fast_inc_bloom_hits(int hits) {
    __atomic_add_fetch(&g_stats.bloom_hits, hits, __ATOMIC_RELAXED);
}

// Increment points checked counter (thread-safe)
void bsgs_fast_inc_points_checked(uint64_t count) {
    __atomic_add_fetch(&g_stats.total_points_checked, count, __ATOMIC_RELAXED);
}

} // extern "C"
