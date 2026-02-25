/*
 * Full CUDA backend for keyhunt
 * Implements: secp256k1 ECC + SHA256 + RIPEMD160 + bloom/matching
 * BitCrack-style all-GPU computation
 *
 * NOTE: Written in C-style to avoid GCC 15 / CUDA 12.4 compatibility issues
 */

#include "gpu_backend.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <cuda_runtime.h>

// Optimized hash functions (fully unrolled, register-only)
#include "gpu_hash_optimized.cuh"

// Parameter validation
#include "../core/parameter_validator.h"

// Use optimized hash functions
#define USE_OPTIMIZED_HASH 1

// ============================================================================
// Global state (C-style, no thread_local)
// ============================================================================

static int g_available = 0;
static gpu_backend_info_t g_info;

// ============================================================================
// Multi-GPU support infrastructure
// ============================================================================

#define MAX_GPUS 8
#define NUM_STREAMS_PER_GPU 2

// Architecture-based optimal parameters
typedef struct {
    int blocks_per_sm;
    int keys_per_thread;
    int threads_per_block;
} arch_params_t;

// Per-GPU context
typedef struct {
    int device_id;
    int active;

    // Device properties
    cudaDeviceProp props;
    int compute_capability;  // e.g., 75 for sm_75, 86 for sm_86

    // Optimal parameters for this GPU architecture
    arch_params_t optimal_params;

    // Device memory (per-GPU)
    uint8_t *d_GTable;
    uint8_t *d_targets;
    uint8_t *d_bloom;

    // Streams
    cudaStream_t streams[NUM_STREAMS_PER_GPU];
    int *d_should_stop[NUM_STREAMS_PER_GPU];

    // Statistics
    volatile uint64_t keys_processed;
} gpu_context_t;

static gpu_context_t g_gpus[MAX_GPUS];
static int g_gpu_count = 0;
static size_t g_GTable_count = 0;
static size_t g_target_count = 0;
static size_t g_bloom_size = 0;
static int g_bloom_hashes = 0;

// Host-side copies for upload to all GPUs
static uint8_t *h_GTable_copy = NULL;
static uint8_t *h_targets_copy = NULL;
static uint8_t *h_bloom_copy = NULL;

// Get optimal parameters based on GPU compute capability
static arch_params_t get_optimal_params(int compute_capability, int multiprocessors) {
    arch_params_t params;

    // Architecture-specific tuning based on compute capability
    // These values are optimized for each architecture's characteristics:
    // - Register file size
    // - Shared memory per SM
    // - L1/L2 cache sizes
    // - Warp scheduler efficiency

    switch (compute_capability) {
        case 75:  // Turing (RTX 2000 series)
            params.blocks_per_sm = 24;
            params.keys_per_thread = 1024;
            params.threads_per_block = 256;
            break;

        case 80:  // Ampere (A100, data center)
            params.blocks_per_sm = 32;
            params.keys_per_thread = 2048;
            params.threads_per_block = 256;
            break;

        case 86:  // Ampere (RTX 3000 series, consumer)
            params.blocks_per_sm = 28;
            params.keys_per_thread = 1536;
            params.threads_per_block = 256;
            break;

        case 87:  // Ampere (Jetson Orin)
            params.blocks_per_sm = 24;
            params.keys_per_thread = 1024;
            params.threads_per_block = 256;
            break;

        case 89:  // Ada Lovelace (RTX 4000 series)
            params.blocks_per_sm = 32;
            params.keys_per_thread = 2048;
            params.threads_per_block = 256;
            break;

        case 90:  // Hopper (H100)
            params.blocks_per_sm = 40;
            params.keys_per_thread = 4096;
            params.threads_per_block = 256;
            break;

        case 70:  // Volta (V100)
        case 72:  // Volta (Xavier)
            params.blocks_per_sm = 20;
            params.keys_per_thread = 1024;
            params.threads_per_block = 256;
            break;

        case 61:  // Pascal (GTX 1000 series)
        case 60:  // Pascal (P100)
            params.blocks_per_sm = 16;
            params.keys_per_thread = 512;
            params.threads_per_block = 256;
            break;

        default:
            // Conservative defaults for unknown architectures
            if (compute_capability >= 89) {
                // Assume similar to Ada for newer
                params.blocks_per_sm = 32;
                params.keys_per_thread = 2048;
            } else if (compute_capability >= 80) {
                // Assume similar to Ampere
                params.blocks_per_sm = 28;
                params.keys_per_thread = 1536;
            } else {
                // Older GPUs: conservative
                params.blocks_per_sm = 16;
                params.keys_per_thread = 512;
            }
            params.threads_per_block = 256;
            break;
    }

    return params;
}

// Fast path for very small target sets: store in constant memory.
#define MAX_SMALL_TARGETS 32
__device__ __constant__ uint8_t d_targets_small[MAX_SMALL_TARGETS * 20];
__device__ __constant__ int d_targets_small_count = 0;

// ============================================================================
// Multi-stream async execution infrastructure (legacy single-GPU compat)
// ============================================================================

// Full search multi-stream context
#define NUM_SEARCH_STREAMS 2
typedef struct {
    cudaStream_t stream;
    cudaEvent_t start_event;
    cudaEvent_t end_event;
    int *d_should_stop;
    int in_use;
} search_stream_t;
static search_stream_t g_search_streams[NUM_SEARCH_STREAMS];
static int g_search_streams_initialized = 0;

// Legacy single stream for compatibility
static cudaStream_t g_stream = NULL;
static uint8_t *d_x32 = NULL;
static uint8_t *d_out02 = NULL;
static uint8_t *d_out03 = NULL;
static size_t g_capacity = 0;

// ============================================================================
// 256-bit integer (device)
// ============================================================================

struct uint256_d {
    uint32_t d[8];
};

struct Point256_d {
    uint256_d x;
    uint256_d y;
    uint256_d z;
};

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
    // Return (a - b) as uint64 if it fits, else UINT64_MAX. If a <= b, return 0.
    if (u256_cmp_host(a, b) <= 0) return 0;

    uint32_t diff[8];
    uint64_t borrow = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t ai = (uint64_t)a->d[i];
        uint64_t bi = (uint64_t)b->d[i];
        uint64_t sub = ai - bi - borrow;
        diff[i] = (uint32_t)sub;
        borrow = (ai < bi + borrow) ? 1ULL : 0ULL;
    }

    // If any upper limbs beyond 64 bits are non-zero, it doesn't fit.
    for (int i = 2; i < 8; i++) {
        if (diff[i] != 0) return UINT64_MAX;
    }
    return ((uint64_t)diff[1] << 32) | (uint64_t)diff[0];
}

// secp256k1 prime p = 2^256 - 2^32 - 977
__device__ __constant__ uint32_t SECP_P[8] = {
    0xFFFFFC2Fu, 0xFFFFFFFEu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
};

// ============================================================================
// 256-bit arithmetic (device)
// ============================================================================

__device__ __forceinline__ void u256_set_zero(uint256_d *r) {
    #pragma unroll
    for (int i = 0; i < 8; i++) r->d[i] = 0;
}

__device__ __forceinline__ void u256_set(uint256_d *r, const uint256_d *a) {
    #pragma unroll
    for (int i = 0; i < 8; i++) r->d[i] = a->d[i];
}

__device__ __forceinline__ int u256_is_zero(const uint256_d *a) {
    uint32_t z = 0;
    #pragma unroll
    for (int i = 0; i < 8; i++) z |= a->d[i];
    return z == 0;
}

__device__ __forceinline__ uint32_t u256_add(uint256_d *r, const uint256_d *a, const uint256_d *b) {
    uint64_t c = 0;
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        c += (uint64_t)a->d[i] + (uint64_t)b->d[i];
        r->d[i] = (uint32_t)c;
        c >>= 32;
    }
    return (uint32_t)c;
}

__device__ __forceinline__ uint32_t u256_sub(uint256_d *r, const uint256_d *a, const uint256_d *b) {
    int64_t c = 0;
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        c = (int64_t)a->d[i] - (int64_t)b->d[i] + c;
        r->d[i] = (uint32_t)c;
        c >>= 32;
    }
    return (uint32_t)(c < 0 ? 1 : 0);
}

__device__ __forceinline__ void mod_add(uint256_d * __restrict__ r,
                                         const uint256_d * __restrict__ a,
                                         const uint256_d * __restrict__ b) {
    uint256_d tmp;
    uint32_t carry = u256_add(&tmp, a, b);

    // OPTIMIZED: Predicated execution to eliminate warp divergence
    // Both reduction and non-reduction paths execute, result selected by mask

    // Compute reduced result (tmp - p) unconditionally
    uint256_d reduced;
    int64_t borrow = 0;
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        borrow = (int64_t)tmp.d[i] - (int64_t)SECP_P[i] + borrow;
        reduced.d[i] = (uint32_t)borrow;
        borrow >>= 32;
    }

    // Determine if we need reduction: carry set OR (tmp >= p AND borrow == 0)
    // borrow < 0 means tmp < p, so we should NOT reduce
    uint32_t need_reduce = carry | (borrow >= 0 ? 1u : 0u);

    // Select result using predication (no branch divergence)
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        r->d[i] = need_reduce ? reduced.d[i] : tmp.d[i];
    }
}

__device__ __forceinline__ void mod_sub(uint256_d * __restrict__ r,
                                         const uint256_d * __restrict__ a,
                                         const uint256_d * __restrict__ b) {
    uint256_d tmp;
    uint32_t borrow = u256_sub(&tmp, a, b);

    // OPTIMIZED: Predicated execution to eliminate warp divergence
    // Compute (tmp + p) unconditionally
    uint256_d wrapped;
    uint64_t carry = 0;
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        carry = (uint64_t)tmp.d[i] + (uint64_t)SECP_P[i] + carry;
        wrapped.d[i] = (uint32_t)carry;
        carry >>= 32;
    }

    // Select result: if borrow occurred, use wrapped; else use tmp
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        r->d[i] = borrow ? wrapped.d[i] : tmp.d[i];
    }
}

// Modular multiplication for secp256k1 - OPTIMIZED
// Uses reduction: 2^256 ≡ 0x1000003D1 (mod p) where p = 2^256 - 2^32 - 977
__device__ __forceinline__ void mod_mul(uint256_d * __restrict__ r,
                                         const uint256_d * __restrict__ a,
                                         const uint256_d * __restrict__ b) {
    uint64_t acc[17] = {0};

    // Full 256x256 multiplication with periodic carry propagation
    for (int i = 0; i < 8; i++) {
        uint64_t ai = a->d[i];
        for (int j = 0; j < 8; j++) {
            acc[i + j] += ai * b->d[j];
        }
        // Propagate carries to prevent overflow
        for (int k = 0; k < i + 9 && k < 16; k++) {
            acc[k + 1] += acc[k] >> 32;
            acc[k] &= 0xFFFFFFFF;
        }
    }

    // Final carry propagation for 512-bit result
    for (int i = 0; i < 16; i++) {
        acc[i + 1] += acc[i] >> 32;
        acc[i] &= 0xFFFFFFFF;
    }

    // secp256k1 reduction: 2^256 ≡ 0x1000003D1 (mod p)
    // For each high word h[i] (i=0..7 representing acc[8+i]):
    //   h[i] * 2^(256 + 32*i) ≡ h[i] * 0x1000003D1 * 2^(32*i) (mod p)
    //   = h[i] * 977 * 2^(32*i) + h[i] * 2^(32*(i+1))
    // So: add h[i]*977 to position i, add h[i] to position i+1

    // Store high words separately before modifying
    uint64_t hi[8];
    for (int i = 0; i < 8; i++) hi[i] = acc[8 + i];

    // Clear high positions
    for (int i = 8; i < 17; i++) acc[i] = 0;

    // Add contributions from high words
    for (int i = 0; i < 8; i++) {
        acc[i] += hi[i] * 977;     // h[i] * 977 to position i
        acc[i + 1] += hi[i];       // h[i] to position i+1
    }

    // Propagate carries
    for (int i = 0; i < 9; i++) {
        acc[i + 1] += acc[i] >> 32;
        acc[i] &= 0xFFFFFFFF;
    }

    // Second reduction if acc[8] is non-zero
    // acc[8] * 2^256 ≡ acc[8] * 0x1000003D1 (mod p)
    while (acc[8]) {
        uint64_t overflow = acc[8];
        acc[8] = 0;
        acc[0] += overflow * 977;
        acc[1] += overflow;

        // Propagate carries
        for (int i = 0; i < 9; i++) {
            acc[i + 1] += acc[i] >> 32;
            acc[i] &= 0xFFFFFFFF;
        }
    }

    // Copy to result
    uint256_d tmp;
    for (int i = 0; i < 8; i++) tmp.d[i] = (uint32_t)acc[i];

    // Final reduction: if tmp >= p, subtract p
    uint256_d p;
    for (int i = 0; i < 8; i++) p.d[i] = SECP_P[i];

    int cmp = 0;
    for (int i = 7; i >= 0; i--) {
        if (tmp.d[i] > p.d[i]) { cmp = 1; break; }
        if (tmp.d[i] < p.d[i]) { cmp = -1; break; }
    }
    if (cmp >= 0) {
        u256_sub(r, &tmp, &p);
    } else {
        u256_set(r, &tmp);
    }
}

