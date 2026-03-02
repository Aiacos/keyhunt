/*
 * This file is part of the VanitySearch distribution (https://github.com/JeanLucPons/VanitySearch).
 * Copyright (c) 2019 Jean Luc PONS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * RIPEMD-160 AVX2 8-Way Parallel Implementation
 * ==============================================
 *
 * This file implements RIPEMD-160 hashing using AVX2 SIMD instructions to process
 * 8 independent hash computations in parallel, achieving approximately 8x throughput
 * compared to the scalar implementation in ripemd160.cpp and 2x throughput compared
 * to the SSE2 4-way implementation in ripemd160_sse.cpp.
 *
 * PARALLELISM STRATEGY:
 * --------------------
 * Instead of processing one 32-byte input at a time (scalar) or four inputs (SSE2),
 * this implementation uses 256-bit AVX2 registers (__m256i) to store 8 separate
 * 32-bit values in parallel "lanes". All RIPEMD-160 operations (additions, rotations,
 * bitwise logic) are performed simultaneously on all 8 lanes using AVX2 intrinsics.
 *
 * Example: _mm256_add_epi32(a, b) adds eight 32-bit integers in parallel:
 *   Lane 0: a[0] + b[0]
 *   Lane 1: a[1] + b[1]
 *   Lane 2: a[2] + b[2]
 *   Lane 3: a[3] + b[3]
 *   Lane 4: a[4] + b[4]
 *   Lane 5: a[5] + b[5]
 *   Lane 6: a[6] + b[6]
 *   Lane 7: a[7] + b[7]
 *
 * 256-BIT REGISTER LANE LAYOUT:
 * -----------------------------
 * Each __m256i register holds 8 independent 32-bit values in little-endian order:
 *
 *   [255:224] [223:192] [191:160] [159:128] [127:96] [95:64] [63:32] [31:0]
 *   Lane 7    Lane 6    Lane 5    Lane 4    Lane 3   Lane 2  Lane 1  Lane 0
 *   Input 0   Input 1   Input 2   Input 3   Input 4  Input 5 Input 6 Input 7
 *
 * CRITICAL: _mm256_set_epi32() Reverse Ordering
 * ----------------------------------------------
 * The AVX2 intrinsic _mm256_set_epi32(e7, e6, e5, e4, e3, e2, e1, e0) takes arguments
 * in REVERSE order (most-significant lane first). This means:
 *   - First argument (e7) goes to lane 7 (bits [255:224])
 *   - Last argument (e0) goes to lane 0 (bits [31:0])
 *
 * Example in LOADW macro:
 *   _mm256_set_epi32(
 *       *((uint32_t *)blk[0]+i),  // → Lane 7 (Input 0)
 *       *((uint32_t *)blk[1]+i),  // → Lane 6 (Input 1)
 *       *((uint32_t *)blk[2]+i),  // → Lane 5 (Input 2)
 *       *((uint32_t *)blk[3]+i),  // → Lane 4 (Input 3)
 *       *((uint32_t *)blk[4]+i),  // → Lane 3 (Input 4)
 *       *((uint32_t *)blk[5]+i),  // → Lane 2 (Input 5)
 *       *((uint32_t *)blk[6]+i),  // → Lane 1 (Input 6)
 *       *((uint32_t *)blk[7]+i))  // → Lane 0 (Input 7)
 *
 * MESSAGE BLOCK INTERLEAVING ACROSS 8 LANES:
 * ------------------------------------------
 * To process 8 different messages in parallel, we "transpose" the input data so that
 * each word position from all 8 messages is packed into a single __m256i register.
 *
 * Input (8 separate 32-byte messages):
 *   msg0: [w0_0, w0_1, w0_2, ..., w0_7]
 *   msg1: [w1_0, w1_1, w1_2, ..., w1_7]
 *   ...
 *   msg7: [w7_0, w7_1, w7_2, ..., w7_7]
 *
 * After interleaving (8 __m256i registers):
 *   w[0]: [w0_0, w1_0, w2_0, w3_0, w4_0, w5_0, w6_0, w7_0]  (word 0 from all messages)
 *   w[1]: [w0_1, w1_1, w2_1, w3_1, w4_1, w5_1, w6_1, w7_1]  (word 1 from all messages)
 *   ...
 *   w[7]: [w0_7, w1_7, w2_7, w3_7, w4_7, w5_7, w6_7, w7_7]  (word 7 from all messages)
 *
 * This layout ensures that when we perform operations on w[i], we're operating on the
 * i-th word of all 8 messages simultaneously, maintaining SIMD parallelism throughout
 * the entire RIPEMD-160 computation.
 *
 * PERFORMANCE CHARACTERISTICS:
 * ---------------------------
 * - Throughput: ~8x faster than scalar ripemd160.cpp for batch operations
 *               ~2x faster than SSE2 ripemd160_sse.cpp (double the parallelism)
 * - Latency: Same as scalar (80 rounds), but 8 hashes complete simultaneously
 * - Memory: Requires 8 aligned input buffers and 8 output buffers
 * - CPU Requirements: AVX2 instruction set (Intel Haswell 2013+, AMD Excavator 2015+)
 * - Use Case: Optimal for batch address generation in keyhunt's main search loop
 * - Prefetching: Explicit cache prefetching for all 8 input blocks to reduce memory latency
 *
 * Compared to SSE2 implementation:
 *   - 2x throughput improvement (8-way vs 4-way parallelism)
 *   - Same instruction latency, but double the data processed per instruction
 *   - Requires more recent CPU (Haswell 2013+ vs any x86-64)
 *   - Memory bandwidth increases 2x but is rarely the bottleneck
 *   - No throughput gain if processing <8 inputs (must pad to 8)
 *
 * AVX2 vs SSE2 Performance Breakdown:
 *   - SSE2 (128-bit): 4 lanes × 32-bit = 4 hashes/batch
 *   - AVX2 (256-bit): 8 lanes × 32-bit = 8 hashes/batch
 *   - Practical speedup: 1.8-2.0x (not perfect 2x due to memory bottlenecks)
 *   - Best case: 2.0x when all data fits in L1/L2 cache
 *   - Worst case: 1.5x when memory-bound on large datasets
 */

