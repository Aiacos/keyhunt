/*
 * AVX2 optimized SHA-256 implementation
 * Processes 8 hashes in parallel (2x faster than SSE2)
 *
 * Based on VanitySearch by Jean Luc PONS
 * AVX2 optimization for keyhunt
 */

#ifndef SHA256_AVX2_H
#define SHA256_AVX2_H

#include <stdint.h>

// Check if AVX2 is available on this CPU
int sha256_avx2_available(void);

// AVX2 8-way parallel SHA256 functions
void sha256avx2_1B(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7);

void sha256avx2_2B(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7);

void sha256avx2_checksum(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7);

// Fused SHA256→RIPEMD160 for compressed keys (1-block SHA256)
// Eliminates intermediate buffer by keeping SHA256 output in registers
void sha256_ripemd160_avx2_1B(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7);

#endif // SHA256_AVX2_H