__device__ void mod_sqr(uint256_d *r, const uint256_d *a) {
    mod_mul(r, a, a);
}

// OPTIMIZED modular inverse using Fermat's little theorem
// Uses the exact addition chain from libsecp256k1 for p-2 exponent
// p - 2 = 0xFFFFFFFF_FFFFFFFF_FFFFFFFF_FFFFFFFF_FFFFFFFF_FFFFFFFF_FFFFFFFE_FFFFFC2D
//
// Total operations: 255 squarings + 15 multiplications = 270 operations
// vs naive square-and-multiply: 256 squarings + ~200 multiplications = ~456 operations
//
// The key insight is building a^(2^k - 1) values and combining them:
// [1], [2], 3, 6, 9, 11, [22], 44, 88, 176, 220, [223]
// where brackets indicate values used in final assembly
__device__ void mod_inv(uint256_d *r, const uint256_d *a) {
    uint256_d x2, x3, x6, x9, x11, x22, x44, x88, x176, x220, x223, t1;
    int j;

    // Naming convention: xN means a^(2^N - 1)
    // This builds the addition chain for secp256k1 p-2 from libsecp256k1

    // x2 = a^(2^2 - 1) = a^3
    mod_sqr(&x2, a);           // a^2
    mod_mul(&x2, &x2, a);      // a^3

    // x3 = a^(2^3 - 1) = a^7
    mod_sqr(&x3, &x2);         // (a^3)^2 = a^6
    mod_mul(&x3, &x3, a);      // a^7

    // x6 = a^(2^6 - 1) = a^63
    u256_set(&x6, &x3);
    for (j = 0; j < 3; j++) mod_sqr(&x6, &x6);  // a^(7 * 2^3) = a^56
    mod_mul(&x6, &x6, &x3);    // a^56 * a^7 = a^63 = a^(2^6 - 1)

    // x9 = a^(2^9 - 1) = a^511
    u256_set(&x9, &x6);
    for (j = 0; j < 3; j++) mod_sqr(&x9, &x9);  // a^(63 * 2^3) = a^504
    mod_mul(&x9, &x9, &x3);    // a^504 * a^7 = a^511 = a^(2^9 - 1)

    // x11 = a^(2^11 - 1) = a^2047
    u256_set(&x11, &x9);
    for (j = 0; j < 2; j++) mod_sqr(&x11, &x11);  // a^(511 * 2^2) = a^2044
    mod_mul(&x11, &x11, &x2);  // a^2044 * a^3 = a^2047 = a^(2^11 - 1)

    // x22 = a^(2^22 - 1)
    u256_set(&x22, &x11);
    for (j = 0; j < 11; j++) mod_sqr(&x22, &x22);  // a^(2047 * 2^11)
    mod_mul(&x22, &x22, &x11);  // a^(2^22 - 1)

    // x44 = a^(2^44 - 1)
    u256_set(&x44, &x22);
    for (j = 0; j < 22; j++) mod_sqr(&x44, &x44);
    mod_mul(&x44, &x44, &x22);

    // x88 = a^(2^88 - 1)
    u256_set(&x88, &x44);
    for (j = 0; j < 44; j++) mod_sqr(&x88, &x88);
    mod_mul(&x88, &x88, &x44);

    // x176 = a^(2^176 - 1)
    u256_set(&x176, &x88);
    for (j = 0; j < 88; j++) mod_sqr(&x176, &x176);
    mod_mul(&x176, &x176, &x88);

    // x220 = a^(2^220 - 1)
    u256_set(&x220, &x176);
    for (j = 0; j < 44; j++) mod_sqr(&x220, &x220);
    mod_mul(&x220, &x220, &x44);

    // x223 = a^(2^223 - 1)
    u256_set(&x223, &x220);
    for (j = 0; j < 3; j++) mod_sqr(&x223, &x223);
    mod_mul(&x223, &x223, &x3);

    // Final assembly using sliding window
    // p-2 = 2^256 - 2^32 - 979
    // Binary: 223 ones, then specific pattern for low 33 bits

    u256_set(&t1, &x223);
    for (j = 0; j < 23; j++) mod_sqr(&t1, &t1);  // Shift by 23
    mod_mul(&t1, &t1, &x22);   // Add 22 ones

    for (j = 0; j < 5; j++) mod_sqr(&t1, &t1);   // Shift by 5
    mod_mul(&t1, &t1, a);      // Add 1 one

    for (j = 0; j < 3; j++) mod_sqr(&t1, &t1);   // Shift by 3
    mod_mul(&t1, &t1, &x2);    // Add 2 ones (x2 = a^3 = a^(2^2-1))

    for (j = 0; j < 2; j++) mod_sqr(&t1, &t1);   // Shift by 2
    mod_mul(r, &t1, a);        // Add 1 one -> final result
}

// ============================================================================
// Batch Modular Inverse (Montgomery's Trick) - MAJOR OPTIMIZATION
// Computes N inverses with 1 inversion + 3N multiplications instead of N*256 muls
// ============================================================================

// Batch size for Montgomery's trick (batch modular inverse)
// Trade-off: larger = fewer mod_inv calls but more stack usage
// Performance analysis on RTX 2080 SUPER (sm_75):
//   512: 116KB stack -> ~600 Mkeys/s (BEST - fewer mod_inv wins despite stack spill)
//   256: ~58KB stack -> ~520 Mkeys/s
//   128: ~29KB stack -> ~422 Mkeys/s (too many mod_inv)
//   64:  14KB stack  -> ~360 Mkeys/s (way too many mod_inv)
// The mod_inv cost (~500 muls) dominates over memory access latency
#define BATCH_INV_SIZE 512

// Batch inverse using Montgomery's trick
// Input: values[n] - values to invert (modified in place with results)
// Output: values[n] contains the inverses
__device__ void batch_mod_inv(uint256_d *values, int n) {
    if (n <= 0) return;
    if (n == 1) {
        mod_inv(&values[0], &values[0]);
        return;
    }

    // products[i] = values[0] * values[1] * ... * values[i]
    uint256_d products[BATCH_INV_SIZE];
    u256_set(&products[0], &values[0]);

    // Forward pass: compute cumulative products
    for (int i = 1; i < n; i++) {
        mod_mul(&products[i], &products[i-1], &values[i]);
    }

    // Single expensive inversion
    uint256_d inv_all;
    mod_inv(&inv_all, &products[n-1]);

    // Backward pass: extract individual inverses
    for (int i = n - 1; i > 0; i--) {
        // values[i]^(-1) = inv_all * products[i-1]
        uint256_d inv_i;
        mod_mul(&inv_i, &inv_all, &products[i-1]);

        // Update inv_all for next iteration: inv_all = inv_all * values[i]
        uint256_d tmp;
        mod_mul(&tmp, &inv_all, &values[i]);
        u256_set(&inv_all, &tmp);

        u256_set(&values[i], &inv_i);
    }

    // First element
    u256_set(&values[0], &inv_all);
}

// Convert batch of Jacobian points to affine using Montgomery's batch inverse trick
// This computes N inverses with 1 mod_inv + ~3N mod_mul (instead of N * 256 muls)
__device__ void batch_points_to_affine(Point256_d *points, uint256_d *x_affine, int *y_parity, int n) {
    if (n <= 0) return;
    if (n == 1) {
        // Single point: just use individual inverse
        if (u256_is_zero(&points[0].z)) {
            u256_set_zero(&x_affine[0]);
            y_parity[0] = 0;
            return;
        }
        uint256_d z_inv, z_inv2, z_inv3, y_aff;
        mod_inv(&z_inv, &points[0].z);
        mod_sqr(&z_inv2, &z_inv);
        mod_mul(&x_affine[0], &points[0].x, &z_inv2);
        mod_mul(&z_inv3, &z_inv2, &z_inv);
        mod_mul(&y_aff, &points[0].y, &z_inv3);
        y_parity[0] = y_aff.d[0] & 1;
        return;
    }

    // Montgomery's trick for batch inverse
    // Step 1: Compute cumulative products of Z values
    uint256_d z_vals[BATCH_INV_SIZE];
    uint256_d products[BATCH_INV_SIZE];

    // Copy Z values and compute products
    u256_set(&z_vals[0], &points[0].z);
    u256_set(&products[0], &points[0].z);
    for (int i = 1; i < n; i++) {
        u256_set(&z_vals[i], &points[i].z);
        mod_mul(&products[i], &products[i-1], &z_vals[i]);
    }

    // Step 2: Single expensive modular inverse
    uint256_d inv_all;
    mod_inv(&inv_all, &products[n-1]);

    // Step 3: Extract individual inverses using backward pass
    // z_inv[i] = inv_all * products[i-1]
    // Then update inv_all = inv_all * z_vals[i] for next iteration
    for (int i = n - 1; i > 0; i--) {
        uint256_d z_inv, z_inv2, z_inv3, y_aff;

        // z_inv[i] = inv_all * products[i-1]
        mod_mul(&z_inv, &inv_all, &products[i-1]);

        // Update inv_all for next iteration
        uint256_d tmp;
        mod_mul(&tmp, &inv_all, &z_vals[i]);
        u256_set(&inv_all, &tmp);

        // Compute affine coordinates
        mod_sqr(&z_inv2, &z_inv);
        mod_mul(&x_affine[i], &points[i].x, &z_inv2);
        mod_mul(&z_inv3, &z_inv2, &z_inv);
        mod_mul(&y_aff, &points[i].y, &z_inv3);
        y_parity[i] = y_aff.d[0] & 1;
    }

    // First element: inv_all is now z_vals[0]^(-1)
    {
        uint256_d z_inv2, z_inv3, y_aff;
        mod_sqr(&z_inv2, &inv_all);
        mod_mul(&x_affine[0], &points[0].x, &z_inv2);
        mod_mul(&z_inv3, &z_inv2, &inv_all);
        mod_mul(&y_aff, &points[0].y, &z_inv3);
        y_parity[0] = y_aff.d[0] & 1;
    }
}

// Variant that also returns affine Y (needed for uncompressed pubkeys).
__device__ void batch_points_to_affine_xy(Point256_d *points,
                                         uint256_d *x_affine,
                                         uint256_d *y_affine,
                                         int *y_parity,
                                         int n) {
    if (n <= 0) return;
    if (n == 1) {
        if (u256_is_zero(&points[0].z)) {
            u256_set_zero(&x_affine[0]);
            u256_set_zero(&y_affine[0]);
            y_parity[0] = 0;
            return;
        }
        uint256_d z_inv, z_inv2, z_inv3;
        mod_inv(&z_inv, &points[0].z);
        mod_sqr(&z_inv2, &z_inv);
        mod_mul(&x_affine[0], &points[0].x, &z_inv2);
        mod_mul(&z_inv3, &z_inv2, &z_inv);
        mod_mul(&y_affine[0], &points[0].y, &z_inv3);
        y_parity[0] = y_affine[0].d[0] & 1;
        return;
    }

    uint256_d z_vals[BATCH_INV_SIZE];
    uint256_d products[BATCH_INV_SIZE];

    u256_set(&z_vals[0], &points[0].z);
    u256_set(&products[0], &points[0].z);
    for (int i = 1; i < n; i++) {
        u256_set(&z_vals[i], &points[i].z);
        mod_mul(&products[i], &products[i-1], &z_vals[i]);
    }

    uint256_d inv_all;
    mod_inv(&inv_all, &products[n-1]);

    for (int i = n - 1; i > 0; i--) {
        uint256_d z_inv, z_inv2, z_inv3;

        mod_mul(&z_inv, &inv_all, &products[i-1]);

        uint256_d tmp;
        mod_mul(&tmp, &inv_all, &z_vals[i]);
        u256_set(&inv_all, &tmp);

        mod_sqr(&z_inv2, &z_inv);
        mod_mul(&x_affine[i], &points[i].x, &z_inv2);
        mod_mul(&z_inv3, &z_inv2, &z_inv);
        mod_mul(&y_affine[i], &points[i].y, &z_inv3);
        y_parity[i] = y_affine[i].d[0] & 1;
    }

    {
        uint256_d z_inv2, z_inv3;
        mod_sqr(&z_inv2, &inv_all);
        mod_mul(&x_affine[0], &points[0].x, &z_inv2);
        mod_mul(&z_inv3, &z_inv2, &inv_all);
        mod_mul(&y_affine[0], &points[0].y, &z_inv3);
        y_parity[0] = y_affine[0].d[0] & 1;
    }
}

