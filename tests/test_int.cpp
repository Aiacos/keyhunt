/*
 * test_int.cpp - Unit tests for the Int (256-bit integer) class
 *
 * Tests basic arithmetic operations:
 * - Add, Sub, Mult
 * - Comparison (IsZero, IsNegative, IsPositive)
 * - Bit operations (ShiftL, ShiftR)
 * - Modular arithmetic
 */

#include "test_framework.h"
#include "secp256k1/Int.h"
#include <cstdlib>  /* free */
#include <cstring>  /* strstr */

/* ============================================================================
 * Basic Construction Tests
 * ============================================================================ */

TEST(int_construct_zero) {
    Int a;
    ASSERT_TRUE(a.IsZero());
}

TEST(int_construct_from_int32) {
    Int a(42);
    ASSERT_EQ(42, a.GetInt64());
    ASSERT_FALSE(a.IsZero());
}

TEST(int_construct_from_int64) {
    Int a((int64_t)0x123456789ABCDEFLL);
    ASSERT_EQ(0x123456789ABCDEFLL, a.GetInt64());
}

TEST(int_construct_from_uint64) {
    Int a((uint64_t)0xFFFFFFFFFFFFFFFFULL);
    ASSERT_EQ(0xFFFFFFFFFFFFFFFFULL, a.GetInt64());
}

TEST(int_construct_negative) {
    Int a(-1);
    ASSERT_TRUE(a.IsNegative());
}

/* ============================================================================
 * Addition Tests
 * ============================================================================ */

TEST(int_add_simple) {
    Int a(100);
    Int b(200);
    a.Add(&b);
    ASSERT_EQ(300, a.GetInt64());
}

TEST(int_add_uint64) {
    Int a(100);
    a.Add((uint64_t)50);
    ASSERT_EQ(150, a.GetInt64());
}

TEST(int_add_zero) {
    Int a(42);
    Int b;
    a.Add(&b);
    ASSERT_EQ(42, a.GetInt64());
}

TEST(int_add_carry) {
    Int a((uint64_t)0xFFFFFFFFFFFFFFFFULL);
    a.Add((uint64_t)1);
    /* Should carry over to next 64-bit block */
    ASSERT_FALSE(a.IsZero());
    ASSERT_EQ(0, a.GetInt64());  /* Lower 64 bits wrap to 0 */
}

TEST(int_add_one) {
    Int a(99);
    a.AddOne();
    ASSERT_EQ(100, a.GetInt64());
}

/* ============================================================================
 * Subtraction Tests
 * ============================================================================ */

TEST(int_sub_simple) {
    Int a(100);
    Int b(30);
    a.Sub(&b);
    ASSERT_EQ(70, a.GetInt64());
}

TEST(int_sub_uint64) {
    Int a(100);
    a.Sub((uint64_t)25);
    ASSERT_EQ(75, a.GetInt64());
}

TEST(int_sub_to_zero) {
    Int a(42);
    Int b(42);
    a.Sub(&b);
    ASSERT_TRUE(a.IsZero());
}

TEST(int_sub_underflow) {
    Int a(0);
    a.Sub((uint64_t)1);
    /* Should become negative (two's complement) */
    ASSERT_TRUE(a.IsNegative());
}

TEST(int_sub_one) {
    Int a(100);
    a.SubOne();
    ASSERT_EQ(99, a.GetInt64());
}

/* ============================================================================
 * Multiplication Tests
 * ============================================================================ */

TEST(int_mult_simple) {
    Int a(7);
    Int b(6);
    a.Mult(&b);
    ASSERT_EQ(42, a.GetInt64());
}

TEST(int_mult_uint64) {
    Int a(100);
    a.Mult((uint64_t)3);
    ASSERT_EQ(300, a.GetInt64());
}

TEST(int_mult_by_zero) {
    Int a(12345);
    Int b(0);
    a.Mult(&b);
    ASSERT_TRUE(a.IsZero());
}

TEST(int_mult_by_one) {
    Int a(42);
    a.Mult((uint64_t)1);
    ASSERT_EQ(42, a.GetInt64());
}

