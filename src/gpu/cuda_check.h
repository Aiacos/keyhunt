/*
 * CUDA error checking macros for keyhunt GPU backend.
 *
 * CUDA_CHECK(call)      - For functions returning int: logs error and returns -1
 * CUDA_CHECK_WARN(call) - For cleanup paths: logs warning but continues execution
 */

#ifndef CUDA_CHECK_H
#define CUDA_CHECK_H

#ifdef HAVE_CUDA_BACKEND

#include <cuda_runtime.h>
#include <cstdio>

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

#endif // HAVE_CUDA_BACKEND
#endif // CUDA_CHECK_H
