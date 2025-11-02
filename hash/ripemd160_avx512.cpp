/*
 * AVX-512 optimized RIPEMD-160 implementation
 * Processes 16 hashes in parallel (2x faster than AVX2)
 *
 * Based on VanitySearch by Jean Luc PONS
 * AVX-512 optimization for keyhunt
 */

#include "ripemd160_avx512.h"
#include "ripemd160.h"
#include <string.h>
#include <immintrin.h>
#include <cpuid.h>
#include <stdio.h>

// Check CPU support for AVX-512F
int ripemd160_avx512_available(void) {
    unsigned int eax, ebx, ecx, edx;

    // Check for AVX-512F support (CPUID function 7, subleaf 0, EBX bit 16)
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        return (ebx & (1 << 16)) != 0;  // AVX-512F bit
    }
    return 0;
}

// Internal AVX-512 RIPEMD-160 implementation
namespace ripemd160avx512 {

#ifdef WIN64
    static const __declspec(align(64)) uint32_t _init[] = {
#else
    static const uint32_t _init[] __attribute__ ((aligned (64))) = {
#endif
        // 16 copies of initial state (for 16-way parallel processing)
        0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,
        0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,
        0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,
        0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,
        0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,
        0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,
        0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,
        0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,
        0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,
        0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul
    };

// AVX-512 macros for RIPEMD-160
#define ROL(x,n) _mm512_or_si512(_mm512_slli_epi32(x, n), _mm512_srli_epi32(x, 32 - n))

#define f1(x,y,z) _mm512_xor_si512(x, _mm512_xor_si512(y, z))
#define f2(x,y,z) _mm512_or_si512(_mm512_and_si512(x,y),_mm512_andnot_si512(x,z))
#define f3(x,y,z) _mm512_xor_si512(_mm512_or_si512(x,_mm512_andnot_si512(y,_mm512_set1_epi32(-1))),z)
#define f4(x,y,z) _mm512_or_si512(_mm512_and_si512(x,z),_mm512_andnot_si512(z,y))
#define f5(x,y,z) _mm512_xor_si512(x,_mm512_or_si512(y,_mm512_andnot_si512(z,_mm512_set1_epi32(-1))))

#define add3(x0, x1, x2) _mm512_add_epi32(_mm512_add_epi32(x0, x1), x2)
#define add4(x0, x1, x2, x3) _mm512_add_epi32(_mm512_add_epi32(x0, x1), _mm512_add_epi32(x2, x3))

#define Round(a,b,c,d,e,f,x,k,r) \
    u = add4(a,f,x,_mm512_set1_epi32(k)); \
    a = _mm512_add_epi32(ROL(u, r),e); \
    c = ROL(c, 10);

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

// Load 16 32-bit words from 16 different message blocks
#define LOADW(i) _mm512_set_epi32( \
    *((const uint32_t *)blk[0]+i),  *((const uint32_t *)blk[1]+i),  *((const uint32_t *)blk[2]+i),  *((const uint32_t *)blk[3]+i), \
    *((const uint32_t *)blk[4]+i),  *((const uint32_t *)blk[5]+i),  *((const uint32_t *)blk[6]+i),  *((const uint32_t *)blk[7]+i), \
    *((const uint32_t *)blk[8]+i),  *((const uint32_t *)blk[9]+i),  *((const uint32_t *)blk[10]+i), *((const uint32_t *)blk[11]+i), \
    *((const uint32_t *)blk[12]+i), *((const uint32_t *)blk[13]+i), *((const uint32_t *)blk[14]+i), *((const uint32_t *)blk[15]+i))

    // Optimized transpose load with prefetching
    static inline void transpose_and_load(__m512i *w, const uint8_t *blk[16]) {
        // Prefetch all input blocks
        for (int i = 0; i < 16; i++) {
            _mm_prefetch((const char*)blk[i], _MM_HINT_T0);
        }

        // Load word by word from each block
        for (int i = 0; i < 8; i++) {
            w[i] = LOADW(i);
        }
    }

    // Initialize RIPEMD-160 state
    void Initialize(__m512i *s) {
        memcpy(s, _init, sizeof(_init));
    }