TEST(int_mult_large) {
    Int a((uint64_t)0x100000000ULL);  /* 2^32 */
    a.Mult((uint64_t)0x100000000ULL);  /* 2^32 */
    /* Result should be 2^64, which doesn't fit in 64 bits */
    ASSERT_EQ(0, a.GetInt64());  /* Lower 64 bits are 0 */
    ASSERT_FALSE(a.IsZero());   /* But the number is not zero */
}

/* ============================================================================
 * Comparison Tests
 * ============================================================================ */

TEST(int_is_zero) {
    Int a;
    ASSERT_TRUE(a.IsZero());

    a.SetInt64(1);
    ASSERT_FALSE(a.IsZero());

    a.SetInt64(0);
    ASSERT_TRUE(a.IsZero());
}

TEST(int_is_one) {
    Int a(1);
    ASSERT_TRUE(a.IsOne());

    Int b(2);
    ASSERT_FALSE(b.IsOne());

    Int c(0);
    ASSERT_FALSE(c.IsOne());
}

TEST(int_is_negative) {
    Int a(-1);
    ASSERT_TRUE(a.IsNegative());

    Int b(1);
    ASSERT_FALSE(b.IsNegative());

    Int c(0);
    ASSERT_FALSE(c.IsNegative());
}

TEST(int_is_positive) {
    Int a(1);
    ASSERT_TRUE(a.IsPositive());

    Int b(0);
    ASSERT_TRUE(b.IsPositive());  /* Zero is considered positive */

    Int c(-1);
    ASSERT_FALSE(c.IsPositive());
}

TEST(int_is_even) {
    Int a(2);
    ASSERT_TRUE(a.IsEven());

    Int b(3);
    ASSERT_FALSE(b.IsEven());

    Int c(0);
    ASSERT_TRUE(c.IsEven());
}

TEST(int_is_odd) {
    Int a(3);
    ASSERT_TRUE(a.IsOdd());

    Int b(2);
    ASSERT_FALSE(b.IsOdd());

    Int c(1);
    ASSERT_TRUE(c.IsOdd());
}

TEST(int_is_greater) {
    Int a(100);
    Int b(50);
    ASSERT_TRUE(a.IsGreater(&b));
    ASSERT_FALSE(b.IsGreater(&a));
}

TEST(int_is_equal) {
    Int a(42);
    Int b(42);
    ASSERT_TRUE(a.IsEqual(&b));

    Int c(43);
    ASSERT_FALSE(a.IsEqual(&c));
}

TEST(int_is_lower) {
    Int a(50);
    Int b(100);
    ASSERT_TRUE(a.IsLower(&b));
    ASSERT_FALSE(b.IsLower(&a));
}

/* ============================================================================
 * Bit Operation Tests
 * ============================================================================ */

TEST(int_shift_left) {
    Int a(1);
    a.ShiftL(4);
    ASSERT_EQ(16, a.GetInt64());  /* 1 << 4 = 16 */
}

TEST(int_shift_right) {
    Int a(16);
    a.ShiftR(2);
    ASSERT_EQ(4, a.GetInt64());  /* 16 >> 2 = 4 */
}

TEST(int_get_bit) {
    Int a((uint64_t)0b10101010);  /* Binary 10101010 */
    ASSERT_EQ(0, a.GetBit(0));
    ASSERT_EQ(1, a.GetBit(1));
    ASSERT_EQ(0, a.GetBit(2));
    ASSERT_EQ(1, a.GetBit(3));
}

TEST(int_get_bit_length) {
    Int a(0);
    ASSERT_EQ(0, a.GetBitLength());

    Int b(1);
    ASSERT_EQ(1, b.GetBitLength());

    Int c(255);
    ASSERT_EQ(8, c.GetBitLength());

    Int d(256);
    ASSERT_EQ(9, d.GetBitLength());
}

/* ============================================================================
 * Hex Conversion Tests
 * ============================================================================ */

TEST(int_set_base16) {
    Int a;
    a.SetBase16("FF");
    ASSERT_EQ(255, a.GetInt64());
}