// ============================================================================
// SHA-256 (device)
// ============================================================================

__device__ __constant__ uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

__device__ __constant__ uint32_t SHA256_H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

__device__ __forceinline__ uint32_t rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

__device__ void sha256_33(const uint8_t *msg, uint8_t *hash) {
    uint32_t W[64];
    uint32_t H[8];

    // Initialize hash values
    for (int i = 0; i < 8; i++) H[i] = SHA256_H0[i];

    // Prepare message schedule (33 bytes + padding)
    // Block 1: bytes 0-63
    for (int i = 0; i < 8; i++) {
        W[i] = ((uint32_t)msg[i*4] << 24) | ((uint32_t)msg[i*4+1] << 16) |
               ((uint32_t)msg[i*4+2] << 8) | (uint32_t)msg[i*4+3];
    }
    // byte 32 + padding bit
    W[8] = ((uint32_t)msg[32] << 24) | 0x800000;
    for (int i = 9; i < 15; i++) W[i] = 0;
    W[15] = 33 * 8;  // Length in bits

    // Extend
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(W[i-15], 7) ^ rotr(W[i-15], 18) ^ (W[i-15] >> 3);
        uint32_t s1 = rotr(W[i-2], 17) ^ rotr(W[i-2], 19) ^ (W[i-2] >> 10);
        W[i] = W[i-16] + s0 + W[i-7] + s1;
    }

    // Compress
    uint32_t a = H[0], b = H[1], c = H[2], d = H[3];
    uint32_t e = H[4], f = H[5], g = H[6], h = H[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + SHA256_K[i] + W[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    H[0] += a; H[1] += b; H[2] += c; H[3] += d;
    H[4] += e; H[5] += f; H[6] += g; H[7] += h;

    // Output (big-endian)
    for (int i = 0; i < 8; i++) {
        hash[i*4]   = (H[i] >> 24) & 0xFF;
        hash[i*4+1] = (H[i] >> 16) & 0xFF;
        hash[i*4+2] = (H[i] >> 8) & 0xFF;
        hash[i*4+3] = H[i] & 0xFF;
    }
}

// SHA256 for 65-byte message (uncompressed pubkey: 0x04||X32||Y32).
// This is a 2-block SHA256 with a fixed-length message.
__device__ __forceinline__ void sha256_compress_block(uint32_t *H, const uint8_t *block) {
    uint32_t W[64];

    #pragma unroll
    for (int i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    }
    #pragma unroll
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(W[i-15], 7) ^ rotr(W[i-15], 18) ^ (W[i-15] >> 3);
        uint32_t s1 = rotr(W[i-2], 17) ^ rotr(W[i-2], 19) ^ (W[i-2] >> 10);
        W[i] = W[i-16] + s0 + W[i-7] + s1;
    }

    uint32_t a = H[0], b = H[1], c = H[2], d = H[3];
    uint32_t e = H[4], f = H[5], g = H[6], h = H[7];

    #pragma unroll
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + SHA256_K[i] + W[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    H[0] += a; H[1] += b; H[2] += c; H[3] += d;
    H[4] += e; H[5] += f; H[6] += g; H[7] += h;
}

__device__ void sha256_65(const uint8_t *msg, uint8_t *hash) {
    uint32_t H[8];
    #pragma unroll
    for (int i = 0; i < 8; i++) H[i] = SHA256_H0[i];

    // Block 0: msg[0..63]
    sha256_compress_block(H, msg);

    // Block 1: msg[64] + padding + length (520 bits)
    uint8_t block[64];
    #pragma unroll
    for (int i = 0; i < 64; i++) block[i] = 0;
    block[0] = msg[64];
    block[1] = 0x80;
    // last 8 bytes: big-endian length in bits (65*8 = 520 = 0x208)
    block[62] = 0x02;
    block[63] = 0x08;

    sha256_compress_block(H, block);

    // Output (big-endian)
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        hash[i*4]   = (H[i] >> 24) & 0xFF;
        hash[i*4+1] = (H[i] >> 16) & 0xFF;
        hash[i*4+2] = (H[i] >> 8) & 0xFF;
        hash[i*4+3] = H[i] & 0xFF;
    }
}

// Forward declaration (defined in RIPEMD-160 section below).
__device__ void ripemd160_32(const uint8_t *msg, uint8_t *hash);

__device__ __forceinline__ void hash160_65(const uint8_t *pubkey65, uint8_t *hash160) {
    uint8_t sha_hash[32];
    sha256_65(pubkey65, sha_hash);
    ripemd160_32(sha_hash, hash160);
}

// ============================================================================
// RIPEMD-160 (device)
// ============================================================================

__device__ __constant__ uint32_t RMD_KL[5] = {0x00000000, 0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xa953fd4e};
__device__ __constant__ uint32_t RMD_KR[5] = {0x50a28be6, 0x5c4dd124, 0x6d703ef3, 0x7a6d76e9, 0x00000000};

__device__ __constant__ uint8_t RMD_RL[80] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    7,4,13,1,10,6,15,3,12,0,9,5,2,14,11,8,
    3,10,14,4,9,15,8,1,2,7,0,6,13,11,5,12,
    1,9,11,10,0,8,12,4,13,3,7,15,14,5,6,2,
    4,0,5,9,7,12,2,10,14,1,3,8,11,6,15,13
};

__device__ __constant__ uint8_t RMD_RR[80] = {
    5,14,7,0,9,2,11,4,13,6,15,8,1,10,3,12,
    6,11,3,7,0,13,5,10,14,15,8,12,4,9,1,2,
    15,5,1,3,7,14,6,9,11,8,12,2,10,0,4,13,
    8,6,4,1,3,11,15,0,5,12,2,13,9,7,10,14,
    12,15,10,4,1,5,8,7,6,2,13,14,0,3,9,11
};

__device__ __constant__ uint8_t RMD_SL[80] = {
    11,14,15,12,5,8,7,9,11,13,14,15,6,7,9,8,
    7,6,8,13,11,9,7,15,7,12,15,9,11,7,13,12,
    11,13,6,7,14,9,13,15,14,8,13,6,5,12,7,5,
    11,12,14,15,14,15,9,8,9,14,5,6,8,6,5,12,
    9,15,5,11,6,8,13,12,5,12,13,14,11,8,5,6
};

__device__ __constant__ uint8_t RMD_SR[80] = {
    8,9,9,11,13,15,15,5,7,7,8,11,14,14,12,6,
    9,13,15,7,12,8,9,11,7,7,12,7,6,15,13,11,
    9,7,15,11,8,6,6,14,12,13,5,14,13,13,7,5,
    15,5,8,11,14,14,6,14,6,9,12,9,12,5,15,8,
    8,5,12,9,12,5,14,6,8,13,6,5,15,13,11,11
};

