/*
 * async_pipeline.c - GPU Async Pipeline (Triple Buffering)
 *
 * Stub implementation for non-CUDA builds.
 * CUDA implementation would be in gpu_backend_cuda.cu.
 */

#include "async_pipeline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __CUDACC__

/* Stub pipeline structure */
struct async_pipeline_s {
    async_pipeline_config_t config;
    async_pipeline_stats_t stats;
    int initialized;
};

int async_pipeline_available(void) {
    return 0;  /* CUDA not available */
}

async_pipeline_t* async_pipeline_create(const async_pipeline_config_t *config) {
    (void)config;
    printf("[Async Pipeline] CUDA not available, pipeline disabled\n");
    return NULL;
}

int async_pipeline_submit(async_pipeline_t *pipeline,
                          const uint8_t *input_keys, size_t count) {
    (void)pipeline;
    (void)input_keys;
    (void)count;
    return -1;
}

int async_pipeline_get_results(async_pipeline_t *pipeline, int batch_id,
                               uint8_t *output_hashes, size_t max_count) {
    (void)pipeline;
    (void)batch_id;
    (void)output_hashes;
    (void)max_count;
    return -1;
}

void async_pipeline_sync(async_pipeline_t *pipeline) {
    (void)pipeline;
}

void async_pipeline_get_stats(const async_pipeline_t *pipeline,
                              async_pipeline_stats_t *stats) {
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
    (void)pipeline;
}

void async_pipeline_destroy(async_pipeline_t *pipeline) {
    if (pipeline) free(pipeline);
}

#endif /* !__CUDACC__ */
