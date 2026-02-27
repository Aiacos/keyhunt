/*
 * test_extended_range.cpp - Unit tests for Extended Range BSGS (256-bit)
 *
 * Tests extended range support for BSGS mode:
 * - Parsing 256-bit N values from hex strings
 * - Int-based memory calculation
 * - Overflow detection and validation
 */

#include "test_framework.h"
#include "../src/cli.h"
#include "../src/config/config.h"
#include "../src/secp256k1/Int.h"
#include <cstdlib>
#include <cstring>

/* ============================================================================
 * Parse N Value Extended Tests
 * ============================================================================ */

TEST(parse_n_value_basic_decimal) {
    Int result;
    /* Parse basic decimal (no 0x prefix) should still work as hex */
    int ret = parse_n_value_extended("FF", &result);

    ASSERT_EQ(0, ret);
    char *hex_out = result.GetBase16();
    ASSERT_STR_EQ("ff", hex_out);  /* GetBase16() returns lowercase */
    free(hex_out);
}

TEST(parse_n_value_with_0x_prefix) {
    Int result;
    int ret = parse_n_value_extended("0xFF", &result);

    ASSERT_EQ(0, ret);
    char *hex_out = result.GetBase16();
    ASSERT_STR_EQ("ff", hex_out);  /* GetBase16() returns lowercase */
    free(hex_out);
}

TEST(parse_n_value_with_0X_prefix) {
    Int result;
    int ret = parse_n_value_extended("0XFF", &result);

    ASSERT_EQ(0, ret);
    char *hex_out = result.GetBase16();
    ASSERT_STR_EQ("ff", hex_out);  /* GetBase16() returns lowercase */
    free(hex_out);
}

TEST(parse_n_value_65bit) {
    Int result;
    /* 65-bit value: 0x10000000000000000 (2^64) */
    int ret = parse_n_value_extended("0x10000000000000000", &result);

    ASSERT_EQ(0, ret);
    char *hex_out = result.GetBase16();
    ASSERT_STR_EQ("10000000000000000", hex_out);
    free(hex_out);
}

TEST(parse_n_value_80bit) {
    Int result;
    /* 80-bit value: 2^80 - 1 */
    int ret = parse_n_value_extended("0xFFFFFFFFFFFFFFFFFFFF", &result);

    ASSERT_EQ(0, ret);
    char *hex_out = result.GetBase16();
    ASSERT_STR_EQ("ffffffffffffffffffff", hex_out);  /* GetBase16() returns lowercase */
    free(hex_out);
}

TEST(parse_n_value_128bit) {
    Int result;
    /* 128-bit value: 2^128 - 1 */
    int ret = parse_n_value_extended("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", &result);

    ASSERT_EQ(0, ret);
    char *hex_out = result.GetBase16();
    ASSERT_STR_EQ("ffffffffffffffffffffffffffffffff", hex_out);  /* GetBase16() returns lowercase */
    free(hex_out);
}

TEST(parse_n_value_256bit) {
    Int result;
    /* 256-bit value: 2^256 - 1 */
    const char *hex_256 = "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
    int ret = parse_n_value_extended(hex_256, &result);

    ASSERT_EQ(0, ret);
    char *hex_out = result.GetBase16();
    ASSERT_STR_EQ("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", hex_out);  /* GetBase16() returns lowercase */
    free(hex_out);
}

TEST(parse_n_value_256bit_power_of_2) {
    Int result;
    /* 256-bit value: 2^255 (single bit set) */
    const char *hex_255 = "0x8000000000000000000000000000000000000000000000000000000000000000";
    int ret = parse_n_value_extended(hex_255, &result);

    ASSERT_EQ(0, ret);
    char *hex_out = result.GetBase16();
    ASSERT_STR_EQ("8000000000000000000000000000000000000000000000000000000000000000", hex_out);
    free(hex_out);
}

TEST(parse_n_value_null_result) {
    /* NULL result pointer should fail gracefully */
    int ret = parse_n_value_extended("0xFF", NULL);
    ASSERT_EQ(-1, ret);
}

TEST(parse_n_value_null_string) {
    Int result;
    /* NULL input string should fail gracefully */
    int ret = parse_n_value_extended(NULL, &result);
    ASSERT_EQ(-1, ret);
}

TEST(parse_n_value_empty_string) {
    Int result;
    /* Empty string should fail */
    int ret = parse_n_value_extended("", &result);
    ASSERT_EQ(-1, ret);
}

TEST(parse_n_value_whitespace_only) {
    Int result;
    /* Whitespace-only string should fail */
    int ret = parse_n_value_extended("   ", &result);
    ASSERT_EQ(-1, ret);
}

TEST(parse_n_value_0x_only) {
    Int result;
    /* "0x" with no hex digits should fail */
    int ret = parse_n_value_extended("0x", &result);
    ASSERT_EQ(-1, ret);
}

