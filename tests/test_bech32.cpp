/*
 * test_bech32.cpp - Unit tests for Bech32 encoding/decoding
 *
 * Tests:
 * - BIP-173 (Bech32) official test vectors
 * - BIP-350 (Bech32m) official test vectors
 * - SegWit address encoding/decoding (P2WPKH, P2WSH, Taproot)
 * - Edge cases (invalid checksums, mixed case, invalid characters)
 * - Real Bitcoin mainnet and testnet addresses
 */

#include "test_framework.h"
#include "bech32/bech32.h"
#include <cstring>
#include <cstdio>

/* Helper function to convert hex string to bytes */
static void hex_to_bytes(const char *hex, uint8_t *bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        sscanf(hex + 2*i, "%2hhx", &bytes[i]);
    }
}

/* ============================================================================
 * BIP-173 Bech32 Test Vectors (Valid)
 * ============================================================================ */

TEST(bech32_valid_simple) {
    /* Simple test from BIP-173 */
    const char *input = "a12uel5l";
    char hrp[84];
    uint8_t data[84];
    size_t data_len;

    int result = bech32_decode(hrp, data, &data_len, input);
    ASSERT_TRUE(result == 1);
    ASSERT_STR_EQ("a", hrp);
    ASSERT_EQ(0, data_len);
}

TEST(bech32_valid_uppercase) {
    /* All uppercase test */
    const char *input = "A12UEL5L";
    char hrp[84];
    uint8_t data[84];
    size_t data_len;

    int result = bech32_decode(hrp, data, &data_len, input);
    ASSERT_TRUE(result == 1);
    ASSERT_STR_EQ("a", hrp);  /* HRP should be lowercased */
    ASSERT_EQ(0, data_len);
}

TEST(bech32_valid_longer) {
    /* Longer HRP and data */
    const char *input = "abcdef1qpzry9x8gf2tvdw0s3jn54khce6mua7lmqqqxw";
    char hrp[84];
    uint8_t data[84];
    size_t data_len;

    int result = bech32_decode(hrp, data, &data_len, input);
    ASSERT_TRUE(result == 1);
    ASSERT_STR_EQ("abcdef", hrp);
    ASSERT_EQ(32, data_len);

    /* Expected data: 0,1,2,...,31 */
    for (size_t i = 0; i < data_len; i++) {
        ASSERT_EQ(i, data[i]);
    }
}

TEST(bech32_valid_max_length) {
    /* Maximum length test (90 characters) */
    const char *input = "split1checkupstagehandshakeupstreamerranterredcaperred2y9e3w";
    char hrp[84];
    uint8_t data[84];
    size_t data_len;

    int result = bech32_decode(hrp, data, &data_len, input);
    ASSERT_TRUE(result == 1);
    ASSERT_STR_EQ("split", hrp);
}

/* ============================================================================
 * BIP-173 Bech32 Test Vectors (Invalid)
 * ============================================================================ */

TEST(bech32_invalid_mixed_case) {
    /* Mixed case (uppercase and lowercase) is invalid */
    const char *input = "HRP1Aaaaaa";
    char hrp[84];
    uint8_t data[84];
    size_t data_len;

    int result = bech32_decode(hrp, data, &data_len, input);
    ASSERT_FALSE(result == 1);  /* Should fail */
}

TEST(bech32_invalid_no_separator) {
    /* No separator character */
    const char *input = "nothingspecial";
    char hrp[84];
    uint8_t data[84];
    size_t data_len;

    int result = bech32_decode(hrp, data, &data_len, input);
    ASSERT_FALSE(result == 1);  /* Should fail */
}

TEST(bech32_invalid_empty_hrp) {
    /* Empty HRP */
    const char *input = "1pzry9x8gf2tvdw0s3jn54khce6mua7lcw20c";
    char hrp[84];
    uint8_t data[84];
    size_t data_len;

    int result = bech32_decode(hrp, data, &data_len, input);
    ASSERT_FALSE(result == 1);  /* Should fail */
}

TEST(bech32_invalid_bad_checksum) {
    /* Invalid checksum */
    const char *input = "a12uel5x";  /* Changed last character */
    char hrp[84];
    uint8_t data[84];
    size_t data_len;

    int result = bech32_decode(hrp, data, &data_len, input);
    ASSERT_FALSE(result == 1);  /* Should fail */
}

TEST(bech32_invalid_too_short) {
    /* Too short (less than 8 characters) */
    const char *input = "a1b2c";
    char hrp[84];
    uint8_t data[84];
    size_t data_len;

    int result = bech32_decode(hrp, data, &data_len, input);
    ASSERT_FALSE(result == 1);  /* Should fail */
}

/* ============================================================================
 * Bech32 Encoding Tests
 * ============================================================================ */

