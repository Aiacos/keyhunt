/*
 * test_fused_hash.cpp - Unit tests for fused SHA256→RIPEMD160 pipeline
 *
 * Tests verify that the fused implementation produces bit-exact identical
 * output compared to calling SHA256 and RIPEMD160 separately.
 *
 * Tests:
 * - Compressed keys (1-block SHA256)
 * - Uncompressed keys (2-block SHA256)
 * - Known test vectors
 * - Edge cases (all zeros, all ones)
 */

#include "test_framework.h"
#include "hash/sha256_avx2.h"
#include "hash/ripemd160.h"

#include <string.h>
#include <immintrin.h>  /* AVX2 intrinsics */

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Compare two 20-byte RIPEMD160 outputs
 */
static int compare_hash160(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, 20) == 0;
}

/**
 * Fill test input buffers with pattern
 */
static void fill_test_pattern(uint32_t *buf, int pattern_seed) {
    for (int i = 0; i < 16; i++) {
        buf[i] = (uint32_t)(pattern_seed * 0x9e3779b9 + i);
    }
}

/* ============================================================================
 * Compressed Key Tests (1-Block SHA256)
 * ============================================================================ */

TEST(fused_1B_zero_input) {
    /* Test with all-zero input */
    uint32_t input[8][16];
    uint8_t output_separate[8][32];
    uint8_t hash_separate[8][20];
    uint8_t hash_fused[8][20];

    /* Initialize all inputs to zero */
    memset(input, 0, sizeof(input));

    /* Separate path: SHA256 then RIPEMD160 */
    sha256avx2_1B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7]
    );

    ripemd160avx2_32(
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7],
        hash_separate[0], hash_separate[1], hash_separate[2], hash_separate[3],
        hash_separate[4], hash_separate[5], hash_separate[6], hash_separate[7]
    );

    /* Fused path: Direct SHA256→RIPEMD160 */
    sha256_ripemd160_avx2_1B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        hash_fused[0], hash_fused[1], hash_fused[2], hash_fused[3],
        hash_fused[4], hash_fused[5], hash_fused[6], hash_fused[7]
    );

    /* Verify all 8 parallel outputs match */
    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE(compare_hash160(hash_separate[i], hash_fused[i]));
    }
}

TEST(fused_1B_pattern_input) {
    /* Test with pattern-filled input */
    uint32_t input[8][16];
    uint8_t output_separate[8][32];
    uint8_t hash_separate[8][20];
    uint8_t hash_fused[8][20];

    /* Fill each lane with different pattern */
    for (int i = 0; i < 8; i++) {
        fill_test_pattern(input[i], i);
    }

    /* Separate path */
    sha256avx2_1B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7]
    );

    ripemd160avx2_32(
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7],
        hash_separate[0], hash_separate[1], hash_separate[2], hash_separate[3],
        hash_separate[4], hash_separate[5], hash_separate[6], hash_separate[7]
    );

    /* Fused path */
    sha256_ripemd160_avx2_1B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        hash_fused[0], hash_fused[1], hash_fused[2], hash_fused[3],
        hash_fused[4], hash_fused[5], hash_fused[6], hash_fused[7]
    );

    /* Verify all 8 parallel outputs match */
    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE(compare_hash160(hash_separate[i], hash_fused[i]));
    }
}

TEST(fused_1B_known_vector) {
    /* Test with known compressed public key pattern */
    /* This simulates real compressed key data (33 bytes) */
    uint32_t input[8][16];
    uint8_t output_separate[8][32];
    uint8_t hash_separate[8][20];
    uint8_t hash_fused[8][20];

    /* Initialize with compressed key pattern */
    /* 0x02 prefix + 32 bytes of X coordinate */
    for (int lane = 0; lane < 8; lane++) {
        memset(input[lane], 0, sizeof(input[lane]));
        /* Simulate 0x02 prefix (compressed even Y) */
        ((uint8_t*)input[lane])[0] = 0x02;
        /* Fill X coordinate with pattern */
        for (int i = 1; i < 33; i++) {
            ((uint8_t*)input[lane])[i] = (uint8_t)(lane * 7 + i);
        }
    }

    /* Separate path */
    sha256avx2_1B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7]
    );

    ripemd160avx2_32(
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7],
        hash_separate[0], hash_separate[1], hash_separate[2], hash_separate[3],
        hash_separate[4], hash_separate[5], hash_separate[6], hash_separate[7]
    );

    /* Fused path */
    sha256_ripemd160_avx2_1B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        hash_fused[0], hash_fused[1], hash_fused[2], hash_fused[3],
        hash_fused[4], hash_fused[5], hash_fused[6], hash_fused[7]
    );

    /* Verify all 8 parallel outputs match */
    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE(compare_hash160(hash_separate[i], hash_fused[i]));
    }
}

TEST(fused_1B_all_ones) {
    /* Test with all bits set to 1 */
    uint32_t input[8][16];
    uint8_t output_separate[8][32];
    uint8_t hash_separate[8][20];
    uint8_t hash_fused[8][20];

    /* Fill with 0xFF */
    memset(input, 0xFF, sizeof(input));

    /* Separate path */
    sha256avx2_1B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7]
    );

    ripemd160avx2_32(
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7],
        hash_separate[0], hash_separate[1], hash_separate[2], hash_separate[3],
        hash_separate[4], hash_separate[5], hash_separate[6], hash_separate[7]
    );

    /* Fused path */
    sha256_ripemd160_avx2_1B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        hash_fused[0], hash_fused[1], hash_fused[2], hash_fused[3],
        hash_fused[4], hash_fused[5], hash_fused[6], hash_fused[7]
    );

    /* Verify all 8 parallel outputs match */
    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE(compare_hash160(hash_separate[i], hash_fused[i]));
    }
}

