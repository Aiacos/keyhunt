/*
 * test_sha256_simd.cpp - Unit tests for SHA256 SIMD implementations
 *
 * Tests AVX2 and AVX-512 optimized SHA256 functions:
 * - CPU feature detection
 * - Correctness vs scalar implementation
 * - Known test vectors (NIST/FIPS)
 * - Parallel processing (8-way AVX2, 16-way AVX-512)
 *
 * IMPORTANT: The SIMD functions (sha256avx2_64, sha256avx512_64) are raw
 * SHA-256 compression functions that process pre-padded 64-byte blocks.
 * The scalar sha256() function handles padding internally. Therefore, all
 * SIMD test inputs must be properly padded before comparison with scalar.
 */

#include "test_framework.h"
#include "hash/sha256.h"
#include "hash/sha256_avx2.h"
#include "hash/sha256_avx512.h"
#include <cstring>  /* memcmp, memset */
#include <cstdio>   /* printf */

/* ============================================================================
 * SHA-256 Padding Helper
 *
 * SIMD hash functions are compression functions - they process raw 64-byte
 * blocks. To compare against the scalar sha256() (which pads internally),
 * we must pre-pad inputs using SHA-256 padding rules:
 *   [message] [0x80] [zeros] [64-bit big-endian bit length]
 * ============================================================================ */

static void sha256_pad_block(uint8_t block[64], const uint8_t *msg, size_t len) {
    memset(block, 0, 64);
    if (len > 0) memcpy(block, msg, len);
    block[len] = 0x80;
    /* Write message length in bits as big-endian uint64 at offset 56 */
    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++)
        block[56 + i] = (uint8_t)(bit_len >> (56 - i * 8));
}

/* ============================================================================
 * Test Vectors - NIST SHA-256 Test Vectors
 * ============================================================================ */

/* Empty string: "" */
static const uint8_t test_input_empty[] = "";
static const uint8_t test_expected_empty[32] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8,
    0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
    0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
};

/* "abc" */
static const uint8_t test_input_abc[] = "abc";
static const uint8_t test_expected_abc[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde,
    0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
};

/* "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" */
static const uint8_t test_input_448[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
static const uint8_t test_expected_448[32] = {
    0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93,
    0x0c, 0x3e, 0x60, 0x39, 0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
    0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1
};

/* ============================================================================
 * CPU Feature Detection Tests
 * ============================================================================ */

TEST(sha256_avx2_detection) {
    int avx2_supported = sha256_avx2_available();
    /* Just verify the function runs without crashing */
    /* Result depends on CPU - both 0 and 1 are valid */
    ASSERT_TRUE(avx2_supported == 0 || avx2_supported == 1);
}

TEST(sha256_avx512_detection) {
    int avx512_supported = sha256_avx512_available();
    /* Just verify the function runs without crashing */
    /* Result depends on CPU - both 0 and 1 are valid */
    ASSERT_TRUE(avx512_supported == 0 || avx512_supported == 1);
}

/* ============================================================================
 * Scalar SHA256 Tests (Baseline)
 * ============================================================================ */

TEST(sha256_scalar_empty) {
    uint8_t digest[32];
    sha256((uint8_t*)test_input_empty, 0, digest);
    ASSERT_MEM_EQ(test_expected_empty, digest, 32);
}

TEST(sha256_scalar_abc) {
    uint8_t digest[32];
    sha256((uint8_t*)test_input_abc, 3, digest);
    ASSERT_MEM_EQ(test_expected_abc, digest, 32);
}

TEST(sha256_scalar_448) {
    uint8_t digest[32];
    sha256((uint8_t*)test_input_448, 56, digest);
    ASSERT_MEM_EQ(test_expected_448, digest, 32);
}

/* ============================================================================
 * AVX2 Tests (8-way parallel)
 * ============================================================================ */

TEST(sha256_avx2_basic) {
    if (!sha256_avx2_available()) {
        return;
    }

    /* Test using the 1B (1-block) function which expects pre-padded input */
    uint32_t input[8][16];  /* 8 hashes × 16 uint32_t = 8 × 64 bytes */
    uint8_t digest[8][32];
    uint8_t scalar_digest[32];

    /* Pre-pad "abc" input into each 64-byte block */
    for (int i = 0; i < 8; i++) {
        uint8_t padded[64];
        sha256_pad_block(padded, test_input_abc, 3);
        /* Convert to uint32_t array (big-endian) */
        for (int j = 0; j < 16; j++) {
            input[i][j] = ((uint32_t)padded[j*4] << 24) |
                          ((uint32_t)padded[j*4+1] << 16) |
                          ((uint32_t)padded[j*4+2] << 8) |
                          ((uint32_t)padded[j*4+3]);
        }
    }

    sha256avx2_1B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        digest[0], digest[1], digest[2], digest[3],
        digest[4], digest[5], digest[6], digest[7]
    );

    sha256((uint8_t*)test_input_abc, 3, scalar_digest);

    for (int i = 0; i < 8; i++) {
        ASSERT_MEM_EQ(scalar_digest, digest[i], 32);
    }
}

