#ifndef AGGRESSIVE_OPTS_H
#define AGGRESSIVE_OPTS_H

#include <stdint.h>
#include <immintrin.h>

#ifdef __cplusplus
extern "C" {
#endif

// Aggressive prefetching distances
#define PREFETCH_DISTANCE_NEAR  8    // L1 cache
#define PREFETCH_DISTANCE_MID   16   // L2 cache
#define PREFETCH_DISTANCE_FAR   32   // L3 cache

// Aggressive prefetch for point data
static inline void prefetch_points_aggressive(void *pts, size_t idx, size_t total) {
    // Prefetch multiple cache levels ahead
    if (idx + PREFETCH_DISTANCE_NEAR < total) {
        _mm_prefetch((const char*)((char*)pts + (idx + PREFETCH_DISTANCE_NEAR) * 64),
                     _MM_HINT_T0);  // L1
    }
    if (idx + PREFETCH_DISTANCE_MID < total) {
        _mm_prefetch((const char*)((char*)pts + (idx + PREFETCH_DISTANCE_MID) * 64),
                     _MM_HINT_T1);  // L2
    }
    if (idx + PREFETCH_DISTANCE_FAR < total) {
        _mm_prefetch((const char*)((char*)pts + (idx + PREFETCH_DISTANCE_FAR) * 64),
                     _MM_HINT_T2);  // L3
    }
}

// Prefetch for Int/modular arithmetic data
static inline void prefetch_int_data(void *data, size_t offset) {
    // Prefetch 32-byte Int data
    _mm_prefetch((const char*)((char*)data + offset), _MM_HINT_T0);
    _mm_prefetch((const char*)((char*)data + offset + 32), _MM_HINT_T0);
}

// Aggressive loop unrolling hint
#define UNROLL_4 _Pragma("GCC unroll 4")
#define UNROLL_8 _Pragma("GCC unroll 8")

// Get optimal thread count for current system
int get_optimal_thread_count(void);

// Set thread affinity for better cache locality
void set_thread_affinity(int thread_id);

#ifdef __cplusplus
}
#endif

#endif // AGGRESSIVE_OPTS_H
