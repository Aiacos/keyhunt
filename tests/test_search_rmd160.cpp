/*
 * test_search_rmd160.cpp - Unit tests for RMD160 search mode
 *
 * Tests the RMD160 search mode which directly compares RIPEMD160 hashes
 * (HASH160 = RIPEMD160(SHA256(pubkey))) against targets. This is similar
 * to ADDRESS mode but skips Base58Check encoding, making it faster.
 *
 * Test coverage:
 * - Single RIPEMD160 hash checking
 * - Batch processing (compressed and uncompressed, with/without endomorphism)
 * - Bloom filter integration
 * - Binary search verification
 * - Edge cases and error conditions
 */

#include "test_framework.h"
#include "../src/search/search_rmd160.h"
#include "../src/secp256k1/Int.h"
#include "../src/secp256k1/Point.h"
#include "../src/secp256k1/SECP256k1.h"
#include "../src/bloom/bloom_wrapper.h"

#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Test Fixtures and Helpers
 * ============================================================================ */

/* Mock address_value structure for testing */
struct address_value {
    char address[64];
};

/* Global test data */
static struct address_value test_targets[10];
static bloom_extended_t test_bloom;
static int test_bloom_initialized = 0;
static Secp256K1 *test_secp = NULL;

/* Mock writekey function for testing */
static int writekey_called = 0;
static Int last_found_key;
static bool last_found_compressed = false;

void mock_writekey(bool compressed, Int *key) {
    writekey_called++;
    last_found_key = *key;
    last_found_compressed = compressed;
}

/* Mock binary search function (returns 1 if found, 0 otherwise) */
extern "C" int searchbinary(struct address_value *buffer, char *data, int64_t array_length) {
    /* Simple mock: compare first 20 bytes (RIPEMD160 size) of first target */
    if (array_length > 0 && memcmp(buffer[0].address, data, 20) == 0) {
        return 1;
    }
    return 0;
}

/* Setup function to initialize test bloom filter */
static void setup_test_bloom(void) {
    if (!test_bloom_initialized) {
        bloom_ext_init(&test_bloom, 1000, 0.01);
        test_bloom_initialized = 1;
    }
}

/* Setup secp256k1 context */
static void setup_test_secp(void) {
    if (test_secp == NULL) {
        test_secp = new Secp256K1();
        test_secp->Init();
    }
}

/* Cleanup function */
static void cleanup_test_bloom(void) {
    if (test_bloom_initialized) {
        bloom_ext_free(&test_bloom);
        test_bloom_initialized = 0;
    }
}

/* Cleanup secp256k1 */
static void cleanup_test_secp(void) {
    if (test_secp != NULL) {
        delete test_secp;
        test_secp = NULL;
    }
}

/* ============================================================================
 * Basic Configuration Tests
 * ============================================================================ */

TEST(rmd160_constant_check) {
    /* Verify RMD160 hash length is 20 bytes */
    ASSERT_EQ(20, RMD160_HASH_LENGTH);
}

TEST(rmd160_mock_writekey) {
    /* Test that mock writekey function works */
    writekey_called = 0;
    Int test_key;
    test_key.SetInt32(12345);

    mock_writekey(true, &test_key);

    ASSERT_EQ(1, writekey_called);
    ASSERT_EQ(12345, last_found_key.GetInt64());
    ASSERT_TRUE(last_found_compressed);
}

/* ============================================================================
 * Single Hash Check Tests
 * ============================================================================ */

TEST(rmd160_check_single_not_in_bloom) {
    setup_test_bloom();

    /* Create a test RIPEMD160 hash */
    unsigned char hash[20] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
        0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14
    };

    /* Hash not in bloom filter should return 0 quickly */
    int result = rmd160_check_single(hash, &test_bloom, test_targets, 1);

    ASSERT_EQ(0, result);
}