    // Perform 16 RIPEMD-160 in parallel using AVX-512
    void Transform(__m512i *s, const uint8_t *blk[16]) {

        __m512i a1 = _mm512_load_si512(s + 0);
        __m512i b1 = _mm512_load_si512(s + 1);
        __m512i c1 = _mm512_load_si512(s + 2);
        __m512i d1 = _mm512_load_si512(s + 3);
        __m512i e1 = _mm512_load_si512(s + 4);
        __m512i a2 = a1;
        __m512i b2 = b1;
        __m512i c2 = c1;
        __m512i d2 = d1;
        __m512i e2 = e1;
        __m512i u;
        __m512i w[16];

        // Load message words with prefetching
        transpose_and_load(w, blk);

        // Padding for 32-byte input
        const __m512i pad80 = _mm512_set1_epi32(0x00000080u);
        const __m512i zero = _mm512_setzero_si512();
        const __m512i bitlen = _mm512_set1_epi32(32 << 3);

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

        // Update state
        __m512i t = s[0];
        s[0] = add3(s[1], c1, d2);
        s[1] = add3(s[2], d1, e2);
        s[2] = add3(s[3], e1, a2);
        s[3] = add3(s[4], a1, b2);
        s[4] = add3(t, b1, c2);
    }

} // namespace ripemd160avx512

// Unpack and deinterleave results from AVX-512 registers
#ifdef WIN64
#define DEPACK(d,i) \
    ((uint32_t *)d)[0] = s[0].m512i_u32[i]; \
    ((uint32_t *)d)[1] = s[1].m512i_u32[i]; \
    ((uint32_t *)d)[2] = s[2].m512i_u32[i]; \
    ((uint32_t *)d)[3] = s[3].m512i_u32[i]; \
    ((uint32_t *)d)[4] = s[4].m512i_u32[i];
#else
#define DEPACK(d,i) \
    ((uint32_t *)d)[0] = s0[i]; \
    ((uint32_t *)d)[1] = s1[i]; \
    ((uint32_t *)d)[2] = s2[i]; \
    ((uint32_t *)d)[3] = s3[i]; \
    ((uint32_t *)d)[4] = s4[i];
#endif

void ripemd160avx512_32(
    const unsigned char *i0,  const unsigned char *i1,  const unsigned char *i2,  const unsigned char *i3,
    const unsigned char *i4,  const unsigned char *i5,  const unsigned char *i6,  const unsigned char *i7,
    const unsigned char *i8,  const unsigned char *i9,  const unsigned char *i10, const unsigned char *i11,
    const unsigned char *i12, const unsigned char *i13, const unsigned char *i14, const unsigned char *i15,
    unsigned char *d0,  unsigned char *d1,  unsigned char *d2,  unsigned char *d3,
    unsigned char *d4,  unsigned char *d5,  unsigned char *d6,  unsigned char *d7,
    unsigned char *d8,  unsigned char *d9,  unsigned char *d10, unsigned char *d11,
    unsigned char *d12, unsigned char *d13, unsigned char *d14, unsigned char *d15) {

    __m512i s[5] __attribute__((aligned(64)));
    const uint8_t *bs[] = { i0, i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15 };

    ripemd160avx512::Initialize(s);
    ripemd160avx512::Transform(s, bs);

#ifndef WIN64
    uint32_t *s0 = (uint32_t *)&s[0];
    uint32_t *s1 = (uint32_t *)&s[1];
    uint32_t *s2 = (uint32_t *)&s[2];
    uint32_t *s3 = (uint32_t *)&s[3];
    uint32_t *s4 = (uint32_t *)&s[4];
#endif

    // Unpack results (AVX-512 order: MSB first)
    DEPACK(d0, 15);
    DEPACK(d1, 14);
    DEPACK(d2, 13);
    DEPACK(d3, 12);
    DEPACK(d4, 11);
    DEPACK(d5, 10);
    DEPACK(d6, 9);
    DEPACK(d7, 8);
    DEPACK(d8, 7);
    DEPACK(d9, 6);
    DEPACK(d10, 5);
    DEPACK(d11, 4);
    DEPACK(d12, 3);
    DEPACK(d13, 2);
    DEPACK(d14, 1);
    DEPACK(d15, 0);
}

void ripemd160avx512_test() {
    if (!ripemd160_avx512_available()) {
        printf("AVX-512 not available on this CPU\n");
        return;
    }

    unsigned char h[16][20], ch[16][20], m[16][64];

    for (int i = 0; i < 16; i++) {
        snprintf((char *)m[i], 64, "Test message %02d for AVX512 RMD160", i + 1);
        ripemd160_32(m[i], ch[i]);
    }

    // Compute AVX-512 hashes
    ripemd160avx512_32(
        m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
        m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15],
        h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7],
        h[8], h[9], h[10], h[11], h[12], h[13], h[14], h[15]);

    // Verify results
    int failed = 0;
    for (int i = 0; i < 16; i++) {
        if (ripemd160_hex(h[i]) != ripemd160_hex(ch[i])) {
            failed = 1;
            printf("RIPEMD160 AVX-512 Result %d FAILED!\n", i);
            printf("Expected: %s\n", ripemd160_hex(ch[i]).c_str());
            printf("Got:      %s\n", ripemd160_hex(h[i]).c_str());
        }
    }

    if (!failed) {
        printf("RIPEMD160 AVX-512 Results OK! (16-way parallel)\n");
    }
}