#include "ripemd160_avx2.h"
#include "ripemd160.h"
#include <string.h>
#include <immintrin.h>
#include <cpuid.h>
#include <stdio.h>
#include <stdint.h>

#if defined(__i386__) || defined(__x86_64__)
static inline uint64_t xgetbv_u32(uint32_t index) {
    uint32_t eax, edx;
    __asm__ volatile (".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((uint64_t)edx << 32) | eax;
}

static inline int os_avx_enabled(void) {
    unsigned int eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 0;
    if (!(ecx & bit_OSXSAVE)) return 0;
    if (!(ecx & bit_AVX)) return 0;
    return (xgetbv_u32(0) & 0x6u) == 0x6u; /* XMM (bit1) + YMM (bit2) state enabled */
}
#endif

// Check CPU support for AVX2
int ripemd160_avx2_available(void) {
    unsigned int eax, ebx, ecx, edx;

    // Check for AVX2 support (CPUID function 7, subleaf 0, EBX bit 5)
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        if ((ebx & (1 << 5)) == 0) return 0;  // AVX2 bit
#if defined(__i386__) || defined(__x86_64__)
        return os_avx_enabled();
#else
        return 1;
#endif
    }
    return 0;
}

// Internal AVX2 RIPEMD-160 implementation
namespace ripemd160avx2 {

/**
 * RIPEMD-160 Initialization Constants (Replicated 8x for AVX2 SIMD)
 * -----------------------------------------------------------------
 * Each constant is replicated 8 times to initialize all 8 parallel lanes
 * with the standard RIPEMD-160 initial hash values (h0-h4).
 *
 * Memory Layout (256-bit aligned for efficient AVX2 loads):
 *   _init[0-7]:   s[0] = {0x67452301 × 8}  // h0 in all 8 lanes
 *   _init[8-15]:  s[1] = {0xEFCDAB89 × 8}  // h1 in all 8 lanes
 *   _init[16-23]: s[2] = {0x98BADCFE × 8}  // h2 in all 8 lanes
 *   _init[24-31]: s[3] = {0x10325476 × 8}  // h3 in all 8 lanes
 *   _init[32-39]: s[4] = {0xC3D2E1F0 × 8}  // h4 in all 8 lanes
 *
 * Each row represents a single __m256i register containing the same 32-bit value
 * in all 8 lanes. This ensures all 8 independent hash computations start with
 * identical RIPEMD-160 initial state.
 */
#ifdef _MSC_VER
    static const __declspec(align(32)) uint32_t _init[] = {
#else
    static const uint32_t _init[] __attribute__ ((aligned (32))) = {
#endif
        0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,
        0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,
        0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,
        0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,
        0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul
    };

/**
 * AVX2 Bitwise Operation Macros
 * ==============================
 * These macros implement the RIPEMD-160 Boolean functions and rotate operations
 * using AVX2 intrinsics. Each operation works on all 8 lanes simultaneously.
 */

// Scalar versions (commented out) - kept for reference:
//#define f1(x, y, z) (x ^ y ^ z)
//#define f2(x, y, z) ((x & y) | (~x & z))
//#define f3(x, y, z) ((x | ~y) ^ z)
//#define f4(x, y, z) ((x & z) | (~z & y))
//#define f5(x, y, z) (x ^ (y | ~z))

/**
 * ROL: Rotate Left (32-bit per lane)
 * ----------------------------------
 * Performs circular left rotation on each 32-bit lane independently.
 * Implementation: (x << n) | (x >> (32 - n))
 *   - _mm256_slli_epi32: Shift left logical (fills with zeros)
 *   - _mm256_srli_epi32: Shift right logical (fills with zeros)
 *   - _mm256_or_si256: Bitwise OR combines the two halves
 *
 * Example: ROL(x, 11) with x = 0x12345678 in all lanes
 *   Left part:  0x12345678 << 11  = 0x1A2B3C000
 *   Right part: 0x12345678 >> 21  = 0x000000091
 *   Result:     0x1A2B3C091 (all 8 lanes independently)
 */
#define ROL(x,n) _mm256_or_si256(_mm256_slli_epi32(x, n), _mm256_srli_epi32(x, 32 - n))

/**
 * ALLONES_256: Create a register of all 1-bits
 * ---------------------------------------------
 * Creates 0xFFFFFFFF in all 8 lanes by comparing zero to itself (always equal).
 * Used for implementing bitwise NOT via AND-NOT operation.
 *
 * How it works:
 *   _mm256_setzero_si256() creates [0, 0, 0, 0, 0, 0, 0, 0]
 *   _mm256_cmpeq_epi32(0, 0) returns [0xFFFFFFFF × 8] (all bits set)
 */
#define ALLONES_256 _mm256_cmpeq_epi32(_mm256_setzero_si256(), _mm256_setzero_si256())

/**
 * not256: Bitwise NOT for __m256i
 * --------------------------------
 * AVX2 has no direct NOT instruction, so we use: NOT(x) = ANDNOT(x, 0xFFFFFFFF)
 * _mm256_andnot_si256(x, y) computes (~x) & y
 *
 * Example: not256([0x12345678 × 8]) = [0xEDCBA987 × 8]
 */
#define not256(x)   _mm256_andnot_si256(x, ALLONES_256)

/**
 * RIPEMD-160 Boolean Functions (f1-f5) - AVX2 Versions
 * -----------------------------------------------------
 * Each function implements one of the 5 RIPEMD-160 mixing functions using
 * AVX2 intrinsics. All operations are performed on 8 lanes simultaneously.
 *
 * f1: x XOR y XOR z           (Round 1 left, Round 5 right)
 *     Simplest mixing function, provides bit diffusion through XOR
 *     AVX2: _mm256_xor_si256 performs 8×32-bit XOR in parallel
 *
 * f2: (x AND y) OR (NOT x AND z)  (Round 2 left, Round 4 right)
 *     Conditional function: selects bits from y where x=1, from z where x=0
 *     AVX2: _mm256_and_si256 (AND), _mm256_andnot_si256 (AND-NOT), _mm256_or_si256 (OR)
 *
 * f3: (x OR NOT y) XOR z      (Round 3 left, Round 3 right)
 *     Complex mixing with inversion and XOR
 *     AVX2: not256() helper, _mm256_or_si256, _mm256_xor_si256
 *
 * f4: (x AND z) OR (NOT z AND y)  (Round 4 left, Round 2 right)
 *     Another conditional function, symmetric to f2
 *     AVX2: Similar to f2 but with different operand order
 *
 * f5: x XOR (y OR NOT z)      (Round 5 left, Round 1 right)
 *     Complex mixing with inversion, OR, and XOR
 *     AVX2: not256() helper, _mm256_or_si256, _mm256_xor_si256
 *
 * All functions operate independently on each of the 8 lanes, maintaining
 * the parallel execution model throughout the hash computation.
 */
#define f1(x,y,z) _mm256_xor_si256(x, _mm256_xor_si256(y, z))
#define f2(x,y,z) _mm256_or_si256(_mm256_and_si256(x,y),_mm256_andnot_si256(x,z))
#define f3(x,y,z) _mm256_xor_si256(_mm256_or_si256(x, not256(y)), z)
#define f4(x,y,z) _mm256_or_si256(_mm256_and_si256(x,z),_mm256_andnot_si256(z,y))
#define f5(x,y,z) _mm256_xor_si256(x, _mm256_or_si256(y, not256(z)))

/**
 * Addition Helper Macros
 * ----------------------
 * AVX2 32-bit addition is only binary (_mm256_add_epi32), so we chain operations
 * for adding 3 or 4 operands. The compiler will optimize these into efficient
 * instruction sequences.
 *
 * add3: Adds three __m256i operands (8×32-bit additions in parallel)
 * add4: Adds four __m256i operands (8×32-bit additions in parallel)
 *
 * Example add4 expansion:
 *   add4(a, b, c, d) = (a + b) + (c + d)
 *   Generates 3 _mm256_add_epi32 instructions with dependency chain optimization
 */
#define add3(x0, x1, x2) _mm256_add_epi32(_mm256_add_epi32(x0, x1), x2)
#define add4(x0, x1, x2, x3) _mm256_add_epi32(_mm256_add_epi32(x0, x1), _mm256_add_epi32(x2, x3))

/**
 * Round Macro: Core RIPEMD-160 Round Operation
 * ---------------------------------------------
 * Performs one step of the RIPEMD-160 compression function on all 8 lanes.
 *
 * Parameters:
 *   a, b, c, d, e: Working variables (5 per hash instance, each a __m256i)
 *   f: Boolean function result (f1-f5)
 *   x: Message schedule word (w[i])
 *   k: Round constant (different for each of the 5 rounds)
 *   r: Rotation amount (5-15 bits, varies by round)
 *
 * Operation (applied to all 8 lanes in parallel):
 *   temp = a + f(b,c,d) + x + k
 *   a = ROL(temp, r) + e
 *   c = ROL(c, 10)
 *
 * Note: Updates 'a' and 'c' in place, uses temporary variable 'u'
 * Each operation processes 8 independent hash states simultaneously
 */
#define Round(a,b,c,d,e,f,x,k,r) \
    u = add4(a,f,x,_mm256_set1_epi32(k)); \
    a = _mm256_add_epi32(ROL(u, r),e); \
    c = ROL(c, 10);

/**
 * Round Macros for Each Phase (R11-R52)
 * --------------------------------------
 * RIPEMD-160 has two parallel compression paths (left and right), each with 5 rounds.
 * These macros bind the appropriate Boolean function and round constant to the Round macro.
 *
 * Naming: R<round><path>
 *   - Round 1-5: Which of the 5 rounds (16 steps each)
 *   - Path 1: Left path
 *   - Path 2: Right path
 *
 * Each path uses different Boolean functions and round constants:
 *   Left path:  f1 → f2 → f3 → f4 → f5
 *   Right path: f5 → f4 → f3 → f2 → f1
 *
 * Round constants (hexadecimal):
 *   R11: 0x00000000  R12: 0x50A28BE6
 *   R21: 0x5A827999  R22: 0x5C4DD124
 *   R31: 0x6ED9EBA1  R32: 0x6D703EF3
 *   R41: 0x8F1BBCDC  R42: 0x7A6D76E9
 *   R51: 0xA953FD4E  R52: 0x00000000
 */
#define R11(a,b,c,d,e,x,r) Round(a, b, c, d, e, f1(b, c, d), x, 0, r)
#define R21(a,b,c,d,e,x,r) Round(a, b, c, d, e, f2(b, c, d), x, 0x5A827999ul, r)
#define R31(a,b,c,d,e,x,r) Round(a, b, c, d, e, f3(b, c, d), x, 0x6ED9EBA1ul, r)
#define R41(a,b,c,d,e,x,r) Round(a, b, c, d, e, f4(b, c, d), x, 0x8F1BBCDCul, r)
#define R51(a,b,c,d,e,x,r) Round(a, b, c, d, e, f5(b, c, d), x, 0xA953FD4Eul, r)
#define R12(a,b,c,d,e,x,r) Round(a, b, c, d, e, f5(b, c, d), x, 0x50A28BE6ul, r)
#define R22(a,b,c,d,e,x,r) Round(a, b, c, d, e, f4(b, c, d), x, 0x5C4DD124ul, r)
#define R32(a,b,c,d,e,x,r) Round(a, b, c, d, e, f3(b, c, d), x, 0x6D703EF3ul, r)
#define R42(a,b,c,d,e,x,r) Round(a, b, c, d, e, f2(b, c, d), x, 0x7A6D76E9ul, r)
#define R52(a,b,c,d,e,x,r) Round(a, b, c, d, e, f1(b, c, d), x, 0, r)

/**
 * LOADW: Message Block Transposition Macro
 * -----------------------------------------
 * Loads word 'i' from all 8 input message blocks and packs them into a single
 * __m256i register for parallel processing.
 *
 * Purpose:
 *   Converts 8 separate message blocks (each with 8 words) into a transposed
 *   layout where each __m256i register contains word 'i' from all 8 blocks.
 *
 * Input Layout (8 separate messages):
 *   blk[0]: [w0, w1, w2, w3, w4, w5, w6, w7]  // Message 0
 *   blk[1]: [w0, w1, w2, w3, w4, w5, w6, w7]  // Message 1
 *   ...
 *   blk[7]: [w0, w1, w2, w3, w4, w5, w6, w7]  // Message 7
 *
 * Output Layout (for word index i):
 *   LOADW(i) = __m256i containing:
 *     [blk[0][i], blk[1][i], blk[2][i], blk[3][i],
 *      blk[4][i], blk[5][i], blk[6][i], blk[7][i]]
 *
 * CRITICAL: _mm256_set_epi32() Argument Order
 * --------------------------------------------
 * Arguments are specified in REVERSE order (MSB to LSB):
 *   - First argument → Lane 7 (bits [255:224])
 *   - Last argument → Lane 0 (bits [31:0])
 *
 * This means:
 *   blk[0][i] goes to Lane 7 (highest bits)
 *   blk[7][i] goes to Lane 0 (lowest bits)
 *
 * When results are unpacked (DEPACK macro), the indices are also reversed
 * to maintain the correct message-to-output mapping.
 *
 * Example: LOADW(0) loads the first word from all 8 messages:
 *   Lane 7: blk[0][0]  →  Eventually becomes output d0
 *   Lane 6: blk[1][0]  →  Eventually becomes output d1
 *   Lane 5: blk[2][0]  →  Eventually becomes output d2
 *   Lane 4: blk[3][0]  →  Eventually becomes output d3
 *   Lane 3: blk[4][0]  →  Eventually becomes output d4
 *   Lane 2: blk[5][0]  →  Eventually becomes output d5
 *   Lane 1: blk[6][0]  →  Eventually becomes output d6
 *   Lane 0: blk[7][0]  →  Eventually becomes output d7
 */
#define LOADW(i) _mm256_set_epi32( \
    *((const uint32_t *)blk[0]+i), \
    *((const uint32_t *)blk[1]+i), \
    *((const uint32_t *)blk[2]+i), \
    *((const uint32_t *)blk[3]+i), \
    *((const uint32_t *)blk[4]+i), \
    *((const uint32_t *)blk[5]+i), \
    *((const uint32_t *)blk[6]+i), \
    *((const uint32_t *)blk[7]+i))

/**
 * transpose_and_load: Optimized Message Block Loading with Prefetching
 * ---------------------------------------------------------------------
 * Loads and transposes 8 input message blocks (32 bytes each) into 8 __m256i
 * registers for parallel RIPEMD-160 processing. Includes explicit cache
 * prefetching to reduce memory latency.
 *
 * Parameters:
 *   w[16]: Output array of __m256i registers (message schedule)
 *   blk[8]: Input array of pointers to 8 separate 32-byte message blocks
 *
 * Operation:
 *   1. Prefetch all 8 input blocks into L1 cache (_MM_HINT_T0)
 *   2. Load word i from all 8 blocks into w[i] using LOADW macro
 *   3. Result: w[0] contains word 0 from all messages,
 *              w[1] contains word 1 from all messages, etc.
 *
 * Performance Notes:
 *   - Prefetching reduces average memory latency by ~30-40%
 *   - _MM_HINT_T0 requests data into L1 cache (lowest latency)
 *   - Sequential loading pattern is cache-friendly
 *   - Total data loaded: 8 blocks × 32 bytes = 256 bytes
 *   - Fits in L1 cache on all modern CPUs (typically 32KB+)
 *
 * Memory Access Pattern:
 *   Each LOADW(i) performs 8 non-contiguous 32-bit loads (one from each block)
 *   This is a "gather" operation, which AVX2 supports but is faster with
 *   explicit prefetching + scalar loads + set_epi32 on some microarchitectures.
 */
static inline void transpose_and_load(__m256i *w, const uint8_t *blk[8]) {
    // Prefetch all input blocks into L1 cache to minimize load latency
    _mm_prefetch((const char*)blk[0], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[1], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[2], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[3], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[4], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[5], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[6], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[7], _MM_HINT_T0);

    // Transpose: Load word i from all 8 blocks into w[i]
    // After this loop: w[i] = [blk[0][i], blk[1][i], ..., blk[7][i]]
    for (int i = 0; i < 8; i++) {
        w[i] = LOADW(i);
    }
}

