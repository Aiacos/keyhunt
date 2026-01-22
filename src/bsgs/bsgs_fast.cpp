/*
 * Fast BSGS - Optimized Baby Step Giant Step Implementation
 * C interface only - avoids macro conflicts with Int.h
 */

#include <immintrin.h>
#include "bsgs_fast.h"
#include "../bloom/bloom.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __linux__
#include <cpuid.h>
#endif

// Global performance stats
static bsgs_perf_stats_t g_stats;
static bool g_initialized = false;
static bool g_avx2_available = false;
static bool g_avx512_available = false;

// Check CPU features
static void detect_cpu_features() {
#ifdef __linux__
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        g_avx2_available = (ebx & (1 << 5)) != 0;
        g_avx512_available = (ebx & (1 << 16)) != 0;
    }
#endif
}

// ============================================================================
// C Interface
// ============================================================================

extern "C" {

int bsgs_fast_init(void) {
    if (g_initialized) return 0;

    detect_cpu_features();
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
    return g_avx2_available ? (g_avx512_available ? 2 : 1) : 0;
}

void bsgs_fast_print_caps(void) {
    printf("BSGS Fast Capabilities:\n");
    printf("  AVX2:    %s\n", g_avx2_available ? "YES" : "NO");
    printf("  AVX-512: %s\n", g_avx512_available ? "YES" : "NO");
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
