/*
 * test_sha512_simd.cpp - Unit tests for SHA512 SIMD implementations
 *
 * Tests AVX2 and AVX-512 optimized SHA512 functions:
 * - CPU feature detection
 * - Correctness vs scalar implementation
 * - Known test vectors (NIST/FIPS)
 * - Parallel processing (4-way AVX2, 8-way AVX-512)
 */

#include "test_framework.h"
#include "src/hash/sha512.h"
#include "src/hash/sha512_avx2.h"
#include "src/hash/sha512_avx512.h"
#include <cstring>  /* memcmp, memset */
#include <cstdio>   /* printf */

/* ============================================================================
 * Test Vectors - NIST SHA-512 Test Vectors
 * ============================================================================ */

/* Empty string: "" */
static const uint8_t test_input_empty[] = "";
static const uint8_t test_expected_empty[64] = {
    0xcf, 0x83, 0xe1, 0x35, 0x7e, 0xef, 0xb8, 0xbd, 0xf1, 0x54, 0x28, 0x50,
    0xd6, 0x6d, 0x80, 0x07, 0xd6, 0x20, 0xe4, 0x05, 0x0b, 0x57, 0x15, 0xdc,
    0x83, 0xf4, 0xa9, 0x21, 0xd3, 0x6c, 0xe9, 0xce, 0x47, 0xd0, 0xd1, 0x3c,
    0x5d, 0x85, 0xf2, 0xb0, 0xff, 0x83, 0x18, 0xd2, 0x87, 0x7e, 0xec, 0x2f,
    0x63, 0xb9, 0x31, 0xbd, 0x47, 0x41, 0x7a, 0x81, 0xa5, 0x38, 0x32, 0x7a,
    0xf9, 0x27, 0xda, 0x3e
};

/* "abc" */
static const uint8_t test_input_abc[] = "abc";
static const uint8_t test_expected_abc[64] = {
    0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba, 0xcc, 0x41, 0x73, 0x49,
    0xae, 0x20, 0x41, 0x31, 0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
    0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a, 0x21, 0x92, 0x99, 0x2a,
    0x27, 0x4f, 0xc1, 0xa8, 0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
    0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e, 0x2a, 0x9a, 0xc9, 0x4f,
    0xa5, 0x4c, 0xa4, 0x9f
};

/* "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" */
static const uint8_t test_input_448[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
static const uint8_t test_expected_448[64] = {
    0x20, 0x4a, 0x8f, 0xc6, 0xdd, 0xa8, 0x2f, 0x0a, 0x0c, 0xed, 0x7b, 0xeb,
    0x8e, 0x08, 0xa4, 0x16, 0x57, 0xc1, 0x6e, 0xf4, 0x68, 0xb2, 0x28, 0xa8,
    0x27, 0x9b, 0xe3, 0x31, 0xa7, 0x03, 0xc3, 0x35, 0x96, 0xfd, 0x15, 0xc1,
    0x3b, 0x1b, 0x07, 0xf9, 0xaa, 0x1d, 0x3b, 0xea, 0x57, 0x78, 0x9c, 0xa0,
    0x31, 0xad, 0x85, 0xc7, 0xa7, 0x1d, 0xd7, 0x03, 0x54, 0xec, 0x63, 0x12,
    0x38, 0xca, 0x34, 0x45
};

/* Helper function to print hash for debugging */
static void print_hash(const char *label, const uint8_t *hash, size_t len) {
    printf("    %s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", hash[i]);
    }
    printf("\n");
}

/* ============================================================================
 * CPU Feature Detection Tests
 * ============================================================================ */

TEST(sha512_avx2_detection) {
    int avx2_supported = sha512_avx2_available();
    /* Just verify the function runs without crashing */
    /* Result depends on CPU - both 0 and 1 are valid */
    ASSERT_TRUE(avx2_supported == 0 || avx2_supported == 1);
}

TEST(sha512_avx512_detection) {
    int avx512_supported = sha512_avx512_available();
    /* Just verify the function runs without crashing */
    /* Result depends on CPU - both 0 and 1 are valid */
    ASSERT_TRUE(avx512_supported == 0 || avx512_supported == 1);
}

/* ============================================================================
 * Scalar SHA512 Tests (Baseline)
 * ============================================================================ */

