/*
 * AVX2-ready implementation for secp256k1 Modular Multiplication
 *
 * Current implementation uses scalar operations with _umul128 and _addcarry_u64.
 * AVX2 infrastructure is in place for future optimizations, but carry-chain
 * vectorization is not beneficial with AVX2 (no native carry support).
 *
 * Future work: AVX-512 provides carry instructions that could benefit this code.
 *
 * Runtime dispatch and CPU detection remain active for when SIMD optimization
 * becomes feasible.
 *
 * Key optimizations:
 * - Efficient carry chain operations using _addcarry_u64
 * - 128-bit multiplication with _umul128
 * - Runtime CPU feature detection (infrastructure for future SIMD)
 * - secp256k1 specific reduction using p = 2^256 - 2^32 - 977
 *   which means: a mod p = a_low + a_high * 0x1000003D1
 *
 * Based on VanitySearch ModMulK1 implementation
 */

#ifndef INTMOD_AVX2_H
#define INTMOD_AVX2_H

#include <stdint.h>
// Prevent adxintrin.h from being included to avoid conflicts with Int.h macros
#define _ADXINTRIN_H_INCLUDED
#define _X86GPRINTRIN_H_INCLUDED
#include <immintrin.h>
#undef _X86GPRINTRIN_H_INCLUDED
#undef _ADXINTRIN_H_INCLUDED
#include <cpuid.h>

#ifdef __cplusplus
extern "C" {
#endif

// Platform-specific XGETBV support for OS-level AVX enablement
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

/**
 * Check CPU support for AVX2
 *
 * @return 1 if AVX2 is supported and OS has enabled YMM state, 0 otherwise
 */
static inline int modmulk1_avx2_available(void) {
    unsigned int eax, ebx, ecx, edx;

    // Check for AVX2 support (CPUID function 7, subleaf 0, EBX bit 5)
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        if ((ebx & (1 << 5)) == 0) return 0;  // AVX2 bit not set
#if defined(__i386__) || defined(__x86_64__)
        return os_avx_enabled();
#else
        return 1;
#endif
    }
    return 0;
}

/**
 * Optimized 256x256 -> 512 bit multiplication with secp256k1 reduction
 *
 * Uses efficient scalar operations with _umul128 and _addcarry_u64.
 * AVX2 carry-chain optimization was evaluated but provides no performance
 * benefit without native carry instructions (available in AVX-512).
 *
 * Input: a[4], b[4] (256-bit integers as 4x64-bit words)
 * Output: r[4] (256-bit result, reduced mod secp256k1 prime)
 */
