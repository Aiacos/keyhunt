/*
 * gpu_backend_unified.c - Unified GPU Backend Dispatcher
 *
 * Implements unified GPU backend that can dispatch operations to both
 * CUDA and OpenCL backends simultaneously, enabling mixed-vendor GPU
 * systems (NVIDIA + AMD + Intel) to work together.
 *
 * Architecture:
 * - Enumerates all available GPU backends (CUDA, OpenCL)
 * - Creates unified device list across all backends
 * - Routes operations to appropriate backend based on device type
 * - Manages work distribution across mixed-vendor GPUs
 * - Aggregates results and statistics from all backends
 */

#include "gpu_backend.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Backend function pointer tables
 * ============================================================================
 * Each backend (CUDA, OpenCL) provides its own implementation of the
 * gpu_backend.h interface. The dispatcher maintains function pointers
 * to route calls to the appropriate backend.
 */

typedef struct {
    gpu_backend_type_t type;

    /* Lifecycle functions */
    int (*init)(gpu_backend_info_t *info);
    int (*available)(void);
    void (*shutdown)(void);

    /* Hash-only mode */
    int (*hash160_fromX_batch)(const uint8_t *x32_be, size_t count,
                               uint8_t *out02, uint8_t *out03);

    /* Full GPU mode */
    int (*upload_gtable)(const uint8_t *gtable, size_t point_count);
    int (*upload_targets)(const uint8_t *targets, size_t count);
    int (*upload_bloom)(const uint8_t *bloom_data, size_t bloom_size, int num_hashes);
    int (*full_search)(const gpu_search_config_t *config);

    /* Tuning and optimization */
    size_t (*get_optimal_batch_size)(void);
    double (*benchmark)(size_t duration_ms);
    int (*autotune)(size_t duration_ms, gpu_tune_result_t *result);
    void (*apply_tune)(const gpu_tune_result_t *tune);
} backend_vtable_t;

/* ============================================================================
 * Unified device tracking
 * ============================================================================
 * Track all devices across all backends with unified numbering
 */

#define MAX_UNIFIED_DEVICES 16

typedef struct {
    int global_device_id;           /* Unified device ID (0-15) */
    gpu_backend_type_t backend_type; /* Which backend owns this device */
    int backend_device_id;          /* Device ID within backend (0-N) */
    gpu_backend_info_t info;        /* Device information */
    double performance_weight;      /* Relative performance (for load balancing) */
    uint64_t keys_processed;        /* Statistics */
    double avg_throughput;          /* Keys/second */
} unified_device_t;

/* ============================================================================
 * Global unified backend state
 * ============================================================================
 */

static struct {
    int initialized;
    int device_count;
    unified_device_t devices[MAX_UNIFIED_DEVICES];

    /* Backend vtables */
    backend_vtable_t cuda_backend;
    backend_vtable_t opencl_backend;

    /* Active backends bitmask */
    int active_backends;  /* Bitmask of GPU_BACKEND_TYPE_* */

    /* Synchronization */
    platform_mutex_t lock;
} g_unified;

/* ============================================================================
 * Forward declarations for backend implementations
 * ============================================================================
 * These are implemented in gpu_backend_cuda.cu and gpu_backend_opencl.c
 */

/* CUDA backend (conditionally compiled) */
#ifdef HAVE_CUDA_BACKEND
extern int cuda_backend_init(gpu_backend_info_t *info);
extern int cuda_backend_available(void);
extern void cuda_backend_shutdown(void);
extern int cuda_hash160_fromX_batch(const uint8_t *x32_be, size_t count,
                                    uint8_t *out02, uint8_t *out03);
extern int cuda_upload_gtable(const uint8_t *gtable, size_t point_count);
extern int cuda_upload_targets(const uint8_t *targets, size_t count);
extern int cuda_upload_bloom(const uint8_t *bloom_data, size_t bloom_size, int num_hashes);
extern int cuda_full_search(const gpu_search_config_t *config);
extern size_t cuda_get_optimal_batch_size(void);
extern double cuda_benchmark(size_t duration_ms);
extern int cuda_autotune(size_t duration_ms, gpu_tune_result_t *result);
extern void cuda_apply_tune(const gpu_tune_result_t *tune);
#endif