TEST(sha512_scalar_empty) {
    uint8_t digest[64];
    sha512((unsigned char*)test_input_empty, 0, digest);
    ASSERT_MEM_EQ(test_expected_empty, digest, 64);
}

TEST(sha512_scalar_abc) {
    uint8_t digest[64];
    sha512((unsigned char*)test_input_abc, 3, digest);
    ASSERT_MEM_EQ(test_expected_abc, digest, 64);
}

TEST(sha512_scalar_448) {
    uint8_t digest[64];
    sha512((unsigned char*)test_input_448, 56, digest);
    ASSERT_MEM_EQ(test_expected_448, digest, 64);
}

/* ============================================================================
 * AVX2 Tests (4-way parallel)
 * Test suite: test_sha512_avx2, test_sha512_avx512
 * ============================================================================ */

TEST(sha512_avx2_basic) {
    if (!sha512_avx2_available()) {
        /* Skip test if AVX2 not supported */
        return;
    }

    /* Prepare 4 copies of "abc" padded to 128 bytes (SHA-512 block size) */
    uint8_t input0[128], input1[128], input2[128], input3[128];
    uint8_t digest0[64], digest1[64], digest2[64], digest3[64];
    uint8_t scalar_digest[64];

    /* Initialize inputs with test data */
    memset(input0, 0, 128);
    memset(input1, 0, 128);
    memset(input2, 0, 128);
    memset(input3, 0, 128);

    memcpy(input0, test_input_abc, 3);
    memcpy(input1, test_input_abc, 3);
    memcpy(input2, test_input_abc, 3);
    memcpy(input3, test_input_abc, 3);

    /* Compute with AVX2 (8-way parallel from sha512.h declaration) */
    /* Note: sha512avx2_128 takes 8 inputs as per sha512.h */
    uint8_t input4[128], input5[128], input6[128], input7[128];
    uint8_t digest4[64], digest5[64], digest6[64], digest7[64];

    memset(input4, 0, 128);
    memset(input5, 0, 128);
    memset(input6, 0, 128);
    memset(input7, 0, 128);

    memcpy(input4, test_input_abc, 3);
    memcpy(input5, test_input_abc, 3);
    memcpy(input6, test_input_abc, 3);
    memcpy(input7, test_input_abc, 3);

    sha512avx2_128(
        input0, input1, input2, input3, input4, input5, input6, input7,
        digest0, digest1, digest2, digest3, digest4, digest5, digest6, digest7
    );

    /* Compute with scalar implementation for comparison */
    sha512((unsigned char*)test_input_abc, 3, scalar_digest);

    /* Verify all 8 digests match the scalar result */
    ASSERT_MEM_EQ(scalar_digest, digest0, 64);
    ASSERT_MEM_EQ(scalar_digest, digest1, 64);
    ASSERT_MEM_EQ(scalar_digest, digest2, 64);
    ASSERT_MEM_EQ(scalar_digest, digest3, 64);
    ASSERT_MEM_EQ(scalar_digest, digest4, 64);
    ASSERT_MEM_EQ(scalar_digest, digest5, 64);
    ASSERT_MEM_EQ(scalar_digest, digest6, 64);
    ASSERT_MEM_EQ(scalar_digest, digest7, 64);
}

TEST(sha512_avx2_empty) {
    if (!sha512_avx2_available()) {
        return;
    }

    uint8_t input0[128], input1[128], input2[128], input3[128];
    uint8_t input4[128], input5[128], input6[128], input7[128];
    uint8_t digest0[64], digest1[64], digest2[64], digest3[64];
    uint8_t digest4[64], digest5[64], digest6[64], digest7[64];

    memset(input0, 0, 128);
    memset(input1, 0, 128);
    memset(input2, 0, 128);
    memset(input3, 0, 128);
    memset(input4, 0, 128);
    memset(input5, 0, 128);
    memset(input6, 0, 128);
    memset(input7, 0, 128);

    sha512avx2_128(
        input0, input1, input2, input3, input4, input5, input6, input7,
        digest0, digest1, digest2, digest3, digest4, digest5, digest6, digest7
    );

    /* All digests should match empty string hash */
    ASSERT_MEM_EQ(test_expected_empty, digest0, 64);
    ASSERT_MEM_EQ(test_expected_empty, digest1, 64);
    ASSERT_MEM_EQ(test_expected_empty, digest2, 64);
    ASSERT_MEM_EQ(test_expected_empty, digest3, 64);
}

