// src/benchmark.h
#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Benchmark results
typedef struct {
    double cpu_speed_mkeys;      // CPU-only speed
    double gpu_speed_mkeys;      // GPU-only speed
    double hybrid_speed_mkeys;   // Combined speed
    int cpu_threads;
    char gpu_name[64];
    int gpu_count;
    double efficiency_ratio;     // hybrid / (cpu + gpu)
} benchmark_result_t;

// Run full benchmark (takes ~30 seconds)
int benchmark_run(benchmark_result_t *result, int duration_seconds);

// Print benchmark results with recommendations
void benchmark_print_results(const benchmark_result_t *result, int bits);

// Quick benchmark for auto-tuning (~5 seconds)
int benchmark_quick(double *cpu_speed, double *gpu_speed);

#ifdef __cplusplus
}
#endif

#endif // BENCHMARK_H
