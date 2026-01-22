/*
 * SHA-NI (SHA Extensions) optimized SHA-256 implementation
 * Uses Intel SHA Extensions for hardware-accelerated hashing
 *
 * Performance: ~3-4x faster than AVX2 implementation
 * Requires: Intel Ice Lake+, AMD Zen+ or newer CPUs
 */

#ifndef SHA256_SHANI_H
#define SHA256_SHANI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Check if SHA-NI is available on this CPU
int sha256_shani_available(void);

// Single message SHA256 using SHA-NI (fastest for single hash)
void sha256_shani(const uint8_t *input, size_t len, uint8_t *digest);

// Optimized for 33-byte input (compressed public key)
void sha256_shani_33(const uint8_t *input, uint8_t *digest);

// Optimized for 65-byte input (uncompressed public key)
void sha256_shani_65(const uint8_t *input, uint8_t *digest);

// 2-way parallel SHA256 using SHA-NI (processes 2 messages simultaneously)
// Uses interleaving of two SHA-NI pipelines for better throughput
void sha256_shani_2way(
    const uint8_t *i0, const uint8_t *i1,
    size_t len,
    uint8_t *d0, uint8_t *d1);

// 2-way parallel SHA256 for 33-byte inputs (compressed public keys)
void sha256_shani_2way_33(
    const uint8_t *i0, const uint8_t *i1,
    uint8_t *d0, uint8_t *d1);

// 4-way parallel SHA256 using 2x interleaved SHA-NI
// Best throughput for batch processing
void sha256_shani_4way_33(
    const uint32_t *i0, const uint32_t *i1,
    const uint32_t *i2, const uint32_t *i3,
    uint8_t *d0, uint8_t *d1,
    uint8_t *d2, uint8_t *d3);

void sha256_shani_4way_65(
    const uint32_t *i0, const uint32_t *i1,
    const uint32_t *i2, const uint32_t *i3,
    uint8_t *d0, uint8_t *d1,
    uint8_t *d2, uint8_t *d3);

// Test function
void sha256_shani_test(void);

#ifdef __cplusplus
}
#endif

#endif // SHA256_SHANI_H