TEST(sha512_avx2_mixed) {
    if (!sha512_avx2_available()) {
        return;
    }

    /* Test with different inputs in each lane */
    uint8_t input0[128], input1[128], input2[128], input3[128];
    uint8_t input4[128], input5[128], input6[128], input7[128];
    uint8_t digest0[64], digest1[64], digest2[64], digest3[64];
    uint8_t digest4[64], digest5[64], digest6[64], digest7[64];

    memset(input0, 0, 128);
    memset(input1, 0, 128);
    memset(input2, 0, 128);
    memset(input3, 0, 128);
    memset(input4, 0, 128);
    memset(input5, 0, 128);
    memset(input6, 0, 128);
    memset(input7, 0, 128);

    /* Lane 0: empty, Lane 1: "abc", Lane 2: empty, Lane 3: "abc" */
    memcpy(input1, test_input_abc, 3);
    memcpy(input3, test_input_abc, 3);
    memcpy(input5, test_input_abc, 3);
    memcpy(input7, test_input_abc, 3);

    sha512avx2_128(
        input0, input1, input2, input3, input4, input5, input6, input7,
        digest0, digest1, digest2, digest3, digest4, digest5, digest6, digest7
    );

    /* Verify each lane independently */
    ASSERT_MEM_EQ(test_expected_empty, digest0, 64);
    ASSERT_MEM_EQ(test_expected_abc, digest1, 64);
    ASSERT_MEM_EQ(test_expected_empty, digest2, 64);
    ASSERT_MEM_EQ(test_expected_abc, digest3, 64);
}

/* ============================================================================
 * AVX-512 Tests (16-way parallel)
 * ============================================================================ */

TEST(sha512_avx512_basic) {
    if (!sha512_avx512_available()) {
        /* Skip test if AVX-512 not supported */
        return;
    }

    /* Prepare 16 copies of "abc" padded to 128 bytes */
    uint8_t input[16][128];
    uint8_t digest[16][64];
    uint8_t scalar_digest[64];

    for (int i = 0; i < 16; i++) {
        memset(input[i], 0, 128);
        memcpy(input[i], test_input_abc, 3);
    }

    /* Compute with AVX-512 (16-way parallel) */
    sha512avx512_128(
        input[0],  input[1],  input[2],  input[3],
        input[4],  input[5],  input[6],  input[7],
        input[8],  input[9],  input[10], input[11],
        input[12], input[13], input[14], input[15],
        digest[0],  digest[1],  digest[2],  digest[3],
        digest[4],  digest[5],  digest[6],  digest[7],
        digest[8],  digest[9],  digest[10], digest[11],
        digest[12], digest[13], digest[14], digest[15]
    );

    /* Compute with scalar implementation for comparison */
    sha512((unsigned char*)test_input_abc, 3, scalar_digest);

    /* Verify all 16 digests match the scalar result */
    for (int i = 0; i < 16; i++) {
        ASSERT_MEM_EQ(scalar_digest, digest[i], 64);
    }
}

TEST(sha512_avx512_empty) {
    if (!sha512_avx512_available()) {
        return;
    }

    uint8_t input[16][128];
    uint8_t digest[16][64];

    for (int i = 0; i < 16; i++) {
        memset(input[i], 0, 128);
    }

    sha512avx512_128(
        input[0],  input[1],  input[2],  input[3],
        input[4],  input[5],  input[6],  input[7],
        input[8],  input[9],  input[10], input[11],
        input[12], input[13], input[14], input[15],
        digest[0],  digest[1],  digest[2],  digest[3],
        digest[4],  digest[5],  digest[6],  digest[7],
        digest[8],  digest[9],  digest[10], digest[11],
        digest[12], digest[13], digest[14], digest[15]
    );

    /* All digests should match empty string hash */
    for (int i = 0; i < 16; i++) {
        ASSERT_MEM_EQ(test_expected_empty, digest[i], 64);
    }
}