/* OpenCL backend (conditionally compiled) */
#ifdef HAVE_OPENCL_BACKEND
extern int opencl_backend_init(gpu_backend_info_t *info);
extern int opencl_backend_available(void);
extern void opencl_backend_shutdown(void);
extern int opencl_hash160_fromX_batch(const uint8_t *x32_be, size_t count,
                                      uint8_t *out02, uint8_t *out03);
extern int opencl_upload_gtable(const uint8_t *gtable, size_t point_count);
extern int opencl_upload_targets(const uint8_t *targets, size_t count);
extern int opencl_upload_bloom(const uint8_t *bloom_data, size_t bloom_size, int num_hashes);
extern int opencl_full_search(const gpu_search_config_t *config);
extern size_t opencl_get_optimal_batch_size(void);
extern double opencl_benchmark(size_t duration_ms);
extern int opencl_autotune(size_t duration_ms, gpu_tune_result_t *result);
extern void opencl_apply_tune(const gpu_tune_result_t *tune);
#endif

/* ============================================================================
 * Backend detection and initialization
 * ============================================================================
 */

static void init_backend_vtables(void) {
    memset(&g_unified.cuda_backend, 0, sizeof(backend_vtable_t));
    memset(&g_unified.opencl_backend, 0, sizeof(backend_vtable_t));

#ifdef HAVE_CUDA_BACKEND
    g_unified.cuda_backend.type = GPU_BACKEND_TYPE_CUDA;
    g_unified.cuda_backend.init = cuda_backend_init;
    g_unified.cuda_backend.available = cuda_backend_available;
    g_unified.cuda_backend.shutdown = cuda_backend_shutdown;
    g_unified.cuda_backend.hash160_fromX_batch = cuda_hash160_fromX_batch;
    g_unified.cuda_backend.upload_gtable = cuda_upload_gtable;
    g_unified.cuda_backend.upload_targets = cuda_upload_targets;
    g_unified.cuda_backend.upload_bloom = cuda_upload_bloom;
    g_unified.cuda_backend.full_search = cuda_full_search;
    g_unified.cuda_backend.get_optimal_batch_size = cuda_get_optimal_batch_size;
    g_unified.cuda_backend.benchmark = cuda_benchmark;
    g_unified.cuda_backend.autotune = cuda_autotune;
    g_unified.cuda_backend.apply_tune = cuda_apply_tune;
#endif

#ifdef HAVE_OPENCL_BACKEND
    g_unified.opencl_backend.type = GPU_BACKEND_TYPE_OPENCL;
    g_unified.opencl_backend.init = opencl_backend_init;
    g_unified.opencl_backend.available = opencl_backend_available;
    g_unified.opencl_backend.shutdown = opencl_backend_shutdown;
    g_unified.opencl_backend.hash160_fromX_batch = opencl_hash160_fromX_batch;
    g_unified.opencl_backend.upload_gtable = opencl_upload_gtable;
    g_unified.opencl_backend.upload_targets = opencl_upload_targets;
    g_unified.opencl_backend.upload_bloom = opencl_upload_bloom;
    g_unified.opencl_backend.full_search = opencl_full_search;
    g_unified.opencl_backend.get_optimal_batch_size = opencl_get_optimal_batch_size;
    g_unified.opencl_backend.benchmark = opencl_benchmark;
    g_unified.opencl_backend.autotune = opencl_autotune;
    g_unified.opencl_backend.apply_tune = opencl_apply_tune;
#endif
}

