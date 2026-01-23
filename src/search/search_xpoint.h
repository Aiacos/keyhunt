/*
 * search_xpoint.h - X-Point mode search implementation
 *
 * XPOINT mode searches for public key X-coordinates directly without
 * computing the full address hash. This is the fastest search mode when
 * the target public key X-coordinate is known (e.g., from Bitcoin puzzles).
 *
 * Pipeline:
 * 1. Generate candidate private key
 * 2. Compute public key point (x, y)
 * 3. Compare x-coordinate with target(s)
 *
 * Advantages over ADDRESS/RMD160 modes:
 * - Skips SHA256 hashing
 * - Skips RIPEMD160 hashing
 * - Direct 32-byte comparison
 * - Best cache utilization
 */

#ifndef SEARCH_XPOINT_H
#define SEARCH_XPOINT_H

#include <stdint.h>
#include <stdbool.h>

#include "../secp256k1/Point.h"
#include "../secp256k1/Int.h"
#include "../bloom/bloom_wrapper.h"

/* Forward declarations for types defined in keyhunt.cpp */
struct address_value;

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * XPOINT Mode Configuration
 * ============================================================================ */

#define XPOINT_COMPARISON_LENGTH 32  /* X-coordinate is 32 bytes */

/* ============================================================================
 * XPOINT Search Functions
 * ============================================================================ */

/*
 * Check a single X-coordinate against targets using bloom filter and binary search.
 *
 * Parameters:
 *   x_coord       - Pointer to 32-byte X-coordinate (big-endian)
 *   bloom         - Bloom filter for fast negative lookups
 *   targets       - Sorted array of target values
 *   target_count  - Number of targets in array
 *   max_length    - Maximum length of address data (MAXLENGTHADDRESS)
 *
 * Returns:
 *   1 if match found, 0 otherwise
 */
int xpoint_check_single(
    const unsigned char *x_coord,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count,
    int max_length
);

/*
 * Check X-coordinate with endomorphism variants.
 * Checks the original point plus beta and beta^2 transformed points.
 *
 * Parameters:
 *   pts              - Array of 4 original points
 *   beta_pts         - Array of 4 beta-transformed points (x * beta mod p)
 *   beta2_pts        - Array of 4 beta^2-transformed points (x * beta^2 mod p)
 *   j                - Batch index (0 to CPU_GRP_SIZE/4 - 1)
 *   key_mpz          - Base private key for this batch
 *   stride           - Step size between consecutive keys
 *   bloom            - Bloom filter for fast negative lookups
 *   targets          - Sorted array of target values
 *   target_count     - Number of targets in array
 *   max_length       - Maximum length of address data
 *   lambda           - Endomorphism scalar lambda
 *   lambda2          - Endomorphism scalar lambda^2
 *   writekey_fn      - Callback function to write found key
 *
 * Returns:
 *   Number of matches found
 */
int xpoint_check_batch_endomorphism(
    Point *pts,
    Point *beta_pts,
    Point *beta2_pts,
    uint64_t j,
    Int *key_mpz,
    Int *stride,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count,
    int max_length,
    Int *lambda,
    Int *lambda2,
    void (*writekey_fn)(bool, Int*)
);

/*
 * Check batch of 4 X-coordinates without endomorphism.
 *
 * Parameters:
 *   pts           - Array of points to check (pts[j*4] to pts[j*4+3])
 *   j             - Batch index
 *   key_mpz       - Base private key
 *   stride        - Step size between consecutive keys
 *   bloom         - Bloom filter
 *   targets       - Target array
 *   target_count  - Number of targets
 *   max_length    - Maximum address length
 *   writekey_fn   - Key write callback
 *
 * Returns:
 *   Number of matches found
 */
int xpoint_check_batch_simple(
    Point *pts,
    uint64_t j,
    Int *key_mpz,
    Int *stride,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count,
    int max_length,
    void (*writekey_fn)(bool, Int*)
);

#ifdef __cplusplus
}
#endif

#endif /* SEARCH_XPOINT_H */