TEST(sha512_avx512_mixed) {
    if (!sha512_avx512_available()) {
        return;
    }

    /* Test with alternating empty and "abc" inputs */
    uint8_t input[16][128];
    uint8_t digest[16][64];

    for (int i = 0; i < 16; i++) {
        memset(input[i], 0, 128);
        if (i % 2 == 1) {
            memcpy(input[i], test_input_abc, 3);
        }
    }

    sha512avx512_128(
        input[0],  input[1],  input[2],  input[3],
        input[4],  input[5],  input[6],  input[7],
        input[8],  input[9],  input[10], input[11],
        input[12], input[13], input[14], input[15],
        digest[0],  digest[1],  digest[2],  digest[3],
        digest[4],  digest[5],  digest[6],  digest[7],
        digest[8],  digest[9],  digest[10], digest[11],
        digest[12], digest[13], digest[14], digest[15]
    );

    /* Verify alternating pattern */
    for (int i = 0; i < 16; i++) {
        if (i % 2 == 0) {
            ASSERT_MEM_EQ(test_expected_empty, digest[i], 64);
        } else {
            ASSERT_MEM_EQ(test_expected_abc, digest[i], 64);
        }
    }
}

/* ============================================================================
 * Consistency Tests - AVX2 vs AVX-512 vs Scalar
 * ============================================================================ */

TEST(sha512_simd_consistency) {
    uint8_t scalar_digest[64];
    uint8_t avx2_digest0[64], avx2_digest1[64], avx2_digest2[64], avx2_digest3[64];
    uint8_t avx2_digest4[64], avx2_digest5[64], avx2_digest6[64], avx2_digest7[64];
    uint8_t avx512_digest0[64];
    uint8_t input[128];

    memset(input, 0, 128);
    memcpy(input, test_input_448, 56);

    /* Compute with scalar */
    sha512((unsigned char*)test_input_448, 56, scalar_digest);

    /* Verify against known test vector first */
    ASSERT_MEM_EQ(test_expected_448, scalar_digest, 64);

    /* Compute with AVX2 if available */
    if (sha512_avx2_available()) {
        uint8_t inputs[8][128];
        for (int i = 0; i < 8; i++) {
            memset(inputs[i], 0, 128);
            memcpy(inputs[i], test_input_448, 56);
        }

        sha512avx2_128(
            inputs[0], inputs[1], inputs[2], inputs[3],
            inputs[4], inputs[5], inputs[6], inputs[7],
            avx2_digest0, avx2_digest1, avx2_digest2, avx2_digest3,
            avx2_digest4, avx2_digest5, avx2_digest6, avx2_digest7
        );

        ASSERT_MEM_EQ(scalar_digest, avx2_digest0, 64);
    }

    /* Compute with AVX-512 if available */
    if (sha512_avx512_available()) {
        uint8_t inputs[16][128];
        uint8_t digests[16][64];

        for (int i = 0; i < 16; i++) {
            memset(inputs[i], 0, 128);
            memcpy(inputs[i], test_input_448, 56);
        }

        sha512avx512_128(
            inputs[0],  inputs[1],  inputs[2],  inputs[3],
            inputs[4],  inputs[5],  inputs[6],  inputs[7],
            inputs[8],  inputs[9],  inputs[10], inputs[11],
            inputs[12], inputs[13], inputs[14], inputs[15],
            digests[0],  digests[1],  digests[2],  digests[3],
            digests[4],  digests[5],  digests[6],  digests[7],
            digests[8],  digests[9],  digests[10], digests[11],
            digests[12], digests[13], digests[14], digests[15]
        );

        ASSERT_MEM_EQ(scalar_digest, digests[0], 64);
    }
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(int argc, char *argv[]) {
    TEST_INIT();

    TEST_SECTION("CPU Feature Detection");
    RUN_TEST(sha512_avx2_detection);
    RUN_TEST(sha512_avx512_detection);

    TEST_SECTION("Scalar SHA512 (Baseline)");
    RUN_TEST(sha512_scalar_empty);
    RUN_TEST(sha512_scalar_abc);
    RUN_TEST(sha512_scalar_448);

    TEST_SECTION("AVX2 SHA512 (8-way parallel)");
    RUN_TEST(sha512_avx2_basic);
    RUN_TEST(sha512_avx2_empty);
    RUN_TEST(sha512_avx2_mixed);

    TEST_SECTION("AVX-512 SHA512 (16-way parallel)");
    RUN_TEST(sha512_avx512_basic);
    RUN_TEST(sha512_avx512_empty);
    RUN_TEST(sha512_avx512_mixed);

    TEST_SECTION("SIMD Consistency");
    RUN_TEST(sha512_simd_consistency);

    return TEST_RESULTS();
}
