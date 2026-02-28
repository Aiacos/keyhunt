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
 * RIPEMD-160 SSE2 4-Way Parallel Implementation
 * ==============================================
 *
 * This file implements RIPEMD-160 hashing using SSE2 SIMD instructions to process
 * 4 independent hash computations in parallel, achieving approximately 4x throughput
 * compared to the scalar implementation in ripemd160.cpp.
 *
 * PARALLELISM STRATEGY:
 * --------------------
 * Instead of processing one 32-byte input at a time, this implementation uses 128-bit
 * SSE2 registers (__m128i) to store 4 separate 32-bit values in parallel "lanes".
 * All RIPEMD-160 operations (additions, rotations, bitwise logic) are performed
 * simultaneously on all 4 lanes using SIMD intrinsics.
 *
 * Example: _mm_add_epi32(a, b) adds four 32-bit integers in parallel:
 *   Lane 0: a[0] + b[0]
 *   Lane 1: a[1] + b[1]
 *   Lane 2: a[2] + b[2]
 *   Lane 3: a[3] + b[3]
 *
 * 128-BIT REGISTER LANE LAYOUT:
 * -----------------------------
 * Each __m128i register holds 4 independent 32-bit values in little-endian order:
 *
 *   [127:96] [95:64] [63:32] [31:0]
 *   Lane 3   Lane 2  Lane 1  Lane 0
 *   Input 0  Input 1 Input 2 Input 3
 *
 * Note: The lane-to-input mapping is reversed (lane 3 = input 0) due to the
 * LOADW macro's _mm_set_epi32() argument order (most-significant first).
 *
 * PERFORMANCE CHARACTERISTICS:
 * ---------------------------
 * - Throughput: ~4x faster than scalar ripemd160.cpp for batch operations
 * - Latency: Same as scalar (80 rounds), but 4 hashes complete simultaneously
 * - Memory: Requires 4 aligned input buffers and 4 output buffers
 * - CPU Requirements: SSE2 instruction set (available on all x86-64 CPUs)
 * - Use Case: Optimal for batch address generation in keyhunt's main search loop
 *
 * Compared to scalar implementation:
 *   - 4x throughput improvement when all 4 lanes utilized
 *   - No throughput gain if processing <4 inputs (must pad to 4)
 *   - Memory bandwidth increases 4x but is rarely the bottleneck
 */

#include "ripemd160.h"
#include <string.h>
#include <immintrin.h>

