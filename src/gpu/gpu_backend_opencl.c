/*
 * OpenCL backend for keyhunt
 * Implements: secp256k1 ECC + SHA256 + RIPEMD160 + bloom/matching
 * For AMD GPUs (RX 6000/7000 series) and other OpenCL 1.2+ devices
 *
 * NOTE: Written in C-style for maximum compatibility
 */

#include "gpu_backend.h"
#include "opencl_check.h"

#ifndef HAVE_OPENCL_BACKEND
#error "gpu_backend_opencl.c must be compiled with -DHAVE_OPENCL_BACKEND=1"
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

// Platform abstraction for cross-platform timing
#include "../platform/platform_time.h"

// Parameter validation
#include "../core/parameter_validator.h"

// ============================================================================
// Global state (C-style)
// ============================================================================

static int g_available = 0;
static gpu_backend_info_t g_info;

// ============================================================================
// Multi-device support infrastructure
// ============================================================================
//
// OpenCL multi-device strategy (following CUDA backend patterns):
// 1. Enumerate all OpenCL platforms (AMD, NVIDIA, Intel, etc.)
// 2. Enumerate all GPU devices across all platforms
// 3. Create separate context and command queue per device
// 4. Distribute work proportionally based on device performance weights
// 5. Track per-device statistics (throughput, keys processed)
// 6. Support heterogeneous multi-vendor systems (AMD + NVIDIA + Intel)
//
// Performance weights are assigned based on:
// - Vendor (AMD RDNA > NVIDIA via OpenCL > Intel)
// - Compute units (more CUs = higher weight)
// - Optimal parameters (blocks/CU, keys/work-item, work-group size)
//
// Work distribution: Faster devices get proportionally more work per iteration,
// reducing overall search time on heterogeneous systems.
// ============================================================================

#define MAX_OPENCL_DEVICES 8
#define MAX_OPENCL_PLATFORMS 4
#define MAX_FOUND_KEYS 256

// 256-bit integer for private keys and ECC operations
typedef struct {
    uint32_t d[8];
} uint256_d;

// Found key result structure (must match kernel definition)
typedef struct {
    uint256_d privkey;
    int compressed;  // 0 = uncompressed, 2/3 = compressed prefix
    int valid;
} FoundKey;

// Device-specific optimal parameters (similar to CUDA's arch_params_t)
typedef struct {
    int blocks_per_cu;         // Blocks per compute unit
    int keys_per_work_item;    // Keys processed per work-item
    int work_group_size;       // Work-items per work-group
    double performance_weight; // Relative performance (for load balancing)
} opencl_device_params_t;

// Per-device context
typedef struct {
    int device_id;
    int active;

    // OpenCL objects
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;

    // Device properties
    char device_name[128];
    char vendor_name[64];
    cl_uint compute_units;
    size_t max_work_group_size;
    cl_ulong global_mem_size;
    cl_ulong local_mem_size;

    // Device-specific optimal parameters
    opencl_device_params_t optimal_params;

    // Device memory buffers
    cl_mem d_GTable;
    cl_mem d_targets;
    cl_mem d_bloom;

    // Found keys buffers
    cl_mem d_found_keys;
    cl_mem d_found_count;

    // Compiled kernels (to be loaded in later subtasks)
    cl_program program;
    cl_kernel kernel_hash160;
    cl_kernel kernel_full_search;

    // Statistics
    volatile uint64_t keys_processed;
    double avg_throughput_mkeys;  // Moving average throughput (MKey/s)
} opencl_device_t;

static opencl_device_t g_devices[MAX_OPENCL_DEVICES];
static int g_device_count = 0;
static cl_platform_id g_platforms[MAX_OPENCL_PLATFORMS];
static int g_platform_count = 0;

// Host-side copies for upload to all devices
static uint8_t *h_GTable_copy = NULL;
static uint8_t *h_targets_copy = NULL;
static uint8_t *h_bloom_copy = NULL;
static size_t g_GTable_count = 0;
static size_t g_target_count = 0;
static size_t g_bloom_size = 0;
static int g_bloom_hashes = 0;

// ============================================================================
// 256-bit arithmetic (host helpers)
// ============================================================================

static inline int u256_cmp_host(const uint256_d *a, const uint256_d *b) {
    for (int i = 7; i >= 0; i--) {
        if (a->d[i] < b->d[i]) return -1;
        if (a->d[i] > b->d[i]) return 1;
    }
    return 0;
}

static inline void u256_add_u64_host(uint256_d *a, uint64_t v) {
    uint64_t carry = v;
    for (int i = 0; i < 8 && carry; i++) {
        uint64_t sum = (uint64_t)a->d[i] + (carry & 0xFFFFFFFFULL);
        a->d[i] = (uint32_t)sum;
        carry = (sum >> 32) + (carry >> 32);
    }
}

static inline uint64_t u256_sub_sat_u64_host(const uint256_d *a, const uint256_d *b) {
    // Return a - b as uint64_t, saturating to 0 if b > a or UINT64_MAX if overflow
    int cmp = u256_cmp_host(a, b);
    if (cmp <= 0) return 0;

    // Simple case: only bottom 2 words differ
    uint64_t diff = ((uint64_t)a->d[1] << 32) | a->d[0];
    uint64_t b_low = ((uint64_t)b->d[1] << 32) | b->d[0];

    // Check if upper words are same
    int same_upper = 1;
    for (int i = 2; i < 8; i++) {
        if (a->d[i] != b->d[i]) {
            same_upper = 0;
            break;
        }
    }

    if (same_upper) {
        if (diff >= b_low) {
            return diff - b_low;
        }
    }

    // Too large, saturate
    return UINT64_MAX;
}

// ============================================================================
// OpenCL kernel source (to be populated in subtasks 2-3, 2-4, 2-5)
// ============================================================================

static char *g_opencl_kernel_source = NULL;
static size_t g_opencl_kernel_source_len = 0;