static int enumerate_backend_devices(backend_vtable_t *backend, gpu_backend_type_t type) {
    if (!backend || !backend->init || !backend->available) {
        return 0;
    }

    if (!backend->available()) {
        return 0;
    }

    gpu_backend_info_t info;
    if (backend->init(&info) != 0) {
        return 0;
    }

    /* Add devices from this backend to unified device list */
    int device_count = info.gpu_count;
    if (device_count <= 0) {
        return 0;
    }

    /* Limit to available slots */
    int slots_available = MAX_UNIFIED_DEVICES - g_unified.device_count;
    if (device_count > slots_available) {
        device_count = slots_available;
    }

    for (int i = 0; i < device_count; i++) {
        unified_device_t *dev = &g_unified.devices[g_unified.device_count];
        dev->global_device_id = g_unified.device_count;
        dev->backend_type = type;
        dev->backend_device_id = i;
        dev->info = info;  /* Copy info from first device (TODO: query per-device) */
        dev->info.backend_type = type;
        dev->performance_weight = 1.0;  /* Default, can be tuned */
        dev->keys_processed = 0;
        dev->avg_throughput = 0.0;

        g_unified.device_count++;
    }

    /* Mark backend as active */
    g_unified.active_backends |= (1 << type);

    return device_count;
}

/* ============================================================================
 * Unified backend API implementation
 * ============================================================================
 */

int gpu_backend_init(gpu_backend_info_t *info) {
    if (g_unified.initialized) {
        if (info) *info = g_unified.devices[0].info;
        return 0;
    }

    memset(&g_unified, 0, sizeof(g_unified));
    platform_mutex_init(&g_unified.lock);

    init_backend_vtables();

    /* Enumerate devices from all available backends */
    int total_devices = 0;

#ifdef HAVE_CUDA_BACKEND
    int cuda_count = enumerate_backend_devices(&g_unified.cuda_backend, GPU_BACKEND_TYPE_CUDA);
    if (cuda_count > 0) {
        printf("[Unified GPU] Found %d CUDA device%s\n", cuda_count, cuda_count > 1 ? "s" : "");
        total_devices += cuda_count;
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    int opencl_count = enumerate_backend_devices(&g_unified.opencl_backend, GPU_BACKEND_TYPE_OPENCL);
    if (opencl_count > 0) {
        printf("[Unified GPU] Found %d OpenCL device%s\n", opencl_count, opencl_count > 1 ? "s" : "");
        total_devices += opencl_count;
    }
#endif

    if (total_devices == 0) {
        printf("[Unified GPU] No GPU devices found\n");
        return -1;
    }

    g_unified.initialized = 1;

    /* Return info about first device */
    if (info) {
        *info = g_unified.devices[0].info;
        info->backend_type = GPU_BACKEND_TYPE_UNIFIED;
        info->gpu_count = total_devices;
    }

    printf("[Unified GPU] Initialized with %d total device%s across %d backend%s\n",
           total_devices, total_devices > 1 ? "s" : "",
           __builtin_popcount(g_unified.active_backends),
           __builtin_popcount(g_unified.active_backends) > 1 ? "s" : "");

    return 0;
}

int gpu_backend_available(void) {
    int available = 0;

#ifdef HAVE_CUDA_BACKEND
    if (g_unified.cuda_backend.available && g_unified.cuda_backend.available()) {
        available = 1;
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    if (g_unified.opencl_backend.available && g_unified.opencl_backend.available()) {
        available = 1;
    }
#endif

    return available;
}

void gpu_backend_shutdown(void) {
    if (!g_unified.initialized) {
        return;
    }

    platform_mutex_lock(&g_unified.lock);

#ifdef HAVE_CUDA_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_CUDA)) {
        if (g_unified.cuda_backend.shutdown) {
            g_unified.cuda_backend.shutdown();
        }
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) {
        if (g_unified.opencl_backend.shutdown) {
            g_unified.opencl_backend.shutdown();
        }
    }
#endif

    g_unified.initialized = 0;
    g_unified.device_count = 0;
    g_unified.active_backends = 0;

    platform_mutex_unlock(&g_unified.lock);
    platform_mutex_destroy(&g_unified.lock);

    printf("[Unified GPU] Shutdown complete\n");
}

/* ============================================================================
 * Multi-backend utility functions
 * ============================================================================
 */

const char* gpu_backend_type_name(gpu_backend_type_t type) {
    switch (type) {
        case GPU_BACKEND_TYPE_CUDA:    return "CUDA";
        case GPU_BACKEND_TYPE_OPENCL:  return "OpenCL";
        case GPU_BACKEND_TYPE_UNIFIED: return "Unified";
        case GPU_BACKEND_TYPE_NONE:
        default:                       return "None";
    }
}

int gpu_enumerate_backends(void) {
    int available = 0;

#ifdef HAVE_CUDA_BACKEND
    if (g_unified.cuda_backend.available && g_unified.cuda_backend.available()) {
        available |= (1 << GPU_BACKEND_TYPE_CUDA);
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    if (g_unified.opencl_backend.available && g_unified.opencl_backend.available()) {
        available |= (1 << GPU_BACKEND_TYPE_OPENCL);
    }
#endif

    return available;
}

int gpu_backend_init_typed(gpu_backend_type_t backend_type, gpu_backend_info_t *info) {
    backend_vtable_t *backend = NULL;

    switch (backend_type) {
#ifdef HAVE_CUDA_BACKEND
        case GPU_BACKEND_TYPE_CUDA:
            backend = &g_unified.cuda_backend;
            break;
#endif
#ifdef HAVE_OPENCL_BACKEND
        case GPU_BACKEND_TYPE_OPENCL:
            backend = &g_unified.opencl_backend;
            break;
#endif
        default:
            return -1;
    }

    if (!backend || !backend->init) {
        return -1;
    }

    return backend->init(info);
}

gpu_backend_type_t gpu_backend_get_type(void) {
    if (!g_unified.initialized) {
        return GPU_BACKEND_TYPE_NONE;
    }

    /* If multiple backends active, return unified */
    if (__builtin_popcount(g_unified.active_backends) > 1) {
        return GPU_BACKEND_TYPE_UNIFIED;
    }

    /* Single backend active */
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_CUDA)) {
        return GPU_BACKEND_TYPE_CUDA;
    }
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) {
        return GPU_BACKEND_TYPE_OPENCL;
    }

    return GPU_BACKEND_TYPE_NONE;
}

