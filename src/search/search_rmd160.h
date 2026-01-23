/*
 * search_rmd160.h - RIPEMD160 hash mode search implementation
 *
 * RMD160 mode searches for known RIPEMD160 hashes (HASH160) instead of
 * full Bitcoin addresses. This skips Base58Check encoding compared to
 * ADDRESS mode but uses the same hashing pipeline.
 *
 * Pipeline:
 * 1. Generate candidate private key
 * 2. Compute public key (compressed or uncompressed)
 * 3. SHA256 hash of public key
 * 4. RIPEMD160 hash of SHA256 result (HASH160)
 * 5. Compare 20-byte hash with targets via bloom filter + binary search
 *
 * Use cases:
 * - When you have the HASH160 from a scriptPubKey
 * - Faster than address mode (no Base58Check encode/compare)
 * - Same cryptographic pipeline as address mode
 *
 * The RMD160 checking logic is shared with ADDRESS mode since both
 * use HASH160 = RIPEMD160(SHA256(pubkey)).
 */

#ifndef SEARCH_RMD160_H
#define SEARCH_RMD160_H

#include <stdint.h>
#include <stdbool.h>

#include "../secp256k1/SECP256k1.h"
#include "../secp256k1/Point.h"
#include "../secp256k1/Int.h"
#include "../bloom/bloom_wrapper.h"

/* Forward declarations */
struct address_value;

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * RMD160 Mode Constants
 * ============================================================================ */

#define RMD160_HASH_LENGTH 20  /* RIPEMD160 output is 20 bytes */

/* ============================================================================
 * RMD160/ADDRESS Mode Search Functions
 * ============================================================================ */

/*
 * Check a single RIPEMD160 hash against targets.
 *
 * Parameters:
 *   hash          - Pointer to 20-byte RIPEMD160 hash
 *   bloom         - Bloom filter for fast negative lookups
 *   targets       - Sorted array of target hashes
 *   target_count  - Number of targets
 *
 * Returns:
 *   1 if match found, 0 otherwise
 */
int rmd160_check_single(
    const unsigned char *hash,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count
);

/*
 * Check batch of compressed key hashes with endomorphism.
 * Checks 6 variants per point: original + beta + beta^2, each with 02/03 prefix.
 *
 * Parameters:
 *   hashes        - Array of [12][4][20] hashes (6 variants x 2 parities x 4 points)
 *   j             - Batch index
 *   key_mpz       - Base private key
 *   stride        - Step between keys
 *   secp          - secp256k1 context
 *   bloom         - Bloom filter
 *   targets       - Target array
 *   target_count  - Number of targets
 *   lambda        - Endomorphism lambda
 *   lambda2       - Endomorphism lambda^2
 *   writekey_fn   - Callback to write found key
 *
 * Returns:
 *   Number of matches found
 */
int rmd160_check_compressed_endomorphism(
    char hashes[12][4][20],
    uint64_t j,
    Int *key_mpz,
    Int *stride,
    Secp256K1 *secp,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count,
    Int *lambda,
    Int *lambda2,
    void (*writekey_fn)(bool, Int*)
);

/*
 * Check batch of compressed key hashes without endomorphism.
 * Checks only 02/03 prefix variants.
 *
 * Parameters:
 *   hashes        - Array of [2][4][20] hashes (2 parities x 4 points)
 *   j             - Batch index
 *   key_mpz       - Base private key
 *   stride        - Step between keys
 *   secp          - secp256k1 context
 *   bloom         - Bloom filter
 *   targets       - Target array
 *   target_count  - Number of targets
 *   writekey_fn   - Callback to write found key
 *
 * Returns:
 *   Number of matches found
 */
int rmd160_check_compressed_simple(
    char hashes[2][4][20],
    uint64_t j,
    Int *key_mpz,
    Int *stride,
    Secp256K1 *secp,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count,
    void (*writekey_fn)(bool, Int*)
);

/*
 * Check batch of uncompressed key hashes with endomorphism.
 * Checks 6 variants: original + beta + beta^2, each with positive/negative Y.
 *
 * Parameters:
 *   hashes        - Array of [12][4][20] hashes
 *   j             - Batch index
 *   key_mpz       - Base private key
 *   stride        - Step between keys
 *   secp          - secp256k1 context
 *   bloom         - Bloom filter
 *   targets       - Target array
 *   target_count  - Number of targets
 *   lambda        - Endomorphism lambda
 *   lambda2       - Endomorphism lambda^2
 *   writekey_fn   - Callback to write found key
 *
 * Returns:
 *   Number of matches found
 */
int rmd160_check_uncompressed_endomorphism(
    char hashes[12][4][20],
    uint64_t j,
    Int *key_mpz,
    Int *stride,
    Secp256K1 *secp,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count,
    Int *lambda,
    Int *lambda2,
    void (*writekey_fn)(bool, Int*)
);

/*
 * Check batch of uncompressed key hashes without endomorphism.
 *
 * Parameters:
 *   hashes        - Array of [4][20] hashes for 4 points
 *   j             - Batch index
 *   key_mpz       - Base private key
 *   stride        - Step between keys
 *   bloom         - Bloom filter
 *   targets       - Target array
 *   target_count  - Number of targets
 *   writekey_fn   - Callback to write found key
 *
 * Returns:
 *   Number of matches found
 */
int rmd160_check_uncompressed_simple(
    char hashes[4][20],
    uint64_t j,
    Int *key_mpz,
    Int *stride,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count,
    void (*writekey_fn)(bool, Int*)
);

#ifdef __cplusplus
}
#endif

#endif /* SEARCH_RMD160_H */