// Get optimal parameters based on device vendor and compute units
static opencl_device_params_t get_optimal_params_opencl(const char *vendor, cl_uint compute_units) {
    opencl_device_params_t params;

    // Default conservative values
    params.blocks_per_cu = 16;
    params.keys_per_work_item = 1024;
    params.work_group_size = 256;
    params.performance_weight = 1.0;

    // Vendor-specific tuning based on architecture characteristics
    if (strstr(vendor, "AMD") || strstr(vendor, "Advanced Micro Devices")) {
        // AMD RDNA architecture (RX 6000/7000 series)
        if (compute_units >= 60) {
            // High-end AMD (RX 7900 XT/XTX, RX 6900 XT)
            params.blocks_per_cu = 32;
            params.keys_per_work_item = 2048;
            params.work_group_size = 256;
            params.performance_weight = 1.2;
        } else if (compute_units >= 40) {
            // Mid-range AMD (RX 7800 XT, RX 6800)
            params.blocks_per_cu = 28;
            params.keys_per_work_item = 1536;
            params.work_group_size = 256;
            params.performance_weight = 1.1;
        } else if (compute_units >= 24) {
            // Entry-level AMD (RX 7600, RX 6700)
            params.blocks_per_cu = 24;
            params.keys_per_work_item = 1024;
            params.work_group_size = 256;
            params.performance_weight = 1.0;
        } else {
            // Low-end AMD
            params.blocks_per_cu = 16;
            params.keys_per_work_item = 512;
            params.work_group_size = 256;
            params.performance_weight = 0.8;
        }
    } else if (strstr(vendor, "NVIDIA")) {
        // NVIDIA GPUs via OpenCL (less optimal than CUDA, but supported)
        if (compute_units >= 80) {
            // High-end NVIDIA (RTX 3090, RTX 4090)
            params.blocks_per_cu = 24;
            params.keys_per_work_item = 1536;
            params.work_group_size = 256;
            params.performance_weight = 1.1;
        } else if (compute_units >= 40) {
            // Mid-range NVIDIA (RTX 3070, RTX 4070)
            params.blocks_per_cu = 20;
            params.keys_per_work_item = 1024;
            params.work_group_size = 256;
            params.performance_weight = 1.0;
        } else {
            // Entry-level NVIDIA
            params.blocks_per_cu = 16;
            params.keys_per_work_item = 512;
            params.work_group_size = 256;
            params.performance_weight = 0.9;
        }
    } else if (strstr(vendor, "Intel")) {
        // Intel GPUs (Arc series, integrated GPUs)
        if (compute_units >= 32) {
            // Intel Arc discrete GPUs
            params.blocks_per_cu = 20;
            params.keys_per_work_item = 1024;
            params.work_group_size = 256;
            params.performance_weight = 0.9;
        } else {
            // Intel integrated GPUs (conservative)
            params.blocks_per_cu = 12;
            params.keys_per_work_item = 512;
            params.work_group_size = 128;
            params.performance_weight = 0.6;
        }
    }

    return params;
}

// Load OpenCL kernel source from file
static int load_opencl_kernel_source(void) {
    if (g_opencl_kernel_source != NULL) {
        return 0; // Already loaded
    }

    // Try to load from file in src/gpu/ directory
    const char *kernel_file = "src/gpu/gpu_hash_opencl.cl";
    FILE *f = fopen(kernel_file, "rb");
    if (!f) {
        fprintf(stderr, "[OpenCL] Failed to open kernel file: %s\n", kernel_file);
        return -1;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fprintf(stderr, "[OpenCL] Invalid kernel file size\n");
        fclose(f);
        return -1;
    }

    // Allocate buffer and read
    g_opencl_kernel_source = (char*)malloc(file_size + 1);
    if (!g_opencl_kernel_source) {
        fprintf(stderr, "[OpenCL] Failed to allocate memory for kernel source\n");
        fclose(f);
        return -1;
    }

    size_t read_size = fread(g_opencl_kernel_source, 1, file_size, f);
    fclose(f);

    if (read_size != (size_t)file_size) {
        fprintf(stderr, "[OpenCL] Failed to read kernel file completely\n");
        free(g_opencl_kernel_source);
        g_opencl_kernel_source = NULL;
        return -1;
    }

    g_opencl_kernel_source[file_size] = '\0';
    g_opencl_kernel_source_len = file_size;

    printf("[OpenCL] Loaded kernel source from %s (%zu bytes)\n", kernel_file, g_opencl_kernel_source_len);
    return 0;
}

// ============================================================================
// Kernel compilation and loading
// ============================================================================

// Compile OpenCL kernels for a specific device
static int compile_kernels_for_device(opencl_device_t *dev) {
    cl_int err;

    if (!dev || !dev->active) {
        return -1;
    }

    printf("[OpenCL] Compiling kernels for device %d: %s\n",
           dev->device_id, dev->device_name);

    // Create program from source
    size_t source_len = strlen(g_opencl_kernel_source);
    dev->program = clCreateProgramWithSource(dev->context, 1,
                                             &g_opencl_kernel_source,
                                             &source_len, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OpenCL] Failed to create program for device %d: %s (%d)\n",
                dev->device_id, clGetErrorString(err), err);
        return -1;
    }

    // Build program with optimizations
    // Note: -cl-fast-relaxed-math for aggressive optimizations (like CUDA's -use_fast_math)
    const char *build_options = "-cl-fast-relaxed-math -cl-mad-enable -Werror";
    err = clBuildProgram(dev->program, 1, &dev->device, build_options, NULL, NULL);

    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OpenCL] Failed to build program for device %d: %s (%d)\n",
                dev->device_id, clGetErrorString(err), err);

        // Get build log for debugging
        size_t log_size = 0;
        clGetProgramBuildInfo(dev->program, dev->device, CL_PROGRAM_BUILD_LOG,
                             0, NULL, &log_size);

        if (log_size > 1) {
            char *build_log = (char*)malloc(log_size + 1);
            if (build_log) {
                clGetProgramBuildInfo(dev->program, dev->device, CL_PROGRAM_BUILD_LOG,
                                     log_size, build_log, NULL);
                build_log[log_size] = '\0';
                fprintf(stderr, "[OpenCL] Build log:\n%s\n", build_log);
                free(build_log);
            }
        }

        clReleaseProgram(dev->program);
        dev->program = NULL;
        return -1;
    }

    printf("[OpenCL] Kernels compiled successfully for device %d\n", dev->device_id);

    // Create kernel objects
    // Subtask 2-3: Hash-only mode kernel
    dev->kernel_hash160 = clCreateKernel(dev->program, "kernel_hash160_fromX", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OpenCL] Failed to create kernel_hash160_fromX: %s (%d)\n",
                clGetErrorString(err), err);
        clReleaseProgram(dev->program);
        dev->program = NULL;
        dev->kernel_hash160 = NULL;
        dev->kernel_full_search = NULL;
        return -1;
    }

    // Subtask 2-4, 2-5: Full search kernel
    dev->kernel_full_search = clCreateKernel(dev->program, "kernel_full_search_compressed", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OpenCL] Warning: Failed to create kernel_full_search_compressed: %s (%d)\n",
                clGetErrorString(err), err);
        // Non-fatal - this kernel may not be implemented yet
        dev->kernel_full_search = NULL;
    }

    printf("[OpenCL] Created kernel objects for device %d\n", dev->device_id);
    return 0;
}