int gpu_backend_type_available(gpu_backend_type_t backend_type) {
    switch (backend_type) {
#ifdef HAVE_CUDA_BACKEND
        case GPU_BACKEND_TYPE_CUDA:
            return g_unified.cuda_backend.available ? g_unified.cuda_backend.available() : 0;
#endif
#ifdef HAVE_OPENCL_BACKEND
        case GPU_BACKEND_TYPE_OPENCL:
            return g_unified.opencl_backend.available ? g_unified.opencl_backend.available() : 0;
#endif
        case GPU_BACKEND_TYPE_UNIFIED:
            return gpu_backend_available();
        default:
            return 0;
    }
}

/* ============================================================================
 * Dispatched operations
 * ============================================================================
 * Route operations to all active backends or to the primary backend
 */

int gpu_hash160_fromX_batch(const uint8_t *x32_be, size_t count,
                            uint8_t *out02, uint8_t *out03) {
    if (!g_unified.initialized) {
        return -1;
    }

    /* Use primary backend (first available) */
#ifdef HAVE_CUDA_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_CUDA)) {
        if (g_unified.cuda_backend.hash160_fromX_batch) {
            return g_unified.cuda_backend.hash160_fromX_batch(x32_be, count, out02, out03);
        }
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) {
        if (g_unified.opencl_backend.hash160_fromX_batch) {
            return g_unified.opencl_backend.hash160_fromX_batch(x32_be, count, out02, out03);
        }
    }
#endif

    return -1;
}

int gpu_upload_gtable(const uint8_t *gtable, size_t point_count) {
    if (!g_unified.initialized) {
        return -1;
    }

    /* Upload to all active backends */
    int result = 0;

#ifdef HAVE_CUDA_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_CUDA)) {
        if (g_unified.cuda_backend.upload_gtable) {
            result |= g_unified.cuda_backend.upload_gtable(gtable, point_count);
        }
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) {
        if (g_unified.opencl_backend.upload_gtable) {
            result |= g_unified.opencl_backend.upload_gtable(gtable, point_count);
        }
    }
#endif

    return result;
}

