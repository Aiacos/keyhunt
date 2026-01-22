#ifndef SYSINFO_H
#define SYSINFO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// System information structure
typedef struct {
    // CPU information
    int cpu_physical_cores;    // Physical CPU cores (without HT)
    int cpu_logical_cores;     // Logical CPU cores (with HT)
    int cpu_threads_optimal;   // Optimal thread count
    char cpu_model[128];       // CPU model name

    // Cache information (in KB)
    uint64_t cache_l1_size;    // L1 cache per core
    uint64_t cache_l2_size;    // L2 cache per core
    uint64_t cache_l3_size;    // L3 cache total

    // Memory information (in MB)
    uint64_t ram_total;        // Total RAM
    uint64_t ram_available;    // Available RAM
    uint64_t ram_free;         // Free RAM

    // CPU features
    bool has_avx2;
    bool has_avx512;           // Any AVX-512 support
    bool has_avx512f;          // AVX-512 Foundation
    bool has_avx512dq;         // AVX-512 Doubleword/Quadword (needed for ripemd160_avx512)
    bool has_avx512bw;         // AVX-512 Byte/Word
    bool has_avx512vl;         // AVX-512 Vector Length Extensions
    bool has_sha_ni;           // Intel SHA extensions
    int numa_nodes;            // NUMA node count (for future affinity)

    // GPU information (primary GPU)
    int gpu_count;             // Number of GPUs detected
    bool has_nvidia;           // NVIDIA driver present
    bool has_cuda;             // CUDA-capable GPU available (NVIDIA)
    uint64_t gpu_vram_mb;      // Total VRAM for primary GPU
    char gpu_name[128];        // Model name for primary GPU
    int gpu_sm_count;          // Streaming Multiprocessors (if available)
    int gpu_compute_capability; // e.g., 75 for sm_75
    int gpu_memory_bus_width;  // Memory bus width in bits

    // Performance scores (computed)
    float cpu_score;           // Relative CPU performance score
    float gpu_score;           // Relative GPU performance score
    float hybrid_ratio;        // Optimal GPU work percentage (0.0-1.0)

    // Auto-tuning recommendations
    int recommended_threads;
    uint32_t recommended_batch_size;
    uint32_t recommended_workload;
    uint64_t recommended_n;        // Optimal N parameter for range coverage
    int recommended_kfactor;       // Optimal K factor
} system_info_t;

// Initialize and detect system information
void sysinfo_init(system_info_t *info);

// Print system information
void sysinfo_print(const system_info_t *info);

// Get optimal parameters based on system info
void sysinfo_get_optimal_params(
    const system_info_t *info,
    int *threads,
    uint32_t *batch_size,
    uint32_t *workload_per_thread,
    uint64_t *n_value,
    int *kfactor
);

// Calculate performance scores
// cpu_score = cores * (1 + 0.5*avx2 + 1.0*avx512)
// gpu_score = sm_count * compute_capability_factor
// hybrid_ratio = gpu_score / (cpu_score + gpu_score)
void sysinfo_compute_scores(system_info_t *info);

// Get recommended hybrid mode split percentage for GPU
// Returns value 0-100 representing % of work for GPU
int sysinfo_get_hybrid_gpu_percent(const system_info_t *info);

#ifdef __cplusplus
}
#endif

#endif // SYSINFO_H