// ============================================================================
// Device enumeration and initialization
// ============================================================================

// Enumerate OpenCL platforms and devices
static int enumerate_opencl_devices(void) {
    cl_int err;
    cl_uint num_platforms = 0;

    // Get number of platforms
    err = clGetPlatformIDs(MAX_OPENCL_PLATFORMS, g_platforms, &num_platforms);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OpenCL] No OpenCL platforms found: %s (%d)\n",
                clGetErrorString(err), err);
        return 0;
    }

    if (num_platforms == 0) {
        fprintf(stderr, "[OpenCL] No OpenCL platforms available\n");
        return 0;
    }

    g_platform_count = (int)num_platforms;
    printf("[OpenCL] Found %d platform(s)\n", g_platform_count);

    // Enumerate devices on each platform
    int total_devices = 0;
    for (int p = 0; p < g_platform_count && total_devices < MAX_OPENCL_DEVICES; p++) {
        char platform_name[128] = {0};
        char platform_vendor[64] = {0};

        clGetPlatformInfo(g_platforms[p], CL_PLATFORM_NAME,
                         sizeof(platform_name), platform_name, NULL);
        clGetPlatformInfo(g_platforms[p], CL_PLATFORM_VENDOR,
                         sizeof(platform_vendor), platform_vendor, NULL);

        printf("[OpenCL] Platform %d: %s (%s)\n", p, platform_name, platform_vendor);

        // Get GPU devices on this platform
        cl_device_id devices[MAX_OPENCL_DEVICES];
        cl_uint num_devices = 0;

        err = clGetDeviceIDs(g_platforms[p], CL_DEVICE_TYPE_GPU,
                            MAX_OPENCL_DEVICES - total_devices,
                            devices, &num_devices);

        if (err != CL_SUCCESS) {
            // No GPU devices on this platform, try CPU as fallback
            err = clGetDeviceIDs(g_platforms[p], CL_DEVICE_TYPE_ALL,
                                MAX_OPENCL_DEVICES - total_devices,
                                devices, &num_devices);
            if (err != CL_SUCCESS) {
                printf("[OpenCL]   No usable devices on this platform\n");
                continue;
            }
        }

        // Store device information
        for (cl_uint d = 0; d < num_devices && total_devices < MAX_OPENCL_DEVICES; d++) {
            opencl_device_t *dev = &g_devices[total_devices];
            memset(dev, 0, sizeof(*dev));

            dev->device = devices[d];
            dev->device_id = total_devices;
            dev->active = 1;

            // Query device properties
            clGetDeviceInfo(dev->device, CL_DEVICE_NAME,
                           sizeof(dev->device_name), dev->device_name, NULL);
            clGetDeviceInfo(dev->device, CL_DEVICE_VENDOR,
                           sizeof(dev->vendor_name), dev->vendor_name, NULL);
            clGetDeviceInfo(dev->device, CL_DEVICE_MAX_COMPUTE_UNITS,
                           sizeof(dev->compute_units), &dev->compute_units, NULL);
            clGetDeviceInfo(dev->device, CL_DEVICE_MAX_WORK_GROUP_SIZE,
                           sizeof(dev->max_work_group_size), &dev->max_work_group_size, NULL);
            clGetDeviceInfo(dev->device, CL_DEVICE_GLOBAL_MEM_SIZE,
                           sizeof(dev->global_mem_size), &dev->global_mem_size, NULL);
            clGetDeviceInfo(dev->device, CL_DEVICE_LOCAL_MEM_SIZE,
                           sizeof(dev->local_mem_size), &dev->local_mem_size, NULL);

            // Set optimal parameters based on vendor and compute units
            dev->optimal_params = get_optimal_params_opencl(dev->vendor_name, dev->compute_units);

            // Initialize statistics
            dev->keys_processed = 0;
            dev->avg_throughput_mkeys = 0.0;

            printf("[OpenCL]   Device %d: %s\n", total_devices, dev->device_name);
            printf("[OpenCL]     Vendor: %s\n", dev->vendor_name);
            printf("[OpenCL]     Compute Units: %u\n", dev->compute_units);
            printf("[OpenCL]     Max Work Group Size: %zu\n", dev->max_work_group_size);
            printf("[OpenCL]     Global Memory: %llu MB\n",
                   (unsigned long long)(dev->global_mem_size / (1024 * 1024)));
            printf("[OpenCL]     Local Memory: %llu KB\n",
                   (unsigned long long)(dev->local_mem_size / 1024));
            printf("[OpenCL]     Optimal config: %d blocks/CU, %d keys/work-item, WG size %d\n",
                   dev->optimal_params.blocks_per_cu,
                   dev->optimal_params.keys_per_work_item,
                   dev->optimal_params.work_group_size);

            // Create context for this device
            dev->context = clCreateContext(NULL, 1, &dev->device, NULL, NULL, &err);
            if (err != CL_SUCCESS) {
                fprintf(stderr, "[OpenCL] Failed to create context for device %d: %s (%d)\n",
                       total_devices, clGetErrorString(err), err);
                dev->active = 0;
                continue;
            }

            // Create command queue
#ifdef CL_VERSION_2_0
            // OpenCL 2.0+ API
            dev->queue = clCreateCommandQueueWithProperties(dev->context, dev->device,
                                                           NULL, &err);
#else
            // OpenCL 1.x API (deprecated but more widely supported)
            dev->queue = clCreateCommandQueue(dev->context, dev->device, 0, &err);
#endif
            if (err != CL_SUCCESS) {
                fprintf(stderr, "[OpenCL] Failed to create command queue for device %d: %s (%d)\n",
                       total_devices, clGetErrorString(err), err);
                clReleaseContext(dev->context);
                dev->active = 0;
                continue;
            }

            // Compile kernels for this device
            if (compile_kernels_for_device(dev) != 0) {
                fprintf(stderr, "[OpenCL] Failed to compile kernels for device %d\n",
                       total_devices);
                clReleaseCommandQueue(dev->queue);
                clReleaseContext(dev->context);
                dev->active = 0;
                continue;
            }

            total_devices++;
        }
    }

    g_device_count = total_devices;

    if (g_device_count == 0) {
        fprintf(stderr, "[OpenCL] No usable OpenCL devices found\n");
        return 0;
    }

    printf("[OpenCL] Successfully initialized %d device(s)\n", g_device_count);
    return g_device_count;
}

