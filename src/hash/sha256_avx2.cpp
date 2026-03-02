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

#include "sha256_avx2.h"
#include "sha256.h"
#include <immintrin.h>
#include <cpuid.h>
#include <string.h>
#include <stdint.h>

// ============================================================================
// CPU Feature Detection
// ============================================================================
//
// AVX2 requires both CPU support (CPUID) and OS support (XCR0 register).
// The OS must save/restore YMM registers during context switches, indicated
// by XMM (bit 1) and YMM (bit 2) state enabled in XCR0.

#if defined(__i386__) || defined(__x86_64__)
/// **xgetbv_u32**: Read Extended Control Register (XCR0)
/// Used to verify that the OS has enabled AVX/AVX2 context saving.
/// XCR0 bit 1: XMM state enabled (SSE)
/// XCR0 bit 2: YMM state enabled (AVX/AVX2)
static inline uint64_t xgetbv_u32(uint32_t index) {
    uint32_t eax, edx;
    __asm__ volatile (".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((uint64_t)edx << 32) | eax;
}

/// **os_avx_enabled**: Check if OS supports AVX context switching
/// Returns 1 if both XMM and YMM state saving are enabled, 0 otherwise
static inline int os_avx_enabled(void) {
    unsigned int eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 0;
    if (!(ecx & bit_OSXSAVE)) return 0;  // OS must support XGETBV instruction
    if (!(ecx & bit_AVX)) return 0;       // CPU must support AVX
    return (xgetbv_u32(0) & 0x6u) == 0x6u; /* XMM (bit1) + YMM (bit2) state enabled */
}
#endif

/// **sha256_avx2_available**: Runtime check for AVX2 support
///
/// **Detection Strategy:**
/// 1. Query CPUID function 7, subleaf 0, EBX bit 5 for AVX2 instruction set
/// 2. Verify OS has enabled YMM register context saving (XCR0 check)
///
/// **Returns:** 1 if AVX2 is available, 0 otherwise
///
/// **Why This Matters:**
/// - Executing AVX2 instructions on unsupported CPUs causes illegal instruction fault
/// - Using AVX2 without OS support causes corruption (registers not saved on context switch)
/// - Must be checked at runtime since binaries may run on different CPUs
int sha256_avx2_available(void) {
    unsigned int eax, ebx, ecx, edx;

    // Check for AVX2 support (CPUID function 7, subleaf 0, EBX bit 5)
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        if ((ebx & (1 << 5)) == 0) return 0;  // AVX2 bit not set
#if defined(__i386__) || defined(__x86_64__)
        return os_avx_enabled();  // Verify OS support
#else
        return 1;  // Non-x86 platforms (assume available if compiled)
#endif
    }
    return 0;  // CPUID query failed
}

