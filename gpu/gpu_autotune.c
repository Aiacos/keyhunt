/*
 * gpu_autotune.c - Runtime GPU performance tuning
 *
 * Runs mini-benchmarks to find optimal kernel parameters for the current GPU.
 * This is a CPU-side stub that calls into CUDA when available.
 */

#include "gpu_backend.h"
#include <stdio.h>
#include <string.h>

/* Stub implementation when CUDA not available */
#ifndef __CUDACC__

int gpu_autotune(size_t duration_ms, gpu_tune_result_t *result) {
    (void)duration_ms;
    if (!result) return -1;

    /* Return conservative defaults */
    result->blocks_per_sm = 16;
    result->keys_per_thread = 512;
    result->threads_per_block = 256;
    result->measured_mkeys = 0.0;

    printf("[GPU Autotune] CUDA not available, using defaults\n");
    return 0;
}

void gpu_apply_tune(const gpu_tune_result_t *tune) {
    (void)tune;
    /* No-op without CUDA */
}

#endif /* !__CUDACC__ */