int gpu_upload_targets(const uint8_t *targets, size_t count) {
    if (!g_unified.initialized) {
        return -1;
    }

    /* Upload to all active backends */
    int result = 0;

#ifdef HAVE_CUDA_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_CUDA)) {
        if (g_unified.cuda_backend.upload_targets) {
            result |= g_unified.cuda_backend.upload_targets(targets, count);
        }
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) {
        if (g_unified.opencl_backend.upload_targets) {
            result |= g_unified.opencl_backend.upload_targets(targets, count);
        }
    }
#endif

    return result;
}

int gpu_upload_bloom(const uint8_t *bloom_data, size_t bloom_size, int num_hashes) {
    if (!g_unified.initialized) {
        return -1;
    }

    /* Upload to all active backends */
    int result = 0;

#ifdef HAVE_CUDA_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_CUDA)) {
        if (g_unified.cuda_backend.upload_bloom) {
            result |= g_unified.cuda_backend.upload_bloom(bloom_data, bloom_size, num_hashes);
        }
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) {
        if (g_unified.opencl_backend.upload_bloom) {
            result |= g_unified.opencl_backend.upload_bloom(bloom_data, bloom_size, num_hashes);
        }
    }
#endif

    return result;
}

int gpu_full_search(const gpu_search_config_t *config) {
    if (!g_unified.initialized || !config) {
        return -1;
    }

    /* For now, dispatch to all backends sequentially */
    /* TODO: Parallel dispatch with work distribution */
    int total_found = 0;

#ifdef HAVE_CUDA_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_CUDA)) {
        if (g_unified.cuda_backend.full_search) {
            int found = g_unified.cuda_backend.full_search(config);
            if (found > 0) total_found += found;
        }
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) {
        if (g_unified.opencl_backend.full_search) {
            int found = g_unified.opencl_backend.full_search(config);
            if (found > 0) total_found += found;
        }
    }
#endif

    return total_found;
}

size_t gpu_get_optimal_batch_size(void) {
    if (!g_unified.initialized) {
        return 0;
    }

    /* Return maximum batch size from all backends */
    size_t max_size = 0;

#ifdef HAVE_CUDA_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_CUDA)) {
        if (g_unified.cuda_backend.get_optimal_batch_size) {
            size_t size = g_unified.cuda_backend.get_optimal_batch_size();
            if (size > max_size) max_size = size;
        }
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) {
        if (g_unified.opencl_backend.get_optimal_batch_size) {
            size_t size = g_unified.opencl_backend.get_optimal_batch_size();
            if (size > max_size) max_size = size;
        }
    }
#endif

    return max_size;
}

double gpu_benchmark(size_t duration_ms) {
    if (!g_unified.initialized) {
        return 0.0;
    }

    /* Sum throughput from all backends */
    double total_throughput = 0.0;

#ifdef HAVE_CUDA_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_CUDA)) {
        if (g_unified.cuda_backend.benchmark) {
            total_throughput += g_unified.cuda_backend.benchmark(duration_ms);
        }
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) {
        if (g_unified.opencl_backend.benchmark) {
            total_throughput += g_unified.opencl_backend.benchmark(duration_ms);
        }
    }
#endif

    return total_throughput;
}

int gpu_autotune(size_t duration_ms, gpu_tune_result_t *result) {
    if (!g_unified.initialized || !result) {
        return -1;
    }

    /* Auto-tune primary backend (first available) */
#ifdef HAVE_CUDA_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_CUDA)) {
        if (g_unified.cuda_backend.autotune) {
            return g_unified.cuda_backend.autotune(duration_ms, result);
        }
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) {
        if (g_unified.opencl_backend.autotune) {
            return g_unified.opencl_backend.autotune(duration_ms, result);
        }
    }
#endif

    return -1;
}

void gpu_apply_tune(const gpu_tune_result_t *tune) {
    if (!g_unified.initialized || !tune) {
        return;
    }

    /* Apply tuning to all backends */
#ifdef HAVE_CUDA_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_CUDA)) {
        if (g_unified.cuda_backend.apply_tune) {
            g_unified.cuda_backend.apply_tune(tune);
        }
    }
#endif

#ifdef HAVE_OPENCL_BACKEND
    if (g_unified.active_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) {
        if (g_unified.opencl_backend.apply_tune) {
            g_unified.opencl_backend.apply_tune(tune);
        }
    }
#endif
}