TEST(sha256_avx2_empty) {
    if (!sha256_avx2_available()) {
        return;
    }

    uint32_t input[8][16];
    uint8_t digest[8][32];

    /* Pre-pad empty string inputs */
    for (int i = 0; i < 8; i++) {
        uint8_t padded[64];
        sha256_pad_block(padded, test_input_empty, 0);
        for (int j = 0; j < 16; j++) {
            input[i][j] = ((uint32_t)padded[j*4] << 24) |
                          ((uint32_t)padded[j*4+1] << 16) |
                          ((uint32_t)padded[j*4+2] << 8) |
                          ((uint32_t)padded[j*4+3]);
        }
    }

    sha256avx2_1B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        digest[0], digest[1], digest[2], digest[3],
        digest[4], digest[5], digest[6], digest[7]
    );

    for (int i = 0; i < 8; i++) {
        ASSERT_MEM_EQ(test_expected_empty, digest[i], 32);
    }
}

TEST(sha256_avx2_mixed) {
    if (!sha256_avx2_available()) {
        return;
    }

    uint32_t input[8][16];
    uint8_t digest[8][32];

    /* Alternating: even=empty, odd="abc" */
    for (int i = 0; i < 8; i++) {
        uint8_t padded[64];
        if (i % 2 == 0)
            sha256_pad_block(padded, test_input_empty, 0);
        else
            sha256_pad_block(padded, test_input_abc, 3);

        for (int j = 0; j < 16; j++) {
            input[i][j] = ((uint32_t)padded[j*4] << 24) |
                          ((uint32_t)padded[j*4+1] << 16) |
                          ((uint32_t)padded[j*4+2] << 8) |
                          ((uint32_t)padded[j*4+3]);
        }
    }

    sha256avx2_1B(
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        digest[0], digest[1], digest[2], digest[3],
        digest[4], digest[5], digest[6], digest[7]
    );

    for (int i = 0; i < 8; i++) {
        if (i % 2 == 0)
            ASSERT_MEM_EQ(test_expected_empty, digest[i], 32);
        else
            ASSERT_MEM_EQ(test_expected_abc, digest[i], 32);
    }
}

/* ============================================================================
 * AVX-512 Tests (16-way parallel)
 * ============================================================================ */

