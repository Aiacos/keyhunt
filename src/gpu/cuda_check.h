/*
 * CUDA error checking macros for keyhunt GPU backend.
 *
 * CUDA_CHECK(call)       - For functions returning int: logs error and returns -1
 * CUDA_CHECK_WARN(call)  - For cleanup paths: logs warning but continues execution
 * CUDA_CHECK_DIAG(call)  - Enhanced diagnostics with CUDA driver/version info
 */

#ifndef CUDA_CHECK_H
#define CUDA_CHECK_H

#ifdef HAVE_CUDA_BACKEND

#include <cuda_runtime.h>
#include <cstdio>

// ANSI color codes for diagnostic output
#define CUDA_CLR_RESET   "\033[0m"
#define CUDA_CLR_BOLD    "\033[1m"
#define CUDA_CLR_DIM     "\033[2m"
#define CUDA_CLR_RED     "\033[31m"
#define CUDA_CLR_YELLOW  "\033[33m"
#define CUDA_CLR_CYAN    "\033[36m"

// CUDA_CHECK: for functions returning int -- logs error and returns -1
#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = (call); \
        if (err != cudaSuccess) { \
            fprintf(stderr, "[CUDA ERROR] %s:%d: %s returned %s (%d)\n", \
                    __FILE__, __LINE__, #call, cudaGetErrorString(err), (int)err); \
            return -1; \
        } \
    } while (0)

// CUDA_CHECK_WARN: for cleanup paths -- logs warning but continues
#define CUDA_CHECK_WARN(call) \
    do { \
        cudaError_t err = (call); \
        if (err != cudaSuccess) { \
            fprintf(stderr, "[CUDA WARN] %s:%d: %s returned %s (%d)\n", \
                    __FILE__, __LINE__, #call, cudaGetErrorString(err), (int)err); \
        } \
    } while (0)

// Helper function to get CUDA driver version string
static inline void cuda_get_driver_version_str(char *buf, size_t size) {
    int driver_version = 0;
    cudaError_t err = cudaDriverGetVersion(&driver_version);
    if (err == cudaSuccess) {
        int major = driver_version / 1000;
        int minor = (driver_version % 1000) / 10;
        snprintf(buf, size, "%d.%d", major, minor);
    } else {
        snprintf(buf, size, "Unknown");
    }
}

// Helper function to get CUDA runtime version string
static inline void cuda_get_runtime_version_str(char *buf, size_t size) {
    int runtime_version = 0;
    cudaError_t err = cudaRuntimeGetVersion(&runtime_version);
    if (err == cudaSuccess) {
        int major = runtime_version / 1000;
        int minor = (runtime_version % 1000) / 10;
        snprintf(buf, size, "%d.%d", major, minor);
    } else {
        snprintf(buf, size, "Unknown");
    }
}

// Helper function to get device name
static inline void cuda_get_device_name(char *buf, size_t size) {
    int device;
    cudaError_t err = cudaGetDevice(&device);
    if (err == cudaSuccess) {
        cudaDeviceProp prop;
        err = cudaGetDeviceProperties(&prop, device);
        if (err == cudaSuccess) {
            snprintf(buf, size, "%s (Device %d)", prop.name, device);
        } else {
            snprintf(buf, size, "Device %d", device);
        }
    } else {
        snprintf(buf, size, "Unknown");
    }
}

// Helper function to get error resolution hint
static inline const char* cuda_get_error_hint(cudaError_t err) {
    switch (err) {
        case cudaErrorMemoryAllocation:
            return "Out of GPU memory - reduce GPU work size or batch size";
        case cudaErrorInitializationError:
            return "CUDA initialization failed - reinstall NVIDIA driver";
        case cudaErrorInsufficientDriver:
            return "Driver version too old - update NVIDIA driver";
        case cudaErrorNoDevice:
            return "No CUDA-capable device found - check GPU detection";
        case cudaErrorInvalidDevice:
            return "Invalid device ID - verify device enumeration";
        case cudaErrorInvalidValue:
            return "Invalid parameter value - check API call arguments";
        case cudaErrorLaunchFailure:
            return "Kernel launch failed - check kernel code and GPU state";
        case cudaErrorLaunchTimeout:
            return "Kernel timeout - reduce work size or disable TDR";
        case cudaErrorLaunchOutOfResources:
            return "Out of GPU resources - reduce threads/blocks or shared memory";
        case cudaErrorInvalidDevicePointer:
            return "Invalid device pointer - check memory allocation and copying";
        case cudaErrorInvalidMemcpyDirection:
            return "Invalid memcpy direction - verify host/device pointers";
        case cudaErrorUnknown:
            return "Unknown CUDA error - check GPU health and driver state";
        case cudaErrorNotReady:
            return "Operation not ready - try synchronizing before checking result";
        case cudaErrorIllegalAddress:
            return "Illegal memory access - check array bounds in kernel";
        default:
            return "Refer to CUDA documentation for this error code";
    }
}

// CUDA_CHECK_DIAG: Enhanced diagnostics with driver/version info and resolution hints
#define CUDA_CHECK_DIAG(call) \
    do { \
        cudaError_t err = (call); \
        if (err != cudaSuccess) { \
            char driver_ver[32], runtime_ver[32], device_name[256]; \
            cuda_get_driver_version_str(driver_ver, sizeof(driver_ver)); \
            cuda_get_runtime_version_str(runtime_ver, sizeof(runtime_ver)); \
            cuda_get_device_name(device_name, sizeof(device_name)); \
            \
            fprintf(stderr, "\n%s%s⛔ CUDA ERROR%s\n", \
                    CUDA_CLR_RED, CUDA_CLR_BOLD, CUDA_CLR_RESET); \
            fprintf(stderr, "%s%s%s\n\n", \
                    CUDA_CLR_RED, cudaGetErrorString(err), CUDA_CLR_RESET); \
            \
            fprintf(stderr, "%sTechnical Details:%s\n", CUDA_CLR_BOLD, CUDA_CLR_RESET); \
            fprintf(stderr, "  • Operation: %s\n", #call); \
            fprintf(stderr, "  • Error Code: %d (%s)\n", (int)err, cudaGetErrorName(err)); \
            fprintf(stderr, "  • Location: %s:%d\n", __FILE__, __LINE__); \
            fprintf(stderr, "  • Device: %s\n", device_name); \
            fprintf(stderr, "  • CUDA Driver: %s\n", driver_ver); \
            fprintf(stderr, "  • CUDA Runtime: %s\n\n", runtime_ver); \
            \
            fprintf(stderr, "%sResolution:%s\n", CUDA_CLR_BOLD, CUDA_CLR_RESET); \
            fprintf(stderr, "  %s\n", cuda_get_error_hint(err)); \
            fprintf(stderr, "  • Update NVIDIA drivers to latest version\n"); \
            fprintf(stderr, "  • Check GPU status: %snvidia-smi%s\n", \
                    CUDA_CLR_CYAN, CUDA_CLR_RESET); \
            fprintf(stderr, "  • Try CPU-only mode (omit -g flag)\n"); \
            fprintf(stderr, "  • Consult CUDA error reference online\n\n"); \
            \
            fprintf(stderr, "%sHelpful command:%s\n", CUDA_CLR_BOLD, CUDA_CLR_RESET); \
            fprintf(stderr, "  %snvidia-smi%s\n\n", CUDA_CLR_CYAN, CUDA_CLR_RESET); \
            \
            return -1; \
        } \
    } while (0)

#endif // HAVE_CUDA_BACKEND
#endif // CUDA_CHECK_H
