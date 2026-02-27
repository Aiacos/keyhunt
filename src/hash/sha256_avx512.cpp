/*
 * AVX-512 optimized SHA-256 implementation
 * Processes 16 hashes in parallel (2x faster than AVX2)
 *
 * Based on VanitySearch by Jean Luc PONS
 * AVX-512 optimization for keyhunt
 */

#include "sha256_avx512.h"
#include "sha256.h"
#include <immintrin.h>
#include <cpuid.h>
#include <string.h>
#include <stdint.h>

#if defined(__i386__) || defined(__x86_64__)
static inline uint64_t xgetbv_u32(uint32_t index) {
    uint32_t eax, edx;
    __asm__ volatile (".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((uint64_t)edx << 32) | eax;
}

static inline int os_avx512_enabled(void) {
    unsigned int eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 0;
    if (!(ecx & bit_OSXSAVE)) return 0;
    if (!(ecx & bit_AVX)) return 0;
    /* XCR0: require XMM, YMM, opmask, ZMM_hi256, Hi16_ZMM (bits 1,2,5,6,7). */
    return (xgetbv_u32(0) & 0xE6u) == 0xE6u;
}
#endif

// Check CPU support for AVX512F (AVX-512 Foundation)
int sha256_avx512_available(void) {
    unsigned int eax, ebx, ecx, edx;

    // Check for AVX512F support (CPUID function 7, subleaf 0, EBX bit 16)
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        if ((ebx & (1 << 16)) == 0) return 0;  // AVX512F bit
#if defined(__i386__) || defined(__x86_64__)
        return os_avx512_enabled();
#else
        return 1;
#endif
    }
    return 0;
}

