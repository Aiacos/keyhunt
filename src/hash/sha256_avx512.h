/*
 * AVX-512 optimized SHA-256 implementation
 * Processes 16 hashes in parallel (2x faster than AVX2)
 *
 * Based on VanitySearch by Jean Luc PONS
 * AVX-512 optimization for keyhunt
 */

#ifndef SHA256_AVX512_H
#define SHA256_AVX512_H

#include <stdint.h>

// Check if AVX-512 is available on this CPU
int sha256_avx512_available(void);

// AVX-512 16-way parallel SHA256 functions
void sha256avx512_1B(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint32_t *i8, uint32_t *i9, uint32_t *i10, uint32_t *i11,
    uint32_t *i12, uint32_t *i13, uint32_t *i14, uint32_t *i15,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7,
    uint8_t *d8, uint8_t *d9, uint8_t *d10, uint8_t *d11,
    uint8_t *d12, uint8_t *d13, uint8_t *d14, uint8_t *d15);

void sha256avx512_2B(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint32_t *i8, uint32_t *i9, uint32_t *i10, uint32_t *i11,
    uint32_t *i12, uint32_t *i13, uint32_t *i14, uint32_t *i15,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7,
    uint8_t *d8, uint8_t *d9, uint8_t *d10, uint8_t *d11,
    uint8_t *d12, uint8_t *d13, uint8_t *d14, uint8_t *d15);

void sha256avx512_checksum(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint32_t *i4, uint32_t *i5, uint32_t *i6, uint32_t *i7,
    uint32_t *i8, uint32_t *i9, uint32_t *i10, uint32_t *i11,
    uint32_t *i12, uint32_t *i13, uint32_t *i14, uint32_t *i15,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7,
    uint8_t *d8, uint8_t *d9, uint8_t *d10, uint8_t *d11,
    uint8_t *d12, uint8_t *d13, uint8_t *d14, uint8_t *d15);

#endif // SHA256_AVX512_H
