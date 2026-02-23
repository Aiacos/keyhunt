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

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Bitcoin Address Generation
 * ============================================================================ */

/*
 * Convert a public key to a Bitcoin address (Base58Check encoded).
 * Caller must free() the returned string.
 *
 * Parameters:
 *   pkey   - Public key bytes (compressed 33 or uncompressed 65 bytes)
 *   length - Length of public key in bytes
 *
 * Returns:
 *   Newly allocated Base58Check encoded address string (caller must free)
 */
char *pubkeytopubaddress(char *pkey, int length);

/*
 * Convert a public key to a Bitcoin address, writing to caller-provided buffer.
 *
 * Parameters:
 *   pkey   - Public key bytes
 *   length - Length of public key in bytes
 *   dst    - Output buffer (must be at least 40 bytes)
 */
void pubkeytopubaddress_dst(char *pkey, int length, char *dst);

/*
 * Convert a RIPEMD160 hash to a Bitcoin address, writing to caller-provided buffer.
 * Uses the global byte_encode_crypto as the version byte.
 *
 * Parameters:
 *   rmd - 20-byte RIPEMD160 hash
 *   dst - Output buffer (must be at least 40 bytes)
 */
void rmd160toaddress_dst(char *rmd, char *dst);

/* ============================================================================
 * Ethereum Address Generation
 * ============================================================================ */

/*
 * Compute KECCAK-256 hash (Ethereum variant of SHA3-256).
 *
 * Parameters:
 *   source - Input data
 *   size   - Input data length in bytes
 *   dst    - Output buffer (32 bytes)
 */
void KECCAK_256(uint8_t *source, size_t size, uint8_t *dst);

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

/* ============================================================================
 * Minikey SHA256 Operations
 * ============================================================================ */

/*
 * SSE-accelerated SHA256 for 22-byte minikey inputs (4-way parallel).
 * Used for minikey format validation and key derivation.
 *
 * Parameters:
 *   src0..src3 - Four 22-byte input buffers
 *   dst0..dst3 - Four 32-byte output buffers
 */
void sha256sse_22(uint8_t *src0, uint8_t *src1, uint8_t *src2, uint8_t *src3,
                  uint8_t *dst0, uint8_t *dst1, uint8_t *dst2, uint8_t *dst3);

/*
 * SSE-accelerated SHA256 for 23-byte minikey check inputs (4-way parallel).
 * Used for minikey validity check (append '?' and hash).
 *
 * Parameters:
 *   src0..src3 - Four 23-byte input buffers
 *   dst0..dst3 - Four 32-byte output buffers
 */
void sha256sse_23(uint8_t *src0, uint8_t *src1, uint8_t *src2, uint8_t *src3,
                  uint8_t *dst0, uint8_t *dst1, uint8_t *dst2, uint8_t *dst3);

/* ============================================================================
 * Base58 Validation
 * ============================================================================ */

/*
 * Check if a single character is valid Base58.
 *
 * Parameters:
 *   c - Character to check
 *
 * Returns:
 *   true if character is in the Base58 alphabet, false otherwise
 */
bool isBase58(char c);

/*
 * Validate that an entire string contains only Base58 characters.
 *
 * Parameters:
 *   str - Null-terminated string to validate
 *
 * Returns:
 *   true if all characters are valid Base58, false otherwise
 */
bool isValidBase58String(char *str);

#ifdef __cplusplus
}
#endif

#endif /* CRYPTO_ADDRESS_UTIL_H */
