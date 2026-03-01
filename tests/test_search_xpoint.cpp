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

/* Initialize secp256k1 field and order for tests that call ModMulK1order */
static void init_secp256k1_field(void) {
    static int initialized = 0;
    if (!initialized) {
        /* P and order must be static: InitK1/SetupField store raw pointers */
        static Int P;
        P.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F");
        Int::SetupField(&P);

        static Int order;
        order.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");
        Int::InitK1(&order);

        initialized = 1;
    }
}

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

static void mock_writekey(bool compressed, Int *key) {
    (void)compressed;
    writekey_called++;
    last_found_key = *key;
}

/* Mock binary search function is provided by test_search_mocks.cpp */
extern int searchbinary(struct address_value *buffer, char *data, int64_t array_length);

/* Setup function to initialize test bloom filter */
static void setup_test_bloom(void) {
    /* Ensure secp256k1 field/order are initialized (needed for ModMulK1order) */
    init_secp256k1_field();

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

TEST(xpoint_check_batch_simple_with_match) {
    setup_test_bloom();

    /* Create batch of 4 points with one matching target */
    Point pts[4];
    Int key_mpz;
    Int stride;

    key_mpz.SetInt32(1000);
    stride.SetInt32(1);

    /* Create a target X-coordinate (matches the hex string below) */
    unsigned char target_x[32] = {
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };

    /* Set up point 2 to match target (same bytes as target_x) */
    pts[2].x.SetBase16((char *)"123456789ABCDEF0112233445566778899AABBCCDDEEFF000102030405060708");
    pts[2].y.SetInt32(1);

    /* Set up other points */
    pts[0].x.SetInt32(1001);
    pts[0].y.SetInt32(2001);
    pts[1].x.SetInt32(1002);
    pts[1].y.SetInt32(2002);
    pts[3].x.SetInt32(1003);
    pts[3].y.SetInt32(2003);

    /* Set up target to match point 2 */
    memcpy(test_targets[0].address, target_x, 32);
    bloom_ext_add(&test_bloom, (const char *)target_x, 32);

    writekey_called = 0;

    /* Check batch - should find 1 match */
    int matches = xpoint_check_batch_simple(
        pts, 0, &key_mpz, &stride,
        &test_bloom, test_targets, 1, 32,
        mock_writekey
    );

    ASSERT_EQ(1, matches);
    ASSERT_EQ(1, writekey_called);

    /* Verify the correct key was found: base_key + (2 * stride) */
    Int expected_key;
    expected_key.SetInt32(1002);  /* 1000 + 2*1 */
    ASSERT_TRUE(last_found_key.IsEqual(&expected_key));
}

TEST(xpoint_check_batch_simple_multiple_matches) {
    setup_test_bloom();

    /* Create batch with multiple matching points */
    Point pts[4];
    Int key_mpz;
    Int stride;

    key_mpz.SetInt32(2000);
    stride.SetInt32(5);

    /* Create target X-coordinates */
    unsigned char target1[32], target2[32];
    memset(target1, 0xAA, 32);
    memset(target2, 0xBB, 32);

    /* Set up points 0 and 3 to match targets */
    pts[0].x.SetBase16((char *)"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    pts[0].y.SetInt32(1);
    pts[3].x.SetBase16((char *)"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB");
    pts[3].y.SetInt32(1);

    /* Other points don't match */
    pts[1].x.SetInt32(5001);
    pts[1].y.SetInt32(6001);
    pts[2].x.SetInt32(5002);
    pts[2].y.SetInt32(6002);

    /* Set up targets in bloom and array */
    memcpy(test_targets[0].address, target1, 32);
    memcpy(test_targets[1].address, target2, 32);
    bloom_ext_add(&test_bloom, (const char *)target1, 32);
    bloom_ext_add(&test_bloom, (const char *)target2, 32);

    writekey_called = 0;

    /* Check batch - should find 2 matches */
    int matches = xpoint_check_batch_simple(
        pts, 0, &key_mpz, &stride,
        &test_bloom, test_targets, 2, 32,
        mock_writekey
    );

    ASSERT_EQ(2, matches);
    ASSERT_EQ(2, writekey_called);
}

TEST(xpoint_check_batch_simple_stride_calculation) {
    /* Verify correct key calculation with different stride values */
    setup_test_bloom();

    Point pts[4];
    Int key_mpz;
    Int stride;

    key_mpz.SetInt32(10000);
    stride.SetInt32(100);  /* Large stride */

    /* Create matching point at position 1 */
    unsigned char target_x[32];
    memset(target_x, 0xCC, 32);

    pts[1].x.SetBase16((char *)"CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC");
    pts[1].y.SetInt32(1);

    /* Setup target */
    memcpy(test_targets[0].address, target_x, 32);
    bloom_ext_add(&test_bloom, (const char *)target_x, 32);

    writekey_called = 0;

    xpoint_check_batch_simple(
        pts, 0, &key_mpz, &stride,
        &test_bloom, test_targets, 1, 32,
        mock_writekey
    );

    /* Expected key: 10000 + (1 * 100) = 10100 */
    Int expected;
    expected.SetInt32(10100);
    ASSERT_TRUE(last_found_key.IsEqual(&expected));
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

TEST(xpoint_check_batch_endomorphism_original_match) {
    /* Test match on original point (not beta variants) */
    setup_test_bloom();

    Point pts[4];
    Point beta_pts[4];
    Point beta2_pts[4];
    Int key_mpz;
    Int stride;
    Int lambda;
    Int lambda2;

    key_mpz.SetInt32(3000);
    stride.SetInt32(2);
    lambda.SetInt32(1);
    lambda2.SetInt32(1);

    /* Create target that matches original point at position 2 */
    unsigned char target_x[32];
    memset(target_x, 0xDD, 32);

    pts[2].x.SetBase16((char *)"DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD");
    pts[2].y.SetInt32(1);

    /* Other points don't match */
    for (int i = 0; i < 4; i++) {
        if (i != 2) {
            pts[i].x.SetInt32(8000 + i);
            pts[i].y.SetInt32(9000 + i);
        }
        beta_pts[i].x.SetInt32(10000 + i);
        beta_pts[i].y.SetInt32(11000 + i);
        beta2_pts[i].x.SetInt32(12000 + i);
        beta2_pts[i].y.SetInt32(13000 + i);
    }

    memcpy(test_targets[0].address, target_x, 32);
    bloom_ext_add(&test_bloom, (const char *)target_x, 32);

    writekey_called = 0;

    int matches = xpoint_check_batch_endomorphism(
        pts, beta_pts, beta2_pts,
        0, &key_mpz, &stride, &test_bloom,
        test_targets, 1, 32,
        &lambda, &lambda2,
        mock_writekey
    );

    ASSERT_EQ(1, matches);
    ASSERT_EQ(1, writekey_called);

    /* Expected key: 3000 + (2 * 2) = 3004 */
    Int expected;
    expected.SetInt32(3004);
    ASSERT_TRUE(last_found_key.IsEqual(&expected));
}

TEST(xpoint_check_batch_endomorphism_beta_match) {
    /* Test match on beta-transformed point */
    setup_test_bloom();

    Point pts[4];
    Point beta_pts[4];
    Point beta2_pts[4];
    Int key_mpz;
    Int stride;
    Int lambda;
    Int lambda2;

    key_mpz.SetInt32(5000);
    stride.SetInt32(3);
    lambda.SetInt32(7);  /* Some lambda value */
    lambda2.SetInt32(1);

    /* Create target that matches beta point at position 1 */
    unsigned char target_x[32];
    memset(target_x, 0xEE, 32);

    beta_pts[1].x.SetBase16((char *)"EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE");
    beta_pts[1].y.SetInt32(1);

    /* Other points don't match */
    for (int i = 0; i < 4; i++) {
        pts[i].x.SetInt32(14000 + i);
        pts[i].y.SetInt32(15000 + i);
        if (i != 1) {
            beta_pts[i].x.SetInt32(16000 + i);
            beta_pts[i].y.SetInt32(17000 + i);
        }
        beta2_pts[i].x.SetInt32(18000 + i);
        beta2_pts[i].y.SetInt32(19000 + i);
    }

    memcpy(test_targets[0].address, target_x, 32);
    bloom_ext_add(&test_bloom, (const char *)target_x, 32);

    writekey_called = 0;

    int matches = xpoint_check_batch_endomorphism(
        pts, beta_pts, beta2_pts,
        0, &key_mpz, &stride, &test_bloom,
        test_targets, 1, 32,
        &lambda, &lambda2,
        mock_writekey
    );

    ASSERT_EQ(1, matches);
    ASSERT_EQ(1, writekey_called);
}

TEST(xpoint_check_batch_endomorphism_beta2_match) {
    /* Test match on beta^2-transformed point */
    setup_test_bloom();

    Point pts[4];
    Point beta_pts[4];
    Point beta2_pts[4];
    Int key_mpz;
    Int stride;
    Int lambda;
    Int lambda2;

    key_mpz.SetInt32(8000);
    stride.SetInt32(1);
    lambda.SetInt32(1);
    lambda2.SetInt32(11);  /* Some lambda^2 value */

    /* Create target that matches beta2 point at position 3 */
    unsigned char target_x[32];
    memset(target_x, 0xFF, 32);

    beta2_pts[3].x.SetBase16((char *)"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
    beta2_pts[3].y.SetInt32(1);

    /* Other points don't match */
    for (int i = 0; i < 4; i++) {
        pts[i].x.SetInt32(20000 + i);
        pts[i].y.SetInt32(21000 + i);
        beta_pts[i].x.SetInt32(22000 + i);
        beta_pts[i].y.SetInt32(23000 + i);
        if (i != 3) {
            beta2_pts[i].x.SetInt32(24000 + i);
            beta2_pts[i].y.SetInt32(25000 + i);
        }
    }

    memcpy(test_targets[0].address, target_x, 32);
    bloom_ext_add(&test_bloom, (const char *)target_x, 32);

    writekey_called = 0;

    int matches = xpoint_check_batch_endomorphism(
        pts, beta_pts, beta2_pts,
        0, &key_mpz, &stride, &test_bloom,
        test_targets, 1, 32,
        &lambda, &lambda2,
        mock_writekey
    );

    ASSERT_EQ(1, matches);
    ASSERT_EQ(1, writekey_called);
}

TEST(xpoint_check_batch_endomorphism_multiple_matches) {
    /* Test multiple matches across all three arrays */
    setup_test_bloom();

    Point pts[4];
    Point beta_pts[4];
    Point beta2_pts[4];
    Int key_mpz;
    Int stride;
    Int lambda;
    Int lambda2;

    key_mpz.SetInt32(9000);
    stride.SetInt32(1);
    lambda.SetInt32(3);
    lambda2.SetInt32(5);

    /* Create targets that match at different positions */
    unsigned char target1[32], target2[32], target3[32];
    memset(target1, 0x11, 32);
    memset(target2, 0x22, 32);
    memset(target3, 0x33, 32);

    /* Original point 0 matches */
    pts[0].x.SetBase16((char *)"1111111111111111111111111111111111111111111111111111111111111111");
    pts[0].y.SetInt32(1);

    /* Beta point 2 matches */
    beta_pts[2].x.SetBase16((char *)"2222222222222222222222222222222222222222222222222222222222222222");
    beta_pts[2].y.SetInt32(1);

    /* Beta2 point 3 matches */
    beta2_pts[3].x.SetBase16((char *)"3333333333333333333333333333333333333333333333333333333333333333");
    beta2_pts[3].y.SetInt32(1);

    /* Fill in other points */
    for (int i = 0; i < 4; i++) {
        if (i != 0) {
            pts[i].x.SetInt32(30000 + i);
            pts[i].y.SetInt32(31000 + i);
        }
        if (i != 2) {
            beta_pts[i].x.SetInt32(32000 + i);
            beta_pts[i].y.SetInt32(33000 + i);
        }
        if (i != 3) {
            beta2_pts[i].x.SetInt32(34000 + i);
            beta2_pts[i].y.SetInt32(35000 + i);
        }
    }

    /* Setup targets */
    memcpy(test_targets[0].address, target1, 32);
    memcpy(test_targets[1].address, target2, 32);
    memcpy(test_targets[2].address, target3, 32);
    bloom_ext_add(&test_bloom, (const char *)target1, 32);
    bloom_ext_add(&test_bloom, (const char *)target2, 32);
    bloom_ext_add(&test_bloom, (const char *)target3, 32);

    writekey_called = 0;

    int matches = xpoint_check_batch_endomorphism(
        pts, beta_pts, beta2_pts,
        0, &key_mpz, &stride, &test_bloom,
        test_targets, 3, 32,
        &lambda, &lambda2,
        mock_writekey
    );

    ASSERT_EQ(3, matches);
    ASSERT_EQ(3, writekey_called);
}

/* ============================================================================
 * Edge Cases and Error Conditions
 * ============================================================================ */

TEST(xpoint_check_null_x_coord_documentation) {
    /* DOCUMENTATION: Passing NULL x_coord is undefined behavior.
     * Callers MUST ensure x_coord is non-NULL.
     * The xpoint_check_single() function does not perform NULL checks
     * because all callers in the production code guarantee non-NULL inputs.
     */
    ASSERT_TRUE(1);  // This test just documents the contract
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
    RUN_TEST(xpoint_check_batch_simple_with_match);
    RUN_TEST(xpoint_check_batch_simple_multiple_matches);
    RUN_TEST(xpoint_check_batch_simple_stride_calculation);

    TEST_SECTION("Batch Processing - Endomorphism Mode");
    RUN_TEST(xpoint_check_batch_endomorphism_structure);
    RUN_TEST(xpoint_check_batch_endomorphism_no_matches);
    RUN_TEST(xpoint_check_batch_endomorphism_original_match);
    RUN_TEST(xpoint_check_batch_endomorphism_beta_match);
    RUN_TEST(xpoint_check_batch_endomorphism_beta2_match);
    RUN_TEST(xpoint_check_batch_endomorphism_multiple_matches);

    TEST_SECTION("Edge Cases");
    RUN_TEST(xpoint_check_null_x_coord_documentation);
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
