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

#ifndef SHA512_H
#define SHA512_H
#include <string>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void sha512(unsigned char *input, int length, unsigned char *digest);
void pbkdf2_hmac_sha512(uint8_t *out, size_t outlen,const uint8_t *passwd, size_t passlen,const uint8_t *salt, size_t saltlen,uint64_t iter);
void hmac_sha512(unsigned char *key, int key_length, unsigned char *message, int message_length, unsigned char *digest);

// AVX2 implementation (8-way parallel) - requires AVX2 support
void sha512avx2_128(const uint8_t *i0, const uint8_t *i1, const uint8_t *i2, const uint8_t *i3,
  const uint8_t *i4, const uint8_t *i5, const uint8_t *i6, const uint8_t *i7,
  uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
  uint8_t *d4, uint8_t *d5, uint8_t *d6, uint8_t *d7);
int sha512_avx2_available(void);
void sha512avx2_test();

// AVX-512 implementation (16-way parallel) - requires AVX-512F support
void sha512avx512_128(
  const uint8_t *i0,  const uint8_t *i1,  const uint8_t *i2,  const uint8_t *i3,
  const uint8_t *i4,  const uint8_t *i5,  const uint8_t *i6,  const uint8_t *i7,
  const uint8_t *i8,  const uint8_t *i9,  const uint8_t *i10, const uint8_t *i11,
  const uint8_t *i12, const uint8_t *i13, const uint8_t *i14, const uint8_t *i15,
  uint8_t *d0,  uint8_t *d1,  uint8_t *d2,  uint8_t *d3,
  uint8_t *d4,  uint8_t *d5,  uint8_t *d6,  uint8_t *d7,
  uint8_t *d8,  uint8_t *d9,  uint8_t *d10, uint8_t *d11,
  uint8_t *d12, uint8_t *d13, uint8_t *d14, uint8_t *d15);
int sha512_avx512_available(void);
void sha512avx512_test();

#ifdef __cplusplus
}
#endif

std::string sha512_hex(unsigned char *digest);

#endif