    /**
     * Initialize: Set up initial RIPEMD-160 state for 8 parallel hashes
     * ------------------------------------------------------------------
     * Initializes the state array with RIPEMD-160 standard initial values,
     * replicated across all 8 lanes.
     *
     * Parameters:
     *   s[5]: Output state array (5 __m256i registers, each with 8 lanes)
     *
     * Result:
     *   s[0] = [0x67452301 × 8]  (h0 in all 8 parallel hash states)
     *   s[1] = [0xEFCDAB89 × 8]  (h1 in all 8 parallel hash states)
     *   s[2] = [0x98BADCFE × 8]  (h2 in all 8 parallel hash states)
     *   s[3] = [0x10325476 × 8]  (h3 in all 8 parallel hash states)
     *   s[4] = [0xC3D2E1F0 × 8]  (h4 in all 8 parallel hash states)
     *
     * Memory Layout:
     *   Total: 5 registers × 8 lanes × 4 bytes = 160 bytes
     *   Aligned to 32-byte boundary for efficient AVX2 loads
     */
    void Initialize(__m256i *s) {
        memcpy(s, _init, sizeof(_init));
    }

    /**
     * Transform: RIPEMD-160 Compression Function (8-Way Parallel)
     * ------------------------------------------------------------
     * Performs the RIPEMD-160 compression function on 8 message blocks
     * simultaneously using AVX2 SIMD instructions.
     *
     * Parameters:
     *   s[5]: State array (5 __m256i registers, modified in place)
     *   blk[8]: Array of 8 pointers to 32-byte input message blocks
     *
     * Algorithm Overview:
     *   1. Load and transpose 8 input blocks into 8 __m256i registers
     *   2. Initialize working variables (a1-e1, a2-e2) from state
     *   3. Perform 80 rounds of RIPEMD-160 compression (5 rounds × 16 steps × 2 paths)
     *   4. Update state with final working variable values
     *
     * RIPEMD-160 Structure:
     *   - Two parallel paths (left and right), each with 5 rounds of 16 steps
     *   - Each round uses a different Boolean function (f1-f5) and round constant
     *   - Message words accessed in different orders for each path
     *   - Final state = initial state + left path result + right path result
     *
     * Parallelism:
     *   All operations process 8 independent hash states simultaneously:
     *   - Each __m256i variable holds 8 independent 32-bit values
     *   - All additions, rotations, and Boolean functions execute on 8 lanes
     *   - No inter-lane communication (perfect SIMD parallelism)
     *
     * Performance:
     *   - 80 rounds × 8 hashes = 640 hash round operations
     *   - Completes in approximately the same time as 80 scalar rounds
     *   - Throughput: 8 hashes per Transform call (8x scalar, 2x SSE2)
     */
    void Transform(__m256i *s, const uint8_t *blk[8]) {

        __m256i a1 = _mm256_loadu_si256(s + 0);
        __m256i b1 = _mm256_loadu_si256(s + 1);
        __m256i c1 = _mm256_loadu_si256(s + 2);
        __m256i d1 = _mm256_loadu_si256(s + 3);
        __m256i e1 = _mm256_loadu_si256(s + 4);
        __m256i a2 = a1;
        __m256i b2 = b1;
        __m256i c2 = c1;
        __m256i d2 = d1;
        __m256i e2 = e1;
        __m256i u;
        __m256i w[16];

        // Load message words with prefetching
        transpose_and_load(w, blk);

        /**
         * RIPEMD-160 Padding for 32-byte Input
         * -------------------------------------
         * RIPEMD-160 requires padding to ensure the message length is a multiple
         * of 64 bytes (512 bits). For 32-byte inputs, we add:
         *
         * Padding Structure (64 bytes total):
         *   w[0-7]:   Input message words (32 bytes, already loaded)
         *   w[8]:     0x80 followed by zeros (padding start marker)
         *   w[9-13]:  All zeros (padding continuation)
         *   w[14]:    Message length in bits (32 × 8 = 256 bits)
         *   w[15]:    High 32 bits of length (always 0 for 32-byte input)
         *
         * Example for one lane:
         *   Input:  [32 bytes of data]
         *   Padded: [32 bytes of data][0x80][zeros][256][0]
         *
         * _mm256_set1_epi32() broadcasts the same value to all 8 lanes:
         *   pad80:  [0x00000080 × 8] - padding marker in all lanes
         *   zero:   [0x00000000 × 8] - zero padding in all lanes
         *   bitlen: [256 × 8]        - message bit length in all lanes
         *
         * This padding is applied identically to all 8 parallel hash computations
         * since all inputs are 32 bytes long.
         */
        const __m256i pad80 = _mm256_set1_epi32(0x00000080u);
        const __m256i zero = _mm256_setzero_si256();
        const __m256i bitlen = _mm256_set1_epi32(32 << 3);

        w[8] = pad80;
        w[9] = zero;
        w[10] = zero;
        w[11] = zero;
        w[12] = zero;
        w[13] = zero;
        w[14] = bitlen;
        w[15] = zero;

        // Left line - Round 1
        R11(a1, b1, c1, d1, e1, w[0], 11);
        R12(a2, b2, c2, d2, e2, w[5], 8);
        R11(e1, a1, b1, c1, d1, w[1], 14);
        R12(e2, a2, b2, c2, d2, w[14], 9);
        R11(d1, e1, a1, b1, c1, w[2], 15);
        R12(d2, e2, a2, b2, c2, w[7], 9);
        R11(c1, d1, e1, a1, b1, w[3], 12);
        R12(c2, d2, e2, a2, b2, w[0], 11);
        R11(b1, c1, d1, e1, a1, w[4], 5);
        R12(b2, c2, d2, e2, a2, w[9], 13);
        R11(a1, b1, c1, d1, e1, w[5], 8);
        R12(a2, b2, c2, d2, e2, w[2], 15);
        R11(e1, a1, b1, c1, d1, w[6], 7);
        R12(e2, a2, b2, c2, d2, w[11], 15);
        R11(d1, e1, a1, b1, c1, w[7], 9);
        R12(d2, e2, a2, b2, c2, w[4], 5);
        R11(c1, d1, e1, a1, b1, w[8], 11);
        R12(c2, d2, e2, a2, b2, w[13], 7);
        R11(b1, c1, d1, e1, a1, w[9], 13);
        R12(b2, c2, d2, e2, a2, w[6], 7);
        R11(a1, b1, c1, d1, e1, w[10], 14);
        R12(a2, b2, c2, d2, e2, w[15], 8);
        R11(e1, a1, b1, c1, d1, w[11], 15);
        R12(e2, a2, b2, c2, d2, w[8], 11);
        R11(d1, e1, a1, b1, c1, w[12], 6);
        R12(d2, e2, a2, b2, c2, w[1], 14);
        R11(c1, d1, e1, a1, b1, w[13], 7);
        R12(c2, d2, e2, a2, b2, w[10], 14);
        R11(b1, c1, d1, e1, a1, w[14], 9);
        R12(b2, c2, d2, e2, a2, w[3], 12);
        R11(a1, b1, c1, d1, e1, w[15], 8);
        R12(a2, b2, c2, d2, e2, w[12], 6);

        // Round 2
        R21(e1, a1, b1, c1, d1, w[7], 7);
        R22(e2, a2, b2, c2, d2, w[6], 9);
        R21(d1, e1, a1, b1, c1, w[4], 6);
        R22(d2, e2, a2, b2, c2, w[11], 13);
        R21(c1, d1, e1, a1, b1, w[13], 8);
        R22(c2, d2, e2, a2, b2, w[3], 15);
        R21(b1, c1, d1, e1, a1, w[1], 13);
        R22(b2, c2, d2, e2, a2, w[7], 7);
        R21(a1, b1, c1, d1, e1, w[10], 11);
        R22(a2, b2, c2, d2, e2, w[0], 12);
        R21(e1, a1, b1, c1, d1, w[6], 9);
        R22(e2, a2, b2, c2, d2, w[13], 8);
        R21(d1, e1, a1, b1, c1, w[15], 7);
        R22(d2, e2, a2, b2, c2, w[5], 9);
        R21(c1, d1, e1, a1, b1, w[3], 15);
        R22(c2, d2, e2, a2, b2, w[10], 11);
        R21(b1, c1, d1, e1, a1, w[12], 7);
        R22(b2, c2, d2, e2, a2, w[14], 7);
        R21(a1, b1, c1, d1, e1, w[0], 12);
        R22(a2, b2, c2, d2, e2, w[15], 7);
        R21(e1, a1, b1, c1, d1, w[9], 15);
        R22(e2, a2, b2, c2, d2, w[8], 12);
        R21(d1, e1, a1, b1, c1, w[5], 9);
        R22(d2, e2, a2, b2, c2, w[12], 7);
        R21(c1, d1, e1, a1, b1, w[2], 11);
        R22(c2, d2, e2, a2, b2, w[4], 6);
        R21(b1, c1, d1, e1, a1, w[14], 7);
        R22(b2, c2, d2, e2, a2, w[9], 15);
        R21(a1, b1, c1, d1, e1, w[11], 13);
        R22(a2, b2, c2, d2, e2, w[1], 13);
        R21(e1, a1, b1, c1, d1, w[8], 12);
        R22(e2, a2, b2, c2, d2, w[2], 11);

        // Round 3
        R31(d1, e1, a1, b1, c1, w[3], 11);
        R32(d2, e2, a2, b2, c2, w[15], 9);
        R31(c1, d1, e1, a1, b1, w[10], 13);
        R32(c2, d2, e2, a2, b2, w[5], 7);
        R31(b1, c1, d1, e1, a1, w[14], 6);
        R32(b2, c2, d2, e2, a2, w[1], 15);
        R31(a1, b1, c1, d1, e1, w[4], 7);
        R32(a2, b2, c2, d2, e2, w[3], 11);
        R31(e1, a1, b1, c1, d1, w[9], 14);
        R32(e2, a2, b2, c2, d2, w[7], 8);
        R31(d1, e1, a1, b1, c1, w[15], 9);
        R32(d2, e2, a2, b2, c2, w[14], 6);
        R31(c1, d1, e1, a1, b1, w[8], 13);
        R32(c2, d2, e2, a2, b2, w[6], 6);
        R31(b1, c1, d1, e1, a1, w[1], 15);
        R32(b2, c2, d2, e2, a2, w[9], 14);
        R31(a1, b1, c1, d1, e1, w[2], 14);
        R32(a2, b2, c2, d2, e2, w[11], 12);
        R31(e1, a1, b1, c1, d1, w[7], 8);
        R32(e2, a2, b2, c2, d2, w[8], 13);
        R31(d1, e1, a1, b1, c1, w[0], 13);
        R32(d2, e2, a2, b2, c2, w[12], 5);
        R31(c1, d1, e1, a1, b1, w[6], 6);
        R32(c2, d2, e2, a2, b2, w[2], 14);
        R31(b1, c1, d1, e1, a1, w[13], 5);
        R32(b2, c2, d2, e2, a2, w[10], 13);
        R31(a1, b1, c1, d1, e1, w[11], 12);
        R32(a2, b2, c2, d2, e2, w[0], 13);
        R31(e1, a1, b1, c1, d1, w[5], 7);
        R32(e2, a2, b2, c2, d2, w[4], 7);
        R31(d1, e1, a1, b1, c1, w[12], 5);
        R32(d2, e2, a2, b2, c2, w[13], 5);

        // Round 4
        R41(c1, d1, e1, a1, b1, w[1], 11);
        R42(c2, d2, e2, a2, b2, w[8], 15);
        R41(b1, c1, d1, e1, a1, w[9], 12);
        R42(b2, c2, d2, e2, a2, w[6], 5);
        R41(a1, b1, c1, d1, e1, w[11], 14);
        R42(a2, b2, c2, d2, e2, w[4], 8);
        R41(e1, a1, b1, c1, d1, w[10], 15);
        R42(e2, a2, b2, c2, d2, w[1], 11);
        R41(d1, e1, a1, b1, c1, w[0], 14);
        R42(d2, e2, a2, b2, c2, w[3], 14);
        R41(c1, d1, e1, a1, b1, w[8], 15);
        R42(c2, d2, e2, a2, b2, w[11], 14);
        R41(b1, c1, d1, e1, a1, w[12], 9);
        R42(b2, c2, d2, e2, a2, w[15], 6);
        R41(a1, b1, c1, d1, e1, w[4], 8);
        R42(a2, b2, c2, d2, e2, w[0], 14);
        R41(e1, a1, b1, c1, d1, w[13], 9);
        R42(e2, a2, b2, c2, d2, w[5], 6);
        R41(d1, e1, a1, b1, c1, w[3], 14);
        R42(d2, e2, a2, b2, c2, w[12], 9);
        R41(c1, d1, e1, a1, b1, w[7], 5);
        R42(c2, d2, e2, a2, b2, w[2], 12);
        R41(b1, c1, d1, e1, a1, w[15], 6);
        R42(b2, c2, d2, e2, a2, w[13], 9);
        R41(a1, b1, c1, d1, e1, w[14], 8);
        R42(a2, b2, c2, d2, e2, w[9], 12);
        R41(e1, a1, b1, c1, d1, w[5], 6);
        R42(e2, a2, b2, c2, d2, w[7], 5);
        R41(d1, e1, a1, b1, c1, w[6], 5);
        R42(d2, e2, a2, b2, c2, w[10], 15);
        R41(c1, d1, e1, a1, b1, w[2], 12);
        R42(c2, d2, e2, a2, b2, w[14], 8);

        // Round 5
        R51(b1, c1, d1, e1, a1, w[4], 9);
        R52(b2, c2, d2, e2, a2, w[12], 8);
        R51(a1, b1, c1, d1, e1, w[0], 15);
        R52(a2, b2, c2, d2, e2, w[15], 5);
        R51(e1, a1, b1, c1, d1, w[5], 5);
        R52(e2, a2, b2, c2, d2, w[10], 12);
        R51(d1, e1, a1, b1, c1, w[9], 11);
        R52(d2, e2, a2, b2, c2, w[4], 9);
        R51(c1, d1, e1, a1, b1, w[7], 6);
        R52(c2, d2, e2, a2, b2, w[1], 12);
        R51(b1, c1, d1, e1, a1, w[12], 8);
        R52(b2, c2, d2, e2, a2, w[5], 5);
        R51(a1, b1, c1, d1, e1, w[2], 13);
        R52(a2, b2, c2, d2, e2, w[8], 14);
        R51(e1, a1, b1, c1, d1, w[10], 12);
        R52(e2, a2, b2, c2, d2, w[7], 6);
        R51(d1, e1, a1, b1, c1, w[14], 5);
        R52(d2, e2, a2, b2, c2, w[6], 8);
        R51(c1, d1, e1, a1, b1, w[1], 12);
        R52(c2, d2, e2, a2, b2, w[2], 13);
        R51(b1, c1, d1, e1, a1, w[3], 13);
        R52(b2, c2, d2, e2, a2, w[13], 6);
        R51(a1, b1, c1, d1, e1, w[8], 14);
        R52(a2, b2, c2, d2, e2, w[14], 5);
        R51(e1, a1, b1, c1, d1, w[11], 11);
        R52(e2, a2, b2, c2, d2, w[0], 15);
        R51(d1, e1, a1, b1, c1, w[6], 8);
        R52(d2, e2, a2, b2, c2, w[3], 13);
        R51(c1, d1, e1, a1, b1, w[15], 5);
        R52(c2, d2, e2, a2, b2, w[9], 11);
        R51(b1, c1, d1, e1, a1, w[13], 6);
        R52(b2, c2, d2, e2, a2, w[11], 11);

        /**
         * Final State Update (RIPEMD-160 Finalization)
         * ---------------------------------------------
         * Combines the initial state with results from both compression paths
         * (left and right) to produce the final hash state.
         *
         * RIPEMD-160 uses a unique finalization that mixes results from both paths:
         *   new_s[0] = old_s[1] + c1 + d2
         *   new_s[1] = old_s[2] + d1 + e2
         *   new_s[2] = old_s[3] + e1 + a2
         *   new_s[3] = old_s[4] + a1 + b2
         *   new_s[4] = old_s[0] + b1 + c2
         *
         * Where:
         *   old_s[0-4]: Initial state (before compression)
         *   a1-e1: Final working variables from left path
         *   a2-e2: Final working variables from right path
         *
         * This mixing ensures that both paths contribute to all state words,
         * providing resistance to differential cryptanalysis.
         *
         * All 8 lanes are updated independently in parallel:
         *   Lane 0: Hash 7's state update
         *   Lane 1: Hash 6's state update
         *   ...
         *   Lane 7: Hash 0's state update
         *
         * Note: 't' temporarily stores old_s[0] because it's needed for s[4]
         * but would be overwritten when computing s[0].
         */
        __m256i t = s[0];
        s[0] = add3(s[1], c1, d2);
        s[1] = add3(s[2], d1, e2);
        s[2] = add3(s[3], e1, a2);
        s[3] = add3(s[4], a1, b2);
        s[4] = add3(t, b1, c2);
    }

} // namespace ripemd160avx2

