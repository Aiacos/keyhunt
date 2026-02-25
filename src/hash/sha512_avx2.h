/*
 * AVX2 optimized SHA-512 implementation
 * Processes 4 hashes in parallel (64-bit operations)
 *
 * Based on VanitySearch by Jean Luc PONS
 * AVX2 optimization for keyhunt
 */

#ifndef SHA512_AVX2_H
#define SHA512_AVX2_H

#include <stdint.h>

// Check if AVX2 is available on this CPU
int sha512_avx2_available(void);

// AVX2 4-way parallel SHA512 functions
void sha512avx2(
    uint64_t *i0, uint64_t *i1, uint64_t *i2, uint64_t *i3,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    int length);

void sha512avx2_hmac(
    uint8_t *key0, uint8_t *key1, uint8_t *key2, uint8_t *key3,
    int key_length,
    uint8_t *msg0, uint8_t *msg1, uint8_t *msg2, uint8_t *msg3,
    int msg_length,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3);

#endif // SHA512_AVX2_H