// ============================================================================
// Backend interface implementation
// ============================================================================

int gpu_backend_init(gpu_backend_info_t *info) {
    if (g_available) {
        if (info) {
            memcpy(info, &g_info, sizeof(*info));
        }
        return 0;
    }

    memset(&g_info, 0, sizeof(g_info));
    memset(g_devices, 0, sizeof(g_devices));
    memset(g_hash_buffers, 0, sizeof(g_hash_buffers));
    g_device_count = 0;
    g_platform_count = 0;

    // Load kernel source first
    if (load_opencl_kernel_source() < 0) {
        fprintf(stderr, "[OpenCL] Failed to load kernel source\n");
        g_available = 0;
        if (info) {
            memset(info, 0, sizeof(*info));
        }
        return -1;
    }

    // Enumerate devices
    int device_count = enumerate_opencl_devices();
    if (device_count == 0) {
        g_available = 0;
        if (info) {
            memset(info, 0, sizeof(*info));
        }
        return -1;
    }

    // Populate info struct with first device's information
    opencl_device_t *dev = &g_devices[0];
    g_info.gpu_count = g_device_count;
    g_info.vram_mb = (uint64_t)(dev->global_mem_size / (1024 * 1024));
    snprintf(g_info.name, sizeof(g_info.name), "%s", dev->device_name);
    g_info.multiprocessors = dev->compute_units;
    g_info.max_threads_per_block = (int)dev->max_work_group_size;

    // OpenCL doesn't have compute capability like CUDA, use placeholder
    g_info.compute_major = 1;
    g_info.compute_minor = 2;

    g_available = 1;

    if (info) {
        memcpy(info, &g_info, sizeof(*info));
    }

    return 0;
}

int gpu_backend_available(void) {
    return g_available;
}

// Get information about all OpenCL devices
int gpu_backend_get_device_count(void) {
    return g_available ? g_device_count : 0;
}

// Get information about a specific OpenCL device
int gpu_backend_get_device_info(int device_id, gpu_backend_info_t *info) {
    if (!g_available || device_id < 0 || device_id >= g_device_count || !info) {
        return -1;
    }

    opencl_device_t *dev = &g_devices[device_id];
    if (!dev->active) {
        return -1;
    }

    memset(info, 0, sizeof(*info));
    info->gpu_count = 1;  // Single device info
    info->vram_mb = (uint64_t)(dev->global_mem_size / (1024 * 1024));
    snprintf(info->name, sizeof(info->name), "%s", dev->device_name);
    info->multiprocessors = dev->compute_units;
    info->max_threads_per_block = (int)dev->max_work_group_size;
    info->compute_major = 1;
    info->compute_minor = 2;

    return 0;
}

// Get multi-device statistics
void gpu_backend_get_stats(uint64_t *total_keys, double *total_mkeys_per_sec) {
    if (!g_available || !total_keys || !total_mkeys_per_sec) {
        return;
    }

    *total_keys = 0;
    *total_mkeys_per_sec = 0.0;

    for (int d = 0; d < g_device_count; d++) {
        opencl_device_t *dev = &g_devices[d];
        if (dev->active) {
            *total_keys += dev->keys_processed;
            *total_mkeys_per_sec += dev->avg_throughput_mkeys;
        }
    }
}

void gpu_backend_shutdown(void) {
    // Release device resources
    for (int i = 0; i < g_device_count; i++) {
        opencl_device_t *dev = &g_devices[i];
        if (!dev->active) continue;

        // Release hash mode buffers
        hash_mode_buffers_t *hash_buf = &g_hash_buffers[i];
        if (hash_buf->d_x32) {
            clReleaseMemObject(hash_buf->d_x32);
            hash_buf->d_x32 = NULL;
        }
        if (hash_buf->d_out02) {
            clReleaseMemObject(hash_buf->d_out02);
            hash_buf->d_out02 = NULL;
        }
        if (hash_buf->d_out03) {
            clReleaseMemObject(hash_buf->d_out03);
            hash_buf->d_out03 = NULL;
        }
        hash_buf->capacity = 0;

        // Release kernels
        if (dev->kernel_hash160) {
            clReleaseKernel(dev->kernel_hash160);
            dev->kernel_hash160 = NULL;
        }
        if (dev->kernel_full_search) {
            clReleaseKernel(dev->kernel_full_search);
            dev->kernel_full_search = NULL;
        }

        // Release program
        if (dev->program) {
            clReleaseProgram(dev->program);
            dev->program = NULL;
        }

        // Release buffers
        if (dev->d_GTable) {
            clReleaseMemObject(dev->d_GTable);
            dev->d_GTable = NULL;
        }
        if (dev->d_targets) {
            clReleaseMemObject(dev->d_targets);
            dev->d_targets = NULL;
        }
        if (dev->d_bloom) {
            clReleaseMemObject(dev->d_bloom);
            dev->d_bloom = NULL;
        }
        if (dev->d_found_keys) {
            clReleaseMemObject(dev->d_found_keys);
            dev->d_found_keys = NULL;
        }
        if (dev->d_found_count) {
            clReleaseMemObject(dev->d_found_count);
            dev->d_found_count = NULL;
        }

        // Release queue and context
        if (dev->queue) {
            clReleaseCommandQueue(dev->queue);
            dev->queue = NULL;
        }
        if (dev->context) {
            clReleaseContext(dev->context);
            dev->context = NULL;
        }

        dev->active = 0;
    }

    // Release host copies
    if (h_GTable_copy) {
        free(h_GTable_copy);
        h_GTable_copy = NULL;
    }
    if (h_targets_copy) {
        free(h_targets_copy);
        h_targets_copy = NULL;
    }
    if (h_bloom_copy) {
        free(h_bloom_copy);
        h_bloom_copy = NULL;
    }

    // Release kernel source
    if (g_opencl_kernel_source) {
        free(g_opencl_kernel_source);
        g_opencl_kernel_source = NULL;
        g_opencl_kernel_source_len = 0;
    }

    g_device_count = 0;
    g_platform_count = 0;
    g_available = 0;

    printf("[OpenCL] Backend shutdown complete\n");
}

