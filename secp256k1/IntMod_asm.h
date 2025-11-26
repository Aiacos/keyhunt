/*
 * Optimized Assembly Implementation for secp256k1 Modular Multiplication
 *
 * Key optimizations:
 * - All inline assembly to eliminate function call overhead
 * - Register-optimized multiplication cascade
 * - Minimized memory accesses
 * - secp256k1 specific reduction using p = 2^256 - 2^32 - 977
 *   which means: a mod p = a_low + a_high * 0x1000003D1
 */

#ifndef INTMOD_ASM_H
#define INTMOD_ASM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optimized 256x256 -> 512 bit multiplication with secp256k1 reduction
 *
 * This replaces ModMulK1 with pure inline assembly.
 * Input: a[4], b[4] (256-bit integers as 4x64-bit words)
 * Output: r[4] (256-bit result, reduced mod secp256k1 prime)
 */
static inline void ModMulK1_asm(const uint64_t *a, const uint64_t *b, uint64_t *r) {
    uint64_t r0, r1, r2, r3, r4, r5, r6, r7;
    uint64_t t0, t1, t2, t3, t4;
    uint64_t c;

    __asm__ __volatile__ (
        /* === First column: a * b[0] === */
        /* t0 = a[0] * b[0] */
        "movq (%[a]), %%rax\n\t"
        "mulq (%[b])\n\t"
        "movq %%rax, %[r0]\n\t"
        "movq %%rdx, %[r1]\n\t"

        /* t1 = a[1] * b[0] */
        "movq 8(%[a]), %%rax\n\t"
        "mulq (%[b])\n\t"
        "addq %%rax, %[r1]\n\t"
        "adcq $0, %%rdx\n\t"
        "movq %%rdx, %[r2]\n\t"

        /* t2 = a[2] * b[0] */
        "movq 16(%[a]), %%rax\n\t"
        "mulq (%[b])\n\t"
        "addq %%rax, %[r2]\n\t"
        "adcq $0, %%rdx\n\t"
        "movq %%rdx, %[r3]\n\t"

        /* t3 = a[3] * b[0] */
        "movq 24(%[a]), %%rax\n\t"
        "mulq (%[b])\n\t"
        "addq %%rax, %[r3]\n\t"
        "adcq $0, %%rdx\n\t"
        "movq %%rdx, %[r4]\n\t"

        /* === Second column: a * b[1] === */
        /* t0 = a[0] * b[1] */
        "movq (%[a]), %%rax\n\t"
        "mulq 8(%[b])\n\t"
        "addq %%rax, %[r1]\n\t"
        "adcq %%rdx, %[r2]\n\t"
        "adcq $0, %[r3]\n\t"
        "adcq $0, %[r4]\n\t"
        "movq $0, %[r5]\n\t"
        "adcq $0, %[r5]\n\t"

        /* t1 = a[1] * b[1] */
        "movq 8(%[a]), %%rax\n\t"
        "mulq 8(%[b])\n\t"
        "addq %%rax, %[r2]\n\t"
        "adcq %%rdx, %[r3]\n\t"
        "adcq $0, %[r4]\n\t"
        "adcq $0, %[r5]\n\t"

        /* t2 = a[2] * b[1] */
        "movq 16(%[a]), %%rax\n\t"
        "mulq 8(%[b])\n\t"
        "addq %%rax, %[r3]\n\t"
        "adcq %%rdx, %[r4]\n\t"
        "adcq $0, %[r5]\n\t"

        /* t3 = a[3] * b[1] */
        "movq 24(%[a]), %%rax\n\t"
        "mulq 8(%[b])\n\t"
        "addq %%rax, %[r4]\n\t"
        "adcq %%rdx, %[r5]\n\t"
        "movq $0, %[r6]\n\t"
        "adcq $0, %[r6]\n\t"

        /* === Third column: a * b[2] === */
        /* t0 = a[0] * b[2] */
        "movq (%[a]), %%rax\n\t"
        "mulq 16(%[b])\n\t"
        "addq %%rax, %[r2]\n\t"
        "adcq %%rdx, %[r3]\n\t"
        "adcq $0, %[r4]\n\t"
        "adcq $0, %[r5]\n\t"
        "adcq $0, %[r6]\n\t"

        /* t1 = a[1] * b[2] */
        "movq 8(%[a]), %%rax\n\t"
        "mulq 16(%[b])\n\t"
        "addq %%rax, %[r3]\n\t"
        "adcq %%rdx, %[r4]\n\t"
        "adcq $0, %[r5]\n\t"
        "adcq $0, %[r6]\n\t"

        /* t2 = a[2] * b[2] */
        "movq 16(%[a]), %%rax\n\t"
        "mulq 16(%[b])\n\t"
        "addq %%rax, %[r4]\n\t"
        "adcq %%rdx, %[r5]\n\t"
        "adcq $0, %[r6]\n\t"

        /* t3 = a[3] * b[2] */
        "movq 24(%[a]), %%rax\n\t"
        "mulq 16(%[b])\n\t"
        "addq %%rax, %[r5]\n\t"
        "adcq %%rdx, %[r6]\n\t"
        "movq $0, %[r7]\n\t"
        "adcq $0, %[r7]\n\t"

        /* === Fourth column: a * b[3] === */
        /* t0 = a[0] * b[3] */
        "movq (%[a]), %%rax\n\t"
        "mulq 24(%[b])\n\t"
        "addq %%rax, %[r3]\n\t"
        "adcq %%rdx, %[r4]\n\t"
        "adcq $0, %[r5]\n\t"
        "adcq $0, %[r6]\n\t"
        "adcq $0, %[r7]\n\t"

        /* t1 = a[1] * b[3] */
        "movq 8(%[a]), %%rax\n\t"
        "mulq 24(%[b])\n\t"
        "addq %%rax, %[r4]\n\t"
        "adcq %%rdx, %[r5]\n\t"
        "adcq $0, %[r6]\n\t"
        "adcq $0, %[r7]\n\t"

        /* t2 = a[2] * b[3] */
        "movq 16(%[a]), %%rax\n\t"
        "mulq 24(%[b])\n\t"
        "addq %%rax, %[r5]\n\t"
        "adcq %%rdx, %[r6]\n\t"
        "adcq $0, %[r7]\n\t"

        /* t3 = a[3] * b[3] */
        "movq 24(%[a]), %%rax\n\t"
        "mulq 24(%[b])\n\t"
        "addq %%rax, %[r6]\n\t"
        "adcq %%rdx, %[r7]\n\t"

        : [r0] "=&r" (r0), [r1] "=&r" (r1), [r2] "=&r" (r2), [r3] "=&r" (r3),
          [r4] "=&r" (r4), [r5] "=&r" (r5), [r6] "=&r" (r6), [r7] "=&r" (r7)
        : [a] "r" (a), [b] "r" (b)
        : "rax", "rdx", "cc", "memory"
    );

    /* === Reduction step 1: from 512 to 320 bits ===
     * High 256 bits (r4-r7) * 0x1000003D1, add to low 256 bits
     * secp256k1 prime: p = 2^256 - 0x1000003D1
     */
    __asm__ __volatile__ (
        /* Multiply r4 by 0x1000003D1 and add to r0-r1 */
        "movq $0x1000003D1, %%rcx\n\t"
        "movq %[r4], %%rax\n\t"
        "mulq %%rcx\n\t"
        "addq %%rax, %[r0]\n\t"
        "adcq %%rdx, %[r1]\n\t"
        "movq $0, %[t0]\n\t"
        "adcq $0, %[t0]\n\t"

        /* Multiply r5 by 0x1000003D1 and add to r1-r2 */
        "movq %[r5], %%rax\n\t"
        "mulq %%rcx\n\t"
        "addq %%rax, %[r1]\n\t"
        "adcq %%rdx, %[r2]\n\t"
        "adcq $0, %[t0]\n\t"
        "movq $0, %[t1]\n\t"
        "adcq $0, %[t1]\n\t"

        /* Multiply r6 by 0x1000003D1 and add to r2-r3 */
        "movq %[r6], %%rax\n\t"
        "mulq %%rcx\n\t"
        "addq %[t0], %[r2]\n\t"
        "adcq $0, %[t1]\n\t"
        "addq %%rax, %[r2]\n\t"
        "adcq %%rdx, %[r3]\n\t"
        "adcq $0, %[t1]\n\t"
        "movq $0, %[t2]\n\t"
        "adcq $0, %[t2]\n\t"

        /* Multiply r7 by 0x1000003D1 and add to r3 */
        "movq %[r7], %%rax\n\t"
        "mulq %%rcx\n\t"
        "addq %[t1], %[r3]\n\t"
        "adcq $0, %[t2]\n\t"
        "addq %%rax, %[r3]\n\t"
        "adcq %%rdx, %[t2]\n\t"

        : [r0] "+&r" (r0), [r1] "+&r" (r1), [r2] "+&r" (r2), [r3] "+&r" (r3),
          [t0] "=&r" (t0), [t1] "=&r" (t1), [t2] "=&r" (t2)
        : [r4] "r" (r4), [r5] "r" (r5), [r6] "r" (r6), [r7] "r" (r7)
        : "rax", "rcx", "rdx", "cc"
    );

    /* === Reduction step 2: from 320 to 256 bits ===
     * If there's any overflow in t2, multiply by 0x1000003D1 and add
     */
    __asm__ __volatile__ (
        "movq $0x1000003D1, %%rcx\n\t"
        "movq %[t2], %%rax\n\t"
        "mulq %%rcx\n\t"
        "addq %%rax, %[r0]\n\t"
        "adcq %%rdx, %[r1]\n\t"
        "adcq $0, %[r2]\n\t"
        "adcq $0, %[r3]\n\t"

        : [r0] "+&r" (r0), [r1] "+&r" (r1), [r2] "+&r" (r2), [r3] "+&r" (r3)
        : [t2] "r" (t2)
        : "rax", "rcx", "rdx", "cc"
    );

    /* Store result */
    r[0] = r0;
    r[1] = r1;
    r[2] = r2;
    r[3] = r3;
}

/*
 * Optimized 256-bit modular squaring for secp256k1
 * Simplified implementation to avoid register pressure
 */
static inline void ModSquareK1_asm(const uint64_t *a, uint64_t *r) {
    /* For squaring, just use multiplication with itself */
    ModMulK1_asm(a, a, r);
}

#ifdef __cplusplus
}
#endif

#endif /* INTMOD_ASM_H */