__device__ __forceinline__ uint32_t rotl(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

__device__ void ripemd160_32(const uint8_t *msg, uint8_t *hash) {
    uint32_t W[16];
    uint32_t H[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};

    // Prepare message (32 bytes + padding)
    for (int i = 0; i < 8; i++) {
        W[i] = msg[i*4] | ((uint32_t)msg[i*4+1] << 8) |
               ((uint32_t)msg[i*4+2] << 16) | ((uint32_t)msg[i*4+3] << 24);
    }
    W[8] = 0x80;  // Padding
    for (int i = 9; i < 14; i++) W[i] = 0;
    W[14] = 32 * 8;  // Length in bits (low)
    W[15] = 0;       // Length (high)

    uint32_t al = H[0], bl = H[1], cl = H[2], dl = H[3], el = H[4];
    uint32_t ar = H[0], br = H[1], cr = H[2], dr = H[3], er = H[4];

    for (int j = 0; j < 80; j++) {
        uint32_t fl, fr, tl, tr;
        int round = j / 16;

        switch (round) {
            case 0: fl = bl ^ cl ^ dl; fr = br ^ (cr | ~dr); break;
            case 1: fl = (bl & cl) | (~bl & dl); fr = (br & dr) | (cr & ~dr); break;
            case 2: fl = (bl | ~cl) ^ dl; fr = (br | ~cr) ^ dr; break;
            case 3: fl = (bl & dl) | (cl & ~dl); fr = (br & cr) | (~br & dr); break;
            case 4: fl = bl ^ (cl | ~dl); fr = br ^ cr ^ dr; break;
            default: fl = fr = 0;
        }

        tl = rotl(al + fl + W[RMD_RL[j]] + RMD_KL[round], RMD_SL[j]) + el;
        tr = rotl(ar + fr + W[RMD_RR[j]] + RMD_KR[round], RMD_SR[j]) + er;

        al = el; el = dl; dl = rotl(cl, 10); cl = bl; bl = tl;
        ar = er; er = dr; dr = rotl(cr, 10); cr = br; br = tr;
    }

    uint32_t t = H[1] + cl + dr;
    H[1] = H[2] + dl + er;
    H[2] = H[3] + el + ar;
    H[3] = H[4] + al + br;
    H[4] = H[0] + bl + cr;
    H[0] = t;

    // Output (little-endian)
    for (int i = 0; i < 5; i++) {
        hash[i*4]   = H[i] & 0xFF;
        hash[i*4+1] = (H[i] >> 8) & 0xFF;
        hash[i*4+2] = (H[i] >> 16) & 0xFF;
        hash[i*4+3] = (H[i] >> 24) & 0xFF;
    }
}

// ============================================================================
// Point operations (device) for full GPU mode
// ============================================================================

__device__ void point_set_infinity(Point256_d *p) {
    u256_set_zero(&p->x);
    u256_set_zero(&p->y);
    u256_set_zero(&p->z);
}

__device__ int point_is_infinity(const Point256_d *p) {
    return u256_is_zero(&p->z);
}

__device__ void point_from_affine(Point256_d *p, const uint256_d *x, const uint256_d *y) {
    u256_set(&p->x, x);
    u256_set(&p->y, y);
    u256_set_zero(&p->z);
    p->z.d[0] = 1;
}

// Point doubling in Jacobian coordinates
__device__ void point_double(Point256_d *r, const Point256_d *p) {
    if (point_is_infinity(p) || u256_is_zero(&p->y)) {
        point_set_infinity(r);
        return;
    }

    uint256_d a, b, c, d, e, f, tmp;

    mod_sqr(&a, &p->x);          // a = X^2
    mod_sqr(&b, &p->y);          // b = Y^2
    mod_sqr(&c, &b);             // c = Y^4

    mod_add(&tmp, &p->x, &b);
    mod_sqr(&d, &tmp);
    mod_sub(&d, &d, &a);
    mod_sub(&d, &d, &c);
    mod_add(&d, &d, &d);         // d = 2*((X+b)^2 - a - c)

    mod_add(&e, &a, &a);
    mod_add(&e, &e, &a);         // e = 3*X^2

    mod_sqr(&f, &e);             // f = e^2

    mod_add(&tmp, &d, &d);
    mod_sub(&r->x, &f, &tmp);    // X3 = f - 2*d

    mod_mul(&r->z, &p->y, &p->z);
    mod_add(&r->z, &r->z, &r->z); // Z3 = 2*Y*Z

    mod_sub(&tmp, &d, &r->x);
    mod_mul(&r->y, &e, &tmp);
    mod_add(&c, &c, &c);
    mod_add(&c, &c, &c);
    mod_add(&c, &c, &c);         // 8*c
    mod_sub(&r->y, &r->y, &c);   // Y3 = e*(d-X3) - 8*c
}

// Point addition with Q in affine (Z=1)
__device__ void point_add_affine(Point256_d *r, const Point256_d *p,
                                  const uint256_d *qx, const uint256_d *qy) {
    if (point_is_infinity(p)) {
        point_from_affine(r, qx, qy);
        return;
    }

    uint256_d z1z1, u2, s2, h, hh, i, j, rr, v, tmp;

    mod_sqr(&z1z1, &p->z);        // Z1Z1 = Z1^2
    mod_mul(&u2, qx, &z1z1);      // U2 = X2*Z1Z1

    mod_mul(&tmp, &p->z, &z1z1);
    mod_mul(&s2, qy, &tmp);       // S2 = Y2*Z1*Z1Z1

    mod_sub(&h, &u2, &p->x);      // H = U2 - X1

    if (u256_is_zero(&h)) {
        mod_sub(&tmp, &s2, &p->y);
        if (u256_is_zero(&tmp)) {
            point_double(r, p);
            return;
        } else {
            point_set_infinity(r);
            return;
        }
    }

    mod_sqr(&hh, &h);             // HH = H^2
    mod_add(&i, &hh, &hh);
    mod_add(&i, &i, &i);          // I = 4*HH

    mod_mul(&j, &h, &i);          // J = H*I

    mod_sub(&rr, &s2, &p->y);
    mod_add(&rr, &rr, &rr);       // r = 2*(S2-Y1)

    mod_mul(&v, &p->x, &i);       // V = X1*I

    mod_sqr(&r->x, &rr);
    mod_sub(&r->x, &r->x, &j);
    mod_sub(&r->x, &r->x, &v);
    mod_sub(&r->x, &r->x, &v);    // X3 = r^2 - J - 2*V

    mod_sub(&tmp, &v, &r->x);
    mod_mul(&r->y, &rr, &tmp);
    mod_mul(&tmp, &p->y, &j);
    mod_add(&tmp, &tmp, &tmp);
    mod_sub(&r->y, &r->y, &tmp);  // Y3 = r*(V-X3) - 2*Y1*J

    mod_add(&r->z, &p->z, &h);
    mod_sqr(&r->z, &r->z);
    mod_sub(&r->z, &r->z, &z1z1);
    mod_sub(&r->z, &r->z, &hh);   // Z3 = (Z1+H)^2 - Z1Z1 - HH
}

// Get X coordinate in affine (for hash160) - LEGACY, use point_to_affine instead
__device__ void point_get_x_affine(uint256_d *x, const Point256_d *p) {
    if (point_is_infinity(p)) {
        u256_set_zero(x);
        return;
    }

    uint256_d z_inv, z_inv2;
    mod_inv(&z_inv, &p->z);
    mod_sqr(&z_inv2, &z_inv);
    mod_mul(x, &p->x, &z_inv2);
}

// Get Y parity (0 = even, 1 = odd) for compressed prefix - LEGACY
__device__ int point_get_y_parity(const Point256_d *p) {
    if (point_is_infinity(p)) return 0;

    uint256_d z_inv, z_inv2, z_inv3, y_affine;
    mod_inv(&z_inv, &p->z);
    mod_sqr(&z_inv2, &z_inv);
    mod_mul(&z_inv3, &z_inv2, &z_inv);
    mod_mul(&y_affine, &p->y, &z_inv3);

    return y_affine.d[0] & 1;
}

// OPTIMIZED: Get both X affine and Y parity with SINGLE mod_inv (50% faster!)
__device__ void point_to_affine_with_parity(uint256_d *x_affine, int *y_parity, const Point256_d *p) {
    if (point_is_infinity(p)) {
        u256_set_zero(x_affine);
        *y_parity = 0;
        return;
    }

    uint256_d z_inv, z_inv2, z_inv3, y_affine;

    // Single mod_inv call (the expensive operation)
    mod_inv(&z_inv, &p->z);

    // z_inv^2 for X
    mod_sqr(&z_inv2, &z_inv);
    mod_mul(x_affine, &p->x, &z_inv2);

    // z_inv^3 for Y
    mod_mul(&z_inv3, &z_inv2, &z_inv);
    mod_mul(&y_affine, &p->y, &z_inv3);

    *y_parity = y_affine.d[0] & 1;
}

// ============================================================================
// G Table access (uploaded from CPU)
// ============================================================================

// G table format: 256 * 32 affine points, each point = 64 bytes (X || Y)
// Entry [byte_pos * 256 + byte_val] = (byte_val+1) * G * 2^(8*byte_pos)
// OPTIMIZED: Uses __ldg() for cached read-only global memory access
__device__ void gtable_get_point(uint256_d *x, uint256_d *y,
                                  const uint8_t *gtable, int byte_pos, uint8_t byte_val) {
    if (byte_val == 0) {
        u256_set_zero(x);
        u256_set_zero(y);
        return;
    }

    const uint8_t * __restrict__ entry = gtable + ((size_t)byte_pos * 256 + (byte_val - 1)) * 64;

    // Use vectorized 128-bit loads with __ldg() for L1 texture cache
    const uint4 * __restrict__ entry128 = (const uint4 *)entry;

    // Load X coordinate using __ldg() for cached read-only access
    uint4 x0 = __ldg(&entry128[0]);  // bytes 0-15
    uint4 x1 = __ldg(&entry128[1]);  // bytes 16-31

    // Load Y coordinate using __ldg() for cached read-only access
    uint4 y0 = __ldg(&entry128[2]);  // bytes 32-47
    uint4 y1 = __ldg(&entry128[3]);  // bytes 48-63

    // Convert from big-endian (network order) to little-endian uint32_t
    // Use __byte_perm for faster byte swapping on CUDA
    #define BSWAP32(v) __byte_perm((v), 0, 0x0123)

    x->d[7] = BSWAP32(x0.x); x->d[6] = BSWAP32(x0.y);
    x->d[5] = BSWAP32(x0.z); x->d[4] = BSWAP32(x0.w);
    x->d[3] = BSWAP32(x1.x); x->d[2] = BSWAP32(x1.y);
    x->d[1] = BSWAP32(x1.z); x->d[0] = BSWAP32(x1.w);

    y->d[7] = BSWAP32(y0.x); y->d[6] = BSWAP32(y0.y);
    y->d[5] = BSWAP32(y0.z); y->d[4] = BSWAP32(y0.w);
    y->d[3] = BSWAP32(y1.x); y->d[2] = BSWAP32(y1.y);
    y->d[1] = BSWAP32(y1.z); y->d[0] = BSWAP32(y1.w);

    #undef BSWAP32
}

// Scalar multiplication using G table - OPTIMIZED with partial unrolling
__device__ __forceinline__ void scalar_mul_G(Point256_d *r, const uint256_d * __restrict__ k,
                                              const uint8_t * __restrict__ gtable) {
    point_set_infinity(r);

    // Unrolled by 4 (8 iterations of 4 bytes = 32 bytes)
    // Each word contains 4 bytes to process
    #pragma unroll 8
    for (int word = 0; word < 8; word++) {
        uint32_t w = k->d[word];

        // Process 4 bytes per word
        uint8_t b0 = (uint8_t)(w);
        uint8_t b1 = (uint8_t)(w >> 8);
        uint8_t b2 = (uint8_t)(w >> 16);
        uint8_t b3 = (uint8_t)(w >> 24);

        int base_pos = word * 4;

        // Process byte 0
        if (b0 > 0) {
            uint256_d gx, gy;
            gtable_get_point(&gx, &gy, gtable, base_pos, b0);
            point_add_affine(r, r, &gx, &gy);
        }

        // Process byte 1
        if (b1 > 0) {
            uint256_d gx, gy;
            gtable_get_point(&gx, &gy, gtable, base_pos + 1, b1);
            point_add_affine(r, r, &gx, &gy);
        }

        // Process byte 2
        if (b2 > 0) {
            uint256_d gx, gy;
            gtable_get_point(&gx, &gy, gtable, base_pos + 2, b2);
            point_add_affine(r, r, &gx, &gy);
        }

        // Process byte 3
        if (b3 > 0) {
            uint256_d gx, gy;
            gtable_get_point(&gx, &gy, gtable, base_pos + 3, b3);
            point_add_affine(r, r, &gx, &gy);
        }
    }
}

// ============================================================================
// Bloom filter check (device) - OPTIMIZED with 64-bit operations
// ============================================================================

__device__ __forceinline__ int bloom_check(const uint8_t *bloom, size_t bloom_size,
                                            int num_hashes, const uint8_t *hash20) {
    // Cast bloom to uint64_t for faster word-sized checks
    const uint64_t *bloom64 = (const uint64_t *)bloom;
    size_t bloom_bits = bloom_size * 8;
    size_t bloom_words = bloom_size / 8;
    size_t bloom_mask = bloom_bits - 1;
    int bloom_is_pow2 = (bloom_bits != 0) && ((bloom_bits & bloom_mask) == 0);

    // Pre-load hash bytes as 32-bit words (unrolled)
    uint32_t h0 = ((uint32_t)hash20[0] << 8) | hash20[1];
    uint32_t h1 = ((uint32_t)hash20[2] << 8) | hash20[3];
    uint32_t h2 = ((uint32_t)hash20[4] << 8) | hash20[5];
    uint32_t h3 = ((uint32_t)hash20[6] << 8) | hash20[7];

    // Golden ratio constant for mixing
    const uint32_t GOLDEN = 0x9E3779B9u;

    // Check 4 hash functions (most common case)
    // Unrolled for performance
    size_t idx0 = (size_t)((uint64_t)h0 * (uint64_t)GOLDEN);
    size_t idx1 = (size_t)((uint64_t)h1 * (uint64_t)GOLDEN);
    size_t idx2 = (size_t)((uint64_t)h2 * (uint64_t)GOLDEN);
    size_t idx3 = (size_t)((uint64_t)h3 * (uint64_t)GOLDEN);

    if (bloom_is_pow2) {
        idx0 &= bloom_mask;
        idx1 &= bloom_mask;
        idx2 &= bloom_mask;
        idx3 &= bloom_mask;
    } else {
        idx0 %= bloom_bits;
        idx1 %= bloom_bits;
        idx2 %= bloom_bits;
        idx3 %= bloom_bits;
    }

    // Use 64-bit word access when possible
    if (bloom_words > 0) {
        // Check using 64-bit accesses
        size_t word0 = idx0 / 64;
        size_t word1 = idx1 / 64;
        size_t word2 = idx2 / 64;
        size_t word3 = idx3 / 64;

        int bit0 = (int)(idx0 % 64);
        int bit1 = (int)(idx1 % 64);
        int bit2 = (int)(idx2 % 64);
        int bit3 = (int)(idx3 % 64);

        // Early exit on first miss
        if (!(bloom64[word0] & (1ULL << bit0))) return 0;
        if (!(bloom64[word1] & (1ULL << bit1))) return 0;
        if (!(bloom64[word2] & (1ULL << bit2))) return 0;
        if (!(bloom64[word3] & (1ULL << bit3))) return 0;
    } else {
        // Fallback for small bloom filters
        if (!(bloom[idx0 / 8] & (1 << (idx0 % 8)))) return 0;
        if (!(bloom[idx1 / 8] & (1 << (idx1 % 8)))) return 0;
        if (!(bloom[idx2 / 8] & (1 << (idx2 % 8)))) return 0;
        if (!(bloom[idx3 / 8] & (1 << (idx3 % 8)))) return 0;
    }

    // Additional hash functions if requested (rare to need >4)
    if (num_hashes > 4) {
        for (int h = 4; h < num_hashes && h < 10; h++) {
            int byte1 = h * 2;
            int byte2 = h * 2 + 1;
            if (byte2 >= 20) byte2 = h % 20;

            uint32_t idx = ((uint32_t)hash20[byte1] << 8) | hash20[byte2];
            if (bloom_is_pow2) {
                idx = (idx * GOLDEN) & (uint32_t)bloom_mask;
            } else {
                idx = (idx * GOLDEN) % (uint32_t)bloom_bits;
            }

            size_t byte_idx = idx / 8;
            int bit_idx = idx % 8;

            if (!(bloom[byte_idx] & (1 << bit_idx))) {
                return 0;
            }
        }
    }

    return 1;  // Might be in set
}

// ============================================================================
// Direct target search (device) - OPTIMIZED with BINARY SEARCH O(log N)
// Assumes targets are sorted in ascending order (big-endian comparison)
// ============================================================================

// Compare two 20-byte hashes (returns -1, 0, +1)
__device__ __forceinline__ int hash20_compare(const uint8_t * __restrict__ a,
                                               const uint8_t * __restrict__ b) {
    // Compare as two 64-bit values + one 32-bit value for speed
    uint64_t a0, b0, a1, b1;
    uint32_t a2, b2;

    // Load bytes 0-7 as uint64 (big-endian order)
    a0 = ((uint64_t)a[0] << 56) | ((uint64_t)a[1] << 48) | ((uint64_t)a[2] << 40) |
         ((uint64_t)a[3] << 32) | ((uint64_t)a[4] << 24) | ((uint64_t)a[5] << 16) |
         ((uint64_t)a[6] << 8) | (uint64_t)a[7];
    b0 = ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) |
         ((uint64_t)b[3] << 32) | ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
         ((uint64_t)b[6] << 8) | (uint64_t)b[7];

    if (a0 < b0) return -1;
    if (a0 > b0) return 1;

    // Load bytes 8-15 as uint64
    a1 = ((uint64_t)a[8] << 56) | ((uint64_t)a[9] << 48) | ((uint64_t)a[10] << 40) |
         ((uint64_t)a[11] << 32) | ((uint64_t)a[12] << 24) | ((uint64_t)a[13] << 16) |
         ((uint64_t)a[14] << 8) | (uint64_t)a[15];
    b1 = ((uint64_t)b[8] << 56) | ((uint64_t)b[9] << 48) | ((uint64_t)b[10] << 40) |
         ((uint64_t)b[11] << 32) | ((uint64_t)b[12] << 24) | ((uint64_t)b[13] << 16) |
         ((uint64_t)b[14] << 8) | (uint64_t)b[15];

    if (a1 < b1) return -1;
    if (a1 > b1) return 1;

    // Load bytes 16-19 as uint32
    a2 = ((uint32_t)a[16] << 24) | ((uint32_t)a[17] << 16) |
         ((uint32_t)a[18] << 8) | (uint32_t)a[19];
    b2 = ((uint32_t)b[16] << 24) | ((uint32_t)b[17] << 16) |
         ((uint32_t)b[18] << 8) | (uint32_t)b[19];

    if (a2 < b2) return -1;
    if (a2 > b2) return 1;

    return 0;  // Equal
}

