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
    bool has_avx512;
    bool has_sha_ni;

    // GPU information (primary GPU)
    int gpu_count;             // Number of GPUs detected
    bool has_nvidia;           // NVIDIA driver present
    bool has_cuda;             // CUDA-capable GPU available (NVIDIA)
    uint64_t gpu_vram_mb;      // Total VRAM for primary GPU
    char gpu_name[128];        // Model name for primary GPU

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

#ifdef __cplusplus
}
#endif

#endif // SYSINFO_H
