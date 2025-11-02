/*
 * CPU auto-tuning for optimal BSGS performance
 * Detects CPU features and calculates optimal parameters
 */

#include "cpu_tuning.h"
#include "hash/ripemd160.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cpuid.h>
#include <unistd.h>

// Detect CPU features using CPUID
void detect_cpu_features(struct CPUFeatures *features) {
    unsigned int eax, ebx, ecx, edx;

    memset(features, 0, sizeof(struct CPUFeatures));

    // Check SSE2 (CPUID.01H:EDX[26])
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        features->has_sse2 = (edx & (1 << 26)) != 0;
    }

    // Check AVX2 (CPUID.(EAX=07H, ECX=0H):EBX[5])
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        features->has_avx2 = (ebx & (1 << 5)) != 0;
        features->has_avx512 = (ebx & (1 << 16)) != 0;  // AVX-512F
    }

    // Detect cache sizes
    features->cache_line_size = 64;  // Standard for x86-64

    // Try to get L1/L2/L3 cache sizes
    // CPUID.04H for cache parameters
    for (int i = 0; i < 10; i++) {
        if (__get_cpuid_count(4, i, &eax, &ebx, &ecx, &edx)) {
            int cache_type = eax & 0x1F;
            if (cache_type == 0) break;  // No more caches

            int cache_level = (eax >> 5) & 0x7;
            int ways = ((ebx >> 22) & 0x3FF) + 1;
            int partitions = ((ebx >> 12) & 0x3FF) + 1;
            int line_size = (ebx & 0xFFF) + 1;
            int sets = ecx + 1;
            int cache_size = ways * partitions * line_size * sets;

            if (cache_level == 1 && cache_type == 1) {  // L1 data cache
                features->l1_cache_size = cache_size;
            } else if (cache_level == 2) {  // L2 cache
                features->l2_cache_size = cache_size;
            } else if (cache_level == 3) {  // L3 cache
                features->l3_cache_size = cache_size;
            }
        }
    }

    // Get number of cores
    features->num_cores = sysconf(_SC_NPROCESSORS_ONLN);
}

// Calculate optimal parameters based on detected features
void calculate_optimal_params(
    const struct CPUFeatures *features,
    uint64_t available_memory,
    struct BSGSOptimizedParams *params
) {
    // Default conservative values
    params->cpu_grp_size = 1024;
    params->bloom_batch_size = 64;
    params->prefetch_distance = 8;
    params->thread_workload = 1048576;
    params->use_avx512 = 0;
    params->use_avx2 = 0;

    // Enable SIMD based on CPU support
    if (features->has_avx512 && ripemd160_avx512_available()) {
        params->use_avx512 = 1;
        params->bloom_batch_size = 128;  // Larger batches for AVX-512
    } else if (features->has_avx2 && ripemd160_avx2_available()) {
        params->use_avx2 = 1;
        params->bloom_batch_size = 64;
    }

    // Optimize CPU_GRP_SIZE based on cache - AGGRESSIVE TUNING
    // Target: Use L3 cache for maximum batch size
    // Larger batches = fewer ModInv calls = better performance

    uint32_t target_cache = features->l3_cache_size;
    if (target_cache == 0) {
        target_cache = features->l2_cache_size;
    }

    if (target_cache > 0) {
        // Each point is ~64 bytes (Point structure)
        // IntGroup needs ~8 bytes per element
        // dx array needs ~32 bytes per element
        // Total per point: ~104 bytes

        // Use 70% of cache for working set (leave room for other data)
        uint32_t available_cache = (target_cache * 70) / 100;
        uint32_t max_points = available_cache / 104;

        // Round down to nearest power of 2
        uint32_t optimal_size = 8192;  // Start from maximum
        while (optimal_size > max_points && optimal_size > 1024) {
            optimal_size /= 2;
        }

        // Prefer larger sizes for better ModInv amortization
        // Minimum 2048 on modern CPUs, up to 16384 on high-end
        if (features->l3_cache_size >= 16 * 1024 * 1024) {
            // Large L3: aim for 8192 or 16384
            if (optimal_size < 8192) optimal_size = 8192;
        } else if (features->l3_cache_size >= 8 * 1024 * 1024) {
            // Medium L3: aim for 4096
            if (optimal_size < 4096) optimal_size = 4096;
        } else if (features->l2_cache_size >= 1 * 1024 * 1024) {
            // Good L2: aim for 2048
            if (optimal_size < 2048) optimal_size = 2048;
        }

        // Clamp to reasonable range: 1024-16384
        if (optimal_size < 1024) optimal_size = 1024;
        if (optimal_size > 16384) optimal_size = 16384;

        params->cpu_grp_size = optimal_size;
    }

    // Adjust batch size for cache line alignment
    if (features->cache_line_size > 0) {
        // Ensure batch size is multiple of cache lines
        params->bloom_batch_size = (params->bloom_batch_size +
                                    features->cache_line_size - 1) &
                                   ~(features->cache_line_size - 1);
    }

    // Prefetch distance based on memory latency
    // More cores = longer prefetch distance helps
    if (features->num_cores >= 16) {
        params->prefetch_distance = 16;
    } else if (features->num_cores >= 8) {
        params->prefetch_distance = 12;
    } else {
        params->prefetch_distance = 8;
    }

    // Adjust thread workload based on available memory
    // More memory = larger workload per thread
    if (available_memory > (8ULL * 1024 * 1024 * 1024)) {  // > 8GB
        params->thread_workload = 2097152;  // 2M
    } else if (available_memory > (4ULL * 1024 * 1024 * 1024)) {  // > 4GB
        params->thread_workload = 1048576;  // 1M
    } else {
        params->thread_workload = 524288;   // 512K
    }
}

// Get optimal CPU_GRP_SIZE for current system
uint32_t get_optimal_cpu_grp_size(void) {
    struct CPUFeatures features;
    struct BSGSOptimizedParams params;

    detect_cpu_features(&features);
    calculate_optimal_params(&features, 0, &params);

    return params.cpu_grp_size;
}

// Get optimal batch size
uint32_t get_optimal_batch_size(void) {
    struct CPUFeatures features;
    struct BSGSOptimizedParams params;

    detect_cpu_features(&features);
    calculate_optimal_params(&features, 0, &params);

    return params.bloom_batch_size;
}

// Print optimization information
void print_optimization_info(const struct BSGSOptimizedParams *params) {
    printf("\n[Optimization Parameters]\n");
    printf("  CPU_GRP_SIZE:     %u\n", params->cpu_grp_size);
    printf("  Bloom batch size: %u\n", params->bloom_batch_size);
    printf("  Prefetch distance: %u\n", params->prefetch_distance);
    printf("  Thread workload:  %u\n", params->thread_workload);
    printf("  SIMD mode:        ");

    if (params->use_avx512) {
        printf("AVX-512 (16-way parallel)\n");
    } else if (params->use_avx2) {
        printf("AVX2 (8-way parallel)\n");
    } else {
        printf("SSE2 (4-way parallel)\n");
    }

    printf("\n");
}