__device__ int target_search(const uint8_t * __restrict__ targets, size_t target_count,
                             const uint8_t * __restrict__ hash20) {
    if (target_count == 0) return -1;

    // Binary search O(log N) instead of linear O(N)
    size_t left = 0;
    size_t right = target_count;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const uint8_t *target = targets + mid * 20;

        int cmp = hash20_compare(hash20, target);

        if (cmp == 0) {
            return (int)mid;  // Found!
        } else if (cmp < 0) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }

    return -1;  // Not found
}

// Linear search fallback for small target counts (< 32)
// Linear is faster for very small N due to no branch mispredictions
__device__ int target_search_linear(const uint8_t * __restrict__ targets, size_t target_count,
                                     const uint8_t * __restrict__ hash20) {
    // Fast 64-bit pre-check: compare first 8 bytes at once
    uint64_t hash_prefix =
        ((uint64_t)hash20[0] << 56) | ((uint64_t)hash20[1] << 48) |
        ((uint64_t)hash20[2] << 40) | ((uint64_t)hash20[3] << 32) |
        ((uint64_t)hash20[4] << 24) | ((uint64_t)hash20[5] << 16) |
        ((uint64_t)hash20[6] << 8) | (uint64_t)hash20[7];

    for (size_t i = 0; i < target_count; i++) {
        const uint8_t *target = targets + i * 20;

        // Fast reject: compare first 8 bytes as uint64
        uint64_t target_prefix =
            ((uint64_t)target[0] << 56) | ((uint64_t)target[1] << 48) |
            ((uint64_t)target[2] << 40) | ((uint64_t)target[3] << 32) |
            ((uint64_t)target[4] << 24) | ((uint64_t)target[5] << 16) |
            ((uint64_t)target[6] << 8) | (uint64_t)target[7];
        if (hash_prefix != target_prefix) continue;

        // Full comparison only if prefix matches (rare)
        int match = 1;
        #pragma unroll
        for (int j = 8; j < 20 && match; j++) {
            if (hash20[j] != target[j]) match = 0;
        }
        if (match) return (int)i;
    }
    return -1;
}

// Smart search: choose algorithm based on target count
__device__ __forceinline__ int target_search_smart(const uint8_t * __restrict__ targets,
                                                    size_t target_count,
                                                    const uint8_t * __restrict__ hash20) {
    // For small N, linear is faster (no branch overhead)
    // For large N, binary search wins O(log N) vs O(N)
    if (target_count <= MAX_SMALL_TARGETS) {
        // If the host provided a constant-memory copy, use it.
        if ((size_t)d_targets_small_count == target_count && target_count > 0) {
            return target_search_linear(d_targets_small, target_count, hash20);
        }
        return target_search_linear(targets, target_count, hash20);
    }
    return target_search(targets, target_count, hash20);
}

// ============================================================================
// Found key result buffer
// ============================================================================

struct FoundKey {
    uint256_d privkey;
    // 0 = uncompressed match, 2/3 = compressed prefix byte (02/03)
    int compressed;
    int valid;
};

#define MAX_FOUND_KEYS 256
__device__ FoundKey d_found_keys[MAX_FOUND_KEYS];
__device__ int d_found_count = 0;

__device__ void report_found_key(const uint256_d *privkey, int compressed) {
    int idx = atomicAdd(&d_found_count, 1);
    if (idx < MAX_FOUND_KEYS) {
        u256_set(&d_found_keys[idx].privkey, privkey);
        d_found_keys[idx].compressed = compressed;
        d_found_keys[idx].valid = 1;
    }
}

// ============================================================================
// Full search kernel - OPTIMIZED with batch modular inverse
// Uses Montgomery's trick: 1 mod_inv for BATCH_INV_SIZE keys instead of 1 each
// Speedup: ~16x on mod_inv portion (with BATCH_INV_SIZE=16)
// ============================================================================

// G point (generator) - hardcoded for secp256k1
// Gx = 79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
// Gy = 483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8
__device__ __constant__ uint32_t SECP_GX[8] = {
    0x16F81798, 0x59F2815B, 0x2DCE28D9, 0x029BFCDB,
    0xCE870B07, 0x55A06295, 0xF9DCBBAC, 0x79BE667E
};
__device__ __constant__ uint32_t SECP_GY[8] = {
    0xFB10D4B8, 0x9C47D08F, 0xA6855419, 0xFD17B448,
    0x0E1108A8, 0x5DA4FBFC, 0x26A3C465, 0x483ADA77
};

__global__ void kernel_full_search_compressed(
    const uint256_d start_key,
    uint64_t key_offset,
    uint64_t keys_per_thread,
    uint64_t keys_limit,
    const uint8_t * __restrict__ gtable,
    const uint8_t * __restrict__ targets, size_t target_count,
    const uint8_t * __restrict__ bloom, size_t bloom_size, int bloom_hashes,
    int use_bloom,
    volatile int *should_stop
) {
    uint64_t thread_id = (uint64_t)blockIdx.x * blockDim.x + threadIdx.x;
    uint64_t base_offset = key_offset + thread_id * keys_per_thread;

    // Clamp per-thread work so the final launch never runs past the requested range.
    if (base_offset >= keys_limit) {
        return;
    }
    uint64_t keys_remaining = keys_per_thread;
    uint64_t max_keys_for_thread = keys_limit - base_offset;
    if (keys_remaining > max_keys_for_thread) {
        keys_remaining = max_keys_for_thread;
    }

    // Compute starting key for this thread
    uint256_d current_key;
    u256_set(&current_key, &start_key);

    // Add base_offset to current_key (optimized: unrolled)
    uint64_t carry = base_offset;
    #pragma unroll
    for (int i = 0; i < 8 && carry; i++) {
        uint64_t sum = (uint64_t)current_key.d[i] + (carry & 0xFFFFFFFF);
        current_key.d[i] = (uint32_t)sum;
        carry = (sum >> 32) + (carry >> 32);
    }

    // Batch processing arrays (in registers/local memory)
    Point256_d batch_points[BATCH_INV_SIZE];
    uint256_d batch_keys[BATCH_INV_SIZE];
    uint256_d batch_x_affine[BATCH_INV_SIZE];
    int batch_y_parity[BATCH_INV_SIZE];

    // Preload generator point for fast increments (constant across the loop).
    uint256_d Gx, Gy;
    gtable_get_point(&Gx, &Gy, gtable, 0, 1);

    while (keys_remaining > 0 && !(*should_stop)) {
        // Determine batch size (up to BATCH_INV_SIZE)
        int batch_size = (keys_remaining >= BATCH_INV_SIZE) ? BATCH_INV_SIZE : (int)keys_remaining;

        // Phase 1: Generate batch_size Jacobian points
        // OPTIMIZED: Compute first point via scalar_mul_G, then increment by G using point_add_affine

        // First point via scalar_mul_G
        scalar_mul_G(&batch_points[0], &current_key, gtable);
        u256_set(&batch_keys[0], &current_key);

        // Increment key for remaining points
        uint64_t c = 1;
        for (int j = 0; j < 8 && c; j++) {
            uint64_t sum = (uint64_t)current_key.d[j] + c;
            current_key.d[j] = (uint32_t)sum;
            c = sum >> 32;
        }

        // Generate remaining points by adding G (MUCH faster than scalar_mul_G)
        for (int b = 1; b < batch_size; b++) {
            // Increment previous point by G
            point_add_affine(&batch_points[b], &batch_points[b-1], &Gx, &Gy);
            u256_set(&batch_keys[b], &current_key);

            // Increment key for next iteration
            c = 1;
            for (int j = 0; j < 8 && c; j++) {
                uint64_t sum = (uint64_t)current_key.d[j] + c;
                current_key.d[j] = (uint32_t)sum;
                c = sum >> 32;
            }
        }

        // Phase 2: Batch convert to affine (SINGLE mod_inv for all batch_size keys!)
        batch_points_to_affine(batch_points, batch_x_affine, batch_y_parity, batch_size);

        // Phase 3: Hash and check all keys in batch (OPTIMIZED: unrolled, minimal branches)
        #pragma unroll 4
        for (int b = 0; b < batch_size; b++) {
            // Convert X to big-endian bytes (optimized: unrolled with direct byte extraction)
            uint8_t pubkey[33];
            #pragma unroll
            for (int w = 0; w < 8; w++) {
                uint32_t val = batch_x_affine[b].d[7 - w];
                pubkey[w*4 + 1] = (val >> 24);
                pubkey[w*4 + 2] = (val >> 16);
                pubkey[w*4 + 3] = (val >> 8);
                pubkey[w*4 + 4] = val;
            }

            uint8_t hash160[20];

            // Compressed pubkey: prefix (02/03) + X
            pubkey[0] = 0x02 + batch_y_parity[b];  // 0x02 if even, 0x03 if odd

#if USE_OPTIMIZED_HASH
            // Use fully unrolled, register-only hash functions
            hash160_33_optimized(pubkey, hash160);
#else
            uint8_t sha_hash[32];
            sha256_33(pubkey, sha_hash);
            ripemd160_32(sha_hash, hash160);
#endif

            // Fast bloom pre-check before expensive target search
            int found = -1;
            if (!use_bloom || !bloom || bloom_size == 0 ||
                bloom_check(bloom, bloom_size, bloom_hashes, hash160)) {
                found = target_search_smart(targets, target_count, hash160);
            }
            if (found >= 0) {
                report_found_key(&batch_keys[b], pubkey[0]);
            }
        }

        keys_remaining -= batch_size;
    }
}