TEST(sha256_avx512_basic) {
    if (!sha256_avx512_available()) {
        return;
    }

    uint32_t input[16][16];
    uint8_t digest[16][32];
    uint8_t scalar_digest[32];

    for (int i = 0; i < 16; i++) {
        uint8_t padded[64];
        sha256_pad_block(padded, test_input_abc, 3);
        for (int j = 0; j < 16; j++) {
            input[i][j] = ((uint32_t)padded[j*4] << 24) |
                          ((uint32_t)padded[j*4+1] << 16) |
                          ((uint32_t)padded[j*4+2] << 8) |
                          ((uint32_t)padded[j*4+3]);
        }
    }

    sha256avx512_1B(
        input[0],  input[1],  input[2],  input[3],
        input[4],  input[5],  input[6],  input[7],
        input[8],  input[9],  input[10], input[11],
        input[12], input[13], input[14], input[15],
        digest[0],  digest[1],  digest[2],  digest[3],
        digest[4],  digest[5],  digest[6],  digest[7],
        digest[8],  digest[9],  digest[10], digest[11],
        digest[12], digest[13], digest[14], digest[15]
    );

    sha256((uint8_t*)test_input_abc, 3, scalar_digest);

    for (int i = 0; i < 16; i++) {
        ASSERT_MEM_EQ(scalar_digest, digest[i], 32);
    }
}

TEST(sha256_avx512_empty) {
    if (!sha256_avx512_available()) {
        return;
    }

    uint32_t input[16][16];
    uint8_t digest[16][32];

    for (int i = 0; i < 16; i++) {
        uint8_t padded[64];
        sha256_pad_block(padded, test_input_empty, 0);
        for (int j = 0; j < 16; j++) {
            input[i][j] = ((uint32_t)padded[j*4] << 24) |
                          ((uint32_t)padded[j*4+1] << 16) |
                          ((uint32_t)padded[j*4+2] << 8) |
                          ((uint32_t)padded[j*4+3]);
        }
    }

    sha256avx512_1B(
        input[0],  input[1],  input[2],  input[3],
        input[4],  input[5],  input[6],  input[7],
        input[8],  input[9],  input[10], input[11],
        input[12], input[13], input[14], input[15],
        digest[0],  digest[1],  digest[2],  digest[3],
        digest[4],  digest[5],  digest[6],  digest[7],
        digest[8],  digest[9],  digest[10], digest[11],
        digest[12], digest[13], digest[14], digest[15]
    );

    for (int i = 0; i < 16; i++) {
        ASSERT_MEM_EQ(test_expected_empty, digest[i], 32);
    }
}

TEST(sha256_avx512_mixed) {
    if (!sha256_avx512_available()) {
        return;
    }

    uint32_t input[16][16];
    uint8_t digest[16][32];

    for (int i = 0; i < 16; i++) {
        uint8_t padded[64];
        if (i % 2 == 0)
            sha256_pad_block(padded, test_input_empty, 0);
        else
            sha256_pad_block(padded, test_input_abc, 3);

        for (int j = 0; j < 16; j++) {
            input[i][j] = ((uint32_t)padded[j*4] << 24) |
                          ((uint32_t)padded[j*4+1] << 16) |
                          ((uint32_t)padded[j*4+2] << 8) |
                          ((uint32_t)padded[j*4+3]);
        }
    }

    sha256avx512_1B(
        input[0],  input[1],  input[2],  input[3],
        input[4],  input[5],  input[6],  input[7],
        input[8],  input[9],  input[10], input[11],
        input[12], input[13], input[14], input[15],
        digest[0],  digest[1],  digest[2],  digest[3],
        digest[4],  digest[5],  digest[6],  digest[7],
        digest[8],  digest[9],  digest[10], digest[11],
        digest[12], digest[13], digest[14], digest[15]
    );

    for (int i = 0; i < 16; i++) {
        if (i % 2 == 0)
            ASSERT_MEM_EQ(test_expected_empty, digest[i], 32);
        else
            ASSERT_MEM_EQ(test_expected_abc, digest[i], 32);
    }
}

/* ============================================================================
 * Consistency Tests - AVX2 vs AVX-512 vs Scalar
 * ============================================================================ */

