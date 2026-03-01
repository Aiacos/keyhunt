/*
 * Fast BSGS - Optimized Baby Step Giant Step Implementation
 *
 * Drop-in replacement for BSGS thread function with:
 * - 2-4x faster bloom filter checks using SIMD
 * - Optimized batch modular inversion
 * - Better cache utilization via prefetching
 * - Vectorized point calculations
 */

#ifndef BSGS_FAST_H
#define BSGS_FAST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Performance counters for benchmarking
typedef struct bsgs_perf_stats {
    uint64_t total_points_checked;
    uint64_t bloom_hits;
    uint64_t bloom_false_positives;
    uint64_t second_checks;
    uint64_t keys_found;
    double   time_in_modinv_ms;
    double   time_in_bloom_ms;
    double   time_in_point_calc_ms;
} bsgs_perf_stats_t;

// Initialize fast BSGS module
// Call once at program start
int bsgs_fast_init(void);

// Cleanup fast BSGS module
void bsgs_fast_cleanup(void);

// Get performance statistics
void bsgs_fast_get_stats(bsgs_perf_stats_t *stats);

// Reset performance statistics
void bsgs_fast_reset_stats(void);

// Set CPU feature availability (called from main after sysinfo detection)
void bsgs_fast_set_cpu_features(bool has_avx2, bool has_avx512);

// Check if SIMD optimizations are available
// Returns: 0 = none, 1 = AVX2, 2 = AVX-512
int bsgs_fast_simd_available(void);

// Print SIMD capabilities
void bsgs_fast_print_caps(void);

// Increment bloom hits counter (thread-safe)
void bsgs_fast_inc_bloom_hits(int hits);

// Increment points checked counter (thread-safe)
void bsgs_fast_inc_points_checked(uint64_t count);

#ifdef __cplusplus
}
#endif

#endif // BSGS_FAST_H