TEST(bech32_encode_simple) {
    /* Encode simple data */
    const char *hrp = "test";
    uint8_t data[] = {0, 1, 2, 3, 4};
    char output[100];

    int result = bech32_encode(output, hrp, data, 5);
    ASSERT_TRUE(result == 1);

    /* Verify we can decode it back */
    char hrp_decoded[84];
    uint8_t data_decoded[84];
    size_t data_len;

    result = bech32_decode(hrp_decoded, data_decoded, &data_len, output);
    ASSERT_TRUE(result == 1);
    ASSERT_STR_EQ(hrp, hrp_decoded);
    ASSERT_EQ(5, data_len);
    ASSERT_MEM_EQ(data, data_decoded, 5);
}

TEST(bech32_encode_decode_roundtrip) {
    /* Test encode -> decode roundtrip with 5-bit data */
    const char *hrp = "bc";
    /* Use 5-bit values (0-31) for low-level bech32_encode */
    uint8_t original_data[20] = {
        0, 14, 20, 15, 7, 13, 9, 10, 18, 3,
        5, 24, 2, 16, 25, 1, 11, 30, 8, 6
    };
    char encoded[100];

    int result = bech32_encode(encoded, hrp, original_data, 20);
    ASSERT_TRUE(result == 1);

    /* Decode and verify */
    char hrp_decoded[84];
    uint8_t data_decoded[84];
    size_t data_len;

    result = bech32_decode(hrp_decoded, data_decoded, &data_len, encoded);
    ASSERT_TRUE(result == 1);
    ASSERT_STR_EQ(hrp, hrp_decoded);
    ASSERT_EQ(20, data_len);
    ASSERT_MEM_EQ(original_data, data_decoded, 20);
}

/* ============================================================================
 * SegWit Address Tests (BIP-173)
 * ============================================================================ */

TEST(segwit_p2wpkh_mainnet) {
    /* P2WPKH mainnet address from BIP-173 */
    const char *addr = "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4";
    int witver;
    uint8_t witprog[40];
    size_t witprog_len;

    int result = segwit_addr_decode(&witver, witprog, &witprog_len, "bc", addr);
    ASSERT_TRUE(result == 1);
    ASSERT_EQ(0, witver);
    ASSERT_EQ(20, witprog_len);

    /* Expected witness program (from BIP-173) */
    uint8_t expected[20];
    hex_to_bytes("751e76e8199196d454941c45d1b3a323f1433bd6", expected, 20);
    ASSERT_MEM_EQ(expected, witprog, 20);
}

TEST(segwit_p2wsh_mainnet) {
    /* P2WSH mainnet address from BIP-173 */
    const char *addr = "bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3";
    int witver;
    uint8_t witprog[40];
    size_t witprog_len;

    int result = segwit_addr_decode(&witver, witprog, &witprog_len, "bc", addr);
    ASSERT_TRUE(result == 1);
    ASSERT_EQ(0, witver);
    ASSERT_EQ(32, witprog_len);

    /* Expected witness program (from BIP-173) */
    uint8_t expected[32];
    hex_to_bytes("1863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262", expected, 32);
    ASSERT_MEM_EQ(expected, witprog, 32);
}

TEST(segwit_p2wpkh_testnet) {
    /* P2WPKH testnet address from BIP-173 */
    const char *addr = "tb1qw508d6qejxtdg4y5r3zarvary0c5xw7kxpjzsx";
    int witver;
    uint8_t witprog[40];
    size_t witprog_len;

    int result = segwit_addr_decode(&witver, witprog, &witprog_len, "tb", addr);
    ASSERT_TRUE(result == 1);
    ASSERT_EQ(0, witver);
    ASSERT_EQ(20, witprog_len);

    /* Expected witness program (same hash as mainnet test) */
    uint8_t expected[20];
    hex_to_bytes("751e76e8199196d454941c45d1b3a323f1433bd6", expected, 20);
    ASSERT_MEM_EQ(expected, witprog, 20);
}

TEST(segwit_encode_p2wpkh) {
    /* Encode P2WPKH address */
    uint8_t witprog[20];
    hex_to_bytes("751e76e8199196d454941c45d1b3a323f1433bd6", witprog, 20);

    char output[100];
    int result = segwit_addr_encode(output, "bc", 0, witprog, 20);
    ASSERT_TRUE(result == 1);

    /* Expected address from BIP-173 */
    ASSERT_STR_EQ("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", output);
}

TEST(segwit_encode_p2wsh) {
    /* Encode P2WSH address */
    uint8_t witprog[32];
    hex_to_bytes("1863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262", witprog, 32);

    char output[100];
    int result = segwit_addr_encode(output, "bc", 0, witprog, 32);
    ASSERT_TRUE(result == 1);

    /* Expected address from BIP-173 */
    ASSERT_STR_EQ("bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3", output);
}