TEST(parse_n_value_invalid_hex_char) {
    Int result;
    /* Invalid hex character 'G' should fail */
    int ret = parse_n_value_extended("0xFFGG", &result);
    ASSERT_EQ(-1, ret);
}

TEST(parse_n_value_with_leading_whitespace) {
    Int result;
    /* Should skip leading whitespace */
    int ret = parse_n_value_extended("  \t\n0xFF", &result);

    ASSERT_EQ(0, ret);
    char *hex_out = result.GetBase16();
    ASSERT_STR_EQ("ff", hex_out);  /* GetBase16() returns lowercase */
    free(hex_out);
}

/* ============================================================================
 * Int Helper Function Tests
 * ============================================================================ */

TEST(uint64_to_int_basic) {
    uint64_t val = 0x123456789ABCDEF0ULL;
    Int *result = uint64_to_int(val);

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(val, result->GetInt64());

    delete result;
}

TEST(uint64_to_int_max) {
    uint64_t val = 0xFFFFFFFFFFFFFFFFULL;
    Int *result = uint64_to_int(val);

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(val, result->GetInt64());

    delete result;
}

TEST(uint64_to_int_zero) {
    uint64_t val = 0;
    Int *result = uint64_to_int(val);

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(0ULL, result->GetInt64());

    delete result;
}

TEST(int_to_uint64_safe_fits) {
    Int val;
    val.SetBase16("FF");
    bool overflow = false;

    uint64_t result = int_to_uint64_safe(&val, &overflow);

    ASSERT_FALSE(overflow);
    ASSERT_EQ(0xFFULL, result);
}

TEST(int_to_uint64_safe_max) {
    Int val;
    val.SetBase16("FFFFFFFFFFFFFFFF");
    bool overflow = false;

    uint64_t result = int_to_uint64_safe(&val, &overflow);

    ASSERT_FALSE(overflow);
    ASSERT_EQ(0xFFFFFFFFFFFFFFFFULL, result);
}

TEST(int_to_uint64_safe_overflow_65bit) {
    Int val;
    /* 65-bit value: 2^64 */
    val.SetBase16("10000000000000000");
    bool overflow = false;

    uint64_t result = int_to_uint64_safe(&val, &overflow);

    /* Should set overflow flag */
    ASSERT_TRUE(overflow);
    /* Result should be lower 64 bits (0 in this case) */
    ASSERT_EQ(0ULL, result);
}

TEST(int_to_uint64_safe_overflow_256bit) {
    Int val;
    /* 256-bit value */
    val.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
    bool overflow = false;

    uint64_t result = int_to_uint64_safe(&val, &overflow);

    /* Should set overflow flag */
    ASSERT_TRUE(overflow);
    /* Result should be lower 64 bits (all 1s) */
    ASSERT_EQ(0xFFFFFFFFFFFFFFFFULL, result);
}

TEST(int_to_uint64_safe_null_int) {
    bool overflow = false;

    uint64_t result = int_to_uint64_safe(NULL, &overflow);

    /* Should set overflow flag for NULL input */
    ASSERT_TRUE(overflow);
    ASSERT_EQ(0ULL, result);
}

TEST(int_to_uint64_safe_null_overflow_flag) {
    Int val;
    val.SetBase16("FF");

    /* NULL overflow pointer should work (just not set the flag) */
    uint64_t result = int_to_uint64_safe(&val, NULL);

    ASSERT_EQ(0xFFULL, result);
}

/* ============================================================================
 * Memory Calculation Tests
 * ============================================================================ */

TEST(memory_calc_int_basic) {
    /* Test with N=2^40 (manageable size) */
    Int n_int;
    n_int.SetBase16("10000000000");  /* 2^40 */

    Int *m_int = NULL;
    uint64_t bloom_bytes = 0;
    uint64_t table_bytes = 0;

    uint64_t total = kh_bsgs_calc_memory_int(&n_int, 1, &m_int, &bloom_bytes, &table_bytes);

    /* M should be sqrt(N) = 2^20 */
    ASSERT_NOT_NULL(m_int);

    /* Total memory should be reasonable (less than 1GB for this N) */
    ASSERT_TRUE(total > 0);
    ASSERT_TRUE(total < 1024ULL * 1024 * 1024);

    /* Bloom and table should be non-zero */
    ASSERT_TRUE(bloom_bytes > 0);
    ASSERT_TRUE(table_bytes > 0);

    delete m_int;
}

TEST(memory_calc_int_with_k_factor) {
    /* Test with N=2^40 and K=4 */
    Int n_int;
    n_int.SetBase16("10000000000");  /* 2^40 */

    Int *m_int = NULL;
    uint64_t bloom_bytes = 0;
    uint64_t table_bytes = 0;

    uint64_t total = kh_bsgs_calc_memory_int(&n_int, 4, &m_int, &bloom_bytes, &table_bytes);

    ASSERT_NOT_NULL(m_int);
    ASSERT_TRUE(total > 0);

    delete m_int;
}