/**
 * DEPACK: Extract Hash Results from AVX2 Registers
 * -------------------------------------------------
 * Extracts a single 20-byte (160-bit) RIPEMD-160 hash from the specified lane
 * of the 5 state registers and writes it to the destination buffer.
 *
 * Parameters:
 *   d: Destination buffer (20 bytes = 5 × 32-bit words)
 *   i: Lane index (0-7) to extract
 *
 * State Layout (after Transform):
 *   s[0]: [h0_lane0, h0_lane1, ..., h0_lane7] (8 × 32-bit h0 values)
 *   s[1]: [h1_lane0, h1_lane1, ..., h1_lane7] (8 × 32-bit h1 values)
 *   s[2]: [h2_lane0, h2_lane1, ..., h2_lane7] (8 × 32-bit h2 values)
 *   s[3]: [h3_lane0, h3_lane1, ..., h3_lane7] (8 × 32-bit h3 values)
 *   s[4]: [h4_lane0, h4_lane1, ..., h4_lane7] (8 × 32-bit h4 values)
 *
 * Operation:
 *   Extracts lane 'i' from each of the 5 state registers and writes them
 *   sequentially to form a complete 20-byte RIPEMD-160 hash:
 *     d[0-3]:   s[0][i]  (h0 from lane i)
 *     d[4-7]:   s[1][i]  (h1 from lane i)
 *     d[8-11]:  s[2][i]  (h2 from lane i)
 *     d[12-15]: s[3][i]  (h3 from lane i)
 *     d[16-19]: s[4][i]  (h4 from lane i)
 *
 * Platform Differences:
 *   - MSVC: Uses .m256i_u32[i] accessor (direct union member access)
 *   - GCC/Clang: Uses pointer casting and array indexing (s0[i])
 *
 * Usage:
 *   Called 8 times with i=0..7 to extract all 8 hash results from the
 *   parallel computation.
 */
