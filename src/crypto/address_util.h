/*
 * address_util.h - Cryptocurrency address generation and validation utilities
 *
 * This module provides functions for converting public keys to Bitcoin/Ethereum
 * addresses, Base58 validation, and specialized SHA256 operations for minikey
 * format support.
 *
 * Address generation pipeline (Bitcoin):
 * 1. SHA256(public_key)
 * 2. RIPEMD160(sha256_result) -> 20-byte hash
 * 3. Prepend version byte (0x00 for mainnet)
 * 4. Double SHA256 for checksum
 * 5. Base58Check encoding
 *
 * Ethereum address generation:
 * 1. KECCAK-256(uncompressed_pubkey_64bytes)
 * 2. Take last 20 bytes
 */

#ifndef CRYPTO_ADDRESS_UTIL_H
#define CRYPTO_ADDRESS_UTIL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "../secp256k1/Point.h"

/* C-linkage functions (pure C-compatible signatures) */
#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Bitcoin Address Generation
 * ============================================================================ */

char *pubkeytopubaddress(char *pkey, int length);
void pubkeytopubaddress_dst(char *pkey, int length, char *dst);
void rmd160toaddress_dst(char *rmd, char *dst);
void rmd160tobech32_dst(char *rmd, char *dst, int witness_version);

/* ============================================================================
 * KECCAK-256
 * ============================================================================ */

void KECCAK_256(uint8_t *source, size_t size, uint8_t *dst);

/* ============================================================================
 * Minikey SHA256 Operations
 * ============================================================================ */

void sha256sse_22(uint8_t *src0, uint8_t *src1, uint8_t *src2, uint8_t *src3,
                  uint8_t *dst0, uint8_t *dst1, uint8_t *dst2, uint8_t *dst3);

void sha256sse_23(uint8_t *src0, uint8_t *src1, uint8_t *src2, uint8_t *src3,
                  uint8_t *dst0, uint8_t *dst1, uint8_t *dst2, uint8_t *dst3);

/* ============================================================================
 * Base58 Validation
 * ============================================================================ */

bool isBase58(char c);
bool isValidBase58String(char *str);

/* ============================================================================
 * Bech32 Validation
 * ============================================================================ */

bool isBech32(char c);
bool isValidBech32String(char *str);

#ifdef __cplusplus
}
#endif

/* ============================================================================
 * C++ only: Ethereum Address Generation (uses Point& reference)
 * ============================================================================ */

/*
 * Generate a 20-byte Ethereum binary address from a public key point.
 * Concatenates x and y coordinates (64 bytes), applies KECCAK-256,
 * and takes the last 20 bytes.
 *
 * Parameters:
 *   publickey   - Elliptic curve point (public key)
 *   dst_address - Output buffer (20 bytes)
 */
void generate_binaddress_eth(Point &publickey, unsigned char *dst_address);

#endif /* CRYPTO_ADDRESS_UTIL_H */
