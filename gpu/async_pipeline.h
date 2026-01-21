/*
 * async_pipeline.h - GPU Async Pipeline (Triple Buffering)
 *
 * Provides overlap of GPU compute, memory transfers, and CPU processing
 * for maximum GPU utilization. Requires CUDA to be enabled.
 *
 * Architecture:
 *   Time ->
 *           ┌─────────┐┌─────────┐┌─────────┐
 *   GPU:    │Compute A││Compute B││Compute C│ ...
 *           └─────────┘└─────────┘└─────────┘
 *                ┌─────────┐┌─────────┐
 *   Transfer:    │Upload B ││Upload C │ ...
 *                │Download A│Download B│
 *                └─────────┘└─────────┘
 *                     ┌─────────┐┌─────────┐
 *   CPU:              │Process A││Process B│ ...
 *                     │Prepare C││Prepare D│
 *                     └─────────┘└─────────┘
 */

#ifndef ASYNC_PIPELINE_H
#define ASYNC_PIPELINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of buffers for triple buffering */
#define ASYNC_PIPELINE_NUM_BUFFERS 3

/* Maximum batch size per buffer (keys) */
#define ASYNC_PIPELINE_MAX_BATCH (1 << 20)  /* 1M keys */

/* Pipeline configuration */
typedef struct {
    size_t batch_size;          /* Keys per batch */
    size_t result_size;         /* Bytes per result (typically 20 for RIPEMD160) */
    bool use_pinned_memory;     /* Use pinned host memory for faster transfers */
    int device_id;              /* GPU device ID */
} async_pipeline_config_t;

/* Pipeline statistics */
typedef struct {
    uint64_t batches_processed;
    uint64_t keys_processed;
    double avg_compute_ms;
    double avg_transfer_ms;
    double gpu_utilization;     /* 0.0 - 1.0 */
} async_pipeline_stats_t;

/* Opaque pipeline handle */
typedef struct async_pipeline_s async_pipeline_t;

/**
 * Create and initialize async pipeline
 * @param config Pipeline configuration
 * @return Pipeline handle, or NULL on failure
 */
async_pipeline_t* async_pipeline_create(const async_pipeline_config_t *config);

/**
 * Submit a batch for processing
 * @param pipeline Pipeline handle
 * @param input_keys Input private keys (32 bytes each, big-endian)
 * @param count Number of keys in batch
 * @return Batch ID (>= 0) or negative error code
 */
int async_pipeline_submit(async_pipeline_t *pipeline,
                          const uint8_t *input_keys, size_t count);

/**
 * Retrieve results from a completed batch
 * @param pipeline Pipeline handle
 * @param batch_id Batch ID from submit
 * @param output_hashes Output buffer for RIPEMD160 hashes (20 bytes each)
 * @param max_count Maximum results to retrieve
 * @return Number of results written, or negative error code
 */
int async_pipeline_get_results(async_pipeline_t *pipeline, int batch_id,
                               uint8_t *output_hashes, size_t max_count);

/**
 * Wait for all submitted batches to complete
 * @param pipeline Pipeline handle
 */
void async_pipeline_sync(async_pipeline_t *pipeline);

/**
 * Get pipeline statistics
 * @param pipeline Pipeline handle
 * @param stats Output statistics structure
 */
void async_pipeline_get_stats(const async_pipeline_t *pipeline,
                              async_pipeline_stats_t *stats);

/**
 * Destroy pipeline and free resources
 * @param pipeline Pipeline handle
 */
void async_pipeline_destroy(async_pipeline_t *pipeline);

/**
 * Check if async pipeline is available (CUDA built and initialized)
 * @return 1 if available, 0 otherwise
 */
int async_pipeline_available(void);

#ifdef __cplusplus
}
#endif

#endif /* ASYNC_PIPELINE_H */
