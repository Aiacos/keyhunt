/*
 * GPU diagnostics module for keyhunt
 * Provides GPU driver version, CUDA version, and compatibility checks
 */

#ifndef GPU_DIAGNOSTICS_H
#define GPU_DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// GPU diagnostic information
typedef struct {
    // Driver and runtime versions
    int driver_version_major;
    int driver_version_minor;
    int cuda_runtime_version_major;
    int cuda_runtime_version_minor;

    // GPU hardware info
    int gpu_count;
    char gpu_name[128];
    uint64_t vram_mb;
    int compute_major;
    int compute_minor;
    int multiprocessors;

    // Compatibility status
    bool cuda_available;
    bool driver_compatible;
    bool compute_capability_ok;  // >= 6.0 required

    // Error message if something failed
    char error_message[256];
} gpu_diag_info_t;

// Get NVIDIA driver version
// Returns 0 on success, non-zero on error
// Fills major and minor with driver version (e.g., 525.125 -> major=525, minor=125)
int gpu_diag_get_driver_version(int *major, int *minor);

// Get CUDA runtime version
// Returns 0 on success, non-zero on error
// Fills major and minor with CUDA version (e.g., 12.0 -> major=12, minor=0)
int gpu_diag_get_cuda_version(int *major, int *minor);

// Run comprehensive GPU diagnostics
// Returns 0 on success (GPU available and compatible)
// Returns non-zero if GPU unavailable or incompatible
// Fills info structure with detailed diagnostic information
int gpu_diag_check_compatibility(gpu_diag_info_t *info);

// Format GPU diagnostic information as human-readable string
// buffer: output buffer for formatted text
// size: size of buffer in bytes
// info: GPU diagnostic information to format
void gpu_diag_format_report(char *buffer, size_t size, const gpu_diag_info_t *info);

// Check minimum CUDA compute capability requirement
// Returns 1 if compute capability >= 6.0, 0 otherwise
bool gpu_diag_check_compute_capability(int major, int minor);

#ifdef __cplusplus
}
#endif

#endif // GPU_DIAGNOSTICS_H
