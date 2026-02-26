/*
 * AVX2 optimized RIPEMD-160 implementation
 * Processes 8 hashes in parallel (2x faster than SSE2)
 *
 * Based on VanitySearch by Jean Luc PONS
 * AVX2 optimization for keyhunt by Claude Code
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

#ifdef WIN64
    static const __declspec(align(32)) uint32_t _init[] = {
#else
    static const uint32_t _init[] __attribute__ ((aligned (32))) = {
#endif
        // 8 copies of initial state (for 8-way parallel processing)
        0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,0x67452301ul,
        0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,0xEFCDAB89ul,
        0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,0x98BADCFEul,
        0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,0x10325476ul,
        0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul,0xC3D2E1F0ul
    };

// AVX2 macros for RIPEMD-160
#define ROL(x,n) _mm256_or_si256(_mm256_slli_epi32(x, n), _mm256_srli_epi32(x, 32 - n))

#define ALLONES_256 _mm256_cmpeq_epi32(_mm256_setzero_si256(), _mm256_setzero_si256())
#define not256(x)   _mm256_andnot_si256(x, ALLONES_256)
#define f1(x,y,z) _mm256_xor_si256(x, _mm256_xor_si256(y, z))
#define f2(x,y,z) _mm256_or_si256(_mm256_and_si256(x,y),_mm256_andnot_si256(x,z))
#define f3(x,y,z) _mm256_xor_si256(_mm256_or_si256(x, not256(y)), z)
#define f4(x,y,z) _mm256_or_si256(_mm256_and_si256(x,z),_mm256_andnot_si256(z,y))
#define f5(x,y,z) _mm256_xor_si256(x, _mm256_or_si256(y, not256(z)))

#define add3(x0, x1, x2) _mm256_add_epi32(_mm256_add_epi32(x0, x1), x2)
#define add4(x0, x1, x2, x3) _mm256_add_epi32(_mm256_add_epi32(x0, x1), _mm256_add_epi32(x2, x3))

#define Round(a,b,c,d,e,f,x,k,r) \
    u = add4(a,f,x,_mm256_set1_epi32(k)); \
    a = _mm256_add_epi32(ROL(u, r),e); \
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

// Optimized load: Load 8 32-bit words from 8 different message blocks
// Note: _mm256_set_epi32 loads in reverse order (MSB first)
#define LOADW(i) _mm256_set_epi32( \
    *((const uint32_t *)blk[0]+i), \
    *((const uint32_t *)blk[1]+i), \
    *((const uint32_t *)blk[2]+i), \
    *((const uint32_t *)blk[3]+i), \
    *((const uint32_t *)blk[4]+i), \
    *((const uint32_t *)blk[5]+i), \
    *((const uint32_t *)blk[6]+i), \
    *((const uint32_t *)blk[7]+i))

// Optimized transpose load with prefetching
static inline void transpose_and_load(__m256i *w, const uint8_t *blk[8]) {
    // Prefetch all input blocks
    _mm_prefetch((const char*)blk[0], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[1], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[2], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[3], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[4], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[5], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[6], _MM_HINT_T0);
    _mm_prefetch((const char*)blk[7], _MM_HINT_T0);

    // Load word by word from each block
    for (int i = 0; i < 8; i++) {
        w[i] = LOADW(i);
    }
}

    // Initialize RIPEMD-160 state
    void Initialize(__m256i *s) {
        memcpy(s, _init, sizeof(_init));
    }

    // Perform 8 RIPEMD-160 in parallel using AVX2
    void Transform(__m256i *s, const uint8_t *blk[8]) {

        __m256i a1 = _mm256_load_si256(s + 0);
        __m256i b1 = _mm256_load_si256(s + 1);
        __m256i c1 = _mm256_load_si256(s + 2);
        __m256i d1 = _mm256_load_si256(s + 3);
        __m256i e1 = _mm256_load_si256(s + 4);
        __m256i a2 = a1;
        __m256i b2 = b1;
        __m256i c2 = c1;
        __m256i d2 = d1;
        __m256i e2 = e1;
        __m256i u;
        __m256i w[16];

        // Load message words with prefetching
        transpose_and_load(w, blk);

        // Padding for 32-byte input
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

        // Update state
        __m256i t = s[0];
        s[0] = add3(s[1], c1, d2);
        s[1] = add3(s[2], d1, e2);
        s[2] = add3(s[3], e1, a2);
        s[3] = add3(s[4], a1, b2);
        s[4] = add3(t, b1, c2);
    }

} // namespace ripemd160avx2

// Unpack and deinterleave results from AVX2 registers
#ifdef WIN64
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

#ifndef WIN64
    uint32_t *s0 = (uint32_t *)&s[0];
    uint32_t *s1 = (uint32_t *)&s[1];
    uint32_t *s2 = (uint32_t *)&s[2];
    uint32_t *s3 = (uint32_t *)&s[3];
    uint32_t *s4 = (uint32_t *)&s[4];
#endif

    // Unpack results (AVX2 order: MSB first)
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