__global__ void kernel_full_search_xy(
    const uint256_d start_key,
    uint64_t key_offset,
    uint64_t keys_per_thread,
    uint64_t keys_limit,
    int search_compressed,
    int search_uncompressed,
    const uint8_t * __restrict__ gtable,
    const uint8_t * __restrict__ targets, size_t target_count,
    const uint8_t * __restrict__ bloom, size_t bloom_size, int bloom_hashes,
    int use_bloom,
    volatile int *should_stop
) {
    uint64_t thread_id = (uint64_t)blockIdx.x * blockDim.x + threadIdx.x;
    uint64_t base_offset = key_offset + thread_id * keys_per_thread;

    if (base_offset >= keys_limit) {
        return;
    }
    uint64_t keys_remaining = keys_per_thread;
    uint64_t max_keys_for_thread = keys_limit - base_offset;
    if (keys_remaining > max_keys_for_thread) {
        keys_remaining = max_keys_for_thread;
    }

    uint256_d current_key;
    u256_set(&current_key, &start_key);

    uint64_t carry = base_offset;
    #pragma unroll
    for (int i = 0; i < 8 && carry; i++) {
        uint64_t sum = (uint64_t)current_key.d[i] + (carry & 0xFFFFFFFF);
        current_key.d[i] = (uint32_t)sum;
        carry = (sum >> 32) + (carry >> 32);
    }

    Point256_d batch_points[BATCH_INV_SIZE];
    uint256_d batch_keys[BATCH_INV_SIZE];
    uint256_d batch_x_affine[BATCH_INV_SIZE];
    uint256_d batch_y_affine[BATCH_INV_SIZE];
    int batch_y_parity[BATCH_INV_SIZE];

    uint256_d Gx, Gy;
    gtable_get_point(&Gx, &Gy, gtable, 0, 1);

    while (keys_remaining > 0 && !(*should_stop)) {
        int batch_size = (keys_remaining >= BATCH_INV_SIZE) ? BATCH_INV_SIZE : (int)keys_remaining;

        scalar_mul_G(&batch_points[0], &current_key, gtable);
        u256_set(&batch_keys[0], &current_key);

        uint64_t c = 1;
        for (int j = 0; j < 8 && c; j++) {
            uint64_t sum = (uint64_t)current_key.d[j] + c;
            current_key.d[j] = (uint32_t)sum;
            c = sum >> 32;
        }

        for (int b = 1; b < batch_size; b++) {
            point_add_affine(&batch_points[b], &batch_points[b-1], &Gx, &Gy);
            u256_set(&batch_keys[b], &current_key);

            c = 1;
            for (int j = 0; j < 8 && c; j++) {
                uint64_t sum = (uint64_t)current_key.d[j] + c;
                current_key.d[j] = (uint32_t)sum;
                c = sum >> 32;
            }
        }

        batch_points_to_affine_xy(batch_points, batch_x_affine, batch_y_affine, batch_y_parity, batch_size);

        #pragma unroll 2
        for (int b = 0; b < batch_size; b++) {
            uint8_t hash160[20];

            if (search_compressed) {
                uint8_t pubkey33[33];
                #pragma unroll
                for (int w = 0; w < 8; w++) {
                    uint32_t val = batch_x_affine[b].d[7 - w];
                    pubkey33[w*4 + 1] = (val >> 24);
                    pubkey33[w*4 + 2] = (val >> 16);
                    pubkey33[w*4 + 3] = (val >> 8);
                    pubkey33[w*4 + 4] = val;
                }
                pubkey33[0] = 0x02 + batch_y_parity[b];

#if USE_OPTIMIZED_HASH
                hash160_33_optimized(pubkey33, hash160);
#else
                uint8_t sha_hash[32];
                sha256_33(pubkey33, sha_hash);
                ripemd160_32(sha_hash, hash160);
#endif

                int found = -1;
                if (!use_bloom || !bloom || bloom_size == 0 ||
                    bloom_check(bloom, bloom_size, bloom_hashes, hash160)) {
                    found = target_search_smart(targets, target_count, hash160);
                }
                if (found >= 0) {
                    report_found_key(&batch_keys[b], pubkey33[0]);
                }
            }

            if (search_uncompressed) {
                uint8_t pubkey65[65];
                pubkey65[0] = 0x04;

                #pragma unroll
                for (int w = 0; w < 8; w++) {
                    uint32_t vx = batch_x_affine[b].d[7 - w];
                    pubkey65[w*4 + 1]  = (vx >> 24);
                    pubkey65[w*4 + 2]  = (vx >> 16);
                    pubkey65[w*4 + 3]  = (vx >> 8);
                    pubkey65[w*4 + 4]  = vx;

                    uint32_t vy = batch_y_affine[b].d[7 - w];
                    pubkey65[33 + w*4]     = (vy >> 24);
                    pubkey65[33 + w*4 + 1] = (vy >> 16);
                    pubkey65[33 + w*4 + 2] = (vy >> 8);
                    pubkey65[33 + w*4 + 3] = vy;
                }

                hash160_65(pubkey65, hash160);

                int found = -1;
                if (!use_bloom || !bloom || bloom_size == 0 ||
                    bloom_check(bloom, bloom_size, bloom_hashes, hash160)) {
                    found = target_search_smart(targets, target_count, hash160);
                }
                if (found >= 0) {
                    report_found_key(&batch_keys[b], 0);
                }
            }
        }

        keys_remaining -= batch_size;
    }
}

// ============================================================================
// Hash160 from X coordinate kernel
// ============================================================================

__global__ void kernel_hash160_fromX(const uint8_t *x32_be, size_t count,
                                      uint8_t *out02, uint8_t *out03) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    const uint8_t *x = x32_be + idx * 32;
    uint8_t pubkey[33];

    // Compressed pubkey with prefix 02
    pubkey[0] = 0x02;
    #pragma unroll
    for (int i = 0; i < 32; i++) pubkey[i + 1] = x[i];

#if USE_OPTIMIZED_HASH
    hash160_33_optimized(pubkey, out02 + idx * 20);
#else
    uint8_t sha_hash[32];
    sha256_33(pubkey, sha_hash);
    ripemd160_32(sha_hash, out02 + idx * 20);
#endif

    // Compressed pubkey with prefix 03
    pubkey[0] = 0x03;
#if USE_OPTIMIZED_HASH
    hash160_33_optimized(pubkey, out03 + idx * 20);
#else
    sha256_33(pubkey, sha_hash);
    ripemd160_32(sha_hash, out03 + idx * 20);
#endif
}

// ============================================================================
// Multi-stream infrastructure functions
// ============================================================================

static void cleanup_search_streams(void) {
    if (!g_search_streams_initialized) return;

    for (int i = 0; i < NUM_SEARCH_STREAMS; i++) {
        if (g_search_streams[i].stream) {
            cudaStreamSynchronize(g_search_streams[i].stream);
            cudaStreamDestroy(g_search_streams[i].stream);
        }
        if (g_search_streams[i].start_event) cudaEventDestroy(g_search_streams[i].start_event);
        if (g_search_streams[i].end_event) cudaEventDestroy(g_search_streams[i].end_event);
        if (g_search_streams[i].d_should_stop) cudaFree(g_search_streams[i].d_should_stop);
        memset(&g_search_streams[i], 0, sizeof(search_stream_t));
    }
    g_search_streams_initialized = 0;
}

// ============================================================================
// Backend API implementation
// ============================================================================

extern "C" {

int gpu_backend_init(gpu_backend_info_t *info) {
    int deviceCount = 0;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);

    if (err != cudaSuccess || deviceCount == 0) {
        g_available = 0;
        g_gpu_count = 0;
        if (info) memset(info, 0, sizeof(*info));
        return 0;
    }

    // Limit to MAX_GPUS
    if (deviceCount > MAX_GPUS) {
        printf("[I] Found %d GPUs, limiting to %d\n", deviceCount, MAX_GPUS);
        deviceCount = MAX_GPUS;
    }

    // Initialize all GPU contexts
    g_gpu_count = 0;
    uint64_t total_vram = 0;
    int total_sms = 0;

    for (int i = 0; i < deviceCount; i++) {
        gpu_context_t *ctx = &g_gpus[g_gpu_count];
        memset(ctx, 0, sizeof(gpu_context_t));

        err = cudaGetDeviceProperties(&ctx->props, i);
        if (err != cudaSuccess) {
            printf("[W] Failed to get properties for GPU %d, skipping\n", i);
            continue;
        }

        // Check if GPU is usable (compute capability >= 6.0)
        int cc = ctx->props.major * 10 + ctx->props.minor;
        if (cc < 60) {
            printf("[W] GPU %d (%s) compute capability %d.%d < 6.0, skipping\n",
                   i, ctx->props.name, ctx->props.major, ctx->props.minor);
            continue;
        }

        ctx->device_id = i;
        ctx->active = 1;
        ctx->compute_capability = cc;
        ctx->optimal_params = get_optimal_params(cc, ctx->props.multiProcessorCount);

        total_vram += ctx->props.totalGlobalMem;
        total_sms += ctx->props.multiProcessorCount;

        printf("[+] GPU %d: %s (sm_%d, %d SMs, %lu MB VRAM)\n",
               i, ctx->props.name, cc, ctx->props.multiProcessorCount,
               (unsigned long)(ctx->props.totalGlobalMem / (1024 * 1024)));
        printf("    Optimal params: %d blocks/SM, %d keys/thread\n",
               ctx->optimal_params.blocks_per_sm, ctx->optimal_params.keys_per_thread);

        g_gpu_count++;
    }

    if (g_gpu_count == 0) {
        g_available = 0;
        if (info) memset(info, 0, sizeof(*info));
        return 0;
    }

    // Report multi-GPU status
    if (g_gpu_count > 1) {
        printf("[+] Multi-GPU mode: %d GPUs active, %d total SMs, %lu MB total VRAM\n",
               g_gpu_count, total_sms, (unsigned long)(total_vram / (1024 * 1024)));
    }

    // Fill info struct with first GPU info (for compatibility)
    cudaDeviceProp *prop = &g_gpus[0].props;
    memset(&g_info, 0, sizeof(g_info));
    g_info.gpu_count = g_gpu_count;
    g_info.vram_mb = prop->totalGlobalMem / (1024 * 1024);
    strncpy(g_info.name, prop->name, sizeof(g_info.name) - 1);
    g_info.compute_major = prop->major;
    g_info.compute_minor = prop->minor;
    g_info.multiprocessors = prop->multiProcessorCount;
    g_info.max_threads_per_block = prop->maxThreadsPerBlock;

    // Validate GPU parameters using first GPU's optimal params
    {
        int blocks_per_sm = g_gpus[0].optimal_params.blocks_per_sm;
        int threads_per_block = g_gpus[0].optimal_params.threads_per_block;
        int keys_per_thread = g_gpus[0].optimal_params.keys_per_thread;

        validate_gpu_parameters(
            &blocks_per_sm,
            &threads_per_block,
            &keys_per_thread,
            &g_info,
            true  // auto_correct enabled
        );

        // Update optimal params if validation corrected them
        g_gpus[0].optimal_params.blocks_per_sm = blocks_per_sm;
        g_gpus[0].optimal_params.threads_per_block = threads_per_block;
        g_gpus[0].optimal_params.keys_per_thread = keys_per_thread;
    }

    if (info) *info = g_info;

    g_available = 1;
    return 0;
}

int gpu_backend_available(void) {
    return g_available;
}

// Forward declarations for cleanup
static void cleanup_pinned_memory(void);

void gpu_backend_shutdown(void) {
    // Clean up multi-stream search infrastructure
    cleanup_search_streams();

    // Clean up all GPU contexts
    for (int g = 0; g < g_gpu_count; g++) {
        gpu_context_t *ctx = &g_gpus[g];
        if (!ctx->active) continue;

        cudaSetDevice(ctx->device_id);

        // Clean up device memory
        if (ctx->d_GTable) { cudaFree(ctx->d_GTable); ctx->d_GTable = NULL; }
        if (ctx->d_targets) { cudaFree(ctx->d_targets); ctx->d_targets = NULL; }
        if (ctx->d_bloom) { cudaFree(ctx->d_bloom); ctx->d_bloom = NULL; }

        // Clean up streams
        for (int s = 0; s < NUM_STREAMS_PER_GPU; s++) {
            if (ctx->streams[s]) { cudaStreamDestroy(ctx->streams[s]); ctx->streams[s] = NULL; }
            if (ctx->d_should_stop[s]) { cudaFree(ctx->d_should_stop[s]); ctx->d_should_stop[s] = NULL; }
        }

        ctx->active = 0;
    }
    g_gpu_count = 0;

    // Clean up host-side copies
    if (h_GTable_copy) { free(h_GTable_copy); h_GTable_copy = NULL; }
    if (h_targets_copy) { free(h_targets_copy); h_targets_copy = NULL; }
    if (h_bloom_copy) { free(h_bloom_copy); h_bloom_copy = NULL; }

    // Clean up legacy single-stream context
    if (d_x32) { cudaFree(d_x32); d_x32 = NULL; }
    if (d_out02) { cudaFree(d_out02); d_out02 = NULL; }
    if (d_out03) { cudaFree(d_out03); d_out03 = NULL; }
    if (g_stream) { cudaStreamDestroy(g_stream); g_stream = NULL; }
    g_capacity = 0;

    // Clean up pinned memory pool
    cleanup_pinned_memory();

    g_available = 0;
}