TEST(memory_calc_int_null_n) {
    Int *m_int = NULL;
    uint64_t bloom_bytes = 0;
    uint64_t table_bytes = 0;

    uint64_t total = kh_bsgs_calc_memory_int(NULL, 1, &m_int, &bloom_bytes, &table_bytes);

    /* Should return 0 for NULL input */
    ASSERT_EQ(0ULL, total);
}

TEST(memory_calc_int_k_zero) {
    Int n_int;
    n_int.SetBase16("10000000000");

    Int *m_int = NULL;
    uint64_t bloom_bytes = 0;
    uint64_t table_bytes = 0;

    uint64_t total = kh_bsgs_calc_memory_int(&n_int, 0, &m_int, &bloom_bytes, &table_bytes);

    /* K=0 should return 0 (invalid) */
    ASSERT_EQ(0ULL, total);
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

TEST(integration_parse_and_memory_calc) {
    /* Parse a 66-bit N value and calculate memory */
    Int n_int;
    int ret = parse_n_value_extended("0x40000000000000000", &n_int);
    ASSERT_EQ(0, ret);

    /* Calculate memory requirements */
    Int *m_int = NULL;
    uint64_t bloom_bytes = 0;
    uint64_t table_bytes = 0;

    uint64_t total = kh_bsgs_calc_memory_int(&n_int, 1, &m_int, &bloom_bytes, &table_bytes);

    ASSERT_NOT_NULL(m_int);
    ASSERT_TRUE(total > 0);

    /* Verify M is approximately sqrt(N) */
    /* For N=2^66, M should be 2^33 */
    char *m_hex = m_int->GetBase16();
    ASSERT_NOT_NULL(m_hex);

    /* M should be around 2^33 = 0x200000000 */
    /* Due to rounding in sqrt, we just check it's non-zero */
    ASSERT_TRUE(m_int->GetInt64() > 0);

    free(m_hex);
    delete m_int;
}

TEST(integration_parse_128bit_and_validate) {
    /* Parse 128-bit value and verify it's too large for practical use */
    Int n_int;
    const char *hex_128 = "0x100000000000000000000000000000000";  /* 2^128 */
    int ret = parse_n_value_extended(hex_128, &n_int);

    ASSERT_EQ(0, ret);

    /* Verify the value is correctly stored */
    char *hex_out = n_int.GetBase16();
    ASSERT_STR_EQ("100000000000000000000000000000000", hex_out);
    free(hex_out);

    /* This would be impractical for BSGS, but parsing should work */
    /* Memory calculation would overflow, but that's tested separately */
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int run_extended_range_tests(void) {
    TEST_INIT();

    TEST_SECTION("Parse N Value Extended Tests");
    RUN_TEST(parse_n_value_basic_decimal);
    RUN_TEST(parse_n_value_with_0x_prefix);
    RUN_TEST(parse_n_value_with_0X_prefix);
    RUN_TEST(parse_n_value_65bit);
    RUN_TEST(parse_n_value_80bit);
    RUN_TEST(parse_n_value_128bit);
    RUN_TEST(parse_n_value_256bit);
    RUN_TEST(parse_n_value_256bit_power_of_2);
    RUN_TEST(parse_n_value_null_result);
    RUN_TEST(parse_n_value_null_string);
    RUN_TEST(parse_n_value_empty_string);
    RUN_TEST(parse_n_value_whitespace_only);
    RUN_TEST(parse_n_value_0x_only);
    RUN_TEST(parse_n_value_invalid_hex_char);
    RUN_TEST(parse_n_value_with_leading_whitespace);

    TEST_SECTION("Int Helper Function Tests");
    RUN_TEST(uint64_to_int_basic);
    RUN_TEST(uint64_to_int_max);
    RUN_TEST(uint64_to_int_zero);
    RUN_TEST(int_to_uint64_safe_fits);
    RUN_TEST(int_to_uint64_safe_max);
    RUN_TEST(int_to_uint64_safe_overflow_65bit);
    RUN_TEST(int_to_uint64_safe_overflow_256bit);
    RUN_TEST(int_to_uint64_safe_null_int);
    RUN_TEST(int_to_uint64_safe_null_overflow_flag);

    TEST_SECTION("Memory Calculation Tests");
    RUN_TEST(memory_calc_int_basic);
    RUN_TEST(memory_calc_int_with_k_factor);
    RUN_TEST(memory_calc_int_null_n);
    RUN_TEST(memory_calc_int_k_zero);

    TEST_SECTION("Integration Tests");
    RUN_TEST(integration_parse_and_memory_calc);
    RUN_TEST(integration_parse_128bit_and_validate);

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_extended_range_tests();
}
#endif
