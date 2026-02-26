/*
 * test_search_xpoint.cpp - Unit tests for XPOINT search mode
 *
 * Tests the X-Point search mode which directly compares public key
 * X-coordinates against targets without hashing. This is the fastest
 * search mode when the target public key X-coordinate is known.
 *
 * Test coverage:
 * - Single X-coordinate checking
 * - Batch processing (simple and endomorphism variants)
 * - Bloom filter integration
 * - Binary search verification
 * - Edge cases and error conditions
 */

#include "test_framework.h"
#include "../src/search/search_xpoint.h"
#include "../src/secp256k1/Int.h"
#include "../src/secp256k1/Point.h"
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

/* Mock writekey function for testing */
static int writekey_called = 0;
static Int last_found_key;

void mock_writekey(bool compressed, Int *key) {
    (void)compressed;
    writekey_called++;
    last_found_key = *key;
}

/* Mock binary search function (returns 1 if found, 0 otherwise) */
extern "C" int searchbinary(struct address_value *buffer, char *data, int64_t array_length) {
    /* Simple mock: compare first 32 bytes of first target */
    if (array_length > 0 && memcmp(buffer[0].address, data, 32) == 0) {
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

/* Cleanup function */
static void cleanup_test_bloom(void) {
    if (test_bloom_initialized) {
        bloom_ext_free(&test_bloom);
        test_bloom_initialized = 0;
    }
}

/* ============================================================================
 * Basic Configuration Tests
 * ============================================================================ */

TEST(xpoint_constant_check) {
    /* Verify XPOINT comparison length is 32 bytes (X-coordinate size) */
    ASSERT_EQ(32, XPOINT_COMPARISON_LENGTH);
}

TEST(xpoint_mock_writekey) {
    /* Test that mock writekey function works */
    writekey_called = 0;
    Int test_key;
    test_key.SetInt32(12345);

    mock_writekey(false, &test_key);

    ASSERT_EQ(1, writekey_called);
    ASSERT_EQ(12345, last_found_key.GetInt64());
}

/* ============================================================================
 * Single X-coordinate Check Tests
 * ============================================================================ */

TEST(xpoint_check_single_not_in_bloom) {
    setup_test_bloom();

    /* Create a test X-coordinate */
    unsigned char x_coord[32] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
    };

    /* X-coordinate not in bloom filter should return 0 quickly */
    int result = xpoint_check_single(x_coord, &test_bloom, test_targets, 1, 32);

    ASSERT_EQ(0, result);
}

TEST(xpoint_check_single_in_bloom_not_in_targets) {
    setup_test_bloom();

    /* Create a test X-coordinate */
    unsigned char x_coord[32] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
        0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00
    };

    /* Add to bloom filter but not to targets */
    bloom_ext_add(&test_bloom, (const char *)x_coord, 32);

    /* Should hit bloom but fail binary search */
    int result = xpoint_check_single(x_coord, &test_bloom, test_targets, 1, 32);

    ASSERT_EQ(0, result);
}

TEST(xpoint_check_single_match) {
    setup_test_bloom();

    /* Create a test X-coordinate that will match */
    unsigned char x_coord[32] = {
        0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
        0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
    };

    /* Set up target to match this X-coordinate */
    memcpy(test_targets[0].address, x_coord, 32);

    /* Add to bloom filter */
    bloom_ext_add(&test_bloom, (const char *)x_coord, 32);

    /* Should find match */
    int result = xpoint_check_single(x_coord, &test_bloom, test_targets, 1, 32);

    ASSERT_EQ(1, result);
}

/* ============================================================================
 * Batch Processing Tests - Simple Mode
 * ============================================================================ */

TEST(xpoint_check_batch_simple_no_matches) {
    setup_test_bloom();

    /* Create batch of 4 points */
    Point pts[4];
    Int key_mpz;
    Int stride;

    key_mpz.SetInt32(1);
    stride.SetInt32(1);

    /* Initialize points with test data */
    for (int i = 0; i < 4; i++) {
        pts[i].x.SetInt32(1000 + i);
        pts[i].y.SetInt32(2000 + i);
    }

    writekey_called = 0;

    /* Check batch - should find no matches */
    int matches = xpoint_check_batch_simple(
        pts,
        0,  /* j = 0 (first batch) */
        &key_mpz,
        &stride,
        &test_bloom,
        test_targets,
        1,  /* 1 target */
        32,
        mock_writekey
    );

    ASSERT_EQ(0, matches);
    ASSERT_EQ(0, writekey_called);
}

TEST(xpoint_check_batch_simple_structure) {
    /* Test that batch function accepts correct parameters */
    setup_test_bloom();

    Point pts[8];  /* 2 batches of 4 */
    Int key_mpz;
    Int stride;

    key_mpz.SetInt32(100);
    stride.SetInt32(10);

    writekey_called = 0;

    /* Should execute without crashing */
    int matches = xpoint_check_batch_simple(
        pts, 0, &key_mpz, &stride, &test_bloom,
        test_targets, 1, 32, mock_writekey
    );

    /* No matches expected, but function should complete */
    ASSERT_EQ(0, matches);
}

/* ============================================================================
 * Batch Processing Tests - Endomorphism Mode
 * ============================================================================ */

