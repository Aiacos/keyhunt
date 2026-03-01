/*
 * search_xpoint.cpp - X-Point mode search implementation
 *
 * MIGRATION STATUS: Config-aware (utility functions)
 *
 * This file implements the XPOINT search mode which compares public key
 * X-coordinates directly against target values. This is the fastest
 * search mode when the target public key is known.
 *
 * The XPOINT mode is ideal for Bitcoin puzzle solving where the public
 * key X-coordinate is published but the private key is unknown.
 *
 * Performance characteristics:
 * - No hashing required (no SHA256, no RIPEMD160)
 * - Direct 32-byte memory comparison
 * - Excellent cache locality
 * - Approximately 2-3x faster than ADDRESS/RMD160 modes
 *
 * Current implementation:
 * - Contains pure utility functions (xpoint_check_single, xpoint_check_batch_*)
 * - Functions accept all dependencies as parameters (no global access)
 * - Called from thread functions in keyhunt.cpp
 * - Functions are already properly structured and config-ready
 *
 * These functions don't access global variables directly - they receive:
 * - bloom filter pointer
 * - target array pointer
 * - configuration values (key, stride, etc.) as parameters
 * - callback function pointer for writing found keys
 *
 * Migration notes:
 * - These functions are already well-structured and don't need modification
 * - They are called from thread_process() which will be migrated to accept
 *   thread_args struct with config pointer
 * - Once thread_process() is migrated, these functions will indirectly use
 *   config values (passed as parameters from config-aware caller)
 *
 * Future enhancements:
 * - Move these functions to a dedicated xpoint module if XPOINT-specific
 *   thread logic is extracted from thread_process()
 * - Add SIMD-optimized batch checking for multiple targets
 * - Consider GPU acceleration for X-coordinate comparison
 *
 * See search_common.h for shared declarations.
 */

#include "search_xpoint.h"
#include <string.h>

/* Binary search for address/hash targets (from sort module) */
#include "../sort/sort.h"

/* ============================================================================
 * XPOINT Search Implementation
 * ============================================================================ */

/*
 * Check a single X-coordinate against targets.
 * Uses bloom filter for fast negative check, then binary search for confirmation.
 */
int xpoint_check_single(
    const unsigned char *x_coord,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count,
    int max_length)
{
    /* Fast bloom filter check - most checks will fail here */
    int bloom_hit = bloom_ext_check(bloom, (const char *)x_coord, max_length);
    if (!bloom_hit) {
        return 0;
    }

    /* Bloom hit - confirm with binary search */
    return searchbinary(targets, (char *)x_coord, target_count);
}

/*
 * Check batch of 4 X-coordinates with endomorphism optimization.
 *
 * For secp256k1, given a point P = (x, y), the endomorphism allows computing
 * related points efficiently:
 *   P' = lambda * P = (x * beta mod p, y)
 *   P'' = lambda^2 * P = (x * beta^2 mod p, y)
 *
 * This lets us check 3 related keys per ECC operation, effectively tripling
 * the search rate for XPOINT mode when searching the full curve.
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
    void (*writekey_fn)(bool, Int*))
{
    int matches = 0;
    char rawvalue[32];
    Int keyfound;
    int r;

    for (int k = 0; k < 4; k++) {
        /* Check original point X-coordinate */
        pts[(4*j)+k].x.Get32Bytes((unsigned char *)rawvalue);
        r = bloom_ext_check(bloom, rawvalue, max_length);
        if (r) {
            r = searchbinary(targets, rawvalue, target_count);
            if (r) {
                keyfound.SetInt32(k);
                keyfound.Mult(stride);
                keyfound.Add(key_mpz);
                writekey_fn(false, &keyfound);
                matches++;
            }
        }

        /* Check beta-transformed point: key * lambda */
        beta_pts[(j*4)+k].x.Get32Bytes((unsigned char *)rawvalue);
        r = bloom_ext_check(bloom, rawvalue, max_length);
        if (r) {
            r = searchbinary(targets, rawvalue, target_count);
            if (r) {
                keyfound.SetInt32(k);
                keyfound.Mult(stride);
                keyfound.Add(key_mpz);
                keyfound.ModMulK1order(lambda);
                writekey_fn(false, &keyfound);
                matches++;
            }
        }

        /* Check beta^2-transformed point: key * lambda^2 */
        beta2_pts[(j*4)+k].x.Get32Bytes((unsigned char *)rawvalue);
        r = bloom_ext_check(bloom, rawvalue, max_length);
        if (r) {
            r = searchbinary(targets, rawvalue, target_count);
            if (r) {
                keyfound.SetInt32(k);
                keyfound.Mult(stride);
                keyfound.Add(key_mpz);
                keyfound.ModMulK1order(lambda2);
                writekey_fn(false, &keyfound);
                matches++;
            }
        }
    }

    return matches;
}

/*
 * Check batch of 4 X-coordinates without endomorphism.
 * Simpler path used when endomorphism is disabled or when searching
 * specific key ranges (puzzle mode).
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
    void (*writekey_fn)(bool, Int*))
{
    int matches = 0;
    char rawvalue[32];
    Int keyfound;
    int r;

    for (int k = 0; k < 4; k++) {
        pts[(4*j)+k].x.Get32Bytes((unsigned char *)rawvalue);
        r = bloom_ext_check(bloom, rawvalue, max_length);
        if (r) {
            r = searchbinary(targets, rawvalue, target_count);
            if (r) {
                keyfound.SetInt32(k);
                keyfound.Mult(stride);
                keyfound.Add(key_mpz);
                writekey_fn(false, &keyfound);
                matches++;
            }
        }
    }

    return matches;
}