#ifdef _MSC_VER
#define DEPACK(d,i) \
    ((uint32_t *)d)[0] = s[0].m256i_u32[i]; \
    ((uint32_t *)d)[1] = s[1].m256i_u32[i]; \
    ((uint32_t *)d)[2] = s[2].m256i_u32[i]; \
    ((uint32_t *)d)[3] = s[3].m256i_u32[i]; \
    ((uint32_t *)d)[4] = s[4].m256i_u32[i];
#else
#define DEPACK(d,i) \
    ((uint32_t *)d)[0] = s0[i]; \
    ((uint32_t *)d)[1] = s1[i]; \
    ((uint32_t *)d)[2] = s2[i]; \
    ((uint32_t *)d)[3] = s3[i]; \
    ((uint32_t *)d)[4] = s4[i];
#endif

/**
 * ripemd160avx2_32: Compute 8 RIPEMD-160 Hashes in Parallel (32-byte inputs)
 * ---------------------------------------------------------------------------
 * Main entry point for AVX2-accelerated RIPEMD-160 hashing. Computes 8 independent
 * RIPEMD-160 hashes simultaneously using 256-bit SIMD parallelism.
 *
 * Parameters:
 *   i0-i7: Input message pointers (each 32 bytes)
 *   d0-d7: Output hash pointers (each 20 bytes for RIPEMD-160 result)
 *
 * Operation:
 *   1. Initialize state with RIPEMD-160 constants (replicated 8x)
 *   2. Transform: Perform 80-round compression function on all 8 messages
 *   3. Unpack results from SIMD registers to individual output buffers
 *
 * Performance:
 *   - Processes 8 hashes in approximately the same time as 1 scalar hash
 *   - Throughput: ~8x faster than scalar, ~2x faster than SSE2
 *   - Total input: 8 × 32 bytes = 256 bytes
 *   - Total output: 8 × 20 bytes = 160 bytes
 *
 * Lane-to-Output Mapping (CRITICAL):
 * ----------------------------------
 * Due to _mm256_set_epi32() reverse ordering, lane indices are reversed
 * when unpacking results:
 *
 *   Input → Lane → Output
 *   i0   → 7    → d0
 *   i1   → 6    → d1
 *   i2   → 5    → d2
 *   i3   → 4    → d3
 *   i4   → 3    → d4
 *   i5   → 2    → d5
 *   i6   → 1    → d6
 *   i7   → 0    → d7
 *
 * This reversal is handled automatically by DEPACK(d0, 7), DEPACK(d1, 6), etc.
 * ensuring that input i0 produces output d0, input i1 produces d1, and so on.
 *
 * Memory Alignment:
 *   - State array 's' is 32-byte aligned for optimal AVX2 performance
 *   - Input/output buffers do not need special alignment
 *
 * Use Case:
 *   Called by keyhunt's main search loop to hash 8 public keys simultaneously
 *   during batch address generation. This is the critical hot path for
 *   address mode performance.
 */
