/*
 * test_hash.cpp - Unit tests for hash functions (RIPEMD160, SHA256)
 *
 * Tests:
 * - RIPEMD160 with official test vectors
 * - SHA256 with official test vectors
 * - SIMD implementations (SSE, AVX2, AVX-512)
 * - Edge cases (empty, single byte, long inputs)
 */

#include "test_framework.h"

extern "C" {
#include "src/hash/ripemd160.h"
#include "src/hash/sha256.h"
}

#include <cstring>
#include <cstdio>

/* Helper function to convert hex string to bytes */
static void hex_to_bytes(const char *hex, unsigned char *bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        sscanf(hex + 2*i, "%2hhx", &bytes[i]);
    }
}

/* Helper function to print bytes as hex for debugging */
static void print_hex(const char *label, const unsigned char *data, size_t len) {
    printf("    %s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
    printf("\n");
}

/* ============================================================================
 * RIPEMD160 Test Vectors (from ISO/IEC 10118-3:2004)
 * ============================================================================ */

TEST(ripemd160_empty) {
    unsigned char input[] = "";
    unsigned char digest[20];
    unsigned char expected[20];

    /* RIPEMD160("") = 9c1185a5c5e9fc54612808977ee8f548b2258d31 */
    hex_to_bytes("9c1185a5c5e9fc54612808977ee8f548b2258d31", expected, 20);

    ripemd160(input, 0, digest);
    ASSERT_MEM_EQ(expected, digest, 20);
}

TEST(ripemd160_abc) {
    unsigned char input[] = "abc";
    unsigned char digest[20];
    unsigned char expected[20];

    /* RIPEMD160("abc") = 8eb208f7e05d987a9b044a8e98c6b087f15a0bfc */
    hex_to_bytes("8eb208f7e05d987a9b044a8e98c6b087f15a0bfc", expected, 20);

    ripemd160(input, 3, digest);
    ASSERT_MEM_EQ(expected, digest, 20);
}

TEST(ripemd160_message_digest) {
    unsigned char input[] = "message digest";
    unsigned char digest[20];
    unsigned char expected[20];

    /* RIPEMD160("message digest") = 5d0689ef49d2fae572b881b123a85ffa21595f36 */
    hex_to_bytes("5d0689ef49d2fae572b881b123a85ffa21595f36", expected, 20);

    ripemd160(input, 14, digest);
    ASSERT_MEM_EQ(expected, digest, 20);
}

TEST(ripemd160_alphabet) {
    unsigned char input[] = "abcdefghijklmnopqrstuvwxyz";
    unsigned char digest[20];
    unsigned char expected[20];

    /* RIPEMD160("abcdefghijklmnopqrstuvwxyz") = f71c27109c692c1b56bbdceb5b9d2865b3708dbc */
    hex_to_bytes("f71c27109c692c1b56bbdceb5b9d2865b3708dbc", expected, 20);

    ripemd160(input, 26, digest);
    ASSERT_MEM_EQ(expected, digest, 20);
}

TEST(ripemd160_alphanumeric) {
    unsigned char input[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    unsigned char digest[20];
    unsigned char expected[20];

    /* RIPEMD160("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
       = 12a053384a9c0c88e405a06c27dcf49ada62eb2b */
    hex_to_bytes("12a053384a9c0c88e405a06c27dcf49ada62eb2b", expected, 20);

    ripemd160(input, strlen((char*)input), digest);
    ASSERT_MEM_EQ(expected, digest, 20);
}

TEST(ripemd160_long_az) {
    /* "A...Za...z0...9" repeated */
    unsigned char input[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    unsigned char digest[20];
    unsigned char expected[20];

    /* RIPEMD160("ABCD...xyz0123456789") = b0e20b6e3116640286ed3a87a5713079b21f5189 */
    hex_to_bytes("b0e20b6e3116640286ed3a87a5713079b21f5189", expected, 20);

    ripemd160(input, strlen((char*)input), digest);
    ASSERT_MEM_EQ(expected, digest, 20);
}

TEST(ripemd160_million_a) {
    /* Test with 1 million 'a' characters */
    const size_t len = 1000000;
    unsigned char *input = (unsigned char *)malloc(len);
    ASSERT_NOT_NULL(input);

    memset(input, 'a', len);

    unsigned char digest[20];
    unsigned char expected[20];

    /* RIPEMD160(1,000,000 x 'a') = 52783243c1697bdbe16d37f97f68f08325dc1528 */
    hex_to_bytes("52783243c1697bdbe16d37f97f68f08325dc1528", expected, 20);

    ripemd160(input, len, digest);
    ASSERT_MEM_EQ(expected, digest, 20);

    free(input);
}

/* ============================================================================
 * RIPEMD160 Basic Tests
 * ============================================================================ */

TEST(ripemd160_basic) {
    unsigned char input[] = "test";
    unsigned char digest[20];

    ripemd160(input, 4, digest);

    /* Verify digest is not all zeros */
    int all_zero = 1;
    for (int i = 0; i < 20; i++) {
        if (digest[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    ASSERT_FALSE(all_zero);
}

TEST(ripemd160_32byte_input) {
    /* Test the optimized 32-byte input version */
    unsigned char input[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    unsigned char digest[20];

    ripemd160_32(input, digest);

    /* Verify digest is not all zeros */
    int all_zero = 1;
    for (int i = 0; i < 20; i++) {
        if (digest[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    ASSERT_FALSE(all_zero);
}

TEST(ripemd160_deterministic) {
    /* Same input should always produce same output */
    unsigned char input[] = "deterministic test";
    unsigned char digest1[20];
    unsigned char digest2[20];

    ripemd160(input, strlen((char*)input), digest1);
    ripemd160(input, strlen((char*)input), digest2);

    ASSERT_MEM_EQ(digest1, digest2, 20);
}

TEST(ripemd160_different_inputs) {
    /* Different inputs should produce different outputs */
    unsigned char input1[] = "test1";
    unsigned char input2[] = "test2";
    unsigned char digest1[20];
    unsigned char digest2[20];

    ripemd160(input1, 5, digest1);
    ripemd160(input2, 5, digest2);

    /* Digests should be different */
    int same = (memcmp(digest1, digest2, 20) == 0);
    ASSERT_FALSE(same);
}

TEST(ripemd160_comp_hash) {
    /* Test the comparison helper function */
    unsigned char hash1[20] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    unsigned char hash2[20] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    unsigned char hash3[20] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 99};

    ASSERT_TRUE(ripemd160_comp_hash(hash1, hash2));
    ASSERT_FALSE(ripemd160_comp_hash(hash1, hash3));
}

/* ============================================================================
 * RIPEMD160 SIMD Tests (SSE2, AVX2, AVX-512)
 * ============================================================================ */

TEST(ripemd160_sse_4way) {
    /* Test SSE2 4-way parallel implementation */
    unsigned char input1[32], input2[32], input3[32], input4[32];
    unsigned char digest1[20], digest2[20], digest3[20], digest4[20];
    unsigned char expected1[20], expected2[20], expected3[20], expected4[20];

    /* Prepare 4 different 32-byte inputs */
    for (int i = 0; i < 32; i++) {
        input1[i] = i;
        input2[i] = i + 1;
        input3[i] = i + 2;
        input4[i] = i + 3;
    }

    /* Compute reference digests using scalar version */
    ripemd160_32(input1, expected1);
    ripemd160_32(input2, expected2);
    ripemd160_32(input3, expected3);
    ripemd160_32(input4, expected4);

    /* Compute using SSE2 4-way parallel */
    ripemd160sse_32(input1, input2, input3, input4,
                    digest1, digest2, digest3, digest4);

    /* Verify all 4 digests match */
    ASSERT_MEM_EQ(expected1, digest1, 20);
    ASSERT_MEM_EQ(expected2, digest2, 20);
    ASSERT_MEM_EQ(expected3, digest3, 20);
    ASSERT_MEM_EQ(expected4, digest4, 20);
}

TEST(ripemd160_avx2_8way) {
    /* Test AVX2 8-way parallel implementation (if available) */
    if (!ripemd160_avx2_available()) {
        printf("\n    (Skipped: AVX2 not available)\n");
        return;
    }

    unsigned char input[8][32];
    unsigned char digest[8][20];
    unsigned char expected[8][20];

    /* Prepare 8 different 32-byte inputs */
    for (int j = 0; j < 8; j++) {
        for (int i = 0; i < 32; i++) {
            input[j][i] = i + j;
        }
    }

    /* Compute reference digests using scalar version */
    for (int j = 0; j < 8; j++) {
        ripemd160_32(input[j], expected[j]);
    }

    /* Compute using AVX2 8-way parallel */
    ripemd160avx2_32(input[0], input[1], input[2], input[3],
                     input[4], input[5], input[6], input[7],
                     digest[0], digest[1], digest[2], digest[3],
                     digest[4], digest[5], digest[6], digest[7]);

    /* Verify all 8 digests match */
    for (int j = 0; j < 8; j++) {
        ASSERT_MEM_EQ(expected[j], digest[j], 20);
    }
}

TEST(ripemd160_avx512_16way) {
    /* Test AVX-512 16-way parallel implementation (if available) */
    if (!ripemd160_avx512_available()) {
        printf("\n    (Skipped: AVX-512 not available)\n");
        return;
    }

    unsigned char input[16][32];
    unsigned char digest[16][20];
    unsigned char expected[16][20];

    /* Prepare 16 different 32-byte inputs */
    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 32; i++) {
            input[j][i] = i + j;
        }
    }

    /* Compute reference digests using scalar version */
    for (int j = 0; j < 16; j++) {
        ripemd160_32(input[j], expected[j]);
    }

    /* Compute using AVX-512 16-way parallel */
    ripemd160avx512_32(
        input[0],  input[1],  input[2],  input[3],
        input[4],  input[5],  input[6],  input[7],
        input[8],  input[9],  input[10], input[11],
        input[12], input[13], input[14], input[15],
        digest[0],  digest[1],  digest[2],  digest[3],
        digest[4],  digest[5],  digest[6],  digest[7],
        digest[8],  digest[9],  digest[10], digest[11],
        digest[12], digest[13], digest[14], digest[15]);

    /* Verify all 16 digests match */
    for (int j = 0; j < 16; j++) {
        ASSERT_MEM_EQ(expected[j], digest[j], 20);
    }
}

/* ============================================================================
 * SHA256 Test Vectors (from NIST FIPS 180-4)
 * ============================================================================ */

TEST(sha256_empty) {
    unsigned char input[] = "";
    unsigned char digest[32];
    unsigned char expected[32];

    /* SHA256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
    hex_to_bytes("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", expected, 32);

    sha256(input, 0, digest);
    ASSERT_MEM_EQ(expected, digest, 32);
}

TEST(sha256_abc) {
    unsigned char input[] = "abc";
    unsigned char digest[32];
    unsigned char expected[32];

    /* SHA256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad */
    hex_to_bytes("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", expected, 32);

    sha256(input, 3, digest);
    ASSERT_MEM_EQ(expected, digest, 32);
}

TEST(sha256_long_string) {
    unsigned char input[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    unsigned char digest[32];
    unsigned char expected[32];

    /* SHA256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
       = 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1 */
    hex_to_bytes("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", expected, 32);

    sha256(input, strlen((char*)input), digest);
    ASSERT_MEM_EQ(expected, digest, 32);
}

TEST(sha256_million_a) {
    /* Test with 1 million 'a' characters */
    const size_t len = 1000000;
    unsigned char *input = (unsigned char *)malloc(len);
    ASSERT_NOT_NULL(input);

    memset(input, 'a', len);

    unsigned char digest[32];
    unsigned char expected[32];

    /* SHA256(1,000,000 x 'a') = cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0 */
    hex_to_bytes("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", expected, 32);

    sha256(input, len, digest);
    ASSERT_MEM_EQ(expected, digest, 32);

    free(input);
}

/* ============================================================================
 * SHA256 Basic Tests
 * ============================================================================ */

TEST(sha256_basic) {
    unsigned char input[] = "test";
    unsigned char digest[32];

    sha256(input, 4, digest);

    /* Verify digest is not all zeros */
    int all_zero = 1;
    for (int i = 0; i < 32; i++) {
        if (digest[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    ASSERT_FALSE(all_zero);
}

TEST(sha256_33byte_input) {
    /* Test the optimized 33-byte input version (compressed public key) */
    unsigned char input[33];
    input[0] = 0x02;  /* Compressed pubkey prefix */
    for (int i = 1; i < 33; i++) {
        input[i] = i;
    }
    unsigned char digest[32];

    sha256_33(input, digest);

    /* Verify digest is not all zeros */
    int all_zero = 1;
    for (int i = 0; i < 32; i++) {
        if (digest[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    ASSERT_FALSE(all_zero);
}

TEST(sha256_65byte_input) {
    /* Test the optimized 65-byte input version (uncompressed public key) */
    unsigned char input[65];
    input[0] = 0x04;  /* Uncompressed pubkey prefix */
    for (int i = 1; i < 65; i++) {
        input[i] = i;
    }
    unsigned char digest[32];

    sha256_65(input, digest);

    /* Verify digest is not all zeros */
    int all_zero = 1;
    for (int i = 0; i < 32; i++) {
        if (digest[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    ASSERT_FALSE(all_zero);
}

TEST(sha256_deterministic) {
    /* Same input should always produce same output */
    unsigned char input[] = "deterministic test";
    unsigned char digest1[32];
    unsigned char digest2[32];

    sha256(input, strlen((char*)input), digest1);
    sha256(input, strlen((char*)input), digest2);

    ASSERT_MEM_EQ(digest1, digest2, 32);
}

TEST(sha256_different_inputs) {
    /* Different inputs should produce different outputs */
    unsigned char input1[] = "test1";
    unsigned char input2[] = "test2";
    unsigned char digest1[32];
    unsigned char digest2[32];

    sha256(input1, 5, digest1);
    sha256(input2, 5, digest2);

    /* Digests should be different */
    int same = (memcmp(digest1, digest2, 32) == 0);
    ASSERT_FALSE(same);
}

TEST(sha256_checksum) {
    /* Test SHA256 checksum (first 4 bytes of double SHA256) */
    unsigned char input[] = "test checksum";
    unsigned char checksum[4];

    sha256_checksum(input, strlen((char*)input), checksum);

    /* Verify checksum is not all zeros */
    int all_zero = 1;
    for (int i = 0; i < 4; i++) {
        if (checksum[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    ASSERT_FALSE(all_zero);
}

/* ============================================================================
 * CRIPEMD160 Class Tests
 * ============================================================================ */

TEST(cripemd160_class_basic) {
    /* Test the C++ class interface */
    CRIPEMD160 hasher;
    unsigned char input[] = "abc";
    unsigned char digest[20];
    unsigned char expected[20];

    hex_to_bytes("8eb208f7e05d987a9b044a8e98c6b087f15a0bfc", expected, 20);

    hasher.Write(input, 3);
    hasher.Finalize(digest);

    ASSERT_MEM_EQ(expected, digest, 20);
}

TEST(cripemd160_class_incremental) {
    /* Test incremental hashing */
    CRIPEMD160 hasher;
    unsigned char part1[] = "abc";
    unsigned char part2[] = "def";
    unsigned char digest[20];

    /* Hash "abcdef" in two parts */
    hasher.Write(part1, 3);
    hasher.Write(part2, 3);
    hasher.Finalize(digest);

    /* Compare with single-pass hash of "abcdef" */
    unsigned char input_full[] = "abcdef";
    unsigned char expected[20];
    ripemd160(input_full, 6, expected);

    ASSERT_MEM_EQ(expected, digest, 20);
}

/* ============================================================================
 * Edge Cases and Error Handling
 * ============================================================================ */

TEST(ripemd160_single_byte) {
    unsigned char input[] = "a";
    unsigned char digest[20];
    unsigned char expected[20];

    /* RIPEMD160("a") = 0bdc9d2d256b3ee9daae347be6f4dc835a467ffe */
    hex_to_bytes("0bdc9d2d256b3ee9daae347be6f4dc835a467ffe", expected, 20);

    ripemd160(input, 1, digest);
    ASSERT_MEM_EQ(expected, digest, 20);
}

TEST(sha256_single_byte) {
    unsigned char input[] = "a";
    unsigned char digest[32];
    unsigned char expected[32];

    /* SHA256("a") = ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb */
    hex_to_bytes("ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb", expected, 32);

    sha256(input, 1, digest);
    ASSERT_MEM_EQ(expected, digest, 32);
}

TEST(ripemd160_binary_data) {
    /* Test with binary data (all possible byte values) */
    unsigned char input[256];
    for (int i = 0; i < 256; i++) {
        input[i] = i;
    }

    unsigned char digest[20];
    ripemd160(input, 256, digest);

    /* Just verify it doesn't crash and produces output */
    int all_zero = 1;
    for (int i = 0; i < 20; i++) {
        if (digest[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    ASSERT_FALSE(all_zero);
}

TEST(sha256_binary_data) {
    /* Test with binary data (all possible byte values) */
    unsigned char input[256];
    for (int i = 0; i < 256; i++) {
        input[i] = i;
    }

    unsigned char digest[32];
    sha256(input, 256, digest);

    /* Just verify it doesn't crash and produces output */
    int all_zero = 1;
    for (int i = 0; i < 32; i++) {
        if (digest[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    ASSERT_FALSE(all_zero);
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(int argc, char *argv[]) {
    TEST_INIT();

    TEST_SECTION("RIPEMD160 Test Vectors");
    RUN_TEST(ripemd160_empty);
    RUN_TEST(ripemd160_abc);
    RUN_TEST(ripemd160_message_digest);
    RUN_TEST(ripemd160_alphabet);
    RUN_TEST(ripemd160_alphanumeric);
    RUN_TEST(ripemd160_long_az);
    RUN_TEST(ripemd160_million_a);

    TEST_SECTION("RIPEMD160 Basic Tests");
    RUN_TEST(ripemd160_basic);
    RUN_TEST(ripemd160_32byte_input);
    RUN_TEST(ripemd160_deterministic);
    RUN_TEST(ripemd160_different_inputs);
    RUN_TEST(ripemd160_comp_hash);

    TEST_SECTION("RIPEMD160 SIMD Tests");
    RUN_TEST(ripemd160_sse_4way);
    RUN_TEST(ripemd160_avx2_8way);
    RUN_TEST(ripemd160_avx512_16way);

    TEST_SECTION("SHA256 Test Vectors");
    RUN_TEST(sha256_empty);
    RUN_TEST(sha256_abc);
    RUN_TEST(sha256_long_string);
    RUN_TEST(sha256_million_a);

    TEST_SECTION("SHA256 Basic Tests");
    RUN_TEST(sha256_basic);
    RUN_TEST(sha256_33byte_input);
    RUN_TEST(sha256_65byte_input);
    RUN_TEST(sha256_deterministic);
    RUN_TEST(sha256_different_inputs);
    RUN_TEST(sha256_checksum);

    TEST_SECTION("CRIPEMD160 Class Tests");
    RUN_TEST(cripemd160_class_basic);
    RUN_TEST(cripemd160_class_incremental);

    TEST_SECTION("Edge Cases");
    RUN_TEST(ripemd160_single_byte);
    RUN_TEST(sha256_single_byte);
    RUN_TEST(ripemd160_binary_data);
    RUN_TEST(sha256_binary_data);

    return TEST_RESULTS();
}
