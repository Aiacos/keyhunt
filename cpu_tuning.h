#ifndef CPU_TUNING_H
#define CPU_TUNING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// CPU feature detection
struct CPUFeatures {
    int has_sse2;
    int has_avx2;
    int has_avx512;
    int cache_line_size;
    int l1_cache_size;
    int l2_cache_size;
    int l3_cache_size;
    int num_cores;
};

// Optimized parameters based on CPU
struct BSGSOptimizedParams {
    uint32_t cpu_grp_size;           // Optimized CPU_GRP_SIZE
    uint32_t bloom_batch_size;        // Batch size for bloom checks
    uint32_t prefetch_distance;       // How far ahead to prefetch
    uint32_t thread_workload;         // THREADBPWORKLOAD
    int use_avx512;                   // Whether to use AVX-512
    int use_avx2;                     // Whether to use AVX2
};

// Detect CPU features and cache sizes
void detect_cpu_features(struct CPUFeatures *features);

// Calculate optimal parameters based on CPU
void calculate_optimal_params(
    const struct CPUFeatures *features,
    uint64_t available_memory,
    struct BSGSOptimizedParams *params
);

// Get recommended CPU_GRP_SIZE for the current system
uint32_t get_optimal_cpu_grp_size(void);

// Get recommended batch size based on cache
uint32_t get_optimal_batch_size(void);

// Print optimization recommendations
void print_optimization_info(const struct BSGSOptimizedParams *params);

#ifdef __cplusplus
}
#endif

#endif // CPU_TUNING_H