// ============================================================================
// Mode 1: Hash-only mode implementation (subtask 2-3)
// ============================================================================

// Device memory for hash-only mode (per-device)
typedef struct {
    cl_mem d_x32;
    cl_mem d_out02;
    cl_mem d_out03;
    size_t capacity;
} hash_mode_buffers_t;

static hash_mode_buffers_t g_hash_buffers[MAX_OPENCL_DEVICES];

int gpu_hash160_fromX_batch(const uint8_t *x32_be, size_t count,
                            uint8_t *out02, uint8_t *out03) {
    if (!g_available || count == 0) {
        return -1;
    }

    if (g_device_count == 0) {
        fprintf(stderr, "[OpenCL] No devices available\n");
        return -1;
    }

    // Use first active device for hash-only mode
    opencl_device_t *dev = NULL;
    hash_mode_buffers_t *buffers = NULL;
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].active) {
            dev = &g_devices[i];
            buffers = &g_hash_buffers[i];
            break;
        }
    }

    if (!dev || !dev->kernel_hash160) {
        fprintf(stderr, "[OpenCL] No active device with hash160 kernel\n");
        return -1;
    }

    cl_int err;

    // Allocate or resize device memory if needed
    if (count > buffers->capacity) {
        // Release old buffers
        if (buffers->d_x32) {
            clReleaseMemObject(buffers->d_x32);
            buffers->d_x32 = NULL;
        }
        if (buffers->d_out02) {
            clReleaseMemObject(buffers->d_out02);
            buffers->d_out02 = NULL;
        }
        if (buffers->d_out03) {
            clReleaseMemObject(buffers->d_out03);
            buffers->d_out03 = NULL;
        }

        // Allocate new buffers
        buffers->d_x32 = clCreateBuffer(dev->context, CL_MEM_READ_ONLY,
                                        count * 32, NULL, &err);
        if (err != CL_SUCCESS) {
            fprintf(stderr, "[OpenCL] Failed to allocate d_x32: %s (%d)\n",
                    clGetErrorString(err), err);
            return -1;
        }

        buffers->d_out02 = clCreateBuffer(dev->context, CL_MEM_WRITE_ONLY,
                                          count * 20, NULL, &err);
        if (err != CL_SUCCESS) {
            fprintf(stderr, "[OpenCL] Failed to allocate d_out02: %s (%d)\n",
                    clGetErrorString(err), err);
            clReleaseMemObject(buffers->d_x32);
            buffers->d_x32 = NULL;
            return -1;
        }

        buffers->d_out03 = clCreateBuffer(dev->context, CL_MEM_WRITE_ONLY,
                                          count * 20, NULL, &err);
        if (err != CL_SUCCESS) {
            fprintf(stderr, "[OpenCL] Failed to allocate d_out03: %s (%d)\n",
                    clGetErrorString(err), err);
            clReleaseMemObject(buffers->d_x32);
            clReleaseMemObject(buffers->d_out02);
            buffers->d_x32 = NULL;
            buffers->d_out02 = NULL;
            return -1;
        }

        buffers->capacity = count;
    }

    // Upload input data
    err = clEnqueueWriteBuffer(dev->queue, buffers->d_x32, CL_FALSE, 0,
                              count * 32, x32_be, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OpenCL] Failed to upload x32 data: %s (%d)\n",
                clGetErrorString(err), err);
        return -1;
    }

    // Set kernel arguments
    cl_ulong count_u64 = (cl_ulong)count;
    err = clSetKernelArg(dev->kernel_hash160, 0, sizeof(cl_mem), &buffers->d_x32);
    err |= clSetKernelArg(dev->kernel_hash160, 1, sizeof(cl_ulong), &count_u64);
    err |= clSetKernelArg(dev->kernel_hash160, 2, sizeof(cl_mem), &buffers->d_out02);
    err |= clSetKernelArg(dev->kernel_hash160, 3, sizeof(cl_mem), &buffers->d_out03);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OpenCL] Failed to set kernel arguments: %s (%d)\n",
                clGetErrorString(err), err);
        return -1;
    }

    // Launch kernel
    size_t global_work_size = count;
    size_t local_work_size = 256;  // Standard work group size

    // Round up global work size to multiple of local work size
    global_work_size = ((count + local_work_size - 1) / local_work_size) * local_work_size;

    err = clEnqueueNDRangeKernel(dev->queue, dev->kernel_hash160, 1, NULL,
                                &global_work_size, &local_work_size,
                                0, NULL, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OpenCL] Failed to launch kernel: %s (%d)\n",
                clGetErrorString(err), err);
        return -1;
    }

    // Download results
    err = clEnqueueReadBuffer(dev->queue, buffers->d_out02, CL_FALSE, 0,
                             count * 20, out02, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OpenCL] Failed to download out02: %s (%d)\n",
                clGetErrorString(err), err);
        return -1;
    }

    err = clEnqueueReadBuffer(dev->queue, buffers->d_out03, CL_TRUE, 0,
                             count * 20, out03, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OpenCL] Failed to download out03: %s (%d)\n",
                clGetErrorString(err), err);
        return -1;
    }

    // Wait for completion (CL_TRUE in last read already does this)
    return 0;
}

// ============================================================================
// Mode 2: Full GPU search (stubs - to be implemented in subtasks 2-4, 2-5)
// ============================================================================

int gpu_upload_gtable(const uint8_t *gtable, size_t point_count) {
    if (!g_available) {
        fprintf(stderr, "[OpenCL] Backend not initialized\n");
        return -1;
    }

    // Store host copy for later upload to devices
    size_t gtable_bytes = point_count * 64; // Each point: 32 bytes X + 32 bytes Y

    if (h_GTable_copy) {
        free(h_GTable_copy);
    }

    h_GTable_copy = (uint8_t*)malloc(gtable_bytes);
    if (!h_GTable_copy) {
        fprintf(stderr, "[OpenCL] Failed to allocate host GTable copy\n");
        return -1;
    }

    memcpy(h_GTable_copy, gtable, gtable_bytes);
    g_GTable_count = point_count;

    // Upload to all active devices
    cl_int err;
    for (int i = 0; i < g_device_count; i++) {
        opencl_device_t *dev = &g_devices[i];
        if (!dev->active) continue;

        // Release old buffer if exists
        if (dev->d_GTable) {
            clReleaseMemObject(dev->d_GTable);
            dev->d_GTable = NULL;
        }

        // Allocate and upload
        dev->d_GTable = clCreateBuffer(dev->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                       gtable_bytes, h_GTable_copy, &err);
        if (err != CL_SUCCESS) {
            fprintf(stderr, "[OpenCL] Failed to upload GTable to device %d: %s (%d)\n",
                    i, clGetErrorString(err), err);
            return -1;
        }
    }

    printf("[OpenCL] GTable uploaded to %d device(s) (%zu points, %.2f MB)\n",
           g_device_count, point_count, (double)gtable_bytes / (1024.0 * 1024.0));

    return 0;
}