TEST(int_set_base16_large) {
    Int a;
    a.SetBase16("FFFFFFFFFFFFFFFF");  /* 2^64 - 1 */
    ASSERT_EQ(0xFFFFFFFFFFFFFFFFULL, a.GetInt64());
}

TEST(int_get_base16) {
    Int a(255);
    char *hex = a.GetBase16();
    /* GetBase16 returns uppercase hex without leading zeros */
    ASSERT_NOT_NULL(hex);
    /* The string contains FF at some point */
    ASSERT_TRUE(strstr(hex, "FF") != NULL || strstr(hex, "ff") != NULL);
    free(hex);
}

/* ============================================================================
 * Negation and Absolute Value Tests
 * ============================================================================ */

TEST(int_neg) {
    Int a(42);
    a.Neg();
    ASSERT_TRUE(a.IsNegative());

    Int b(-42);
    b.Neg();
    ASSERT_TRUE(b.IsPositive());
}

TEST(int_abs) {
    Int a(-42);
    a.Abs();
    ASSERT_TRUE(a.IsPositive());
    ASSERT_EQ(42, a.GetInt64());
}

/* ============================================================================
 * Set and Get Tests
 * ============================================================================ */

TEST(int_set) {
    Int a(100);
    Int b(200);
    a.Set(&b);
    ASSERT_EQ(200, a.GetInt64());
    ASSERT_TRUE(a.IsEqual(&b));
}

TEST(int_set_int32) {
    Int a;
    a.SetInt32(12345);
    ASSERT_EQ(12345, a.GetInt32());
}

TEST(int_set_int64) {
    Int a;
    a.SetInt64(0x123456789ABCDEFULL);
    ASSERT_EQ(0x123456789ABCDEFULL, a.GetInt64());
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

/* Exported function for test runner */
int run_int_tests(void) {
    TEST_INIT();

    TEST_SECTION("Construction");
    RUN_TEST(int_construct_zero);
    RUN_TEST(int_construct_from_int32);
    RUN_TEST(int_construct_from_int64);
    RUN_TEST(int_construct_from_uint64);
    RUN_TEST(int_construct_negative);

    TEST_SECTION("Addition");
    RUN_TEST(int_add_simple);
    RUN_TEST(int_add_uint64);
    RUN_TEST(int_add_zero);
    RUN_TEST(int_add_carry);
    RUN_TEST(int_add_one);

    TEST_SECTION("Subtraction");
    RUN_TEST(int_sub_simple);
    RUN_TEST(int_sub_uint64);
    RUN_TEST(int_sub_to_zero);
    RUN_TEST(int_sub_underflow);
    RUN_TEST(int_sub_one);

    TEST_SECTION("Multiplication");
    RUN_TEST(int_mult_simple);
    RUN_TEST(int_mult_uint64);
    RUN_TEST(int_mult_by_zero);
    RUN_TEST(int_mult_by_one);
    RUN_TEST(int_mult_large);

    TEST_SECTION("Comparison");
    RUN_TEST(int_is_zero);
    RUN_TEST(int_is_one);
    RUN_TEST(int_is_negative);
    RUN_TEST(int_is_positive);
    RUN_TEST(int_is_even);
    RUN_TEST(int_is_odd);
    RUN_TEST(int_is_greater);
    RUN_TEST(int_is_equal);
    RUN_TEST(int_is_lower);

    TEST_SECTION("Bit Operations");
    RUN_TEST(int_shift_left);
    RUN_TEST(int_shift_right);
    RUN_TEST(int_get_bit);
    RUN_TEST(int_get_bit_length);

    TEST_SECTION("Hex Conversion");
    RUN_TEST(int_set_base16);
    RUN_TEST(int_set_base16_large);
    RUN_TEST(int_get_base16);

    TEST_SECTION("Negation and Absolute");
    RUN_TEST(int_neg);
    RUN_TEST(int_abs);

    TEST_SECTION("Set and Get");
    RUN_TEST(int_set);
    RUN_TEST(int_set_int32);
    RUN_TEST(int_set_int64);

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_int_tests();
}
#endif