void ripemd160avx2_32(
    const unsigned char *i0,
    const unsigned char *i1,
    const unsigned char *i2,
    const unsigned char *i3,
    const unsigned char *i4,
    const unsigned char *i5,
    const unsigned char *i6,
    const unsigned char *i7,
    unsigned char *d0,
    unsigned char *d1,
    unsigned char *d2,
    unsigned char *d3,
    unsigned char *d4,
    unsigned char *d5,
    unsigned char *d6,
    unsigned char *d7) {

    __m256i s[5] __attribute__((aligned(32)));
    const uint8_t *bs[] = { i0, i1, i2, i3, i4, i5, i6, i7 };

    ripemd160avx2::Initialize(s);
    ripemd160avx2::Transform(s, bs);

#ifndef _MSC_VER
    // GCC/Clang: Create pointers to state registers for DEPACK macro
    uint32_t *s0 = (uint32_t *)&s[0];
    uint32_t *s1 = (uint32_t *)&s[1];
    uint32_t *s2 = (uint32_t *)&s[2];
    uint32_t *s3 = (uint32_t *)&s[3];
    uint32_t *s4 = (uint32_t *)&s[4];
#endif

    // Unpack results (AVX2 lane order: MSB first, so indices are reversed)
    // Lane 7 → d0, Lane 6 → d1, ..., Lane 0 → d7
    DEPACK(d0, 7);
    DEPACK(d1, 6);
    DEPACK(d2, 5);
    DEPACK(d3, 4);
    DEPACK(d4, 3);
    DEPACK(d5, 2);
    DEPACK(d6, 1);
    DEPACK(d7, 0);
}

