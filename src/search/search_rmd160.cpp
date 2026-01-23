/*
 * search_rmd160.cpp - RIPEMD160 hash mode search implementation
 *
 * This file is a placeholder for future refactoring. The RMD160 mode
 * is currently handled within thread_process() in keyhunt.cpp.
 *
 * RMD160 mode searches for known RIPEMD160 hashes (HASH160) instead
 * of full Bitcoin addresses. This skips Base58Check encoding.
 *
 * Use cases:
 * - When you have the HASH160 from a scriptPubKey
 * - Faster than address mode (no Base58Check)
 * - Same pipeline as address mode otherwise
 *
 * See search_common.h for shared declarations.
 */

#include "search_common.h"

/*
 * RMD160 Mode Details:
 *
 * Bitcoin HASH160 = RIPEMD160(SHA256(public_key))
 *
 * This is the raw 20-byte hash before Base58Check encoding to create
 * a human-readable address.
 *
 * Pipeline:
 * 1. Generate candidate private key
 * 2. Compute public key (compressed or uncompressed)
 * 3. SHA256 hash of public key
 * 4. RIPEMD160 hash of SHA256 result
 * 5. Compare 20-byte hash with targets
 *
 * Bloom filter provides O(1) lookup with configurable false positive rate.
 * Binary search confirms positive hits.
 *
 * NOTE: Implementation currently in keyhunt.cpp (MODE_RMD160 branch)
 */

/* Placeholder - actual implementation in keyhunt.cpp thread_process() */