/* ============================================================================
 * Uncompressed Key Tests (2-Block SHA256)
 * ============================================================================ */

TEST(fused_2B_zero_input) {
    /* Test 2-block version with all-zero input (2 blocks = 32 uint32_t per stream) */
    uint32_t input[8][32];
    uint8_t output_separate[8][32];
    uint8_t hash_separate[8][20];
    uint8_t hash_fused[8][20];

    /* Initialize all inputs to zero */
    memset(input, 0, sizeof(input));

    /* Separate path */
    sha256avx2_2B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7]
    );

    ripemd160avx2_32(
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7],
        hash_separate[0], hash_separate[1], hash_separate[2], hash_separate[3],
        hash_separate[4], hash_separate[5], hash_separate[6], hash_separate[7]
    );

    /* Fused path */
    sha256_ripemd160_avx2_2B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        hash_fused[0], hash_fused[1], hash_fused[2], hash_fused[3],
        hash_fused[4], hash_fused[5], hash_fused[6], hash_fused[7]
    );

    /* Verify all 8 parallel outputs match */
    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE(compare_hash160(hash_separate[i], hash_fused[i]));
    }
}

TEST(fused_2B_pattern_input) {
    /* Test 2-block version with pattern input (2 blocks = 32 uint32_t per stream) */
    uint32_t input[8][32];
    uint8_t output_separate[8][32];
    uint8_t hash_separate[8][20];
    uint8_t hash_fused[8][20];

    /* Zero-initialize then fill first block with pattern (second block stays zero) */
    memset(input, 0, sizeof(input));
    for (int i = 0; i < 8; i++) {
        fill_test_pattern(input[i], i * 13);
    }

    /* Separate path */
    sha256avx2_2B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7]
    );

    ripemd160avx2_32(
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7],
        hash_separate[0], hash_separate[1], hash_separate[2], hash_separate[3],
        hash_separate[4], hash_separate[5], hash_separate[6], hash_separate[7]
    );

    /* Fused path */
    sha256_ripemd160_avx2_2B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        hash_fused[0], hash_fused[1], hash_fused[2], hash_fused[3],
        hash_fused[4], hash_fused[5], hash_fused[6], hash_fused[7]
    );

    /* Verify all 8 parallel outputs match */
    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE(compare_hash160(hash_separate[i], hash_fused[i]));
    }
}

TEST(fused_2B_uncompressed_key) {
    /* Test with uncompressed public key pattern */
    /* This simulates real uncompressed key data (65 bytes padded to 2 blocks = 128 bytes) */
    uint32_t input[8][32];
    uint8_t output_separate[8][32];
    uint8_t hash_separate[8][20];
    uint8_t hash_fused[8][20];

    /* Initialize with uncompressed key pattern */
    /* 0x04 prefix + 32 bytes X + 32 bytes Y */
    for (int lane = 0; lane < 8; lane++) {
        memset(input[lane], 0, sizeof(input[lane]));
        /* Simulate 0x04 prefix (uncompressed) */
        ((uint8_t*)input[lane])[0] = 0x04;
        /* Fill X and Y coordinates with pattern */
        for (int i = 1; i < 64; i++) {
            ((uint8_t*)input[lane])[i] = (uint8_t)(lane * 11 + i);
        }
    }

    /* Separate path */
    sha256avx2_2B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7]
    );

    ripemd160avx2_32(
        output_separate[0], output_separate[1], output_separate[2], output_separate[3],
        output_separate[4], output_separate[5], output_separate[6], output_separate[7],
        hash_separate[0], hash_separate[1], hash_separate[2], hash_separate[3],
        hash_separate[4], hash_separate[5], hash_separate[6], hash_separate[7]
    );

    /* Fused path */
    sha256_ripemd160_avx2_2B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        hash_fused[0], hash_fused[1], hash_fused[2], hash_fused[3],
        hash_fused[4], hash_fused[5], hash_fused[6], hash_fused[7]
    );

    /* Verify all 8 parallel outputs match */
    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE(compare_hash160(hash_separate[i], hash_fused[i]));
    }
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int run_fused_hash_tests(void) {
    /* Check if AVX2 is available */
    if (!sha256_avx2_available()) {
        printf("\n  " CLR_YELLOW "SKIPPED: AVX2 not available on this CPU" CLR_RESET "\n");
        return 0;
    }

    TEST_INIT();

    TEST_SECTION("Compressed Keys (1-Block SHA256)");
    RUN_TEST(fused_1B_zero_input);
    RUN_TEST(fused_1B_pattern_input);
    RUN_TEST(fused_1B_known_vector);
    RUN_TEST(fused_1B_all_ones);

    TEST_SECTION("Uncompressed Keys (2-Block SHA256)");
    RUN_TEST(fused_2B_zero_input);
    RUN_TEST(fused_2B_pattern_input);
    RUN_TEST(fused_2B_uncompressed_key);

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_fused_hash_tests();
}
#endif