int gpu_upload_targets(const uint8_t *targets, size_t count) {
    if (!g_available) {
        fprintf(stderr, "[OpenCL] Backend not initialized\n");
        return -1;
    }

    size_t targets_bytes = count * 20; // 20 bytes per HASH160 target

    if (h_targets_copy) {
        free(h_targets_copy);
    }

    h_targets_copy = (uint8_t*)malloc(targets_bytes);
    if (!h_targets_copy) {
        fprintf(stderr, "[OpenCL] Failed to allocate host targets copy\n");
        return -1;
    }

    memcpy(h_targets_copy, targets, targets_bytes);
    g_target_count = count;

    // Upload to all active devices
    cl_int err;
    for (int i = 0; i < g_device_count; i++) {
        opencl_device_t *dev = &g_devices[i];
        if (!dev->active) continue;

        // Release old buffer if exists
        if (dev->d_targets) {
            clReleaseMemObject(dev->d_targets);
            dev->d_targets = NULL;
        }

        // Allocate and upload
        dev->d_targets = clCreateBuffer(dev->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                        targets_bytes, h_targets_copy, &err);
        if (err != CL_SUCCESS) {
            fprintf(stderr, "[OpenCL] Failed to upload targets to device %d: %s (%d)\n",
                    i, clGetErrorString(err), err);
            return -1;
        }
    }

    printf("[OpenCL] Targets uploaded to %d device(s) (%zu hashes, %.2f KB)\n",
           g_device_count, count, (double)targets_bytes / 1024.0);

    return 0;
}

int gpu_upload_bloom(const uint8_t *bloom_data, size_t bloom_size, int num_hashes) {
    if (!g_available) {
        fprintf(stderr, "[OpenCL] Backend not initialized\n");
        return -1;
    }

    if (h_bloom_copy) {
        free(h_bloom_copy);
    }

    h_bloom_copy = (uint8_t*)malloc(bloom_size);
    if (!h_bloom_copy) {
        fprintf(stderr, "[OpenCL] Failed to allocate host bloom copy\n");
        return -1;
    }

    memcpy(h_bloom_copy, bloom_data, bloom_size);
    g_bloom_size = bloom_size;
    g_bloom_hashes = num_hashes;

    // Upload to all active devices
    cl_int err;
    for (int i = 0; i < g_device_count; i++) {
        opencl_device_t *dev = &g_devices[i];
        if (!dev->active) continue;

        // Release old buffer if exists
        if (dev->d_bloom) {
            clReleaseMemObject(dev->d_bloom);
            dev->d_bloom = NULL;
        }

        // Allocate and upload
        dev->d_bloom = clCreateBuffer(dev->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                      bloom_size, h_bloom_copy, &err);
        if (err != CL_SUCCESS) {
            fprintf(stderr, "[OpenCL] Failed to upload bloom filter to device %d: %s (%d)\n",
                    i, clGetErrorString(err), err);
            return -1;
        }
    }

    printf("[OpenCL] Bloom filter uploaded to %d device(s) (%.2f MB, %d hashes)\n",
           g_device_count, (double)bloom_size / (1024.0 * 1024.0), num_hashes);

    return 0;
}

