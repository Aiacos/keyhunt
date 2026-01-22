#ifndef RIPEMD160_AVX512_H
#define RIPEMD160_AVX512_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Check if AVX-512F is available on the CPU
int ripemd160_avx512_available(void);

// Process 16 RIPEMD-160 hashes in parallel using AVX-512
// Each input is 32 bytes, each output is 20 bytes
void ripemd160avx512_32(
    const unsigned char *i0,  const unsigned char *i1,  const unsigned char *i2,  const unsigned char *i3,
    const unsigned char *i4,  const unsigned char *i5,  const unsigned char *i6,  const unsigned char *i7,
    const unsigned char *i8,  const unsigned char *i9,  const unsigned char *i10, const unsigned char *i11,
    const unsigned char *i12, const unsigned char *i13, const unsigned char *i14, const unsigned char *i15,
    unsigned char *d0,  unsigned char *d1,  unsigned char *d2,  unsigned char *d3,
    unsigned char *d4,  unsigned char *d5,  unsigned char *d6,  unsigned char *d7,
    unsigned char *d8,  unsigned char *d9,  unsigned char *d10, unsigned char *d11,
    unsigned char *d12, unsigned char *d13, unsigned char *d14, unsigned char *d15);

// Test function
void ripemd160avx512_test(void);

#ifdef __cplusplus
}
#endif

#endif // RIPEMD160_AVX512_H
