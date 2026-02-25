/*
 * AVX-512 optimized SHA-512 implementation
 * Processes 8 hashes in parallel (64-bit operations)
 *
 * Based on VanitySearch by Jean Luc PONS
 * AVX-512 optimization for keyhunt
 */

#ifndef SHA512_AVX512_H
#define SHA512_AVX512_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Check if AVX-512F is available on this CPU
int sha512_avx512_available(void);

// AVX-512 8-way parallel SHA512 functions
void sha512avx512(
    uint64_t *i0, uint64_t *i1, uint64_t *i2, uint64_t *i3,
    uint64_t *i4, uint64_t *i5, uint64_t *i6, uint64_t *i7,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7,
    int length);

void sha512avx512_hmac(
    uint8_t *key0, uint8_t *key1, uint8_t *key2, uint8_t *key3,
    uint8_t *key4, uint8_t *key5, uint8_t *key6, uint8_t *key7,
    int key_length,
    uint8_t *msg0, uint8_t *msg1, uint8_t *msg2, uint8_t *msg3,
    uint8_t *msg4, uint8_t *msg5, uint8_t *msg6, uint8_t *msg7,
    int msg_length,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7);

// Test function
void sha512avx512_test(void);

#ifdef __cplusplus
}
#endif

#endif // SHA512_AVX512_H