int gpu_full_search(const gpu_search_config_t *config) {
    if (!g_available || !config || g_device_count == 0) {
        return -1;
    }

    // Verify all devices have required data
    for (int d = 0; d < g_device_count; d++) {
        opencl_device_t *dev = &g_devices[d];
        if (!dev->active) continue;

        if (!dev->d_GTable || g_GTable_count == 0) {
            fprintf(stderr, "[OpenCL] Device %d: G table not uploaded\n", d);
            return -1;
        }
        if (!dev->d_targets || g_target_count == 0) {
            fprintf(stderr, "[OpenCL] Device %d: targets not uploaded\n", d);
            return -1;
        }
        if (!dev->kernel_full_search) {
            fprintf(stderr, "[OpenCL] Device %d: full search kernel not available\n", d);
            return -1;
        }
    }

    // Count active devices
    int active_devices = 0;
    for (int d = 0; d < g_device_count; d++) {
        if (g_devices[d].active) {
            active_devices++;
        }
    }

    if (active_devices == 0) {
        fprintf(stderr, "[OpenCL] No active devices\n");
        return -1;
    }

    // Convert start_key and end_key to uint256_d format
    uint256_d start_key, end_key;
    for (int i = 0; i < 8; i++) {
        start_key.d[i] = ((uint32_t)config->start_key[(7-i)*4+3]) |
                         ((uint32_t)config->start_key[(7-i)*4+2] << 8) |
                         ((uint32_t)config->start_key[(7-i)*4+1] << 16) |
                         ((uint32_t)config->start_key[(7-i)*4] << 24);
        end_key.d[i] = ((uint32_t)config->end_key[(7-i)*4+3]) |
                       ((uint32_t)config->end_key[(7-i)*4+2] << 8) |
                       ((uint32_t)config->end_key[(7-i)*4+1] << 16) |
                       ((uint32_t)config->end_key[(7-i)*4] << 24);
    }

    // Allocate found keys buffers on all active devices
    cl_int err;
    for (int d = 0; d < g_device_count; d++) {
        opencl_device_t *dev = &g_devices[d];
        if (!dev->active) continue;

        // Allocate found keys buffer
        if (!dev->d_found_keys) {
            dev->d_found_keys = clCreateBuffer(dev->context, CL_MEM_READ_WRITE,
                                               sizeof(FoundKey) * MAX_FOUND_KEYS,
                                               NULL, &err);
            if (err != CL_SUCCESS) {
                fprintf(stderr, "[OpenCL] Failed to allocate found_keys buffer on device %d: %s\n",
                        d, clGetErrorString(err));
                return -1;
            }
        }

        // Allocate found count buffer
        if (!dev->d_found_count) {
            dev->d_found_count = clCreateBuffer(dev->context, CL_MEM_READ_WRITE,
                                                sizeof(int), NULL, &err);
            if (err != CL_SUCCESS) {
                fprintf(stderr, "[OpenCL] Failed to allocate found_count buffer on device %d: %s\n",
                        d, clGetErrorString(err));
                return -1;
            }
        }

        // Initialize found count to 0
        int zero = 0;
        err = clEnqueueWriteBuffer(dev->queue, dev->d_found_count, CL_TRUE, 0,
                                   sizeof(int), &zero, 0, NULL, NULL);
        if (err != CL_SUCCESS) {
            fprintf(stderr, "[OpenCL] Failed to initialize found_count on device %d: %s\n",
                    d, clGetErrorString(err));
            return -1;
        }
    }

    // Print search info
    printf("[+] OpenCL search: %d device(s)\n", active_devices);
    for (int d = 0; d < g_device_count; d++) {
        opencl_device_t *dev = &g_devices[d];
        if (!dev->active) continue;
        printf("    Device %d: %s (%u CUs, max WG size %zu)\n",
               d, dev->device_name, dev->compute_units, dev->max_work_group_size);
    }
    printf("[+] Range: [start, end)\n");
    fflush(stdout);

    // Main search loop parameters
    uint256_d cursor;
    memcpy(&cursor, &start_key, sizeof(uint256_d));

    uint64_t total_keys = 0;
    int total_found = 0;

    // Calculate total performance weight for load balancing
    double total_performance_weight = 0.0;
    for (int d = 0; d < g_device_count; d++) {
        if (g_devices[d].active) {
            total_performance_weight += g_devices[d].optimal_params.performance_weight;
        }
    }

    // Timing
    uint64_t start_time_ns = platform_time_now_ns();
    uint64_t last_report_time_ns = start_time_ns;

#ifdef __cplusplus
    // C++ mode: use atomic load
    while (!config->should_stop->load(std::memory_order_acquire) && u256_cmp_host(&cursor, &end_key) < 0) {
#else
    // C mode: use volatile read
    while (!(*config->should_stop) && u256_cmp_host(&cursor, &end_key) < 0) {
#endif
        // Launch kernel on all active devices with proportional work distribution
        for (int d = 0; d < g_device_count; d++) {
            opencl_device_t *dev = &g_devices[d];
            if (!dev->active) continue;

            // Use device-specific optimal parameters
            int blocks_per_cu = dev->optimal_params.blocks_per_cu;
            int keys_per_work_item = dev->optimal_params.keys_per_work_item;
            size_t work_group_size = dev->optimal_params.work_group_size;

            // Calculate work sizes based on device capabilities
            size_t blocks = dev->compute_units * blocks_per_cu;
            size_t global_work_size = blocks * work_group_size;
            size_t local_work_size = work_group_size;

            // Adjust for performance weighting (distribute more work to faster devices)
            double device_weight = dev->optimal_params.performance_weight / total_performance_weight;
            uint64_t base_keys_per_launch = global_work_size * keys_per_work_item;
            uint64_t keys_per_launch = (uint64_t)(base_keys_per_launch * device_weight * active_devices);

            // Check remaining keys
            uint64_t remaining64 = u256_sub_sat_u64_host(&end_key, &cursor);
            if (remaining64 == 0) {
                dev->active = 0;
                continue;
            }

            uint64_t keys_this_launch = (remaining64 < keys_per_launch) ? remaining64 : keys_per_launch;

            // Adjust keys_per_thread for last batch
            cl_ulong actual_keys_per_thread = keys_per_thread;
            if (keys_this_launch < keys_per_launch) {
                actual_keys_per_thread = (keys_this_launch + global_work_size - 1) / global_work_size;
                if (actual_keys_per_thread < 1) actual_keys_per_thread = 1;
            }

            // Set kernel arguments (matches kernel_full_search_compressed signature)
            // kernel_full_search_compressed(cursor, start_idx, keys_per_thread, total_keys,
            //                               d_GTable, d_targets, target_count,
            //                               d_bloom, bloom_size, bloom_hashes, use_bloom,
            //                               d_found_keys, d_found_count)
            cl_ulong start_idx = 0;
            cl_ulong total_keys_arg = keys_this_launch;
            cl_ulong target_count_arg = g_target_count;
            cl_ulong bloom_size_arg = g_bloom_size;
            cl_int bloom_hashes_arg = g_bloom_hashes;
            cl_int use_bloom_arg = config->use_bloom ? 1 : 0;

            int arg_idx = 0;
            err  = clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(uint256_d), &cursor);
            err |= clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(cl_ulong), &start_idx);
            err |= clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(cl_ulong), &actual_keys_per_thread);
            err |= clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(cl_ulong), &total_keys_arg);
            err |= clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(cl_mem), &dev->d_GTable);
            err |= clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(cl_mem), &dev->d_targets);
            err |= clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(cl_ulong), &target_count_arg);
            err |= clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(cl_mem), &dev->d_bloom);
            err |= clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(cl_ulong), &bloom_size_arg);
            err |= clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(cl_int), &bloom_hashes_arg);
            err |= clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(cl_int), &use_bloom_arg);
            err |= clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(cl_mem), &dev->d_found_keys);
            err |= clSetKernelArg(dev->kernel_full_search, arg_idx++, sizeof(cl_mem), &dev->d_found_count);

            if (err != CL_SUCCESS) {
                fprintf(stderr, "[OpenCL] Failed to set kernel arguments on device %d: %s (%d)\n",
                        d, clGetErrorString(err), err);
                dev->active = 0;
                continue;
            }

            // Launch kernel
            err = clEnqueueNDRangeKernel(dev->queue, dev->kernel_full_search, 1, NULL,
                                        &global_work_size, &local_work_size,
                                        0, NULL, NULL);
            if (err != CL_SUCCESS) {
                fprintf(stderr, "[OpenCL] Failed to launch kernel on device %d: %s (%d)\n",
                        d, clGetErrorString(err), err);
                dev->active = 0;
                continue;
            }

            // Advance cursor
            u256_add_u64_host(&cursor, keys_this_launch);
            dev->keys_processed += keys_this_launch;
            total_keys += keys_this_launch;
        }

        // Synchronize all devices and check for results
        for (int d = 0; d < g_device_count; d++) {
            opencl_device_t *dev = &g_devices[d];
            if (!dev->active) continue;

            // Wait for kernel completion
            err = clFinish(dev->queue);
            if (err != CL_SUCCESS) {
                fprintf(stderr, "\n[OpenCL ERROR] Device %d kernel error: %s (%d)\n",
                        d, clGetErrorString(err), err);
                dev->active = 0;
                continue;
            }

            // Check for found keys
            int found_count = 0;
            err = clEnqueueReadBuffer(dev->queue, dev->d_found_count, CL_TRUE, 0,
                                     sizeof(int), &found_count, 0, NULL, NULL);
            if (err != CL_SUCCESS) {
                fprintf(stderr, "[OpenCL] Failed to read found_count from device %d: %s\n",
                        d, clGetErrorString(err));
                continue;
            }

            if (found_count > 0) {
                // Read found keys
                FoundKey h_found[MAX_FOUND_KEYS];
                err = clEnqueueReadBuffer(dev->queue, dev->d_found_keys, CL_TRUE, 0,
                                         sizeof(FoundKey) * found_count, h_found,
                                         0, NULL, NULL);
                if (err != CL_SUCCESS) {
                    fprintf(stderr, "[OpenCL] Failed to read found_keys from device %d: %s\n",
                            d, clGetErrorString(err));
                    continue;
                }

                // Process found keys
                for (int i = 0; i < found_count && i < MAX_FOUND_KEYS; i++) {
                    if (h_found[i].valid) {
                        total_found++;

                        // Convert uint256_d to big-endian bytes
                        uint8_t privkey_be[32];
                        for (int w = 0; w < 8; w++) {
                            uint32_t val = h_found[i].privkey.d[7 - w];
                            privkey_be[w*4]     = (val >> 24) & 0xFF;
                            privkey_be[w*4 + 1] = (val >> 16) & 0xFF;
                            privkey_be[w*4 + 2] = (val >> 8) & 0xFF;
                            privkey_be[w*4 + 3] = val & 0xFF;
                        }

                        // Call user callback
                        if (config->callback) {
                            config->callback(privkey_be,
                                           h_found[i].compressed != 0,
                                           config->callback_userdata);
                        }
                    }
                }

                // Reset found count
                int zero = 0;
                err = clEnqueueWriteBuffer(dev->queue, dev->d_found_count, CL_TRUE, 0,
                                          sizeof(int), &zero, 0, NULL, NULL);
                if (err != CL_SUCCESS) {
                    fprintf(stderr, "[OpenCL] Failed to reset found_count on device %d: %s\n",
                            d, clGetErrorString(err));
                }
            }
        }

        // Progress reporting (every ~5 seconds)
        uint64_t current_time_ns = platform_time_now_ns();
        uint64_t elapsed_since_report_ns = current_time_ns - last_report_time_ns;
        if (!config->quiet && elapsed_since_report_ns > 5000000000ULL) {
            double elapsed_sec = elapsed_since_report_ns / 1e9;
            double mkeys_per_sec = (total_keys / 1000000.0) / (elapsed_sec);

            printf("[OpenCL] %.2f MKey/s total (%.2f M keys checked)\n",
                   mkeys_per_sec, total_keys / 1000000.0);

            // Update per-device statistics and show breakdown
            for (int d = 0; d < g_device_count; d++) {
                opencl_device_t *dev = &g_devices[d];
                if (!dev->active) continue;

                double dev_mkeys_per_sec = (dev->keys_processed / 1000000.0) / elapsed_sec;
                // Exponential moving average for throughput
                if (dev->avg_throughput_mkeys == 0.0) {
                    dev->avg_throughput_mkeys = dev_mkeys_per_sec;
                } else {
                    dev->avg_throughput_mkeys = dev->avg_throughput_mkeys * 0.7 + dev_mkeys_per_sec * 0.3;
                }

                printf("  Device %d (%s): %.2f MKey/s (%.2f M keys)\n",
                       d, dev->device_name, dev_mkeys_per_sec,
                       dev->keys_processed / 1000000.0);
            }

            fflush(stdout);
            last_report_time_ns = current_time_ns;
        }
    }

    // Final statistics
    if (!config->quiet) {
        uint64_t final_elapsed_ns = platform_time_now_ns() - start_time_ns;
        double final_elapsed_sec = final_elapsed_ns / 1e9;
        if (final_elapsed_sec > 0) {
            double final_mkeys_per_sec = (total_keys / 1000000.0) / final_elapsed_sec;
            printf("[OpenCL] Search complete: %.2f MKey/s average (%.2f M keys total)\n",
                   final_mkeys_per_sec, total_keys / 1000000.0);

            // Per-device final statistics
            printf("[OpenCL] Per-device breakdown:\n");
            for (int d = 0; d < g_device_count; d++) {
                opencl_device_t *dev = &g_devices[d];
                if (dev->keys_processed > 0) {
                    double dev_final_mkeys_per_sec = (dev->keys_processed / 1000000.0) / final_elapsed_sec;
                    double percentage = (total_keys > 0) ? (dev->keys_processed * 100.0 / total_keys) : 0.0;
                    printf("  Device %d (%s): %.2f MKey/s, %.2f M keys (%.1f%%)\n",
                           d, dev->device_name, dev_final_mkeys_per_sec,
                           dev->keys_processed / 1000000.0, percentage);
                }
            }
        }
    }

    return total_found;
}

size_t gpu_get_optimal_batch_size(void) {
    if (!g_available || g_device_count == 0) {
        return 0;
    }

    // Conservative default for OpenCL
    // Will be refined in auto-tuning subtask 2-6
    return 1024;
}

double gpu_benchmark(size_t duration_ms) {
    (void)duration_ms;
    fprintf(stderr, "[OpenCL] gpu_benchmark not yet implemented\n");
    return 0.0;
}

// ============================================================================
// Auto-tuning (stub - to be implemented in subtask 2-6)
// ============================================================================

int gpu_autotune(size_t duration_ms, gpu_tune_result_t *result) {
    (void)duration_ms;
    if (!result) {
        return -1;
    }

    // Return conservative defaults for now
    result->blocks_per_sm = 16;
    result->keys_per_thread = 1024;
    result->threads_per_block = 256;
    result->measured_mkeys = 0.0;

    fprintf(stderr, "[OpenCL] gpu_autotune not yet implemented (using defaults)\n");
    return 0;
}

void gpu_apply_tune(const gpu_tune_result_t *tune) {
    (void)tune;
    // Stub - tuning parameters will be applied in subtask 2-6
}