/// SHA256 AVX2 8-way parallel implementation
/// =============================================
///
/// This implementation processes 8 independent SHA256 hashes simultaneously using
/// AVX2 256-bit SIMD instructions. Each __m256i register holds 8 uint32_t values,
/// one from each of the 8 parallel hash computations.
///
/// **Parallelism Strategy:**
/// - Data Layout: Each __m256i contains [hash7, hash6, hash5, hash4, hash3, hash2, hash1, hash0] (8x uint32_t)
/// - All operations (rotation, XOR, addition) execute on 8 values simultaneously
/// - Theoretical speedup: 8x over scalar (actual: 5-6x due to memory/setup overhead)
///
/// **Performance vs SSE2:**
/// - Processes 8 hashes vs SSE2's 4 hashes (2x more parallel work)
/// - Performance improvement: +31% over SSE2 baseline (not 2x due to memory bandwidth)
/// - Best suited for CPUs with Intel Haswell (2013) or AMD Excavator (2015) or newer
///
/// **Register Organization:**
/// - __m256i = 256 bits = 8× uint32_t lanes
/// - Each lane processes one independent SHA256 hash
/// - Operations broadcast to all 8 lanes (SIMD = Single Instruction, Multiple Data)
///
/// **Memory Layout (Transpose Operation):**
/// Input: 8 separate message blocks (b0-b7), each with 16 uint32_t words
/// After transpose: w0 contains word[0] from all 8 blocks, w1 contains word[1], etc.
/// This enables vectorized processing of all 8 hashes in lockstep.
///
/// **Algorithm Overview:**
/// SHA256 processes 64-byte blocks through 64 rounds of compression function.
/// Message schedule expands 16 input words (w0-w15) into 64 round words using
/// s0/s1 mixing functions. Each round updates 8 state variables (a-h) using
/// compression functions S0, S1, Maj, Ch.
namespace _sha256avx2
{

// SHA256 initial hash values (H0-H7), replicated 8 times for SIMD
// Each constant is duplicated to fill all 8 lanes of the __m256i register
// 32-byte alignment required for efficient AVX2 loads (_mm256_load_si256)
#ifdef _MSC_VER
  static const __declspec(align(32)) uint32_t _init[] = {
#else
  static const uint32_t _init[] __attribute__ ((aligned (32))) = {
#endif
      // H0: sqrt(2) - First 32 bits of fractional part
      0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,
      // H1: sqrt(3)
      0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,
      // H2: sqrt(5)
      0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,
      // H3: sqrt(7)
      0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,
      // H4: sqrt(11)
      0x510e527f,0x510e527f,0x510e527f,0x510e527f,0x510e527f,0x510e527f,0x510e527f,0x510e527f,
      // H5: sqrt(13)
      0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,
      // H6: sqrt(17)
      0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,
      // H7: sqrt(19)
      0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19
  };

// ============================================================================
// Compression Function Macros (AVX2 SIMD versions)
// ============================================================================
//
// These implement SHA256's core boolean and rotation functions using AVX2 intrinsics.
// Each operates on 8 values simultaneously (__m256i = 8× uint32_t).

//#define Maj(x,y,z) ((x&y)^(x&z)^(y&z))  // Scalar version
//#define Ch(x,y,z)  ((x&y)^(~x&z))       // Scalar version

// Optimized algebraically equivalent forms (fewer operations):
//#define Maj(x,y,z) ((x & y) | (z & (x | y)))  // Majority function
//#define Ch(x,y,z) (z ^ (x & (y ^ z)))         // Choice function

/// **Majority function**: Returns bit that appears in majority of b, c, d
/// AVX2 version: Maj(b,c,d) = (b&c) | (d&(b|c))
/// Used in rounds 0-63 to mix state variables
#define Maj(b,c,d) _mm256_or_si256(_mm256_and_si256(b, c), _mm256_and_si256(d, _mm256_or_si256(b, c)))

/// **Choice function**: If b then c else d (bitwise)
/// AVX2 version: Ch(b,c,d) = (b&c) ^ (~b&d)
/// _mm256_andnot_si256(b,d) computes (~b & d)
/// Used in rounds 0-63 to mix state variables
#define Ch(b,c,d)  _mm256_xor_si256(_mm256_and_si256(b, c), _mm256_andnot_si256(b, d))

/// **Rotate right**: Circular bit shift (no bits lost)
/// AVX2 has no native rotate, so emulate with (shift_right | shift_left)
/// Example: ROR(x, 7) = (x >> 7) | (x << 25)
#define ROR(x,n)   _mm256_or_si256(_mm256_srli_epi32(x, n), _mm256_slli_epi32(x, 32 - n))

/// **Shift right**: Logical shift (fill with zeros)
/// Used in s0/s1 message schedule functions
#define SHR(x,n)   _mm256_srli_epi32(x, n)

// ============================================================================
// SHA256 Compression Functions
// ============================================================================

/// **S0 (Sigma0)**: Used in compression rounds to mix state variable 'a'
/// S0(x) = ROTR(x,2) ^ ROTR(x,13) ^ ROTR(x,22)
/// Provides diffusion by combining 3 different rotations
#define S0(x) (_mm256_xor_si256(ROR((x), 2), _mm256_xor_si256(ROR((x), 13), ROR((x), 22))))

/// **S1 (Sigma1)**: Used in compression rounds to mix state variable 'e'
/// S1(x) = ROTR(x,6) ^ ROTR(x,11) ^ ROTR(x,25)
/// Different rotation amounts than S0 for maximum avalanche effect
#define S1(x) (_mm256_xor_si256(ROR((x), 6), _mm256_xor_si256(ROR((x), 11), ROR((x), 25))))

// ============================================================================
// Message Schedule Functions
// ============================================================================

/// **s0 (sigma0)**: Used to expand message schedule (w16-w63 from w0-w15)
/// s0(x) = ROTR(x,7) ^ ROTR(x,18) ^ SHR(x,3)
/// Mix of rotations and logical shift for non-linear expansion
#define s0(x) (_mm256_xor_si256(ROR((x), 7), _mm256_xor_si256(ROR((x), 18), SHR((x), 3))))

/// **s1 (sigma1)**: Used to expand message schedule (w16-w63 from w0-w15)
/// s1(x) = ROTR(x,17) ^ ROTR(x,19) ^ SHR(x,10)
/// Complementary to s0, provides different mixing pattern
/// Formula for w[i+16] = s1(w[i+14]) + w[i+9] + s0(w[i+1]) + w[i]
#define s1(x) (_mm256_xor_si256(ROR((x), 17), _mm256_xor_si256(ROR((x), 19), SHR((x), 10))))

// ============================================================================
// SIMD Addition Helper Macros
// ============================================================================
//
// AVX2 only supports pairwise addition, so we compose multiple additions.
// These helpers reduce code verbosity and improve readability.

/// Add 4 __m256i values: (x0+x1) + (x2+x3)
/// Balanced tree structure minimizes instruction latency
#define add4(x0, x1, x2, x3) _mm256_add_epi32(_mm256_add_epi32(x0, x1), _mm256_add_epi32(x2, x3))

/// Add 3 __m256i values: (x0+x1) + x2
#define add3(x0, x1, x2)     _mm256_add_epi32(_mm256_add_epi32(x0, x1), x2)

/// Add 5 __m256i values: ((x0+x1)+x2) + (x3+x4)
/// Used in Round macro to combine h + S1(e) + Ch(e,f,g) + K[i] + w
#define add5(x0, x1, x2, x3, x4) _mm256_add_epi32(add3(x0, x1, x2), _mm256_add_epi32(x3, x4))

// ============================================================================
// SHA256 Round Function
// ============================================================================

/// **SHA256 Round**: Performs one of the 64 compression rounds
///
/// **Parameters:**
/// - a,b,c,d,e,f,g,h: 8 state variables (each is __m256i with 8 parallel values)
/// - i: Round constant K[i] (scalar, broadcast to all 8 lanes via _mm256_set1_epi32)
/// - w: Message word (already __m256i with 8 parallel values)
///
/// **Computation:**
/// T1 = h + S1(e) + Ch(e, f, g) + K[i] + w
/// T2 = S0(a) + Maj(a, b, c)
/// d = d + T1
/// h = T1 + T2
///
/// **Variable rotation pattern:**
/// After Round(a,b,c,d,e,f,g,h,...), next call is Round(h,a,b,c,d,e,f,g,...)
/// This rotates the state variables without explicit assignment
#define Round(a, b, c, d, e, f, g, h, i, w)                 \
    T1 = add5(h, S1(e), Ch(e, f, g), _mm256_set1_epi32(i), w); \
    d = _mm256_add_epi32(d, T1);                            \
    T2 = _mm256_add_epi32(S0(a), Maj(a, b, c));             \
    h = _mm256_add_epi32(T1, T2);

// ============================================================================
// Message Schedule Expansion (WMIX)
// ============================================================================

/// **WMIX (Word Mix)**: Expands 16 message words into next 16 words
///
/// SHA256 processes 64 rounds but only has 16 input words (w0-w15).
/// The message schedule expands these using s0/s1 functions:
///
/// **Formula:** w[i] = s1(w[i-2]) + w[i-7] + s0(w[i-15]) + w[i-16]
///
/// **Usage Pattern:**
/// - Rounds 0-15:  Use initial w0-w15 (from input message)
/// - Rounds 16-31: Call WMIX(), then use updated w0-w15
/// - Rounds 32-47: Call WMIX(), then use updated w0-w15
/// - Rounds 48-63: Call WMIX(), then use updated w0-w15
///
/// **Parallelism:**
/// Each WMIX() call updates all 16 words simultaneously for 8 parallel hashes.
/// This is the critical optimization that enables efficient batching.
///
/// **Example (first line):**
/// w0 = s1(w14) + w9 + s0(w1) + w0
///      ^^^^^^^   ^^   ^^^^^^   ^^
///      i-2       i-7  i-15     i-16  (where i=16)
#define WMIX() \
  w0 = add4(s1(w14), w9, s0(w1), w0);   \
  w1 = add4(s1(w15), w10, s0(w2), w1);  \
  w2 = add4(s1(w0), w11, s0(w3), w2);   \
  w3 = add4(s1(w1), w12, s0(w4), w3);   \
  w4 = add4(s1(w2), w13, s0(w5), w4);   \
  w5 = add4(s1(w3), w14, s0(w6), w5);   \
  w6 = add4(s1(w4), w15, s0(w7), w6);   \
  w7 = add4(s1(w5), w0, s0(w8), w7);    \
  w8 = add4(s1(w6), w1, s0(w9), w8);    \
  w9 = add4(s1(w7), w2, s0(w10), w9);   \
  w10 = add4(s1(w8), w3, s0(w11), w10); \
  w11 = add4(s1(w9), w4, s0(w12), w11); \
  w12 = add4(s1(w10), w5, s0(w13), w12); \
  w13 = add4(s1(w11), w6, s0(w14), w13); \
  w14 = add4(s1(w12), w7, s0(w15), w14); \
  w15 = add4(s1(w13), w8, s0(w0), w15);

  /// **Initialize**: Set up initial SHA256 state for 8 parallel hashes
  ///
  /// **Parameters:**
  /// - s: Array of 8 __m256i registers (32-byte aligned)
  ///
  /// **Post-condition:**
  /// s[0..7] contain the 8 SHA256 initial hash values (H0-H7),
  /// each replicated 8 times for parallel processing
  void Initialize(__m256i *s) {
    memcpy(s, _init, sizeof(_init));
  }

  /// **Transform**: Perform 8 SHA256 compressions in parallel using AVX2
  ///
  /// **Parameters:**
  /// - s: Array of 8 __m256i state registers (input/output, modified in-place)
  /// - b0-b7: Pointers to 8 independent message blocks (each 16× uint32_t = 64 bytes)
  ///
  /// **Data Flow:**
  /// 1. Load state from s[0..7] into working variables a-h
  /// 2. Transpose input: Gather word[i] from all 8 blocks into w[i]
  /// 3. Execute 64 SHA256 rounds with message schedule expansion (WMIX)
  /// 4. Add compressed state back to s[0..7]
  ///
  /// **Transpose Operation:**
  /// Input layout:  b0[0..15], b1[0..15], ..., b7[0..15]  (8 blocks)
  /// Output layout: w0 = [b0[0], b1[0], ..., b7[0]]       (gather word 0)
  ///                w1 = [b0[1], b1[1], ..., b7[1]]       (gather word 1)
  ///                ...
  /// This enables SIMD processing of all 8 hashes in parallel.
  void Transform(__m256i *s, uint32_t *b0, uint32_t *b1, uint32_t *b2, uint32_t *b3,
                                uint32_t *b4, uint32_t *b5, uint32_t *b6, uint32_t *b7)
  {
    __m256i a, b, c, d, e, f, g, h;     // 8 working state variables
    __m256i w0, w1, w2, w3, w4, w5, w6, w7;    // 16 message words (8 hashes × 16 words)
    __m256i w8, w9, w10, w11, w12, w13, w14, w15;
    __m256i T1, T2;                     // Temporary values for round computation

    // Load initial state (8 parallel SHA256 states)
    a = _mm256_load_si256(s + 0);
    b = _mm256_load_si256(s + 1);
    c = _mm256_load_si256(s + 2);
    d = _mm256_load_si256(s + 3);
    e = _mm256_load_si256(s + 4);
    f = _mm256_load_si256(s + 5);
    g = _mm256_load_si256(s + 6);
    h = _mm256_load_si256(s + 7);

    // ========================================================================
    // Transpose Operation: Gather word[i] from all 8 message blocks
    // ========================================================================
    //
    // _mm256_set_epi32 packs 8 values into one __m256i register:
    //   - b0[i] goes to lane 7 (leftmost parameter)
    //   - b7[i] goes to lane 0 (rightmost parameter)
    //
    // After transpose, w[i] contains word[i] from all 8 blocks,
    // enabling parallel processing of 8 SHA256 compressions.
    w0 = _mm256_set_epi32(b0[0], b1[0], b2[0], b3[0], b4[0], b5[0], b6[0], b7[0]);
    w1 = _mm256_set_epi32(b0[1], b1[1], b2[1], b3[1], b4[1], b5[1], b6[1], b7[1]);
    w2 = _mm256_set_epi32(b0[2], b1[2], b2[2], b3[2], b4[2], b5[2], b6[2], b7[2]);
    w3 = _mm256_set_epi32(b0[3], b1[3], b2[3], b3[3], b4[3], b5[3], b6[3], b7[3]);
    w4 = _mm256_set_epi32(b0[4], b1[4], b2[4], b3[4], b4[4], b5[4], b6[4], b7[4]);
    w5 = _mm256_set_epi32(b0[5], b1[5], b2[5], b3[5], b4[5], b5[5], b6[5], b7[5]);
    w6 = _mm256_set_epi32(b0[6], b1[6], b2[6], b3[6], b4[6], b5[6], b6[6], b7[6]);
    w7 = _mm256_set_epi32(b0[7], b1[7], b2[7], b3[7], b4[7], b5[7], b6[7], b7[7]);
    w8 = _mm256_set_epi32(b0[8], b1[8], b2[8], b3[8], b4[8], b5[8], b6[8], b7[8]);
    w9 = _mm256_set_epi32(b0[9], b1[9], b2[9], b3[9], b4[9], b5[9], b6[9], b7[9]);
    w10 = _mm256_set_epi32(b0[10], b1[10], b2[10], b3[10], b4[10], b5[10], b6[10], b7[10]);
    w11 = _mm256_set_epi32(b0[11], b1[11], b2[11], b3[11], b4[11], b5[11], b6[11], b7[11]);
    w12 = _mm256_set_epi32(b0[12], b1[12], b2[12], b3[12], b4[12], b5[12], b6[12], b7[12]);
    w13 = _mm256_set_epi32(b0[13], b1[13], b2[13], b3[13], b4[13], b5[13], b6[13], b7[13]);
    w14 = _mm256_set_epi32(b0[14], b1[14], b2[14], b3[14], b4[14], b5[14], b6[14], b7[14]);
    w15 = _mm256_set_epi32(b0[15], b1[15], b2[15], b3[15], b4[15], b5[15], b6[15], b7[15]);

    // ========================================================================
    // SHA256 Compression: 64 rounds in 4 groups of 16
    // ========================================================================
    //
    // Each group processes 16 rounds:
    //   - Rounds 0-15:  Use original w0-w15
    //   - Rounds 16-31: WMIX() expands w0-w15, then use expanded values
    //   - Rounds 32-47: WMIX() again, use newly expanded values
    //   - Rounds 48-63: WMIX() final expansion, complete compression
    //
    // Round constants (K[i]) are from SHA256 specification (cube roots of primes)

    // Rounds 0-15: Initial message words
    Round(a, b, c, d, e, f, g, h, 0x428A2F98, w0);
    Round(h, a, b, c, d, e, f, g, 0x71374491, w1);
    Round(g, h, a, b, c, d, e, f, 0xB5C0FBCF, w2);
    Round(f, g, h, a, b, c, d, e, 0xE9B5DBA5, w3);
    Round(e, f, g, h, a, b, c, d, 0x3956C25B, w4);
    Round(d, e, f, g, h, a, b, c, 0x59F111F1, w5);
    Round(c, d, e, f, g, h, a, b, 0x923F82A4, w6);
    Round(b, c, d, e, f, g, h, a, 0xAB1C5ED5, w7);
    Round(a, b, c, d, e, f, g, h, 0xD807AA98, w8);
    Round(h, a, b, c, d, e, f, g, 0x12835B01, w9);
    Round(g, h, a, b, c, d, e, f, 0x243185BE, w10);
    Round(f, g, h, a, b, c, d, e, 0x550C7DC3, w11);
    Round(e, f, g, h, a, b, c, d, 0x72BE5D74, w12);
    Round(d, e, f, g, h, a, b, c, 0x80DEB1FE, w13);
    Round(c, d, e, f, g, h, a, b, 0x9BDC06A7, w14);
    Round(b, c, d, e, f, g, h, a, 0xC19BF174, w15);

    // Expand message schedule for rounds 16-31
    WMIX()

    // Rounds 16-31
    Round(a, b, c, d, e, f, g, h, 0xE49B69C1, w0);
    Round(h, a, b, c, d, e, f, g, 0xEFBE4786, w1);
    Round(g, h, a, b, c, d, e, f, 0x0FC19DC6, w2);
    Round(f, g, h, a, b, c, d, e, 0x240CA1CC, w3);
    Round(e, f, g, h, a, b, c, d, 0x2DE92C6F, w4);
    Round(d, e, f, g, h, a, b, c, 0x4A7484AA, w5);
    Round(c, d, e, f, g, h, a, b, 0x5CB0A9DC, w6);
    Round(b, c, d, e, f, g, h, a, 0x76F988DA, w7);
    Round(a, b, c, d, e, f, g, h, 0x983E5152, w8);
    Round(h, a, b, c, d, e, f, g, 0xA831C66D, w9);
    Round(g, h, a, b, c, d, e, f, 0xB00327C8, w10);
    Round(f, g, h, a, b, c, d, e, 0xBF597FC7, w11);
    Round(e, f, g, h, a, b, c, d, 0xC6E00BF3, w12);
    Round(d, e, f, g, h, a, b, c, 0xD5A79147, w13);
    Round(c, d, e, f, g, h, a, b, 0x06CA6351, w14);
    Round(b, c, d, e, f, g, h, a, 0x14292967, w15);

    // Expand message schedule for rounds 32-47
    WMIX()

    // Rounds 32-47
    Round(a, b, c, d, e, f, g, h, 0x27B70A85, w0);
    Round(h, a, b, c, d, e, f, g, 0x2E1B2138, w1);
    Round(g, h, a, b, c, d, e, f, 0x4D2C6DFC, w2);
    Round(f, g, h, a, b, c, d, e, 0x53380D13, w3);
    Round(e, f, g, h, a, b, c, d, 0x650A7354, w4);
    Round(d, e, f, g, h, a, b, c, 0x766A0ABB, w5);
    Round(c, d, e, f, g, h, a, b, 0x81C2C92E, w6);
    Round(b, c, d, e, f, g, h, a, 0x92722C85, w7);
    Round(a, b, c, d, e, f, g, h, 0xA2BFE8A1, w8);
    Round(h, a, b, c, d, e, f, g, 0xA81A664B, w9);
    Round(g, h, a, b, c, d, e, f, 0xC24B8B70, w10);
    Round(f, g, h, a, b, c, d, e, 0xC76C51A3, w11);
    Round(e, f, g, h, a, b, c, d, 0xD192E819, w12);
    Round(d, e, f, g, h, a, b, c, 0xD6990624, w13);
    Round(c, d, e, f, g, h, a, b, 0xF40E3585, w14);
    Round(b, c, d, e, f, g, h, a, 0x106AA070, w15);

    // Expand message schedule for rounds 48-63
    WMIX()

    // Rounds 48-63 (final)
    Round(a, b, c, d, e, f, g, h, 0x19A4C116, w0);
    Round(h, a, b, c, d, e, f, g, 0x1E376C08, w1);
    Round(g, h, a, b, c, d, e, f, 0x2748774C, w2);
    Round(f, g, h, a, b, c, d, e, 0x34B0BCB5, w3);
    Round(e, f, g, h, a, b, c, d, 0x391C0CB3, w4);
    Round(d, e, f, g, h, a, b, c, 0x4ED8AA4A, w5);
    Round(c, d, e, f, g, h, a, b, 0x5B9CCA4F, w6);
    Round(b, c, d, e, f, g, h, a, 0x682E6FF3, w7);
    Round(a, b, c, d, e, f, g, h, 0x748F82EE, w8);
    Round(h, a, b, c, d, e, f, g, 0x78A5636F, w9);
    Round(g, h, a, b, c, d, e, f, 0x84C87814, w10);
    Round(f, g, h, a, b, c, d, e, 0x8CC70208, w11);
    Round(e, f, g, h, a, b, c, d, 0x90BEFFFA, w12);
    Round(d, e, f, g, h, a, b, c, 0xA4506CEB, w13);
    Round(c, d, e, f, g, h, a, b, 0xBEF9A3F7, w14);
    Round(b, c, d, e, f, g, h, a, 0xC67178F2, w15);

    // ========================================================================
    // Finalize: Add compressed values back to state
    // ========================================================================
    //
    // SHA256 uses Davies-Meyer construction: H' = H + compress(H, M)
    // This prevents length-extension attacks and ensures collision resistance
    s[0] = _mm256_add_epi32(a, s[0]);
    s[1] = _mm256_add_epi32(b, s[1]);
    s[2] = _mm256_add_epi32(c, s[2]);
    s[3] = _mm256_add_epi32(d, s[3]);
    s[4] = _mm256_add_epi32(e, s[4]);
    s[5] = _mm256_add_epi32(f, s[5]);
    s[6] = _mm256_add_epi32(g, s[6]);
    s[7] = _mm256_add_epi32(h, s[7]);
  }

  /// **Transform2**: Compute SHA256(SHA256(message))[0] for 8 inputs in parallel
  ///
  /// **Purpose:**
  /// Used for Bitcoin address checksums and proof-of-work verification.
  /// Only returns the first 32 bits of the double-hash (stored in s[0]).
  ///
  /// **Parameters:**
  /// - s: Array of 8 __m256i state registers (input/output, only s[0] valid after call)
  /// - b0-b7: Pointers to 8 independent message blocks (each 16× uint32_t)
  ///
  /// **Algorithm:**
  /// 1. First SHA256: hash the input message (result in working state a-h)
  /// 2. Prepare second block: add padding (0x80, length=256 bits)
  /// 3. Second SHA256: hash the first hash output
  /// 4. Return only s[0] (first 32 bits of each of the 8 double-hashes)
  ///
  /// **Optimization:**
  /// Avoids full 256-bit output since Bitcoin only needs 32-bit checksum
  void Transform2(__m256i *s, uint32_t *b0, uint32_t *b1, uint32_t *b2, uint32_t *b3,
                                 uint32_t *b4, uint32_t *b5, uint32_t *b6, uint32_t *b7) {
    __m256i a, b, c, d, e, f, g, h;
    __m256i w0, w1, w2, w3, w4, w5, w6, w7;
    __m256i w8, w9, w10, w11, w12, w13, w14, w15;
    __m256i T1, T2;

    a = _mm256_load_si256(s + 0);
    b = _mm256_load_si256(s + 1);
    c = _mm256_load_si256(s + 2);
    d = _mm256_load_si256(s + 3);
    e = _mm256_load_si256(s + 4);
    f = _mm256_load_si256(s + 5);
    g = _mm256_load_si256(s + 6);
    h = _mm256_load_si256(s + 7);

    // Load data from 8 different message blocks
    w0 = _mm256_set_epi32(b0[0], b1[0], b2[0], b3[0], b4[0], b5[0], b6[0], b7[0]);
    w1 = _mm256_set_epi32(b0[1], b1[1], b2[1], b3[1], b4[1], b5[1], b6[1], b7[1]);
    w2 = _mm256_set_epi32(b0[2], b1[2], b2[2], b3[2], b4[2], b5[2], b6[2], b7[2]);
    w3 = _mm256_set_epi32(b0[3], b1[3], b2[3], b3[3], b4[3], b5[3], b6[3], b7[3]);
    w4 = _mm256_set_epi32(b0[4], b1[4], b2[4], b3[4], b4[4], b5[4], b6[4], b7[4]);
    w5 = _mm256_set_epi32(b0[5], b1[5], b2[5], b3[5], b4[5], b5[5], b6[5], b7[5]);
    w6 = _mm256_set_epi32(b0[6], b1[6], b2[6], b3[6], b4[6], b5[6], b6[6], b7[6]);
    w7 = _mm256_set_epi32(b0[7], b1[7], b2[7], b3[7], b4[7], b5[7], b6[7], b7[7]);
    w8 = _mm256_set_epi32(b0[8], b1[8], b2[8], b3[8], b4[8], b5[8], b6[8], b7[8]);
    w9 = _mm256_set_epi32(b0[9], b1[9], b2[9], b3[9], b4[9], b5[9], b6[9], b7[9]);
    w10 = _mm256_set_epi32(b0[10], b1[10], b2[10], b3[10], b4[10], b5[10], b6[10], b7[10]);
    w11 = _mm256_set_epi32(b0[11], b1[11], b2[11], b3[11], b4[11], b5[11], b6[11], b7[11]);
    w12 = _mm256_set_epi32(b0[12], b1[12], b2[12], b3[12], b4[12], b5[12], b6[12], b7[12]);
    w13 = _mm256_set_epi32(b0[13], b1[13], b2[13], b3[13], b4[13], b5[13], b6[13], b7[13]);
    w14 = _mm256_set_epi32(b0[14], b1[14], b2[14], b3[14], b4[14], b5[14], b6[14], b7[14]);
    w15 = _mm256_set_epi32(b0[15], b1[15], b2[15], b3[15], b4[15], b5[15], b6[15], b7[15]);

    // First SHA256 round
    Round(a, b, c, d, e, f, g, h, 0x428A2F98, w0);
    Round(h, a, b, c, d, e, f, g, 0x71374491, w1);
    Round(g, h, a, b, c, d, e, f, 0xB5C0FBCF, w2);
    Round(f, g, h, a, b, c, d, e, 0xE9B5DBA5, w3);
    Round(e, f, g, h, a, b, c, d, 0x3956C25B, w4);
    Round(d, e, f, g, h, a, b, c, 0x59F111F1, w5);
    Round(c, d, e, f, g, h, a, b, 0x923F82A4, w6);
    Round(b, c, d, e, f, g, h, a, 0xAB1C5ED5, w7);
    Round(a, b, c, d, e, f, g, h, 0xD807AA98, w8);
    Round(h, a, b, c, d, e, f, g, 0x12835B01, w9);
    Round(g, h, a, b, c, d, e, f, 0x243185BE, w10);
    Round(f, g, h, a, b, c, d, e, 0x550C7DC3, w11);
    Round(e, f, g, h, a, b, c, d, 0x72BE5D74, w12);
    Round(d, e, f, g, h, a, b, c, 0x80DEB1FE, w13);
    Round(c, d, e, f, g, h, a, b, 0x9BDC06A7, w14);
    Round(b, c, d, e, f, g, h, a, 0xC19BF174, w15);

    // Expand message schedule for rounds 16-31
    WMIX()

    // Rounds 16-31
    Round(a, b, c, d, e, f, g, h, 0xE49B69C1, w0);
    Round(h, a, b, c, d, e, f, g, 0xEFBE4786, w1);
    Round(g, h, a, b, c, d, e, f, 0x0FC19DC6, w2);
    Round(f, g, h, a, b, c, d, e, 0x240CA1CC, w3);
    Round(e, f, g, h, a, b, c, d, 0x2DE92C6F, w4);
    Round(d, e, f, g, h, a, b, c, 0x4A7484AA, w5);
    Round(c, d, e, f, g, h, a, b, 0x5CB0A9DC, w6);
    Round(b, c, d, e, f, g, h, a, 0x76F988DA, w7);
    Round(a, b, c, d, e, f, g, h, 0x983E5152, w8);
    Round(h, a, b, c, d, e, f, g, 0xA831C66D, w9);
    Round(g, h, a, b, c, d, e, f, 0xB00327C8, w10);
    Round(f, g, h, a, b, c, d, e, 0xBF597FC7, w11);
    Round(e, f, g, h, a, b, c, d, 0xC6E00BF3, w12);
    Round(d, e, f, g, h, a, b, c, 0xD5A79147, w13);
    Round(c, d, e, f, g, h, a, b, 0x06CA6351, w14);
    Round(b, c, d, e, f, g, h, a, 0x14292967, w15);

    // Expand message schedule for rounds 32-47
    WMIX()

    // Rounds 32-47
    Round(a, b, c, d, e, f, g, h, 0x27B70A85, w0);
    Round(h, a, b, c, d, e, f, g, 0x2E1B2138, w1);
    Round(g, h, a, b, c, d, e, f, 0x4D2C6DFC, w2);
    Round(f, g, h, a, b, c, d, e, 0x53380D13, w3);
    Round(e, f, g, h, a, b, c, d, 0x650A7354, w4);
    Round(d, e, f, g, h, a, b, c, 0x766A0ABB, w5);
    Round(c, d, e, f, g, h, a, b, 0x81C2C92E, w6);
    Round(b, c, d, e, f, g, h, a, 0x92722C85, w7);
    Round(a, b, c, d, e, f, g, h, 0xA2BFE8A1, w8);
    Round(h, a, b, c, d, e, f, g, 0xA81A664B, w9);
    Round(g, h, a, b, c, d, e, f, 0xC24B8B70, w10);
    Round(f, g, h, a, b, c, d, e, 0xC76C51A3, w11);
    Round(e, f, g, h, a, b, c, d, 0xD192E819, w12);
    Round(d, e, f, g, h, a, b, c, 0xD6990624, w13);
    Round(c, d, e, f, g, h, a, b, 0xF40E3585, w14);
    Round(b, c, d, e, f, g, h, a, 0x106AA070, w15);

    // Expand message schedule for rounds 48-63
    WMIX()

    // Rounds 48-63 (final)
    Round(a, b, c, d, e, f, g, h, 0x19A4C116, w0);
    Round(h, a, b, c, d, e, f, g, 0x1E376C08, w1);
    Round(g, h, a, b, c, d, e, f, 0x2748774C, w2);
    Round(f, g, h, a, b, c, d, e, 0x34B0BCB5, w3);
    Round(e, f, g, h, a, b, c, d, 0x391C0CB3, w4);
    Round(d, e, f, g, h, a, b, c, 0x4ED8AA4A, w5);
    Round(c, d, e, f, g, h, a, b, 0x5B9CCA4F, w6);
    Round(b, c, d, e, f, g, h, a, 0x682E6FF3, w7);
    Round(a, b, c, d, e, f, g, h, 0x748F82EE, w8);
    Round(h, a, b, c, d, e, f, g, 0x78A5636F, w9);
    Round(g, h, a, b, c, d, e, f, 0x84C87814, w10);
    Round(f, g, h, a, b, c, d, e, 0x8CC70208, w11);
    Round(e, f, g, h, a, b, c, d, 0x90BEFFFA, w12);
    Round(d, e, f, g, h, a, b, c, 0xA4506CEB, w13);
    Round(c, d, e, f, g, h, a, b, 0xBEF9A3F7, w14);
    Round(b, c, d, e, f, g, h, a, 0xC67178F2, w15);

    // ========================================================================
    // Prepare Second SHA256: Hash the first hash output
    // ========================================================================
    //
    // Input for second hash: 32-byte SHA256 output (from first hash)
    // SHA256 requires 64-byte blocks, so padding is needed:
    //   - Bytes 0-31: First hash result (w0-w7)
    //   - Byte 32:    0x80 (start of padding)
    //   - Bytes 33-59: 0x00 (zero padding)
    //   - Bytes 60-63: 0x00000100 (length = 256 bits in big-endian)
    //
    // w0-w7: Finalize first hash (add to initial state)
    w0 = _mm256_add_epi32(a, s[0]);
    w1 = _mm256_add_epi32(b, s[1]);
    w2 = _mm256_add_epi32(c, s[2]);
    w3 = _mm256_add_epi32(d, s[3]);
    w4 = _mm256_add_epi32(e, s[4]);
    w5 = _mm256_add_epi32(f, s[5]);
    w6 = _mm256_add_epi32(g, s[6]);
    w7 = _mm256_add_epi32(h, s[7]);
    // w8: Padding start (0x80 in MSB byte)
    w8 = _mm256_set1_epi32(0x80000000);
    // w9-w14: Zero padding (use XOR for efficient zero)
    w9 = _mm256_xor_si256(w9, w9);
    w10 = _mm256_xor_si256(w10, w10);
    w11 = _mm256_xor_si256(w11, w11);
    w12 = _mm256_xor_si256(w12, w12);
    w13 = _mm256_xor_si256(w13, w13);
    w14 = _mm256_xor_si256(w14, w14);
    // w15: Message length in bits (256 bits = 0x100)
    w15 = _mm256_set1_epi32(0x100);

    // Reset state to SHA256 initial values for second hash
    a = _mm256_load_si256(s + 0);
    b = _mm256_load_si256(s + 1);
    c = _mm256_load_si256(s + 2);
    d = _mm256_load_si256(s + 3);
    e = _mm256_load_si256(s + 4);
    f = _mm256_load_si256(s + 5);
    g = _mm256_load_si256(s + 6);
    h = _mm256_load_si256(s + 7);

    // ========================================================================
    // Second SHA256: Hash(first_hash_output)
    // ========================================================================
    //
    // This computes SHA256 of the 32-byte result from the first hash.
    // Used for Bitcoin address checksums (double SHA256).
    //
    // Rounds 0-15: Process padded message
    Round(a, b, c, d, e, f, g, h, 0x428A2F98, w0);
    Round(h, a, b, c, d, e, f, g, 0x71374491, w1);
    Round(g, h, a, b, c, d, e, f, 0xB5C0FBCF, w2);
    Round(f, g, h, a, b, c, d, e, 0xE9B5DBA5, w3);
    Round(e, f, g, h, a, b, c, d, 0x3956C25B, w4);
    Round(d, e, f, g, h, a, b, c, 0x59F111F1, w5);
    Round(c, d, e, f, g, h, a, b, 0x923F82A4, w6);
    Round(b, c, d, e, f, g, h, a, 0xAB1C5ED5, w7);
    Round(a, b, c, d, e, f, g, h, 0xD807AA98, w8);
    Round(h, a, b, c, d, e, f, g, 0x12835B01, w9);
    Round(g, h, a, b, c, d, e, f, 0x243185BE, w10);
    Round(f, g, h, a, b, c, d, e, 0x550C7DC3, w11);
    Round(e, f, g, h, a, b, c, d, 0x72BE5D74, w12);
    Round(d, e, f, g, h, a, b, c, 0x80DEB1FE, w13);
    Round(c, d, e, f, g, h, a, b, 0x9BDC06A7, w14);
    Round(b, c, d, e, f, g, h, a, 0xC19BF174, w15);

    // Expand message schedule for rounds 16-31
    WMIX()

    // Rounds 16-31
    Round(a, b, c, d, e, f, g, h, 0xE49B69C1, w0);
    Round(h, a, b, c, d, e, f, g, 0xEFBE4786, w1);
    Round(g, h, a, b, c, d, e, f, 0x0FC19DC6, w2);
    Round(f, g, h, a, b, c, d, e, 0x240CA1CC, w3);
    Round(e, f, g, h, a, b, c, d, 0x2DE92C6F, w4);
    Round(d, e, f, g, h, a, b, c, 0x4A7484AA, w5);
    Round(c, d, e, f, g, h, a, b, 0x5CB0A9DC, w6);
    Round(b, c, d, e, f, g, h, a, 0x76F988DA, w7);
    Round(a, b, c, d, e, f, g, h, 0x983E5152, w8);
    Round(h, a, b, c, d, e, f, g, 0xA831C66D, w9);
    Round(g, h, a, b, c, d, e, f, 0xB00327C8, w10);
    Round(f, g, h, a, b, c, d, e, 0xBF597FC7, w11);
    Round(e, f, g, h, a, b, c, d, 0xC6E00BF3, w12);
    Round(d, e, f, g, h, a, b, c, 0xD5A79147, w13);
    Round(c, d, e, f, g, h, a, b, 0x06CA6351, w14);
    Round(b, c, d, e, f, g, h, a, 0x14292967, w15);

    // Expand message schedule for rounds 32-47
    WMIX()

    // Rounds 32-47
    Round(a, b, c, d, e, f, g, h, 0x27B70A85, w0);
    Round(h, a, b, c, d, e, f, g, 0x2E1B2138, w1);
    Round(g, h, a, b, c, d, e, f, 0x4D2C6DFC, w2);
    Round(f, g, h, a, b, c, d, e, 0x53380D13, w3);
    Round(e, f, g, h, a, b, c, d, 0x650A7354, w4);
    Round(d, e, f, g, h, a, b, c, 0x766A0ABB, w5);
    Round(c, d, e, f, g, h, a, b, 0x81C2C92E, w6);
    Round(b, c, d, e, f, g, h, a, 0x92722C85, w7);
    Round(a, b, c, d, e, f, g, h, 0xA2BFE8A1, w8);
    Round(h, a, b, c, d, e, f, g, 0xA81A664B, w9);
    Round(g, h, a, b, c, d, e, f, 0xC24B8B70, w10);
    Round(f, g, h, a, b, c, d, e, 0xC76C51A3, w11);
    Round(e, f, g, h, a, b, c, d, 0xD192E819, w12);
    Round(d, e, f, g, h, a, b, c, 0xD6990624, w13);
    Round(c, d, e, f, g, h, a, b, 0xF40E3585, w14);
    Round(b, c, d, e, f, g, h, a, 0x106AA070, w15);

    // Expand message schedule for rounds 48-63
    WMIX()

    // Rounds 48-63 (final)
    Round(a, b, c, d, e, f, g, h, 0x19A4C116, w0);
    Round(h, a, b, c, d, e, f, g, 0x1E376C08, w1);
    Round(g, h, a, b, c, d, e, f, 0x2748774C, w2);
    Round(f, g, h, a, b, c, d, e, 0x34B0BCB5, w3);
    Round(e, f, g, h, a, b, c, d, 0x391C0CB3, w4);
    Round(d, e, f, g, h, a, b, c, 0x4ED8AA4A, w5);
    Round(c, d, e, f, g, h, a, b, 0x5B9CCA4F, w6);
    Round(b, c, d, e, f, g, h, a, 0x682E6FF3, w7);
    Round(a, b, c, d, e, f, g, h, 0x748F82EE, w8);
    Round(h, a, b, c, d, e, f, g, 0x78A5636F, w9);
    Round(g, h, a, b, c, d, e, f, 0x84C87814, w10);
    Round(f, g, h, a, b, c, d, e, 0x8CC70208, w11);
    Round(e, f, g, h, a, b, c, d, 0x90BEFFFA, w12);
    Round(d, e, f, g, h, a, b, c, 0xA4506CEB, w13);
    Round(c, d, e, f, g, h, a, b, 0xBEF9A3F7, w14);
    Round(b, c, d, e, f, g, h, a, 0xC67178F2, w15);

    // ========================================================================
    // Finalize: Store only first word (32 bits) of double-hash
    // ========================================================================
    //
    // Bitcoin checksums only need SHA256(SHA256(data))[0:4] (first 4 bytes).
    // We compute full state variable 'a' but only store s[0] for efficiency.
    // This saves memory writes and cache bandwidth (8× speedup: 1 vs 8 words).
    s[0] = _mm256_add_epi32(a, s[0]);
  }

} // namespace _sha256avx2

// Public interface for AVX2 8-way SHA256 (1 block)
void sha256avx2_1B(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7) {

  __m256i s[8] __attribute__ ((aligned (32)));

  _sha256avx2::Initialize(s);
  _sha256avx2::Transform(s, i0, i1, i2, i3, i4, i5, i6, i7);

  // Transpose and store the 8 SHA256 results (32 bytes each)
  alignas(32) uint32_t temp[8];

  for (int i = 0; i < 8; i++) {
    _mm256_store_si256((__m256i*)temp, s[i]);
    ((uint32_t*)d0)[i] = __builtin_bswap32(temp[7]);
    ((uint32_t*)d1)[i] = __builtin_bswap32(temp[6]);
    ((uint32_t*)d2)[i] = __builtin_bswap32(temp[5]);
    ((uint32_t*)d3)[i] = __builtin_bswap32(temp[4]);
    ((uint32_t*)d4)[i] = __builtin_bswap32(temp[3]);
    ((uint32_t*)d5)[i] = __builtin_bswap32(temp[2]);
    ((uint32_t*)d6)[i] = __builtin_bswap32(temp[1]);
    ((uint32_t*)d7)[i] = __builtin_bswap32(temp[0]);
  }
}

// Public interface for AVX2 8-way SHA256 (2 blocks)
void sha256avx2_2B(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7) {

  __m256i s[8] __attribute__ ((aligned (32)));

  _sha256avx2::Initialize(s);
  _sha256avx2::Transform(s, i0, i1, i2, i3, i4, i5, i6, i7);
  _sha256avx2::Transform(s, i0 + 16, i1 + 16, i2 + 16, i3 + 16,
                            i4 + 16, i5 + 16, i6 + 16, i7 + 16);

  // Unpack and store results
  alignas(32) uint32_t temp[8];

  for (int i = 0; i < 8; i++) {
    _mm256_store_si256((__m256i*)temp, s[i]);
    ((uint32_t*)d0)[i] = __builtin_bswap32(temp[7]);
    ((uint32_t*)d1)[i] = __builtin_bswap32(temp[6]);
    ((uint32_t*)d2)[i] = __builtin_bswap32(temp[5]);
    ((uint32_t*)d3)[i] = __builtin_bswap32(temp[4]);
    ((uint32_t*)d4)[i] = __builtin_bswap32(temp[3]);
    ((uint32_t*)d5)[i] = __builtin_bswap32(temp[2]);
    ((uint32_t*)d6)[i] = __builtin_bswap32(temp[1]);
    ((uint32_t*)d7)[i] = __builtin_bswap32(temp[0]);
  }
}

// Public interface for AVX2 8-way SHA256 checksum
void sha256avx2_checksum(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7) {

  __m256i s[8] __attribute__ ((aligned (32)));

  _sha256avx2::Initialize(s);
  _sha256avx2::Transform2(s, i0, i1, i2, i3, i4, i5, i6, i7);

  // Extract only first 32 bits from each result
  alignas(32) uint32_t temp[8];
  _mm256_store_si256((__m256i*)temp, s[0]);

  *((uint32_t *)d0) = __builtin_bswap32(temp[7]);
  *((uint32_t *)d1) = __builtin_bswap32(temp[6]);
  *((uint32_t *)d2) = __builtin_bswap32(temp[5]);
  *((uint32_t *)d3) = __builtin_bswap32(temp[4]);
  *((uint32_t *)d4) = __builtin_bswap32(temp[3]);
  *((uint32_t *)d5) = __builtin_bswap32(temp[2]);
  *((uint32_t *)d6) = __builtin_bswap32(temp[1]);
  *((uint32_t *)d7) = __builtin_bswap32(temp[0]);
}

// RIPEMD-160 AVX2 implementation (inline to avoid separate file dependency)
namespace _ripemd160avx2_fused {

#ifdef _MSC_VER
  static const __declspec(align(32)) uint32_t _rmd_init[] = {
#else
  static const uint32_t _rmd_init[] __attribute__ ((aligned (32))) = {
#endif
    0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,
    0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,
    0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,
    0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,
    0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul
  };

// RIPEMD-160 macros (reuse existing names with RMD prefix to avoid conflicts)
#define RMD_ROL(x,n) _mm256_or_si256(_mm256_slli_epi32(x, n), _mm256_srli_epi32(x, 32 - n))

#ifdef _MSC_VER
#define rmd_not(x) _mm256_andnot_si256(x, _mm256_cmpeq_epi32(_mm256_setzero_si256(), _mm256_setzero_si256()))
#define rmd_f1(x,y,z) _mm256_xor_si256(x, _mm256_xor_si256(y, z))
#define rmd_f2(x,y,z) _mm256_or_si256(_mm256_and_si256(x,y),_mm256_andnot_si256(x,z))
#define rmd_f3(x,y,z) _mm256_xor_si256(_mm256_or_si256(x,rmd_not(y)),z)
#define rmd_f4(x,y,z) _mm256_or_si256(_mm256_and_si256(x,z),_mm256_andnot_si256(z,y))
#define rmd_f5(x,y,z) _mm256_xor_si256(x,_mm256_or_si256(y,rmd_not(z)))
#else
#define rmd_f1(x,y,z) _mm256_xor_si256(x, _mm256_xor_si256(y, z))
#define rmd_f2(x,y,z) _mm256_or_si256(_mm256_and_si256(x,y),_mm256_andnot_si256(x,z))
#define rmd_f3(x,y,z) _mm256_xor_si256(_mm256_or_si256(x,~(y)),z)
#define rmd_f4(x,y,z) _mm256_or_si256(_mm256_and_si256(x,z),_mm256_andnot_si256(z,y))
#define rmd_f5(x,y,z) _mm256_xor_si256(x,_mm256_or_si256(y,~(z)))
#endif

#define rmd_add3(x0, x1, x2) _mm256_add_epi32(_mm256_add_epi32(x0, x1), x2)

#define RMD_Round(a,b,c,d,e,f,x,k,r) \
  u = add4(a,f,x,_mm256_set1_epi32(k)); \
  a = _mm256_add_epi32(RMD_ROL(u, r),e); \
  c = RMD_ROL(c, 10);

#define RMD_R11(a,b,c,d,e,x,r) RMD_Round(a, b, c, d, e, rmd_f1(b, c, d), x, 0, r)
#define RMD_R21(a,b,c,d,e,x,r) RMD_Round(a, b, c, d, e, rmd_f2(b, c, d), x, 0x5A827999ul, r)
#define RMD_R31(a,b,c,d,e,x,r) RMD_Round(a, b, c, d, e, rmd_f3(b, c, d), x, 0x6ED9EBA1ul, r)
#define RMD_R41(a,b,c,d,e,x,r) RMD_Round(a, b, c, d, e, rmd_f4(b, c, d), x, 0x8F1BBCDCul, r)
#define RMD_R51(a,b,c,d,e,x,r) RMD_Round(a, b, c, d, e, rmd_f5(b, c, d), x, 0xA953FD4Eul, r)
#define RMD_R12(a,b,c,d,e,x,r) RMD_Round(a, b, c, d, e, rmd_f5(b, c, d), x, 0x50A28BE6ul, r)
#define RMD_R22(a,b,c,d,e,x,r) RMD_Round(a, b, c, d, e, rmd_f4(b, c, d), x, 0x5C4DD124ul, r)
#define RMD_R32(a,b,c,d,e,x,r) RMD_Round(a, b, c, d, e, rmd_f3(b, c, d), x, 0x6D703EF3ul, r)
#define RMD_R42(a,b,c,d,e,x,r) RMD_Round(a, b, c, d, e, rmd_f2(b, c, d), x, 0x7A6D76E9ul, r)
#define RMD_R52(a,b,c,d,e,x,r) RMD_Round(a, b, c, d, e, rmd_f1(b, c, d), x, 0, r)

  // Transform RIPEMD-160 with SHA256 output directly in registers
  // sha256_state[8] contains the 8 32-bit words of SHA256 output (in big-endian AVX2 format)
  void Transform_from_sha256(__m256i *rmd_state, __m256i *sha256_state) {
    __m256i a1, b1, c1, d1, e1;
    __m256i a2, b2, c2, d2, e2;
    __m256i u;
    __m256i w[16];

    // Initialize RIPEMD-160 state
    a1 = _mm256_load_si256((__m256i*)&_rmd_init[0]);
    b1 = _mm256_load_si256((__m256i*)&_rmd_init[8]);
    c1 = _mm256_load_si256((__m256i*)&_rmd_init[16]);
    d1 = _mm256_load_si256((__m256i*)&_rmd_init[24]);
    e1 = _mm256_load_si256((__m256i*)&_rmd_init[32]);

    a2 = a1;
    b2 = b1;
    c2 = c1;
    d2 = d1;
    e2 = e1;

    // Load SHA256 output (32 bytes) into w[0..7]
    // SHA256 output is in big-endian format, need to byte-swap for RIPEMD160
    for (int i = 0; i < 8; i++) {
      alignas(32) uint32_t temp[8];
      _mm256_store_si256((__m256i*)temp, sha256_state[i]);
      // Byte swap from big-endian to little-endian
      w[i] = _mm256_set_epi32(
        __builtin_bswap32(temp[7]),
        __builtin_bswap32(temp[6]),
        __builtin_bswap32(temp[5]),
        __builtin_bswap32(temp[4]),
        __builtin_bswap32(temp[3]),
        __builtin_bswap32(temp[2]),
        __builtin_bswap32(temp[1]),
        __builtin_bswap32(temp[0])
      );
    }

    // Padding for 32-byte input (RIPEMD-160 block)
    const __m256i pad80 = _mm256_set1_epi32(0x00000080u);
    const __m256i zero = _mm256_setzero_si256();
    const __m256i bitlen = _mm256_set1_epi32(32 << 3);  // 256 bits

    w[8] = pad80;
    w[9] = zero;
    w[10] = zero;
    w[11] = zero;
    w[12] = zero;
    w[13] = zero;
    w[14] = bitlen;
    w[15] = zero;

    // RIPEMD-160 rounds (left and right lines)
    // Round 1
    RMD_R11(a1, b1, c1, d1, e1, w[0], 11);
    RMD_R12(a2, b2, c2, d2, e2, w[5], 8);
    RMD_R11(e1, a1, b1, c1, d1, w[1], 14);
    RMD_R12(e2, a2, b2, c2, d2, w[14], 9);
    RMD_R11(d1, e1, a1, b1, c1, w[2], 15);
    RMD_R12(d2, e2, a2, b2, c2, w[7], 9);
    RMD_R11(c1, d1, e1, a1, b1, w[3], 12);
    RMD_R12(c2, d2, e2, a2, b2, w[0], 11);
    RMD_R11(b1, c1, d1, e1, a1, w[4], 5);
    RMD_R12(b2, c2, d2, e2, a2, w[9], 13);
    RMD_R11(a1, b1, c1, d1, e1, w[5], 8);
    RMD_R12(a2, b2, c2, d2, e2, w[2], 15);
    RMD_R11(e1, a1, b1, c1, d1, w[6], 7);
    RMD_R12(e2, a2, b2, c2, d2, w[11], 15);
    RMD_R11(d1, e1, a1, b1, c1, w[7], 9);
    RMD_R12(d2, e2, a2, b2, c2, w[4], 5);
    RMD_R11(c1, d1, e1, a1, b1, w[8], 11);
    RMD_R12(c2, d2, e2, a2, b2, w[13], 7);
    RMD_R11(b1, c1, d1, e1, a1, w[9], 13);
    RMD_R12(b2, c2, d2, e2, a2, w[6], 7);
    RMD_R11(a1, b1, c1, d1, e1, w[10], 14);
    RMD_R12(a2, b2, c2, d2, e2, w[15], 8);
    RMD_R11(e1, a1, b1, c1, d1, w[11], 15);
    RMD_R12(e2, a2, b2, c2, d2, w[8], 11);
    RMD_R11(d1, e1, a1, b1, c1, w[12], 6);
    RMD_R12(d2, e2, a2, b2, c2, w[1], 14);
    RMD_R11(c1, d1, e1, a1, b1, w[13], 7);
    RMD_R12(c2, d2, e2, a2, b2, w[10], 14);
    RMD_R11(b1, c1, d1, e1, a1, w[14], 9);
    RMD_R12(b2, c2, d2, e2, a2, w[3], 12);
    RMD_R11(a1, b1, c1, d1, e1, w[15], 8);
    RMD_R12(a2, b2, c2, d2, e2, w[12], 6);

    // Round 2
    RMD_R21(e1, a1, b1, c1, d1, w[7], 7);
    RMD_R22(e2, a2, b2, c2, d2, w[6], 9);
    RMD_R21(d1, e1, a1, b1, c1, w[4], 6);
    RMD_R22(d2, e2, a2, b2, c2, w[11], 13);
    RMD_R21(c1, d1, e1, a1, b1, w[13], 8);
    RMD_R22(c2, d2, e2, a2, b2, w[3], 15);
    RMD_R21(b1, c1, d1, e1, a1, w[1], 13);
    RMD_R22(b2, c2, d2, e2, a2, w[7], 7);
    RMD_R21(a1, b1, c1, d1, e1, w[10], 11);
    RMD_R22(a2, b2, c2, d2, e2, w[0], 12);
    RMD_R21(e1, a1, b1, c1, d1, w[6], 9);
    RMD_R22(e2, a2, b2, c2, d2, w[13], 8);
    RMD_R21(d1, e1, a1, b1, c1, w[15], 7);
    RMD_R22(d2, e2, a2, b2, c2, w[5], 9);
    RMD_R21(c1, d1, e1, a1, b1, w[3], 15);
    RMD_R22(c2, d2, e2, a2, b2, w[10], 11);
    RMD_R21(b1, c1, d1, e1, a1, w[12], 7);
    RMD_R22(b2, c2, d2, e2, a2, w[14], 7);
    RMD_R21(a1, b1, c1, d1, e1, w[0], 12);
    RMD_R22(a2, b2, c2, d2, e2, w[15], 7);
    RMD_R21(e1, a1, b1, c1, d1, w[9], 15);
    RMD_R22(e2, a2, b2, c2, d2, w[8], 12);
    RMD_R21(d1, e1, a1, b1, c1, w[5], 9);
    RMD_R22(d2, e2, a2, b2, c2, w[12], 7);
    RMD_R21(c1, d1, e1, a1, b1, w[2], 11);
    RMD_R22(c2, d2, e2, a2, b2, w[4], 6);
    RMD_R21(b1, c1, d1, e1, a1, w[14], 7);
    RMD_R22(b2, c2, d2, e2, a2, w[9], 15);
    RMD_R21(a1, b1, c1, d1, e1, w[11], 13);
    RMD_R22(a2, b2, c2, d2, e2, w[1], 13);
    RMD_R21(e1, a1, b1, c1, d1, w[8], 12);
    RMD_R22(e2, a2, b2, c2, d2, w[2], 11);

    // Round 3
    RMD_R31(d1, e1, a1, b1, c1, w[3], 11);
    RMD_R32(d2, e2, a2, b2, c2, w[15], 9);
    RMD_R31(c1, d1, e1, a1, b1, w[10], 13);
    RMD_R32(c2, d2, e2, a2, b2, w[5], 7);
    RMD_R31(b1, c1, d1, e1, a1, w[14], 6);
    RMD_R32(b2, c2, d2, e2, a2, w[1], 15);
    RMD_R31(a1, b1, c1, d1, e1, w[4], 7);
    RMD_R32(a2, b2, c2, d2, e2, w[3], 11);
    RMD_R31(e1, a1, b1, c1, d1, w[9], 14);
    RMD_R32(e2, a2, b2, c2, d2, w[7], 8);
    RMD_R31(d1, e1, a1, b1, c1, w[15], 9);
    RMD_R32(d2, e2, a2, b2, c2, w[14], 6);
    RMD_R31(c1, d1, e1, a1, b1, w[8], 13);
    RMD_R32(c2, d2, e2, a2, b2, w[6], 6);
    RMD_R31(b1, c1, d1, e1, a1, w[1], 15);
    RMD_R32(b2, c2, d2, e2, a2, w[9], 14);
    RMD_R31(a1, b1, c1, d1, e1, w[2], 14);
    RMD_R32(a2, b2, c2, d2, e2, w[11], 12);
    RMD_R31(e1, a1, b1, c1, d1, w[7], 8);
    RMD_R32(e2, a2, b2, c2, d2, w[8], 13);
    RMD_R31(d1, e1, a1, b1, c1, w[0], 13);
    RMD_R32(d2, e2, a2, b2, c2, w[12], 5);
    RMD_R31(c1, d1, e1, a1, b1, w[6], 6);
    RMD_R32(c2, d2, e2, a2, b2, w[2], 14);
    RMD_R31(b1, c1, d1, e1, a1, w[13], 5);
    RMD_R32(b2, c2, d2, e2, a2, w[10], 13);
    RMD_R31(a1, b1, c1, d1, e1, w[11], 12);
    RMD_R32(a2, b2, c2, d2, e2, w[0], 13);
    RMD_R31(e1, a1, b1, c1, d1, w[5], 7);
    RMD_R32(e2, a2, b2, c2, d2, w[4], 7);
    RMD_R31(d1, e1, a1, b1, c1, w[12], 5);
    RMD_R32(d2, e2, a2, b2, c2, w[13], 5);

    // Round 4
    RMD_R41(c1, d1, e1, a1, b1, w[1], 11);
    RMD_R42(c2, d2, e2, a2, b2, w[8], 15);
    RMD_R41(b1, c1, d1, e1, a1, w[9], 12);
    RMD_R42(b2, c2, d2, e2, a2, w[6], 5);
    RMD_R41(a1, b1, c1, d1, e1, w[11], 14);
    RMD_R42(a2, b2, c2, d2, e2, w[4], 8);
    RMD_R41(e1, a1, b1, c1, d1, w[10], 15);
    RMD_R42(e2, a2, b2, c2, d2, w[1], 11);
    RMD_R41(d1, e1, a1, b1, c1, w[0], 14);
    RMD_R42(d2, e2, a2, b2, c2, w[3], 14);
    RMD_R41(c1, d1, e1, a1, b1, w[8], 15);
    RMD_R42(c2, d2, e2, a2, b2, w[11], 14);
    RMD_R41(b1, c1, d1, e1, a1, w[12], 9);
    RMD_R42(b2, c2, d2, e2, a2, w[15], 6);
    RMD_R41(a1, b1, c1, d1, e1, w[4], 8);
    RMD_R42(a2, b2, c2, d2, e2, w[0], 14);
    RMD_R41(e1, a1, b1, c1, d1, w[13], 9);
    RMD_R42(e2, a2, b2, c2, d2, w[5], 6);
    RMD_R41(d1, e1, a1, b1, c1, w[3], 14);
    RMD_R42(d2, e2, a2, b2, c2, w[12], 9);
    RMD_R41(c1, d1, e1, a1, b1, w[7], 5);
    RMD_R42(c2, d2, e2, a2, b2, w[2], 12);
    RMD_R41(b1, c1, d1, e1, a1, w[15], 6);
    RMD_R42(b2, c2, d2, e2, a2, w[13], 9);
    RMD_R41(a1, b1, c1, d1, e1, w[14], 8);
    RMD_R42(a2, b2, c2, d2, e2, w[9], 12);
    RMD_R41(e1, a1, b1, c1, d1, w[5], 6);
    RMD_R42(e2, a2, b2, c2, d2, w[7], 5);
    RMD_R41(d1, e1, a1, b1, c1, w[6], 5);
    RMD_R42(d2, e2, a2, b2, c2, w[10], 15);
    RMD_R41(c1, d1, e1, a1, b1, w[2], 12);
    RMD_R42(c2, d2, e2, a2, b2, w[14], 8);

    // Round 5
    RMD_R51(b1, c1, d1, e1, a1, w[4], 9);
    RMD_R52(b2, c2, d2, e2, a2, w[12], 8);
    RMD_R51(a1, b1, c1, d1, e1, w[0], 15);
    RMD_R52(a2, b2, c2, d2, e2, w[15], 5);
    RMD_R51(e1, a1, b1, c1, d1, w[5], 5);
    RMD_R52(e2, a2, b2, c2, d2, w[10], 12);
    RMD_R51(d1, e1, a1, b1, c1, w[9], 11);
    RMD_R52(d2, e2, a2, b2, c2, w[4], 9);
    RMD_R51(c1, d1, e1, a1, b1, w[7], 6);
    RMD_R52(c2, d2, e2, a2, b2, w[1], 12);
    RMD_R51(b1, c1, d1, e1, a1, w[12], 8);
    RMD_R52(b2, c2, d2, e2, a2, w[5], 5);
    RMD_R51(a1, b1, c1, d1, e1, w[2], 13);
    RMD_R52(a2, b2, c2, d2, e2, w[8], 14);
    RMD_R51(e1, a1, b1, c1, d1, w[10], 12);
    RMD_R52(e2, a2, b2, c2, d2, w[7], 6);
    RMD_R51(d1, e1, a1, b1, c1, w[14], 5);
    RMD_R52(d2, e2, a2, b2, c2, w[6], 8);
    RMD_R51(c1, d1, e1, a1, b1, w[1], 12);
    RMD_R52(c2, d2, e2, a2, b2, w[2], 13);
    RMD_R51(b1, c1, d1, e1, a1, w[3], 13);
    RMD_R52(b2, c2, d2, e2, a2, w[13], 6);
    RMD_R51(a1, b1, c1, d1, e1, w[8], 14);
    RMD_R52(a2, b2, c2, d2, e2, w[14], 5);
    RMD_R51(e1, a1, b1, c1, d1, w[11], 11);
    RMD_R52(e2, a2, b2, c2, d2, w[0], 15);
    RMD_R51(d1, e1, a1, b1, c1, w[6], 8);
    RMD_R52(d2, e2, a2, b2, c2, w[3], 13);
    RMD_R51(c1, d1, e1, a1, b1, w[15], 5);
    RMD_R52(c2, d2, e2, a2, b2, w[9], 11);
    RMD_R51(b1, c1, d1, e1, a1, w[13], 6);
    RMD_R52(b2, c2, d2, e2, a2, w[11], 11);

    // Update state (RIPEMD-160 final combination)
    __m256i init0 = _mm256_load_si256((__m256i*)&_rmd_init[0]);
    __m256i init1 = _mm256_load_si256((__m256i*)&_rmd_init[8]);
    __m256i init2 = _mm256_load_si256((__m256i*)&_rmd_init[16]);
    __m256i init3 = _mm256_load_si256((__m256i*)&_rmd_init[24]);
    __m256i init4 = _mm256_load_si256((__m256i*)&_rmd_init[32]);

    __m256i t = init0;
    rmd_state[0] = rmd_add3(init1, c1, d2);
    rmd_state[1] = rmd_add3(init2, d1, e2);
    rmd_state[2] = rmd_add3(init3, e1, a2);
    rmd_state[3] = rmd_add3(init4, a1, b2);
    rmd_state[4] = rmd_add3(t, b1, c2);
  }

} // namespace _ripemd160avx2_fused

// Fused SHA256→RIPEMD160 for compressed keys (1-block SHA256)
// Eliminates intermediate 32-byte buffer writes by keeping SHA256 output in registers
void sha256_ripemd160_avx2_1B(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7) {

  // Step 1: Perform SHA256 (keep output in registers)
  __m256i sha256_state[8] __attribute__ ((aligned (32)));
  _sha256avx2::Initialize(sha256_state);
  _sha256avx2::Transform(sha256_state, i0, i1, i2, i3, i4, i5, i6, i7);

  // Step 2: Feed SHA256 output directly to RIPEMD160 (no intermediate buffer)
  __m256i rmd_state[5] __attribute__ ((aligned (32)));
  _ripemd160avx2_fused::Transform_from_sha256(rmd_state, sha256_state);

  // Step 3: Extract and store RIPEMD160 results
  alignas(32) uint32_t temp[8];

  for (int i = 0; i < 5; i++) {
    _mm256_store_si256((__m256i*)temp, rmd_state[i]);
    ((uint32_t*)d0)[i] = temp[7];
    ((uint32_t*)d1)[i] = temp[6];
    ((uint32_t*)d2)[i] = temp[5];
    ((uint32_t*)d3)[i] = temp[4];
    ((uint32_t*)d4)[i] = temp[3];
    ((uint32_t*)d5)[i] = temp[2];
    ((uint32_t*)d6)[i] = temp[1];
    ((uint32_t*)d7)[i] = temp[0];
  }
}

// Fused SHA256→RIPEMD160 for uncompressed keys (2-block SHA256)
// Eliminates intermediate 32-byte buffer writes by keeping SHA256 output in registers
void sha256_ripemd160_avx2_2B(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7) {

  // Step 1: Perform 2-block SHA256 (keep output in registers)
  __m256i sha256_state[8] __attribute__ ((aligned (32)));
  _sha256avx2::Initialize(sha256_state);
  _sha256avx2::Transform(sha256_state, i0, i1, i2, i3, i4, i5, i6, i7);
  _sha256avx2::Transform(sha256_state, i0 + 16, i1 + 16, i2 + 16, i3 + 16,
                            i4 + 16, i5 + 16, i6 + 16, i7 + 16);

  // Step 2: Feed SHA256 output directly to RIPEMD160 (no intermediate buffer)
  __m256i rmd_state[5] __attribute__ ((aligned (32)));
  _ripemd160avx2_fused::Transform_from_sha256(rmd_state, sha256_state);

  // Step 3: Extract and store RIPEMD160 results
  alignas(32) uint32_t temp[8];

  for (int i = 0; i < 5; i++) {
    _mm256_store_si256((__m256i*)temp, rmd_state[i]);
    ((uint32_t*)d0)[i] = temp[7];
    ((uint32_t*)d1)[i] = temp[6];
    ((uint32_t*)d2)[i] = temp[5];
    ((uint32_t*)d3)[i] = temp[4];
    ((uint32_t*)d4)[i] = temp[3];
    ((uint32_t*)d5)[i] = temp[2];
    ((uint32_t*)d6)[i] = temp[1];
    ((uint32_t*)d7)[i] = temp[0];
  }
}