// Internal SSE2 RIPEMD-160 implementation
namespace ripemd160sse {

/**
 * RIPEMD-160 Initialization Constants (Replicated 4x for SIMD)
 * ------------------------------------------------------------
 * Each constant is replicated 4 times to initialize all 4 parallel lanes
 * with the standard RIPEMD-160 initial hash values.
 *
 * Layout: Each row contains the same constant repeated 4 times:
 *   s[0] = {0x67452301, 0x67452301, 0x67452301, 0x67452301}  // h0
 *   s[1] = {0xEFCDAB89, 0xEFCDAB89, 0xEFCDAB89, 0xEFCDAB89}  // h1
 *   s[2] = {0x98BADCFE, 0x98BADCFE, 0x98BADCFE, 0x98BADCFE}  // h2
 *   s[3] = {0x10325476, 0x10325476, 0x10325476, 0x10325476}  // h3
 *   s[4] = {0xC3D2E1F0, 0xC3D2E1F0, 0xC3D2E1F0, 0xC3D2E1F0}  // h4
 */
#ifdef _MSC_VER
  static const __declspec(align(16)) uint32_t _init[] = {
#else
  static const uint32_t _init[] __attribute__ ((aligned (16))) = {
#endif
      0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,
      0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,
      0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,
      0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,
      0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul
  };

/**
 * SSE2 Bitwise Operation Macros
 * ==============================
 * These macros implement the RIPEMD-160 Boolean functions and rotate operations
 * using SSE2 intrinsics. Each operation works on all 4 lanes simultaneously.
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
 *   - _mm_slli_epi32: Shift left logical (fills with zeros)
 *   - _mm_srli_epi32: Shift right logical (fills with zeros)
 *   - _mm_or_si128: Bitwise OR combines the two halves
 */
#define ROL(x,n) _mm_or_si128( _mm_slli_epi32(x, n) , _mm_srli_epi32(x, 32 - n) )

/**
 * ALLONES_128: Create a register of all 1-bits
 * ---------------------------------------------
 * Creates 0xFFFFFFFF in all 4 lanes by comparing zero to itself (always equal).
 * Used for implementing bitwise NOT via AND-NOT operation.
 */
#define ALLONES_128 _mm_cmpeq_epi32(_mm_setzero_si128(), _mm_setzero_si128())

/**
 * not128: Bitwise NOT for __m128i
 * --------------------------------
 * SSE2 has no direct NOT instruction, so we use: NOT(x) = ANDNOT(x, 0xFFFFFFFF)
 * _mm_andnot_si128(x, y) computes (~x) & y
 */
#define not128(x)   _mm_andnot_si128(x, ALLONES_128)

/**
 * RIPEMD-160 Boolean Functions (f1-f5) - SSE2 Versions
 * -----------------------------------------------------
 * Each function implements one of the 5 RIPEMD-160 mixing functions using
 * SSE2 intrinsics. All operations are performed on 4 lanes simultaneously.
 *
 * f1: x XOR y XOR z           (Round 1 left, Round 5 right)
 * f2: (x AND y) OR (NOT x AND z)  (Round 2 left, Round 4 right)
 * f3: (x OR NOT y) XOR z      (Round 3 left, Round 3 right)
 * f4: (x AND z) OR (NOT z AND y)  (Round 4 left, Round 2 right)
 * f5: x XOR (y OR NOT z)      (Round 5 left, Round 1 right)
 */
#define f1(x,y,z) _mm_xor_si128(x, _mm_xor_si128(y, z))
#define f2(x,y,z) _mm_or_si128(_mm_and_si128(x,y),_mm_andnot_si128(x,z))
#define f3(x,y,z) _mm_xor_si128(_mm_or_si128(x, not128(y)), z)
#define f4(x,y,z) _mm_or_si128(_mm_and_si128(x,z),_mm_andnot_si128(z,y))
#define f5(x,y,z) _mm_xor_si128(x, _mm_or_si128(y, not128(z)))

/**
 * Addition Helper Macros
 * ----------------------
 * SSE2 32-bit addition is only binary (_mm_add_epi32), so we chain operations
 * for adding 3 or 4 operands. The compiler will optimize these into efficient
 * instruction sequences.
 */
#define add3(x0, x1, x2 ) _mm_add_epi32(_mm_add_epi32(x0, x1), x2)
#define add4(x0, x1, x2, x3) _mm_add_epi32(_mm_add_epi32(x0, x1), _mm_add_epi32(x2, x3))

/**
 * Round Macro: Core RIPEMD-160 Round Operation
 * ---------------------------------------------
 * Performs one step of the RIPEMD-160 compression function on all 4 lanes.
 *
 * Parameters:
 *   a, b, c, d, e: Working variables (5 per hash instance, each a __m128i)
 *   f: Boolean function result (f1-f5)
 *   x: Message schedule word (w[i])
 *   k: Round constant (different for each of the 5 rounds)
 *   r: Rotation amount (5-15 bits, varies by round)
 *
 * Operation:
 *   temp = a + f(b,c,d) + x + k
 *   a = ROL(temp, r) + e
 *   c = ROL(c, 10)
 *
 * Note: Updates 'a' and 'c' in place, uses temporary variable 'u'
 */
#define Round(a,b,c,d,e,f,x,k,r) \
  u = add4(a,f,x,_mm_set1_epi32(k)); \
  a = _mm_add_epi32(ROL(u, r),e); \
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
 * Each path uses different:
 *   - Boolean functions (f1-f5 in different orders)
 *   - Round constants (0x00000000, 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xA953FD4E, 0x50A28BE6, etc.)
 *   - Message schedule order (different permutation of w[0]-w[15])
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
 * LOADW: Message Schedule Word Loader
 * ------------------------------------
 * Loads the i-th 32-bit word from 4 different input buffers into a single __m128i.
 *
 * Input Layout:
 *   blk[0], blk[1], blk[2], blk[3]: Four 32-byte (8 uint32_t) input buffers
 *
 * Operation:
 *   Creates a __m128i with:
 *     Lane 3 (bits 127:96): blk[0][i]
 *     Lane 2 (bits  95:64): blk[1][i]
 *     Lane 1 (bits  63:32): blk[2][i]
 *     Lane 0 (bits  31:0):  blk[3][i]
 *
 * Note: _mm_set_epi32 takes arguments in reverse order (most-significant first),
 * so blk[0] ends up in the highest lane (lane 3).
 *
 * Example: LOADW(0) loads the first word from each of the 4 inputs:
 *   w[0] = {blk[0][0], blk[1][0], blk[2][0], blk[3][0]}
 */
#define LOADW(i) _mm_set_epi32(*((const uint32_t *)blk[0]+i),*((const uint32_t *)blk[1]+i),*((const uint32_t *)blk[2]+i),*((const uint32_t *)blk[3]+i))

  /**
   * Initialize: Set up initial RIPEMD-160 state for 4 parallel hashes
   * -----------------------------------------------------------------
   * Copies the replicated initial hash values into the state array.
   * After initialization, each of the 5 state registers (s[0]-s[4])
   * contains the same constant replicated 4 times, one for each lane.
   */
  void Initialize(__m128i *s) {
    memcpy(s, _init, sizeof(_init));
  }

  /**
   * Transform: RIPEMD-160 Compression Function (4-way Parallel)
   * -----------------------------------------------------------
   * Performs the RIPEMD-160 compression function on 4 independent 32-byte inputs
   * simultaneously using SSE2 instructions.
   *
   * Parameters:
   *   s: State array (5 __m128i registers, each holding 4 lanes)
   *   blk: Array of 4 pointers to 32-byte input blocks
   *
   * Algorithm Overview:
   * ------------------
   * RIPEMD-160 uses two parallel computation paths (left and right) that process
   * the same message with different schedules, then combines the results.
   *
   * 1. Load initial state into working variables (a1-e1, a2-e2)
   * 2. Load and schedule 16 message words (w[0]-w[15])
   * 3. Execute 80 rounds (16 rounds × 5 phases, interleaved left/right)
   * 4. Combine left and right path results into final state
   *
   * Working Variables:
   * -----------------
   *   a1, b1, c1, d1, e1: Left path working variables (each is __m128i)
   *   a2, b2, c2, d2, e2: Right path working variables (each is __m128i)
   *   u: Temporary register for round calculations
   *   w[16]: Message schedule (16 words, each containing 4 parallel values)
   */
  void Transform(__m128i *s, const uint8_t *blk[4]) {

    // Load initial state into working variables for both paths
    __m128i a1 = _mm_load_si128(s + 0);  // Left path: h0 for all 4 lanes
    __m128i b1 = _mm_load_si128(s + 1);  // Left path: h1 for all 4 lanes
    __m128i c1 = _mm_load_si128(s + 2);  // Left path: h2 for all 4 lanes
    __m128i d1 = _mm_load_si128(s + 3);  // Left path: h3 for all 4 lanes
    __m128i e1 = _mm_load_si128(s + 4);  // Left path: h4 for all 4 lanes
    __m128i a2 = a1;  // Right path starts with same initial state
    __m128i b2 = b1;
    __m128i c2 = c1;
    __m128i d2 = d1;
    __m128i e2 = e1;
    __m128i u;        // Temporary for round calculations
    __m128i w[16];    // Message schedule (16 words × 4 lanes)

    /**
     * MESSAGE SCHEDULING AND PADDING
     * ==============================
     * RIPEMD-160 operates on 512-bit (64-byte) blocks, but our input is exactly
     * 32 bytes (256 bits). The message must be padded according to the Merkle-Damgård
     * construction used by RIPEMD-160.
     *
     * Input Layout (per lane):
     * -----------------------
     *   w[0]-w[7]:   32 bytes of actual input data (8 × 32-bit words)
     *   w[8]:        Padding byte 0x80 (start of padding)
     *   w[9]-w[13]:  Zero padding (5 × 32-bit words = 20 bytes)
     *   w[14]:       Message length in bits (256 bits = 32 << 3)
     *   w[15]:       High bits of length (always 0 for our 32-byte inputs)
     *
     * Total: 16 × 4 bytes = 64 bytes (512 bits) - one RIPEMD-160 block
     */

    // Load 8 words (32 bytes) of actual message data from all 4 input buffers
    w[0] = LOADW(0);  // Load word 0 from blk[0][0], blk[1][0], blk[2][0], blk[3][0]
    w[1] = LOADW(1);  // Load word 1 from all 4 inputs
    w[2] = LOADW(2);  // Load word 2 from all 4 inputs
    w[3] = LOADW(3);  // Load word 3 from all 4 inputs
    w[4] = LOADW(4);  // Load word 4 from all 4 inputs
    w[5] = LOADW(5);  // Load word 5 from all 4 inputs
    w[6] = LOADW(6);  // Load word 6 from all 4 inputs
    w[7] = LOADW(7);  // Load word 7 from all 4 inputs (completes 32 bytes)

    // Prepare padding constants (replicated to all 4 lanes via _mm_set1_epi32)
    const __m128i pad80 = _mm_set1_epi32(0x00000080u);  // Padding start marker
    const __m128i zero = _mm_setzero_si128();            // Zero padding
    const __m128i bitlen = _mm_set1_epi32(32 << 3);     // 256 bits (32 bytes × 8)

    // Apply Merkle-Damgård padding to remaining 32 bytes
    w[8] = pad80;     // 0x80 byte marks end of message, rest zero
    w[9] = zero;      // Zero padding
    w[10] = zero;     // Zero padding
    w[11] = zero;     // Zero padding
    w[12] = zero;     // Zero padding
    w[13] = zero;     // Zero padding
    w[14] = bitlen;   // Message length in bits (low 32 bits)
    w[15] = zero;     // Message length in bits (high 32 bits, always 0 for us)

    /**
     * ROUND OPERATIONS (80 rounds total, interleaved left/right paths)
     * =================================================================
     *
     * RIPEMD-160 executes 80 round operations organized as:
     *   - 5 phases of 16 rounds each
     *   - 2 parallel paths (left path: a1-e1, right path: a2-e2)
     *   - Paths use different message schedules and round constants
     *
     * Round Structure:
     * ---------------
     * Each round updates the working variables via:
     *   temp = a + f(b,c,d) + w[schedule[i]] + round_constant
     *   a = ROL(temp, rotation_amount) + e
     *   c = ROL(c, 10)
     *
     * The rounds alternate between left path (R*1) and right path (R*2) for
     * better instruction-level parallelism on modern CPUs.
     *
     * Phase Summary:
     * -------------
     * Round  1 (R11/R12): f1/f5, constants 0x00000000/0x50A28BE6
     * Round  2 (R21/R22): f2/f4, constants 0x5A827999/0x5C4DD124
     * Round  3 (R31/R32): f3/f3, constants 0x6ED9EBA1/0x6D703EF3
     * Round  4 (R41/R42): f4/f2, constants 0x8F1BBCDC/0x7A6D76E9
     * Round  5 (R51/R52): f5/f1, constants 0xA953FD4E/0x00000000
     *
     * Register Rotation Pattern:
     * -------------------------
     * Variables rotate through positions: a→e, e→d, d→c, c→b, b→a
     * This is implemented by using variables in different positions each round
     * rather than actually moving data between registers.
     */

    // Phase 1: 16 rounds (f1 left, f5 right)
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

    // Phase 2: 16 rounds (f2 left, f4 right)
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

    // Phase 3: 16 rounds (f3 left, f3 right)
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

    // Phase 4: 16 rounds (f4 left, f2 right)
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

    // Phase 5: 16 rounds (f5 left, f1 right)
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
     * FINAL HASH VALUE COMBINATION
     * ============================
     * After 80 rounds, the left path (a1-e1) and right path (a2-e2) results
     * are combined with the original state to produce the final hash value.
     *
     * Standard RIPEMD-160 finalization formula:
     *   h0' = h1 + c1 + d2
     *   h1' = h2 + d1 + e2
     *   h2' = h3 + e1 + a2
     *   h3' = h4 + a1 + b2
     *   h4' = h0 + b1 + c2
     *
     * Note: The original h0 value (saved as 't') is used in the h4' calculation,
     * which is why we save it before overwriting s[0].
     *
     * This combination step ensures that both paths contribute to every output
     * word, providing the security properties of RIPEMD-160.
     */
    __m128i t = s[0];  // Save original h0 (needed for h4' calculation)
    s[0] = add3(s[1],c1,d2);  // h0' = h1 + c1 + d2
    s[1] = add3(s[2],d1,e2);  // h1' = h2 + d1 + e2
    s[2] = add3(s[3],e1,a2);  // h2' = h3 + e1 + a2
    s[3] = add3(s[4],a1,b2);  // h3' = h4 + a1 + b2
    s[4] = add3(t,b1,c2);     // h4' = h0 + b1 + c2 (uses saved h0)
  }

} // namespace ripemd160sse

/**
 * DEPACK: Extract Single Hash from Parallel State
 * ================================================
 * After computing 4 hashes in parallel, we need to extract each individual
 * 160-bit (20-byte) hash from the packed __m128i state registers.
 *
 * State Layout:
 * ------------
 * The state consists of 5 __m128i registers (s[0]-s[4]), each containing
 * 4 lanes of 32-bit values:
 *
 *   s[0] = {h0[lane3], h0[lane2], h0[lane1], h0[lane0]}
 *   s[1] = {h1[lane3], h1[lane2], h1[lane1], h1[lane0]}
 *   ...
 *   s[4] = {h4[lane3], h4[lane2], h4[lane1], h4[lane0]}
 *
 * Extraction:
 * ----------
 * DEPACK(d, i) extracts lane 'i' from all 5 state registers and writes
 * the resulting 20-byte hash to destination buffer 'd':
 *
 *   d[0:3]   = s[0][i]  (h0)
 *   d[4:7]   = s[1][i]  (h1)
 *   d[8:11]  = s[2][i]  (h2)
 *   d[12:15] = s[3][i]  (h3)
 *   d[16:19] = s[4][i]  (h4)
 *
 * Platform Differences:
 * --------------------
 * - MSVC: Direct access via .m128i_u32[i] union member
 * - GCC/Clang: Cast to uint32_t* and index (relies on aliasing)
 */
#ifdef _MSC_VER

#define DEPACK(d,i) \
((uint32_t *)d)[0] = s[0].m128i_u32[i]; \
((uint32_t *)d)[1] = s[1].m128i_u32[i]; \
((uint32_t *)d)[2] = s[2].m128i_u32[i]; \
((uint32_t *)d)[3] = s[3].m128i_u32[i]; \
((uint32_t *)d)[4] = s[4].m128i_u32[i];

#else

#define DEPACK(d,i) \
((uint32_t *)d)[0] = s0[i]; \
((uint32_t *)d)[1] = s1[i]; \
((uint32_t *)d)[2] = s2[i]; \
((uint32_t *)d)[3] = s3[i]; \
((uint32_t *)d)[4] = s4[i];

#endif

/**
 * ripemd160sse_32: Public API for 4-Way Parallel RIPEMD-160
 * ==========================================================
 * Computes RIPEMD-160 hashes for 4 independent 32-byte inputs in parallel
 * using SSE2 instructions.
 *
 * Parameters:
 * ----------
 *   i0, i1, i2, i3: Pointers to four 32-byte input buffers
 *   d0, d1, d2, d3: Pointers to four 20-byte output buffers
 *
 * Usage Example:
 * -------------
 *   unsigned char input0[32], input1[32], input2[32], input3[32];
 *   unsigned char hash0[20], hash1[20], hash2[20], hash3[20];
 *   // ... fill input buffers ...
 *   ripemd160sse_32(input0, input1, input2, input3,
 *                   hash0, hash1, hash2, hash3);
 *
 * Performance:
 * -----------
 * - ~4x faster than calling ripemd160_32() four times sequentially
 * - Optimal when you have exactly 4 inputs ready to hash
 * - If you have fewer than 4 inputs, pad with dummy data (hashes will compute but can be ignored)
 *
 * Memory Alignment:
 * ----------------
 * Input buffers do not need special alignment (LOADW handles unaligned reads).
 * Output buffers do not need special alignment (DEPACK handles unaligned writes).
 */
void ripemd160sse_32(
  const unsigned char *i0,
  const unsigned char *i1,
  const unsigned char *i2,
  const unsigned char *i3,
  unsigned char *d0,
  unsigned char *d1,
  unsigned char *d2,
  unsigned char *d3) {

  __m128i s[5];  // State: 5 hash words × 4 lanes (aligned for SSE2)
  const uint8_t *bs[] = { i0,i1,i2,i3 };  // Array of input pointers

  // Initialize state with RIPEMD-160 IV (replicated 4 times)
  ripemd160sse::Initialize(s);

  // Perform compression function on all 4 inputs in parallel
  ripemd160sse::Transform(s, bs);

#ifndef _MSC_VER
  // GCC/Clang: Extract lane pointers for DEPACK macro
  uint32_t *s0 = (uint32_t *)&s[0];
  uint32_t *s1 = (uint32_t *)&s[1];
  uint32_t *s2 = (uint32_t *)&s[2];
  uint32_t *s3 = (uint32_t *)&s[3];
  uint32_t *s4 = (uint32_t *)&s[4];
#endif

  // Extract individual hashes from packed state
  // Note: Lane order is reversed due to _mm_set_epi32() argument order
  DEPACK(d0,3);  // Extract lane 3 → output 0
  DEPACK(d1,2);  // Extract lane 2 → output 1
  DEPACK(d2,1);  // Extract lane 1 → output 2
  DEPACK(d3,0);  // Extract lane 0 → output 3

}

/**
 * ripemd160sse_test: Validation Test for SSE2 Implementation
 * ===========================================================
 * Verifies that the SSE2 parallel implementation produces identical results
 * to the scalar reference implementation.
 *
 * Test Strategy:
 * -------------
 * 1. Create 4 different test messages
 * 2. Hash each message using scalar ripemd160_32() (reference)
 * 3. Hash all 4 messages in parallel using ripemd160sse_32() (SSE2)
 * 4. Compare results - they must match exactly
 *
 * This test ensures:
 * -----------------
 * - Correct lane packing/unpacking (LOADW and DEPACK macros)
 * - Correct SIMD Boolean functions (f1-f5)
 * - Correct rotation operations (ROL macro)
 * - Correct round constants and message schedule
 * - Correct final state combination
 *
 * Expected Output:
 * ---------------
 * "RIPE() Results OK !" if all hashes match
 * "RIPEMD160() Results Wrong !" with hex dumps if any mismatch
 */
void ripemd160sse_test() {

  // Output buffers for SSE2 implementation
  unsigned char h0[20];
  unsigned char h1[20];
  unsigned char h2[20];
  unsigned char h3[20];

  // Output buffers for scalar reference implementation
  unsigned char ch0[20];
  unsigned char ch1[20];
  unsigned char ch2[20];
  unsigned char ch3[20];

  // Input message buffers (64 bytes each, but only first 32 used)
  unsigned char m0[64];
  unsigned char m1[64];
  unsigned char m2[64];
  unsigned char m3[64];

  // Prepare test messages (zero-filled, then string copied)
  memset(m0, 0, sizeof(m0));
  memset(m1, 0, sizeof(m1));
  memset(m2, 0, sizeof(m2));
  memset(m3, 0, sizeof(m3));
  strncpy((char *)m0, "This is a test message to test01", sizeof(m0) - 1);
  strncpy((char *)m1, "This is a test message to test02", sizeof(m1) - 1);
  strncpy((char *)m2, "This is a test message to test03", sizeof(m2) - 1);
  strncpy((char *)m3, "This is a test message to test04", sizeof(m3) - 1);

  // Compute reference hashes using scalar implementation
  ripemd160_32(m0, ch0);
  ripemd160_32(m1, ch1);
  ripemd160_32(m2, ch2);
  ripemd160_32(m3, ch3);

  // Compute parallel hashes using SSE2 implementation
  ripemd160sse_32(m0, m1, m2, m3, h0, h1, h2, h3);

  // Validate: SSE2 results must match scalar results exactly
  if ((ripemd160_hex(h0) != ripemd160_hex(ch0)) ||
    (ripemd160_hex(h1) != ripemd160_hex(ch1)) ||
    (ripemd160_hex(h2) != ripemd160_hex(ch2)) ||
    (ripemd160_hex(h3) != ripemd160_hex(ch3))) {

    // Test failed - print both sets of hashes for debugging
    printf("RIPEMD160() Results Wrong !\n");
    printf("RIP: %s\n", ripemd160_hex(ch0).c_str());
    printf("RIP: %s\n", ripemd160_hex(ch1).c_str());
    printf("RIP: %s\n", ripemd160_hex(ch2).c_str());
    printf("RIP: %s\n\n", ripemd160_hex(ch3).c_str());
    printf("SSE: %s\n", ripemd160_hex(h0).c_str());
    printf("SSE: %s\n", ripemd160_hex(h1).c_str());
    printf("SSE: %s\n", ripemd160_hex(h2).c_str());
    printf("SSE: %s\n\n", ripemd160_hex(h3).c_str());

  } else {
    // Test passed - SSE2 implementation is correct
    printf("RIPE() Results OK !\n");
  }

}