TEST(rmd160_check_single_in_bloom_not_in_targets) {
    setup_test_bloom();

    /* Create a test RIPEMD160 hash */
    unsigned char hash[20] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x11, 0x22, 0x33, 0x44
    };

    /* Add to bloom filter but not to targets */
    bloom_ext_add(&test_bloom, (const char *)hash, 20);

    /* Should hit bloom but fail binary search */
    int result = rmd160_check_single(hash, &test_bloom, test_targets, 1);

    ASSERT_EQ(0, result);
}

TEST(rmd160_check_single_match) {
    setup_test_bloom();

    /* Create a test RIPEMD160 hash that will match */
    unsigned char hash[20] = {
        0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88, 0x77, 0x66,
        0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0x01, 0x02, 0x03, 0x04
    };

    /* Set up target to match this hash */
    memcpy(test_targets[0].address, hash, 20);

    /* Add to bloom filter */
    bloom_ext_add(&test_bloom, (const char *)hash, 20);

    /* Should find match */
    int result = rmd160_check_single(hash, &test_bloom, test_targets, 1);

    ASSERT_EQ(1, result);
}

/* ============================================================================
 * Compressed Mode Tests - Simple (No Endomorphism)
 * ============================================================================ */

TEST(rmd160_compressed_simple_no_matches) {
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes [2][4][20] - 2 parities, 4 points */
    char hashes[2][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Fill with test data that won't match */
    for (int parity = 0; parity < 2; parity++) {
        for (int point = 0; point < 4; point++) {
            for (int byte = 0; byte < 20; byte++) {
                hashes[parity][point][byte] = (char)(parity * 100 + point * 10 + byte);
            }
        }
    }

    Int key_mpz;
    Int stride;
    key_mpz.SetInt32(1);
    stride.SetInt32(1);

    writekey_called = 0;

    /* Check batch - should find no matches */
    int matches = rmd160_check_compressed_simple(
        hashes,
        0,  /* j = 0 (first batch) */
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,  /* 1 target */
        mock_writekey
    );

    ASSERT_EQ(0, matches);
    ASSERT_EQ(0, writekey_called);
}

TEST(rmd160_compressed_simple_structure) {
    /* Test that batch function accepts correct parameters */
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes [2][4][20] - 2 parities, 4 points */
    char hashes[2][4][20];
    memset(hashes, 0, sizeof(hashes));

    Int key_mpz;
    Int stride;
    key_mpz.SetInt32(100);
    stride.SetInt32(10);

    writekey_called = 0;

    /* Should execute without crashing */
    int matches = rmd160_check_compressed_simple(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        mock_writekey
    );

    /* No matches expected, but function should complete */
    ASSERT_EQ(0, matches);
}

TEST(rmd160_compressed_simple_with_match) {
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes */
    char hashes[2][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Create a specific hash to match */
    unsigned char target_hash[20] = {
        0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89, 0x9A, 0xBC,
        0xDE, 0xF0, 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF
    };

    /* Place target hash at parity 1, point 2 */
    memcpy(hashes[1][2], target_hash, 20);

    /* Set up target */
    memcpy(test_targets[0].address, target_hash, 20);
    bloom_ext_add(&test_bloom, (const char *)target_hash, 20);

    Int key_mpz;
    Int stride;
    key_mpz.SetInt32(100);
    stride.SetInt32(1);

    writekey_called = 0;

    /* Check batch - should find 1 match */
    int matches = rmd160_check_compressed_simple(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        mock_writekey
    );

    ASSERT_EQ(1, matches);
    ASSERT_EQ(1, writekey_called);
}

TEST(rmd160_compressed_simple_multiple_matches) {
    setup_test_bloom();
    setup_test_secp();

    /* Create batch with multiple matching hashes */
    char hashes[2][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Create two target hashes */
    unsigned char target1[20] = {
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11
    };
    unsigned char target2[20] = {
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22
    };

    /* Place target hashes at parity 0 point 1 and parity 1 point 3 */
    memcpy(hashes[0][1], target1, 20);
    memcpy(hashes[1][3], target2, 20);

    /* Set up targets in bloom and array */
    memcpy(test_targets[0].address, target1, 20);
    memcpy(test_targets[1].address, target2, 20);
    bloom_ext_add(&test_bloom, (const char *)target1, 20);
    bloom_ext_add(&test_bloom, (const char *)target2, 20);

    Int key_mpz;
    Int stride;
    key_mpz.SetInt32(2000);
    stride.SetInt32(5);

    writekey_called = 0;

    /* Check batch - should find 2 matches */
    int matches = rmd160_check_compressed_simple(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        2,
        mock_writekey
    );

    ASSERT_EQ(2, matches);
    ASSERT_EQ(2, writekey_called);
}

/* ============================================================================
 * Compressed Mode Tests - Endomorphism
 * ============================================================================ */

TEST(rmd160_compressed_endomorphism_no_matches) {
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes [12][4][20] - 6 variants, 2 parities, 4 points */
    char hashes[12][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Fill with test data that won't match */
    for (int variant = 0; variant < 12; variant++) {
        for (int point = 0; point < 4; point++) {
            for (int byte = 0; byte < 20; byte++) {
                hashes[variant][point][byte] = (char)(variant * 20 + point * 5 + byte);
            }
        }
    }

    Int key_mpz, stride, lambda, lambda2;
    key_mpz.SetInt32(1000);
    stride.SetInt32(1);
    lambda.SetBase16((char *)"5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    lambda2.SetBase16((char *)"ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

    writekey_called = 0;

    /* Check batch - should find no matches */
    int matches = rmd160_check_compressed_endomorphism(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        &lambda,
        &lambda2,
        mock_writekey
    );

    ASSERT_EQ(0, matches);
    ASSERT_EQ(0, writekey_called);
}

TEST(rmd160_compressed_endomorphism_structure) {
    /* Test that endomorphism batch function accepts correct parameters */
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes [12][4][20] */
    char hashes[12][4][20];
    memset(hashes, 0, sizeof(hashes));

    Int key_mpz, stride, lambda, lambda2;
    key_mpz.SetInt32(100);
    stride.SetInt32(10);
    lambda.SetBase16((char *)"5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    lambda2.SetBase16((char *)"ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

    writekey_called = 0;

    /* Should execute without crashing */
    int matches = rmd160_check_compressed_endomorphism(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        &lambda,
        &lambda2,
        mock_writekey
    );

    /* No matches expected, but function should complete */
    ASSERT_EQ(0, matches);
}

TEST(rmd160_compressed_endomorphism_original_match) {
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes */
    char hashes[12][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Create a target hash for original point (variant 0, point 1) */
    unsigned char target_hash[20] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA,
        0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22, 0x33, 0x44
    };

    /* Place at variant 0 (original), point 1 */
    memcpy(hashes[0][1], target_hash, 20);

    /* Set up target */
    memcpy(test_targets[0].address, target_hash, 20);
    bloom_ext_add(&test_bloom, (const char *)target_hash, 20);

    Int key_mpz, stride, lambda, lambda2;
    key_mpz.SetInt32(2000);
    stride.SetInt32(1);
    lambda.SetBase16((char *)"5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    lambda2.SetBase16((char *)"ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

    writekey_called = 0;

    /* Check batch - should find 1 match */
    int matches = rmd160_check_compressed_endomorphism(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        &lambda,
        &lambda2,
        mock_writekey
    );

    ASSERT_EQ(1, matches);
    ASSERT_EQ(1, writekey_called);
}

TEST(rmd160_compressed_endomorphism_beta_match) {
    /* Test match on beta-transformed point */
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes */
    char hashes[12][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Create a target hash for beta point (variants 2-3 are beta with parities) */
    unsigned char target_hash[20] = {
        0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC,
        0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66
    };

    /* Place at variant 2 (beta, even parity), point 1 */
    memcpy(hashes[2][1], target_hash, 20);

    /* Set up target */
    memcpy(test_targets[0].address, target_hash, 20);
    bloom_ext_add(&test_bloom, (const char *)target_hash, 20);

    Int key_mpz, stride, lambda, lambda2;
    key_mpz.SetInt32(5000);
    stride.SetInt32(3);
    lambda.SetBase16((char *)"5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    lambda2.SetBase16((char *)"ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

    writekey_called = 0;

    /* Check batch - should find 1 match */
    int matches = rmd160_check_compressed_endomorphism(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        &lambda,
        &lambda2,
        mock_writekey
    );

    ASSERT_EQ(1, matches);
    ASSERT_EQ(1, writekey_called);
}

TEST(rmd160_compressed_endomorphism_beta2_match) {
    /* Test match on beta^2-transformed point */
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes */
    char hashes[12][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Create a target hash for beta2 point (variants 4-5 are beta2 with parities) */
    unsigned char target_hash[20] = {
        0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA
    };

    /* Place at variant 4 (beta2, even parity), point 3 */
    memcpy(hashes[4][3], target_hash, 20);

    /* Set up target */
    memcpy(test_targets[0].address, target_hash, 20);
    bloom_ext_add(&test_bloom, (const char *)target_hash, 20);

    Int key_mpz, stride, lambda, lambda2;
    key_mpz.SetInt32(8000);
    stride.SetInt32(1);
    lambda.SetBase16((char *)"5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    lambda2.SetBase16((char *)"ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

    writekey_called = 0;

    /* Check batch - should find 1 match */
    int matches = rmd160_check_compressed_endomorphism(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        &lambda,
        &lambda2,
        mock_writekey
    );

    ASSERT_EQ(1, matches);
    ASSERT_EQ(1, writekey_called);
}

TEST(rmd160_compressed_endomorphism_multiple_matches) {
    /* Test multiple matches across different variants */
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes */
    char hashes[12][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Create three target hashes for different variants */
    unsigned char target1[20] = {
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA
    };
    unsigned char target2[20] = {
        0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
        0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB
    };
    unsigned char target3[20] = {
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC
    };

    /* Place at: variant 0 (original) point 0, variant 3 (beta odd) point 2, variant 5 (beta2 odd) point 1 */
    memcpy(hashes[0][0], target1, 20);
    memcpy(hashes[3][2], target2, 20);
    memcpy(hashes[5][1], target3, 20);

    /* Set up targets */
    memcpy(test_targets[0].address, target1, 20);
    memcpy(test_targets[1].address, target2, 20);
    memcpy(test_targets[2].address, target3, 20);
    bloom_ext_add(&test_bloom, (const char *)target1, 20);
    bloom_ext_add(&test_bloom, (const char *)target2, 20);
    bloom_ext_add(&test_bloom, (const char *)target3, 20);

    Int key_mpz, stride, lambda, lambda2;
    key_mpz.SetInt32(9000);
    stride.SetInt32(1);
    lambda.SetBase16((char *)"5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    lambda2.SetBase16((char *)"ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

    writekey_called = 0;

    /* Check batch - should find 3 matches */
    int matches = rmd160_check_compressed_endomorphism(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        3,
        &lambda,
        &lambda2,
        mock_writekey
    );

    ASSERT_EQ(3, matches);
    ASSERT_EQ(3, writekey_called);
}

/* ============================================================================
 * Uncompressed Mode Tests - Simple (No Endomorphism)
 * ============================================================================ */

TEST(rmd160_uncompressed_simple_no_matches) {
    setup_test_bloom();

    /* Create batch of hashes [4][20] - 4 points */
    char hashes[4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Fill with test data that won't match */
    for (int point = 0; point < 4; point++) {
        for (int byte = 0; byte < 20; byte++) {
            hashes[point][byte] = (char)(point * 30 + byte);
        }
    }

    Int key_mpz;
    Int stride;
    key_mpz.SetInt32(3000);
    stride.SetInt32(1);

    writekey_called = 0;

    /* Check batch - should find no matches */
    int matches = rmd160_check_uncompressed_simple(
        hashes,
        0,
        &key_mpz,
        &stride,
        &test_bloom,
        test_targets,
        1,
        mock_writekey
    );

    ASSERT_EQ(0, matches);
    ASSERT_EQ(0, writekey_called);
}

TEST(rmd160_uncompressed_simple_structure) {
    /* Test that batch function accepts correct parameters */
    setup_test_bloom();

    /* Create batch of hashes [4][20] - 4 points */
    char hashes[4][20];
    memset(hashes, 0, sizeof(hashes));

    Int key_mpz;
    Int stride;
    key_mpz.SetInt32(100);
    stride.SetInt32(10);

    writekey_called = 0;

    /* Should execute without crashing */
    int matches = rmd160_check_uncompressed_simple(
        hashes,
        0,
        &key_mpz,
        &stride,
        &test_bloom,
        test_targets,
        1,
        mock_writekey
    );

    /* No matches expected, but function should complete */
    ASSERT_EQ(0, matches);
}

TEST(rmd160_uncompressed_simple_with_match) {
    setup_test_bloom();

    /* Create batch of hashes */
    char hashes[4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Create a target hash for point 3 */
    unsigned char target_hash[20] = {
        0xF0, 0xE1, 0xD2, 0xC3, 0xB4, 0xA5, 0x96, 0x87, 0x78, 0x69,
        0x5A, 0x4B, 0x3C, 0x2D, 0x1E, 0x0F, 0x00, 0x11, 0x22, 0x33
    };

    /* Place at point 3 */
    memcpy(hashes[3], target_hash, 20);

    /* Set up target */
    memcpy(test_targets[0].address, target_hash, 20);
    bloom_ext_add(&test_bloom, (const char *)target_hash, 20);

    Int key_mpz;
    Int stride;
    key_mpz.SetInt32(4000);
    stride.SetInt32(1);

    writekey_called = 0;

    /* Check batch - should find 1 match */
    int matches = rmd160_check_uncompressed_simple(
        hashes,
        0,
        &key_mpz,
        &stride,
        &test_bloom,
        test_targets,
        1,
        mock_writekey
    );

    ASSERT_EQ(1, matches);
    ASSERT_EQ(1, writekey_called);
}

TEST(rmd160_uncompressed_simple_multiple_matches) {
    setup_test_bloom();

    /* Create batch with multiple matching hashes */
    char hashes[4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Create two target hashes */
    unsigned char target1[20] = {
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55
    };
    unsigned char target2[20] = {
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66
    };

    /* Place target hashes at point 1 and point 2 */
    memcpy(hashes[1], target1, 20);
    memcpy(hashes[2], target2, 20);

    /* Set up targets in bloom and array */
    memcpy(test_targets[0].address, target1, 20);
    memcpy(test_targets[1].address, target2, 20);
    bloom_ext_add(&test_bloom, (const char *)target1, 20);
    bloom_ext_add(&test_bloom, (const char *)target2, 20);

    Int key_mpz;
    Int stride;
    key_mpz.SetInt32(4500);
    stride.SetInt32(5);

    writekey_called = 0;

    /* Check batch - should find 2 matches */
    int matches = rmd160_check_uncompressed_simple(
        hashes,
        0,
        &key_mpz,
        &stride,
        &test_bloom,
        test_targets,
        2,
        mock_writekey
    );

    ASSERT_EQ(2, matches);
    ASSERT_EQ(2, writekey_called);
}

/* ============================================================================
 * Uncompressed Mode Tests - Endomorphism
 * ============================================================================ */

TEST(rmd160_uncompressed_endomorphism_no_matches) {
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes [12][4][20] - 6 variants, 2 Y-parities, 4 points */
    char hashes[12][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Fill with test data that won't match */
    for (int variant = 0; variant < 12; variant++) {
        for (int point = 0; point < 4; point++) {
            for (int byte = 0; byte < 20; byte++) {
                hashes[variant][point][byte] = (char)(variant * 15 + point * 3 + byte);
            }
        }
    }

    Int key_mpz, stride, lambda, lambda2;
    key_mpz.SetInt32(5000);
    stride.SetInt32(1);
    lambda.SetBase16((char *)"5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    lambda2.SetBase16((char *)"ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

    writekey_called = 0;

    /* Check batch - should find no matches */
    int matches = rmd160_check_uncompressed_endomorphism(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        &lambda,
        &lambda2,
        mock_writekey
    );

    ASSERT_EQ(0, matches);
    ASSERT_EQ(0, writekey_called);
}

TEST(rmd160_uncompressed_endomorphism_structure) {
    /* Test that endomorphism batch function accepts correct parameters */
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes [12][4][20] */
    char hashes[12][4][20];
    memset(hashes, 0, sizeof(hashes));

    Int key_mpz, stride, lambda, lambda2;
    key_mpz.SetInt32(100);
    stride.SetInt32(10);
    lambda.SetBase16((char *)"5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    lambda2.SetBase16((char *)"ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

    writekey_called = 0;

    /* Should execute without crashing */
    int matches = rmd160_check_uncompressed_endomorphism(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        &lambda,
        &lambda2,
        mock_writekey
    );

    /* No matches expected, but function should complete */
    ASSERT_EQ(0, matches);
}

TEST(rmd160_uncompressed_endomorphism_original_match) {
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes */
    char hashes[12][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Create a target hash for original point (variant 0, point 2) */
    unsigned char target_hash[20] = {
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD,
        0xEE, 0xFF, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77
    };

    /* Place at variant 0 (original), point 2 */
    memcpy(hashes[0][2], target_hash, 20);

    /* Set up target */
    memcpy(test_targets[0].address, target_hash, 20);
    bloom_ext_add(&test_bloom, (const char *)target_hash, 20);

    Int key_mpz, stride, lambda, lambda2;
    key_mpz.SetInt32(5500);
    stride.SetInt32(1);
    lambda.SetBase16((char *)"5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    lambda2.SetBase16((char *)"ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

    writekey_called = 0;

    /* Check batch - should find 1 match */
    int matches = rmd160_check_uncompressed_endomorphism(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        &lambda,
        &lambda2,
        mock_writekey
    );

    ASSERT_EQ(1, matches);
    ASSERT_EQ(1, writekey_called);
}

TEST(rmd160_uncompressed_endomorphism_beta_match) {
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes */
    char hashes[12][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Create a target hash for beta-transformed point (variant 2, point 0) */
    unsigned char target_hash[20] = {
        0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
        0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88, 0x77, 0x66
    };

    /* Place at variant 2 (beta positive Y), point 0 */
    memcpy(hashes[2][0], target_hash, 20);

    /* Set up target */
    memcpy(test_targets[0].address, target_hash, 20);
    bloom_ext_add(&test_bloom, (const char *)target_hash, 20);

    Int key_mpz, stride, lambda, lambda2;
    key_mpz.SetInt32(6000);
    stride.SetInt32(1);
    lambda.SetBase16((char *)"5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    lambda2.SetBase16((char *)"ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

    writekey_called = 0;

    /* Check batch - should find 1 match */
    int matches = rmd160_check_uncompressed_endomorphism(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        &lambda,
        &lambda2,
        mock_writekey
    );

    ASSERT_EQ(1, matches);
    ASSERT_EQ(1, writekey_called);
}

TEST(rmd160_uncompressed_endomorphism_beta2_match) {
    /* Test match on beta^2-transformed point */
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes */
    char hashes[12][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Create a target hash for beta2 point (variants 4-5 are beta2 with parities) */
    unsigned char target_hash[20] = {
        0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
        0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00
    };

    /* Place at variant 4 (beta2, positive Y), point 1 */
    memcpy(hashes[4][1], target_hash, 20);

    /* Set up target */
    memcpy(test_targets[0].address, target_hash, 20);
    bloom_ext_add(&test_bloom, (const char *)target_hash, 20);

    Int key_mpz, stride, lambda, lambda2;
    key_mpz.SetInt32(7000);
    stride.SetInt32(1);
    lambda.SetBase16((char *)"5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    lambda2.SetBase16((char *)"ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

    writekey_called = 0;

    /* Check batch - should find 1 match */
    int matches = rmd160_check_uncompressed_endomorphism(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        &lambda,
        &lambda2,
        mock_writekey
    );

    ASSERT_EQ(1, matches);
    ASSERT_EQ(1, writekey_called);
}

TEST(rmd160_uncompressed_endomorphism_multiple_matches) {
    /* Test multiple matches across different variants */
    setup_test_bloom();
    setup_test_secp();

    /* Create batch of hashes */
    char hashes[12][4][20];
    memset(hashes, 0, sizeof(hashes));

    /* Create three target hashes for different variants */
    unsigned char target1[20] = {
        0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
        0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE
    };
    unsigned char target2[20] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };
    unsigned char target3[20] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22, 0x33
    };

    /* Place at: variant 0 (original) point 1, variant 2 (beta pos) point 3, variant 5 (beta2 neg) point 2 */
    memcpy(hashes[0][1], target1, 20);
    memcpy(hashes[2][3], target2, 20);
    memcpy(hashes[5][2], target3, 20);

    /* Set up targets */
    memcpy(test_targets[0].address, target1, 20);
    memcpy(test_targets[1].address, target2, 20);
    memcpy(test_targets[2].address, target3, 20);
    bloom_ext_add(&test_bloom, (const char *)target1, 20);
    bloom_ext_add(&test_bloom, (const char *)target2, 20);
    bloom_ext_add(&test_bloom, (const char *)target3, 20);

    Int key_mpz, stride, lambda, lambda2;
    key_mpz.SetInt32(7500);
    stride.SetInt32(1);
    lambda.SetBase16((char *)"5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    lambda2.SetBase16((char *)"ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

    writekey_called = 0;

    /* Check batch - should find 3 matches */
    int matches = rmd160_check_uncompressed_endomorphism(
        hashes,
        0,
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        3,
        &lambda,
        &lambda2,
        mock_writekey
    );

    ASSERT_EQ(3, matches);
    ASSERT_EQ(3, writekey_called);
}

/* ============================================================================
 * Edge Cases and Error Conditions
 * ============================================================================ */

TEST(rmd160_check_null_hash) {
    setup_test_bloom();

    /* NULL hash pointer should be handled gracefully */
    int result = rmd160_check_single(NULL, &test_bloom, test_targets, 1);

    /* Implementation may return 0 or handle error - just verify no crash */
    (void)result;
}

TEST(rmd160_check_zero_targets) {
    setup_test_bloom();

    unsigned char hash[20] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
                               0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14};

    /* Zero targets should return 0 */
    int result = rmd160_check_single(hash, &test_bloom, test_targets, 0);

    ASSERT_EQ(0, result);
}

TEST(rmd160_check_multiple_batches) {
    setup_test_bloom();

    /* Test that batch index j is handled correctly */
    char hashes[2][4][20];
    memset(hashes, 0, sizeof(hashes));

    Int key_mpz;
    Int stride;
    key_mpz.SetInt32(1);
    stride.SetInt32(1);

    writekey_called = 0;

    /* Check with different batch indices */
    for (uint64_t j = 0; j < 3; j++) {
        int matches = rmd160_check_compressed_simple(
            hashes,
            j,
            &key_mpz,
            &stride,
            test_secp,
            &test_bloom,
            test_targets,
            1,
            mock_writekey
        );
        ASSERT_EQ(0, matches);
    }

    ASSERT_EQ(0, writekey_called);
}

TEST(rmd160_compressed_simple_stride_calculation) {
    setup_test_bloom();
    setup_test_secp();

    /* Test that stride is used correctly in key calculation */
    char hashes[2][4][20];
    memset(hashes, 0, sizeof(hashes));

    Int key_mpz;
    Int stride;
    key_mpz.SetInt32(1000);
    stride.SetInt32(10);  /* Larger stride */

    writekey_called = 0;

    /* No matches, but verify stride doesn't cause crashes */
    int matches = rmd160_check_compressed_simple(
        hashes,
        5,  /* Batch 5 */
        &key_mpz,
        &stride,
        test_secp,
        &test_bloom,
        test_targets,
        1,
        mock_writekey
    );

    ASSERT_EQ(0, matches);
    ASSERT_EQ(0, writekey_called);
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

/* Exported function for test runner */
int run_search_rmd160_tests(void) {
    TEST_INIT();

    TEST_SECTION("Basic Configuration");
    RUN_TEST(rmd160_constant_check);
    RUN_TEST(rmd160_mock_writekey);

    TEST_SECTION("Single Hash Checks");
    RUN_TEST(rmd160_check_single_not_in_bloom);
    RUN_TEST(rmd160_check_single_in_bloom_not_in_targets);
    RUN_TEST(rmd160_check_single_match);

    TEST_SECTION("Compressed Mode - Simple");
    RUN_TEST(rmd160_compressed_simple_no_matches);
    RUN_TEST(rmd160_compressed_simple_structure);
    RUN_TEST(rmd160_compressed_simple_with_match);
    RUN_TEST(rmd160_compressed_simple_multiple_matches);

    TEST_SECTION("Compressed Mode - Endomorphism");
    RUN_TEST(rmd160_compressed_endomorphism_no_matches);
    RUN_TEST(rmd160_compressed_endomorphism_structure);
    RUN_TEST(rmd160_compressed_endomorphism_original_match);
    RUN_TEST(rmd160_compressed_endomorphism_beta_match);
    RUN_TEST(rmd160_compressed_endomorphism_beta2_match);
    RUN_TEST(rmd160_compressed_endomorphism_multiple_matches);

    TEST_SECTION("Uncompressed Mode - Simple");
    RUN_TEST(rmd160_uncompressed_simple_no_matches);
    RUN_TEST(rmd160_uncompressed_simple_structure);
    RUN_TEST(rmd160_uncompressed_simple_with_match);
    RUN_TEST(rmd160_uncompressed_simple_multiple_matches);

    TEST_SECTION("Uncompressed Mode - Endomorphism");
    RUN_TEST(rmd160_uncompressed_endomorphism_no_matches);
    RUN_TEST(rmd160_uncompressed_endomorphism_structure);
    RUN_TEST(rmd160_uncompressed_endomorphism_original_match);
    RUN_TEST(rmd160_uncompressed_endomorphism_beta_match);
    RUN_TEST(rmd160_uncompressed_endomorphism_beta2_match);
    RUN_TEST(rmd160_uncompressed_endomorphism_multiple_matches);

    TEST_SECTION("Edge Cases");
    RUN_TEST(rmd160_check_null_hash);
    RUN_TEST(rmd160_check_zero_targets);
    RUN_TEST(rmd160_check_multiple_batches);
    RUN_TEST(rmd160_compressed_simple_stride_calculation);

    /* Cleanup */
    cleanup_test_bloom();
    cleanup_test_secp();

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_search_rmd160_tests();
}
#endif