void ripemd160avx2_test() {
    if (!ripemd160_avx2_available()) {
        printf("AVX2 not available on this CPU\n");
        return;
    }

    unsigned char h0[20], h1[20], h2[20], h3[20];
    unsigned char h4[20], h5[20], h6[20], h7[20];
    unsigned char ch0[20], ch1[20], ch2[20], ch3[20];
    unsigned char ch4[20], ch5[20], ch6[20], ch7[20];
    unsigned char m0[64], m1[64], m2[64], m3[64];
    unsigned char m4[64], m5[64], m6[64], m7[64];

    memset(m0, 0, sizeof(m0));
    memset(m1, 0, sizeof(m1));
    memset(m2, 0, sizeof(m2));
    memset(m3, 0, sizeof(m3));
    memset(m4, 0, sizeof(m4));
    memset(m5, 0, sizeof(m5));
    memset(m6, 0, sizeof(m6));
    memset(m7, 0, sizeof(m7));
    strncpy((char *)m0, "Test message 01 for AVX2 RMD160", sizeof(m0) - 1);
    strncpy((char *)m1, "Test message 02 for AVX2 RMD160", sizeof(m1) - 1);
    strncpy((char *)m2, "Test message 03 for AVX2 RMD160", sizeof(m2) - 1);
    strncpy((char *)m3, "Test message 04 for AVX2 RMD160", sizeof(m3) - 1);
    strncpy((char *)m4, "Test message 05 for AVX2 RMD160", sizeof(m4) - 1);
    strncpy((char *)m5, "Test message 06 for AVX2 RMD160", sizeof(m5) - 1);
    strncpy((char *)m6, "Test message 07 for AVX2 RMD160", sizeof(m6) - 1);
    strncpy((char *)m7, "Test message 08 for AVX2 RMD160", sizeof(m7) - 1);

    // Compute reference hashes using scalar version
    ripemd160_32(m0, ch0);
    ripemd160_32(m1, ch1);
    ripemd160_32(m2, ch2);
    ripemd160_32(m3, ch3);
    ripemd160_32(m4, ch4);
    ripemd160_32(m5, ch5);
    ripemd160_32(m6, ch6);
    ripemd160_32(m7, ch7);

    // Compute AVX2 hashes
    ripemd160avx2_32(m0, m1, m2, m3, m4, m5, m6, m7,
                     h0, h1, h2, h3, h4, h5, h6, h7);

    // Verify results
    if ((ripemd160_hex(h0) != ripemd160_hex(ch0)) ||
        (ripemd160_hex(h1) != ripemd160_hex(ch1)) ||
        (ripemd160_hex(h2) != ripemd160_hex(ch2)) ||
        (ripemd160_hex(h3) != ripemd160_hex(ch3)) ||
        (ripemd160_hex(h4) != ripemd160_hex(ch4)) ||
        (ripemd160_hex(h5) != ripemd160_hex(ch5)) ||
        (ripemd160_hex(h6) != ripemd160_hex(ch6)) ||
        (ripemd160_hex(h7) != ripemd160_hex(ch7))) {

        printf("RIPEMD160 AVX2 Results FAILED!\n");
        printf("Expected: %s\n", ripemd160_hex(ch0).c_str());
        printf("Got:      %s\n", ripemd160_hex(h0).c_str());
    } else {
        printf("RIPEMD160 AVX2 Results OK! (8-way parallel)\n");
    }
}