namespace _sha256avx512
{

#ifdef _MSC_VER
  static const __declspec(align(64)) uint32_t _init[] = {
#else
  static const uint32_t _init[] __attribute__ ((aligned (64))) = {
#endif
      // 16 copies of SHA256 initial state (for 16-way parallel processing)
      0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,
      0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,0x6a09e667,
      0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,
      0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,0xbb67ae85,
      0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,
      0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,0x3c6ef372,
      0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,
      0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,0xa54ff53a,
      0x510e527f,0x510e527f,0x510e527f,0x510e527f,0x510e527f,0x510e527f,0x510e527f,0x510e527f,
      0x510e527f,0x510e527f,0x510e527f,0x510e527f,0x510e527f,0x510e527f,0x510e527f,0x510e527f,
      0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,
      0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,0x9b05688c,
      0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,
      0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,0x1f83d9ab,
      0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,
      0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19,0x5be0cd19
  };

// AVX-512 SHA-256 macros using 512-bit registers (16-way parallel)
#define ROR(x,n)   _mm512_or_si512(_mm512_srli_epi32(x, n), _mm512_slli_epi32(x, 32 - n))
#define SHR(x,n)   _mm512_srli_epi32(x, n)

// Ch(e,f,g) = (e & f) ^ (~e & g) using ternarylogic (AVX-512 optimized)
#define Ch(e, f, g) _mm512_ternarylogic_epi32(e, f, g, 0xCA)

// Maj(a,b,c) = (a & b) ^ (a & c) ^ (b & c) using ternarylogic (AVX-512 optimized)
#define Maj(a, b, c) _mm512_ternarylogic_epi32(a, b, c, 0xE8)

/* SHA256 Functions - AVX-512 versions */
#define S0(x) (_mm512_xor_si512(ROR((x), 2), _mm512_xor_si512(ROR((x), 13), ROR((x), 22))))
#define S1(x) (_mm512_xor_si512(ROR((x), 6), _mm512_xor_si512(ROR((x), 11), ROR((x), 25))))
#define s0(x) (_mm512_xor_si512(ROR((x), 7), _mm512_xor_si512(ROR((x), 18), SHR((x), 3))))
#define s1(x) (_mm512_xor_si512(ROR((x), 17), _mm512_xor_si512(ROR((x), 19), SHR((x), 10))))

#define add4(x0, x1, x2, x3) _mm512_add_epi32(_mm512_add_epi32(x0, x1), _mm512_add_epi32(x2, x3))
#define add3(x0, x1, x2)     _mm512_add_epi32(_mm512_add_epi32(x0, x1), x2)
#define add5(x0, x1, x2, x3, x4) _mm512_add_epi32(add3(x0, x1, x2), _mm512_add_epi32(x3, x4))

#define Round(a, b, c, d, e, f, g, h, i, w)                 \
    T1 = add5(h, S1(e), Ch(e, f, g), _mm512_set1_epi32(i), w); \
    d = _mm512_add_epi32(d, T1);                            \
    T2 = _mm512_add_epi32(S0(a), Maj(a, b, c));             \
    h = _mm512_add_epi32(T1, T2);

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

  // Initialize state
  void Initialize(__m512i *s) {
    memcpy(s, _init, sizeof(_init));
  }

  // Perform 16 SHA256 in parallel using AVX-512
  void Transform(__m512i *s, uint32_t *b0, uint32_t *b1, uint32_t *b2, uint32_t *b3,
                                uint32_t *b4, uint32_t *b5, uint32_t *b6, uint32_t *b7,
                                uint32_t *b8, uint32_t *b9, uint32_t *b10, uint32_t *b11,
                                uint32_t *b12, uint32_t *b13, uint32_t *b14, uint32_t *b15)
  {
    __m512i a, b, c, d, e, f, g, h;
    __m512i w0, w1, w2, w3, w4, w5, w6, w7;
    __m512i w8, w9, w10, w11, w12, w13, w14, w15;
    __m512i T1, T2;

    a = _mm512_load_si512(s + 0);
    b = _mm512_load_si512(s + 1);
    c = _mm512_load_si512(s + 2);
    d = _mm512_load_si512(s + 3);
    e = _mm512_load_si512(s + 4);
    f = _mm512_load_si512(s + 5);
    g = _mm512_load_si512(s + 6);
    h = _mm512_load_si512(s + 7);

    // Load data from 16 different message blocks (transpose operation)
    w0 = _mm512_set_epi32(b0[0], b1[0], b2[0], b3[0], b4[0], b5[0], b6[0], b7[0],
                          b8[0], b9[0], b10[0], b11[0], b12[0], b13[0], b14[0], b15[0]);
    w1 = _mm512_set_epi32(b0[1], b1[1], b2[1], b3[1], b4[1], b5[1], b6[1], b7[1],
                          b8[1], b9[1], b10[1], b11[1], b12[1], b13[1], b14[1], b15[1]);
    w2 = _mm512_set_epi32(b0[2], b1[2], b2[2], b3[2], b4[2], b5[2], b6[2], b7[2],
                          b8[2], b9[2], b10[2], b11[2], b12[2], b13[2], b14[2], b15[2]);
    w3 = _mm512_set_epi32(b0[3], b1[3], b2[3], b3[3], b4[3], b5[3], b6[3], b7[3],
                          b8[3], b9[3], b10[3], b11[3], b12[3], b13[3], b14[3], b15[3]);
    w4 = _mm512_set_epi32(b0[4], b1[4], b2[4], b3[4], b4[4], b5[4], b6[4], b7[4],
                          b8[4], b9[4], b10[4], b11[4], b12[4], b13[4], b14[4], b15[4]);
    w5 = _mm512_set_epi32(b0[5], b1[5], b2[5], b3[5], b4[5], b5[5], b6[5], b7[5],
                          b8[5], b9[5], b10[5], b11[5], b12[5], b13[5], b14[5], b15[5]);
    w6 = _mm512_set_epi32(b0[6], b1[6], b2[6], b3[6], b4[6], b5[6], b6[6], b7[6],
                          b8[6], b9[6], b10[6], b11[6], b12[6], b13[6], b14[6], b15[6]);
    w7 = _mm512_set_epi32(b0[7], b1[7], b2[7], b3[7], b4[7], b5[7], b6[7], b7[7],
                          b8[7], b9[7], b10[7], b11[7], b12[7], b13[7], b14[7], b15[7]);
    w8 = _mm512_set_epi32(b0[8], b1[8], b2[8], b3[8], b4[8], b5[8], b6[8], b7[8],
                          b8[8], b9[8], b10[8], b11[8], b12[8], b13[8], b14[8], b15[8]);
    w9 = _mm512_set_epi32(b0[9], b1[9], b2[9], b3[9], b4[9], b5[9], b6[9], b7[9],
                          b8[9], b9[9], b10[9], b11[9], b12[9], b13[9], b14[9], b15[9]);
    w10 = _mm512_set_epi32(b0[10], b1[10], b2[10], b3[10], b4[10], b5[10], b6[10], b7[10],
                           b8[10], b9[10], b10[10], b11[10], b12[10], b13[10], b14[10], b15[10]);
    w11 = _mm512_set_epi32(b0[11], b1[11], b2[11], b3[11], b4[11], b5[11], b6[11], b7[11],
                           b8[11], b9[11], b10[11], b11[11], b12[11], b13[11], b14[11], b15[11]);
    w12 = _mm512_set_epi32(b0[12], b1[12], b2[12], b3[12], b4[12], b5[12], b6[12], b7[12],
                           b8[12], b9[12], b10[12], b11[12], b12[12], b13[12], b14[12], b15[12]);
    w13 = _mm512_set_epi32(b0[13], b1[13], b2[13], b3[13], b4[13], b5[13], b6[13], b7[13],
                           b8[13], b9[13], b10[13], b11[13], b12[13], b13[13], b14[13], b15[13]);
    w14 = _mm512_set_epi32(b0[14], b1[14], b2[14], b3[14], b4[14], b5[14], b6[14], b7[14],
                           b8[14], b9[14], b10[14], b11[14], b12[14], b13[14], b14[14], b15[14]);
    w15 = _mm512_set_epi32(b0[15], b1[15], b2[15], b3[15], b4[15], b5[15], b6[15], b7[15],
                           b8[15], b9[15], b10[15], b11[15], b12[15], b13[15], b14[15], b15[15]);

    // 64 rounds of SHA-256
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

    WMIX()

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

    WMIX()

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

    WMIX()

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

    // Add to state
    s[0] = _mm512_add_epi32(a, s[0]);
    s[1] = _mm512_add_epi32(b, s[1]);
    s[2] = _mm512_add_epi32(c, s[2]);
    s[3] = _mm512_add_epi32(d, s[3]);
    s[4] = _mm512_add_epi32(e, s[4]);
    s[5] = _mm512_add_epi32(f, s[5]);
    s[6] = _mm512_add_epi32(g, s[6]);
    s[7] = _mm512_add_epi32(h, s[7]);
  }

  // Perform 16 SHA(SHA(bi))[0] in parallel using AVX-512
  void Transform2(__m512i *s, uint32_t *b0, uint32_t *b1, uint32_t *b2, uint32_t *b3,
                                 uint32_t *b4, uint32_t *b5, uint32_t *b6, uint32_t *b7,
                                 uint32_t *b8, uint32_t *b9, uint32_t *b10, uint32_t *b11,
                                 uint32_t *b12, uint32_t *b13, uint32_t *b14, uint32_t *b15) {
    __m512i a, b, c, d, e, f, g, h;
    __m512i w0, w1, w2, w3, w4, w5, w6, w7;
    __m512i w8, w9, w10, w11, w12, w13, w14, w15;
    __m512i T1, T2;

    a = _mm512_load_si512(s + 0);
    b = _mm512_load_si512(s + 1);
    c = _mm512_load_si512(s + 2);
    d = _mm512_load_si512(s + 3);
    e = _mm512_load_si512(s + 4);
    f = _mm512_load_si512(s + 5);
    g = _mm512_load_si512(s + 6);
    h = _mm512_load_si512(s + 7);

    // Load data from 16 different message blocks
    w0 = _mm512_set_epi32(b0[0], b1[0], b2[0], b3[0], b4[0], b5[0], b6[0], b7[0],
                          b8[0], b9[0], b10[0], b11[0], b12[0], b13[0], b14[0], b15[0]);
    w1 = _mm512_set_epi32(b0[1], b1[1], b2[1], b3[1], b4[1], b5[1], b6[1], b7[1],
                          b8[1], b9[1], b10[1], b11[1], b12[1], b13[1], b14[1], b15[1]);
    w2 = _mm512_set_epi32(b0[2], b1[2], b2[2], b3[2], b4[2], b5[2], b6[2], b7[2],
                          b8[2], b9[2], b10[2], b11[2], b12[2], b13[2], b14[2], b15[2]);
    w3 = _mm512_set_epi32(b0[3], b1[3], b2[3], b3[3], b4[3], b5[3], b6[3], b7[3],
                          b8[3], b9[3], b10[3], b11[3], b12[3], b13[3], b14[3], b15[3]);
    w4 = _mm512_set_epi32(b0[4], b1[4], b2[4], b3[4], b4[4], b5[4], b6[4], b7[4],
                          b8[4], b9[4], b10[4], b11[4], b12[4], b13[4], b14[4], b15[4]);
    w5 = _mm512_set_epi32(b0[5], b1[5], b2[5], b3[5], b4[5], b5[5], b6[5], b7[5],
                          b8[5], b9[5], b10[5], b11[5], b12[5], b13[5], b14[5], b15[5]);
    w6 = _mm512_set_epi32(b0[6], b1[6], b2[6], b3[6], b4[6], b5[6], b6[6], b7[6],
                          b8[6], b9[6], b10[6], b11[6], b12[6], b13[6], b14[6], b15[6]);
    w7 = _mm512_set_epi32(b0[7], b1[7], b2[7], b3[7], b4[7], b5[7], b6[7], b7[7],
                          b8[7], b9[7], b10[7], b11[7], b12[7], b13[7], b14[7], b15[7]);
    w8 = _mm512_set_epi32(b0[8], b1[8], b2[8], b3[8], b4[8], b5[8], b6[8], b7[8],
                          b8[8], b9[8], b10[8], b11[8], b12[8], b13[8], b14[8], b15[8]);
    w9 = _mm512_set_epi32(b0[9], b1[9], b2[9], b3[9], b4[9], b5[9], b6[9], b7[9],
                          b8[9], b9[9], b10[9], b11[9], b12[9], b13[9], b14[9], b15[9]);
    w10 = _mm512_set_epi32(b0[10], b1[10], b2[10], b3[10], b4[10], b5[10], b6[10], b7[10],
                           b8[10], b9[10], b10[10], b11[10], b12[10], b13[10], b14[10], b15[10]);
    w11 = _mm512_set_epi32(b0[11], b1[11], b2[11], b3[11], b4[11], b5[11], b6[11], b7[11],
                           b8[11], b9[11], b10[11], b11[11], b12[11], b13[11], b14[11], b15[11]);
    w12 = _mm512_set_epi32(b0[12], b1[12], b2[12], b3[12], b4[12], b5[12], b6[12], b7[12],
                           b8[12], b9[12], b10[12], b11[12], b12[12], b13[12], b14[12], b15[12]);
    w13 = _mm512_set_epi32(b0[13], b1[13], b2[13], b3[13], b4[13], b5[13], b6[13], b7[13],
                           b8[13], b9[13], b10[13], b11[13], b12[13], b13[13], b14[13], b15[13]);
    w14 = _mm512_set_epi32(b0[14], b1[14], b2[14], b3[14], b4[14], b5[14], b6[14], b7[14],
                           b8[14], b9[14], b10[14], b11[14], b12[14], b13[14], b14[14], b15[14]);
    w15 = _mm512_set_epi32(b0[15], b1[15], b2[15], b3[15], b4[15], b5[15], b6[15], b7[15],
                           b8[15], b9[15], b10[15], b11[15], b12[15], b13[15], b14[15], b15[15]);

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

    WMIX()

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

    WMIX()

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

    WMIX()

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

    // Prepare second round
    w0 = _mm512_add_epi32(a, s[0]);
    w1 = _mm512_add_epi32(b, s[1]);
    w2 = _mm512_add_epi32(c, s[2]);
    w3 = _mm512_add_epi32(d, s[3]);
    w4 = _mm512_add_epi32(e, s[4]);
    w5 = _mm512_add_epi32(f, s[5]);
    w6 = _mm512_add_epi32(g, s[6]);
    w7 = _mm512_add_epi32(h, s[7]);
    w8 = _mm512_set1_epi32(0x80000000);
    w9 = _mm512_xor_si512(w9, w9);
    w10 = _mm512_xor_si512(w10, w10);
    w11 = _mm512_xor_si512(w11, w11);
    w12 = _mm512_xor_si512(w12, w12);
    w13 = _mm512_xor_si512(w13, w13);
    w14 = _mm512_xor_si512(w14, w14);
    w15 = _mm512_set1_epi32(0x100);

    a = _mm512_load_si512(s + 0);
    b = _mm512_load_si512(s + 1);
    c = _mm512_load_si512(s + 2);
    d = _mm512_load_si512(s + 3);
    e = _mm512_load_si512(s + 4);
    f = _mm512_load_si512(s + 5);
    g = _mm512_load_si512(s + 6);
    h = _mm512_load_si512(s + 7);

    // Second SHA256 round
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

    WMIX()

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

    WMIX()

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

    WMIX()

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

    s[0] = _mm512_add_epi32(a, s[0]);
  }

} // namespace _sha256avx512

// Public interface for AVX-512 16-way SHA256 (1 block)
void sha256avx512_1B(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint32_t *i8, uint32_t *i9, uint32_t *i10, uint32_t *i11,
    uint32_t *i12, uint32_t *i13, uint32_t *i14, uint32_t *i15,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7,
    uint8_t *d8, uint8_t *d9, uint8_t *d10, uint8_t *d11,
    uint8_t *d12, uint8_t *d13, uint8_t *d14, uint8_t *d15) {

  __m512i s[8] __attribute__ ((aligned (64)));

  _sha256avx512::Initialize(s);
  _sha256avx512::Transform(s, i0, i1, i2, i3, i4, i5, i6, i7,
                              i8, i9, i10, i11, i12, i13, i14, i15);

  // Transpose and store the 16 SHA256 results (32 bytes each)
  alignas(64) uint32_t temp[16];

  for (int i = 0; i < 8; i++) {
    _mm512_store_si512((__m512i*)temp, s[i]);
    ((uint32_t*)d0)[i] = __builtin_bswap32(temp[15]);
    ((uint32_t*)d1)[i] = __builtin_bswap32(temp[14]);
    ((uint32_t*)d2)[i] = __builtin_bswap32(temp[13]);
    ((uint32_t*)d3)[i] = __builtin_bswap32(temp[12]);
    ((uint32_t*)d4)[i] = __builtin_bswap32(temp[11]);
    ((uint32_t*)d5)[i] = __builtin_bswap32(temp[10]);
    ((uint32_t*)d6)[i] = __builtin_bswap32(temp[9]);
    ((uint32_t*)d7)[i] = __builtin_bswap32(temp[8]);
    ((uint32_t*)d8)[i] = __builtin_bswap32(temp[7]);
    ((uint32_t*)d9)[i] = __builtin_bswap32(temp[6]);
    ((uint32_t*)d10)[i] = __builtin_bswap32(temp[5]);
    ((uint32_t*)d11)[i] = __builtin_bswap32(temp[4]);
    ((uint32_t*)d12)[i] = __builtin_bswap32(temp[3]);
    ((uint32_t*)d13)[i] = __builtin_bswap32(temp[2]);
    ((uint32_t*)d14)[i] = __builtin_bswap32(temp[1]);
    ((uint32_t*)d15)[i] = __builtin_bswap32(temp[0]);
  }
}

// Public interface for AVX-512 16-way SHA256 (2 blocks)
void sha256avx512_2B(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint32_t *i8, uint32_t *i9, uint32_t *i10, uint32_t *i11,
    uint32_t *i12, uint32_t *i13, uint32_t *i14, uint32_t *i15,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7,
    uint8_t *d8, uint8_t *d9, uint8_t *d10, uint8_t *d11,
    uint8_t *d12, uint8_t *d13, uint8_t *d14, uint8_t *d15) {

  __m512i s[8] __attribute__ ((aligned (64)));

  _sha256avx512::Initialize(s);
  _sha256avx512::Transform(s, i0, i1, i2, i3, i4, i5, i6, i7,
                              i8, i9, i10, i11, i12, i13, i14, i15);
  _sha256avx512::Transform(s, i0 + 16, i1 + 16, i2 + 16, i3 + 16,
                              i4 + 16, i5 + 16, i6 + 16, i7 + 16,
                              i8 + 16, i9 + 16, i10 + 16, i11 + 16,
                              i12 + 16, i13 + 16, i14 + 16, i15 + 16);

  // Unpack and store results
  alignas(64) uint32_t temp[16];

  for (int i = 0; i < 8; i++) {
    _mm512_store_si512((__m512i*)temp, s[i]);
    ((uint32_t*)d0)[i] = __builtin_bswap32(temp[15]);
    ((uint32_t*)d1)[i] = __builtin_bswap32(temp[14]);
    ((uint32_t*)d2)[i] = __builtin_bswap32(temp[13]);
    ((uint32_t*)d3)[i] = __builtin_bswap32(temp[12]);
    ((uint32_t*)d4)[i] = __builtin_bswap32(temp[11]);
    ((uint32_t*)d5)[i] = __builtin_bswap32(temp[10]);
    ((uint32_t*)d6)[i] = __builtin_bswap32(temp[9]);
    ((uint32_t*)d7)[i] = __builtin_bswap32(temp[8]);
    ((uint32_t*)d8)[i] = __builtin_bswap32(temp[7]);
    ((uint32_t*)d9)[i] = __builtin_bswap32(temp[6]);
    ((uint32_t*)d10)[i] = __builtin_bswap32(temp[5]);
    ((uint32_t*)d11)[i] = __builtin_bswap32(temp[4]);
    ((uint32_t*)d12)[i] = __builtin_bswap32(temp[3]);
    ((uint32_t*)d13)[i] = __builtin_bswap32(temp[2]);
    ((uint32_t*)d14)[i] = __builtin_bswap32(temp[1]);
    ((uint32_t*)d15)[i] = __builtin_bswap32(temp[0]);
  }
}

// Public interface for AVX-512 16-way SHA256 checksum
void sha256avx512_checksum(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint32_t *i8, uint32_t *i9, uint32_t *i10, uint32_t *i11,
    uint32_t *i12, uint32_t *i13, uint32_t *i14, uint32_t *i15,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7,
    uint8_t *d8, uint8_t *d9, uint8_t *d10, uint8_t *d11,
    uint8_t *d12, uint8_t *d13, uint8_t *d14, uint8_t *d15) {

  __m512i s[8] __attribute__ ((aligned (64)));

  _sha256avx512::Initialize(s);
  _sha256avx512::Transform2(s, i0, i1, i2, i3, i4, i5, i6, i7,
                               i8, i9, i10, i11, i12, i13, i14, i15);

  // Extract only first 32 bits from each result
  alignas(64) uint32_t temp[16];
  _mm512_store_si512((__m512i*)temp, s[0]);

  *((uint32_t *)d0) = __builtin_bswap32(temp[15]);
  *((uint32_t *)d1) = __builtin_bswap32(temp[14]);
  *((uint32_t *)d2) = __builtin_bswap32(temp[13]);
  *((uint32_t *)d3) = __builtin_bswap32(temp[12]);
  *((uint32_t *)d4) = __builtin_bswap32(temp[11]);
  *((uint32_t *)d5) = __builtin_bswap32(temp[10]);
  *((uint32_t *)d6) = __builtin_bswap32(temp[9]);
  *((uint32_t *)d7) = __builtin_bswap32(temp[8]);
  *((uint32_t *)d8) = __builtin_bswap32(temp[7]);
  *((uint32_t *)d9) = __builtin_bswap32(temp[6]);
  *((uint32_t *)d10) = __builtin_bswap32(temp[5]);
  *((uint32_t *)d11) = __builtin_bswap32(temp[4]);
  *((uint32_t *)d12) = __builtin_bswap32(temp[3]);
  *((uint32_t *)d13) = __builtin_bswap32(temp[2]);
  *((uint32_t *)d14) = __builtin_bswap32(temp[1]);
  *((uint32_t *)d15) = __builtin_bswap32(temp[0]);
}
