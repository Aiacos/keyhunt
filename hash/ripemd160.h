/*
 * This file is part of the VanitySearch distribution (https://github.com/JeanLucPons/VanitySearch).
 * Copyright (c) 2019 Jean Luc PONS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef RIPEMD160_H
#define RIPEMD160_H

#include <string>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

/** A hasher class for RIPEMD-160. */
class CRIPEMD160
{
private:
    uint32_t s[5];
    unsigned char buf[64];
    uint64_t bytes;

public:
    CRIPEMD160();
    void Write(const unsigned char* data, size_t len);
    void Finalize(unsigned char hash[20]);
};

#ifdef __cplusplus
extern "C" {
#endif

void ripemd160(unsigned char *input,int length,unsigned char *digest);
void ripemd160_32(const unsigned char *input, unsigned char *digest);

// SSE2 implementation (4-way parallel)
void ripemd160sse_32(const uint8_t *i0, const uint8_t *i1, const uint8_t *i2, const uint8_t *i3,
  uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3);
void ripemd160sse_test();

// AVX2 implementation (8-way parallel) - requires AVX2 support
void ripemd160avx2_32(const uint8_t *i0, const uint8_t *i1, const uint8_t *i2, const uint8_t *i3,
  const uint8_t *i4, const uint8_t *i5, const uint8_t *i6, const uint8_t *i7,
  uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
  uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7);
int ripemd160_avx2_available(void);
void ripemd160avx2_test();

// AVX-512 implementation (16-way parallel) - requires AVX-512F support
void ripemd160avx512_32(
  const uint8_t *i0,  const uint8_t *i1,  const uint8_t *i2,  const uint8_t *i3,
  const uint8_t *i4,  const uint8_t *i5,  const uint8_t *i6,  const uint8_t *i7,
  const uint8_t *i8,  const uint8_t *i9,  const uint8_t *i10, const uint8_t *i11,
  const uint8_t *i12, const uint8_t *i13, const uint8_t *i14, const uint8_t *i15,
  uint8_t *d0,  uint8_t *d1,  uint8_t *d2,  uint8_t *d3,
  uint8_t *d4,  uint8_t *d5,  uint8_t *d6,  uint8_t *d7,
  uint8_t *d8,  uint8_t *d9,  uint8_t *d10, uint8_t *d11,
  uint8_t *d12, uint8_t *d13, uint8_t *d14, uint8_t *d15);
int ripemd160_avx512_available(void);
void ripemd160avx512_test();

#ifdef __cplusplus
}
#endif

std::string ripemd160_hex(unsigned char *digest);

static inline bool ripemd160_comp_hash(uint8_t *h0, uint8_t *h1) {
  uint32_t *h0i = (uint32_t *)h0;
  uint32_t *h1i = (uint32_t *)h1;
  return (h0i[0] == h1i[0]) &&
    (h0i[1] == h1i[1]) &&
    (h0i[2] == h1i[2]) &&
    (h0i[3] == h1i[3]) &&
    (h0i[4] == h1i[4]);
}

#endif // RIPEMD160_H