// Pinned memory pool for hash160 batch operations
static uint8_t *h_x32_pinned = NULL;
static uint8_t *h_out02_pinned = NULL;
static uint8_t *h_out03_pinned = NULL;
static size_t g_pinned_capacity = 0;

static void ensure_pinned_memory(size_t count) {
    if (count <= g_pinned_capacity) return;

    // Free old pinned memory
    if (h_x32_pinned) cudaFreeHost(h_x32_pinned);
    if (h_out02_pinned) cudaFreeHost(h_out02_pinned);
    if (h_out03_pinned) cudaFreeHost(h_out03_pinned);

    // Allocate new pinned memory
    cudaMallocHost(&h_x32_pinned, count * 32);
    cudaMallocHost(&h_out02_pinned, count * 20);
    cudaMallocHost(&h_out03_pinned, count * 20);
    g_pinned_capacity = count;
}

static void cleanup_pinned_memory(void) {
    if (h_x32_pinned) { cudaFreeHost(h_x32_pinned); h_x32_pinned = NULL; }
    if (h_out02_pinned) { cudaFreeHost(h_out02_pinned); h_out02_pinned = NULL; }
    if (h_out03_pinned) { cudaFreeHost(h_out03_pinned); h_out03_pinned = NULL; }
    g_pinned_capacity = 0;
}

int gpu_hash160_fromX_batch(const uint8_t *x32_be, size_t count,
                            uint8_t *out02, uint8_t *out03) {
    if (!g_available || count == 0) return 1;

    // Ensure device memory is allocated
    if (count > g_capacity) {
        if (d_x32) cudaFree(d_x32);
        if (d_out02) cudaFree(d_out02);
        if (d_out03) cudaFree(d_out03);

        cudaMalloc(&d_x32, count * 32);
        cudaMalloc(&d_out02, count * 20);
        cudaMalloc(&d_out03, count * 20);
        g_capacity = count;

        if (!g_stream) cudaStreamCreate(&g_stream);
    }

    // Ensure pinned host memory for faster transfers
    ensure_pinned_memory(count);

    // Copy to pinned memory, then async transfer to device
    memcpy(h_x32_pinned, x32_be, count * 32);
    cudaMemcpyAsync(d_x32, h_x32_pinned, count * 32, cudaMemcpyHostToDevice, g_stream);

    // Launch kernel
    int threads = 256;
    int blocks = (count + threads - 1) / threads;
    kernel_hash160_fromX<<<blocks, threads, 0, g_stream>>>(d_x32, count, d_out02, d_out03);

    // Copy results to pinned memory, then to output
    cudaMemcpyAsync(h_out02_pinned, d_out02, count * 20, cudaMemcpyDeviceToHost, g_stream);
    cudaMemcpyAsync(h_out03_pinned, d_out03, count * 20, cudaMemcpyDeviceToHost, g_stream);

    cudaStreamSynchronize(g_stream);

    // Copy from pinned to output (fast memcpy from page-locked memory)
    memcpy(out02, h_out02_pinned, count * 20);
    memcpy(out03, h_out03_pinned, count * 20);

    return (cudaGetLastError() == cudaSuccess) ? 0 : 1;
}

int gpu_upload_gtable(const uint8_t *gtable, size_t point_count) {
    if (!g_available || g_gpu_count == 0) return 1;

    size_t size = point_count * 64;

    // Keep host copy for multi-GPU upload
    if (h_GTable_copy) free(h_GTable_copy);
    h_GTable_copy = (uint8_t*)malloc(size);
    if (!h_GTable_copy) return 1;
    memcpy(h_GTable_copy, gtable, size);

    // Upload to all GPUs
    int success = 0;
    for (int g = 0; g < g_gpu_count; g++) {
        gpu_context_t *ctx = &g_gpus[g];
        if (!ctx->active) continue;

        cudaSetDevice(ctx->device_id);

        if (ctx->d_GTable) cudaFree(ctx->d_GTable);

        cudaError_t err = cudaMalloc(&ctx->d_GTable, size);
        if (err != cudaSuccess) {
            printf("[W] GPU %d: Failed to allocate G table memory\n", ctx->device_id);
            continue;
        }

        err = cudaMemcpy(ctx->d_GTable, gtable, size, cudaMemcpyHostToDevice);
        if (err != cudaSuccess) {
            cudaFree(ctx->d_GTable);
            ctx->d_GTable = NULL;
            printf("[W] GPU %d: Failed to upload G table\n", ctx->device_id);
            continue;
        }

        success++;
    }

    g_GTable_count = point_count;
    return (success > 0) ? 0 : 1;
}

int gpu_upload_targets(const uint8_t *targets, size_t count) {
    if (!g_available || g_gpu_count == 0) return 1;

    size_t size = count * 20;

    // Keep host copy for multi-GPU upload
    if (h_targets_copy) free(h_targets_copy);
    h_targets_copy = (uint8_t*)malloc(size);
    if (!h_targets_copy) return 1;
    memcpy(h_targets_copy, targets, size);

    // Upload to all GPUs
    int success = 0;
    for (int g = 0; g < g_gpu_count; g++) {
        gpu_context_t *ctx = &g_gpus[g];
        if (!ctx->active) continue;

        cudaSetDevice(ctx->device_id);

        if (ctx->d_targets) cudaFree(ctx->d_targets);

        cudaError_t err = cudaMalloc(&ctx->d_targets, size);
        if (err != cudaSuccess) {
            printf("[W] GPU %d: Failed to allocate targets memory\n", ctx->device_id);
            continue;
        }

        err = cudaMemcpy(ctx->d_targets, targets, size, cudaMemcpyHostToDevice);
        if (err != cudaSuccess) {
            cudaFree(ctx->d_targets);
            ctx->d_targets = NULL;
            printf("[W] GPU %d: Failed to upload targets\n", ctx->device_id);
            continue;
        }

        // Populate constant-memory fast path for very small target sets.
        {
            int small_count = 0;
            if (count > 0 && count <= MAX_SMALL_TARGETS) {
                cudaMemcpyToSymbol(d_targets_small, targets, size);
                small_count = (int)count;
            }
            cudaMemcpyToSymbol(d_targets_small_count, &small_count, sizeof(int));
        }

        success++;
    }

    g_target_count = count;
    return (success > 0) ? 0 : 1;
}

int gpu_upload_bloom(const uint8_t *bloom_data, size_t bloom_size, int num_hashes) {
    if (!g_available || g_gpu_count == 0) return 1;

    // Keep host copy for multi-GPU upload
    if (h_bloom_copy) free(h_bloom_copy);
    h_bloom_copy = (uint8_t*)malloc(bloom_size);
    if (!h_bloom_copy) return 1;
    memcpy(h_bloom_copy, bloom_data, bloom_size);

    // Upload to all GPUs
    int success = 0;
    for (int g = 0; g < g_gpu_count; g++) {
        gpu_context_t *ctx = &g_gpus[g];
        if (!ctx->active) continue;

        cudaSetDevice(ctx->device_id);

        if (ctx->d_bloom) cudaFree(ctx->d_bloom);

        cudaError_t err = cudaMalloc(&ctx->d_bloom, bloom_size);
        if (err != cudaSuccess) {
            printf("[W] GPU %d: Failed to allocate bloom filter memory\n", ctx->device_id);
            continue;
        }

        err = cudaMemcpy(ctx->d_bloom, bloom_data, bloom_size, cudaMemcpyHostToDevice);
        if (err != cudaSuccess) {
            cudaFree(ctx->d_bloom);
            ctx->d_bloom = NULL;
            printf("[W] GPU %d: Failed to upload bloom filter\n", ctx->device_id);
            continue;
        }

        success++;
    }

    g_bloom_size = bloom_size;
    g_bloom_hashes = num_hashes;
    return (success > 0) ? 0 : 1;
}

// Per-GPU worker state for multi-GPU search
typedef struct {
    int gpu_idx;
    gpu_context_t *ctx;
    uint256_d cursor;
    uint256_d end_key;
    const gpu_search_config_t *config;
    uint64_t keys_processed;
    int found_count;
    int active;
    int stream_idx;
} gpu_worker_state_t;

// Initialize streams for a specific GPU
static int init_gpu_streams(gpu_context_t *ctx) {
    cudaSetDevice(ctx->device_id);

    for (int s = 0; s < NUM_STREAMS_PER_GPU; s++) {
        if (!ctx->streams[s]) {
            cudaError_t err = cudaStreamCreateWithFlags(&ctx->streams[s], cudaStreamNonBlocking);
            if (err != cudaSuccess) return -1;
        }
        if (!ctx->d_should_stop[s]) {
            cudaError_t err = cudaMalloc(&ctx->d_should_stop[s], sizeof(int));
            if (err != cudaSuccess) return -1;
            int zero = 0;
            cudaMemcpy(ctx->d_should_stop[s], &zero, sizeof(int), cudaMemcpyHostToDevice);
        }
    }
    return 0;
}