TEST(segwit_encode_decode_roundtrip) {
    /* Test SegWit encode -> decode roundtrip */
    uint8_t original_witprog[20];
    hex_to_bytes("001122334455667788899aabbccddeeff0011223", original_witprog, 20);

    char encoded[100];
    int result = segwit_addr_encode(encoded, "bc", 0, original_witprog, 20);
    ASSERT_TRUE(result == 1);

    /* Decode and verify */
    int witver;
    uint8_t decoded_witprog[40];
    size_t witprog_len;

    result = segwit_addr_decode(&witver, decoded_witprog, &witprog_len, "bc", encoded);
    ASSERT_TRUE(result == 1);
    ASSERT_EQ(0, witver);
    ASSERT_EQ(20, witprog_len);
    ASSERT_MEM_EQ(original_witprog, decoded_witprog, 20);
}

/* ============================================================================
 * BIP-350 Bech32m Tests (Taproot - Witness v1+)
 * ============================================================================ */

TEST(bech32m_taproot_mainnet) {
    /* Taproot (v1) mainnet address from BIP-350 */
    const char *addr = "bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqzk5jj0";
    int witver;
    uint8_t witprog[40];
    size_t witprog_len;

    int result = segwit_addr_decode(&witver, witprog, &witprog_len, "bc", addr);
    ASSERT_TRUE(result == 1);
    ASSERT_EQ(1, witver);
    ASSERT_EQ(32, witprog_len);

    /* Expected witness program (from BIP-350) */
    uint8_t expected[32];
    hex_to_bytes("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798", expected, 32);
    ASSERT_MEM_EQ(expected, witprog, 32);
}

TEST(bech32m_encode_taproot) {
    /* Encode Taproot (v1) address */
    uint8_t witprog[32];
    hex_to_bytes("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798", witprog, 32);

    char output[100];
    int result = segwit_addr_encode(output, "bc", 1, witprog, 32);
    ASSERT_TRUE(result == 1);

    /* Expected address from BIP-350 */
    ASSERT_STR_EQ("bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqzk5jj0", output);
}

TEST(bech32m_testnet_taproot) {
    /* Taproot testnet address */
    const char *addr = "tb1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vq47zagq";
    int witver;
    uint8_t witprog[40];
    size_t witprog_len;

    int result = segwit_addr_decode(&witver, witprog, &witprog_len, "tb", addr);
    ASSERT_TRUE(result == 1);
    ASSERT_EQ(1, witver);
    ASSERT_EQ(32, witprog_len);

    /* Expected witness program (same hash as mainnet) */
    uint8_t expected[32];
    hex_to_bytes("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798", expected, 32);
    ASSERT_MEM_EQ(expected, witprog, 32);
}

/* ============================================================================
 * Invalid SegWit Address Tests
 * ============================================================================ */

TEST(segwit_invalid_wrong_hrp) {
    /* Decode with wrong HRP should fail */
    const char *addr = "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4";
    int witver;
    uint8_t witprog[40];
    size_t witprog_len;

    /* Try to decode mainnet address as testnet */
    int result = segwit_addr_decode(&witver, witprog, &witprog_len, "tb", addr);
    ASSERT_FALSE(result == 1);  /* Should fail */
}

TEST(segwit_invalid_witness_version) {
    /* Invalid witness version (>16) */
    uint8_t witprog[20];
    memset(witprog, 0, 20);
    char output[100];

    int result = segwit_addr_encode(output, "bc", 17, witprog, 20);
    ASSERT_FALSE(result == 1);  /* Should fail */
}

TEST(segwit_invalid_v0_length) {
    /* v0 witness program must be 20 or 32 bytes */
    uint8_t witprog[25];
    memset(witprog, 0, 25);
    char output[100];

    int result = segwit_addr_encode(output, "bc", 0, witprog, 25);
    ASSERT_FALSE(result == 1);  /* Should fail */
}

TEST(segwit_invalid_program_length) {
    /* Witness program too short (< 2 bytes) */
    uint8_t witprog[1] = {0};
    char output[100];

    int result = segwit_addr_encode(output, "bc", 1, witprog, 1);
    ASSERT_FALSE(result == 1);  /* Should fail */
}

/* ============================================================================
 * Edge Cases and Boundary Tests
 * ============================================================================ */

TEST(bech32_edge_all_zeros) {
    /* All zero data */
    const char *hrp = "test";
    uint8_t data[20];
    memset(data, 0, 20);
    char encoded[100];

    int result = bech32_encode(encoded, hrp, data, 20);
    ASSERT_TRUE(result == 1);

    /* Decode and verify */
    char hrp_decoded[84];
    uint8_t data_decoded[84];
    size_t data_len;

    result = bech32_decode(hrp_decoded, data_decoded, &data_len, encoded);
    ASSERT_TRUE(result == 1);
    ASSERT_MEM_EQ(data, data_decoded, 20);
}

