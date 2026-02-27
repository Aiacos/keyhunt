/*
 * GPU diagnostics implementation for keyhunt
 * Provides GPU driver version, CUDA version, and compatibility checks
 */

#include "gpu_diagnostics.h"
#include <stdio.h>
#include <string.h>

// CUDA headers are only available when building with CUDA support
#ifdef HAVE_CUDA_BACKEND
    #include <cuda_runtime.h>
#endif

// ============================================================================
// CUDA-enabled implementation
// ============================================================================

#ifdef HAVE_CUDA_BACKEND

int gpu_diag_get_driver_version(int *major, int *minor) {
    if (!major || !minor) return -1;

    int driver_version = 0;
    cudaError_t err = cudaDriverGetVersion(&driver_version);

    if (err != cudaSuccess) {
        *major = 0;
        *minor = 0;
        return -1;
    }

    // CUDA version format: major*1000 + minor*10
    // E.g., 12000 = CUDA 12.0, 11080 = CUDA 11.8
    *major = driver_version / 1000;
    *minor = (driver_version % 1000) / 10;

    return 0;
}

int gpu_diag_get_cuda_version(int *major, int *minor) {
    if (!major || !minor) return -1;

    int runtime_version = 0;
    cudaError_t err = cudaRuntimeGetVersion(&runtime_version);

    if (err != cudaSuccess) {
        *major = 0;
        *minor = 0;
        return -1;
    }

    // CUDA version format: major*1000 + minor*10
    *major = runtime_version / 1000;
    *minor = (runtime_version % 1000) / 10;

    return 0;
}

int gpu_diag_check_compatibility(gpu_diag_info_t *info) {
    if (!info) return -1;

    // Initialize structure
    memset(info, 0, sizeof(gpu_diag_info_t));
    info->cuda_available = true;

    // Get driver version
    if (gpu_diag_get_driver_version(&info->driver_version_major,
                                     &info->driver_version_minor) != 0) {
        snprintf(info->error_message, sizeof(info->error_message),
                 "Failed to query NVIDIA driver version");
        info->driver_compatible = false;
        return -1;
    }
    info->driver_compatible = true;

    // Get CUDA runtime version
    if (gpu_diag_get_cuda_version(&info->cuda_runtime_version_major,
                                   &info->cuda_runtime_version_minor) != 0) {
        snprintf(info->error_message, sizeof(info->error_message),
                 "Failed to query CUDA runtime version");
        return -1;
    }

    // Get GPU count
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);

    if (err != cudaSuccess || device_count == 0) {
        snprintf(info->error_message, sizeof(info->error_message),
                 "No CUDA-capable GPU detected");
        info->gpu_count = 0;
        return -1;
    }
    info->gpu_count = device_count;

    // Get properties of first GPU (primary device)
    cudaDeviceProp props;
    err = cudaGetDeviceProperties(&props, 0);

    if (err != cudaSuccess) {
        snprintf(info->error_message, sizeof(info->error_message),
                 "Failed to query GPU properties");
        return -1;
    }

    // Fill GPU hardware information
    strncpy(info->gpu_name, props.name, sizeof(info->gpu_name) - 1);
    info->gpu_name[sizeof(info->gpu_name) - 1] = '\0';
    info->vram_mb = props.totalGlobalMem / (1024 * 1024);
    info->compute_major = props.major;
    info->compute_minor = props.minor;
    info->multiprocessors = props.multiProcessorCount;

    // Check compute capability (minimum 6.0 required)
    info->compute_capability_ok = gpu_diag_check_compute_capability(
        props.major, props.minor
    );

    if (!info->compute_capability_ok) {
        snprintf(info->error_message, sizeof(info->error_message),
                 "GPU compute capability %d.%d is below minimum 6.0",
                 props.major, props.minor);
        return -1;
    }

    return 0;  // Success
}

bool gpu_diag_check_compute_capability(int major, int minor) {
    int compute_capability = major * 10 + minor;
    return compute_capability >= 60;  // Minimum 6.0 required
}

void gpu_diag_format_report(char *buffer, size_t size, const gpu_diag_info_t *info) {
    if (!buffer || size == 0 || !info) return;

    int offset = 0;

    // CUDA availability
    if (!info->cuda_available) {
        offset += snprintf(buffer + offset, size - offset,
                          "CUDA Status: NOT AVAILABLE\n");
        if (info->error_message[0] != '\0') {
            offset += snprintf(buffer + offset, size - offset,
                              "Error: %s\n", info->error_message);
        }
        return;
    }

    // Driver version
    offset += snprintf(buffer + offset, size - offset,
                      "NVIDIA Driver: %d.%d\n",
                      info->driver_version_major, info->driver_version_minor);

    // CUDA runtime version
    offset += snprintf(buffer + offset, size - offset,
                      "CUDA Runtime: %d.%d\n",
                      info->cuda_runtime_version_major,
                      info->cuda_runtime_version_minor);

    // GPU count
    offset += snprintf(buffer + offset, size - offset,
                      "GPU Count: %d\n", info->gpu_count);

    if (info->gpu_count > 0) {
        // GPU hardware details
        offset += snprintf(buffer + offset, size - offset,
                          "GPU Model: %s\n", info->gpu_name);

        offset += snprintf(buffer + offset, size - offset,
                          "VRAM: %lu MB\n", (unsigned long)info->vram_mb);

        offset += snprintf(buffer + offset, size - offset,
                          "Compute Capability: %d.%d\n",
                          info->compute_major, info->compute_minor);

        offset += snprintf(buffer + offset, size - offset,
                          "Multiprocessors: %d\n", info->multiprocessors);

        // Compatibility status
        if (info->compute_capability_ok) {
            offset += snprintf(buffer + offset, size - offset,
                              "Compatibility: OK (meets minimum 6.0 requirement)\n");
        } else {
            offset += snprintf(buffer + offset, size - offset,
                              "Compatibility: INCOMPATIBLE (requires >= 6.0)\n");
        }
    }

    // Error message if present
    if (info->error_message[0] != '\0') {
        offset += snprintf(buffer + offset, size - offset,
                          "Note: %s\n", info->error_message);
    }
}

#else

// ============================================================================
// No-CUDA stub implementation (when HAVE_CUDA_BACKEND is not defined)
// ============================================================================

int gpu_diag_get_driver_version(int *major, int *minor) {
    if (major) *major = 0;
    if (minor) *minor = 0;
    return -1;  // CUDA not available
}

int gpu_diag_get_cuda_version(int *major, int *minor) {
    if (major) *major = 0;
    if (minor) *minor = 0;
    return -1;  // CUDA not available
}

int gpu_diag_check_compatibility(gpu_diag_info_t *info) {
    if (!info) return -1;

    memset(info, 0, sizeof(gpu_diag_info_t));
    info->cuda_available = false;
    snprintf(info->error_message, sizeof(info->error_message),
             "CUDA support not compiled in this build");

    return -1;  // No CUDA support
}

bool gpu_diag_check_compute_capability(int major, int minor) {
    (void)major;  // Unused
    (void)minor;  // Unused
    return false;  // No CUDA support
}

void gpu_diag_format_report(char *buffer, size_t size, const gpu_diag_info_t *info) {
    if (!buffer || size == 0 || !info) return;

    snprintf(buffer, size,
             "CUDA Status: NOT AVAILABLE\n"
             "Note: This binary was compiled without CUDA support.\n"
             "      To enable GPU acceleration, rebuild with CUDA toolkit installed.\n");
}

#endif  // HAVE_CUDA_BACKEND
