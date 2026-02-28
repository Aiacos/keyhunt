#include "gpu_backend.h"
#include <string.h>

static int g_available = 0;

int gpu_backend_init(gpu_backend_info_t *info) {
    if (info) {
        memset(info, 0, sizeof(*info));
        info->name[0] = '\0';
    }
    g_available = 0;
    return 0;
}

int gpu_backend_available(void) {
    return g_available;
}

void gpu_backend_shutdown(void) {
    g_available = 0;
}

int gpu_hash160_fromX_batch(const uint8_t *x32_be, size_t count,
                            uint8_t *out02, uint8_t *out03) {
    (void)x32_be;
    (void)count;
    (void)out02;
    (void)out03;
    return 1;
}

int gpu_upload_gtable(const uint8_t *gtable, size_t point_count) {
    (void)gtable;
    (void)point_count;
    return 1;
}

int gpu_upload_targets(const uint8_t *targets, size_t count) {
    (void)targets;
    (void)count;
    return 1;
}

int gpu_upload_bloom(const uint8_t *bloom_data, size_t bloom_size, int num_hashes) {
    (void)bloom_data;
    (void)bloom_size;
    (void)num_hashes;
    return 1;
}

int gpu_full_search(const gpu_search_config_t *config) {
    (void)config;
    return 0;
}

size_t gpu_get_optimal_batch_size(void) {
    return 0;
}

double gpu_benchmark(size_t duration_ms) {
    (void)duration_ms;
    return 0.0;
}

int gpu_autotune(size_t duration_ms, gpu_tune_result_t *result) {
    (void)duration_ms;
    if (result) memset(result, 0, sizeof(*result));
    return 1;
}

void gpu_apply_tune(const gpu_tune_result_t *tune) {
    (void)tune;
}

const char* gpu_backend_type_name(gpu_backend_type_t type) {
    (void)type;
    return "None";
}

int gpu_enumerate_backends(void) {
    return 0;
}

int gpu_backend_init_typed(gpu_backend_type_t backend_type, gpu_backend_info_t *info) {
    (void)backend_type;
    if (info) memset(info, 0, sizeof(*info));
    return 1;
}

gpu_backend_type_t gpu_backend_get_type(void) {
    return GPU_BACKEND_TYPE_NONE;
}

int gpu_backend_type_available(gpu_backend_type_t backend_type) {
    (void)backend_type;
    return 0;
}