TEST(bech32_edge_all_ones) {
    /* All 31 data (maximum 5-bit value) */
    const char *hrp = "test";
    uint8_t data[20];
    memset(data, 31, 20);
    char encoded[100];

    int result = bech32_encode(encoded, hrp, data, 20);
    ASSERT_TRUE(result == 1);

    /* Decode and verify */
    char hrp_decoded[84];
    uint8_t data_decoded[84];
    size_t data_len;

    result = bech32_decode(hrp_decoded, data_decoded, &data_len, encoded);
    ASSERT_TRUE(result == 1);
    ASSERT_MEM_EQ(data, data_decoded, 20);
}

TEST(bech32_deterministic) {
    /* Same input should always produce same output */
    const char *hrp = "bc";
    uint8_t data[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    char output1[100];
    char output2[100];

    int result1 = bech32_encode(output1, hrp, data, 20);
    int result2 = bech32_encode(output2, hrp, data, 20);

    ASSERT_TRUE(result1 == 1);
    ASSERT_TRUE(result2 == 1);
    ASSERT_STR_EQ(output1, output2);
}

TEST(bech32_different_data) {
    /* Different data should produce different output */
    const char *hrp = "bc";
    uint8_t data1[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    uint8_t data2[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20};
    char output1[100];
    char output2[100];

    bech32_encode(output1, hrp, data1, 20);
    bech32_encode(output2, hrp, data2, 20);

    /* Outputs should be different */
    int same = (strcmp(output1, output2) == 0);
    ASSERT_FALSE(same);
}

TEST(segwit_case_insensitive_decode) {
    /* SegWit addresses should decode regardless of case */
    const char *addr_lower = "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4";
    const char *addr_upper = "BC1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3T4";

    int witver1, witver2;
    uint8_t witprog1[40], witprog2[40];
    size_t len1, len2;

    int result1 = segwit_addr_decode(&witver1, witprog1, &len1, "bc", addr_lower);
    int result2 = segwit_addr_decode(&witver2, witprog2, &len2, "bc", addr_upper);

    ASSERT_TRUE(result1 == 1);
    ASSERT_TRUE(result2 == 1);
    ASSERT_EQ(witver1, witver2);
    ASSERT_EQ(len1, len2);
    ASSERT_MEM_EQ(witprog1, witprog2, len1);
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int run_bech32_tests(void) {
    TEST_INIT();

    TEST_SECTION("BIP-173 Bech32 Valid Test Vectors");
    RUN_TEST(bech32_valid_simple);
    RUN_TEST(bech32_valid_uppercase);
    RUN_TEST(bech32_valid_longer);
    RUN_TEST(bech32_valid_max_length);

    TEST_SECTION("BIP-173 Bech32 Invalid Test Vectors");
    RUN_TEST(bech32_invalid_mixed_case);
    RUN_TEST(bech32_invalid_no_separator);
    RUN_TEST(bech32_invalid_empty_hrp);
    RUN_TEST(bech32_invalid_bad_checksum);
    RUN_TEST(bech32_invalid_too_short);

    TEST_SECTION("Bech32 Encoding Tests");
    RUN_TEST(bech32_encode_simple);
    RUN_TEST(bech32_encode_decode_roundtrip);

    TEST_SECTION("SegWit P2WPKH/P2WSH Tests (BIP-173)");
    RUN_TEST(segwit_p2wpkh_mainnet);
    RUN_TEST(segwit_p2wsh_mainnet);
    RUN_TEST(segwit_p2wpkh_testnet);
    RUN_TEST(segwit_encode_p2wpkh);
    RUN_TEST(segwit_encode_p2wsh);
    RUN_TEST(segwit_encode_decode_roundtrip);

    TEST_SECTION("BIP-350 Bech32m Tests (Taproot)");
    RUN_TEST(bech32m_taproot_mainnet);
    RUN_TEST(bech32m_encode_taproot);
    RUN_TEST(bech32m_testnet_taproot);

    TEST_SECTION("Invalid SegWit Address Tests");
    RUN_TEST(segwit_invalid_wrong_hrp);
    RUN_TEST(segwit_invalid_witness_version);
    RUN_TEST(segwit_invalid_v0_length);
    RUN_TEST(segwit_invalid_program_length);

    TEST_SECTION("Edge Cases and Boundary Tests");
    RUN_TEST(bech32_edge_all_zeros);
    RUN_TEST(bech32_edge_all_ones);
    RUN_TEST(bech32_deterministic);
    RUN_TEST(bech32_different_data);
    RUN_TEST(segwit_case_insensitive_decode);

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_bech32_tests();
}
#endif