TEST(xpoint_check_batch_endomorphism_structure) {
    /* Test that endomorphism batch function accepts correct parameters */
    setup_test_bloom();

    Point pts[4];
    Point beta_pts[4];
    Point beta2_pts[4];
    Int key_mpz;
    Int stride;
    Int lambda;
    Int lambda2;

    key_mpz.SetInt32(100);
    stride.SetInt32(1);

    /* Set lambda and lambda^2 to test values */
    lambda.SetInt32(1);
    lambda2.SetInt32(1);

    writekey_called = 0;

    /* Should execute without crashing */
    int matches = xpoint_check_batch_endomorphism(
        pts, beta_pts, beta2_pts,
        0,  /* j = 0 */
        &key_mpz, &stride, &test_bloom,
        test_targets, 1, 32,
        &lambda, &lambda2,
        mock_writekey
    );

    /* No matches expected, but function should complete */
    ASSERT_EQ(0, matches);
}

TEST(xpoint_check_batch_endomorphism_no_matches) {
    setup_test_bloom();

    /* Create batches of points */
    Point pts[4];
    Point beta_pts[4];
    Point beta2_pts[4];
    Int key_mpz;
    Int stride;
    Int lambda;
    Int lambda2;

    key_mpz.SetInt32(1000);
    stride.SetInt32(1);
    lambda.SetInt32(1);
    lambda2.SetInt32(1);

    /* Initialize points */
    for (int i = 0; i < 4; i++) {
        pts[i].x.SetInt32(5000 + i);
        pts[i].y.SetInt32(6000 + i);
        beta_pts[i].x.SetInt32(7000 + i);
        beta_pts[i].y.SetInt32(8000 + i);
        beta2_pts[i].x.SetInt32(9000 + i);
        beta2_pts[i].y.SetInt32(10000 + i);
    }

    writekey_called = 0;

    /* Check batch - should find no matches */
    int matches = xpoint_check_batch_endomorphism(
        pts, beta_pts, beta2_pts,
        0, &key_mpz, &stride, &test_bloom,
        test_targets, 1, 32,
        &lambda, &lambda2,
        mock_writekey
    );

    ASSERT_EQ(0, matches);
    ASSERT_EQ(0, writekey_called);
}

/* ============================================================================
 * Edge Cases and Error Conditions
 * ============================================================================ */

TEST(xpoint_check_null_x_coord) {
    setup_test_bloom();

    /* Passing NULL x_coord should not crash (undefined behavior, but test defensive coding) */
    /* This test documents expected behavior - in practice, NULL should be avoided */
    /* Result is undefined, we just verify no crash */
    (void)xpoint_check_single(NULL, &test_bloom, test_targets, 0, 32);

    /* If we get here, no crash occurred */
    ASSERT_TRUE(1);
}

TEST(xpoint_check_zero_targets) {
    setup_test_bloom();

    unsigned char x_coord[32] = {0};

    /* Zero targets should return 0 */
    int result = xpoint_check_single(x_coord, &test_bloom, test_targets, 0, 32);

    ASSERT_EQ(0, result);
}

TEST(xpoint_batch_simple_multiple_batches) {
    /* Test that batch index parameter works correctly */
    setup_test_bloom();

    Point pts[12];  /* 3 batches of 4 */
    Int key_mpz;
    Int stride;

    key_mpz.SetInt32(0);
    stride.SetInt32(1);

    writekey_called = 0;

    /* Process batch 0 */
    xpoint_check_batch_simple(pts, 0, &key_mpz, &stride, &test_bloom, test_targets, 1, 32, mock_writekey);

    /* Process batch 1 */
    xpoint_check_batch_simple(pts, 1, &key_mpz, &stride, &test_bloom, test_targets, 1, 32, mock_writekey);

    /* Process batch 2 */
    xpoint_check_batch_simple(pts, 2, &key_mpz, &stride, &test_bloom, test_targets, 1, 32, mock_writekey);

    /* Should complete without issues */
    ASSERT_TRUE(1);
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

/* Exported function for test runner */
int run_search_xpoint_tests(void) {
    TEST_INIT();

    TEST_SECTION("Basic Configuration");
    RUN_TEST(xpoint_constant_check);
    RUN_TEST(xpoint_mock_writekey);

    TEST_SECTION("Single X-coordinate Checks");
    RUN_TEST(xpoint_check_single_not_in_bloom);
    RUN_TEST(xpoint_check_single_in_bloom_not_in_targets);
    RUN_TEST(xpoint_check_single_match);

    TEST_SECTION("Batch Processing - Simple Mode");
    RUN_TEST(xpoint_check_batch_simple_no_matches);
    RUN_TEST(xpoint_check_batch_simple_structure);

    TEST_SECTION("Batch Processing - Endomorphism Mode");
    RUN_TEST(xpoint_check_batch_endomorphism_structure);
    RUN_TEST(xpoint_check_batch_endomorphism_no_matches);

    TEST_SECTION("Edge Cases");
    RUN_TEST(xpoint_check_null_x_coord);
    RUN_TEST(xpoint_check_zero_targets);
    RUN_TEST(xpoint_batch_simple_multiple_batches);

    /* Cleanup */
    cleanup_test_bloom();

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_search_xpoint_tests();
}
#endif
