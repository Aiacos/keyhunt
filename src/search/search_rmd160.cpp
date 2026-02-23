/*
 * search_rmd160.cpp - RIPEMD160 hash mode search implementation
 *
 * MIGRATION STATUS: Config-aware (utility functions)
 *
 * This file implements the RMD160 search mode which searches for known
 * RIPEMD160 hashes (HASH160) derived from public keys. This mode is
 * also used by ADDRESS mode since both use the same HASH160 pipeline.
 *
 * Bitcoin HASH160 = RIPEMD160(SHA256(public_key))
 *
 * The code supports:
 * - Compressed public keys (33 bytes: 02/03 prefix + X)
 * - Uncompressed public keys (65 bytes: 04 prefix + X + Y)
 * - Endomorphism optimization (6x keys per EC operation)
 *
 * Current implementation:
 * - Contains pure utility functions for HASH160 checking
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
 * - Move these functions to a dedicated rmd160 module if RMD160-specific
 *   thread logic is extracted from thread_process()
 * - Optimize bloom filter batching for better cache utilization
 * - Consider AVX-512 optimizations for batch hash checking
 *
 * See search_common.h for shared declarations.
 */

#include "search_rmd160.h"
#include <string.h>

/* ============================================================================
 * External Dependencies
 * These are defined in keyhunt.cpp and linked at compile time
 * ============================================================================ */

/* Binary search function from keyhunt.cpp */
extern int searchbinary(struct address_value *buffer, char *data, int64_t array_length);

/* ============================================================================
 * RMD160 Search Implementation
 * ============================================================================ */

/*
 * Check a single RIPEMD160 hash against targets.
 * Uses bloom filter for O(1) negative lookup, binary search for confirmation.
 */
int rmd160_check_single(
    const unsigned char *hash,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count)
{
    /* Fast bloom filter check using specialized RMD160 function */
    int bloom_hit = bloom_ext_check_rmd160(bloom, hash);
    if (!bloom_hit) {
        return 0;
    }

    /* Bloom hit - confirm with binary search in sorted target array */
    return searchbinary(targets, (char *)hash, target_count);
}

/*
 * Check batch of compressed key hashes with endomorphism.
 */
int rmd160_check_compressed_endomorphism(
    char hashes[12][4][20],
    uint64_t /* j */,
    Int *key_mpz,
    Int *stride,
    Secp256K1 *secp,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count,
    Int *lambda,
    Int *lambda2,
    void (*writekey_fn)(bool, Int*))
{
    int matches = 0;
    Int keyfound;
    Point publickey;
    int r;

    for (int k = 0; k < 4; k++) {
        for (int l = 0; l < 6; l++) {
            r = bloom_ext_check_rmd160(bloom, (uint8_t*)hashes[l][k]);
            if (r) {
                r = searchbinary(targets, hashes[l][k], target_count);
                if (r) {
                    keyfound.SetInt32(k);
                    keyfound.Mult(stride);
                    keyfound.Add(key_mpz);
                    publickey = secp->ComputePublicKey(&keyfound);

                    switch (l) {
                        case 0:
                            if (publickey.y.IsOdd()) { keyfound.Neg(); keyfound.Add(&secp->order); }
                            break;
                        case 1:
                            if (publickey.y.IsEven()) { keyfound.Neg(); keyfound.Add(&secp->order); }
                            break;
                        case 2:
                            keyfound.ModMulK1order(lambda);
                            if (publickey.y.IsOdd()) { keyfound.Neg(); keyfound.Add(&secp->order); }
                            break;
                        case 3:
                            keyfound.ModMulK1order(lambda);
                            if (publickey.y.IsEven()) { keyfound.Neg(); keyfound.Add(&secp->order); }
                            break;
                        case 4:
                            keyfound.ModMulK1order(lambda2);
                            if (publickey.y.IsOdd()) { keyfound.Neg(); keyfound.Add(&secp->order); }
                            break;
                        case 5:
                            keyfound.ModMulK1order(lambda2);
                            if (publickey.y.IsEven()) { keyfound.Neg(); keyfound.Add(&secp->order); }
                            break;
                    }
                    writekey_fn(true, &keyfound);
                    matches++;
                }
            }
        }
    }
    return matches;
}

/*
 * Check batch of compressed key hashes without endomorphism.
 */
int rmd160_check_compressed_simple(
    char hashes[2][4][20],
    uint64_t /* j */,
    Int *key_mpz,
    Int *stride,
    Secp256K1 *secp,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count,
    void (*writekey_fn)(bool, Int*))
{
    int matches = 0;
    Int keyfound;
    Point publickey;
    char computed_hash[20];
    int r;

    for (int k = 0; k < 4; k++) {
        for (int l = 0; l < 2; l++) {
            r = bloom_ext_check_rmd160(bloom, (uint8_t*)hashes[l][k]);
            if (r) {
                r = searchbinary(targets, hashes[l][k], target_count);
                if (r) {
                    keyfound.SetInt32(k);
                    keyfound.Mult(stride);
                    keyfound.Add(key_mpz);
                    publickey = secp->ComputePublicKey(&keyfound);
                    secp->GetHash160(P2PKH, true, publickey, (uint8_t*)computed_hash);
                    if (memcmp(hashes[l][k], computed_hash, 20) != 0) {
                        keyfound.Neg();
                        keyfound.Add(&secp->order);
                    }
                    writekey_fn(true, &keyfound);
                    matches++;
                }
            }
        }
    }
    return matches;
}

/*
 * Check batch of uncompressed key hashes with endomorphism.
 */
int rmd160_check_uncompressed_endomorphism(
    char hashes[12][4][20],
    uint64_t /* j */,
    Int *key_mpz,
    Int *stride,
    Secp256K1 *secp,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count,
    Int *lambda,
    Int *lambda2,
    void (*writekey_fn)(bool, Int*))
{
    int matches = 0;
    Int keyfound;
    Point publickey;
    char computed_hash[20];
    int r;

    for (int k = 0; k < 4; k++) {
        for (int l = 6; l < 12; l++) {
            r = bloom_ext_check_rmd160(bloom, (uint8_t*)hashes[l][k]);
            if (r) {
                r = searchbinary(targets, hashes[l][k], target_count);
                if (r) {
                    keyfound.SetInt32(k);
                    keyfound.Mult(stride);
                    keyfound.Add(key_mpz);

                    if (l < 8) { /* Original point */ }
                    else if (l < 10) { keyfound.ModMulK1order(lambda); }
                    else { keyfound.ModMulK1order(lambda2); }

                    publickey = secp->ComputePublicKey(&keyfound);
                    secp->GetHash160(P2PKH, false, publickey, (uint8_t*)computed_hash);
                    if (memcmp(hashes[l][k], computed_hash, 20) != 0) {
                        keyfound.Neg();
                        keyfound.Add(&secp->order);
                    }
                    writekey_fn(false, &keyfound);
                    matches++;
                }
            }
        }
    }
    return matches;
}

/*
 * Check batch of uncompressed key hashes without endomorphism.
 */
int rmd160_check_uncompressed_simple(
    char hashes[4][20],
    uint64_t /* j */,
    Int *key_mpz,
    Int *stride,
    bloom_extended_t *bloom,
    struct address_value *targets,
    int64_t target_count,
    void (*writekey_fn)(bool, Int*))
{
    int matches = 0;
    Int keyfound;
    int r;

    for (int k = 0; k < 4; k++) {
        r = bloom_ext_check_rmd160(bloom, (uint8_t*)hashes[k]);
        if (r) {
            r = searchbinary(targets, hashes[k], target_count);
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