int gpu_full_search(const gpu_search_config_t *config) {
    if (!g_available || !config || g_gpu_count == 0) return -1;

    // Verify all GPUs have required data
    for (int g = 0; g < g_gpu_count; g++) {
        if (!g_gpus[g].active) continue;
        if (!g_gpus[g].d_GTable || g_GTable_count == 0) {
            fprintf(stderr, "[!] GPU %d: G table not uploaded\n", g_gpus[g].device_id);
            return -1;
        }
        if (!g_gpus[g].d_targets || g_target_count == 0) {
            fprintf(stderr, "[!] GPU %d: targets not uploaded\n", g_gpus[g].device_id);
            return -1;
        }
    }

    // Count active GPUs and compute total SMs for proportional work distribution
    int active_gpus = 0;
    int total_sms = 0;
    for (int g = 0; g < g_gpu_count; g++) {
        if (g_gpus[g].active) {
            active_gpus++;
            total_sms += g_gpus[g].props.multiProcessorCount;
        }
    }

    if (active_gpus == 0) {
        fprintf(stderr, "[!] No active GPUs\n");
        return -1;
    }

    // Convert start_key and end_key to uint256_d
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

    // Initialize worker states for each GPU
    gpu_worker_state_t workers[MAX_GPUS];
    memset(workers, 0, sizeof(workers));

    // Calculate total keys per launch for all GPUs combined
    uint64_t total_keys_per_round = 0;
    for (int g = 0; g < g_gpu_count; g++) {
        gpu_context_t *ctx = &g_gpus[g];
        if (!ctx->active) continue;

        // Use architecture-optimized parameters
        arch_params_t *params = &ctx->optimal_params;

        // Allow runtime override
        int blocks_per_sm = params->blocks_per_sm;
        uint64_t keys_per_thread = params->keys_per_thread;
        {
            const char *env = getenv("KEYHUNT_GPU_BLOCKS_PER_SM");
            if (env && *env) {
                int v = atoi(env);
                if (v >= 4 && v <= 64) blocks_per_sm = v;
            }
            env = getenv("KEYHUNT_GPU_KEYS_PER_THREAD");
            if (env && *env) {
                unsigned long long v = strtoull(env, NULL, 10);
                if (v >= 64 && v <= 65536) keys_per_thread = (uint64_t)v;
            }
        }

        int blocks = ctx->props.multiProcessorCount * blocks_per_sm;
        uint64_t keys_per_launch = (uint64_t)blocks * params->threads_per_block * keys_per_thread;
        total_keys_per_round += keys_per_launch;

        // Initialize streams for this GPU
        if (init_gpu_streams(ctx) < 0) {
            printf("[W] GPU %d: Failed to initialize streams\n", ctx->device_id);
            ctx->active = 0;
            continue;
        }

        // Reset found keys counter on this GPU
        cudaSetDevice(ctx->device_id);
        int zero = 0;
        cudaMemcpyToSymbol(d_found_count, &zero, sizeof(int));
    }

    // Distribute work range across GPUs proportionally to their SM count
    uint256_d cursor;
    memcpy(&cursor, &start_key, sizeof(uint256_d));

    int worker_count = 0;
    for (int g = 0; g < g_gpu_count; g++) {
        gpu_context_t *ctx = &g_gpus[g];
        if (!ctx->active) continue;

        workers[worker_count].gpu_idx = g;
        workers[worker_count].ctx = ctx;
        workers[worker_count].config = config;
        workers[worker_count].keys_processed = 0;
        workers[worker_count].found_count = 0;
        workers[worker_count].active = 1;
        workers[worker_count].stream_idx = 0;
        memcpy(&workers[worker_count].cursor, &cursor, sizeof(uint256_d));
        worker_count++;
    }

    // Timing
    struct timeval tv0;
    gettimeofday(&tv0, NULL);

    // Print multi-GPU info
    if (active_gpus > 1) {
        printf("[+] Multi-GPU search: %d GPUs, %d total SMs\n", active_gpus, total_sms);
        for (int w = 0; w < worker_count; w++) {
            gpu_context_t *ctx = workers[w].ctx;
            printf("    GPU %d (%s): sm_%d, %d SMs, %d blocks/SM, %d keys/thread\n",
                   ctx->device_id, ctx->props.name, ctx->compute_capability,
                   ctx->props.multiProcessorCount,
                   ctx->optimal_params.blocks_per_sm, ctx->optimal_params.keys_per_thread);
        }
    } else {
        gpu_context_t *ctx = workers[0].ctx;
        arch_params_t *params = &ctx->optimal_params;
        int blocks = ctx->props.multiProcessorCount * params->blocks_per_sm;
        printf("[+] GPU search (sm_%d): %d blocks x %d threads x %d keys/thread\n",
               ctx->compute_capability, blocks, params->threads_per_block, params->keys_per_thread);
    }
    printf("[+] Range: [start, end) using architecture-optimized parameters\n");
    fflush(stdout);

    uint64_t total_keys = 0;
    int total_found = 0;
    int zero = 0;

    // Main search loop - round-robin across GPUs
    while (!*(config->should_stop) && u256_cmp_host(&cursor, &end_key) < 0) {
        // Launch kernels on all GPUs
        for (int w = 0; w < worker_count; w++) {
            gpu_worker_state_t *worker = &workers[w];
            if (!worker->active) continue;

            gpu_context_t *ctx = worker->ctx;
            arch_params_t *params = &ctx->optimal_params;

            cudaSetDevice(ctx->device_id);

            // Use architecture-optimized parameters (with optional override)
            int blocks_per_sm = params->blocks_per_sm;
            uint64_t keys_per_thread = params->keys_per_thread;
            {
                const char *env = getenv("KEYHUNT_GPU_BLOCKS_PER_SM");
                if (env && *env) {
                    int v = atoi(env);
                    if (v >= 4 && v <= 64) blocks_per_sm = v;
                }
                env = getenv("KEYHUNT_GPU_KEYS_PER_THREAD");
                if (env && *env) {
                    unsigned long long v = strtoull(env, NULL, 10);
                    if (v >= 64 && v <= 65536) keys_per_thread = (uint64_t)v;
                }
            }

            int threads_per_block = params->threads_per_block;
            int blocks = ctx->props.multiProcessorCount * blocks_per_sm;
            uint64_t keys_per_launch = (uint64_t)blocks * threads_per_block * keys_per_thread;

            // Check remaining keys
            uint64_t remaining64 = u256_sub_sat_u64_host(&end_key, &cursor);
            if (remaining64 == 0) {
                worker->active = 0;
                continue;
            }

            uint64_t keys_this_launch = (remaining64 < keys_per_launch) ? remaining64 : keys_per_launch;

            // Adjust keys_per_thread for last batch
            uint64_t actual_keys_per_thread = keys_per_thread;
            if (keys_this_launch < keys_per_launch) {
                uint64_t denom = (uint64_t)blocks * (uint64_t)threads_per_block;
                actual_keys_per_thread = (keys_this_launch + denom - 1) / denom;
                if (actual_keys_per_thread < 1) actual_keys_per_thread = 1;
            }

            // Update should_stop
            int should_stop_val = *(config->should_stop);
            cudaMemcpyAsync(ctx->d_should_stop[worker->stream_idx], &should_stop_val, sizeof(int),
                           cudaMemcpyHostToDevice, ctx->streams[worker->stream_idx]);

            // Launch kernel (keep the compressed-only kernel as the fast path)
            if (config->search_uncompressed) {
                kernel_full_search_xy<<<blocks, threads_per_block, 0, ctx->streams[worker->stream_idx]>>>(
                    cursor,
                    0,
                    actual_keys_per_thread,
                    keys_this_launch,
                    config->search_compressed,
                    config->search_uncompressed,
                    ctx->d_GTable,
                    ctx->d_targets, g_target_count,
                    ctx->d_bloom, g_bloom_size, g_bloom_hashes,
                    config->use_bloom,
                    ctx->d_should_stop[worker->stream_idx]
                );
            } else {
                kernel_full_search_compressed<<<blocks, threads_per_block, 0, ctx->streams[worker->stream_idx]>>>(
                    cursor,
                    0,
                    actual_keys_per_thread,
                    keys_this_launch,
                    ctx->d_GTable,
                    ctx->d_targets, g_target_count,
                    ctx->d_bloom, g_bloom_size, g_bloom_hashes,
                    config->use_bloom,
                    ctx->d_should_stop[worker->stream_idx]
                );
            }

            // Advance cursor
            u256_add_u64_host(&cursor, keys_this_launch);
            worker->keys_processed += keys_this_launch;
            total_keys += keys_this_launch;

            // Switch stream for double-buffering
            worker->stream_idx = (worker->stream_idx + 1) % NUM_STREAMS_PER_GPU;
        }

        // Synchronize all GPUs and check for results
        for (int w = 0; w < worker_count; w++) {
            gpu_worker_state_t *worker = &workers[w];
            if (!worker->active) continue;

            gpu_context_t *ctx = worker->ctx;
            cudaSetDevice(ctx->device_id);

            // Wait for the previous stream
            int prev_stream = (worker->stream_idx + NUM_STREAMS_PER_GPU - 1) % NUM_STREAMS_PER_GPU;
            cudaError_t err = cudaStreamSynchronize(ctx->streams[prev_stream]);
            if (err != cudaSuccess) {
                fprintf(stderr, "\n[!] GPU %d kernel error: %s\n", ctx->device_id, cudaGetErrorString(err));
                worker->active = 0;
                continue;
            }

            // Check for found keys
            int found_count = 0;
            cudaMemcpyFromSymbol(&found_count, d_found_count, sizeof(int));

            if (found_count > 0) {
                FoundKey h_found[MAX_FOUND_KEYS];
                cudaMemcpyFromSymbol(h_found, d_found_keys, sizeof(FoundKey) * found_count);

                for (int i = 0; i < found_count && i < MAX_FOUND_KEYS; i++) {
                    if (h_found[i].valid) {
                        total_found++;
                        worker->found_count++;

                        uint8_t privkey_be[32];
                        for (int w2 = 0; w2 < 8; w2++) {
                            uint32_t val = h_found[i].privkey.d[7 - w2];
                            privkey_be[w2*4]     = (val >> 24) & 0xFF;
                            privkey_be[w2*4 + 1] = (val >> 16) & 0xFF;
                            privkey_be[w2*4 + 2] = (val >> 8) & 0xFF;
                            privkey_be[w2*4 + 3] = val & 0xFF;
                        }

	                        if (config->callback) {
	                            config->callback(privkey_be,
	                                           h_found[i].compressed != 0,
	                                           config->callback_userdata);
	                        }
                    }
                }

                cudaMemcpyToSymbol(d_found_count, &zero, sizeof(int));
            }
        }

        // Update keys_checked
        if (config->keys_checked) {
            *(config->keys_checked) = total_keys;
        }

        // Progress output
        static uint64_t last_progress_keys = 0;
        if (!config->quiet && (total_keys - last_progress_keys) >= (total_keys_per_round * 10)) {
            struct timeval tv1;
            gettimeofday(&tv1, NULL);
            double elapsed_sec = (double)(tv1.tv_sec - tv0.tv_sec) +
                                 (double)(tv1.tv_usec - tv0.tv_usec) / 1e6;
            last_progress_keys = total_keys;

            double speed = 0.0;
            const char *speed_unit = "keys/s";

            if (elapsed_sec > 0.1) {
                speed = (double)total_keys / elapsed_sec;

                if (speed >= 1e9) {
                    speed /= 1e9;
                    speed_unit = "Gkeys/s";
                } else if (speed >= 1e6) {
                    speed /= 1e6;
                    speed_unit = "Mkeys/s";
                } else if (speed >= 1e3) {
                    speed /= 1e3;
                    speed_unit = "Kkeys/s";
                }
            }

            if (active_gpus > 1) {
                printf("\r[+] %d GPUs: %.2f %s | %lu keys | found: %d   ",
                       active_gpus, speed, speed_unit, (unsigned long)total_keys, total_found);
            } else {
                printf("\r[+] GPU: %.2f %s | %lu keys | found: %d   ",
                       speed, speed_unit, (unsigned long)total_keys, total_found);
            }
            fflush(stdout);
        }
    }

    // Final synchronization and result collection
    for (int w = 0; w < worker_count; w++) {
        gpu_worker_state_t *worker = &workers[w];
        gpu_context_t *ctx = worker->ctx;

        cudaSetDevice(ctx->device_id);

        for (int s = 0; s < NUM_STREAMS_PER_GPU; s++) {
            cudaStreamSynchronize(ctx->streams[s]);
        }

        // Final check for found keys
        int found_count = 0;
        cudaMemcpyFromSymbol(&found_count, d_found_count, sizeof(int));

        if (found_count > 0) {
            FoundKey h_found[MAX_FOUND_KEYS];
            cudaMemcpyFromSymbol(h_found, d_found_keys, sizeof(FoundKey) * found_count);

            for (int i = 0; i < found_count && i < MAX_FOUND_KEYS; i++) {
                if (h_found[i].valid) {
                    total_found++;
                    uint8_t privkey_be[32];
                    for (int w2 = 0; w2 < 8; w2++) {
                        uint32_t val = h_found[i].privkey.d[7 - w2];
                        privkey_be[w2*4]     = (val >> 24) & 0xFF;
                        privkey_be[w2*4 + 1] = (val >> 16) & 0xFF;
                        privkey_be[w2*4 + 2] = (val >> 8) & 0xFF;
                        privkey_be[w2*4 + 3] = val & 0xFF;
                    }
	                    if (config->callback) {
	                        config->callback(privkey_be,
	                                        h_found[i].compressed != 0,
	                                        config->callback_userdata);
	                    }
                }
            }
        }
    }

    // Final timing
    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);
    double total_elapsed_sec = (double)(tv_end.tv_sec - tv0.tv_sec) +
                               (double)(tv_end.tv_usec - tv0.tv_usec) / 1e6;

    double final_speed = (total_elapsed_sec > 0) ? (double)total_keys / total_elapsed_sec : 0.0;
    const char *final_unit = "keys/s";
    if (final_speed >= 1e9) { final_speed /= 1e9; final_unit = "Gkeys/s"; }
    else if (final_speed >= 1e6) { final_speed /= 1e6; final_unit = "Mkeys/s"; }
    else if (final_speed >= 1e3) { final_speed /= 1e3; final_unit = "Kkeys/s"; }

    printf("\n[+] GPU search completed in %.2f seconds (avg: %.2f %s)\n",
           total_elapsed_sec, final_speed, final_unit);

    if (active_gpus > 1) {
        printf("[+] Per-GPU stats:\n");
        for (int w = 0; w < worker_count; w++) {
            gpu_context_t *ctx = workers[w].ctx;
            printf("    GPU %d: %lu keys, %d found\n",
                   ctx->device_id, (unsigned long)workers[w].keys_processed, workers[w].found_count);
        }
    }

    return total_found;
}

size_t gpu_get_optimal_batch_size(void) {
    if (!g_available) return 0;
    return g_info.multiprocessors * 2048;
}

double gpu_benchmark(size_t duration_ms) {
    (void)duration_ms;
    return 0.0;
}

} // extern "C"