TEST(sha256_simd_consistency) {
    uint8_t scalar_digest[32];

    /* Verify scalar against known test vector (448-bit / 56-byte input) */
    sha256((uint8_t*)test_input_448, 56, scalar_digest);
    ASSERT_MEM_EQ(test_expected_448, scalar_digest, 32);

    /* For SIMD consistency test, use "abc" (3 bytes) instead of the 56-byte
     * input. The 56-byte message requires TWO SHA-256 blocks (56 + 1 + 8 = 65
     * > 64), but sha256avx2_1B/sha256avx512_1B are single-block (1B) compression
     * functions that process exactly one 64-byte block. Using a message that
     * fits in a single padded block ensures correct comparison with scalar. */
    sha256((uint8_t*)test_input_abc, 3, scalar_digest);
    ASSERT_MEM_EQ(test_expected_abc, scalar_digest, 32);

    /* Compute with AVX2 if available */
    if (sha256_avx2_available()) {
        uint32_t inputs[8][16];
        uint8_t digests[8][32];

        for (int i = 0; i < 8; i++) {
            uint8_t padded[64];
            sha256_pad_block(padded, test_input_abc, 3);
            for (int j = 0; j < 16; j++) {
                inputs[i][j] = ((uint32_t)padded[j*4] << 24) |
                               ((uint32_t)padded[j*4+1] << 16) |
                               ((uint32_t)padded[j*4+2] << 8) |
                               ((uint32_t)padded[j*4+3]);
            }
        }

        sha256avx2_1B(
            inputs[0], inputs[1], inputs[2], inputs[3],
            inputs[4], inputs[5], inputs[6], inputs[7],
            digests[0], digests[1], digests[2], digests[3],
            digests[4], digests[5], digests[6], digests[7]
        );

        for (int i = 0; i < 8; i++) {
            ASSERT_MEM_EQ(scalar_digest, digests[i], 32);
        }
    }

    /* Compute with AVX-512 if available */
    if (sha256_avx512_available()) {
        uint32_t inputs[16][16];
        uint8_t digests[16][32];

        for (int i = 0; i < 16; i++) {
            uint8_t padded[64];
            sha256_pad_block(padded, test_input_abc, 3);
            for (int j = 0; j < 16; j++) {
                inputs[i][j] = ((uint32_t)padded[j*4] << 24) |
                               ((uint32_t)padded[j*4+1] << 16) |
                               ((uint32_t)padded[j*4+2] << 8) |
                               ((uint32_t)padded[j*4+3]);
            }
        }

        sha256avx512_1B(
            inputs[0],  inputs[1],  inputs[2],  inputs[3],
            inputs[4],  inputs[5],  inputs[6],  inputs[7],
            inputs[8],  inputs[9],  inputs[10], inputs[11],
            inputs[12], inputs[13], inputs[14], inputs[15],
            digests[0],  digests[1],  digests[2],  digests[3],
            digests[4],  digests[5],  digests[6],  digests[7],
            digests[8],  digests[9],  digests[10], digests[11],
            digests[12], digests[13], digests[14], digests[15]
        );

        for (int i = 0; i < 16; i++) {
            ASSERT_MEM_EQ(scalar_digest, digests[i], 32);
        }
    }
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

/* Exported function for test runner */
int run_sha256_simd_tests(void) {
    TEST_INIT();

    TEST_SECTION("CPU Feature Detection");
    RUN_TEST(sha256_avx2_detection);
    RUN_TEST(sha256_avx512_detection);

    TEST_SECTION("Scalar SHA256 (Baseline)");
    RUN_TEST(sha256_scalar_empty);
    RUN_TEST(sha256_scalar_abc);
    RUN_TEST(sha256_scalar_448);

    TEST_SECTION("AVX2 SHA256 (8-way parallel)");
    RUN_TEST(sha256_avx2_basic);
    RUN_TEST(sha256_avx2_empty);
    RUN_TEST(sha256_avx2_mixed);

    TEST_SECTION("AVX-512 SHA256 (16-way parallel)");
    RUN_TEST(sha256_avx512_basic);
    RUN_TEST(sha256_avx512_empty);
    RUN_TEST(sha256_avx512_mixed);

    TEST_SECTION("SIMD Consistency");
    RUN_TEST(sha256_simd_consistency);

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_sha256_simd_tests();
}
#endif