static inline void ModMulK1_avx2(const uint64_t *a, const uint64_t *b, uint64_t *r) {
    uint64_t t[5];
    uint64_t r512[8];
    uint64_t ah, al;
    unsigned char c;

    // Initialize high words to zero
    r512[5] = 0;
    r512[6] = 0;
    r512[7] = 0;

    // === 256*256 multiplication ===
    // Uses imm_umul pattern from Int.h: multiply 4-word array by single word,
    // producing 5-word result with a single carry chain per column.
    // Previous version split columns into sub-groups with separate carry chains,
    // which lost carries between sub-groups.
    {
        uint64_t h, carry;

        // Column 0: r512 = a * b[0] (produces r512[0..4])
        r512[0] = _umul128(a[0], b[0], &h); carry = h;
        c = _addcarry_u64(0, _umul128(a[1], b[0], &h), carry, &r512[1]); carry = h;
        c = _addcarry_u64(c, _umul128(a[2], b[0], &h), carry, &r512[2]); carry = h;
        c = _addcarry_u64(c, _umul128(a[3], b[0], &h), carry, &r512[3]); carry = h;
        _addcarry_u64(c, 0ULL, carry, &r512[4]);

        // Column 1: t = a * b[1], then r512[1..5] += t[0..4]
        t[0] = _umul128(a[0], b[1], &h); carry = h;
        c = _addcarry_u64(0, _umul128(a[1], b[1], &h), carry, &t[1]); carry = h;
        c = _addcarry_u64(c, _umul128(a[2], b[1], &h), carry, &t[2]); carry = h;
        c = _addcarry_u64(c, _umul128(a[3], b[1], &h), carry, &t[3]); carry = h;
        _addcarry_u64(c, 0ULL, carry, &t[4]);

        c = _addcarry_u64(0, r512[1], t[0], &r512[1]);
        c = _addcarry_u64(c, r512[2], t[1], &r512[2]);
        c = _addcarry_u64(c, r512[3], t[2], &r512[3]);
        c = _addcarry_u64(c, r512[4], t[3], &r512[4]);
        c = _addcarry_u64(c, r512[5], t[4], &r512[5]);

        // Column 2: t = a * b[2], then r512[2..6] += t[0..4]
        t[0] = _umul128(a[0], b[2], &h); carry = h;
        c = _addcarry_u64(0, _umul128(a[1], b[2], &h), carry, &t[1]); carry = h;
        c = _addcarry_u64(c, _umul128(a[2], b[2], &h), carry, &t[2]); carry = h;
        c = _addcarry_u64(c, _umul128(a[3], b[2], &h), carry, &t[3]); carry = h;
        _addcarry_u64(c, 0ULL, carry, &t[4]);

        c = _addcarry_u64(0, r512[2], t[0], &r512[2]);
        c = _addcarry_u64(c, r512[3], t[1], &r512[3]);
        c = _addcarry_u64(c, r512[4], t[2], &r512[4]);
        c = _addcarry_u64(c, r512[5], t[3], &r512[5]);
        c = _addcarry_u64(c, r512[6], t[4], &r512[6]);

        // Column 3: t = a * b[3], then r512[3..7] += t[0..4]
        t[0] = _umul128(a[0], b[3], &h); carry = h;
        c = _addcarry_u64(0, _umul128(a[1], b[3], &h), carry, &t[1]); carry = h;
        c = _addcarry_u64(c, _umul128(a[2], b[3], &h), carry, &t[2]); carry = h;
        c = _addcarry_u64(c, _umul128(a[3], b[3], &h), carry, &t[3]); carry = h;
        _addcarry_u64(c, 0ULL, carry, &t[4]);

        c = _addcarry_u64(0, r512[3], t[0], &r512[3]);
        c = _addcarry_u64(c, r512[4], t[1], &r512[4]);
        c = _addcarry_u64(c, r512[5], t[2], &r512[5]);
        c = _addcarry_u64(c, r512[6], t[3], &r512[6]);
        c = _addcarry_u64(c, r512[7], t[4], &r512[7]);
    }

    // === Reduction step 1: from 512 to 320 bits ===
    // Multiply high 256 bits (r512[4:7]) by 0x1000003D1 and add to low 256 bits.
    // secp256k1 prime: p = 2^256 - 0x1000003D1
    //
    // Uses single-pass multiplication matching the scalar fallback in IntMod.cpp.
    // Previous per-word reduction had carry propagation bugs.
    {
        uint64_t h, carry;
        uint64_t rt[5];
        unsigned char rc = 0;
        rt[0] = _umul128(r512[4], 0x1000003D1ULL, &h); carry = h;
        rc = _addcarry_u64(rc, _umul128(r512[5], 0x1000003D1ULL, &h), carry, &rt[1]); carry = h;
        rc = _addcarry_u64(rc, _umul128(r512[6], 0x1000003D1ULL, &h), carry, &rt[2]); carry = h;
        rc = _addcarry_u64(rc, _umul128(r512[7], 0x1000003D1ULL, &h), carry, &rt[3]); carry = h;
        _addcarry_u64(rc, 0ULL, carry, &rt[4]);

        c = _addcarry_u64(0, r512[0], rt[0], &r512[0]);
        c = _addcarry_u64(c, r512[1], rt[1], &r512[1]);
        c = _addcarry_u64(c, r512[2], rt[2], &r512[2]);
        c = _addcarry_u64(c, r512[3], rt[3], &r512[3]);

        // === Reduction step 2: from 320 to 256 bits ===
        // No overflow possible here: rt[4]+c <= 0x1000003D1ULL
        al = _umul128(rt[4] + c, 0x1000003D1ULL, &ah);
        c = _addcarry_u64(0, r512[0], al, &r[0]);
        c = _addcarry_u64(c, r512[1], ah, &r[1]);
        c = _addcarry_u64(c, r512[2], 0ULL, &r[2]);
        c = _addcarry_u64(c, r512[3], 0ULL, &r[3]);
    }

    // Probability of carry here or that result > P is very very unlikely
}

/**
 * Optimized 256-bit modular squaring for secp256k1
 *
 * For squaring, we use the multiplication with itself
 * This could be further optimized by exploiting a*a structure
 * but for simplicity and correctness, we use the general multiplication
 */
static inline void ModSquareK1_avx2(const uint64_t *a, uint64_t *r) {
    ModMulK1_avx2(a, a, r);
}

#ifdef __cplusplus
}
#endif

#endif /* INTMOD_AVX2_H */
