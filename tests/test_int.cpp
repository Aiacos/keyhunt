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
 * Division Tests
 * ============================================================================ */

TEST(int_div_basic) {
    Int a(7);
    Int b(3);
    Int mod;
    a.Div(&b, &mod);
    ASSERT_EQ(2, a.GetInt64());     /* quotient = 2 */
    ASSERT_EQ(1, mod.GetInt64());   /* remainder = 1 */
}

TEST(int_div_large) {
    Int a((uint64_t)0xFFFFFFFFULL);
    Int b((uint64_t)0x10000ULL);
    Int mod;
    a.Div(&b, &mod);
    ASSERT_EQ(0xFFFFULL, a.GetInt64());     /* quotient */
    ASSERT_EQ(0xFFFFULL, mod.GetInt64());   /* remainder */
}

TEST(int_div_by_one) {
    Int a(42);
    Int b(1);
    Int mod;
    a.Div(&b, &mod);
    ASSERT_EQ(42, a.GetInt64());
    ASSERT_TRUE(mod.IsZero());
}

TEST(int_div_exact) {
    Int a(100);
    Int b(25);
    Int mod;
    a.Div(&b, &mod);
    ASSERT_EQ(4, a.GetInt64());
    ASSERT_TRUE(mod.IsZero());
}

TEST(int_div_256bit) {
    /* Divide a large 256-bit value by a smaller value */
    Int a;
    a.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
    Int b((uint64_t)0x100000000ULL);  /* 2^32 */
    Int mod;
    a.Div(&b, &mod);
    /* Quotient should be non-zero */
    ASSERT_FALSE(a.IsZero());
    /* Remainder should be 0xFFFFFFFF (lower 32 bits of max value) */
    ASSERT_EQ(0xFFFFFFFFULL, mod.GetInt64());
}

TEST(int_div_smaller_dividend) {
    /* When a > this, result is 0 with mod = this */
    Int a(3);
    Int b(7);
    Int mod;
    a.Div(&b, &mod);
    ASSERT_TRUE(a.IsZero());       /* quotient = 0 */
    ASSERT_EQ(3, mod.GetInt64());  /* remainder = original value */
}

TEST(int_div_equal) {
    /* When a == this, result is 1 with mod = 0 */
    Int a(42);
    Int b(42);
    Int mod;
    a.Div(&b, &mod);
    ASSERT_EQ(1, a.GetInt64());
    ASSERT_TRUE(mod.IsZero());
}

TEST(int_div_no_mod) {
    /* Div with NULL mod pointer -- just quotient */
    Int a(100);
    Int b(7);
    a.Div(&b);
    ASSERT_EQ(14, a.GetInt64());
}

/* ============================================================================
 * GCD Tests
 * ============================================================================ */

TEST(int_gcd_coprime) {
    Int a(17);
    Int b(13);
    a.GCD(&b);
    ASSERT_EQ(1, a.GetInt64());
}

TEST(int_gcd_common) {
    Int a(12);
    Int b(8);
    a.GCD(&b);
    ASSERT_EQ(4, a.GetInt64());
}

TEST(int_gcd_same) {
    Int a(42);
    Int b(42);
    a.GCD(&b);
    ASSERT_EQ(42, a.GetInt64());
}

TEST(int_gcd_one) {
    Int a(1);
    Int b(12345);
    a.GCD(&b);
    ASSERT_EQ(1, a.GetInt64());
}

TEST(int_gcd_large) {
    /* GCD(2^64, 2^32) = 2^32 */
    Int a;
    a.SetBase16("10000000000000000");  /* 2^64 */
    Int b((uint64_t)0x100000000ULL);   /* 2^32 */
    a.GCD(&b);
    ASSERT_EQ(0x100000000ULL, a.GetInt64());
}

TEST(int_gcd_zero_this) {
    /* GCD(0, x) = x */
    Int a(0);
    Int b(42);
    a.GCD(&b);
    ASSERT_EQ(42, a.GetInt64());
}

TEST(int_gcd_zero_arg) {
    /* GCD(x, 0) = x */
    Int a(42);
    Int b(0);
    a.GCD(&b);
    ASSERT_EQ(42, a.GetInt64());
}

/* ============================================================================
 * Base Conversion Tests
 * ============================================================================ */

TEST(int_set_base10_simple) {
    Int a;
    a.SetBase10("12345");
    ASSERT_EQ(12345, a.GetInt64());
}

TEST(int_set_base10_large) {
    /* Set a large decimal, verify via GetBase16 */
    Int a;
    a.SetBase10("1000000000000000000");  /* 10^18 */
    /* 10^18 = 0xDE0B6B3A7640000 */
    char *hex = a.GetBase16();
    ASSERT_NOT_NULL(hex);
    /* Should contain the expected hex */
    ASSERT_TRUE(strstr(hex, "de0b6b3a7640000") != NULL);
    free(hex);
}

TEST(int_get_base10_simple) {
    Int a(42);
    char *dec = a.GetBase10();
    ASSERT_NOT_NULL(dec);
    ASSERT_STR_EQ("42", dec);
    free(dec);
}

TEST(int_get_base10_zero) {
    Int a(0);
    char *dec = a.GetBase10();
    ASSERT_NOT_NULL(dec);
    ASSERT_STR_EQ("0", dec);
    free(dec);
}

TEST(int_get_base16_roundtrip) {
    /* Set a hex value, get it back, verify roundtrip */
    Int a;
    a.SetBase16("DEADBEEF");
    char *hex = a.GetBase16();
    ASSERT_NOT_NULL(hex);
    /* GetBase16 uses lowercase */
    ASSERT_TRUE(strstr(hex, "deadbeef") != NULL);
    free(hex);
}

TEST(int_get_block_str) {
    Int a(42);
    char *block = a.GetBlockStr();
    ASSERT_NOT_NULL(block);
    /* Should contain "0000002A" somewhere (42 in hex) */
    ASSERT_TRUE(strstr(block, "0000002A") != NULL);
    free(block);
}

TEST(int_get_c64_str) {
    Int a(42);
    char *c64 = a.GetC64Str(4);
    ASSERT_NOT_NULL(c64);
    /* Should start with { and end with } */
    ASSERT_EQ('{', c64[0]);
    ASSERT_TRUE(strchr(c64, '}') != NULL);
    free(c64);
}

TEST(int_get_base2) {
    Int a(1);
    char *bin = a.GetBase2();
    ASSERT_NOT_NULL(bin);
    /* Binary of 1: should have '1' at some position */
    ASSERT_TRUE(strchr(bin, '1') != NULL);
    free(bin);
}

TEST(int_set_base_n) {
    /* SetBaseN with hex charset */
    Int a;
    a.SetBaseN(16, "0123456789ABCDEF", "FF");
    ASSERT_EQ(255, a.GetInt64());
}

TEST(int_get_base_n) {
    /* GetBaseN with decimal charset, roundtrip */
    Int a(123);
    char *dec = a.GetBaseN(10, "0123456789");
    ASSERT_NOT_NULL(dec);
    ASSERT_STR_EQ("123", dec);
    free(dec);
}

TEST(int_base10_roundtrip) {
    /* Large roundtrip: set base10, get base10, compare */
    Int a;
    a.SetBase10("999999999999999999");
    char *dec = a.GetBase10();
    ASSERT_NOT_NULL(dec);
    ASSERT_STR_EQ("999999999999999999", dec);
    free(dec);
}

/* ============================================================================
 * Shift Operation Tests
 * ============================================================================ */

TEST(int_shiftl32bit) {
    /* ShiftL32Bit() shifts entire value left by 32 bits */
    Int a(1);
    a.ShiftL32Bit();
    ASSERT_EQ(0, a.GetInt64() & 0xFFFFFFFF);  /* Lower 32 bits are 0 */
    ASSERT_EQ(0x100000000ULL, a.GetInt64());   /* Value moved up by 32 */
}

TEST(int_shiftl64bit) {
    /* ShiftL64Bit() shifts entire value left by 64 bits */
    Int a(1);
    a.ShiftL64Bit();
    ASSERT_EQ(0, a.GetInt64());     /* Lower 64 bits are 0 */
    ASSERT_FALSE(a.IsZero());       /* But the number is not zero */
}

TEST(int_shiftr32bit) {
    /* ShiftR32Bit() shifts entire value right by 32 bits */
    Int a((uint64_t)0x100000000ULL);  /* 2^32 */
    a.ShiftR32Bit();
    ASSERT_EQ(1, a.GetInt64());
}

TEST(int_shiftr64bit) {
    /* ShiftR64Bit() shifts entire value right by 64 bits */
    Int a;
    a.SetBase16("10000000000000000");  /* 2^64 */
    a.ShiftR64Bit();
    ASSERT_EQ(1, a.GetInt64());
}

TEST(int_shiftl_various) {
    Int a(1);
    a.ShiftL(1);
    ASSERT_EQ(2, a.GetInt64());

    Int b(1);
    b.ShiftL(16);
    ASSERT_EQ(65536, b.GetInt64());

    Int c(1);
    c.ShiftL(64);
    ASSERT_EQ(0, c.GetInt64());  /* Lower 64 bits are 0 */
    ASSERT_FALSE(c.IsZero());    /* But number is not zero */
}

TEST(int_shiftr_various) {
    Int a(4);
    a.ShiftR(1);
    ASSERT_EQ(2, a.GetInt64());

    Int b(65536);
    b.ShiftR(16);
    ASSERT_EQ(1, b.GetInt64());

    Int c;
    c.SetBase16("10000000000000000");  /* 2^64 */
    c.ShiftR(64);
    ASSERT_EQ(1, c.GetInt64());
}

/* ============================================================================
 * Constructor and Accessor Tests
 * ============================================================================ */

TEST(int_construct_from_ptr) {
    Int b(42);
    Int a(&b);
    ASSERT_EQ(42, a.GetInt64());
    ASSERT_TRUE(a.IsEqual(&b));
}

TEST(int_construct_from_null_ptr) {
    /* Int(NULL) should produce zero */
    Int a((Int*)NULL);
    ASSERT_TRUE(a.IsZero());
}

TEST(int_is_strict_positive) {
    Int a(1);
    ASSERT_TRUE(a.IsStrictPositive());

    Int b(0);
    ASSERT_FALSE(b.IsStrictPositive());  /* Zero is NOT strictly positive */

    Int c(-1);
    ASSERT_FALSE(c.IsStrictPositive());
}

TEST(int_is_greater_or_equal) {
    Int a(10);
    Int b(10);
    ASSERT_TRUE(a.IsGreaterOrEqual(&b));  /* equal */

    Int c(11);
    ASSERT_TRUE(c.IsGreaterOrEqual(&b));  /* greater */

    Int d(9);
    ASSERT_FALSE(d.IsGreaterOrEqual(&b)); /* less */
}

TEST(int_is_lower_or_equal) {
    Int a(10);
    Int b(10);
    ASSERT_TRUE(a.IsLowerOrEqual(&b));   /* equal */

    Int c(9);
    ASSERT_TRUE(c.IsLowerOrEqual(&b));   /* less */

    Int d(11);
    ASSERT_FALSE(d.IsLowerOrEqual(&b));  /* greater */
}

TEST(int_set_32bytes) {
    /* Set from 32 bytes (big-endian) and verify */
    unsigned char bytes[32];
    memset(bytes, 0, 32);
    bytes[31] = 0x42;  /* Value 0x42 at the least significant byte */
    Int a;
    a.Set32Bytes(bytes);
    ASSERT_EQ(0x42, a.GetInt64());
}

TEST(int_set_32bytes_large) {
    /* Set from 32 bytes with a known pattern and verify via Get32Bytes roundtrip */
    unsigned char bytes_in[32];
    unsigned char bytes_out[32];
    for (int i = 0; i < 32; i++) bytes_in[i] = (unsigned char)i;
    Int a;
    a.Set32Bytes(bytes_in);
    a.Get32Bytes(bytes_out);
    ASSERT_MEM_EQ(bytes_in, bytes_out, 32);
}

TEST(int_get_hi16_bytes) {
    /* Set a value where high bytes are distinct from low bytes */
    Int a;
    a.SetBase16("0102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20");
    unsigned char hi[16];
    a.GetHi16Bytes(hi);
    /* Hi 16 bytes should be the first 16 bytes of the 32-byte big-endian repr */
    unsigned char full[32];
    a.Get32Bytes(full);
    ASSERT_MEM_EQ(full, hi, 16);
}

TEST(int_get_lo16_bytes) {
    Int a;
    a.SetBase16("0102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20");
    unsigned char lo[16];
    a.GetLo16Bytes(lo);
    /* Lo 16 bytes should be bytes 16-31 of the 32-byte big-endian repr */
    unsigned char full[32];
    a.Get32Bytes(full);
    ASSERT_MEM_EQ(full + 16, lo, 16);
}

TEST(int_set_byte) {
    Int a;
    a.SetByte(0, 0xAB);
    ASSERT_EQ(0xAB, a.GetByte(0));

    a.SetByte(1, 0xCD);
    ASSERT_EQ(0xCD, a.GetByte(1));

    /* Original byte still intact */
    ASSERT_EQ(0xAB, a.GetByte(0));
}

TEST(int_set_dword) {
    Int a;
    a.SetDWord(0, 0xDEADBEEF);
    ASSERT_EQ(0xDEADBEEF, (uint32_t)(a.GetInt64() & 0xFFFFFFFF));

    a.SetDWord(1, 0xCAFEBABE);
    /* DWord 1 is at bits 32-63 */
    ASSERT_EQ(0xCAFEBABE, (uint32_t)((a.GetInt64() >> 32) & 0xFFFFFFFF));
}

TEST(int_set_qword) {
    Int a;
    a.SetQWord(0, 0x123456789ABCDEF0ULL);
    ASSERT_EQ(0x123456789ABCDEF0ULL, a.GetInt64());
}

/* ============================================================================
 * Miscellaneous Function Tests
 * ============================================================================ */

TEST(int_mask_byte) {
    /* MaskByte(n) zeros out 32-bit words from position n and up */
    Int a;
    a.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
    a.MaskByte(4);  /* Zero out from dword[4] and up */
    /* Lower 128 bits should still be all 1s, upper should be 0 */
    ASSERT_EQ(0xFFFFFFFFFFFFFFFFULL, a.GetInt64());
    /* But the upper portion should be zeroed */
    unsigned char bytes[32];
    a.Get32Bytes(bytes);
    /* First 16 bytes (big-endian upper) should be zero */
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(0, bytes[i]);
    }
}

TEST(int_imult_signed) {
    Int a(10);
    a.IMult((int64_t)-5);
    ASSERT_TRUE(a.IsNegative());
    a.Neg();  /* Make positive to check value */
    ASSERT_EQ(50, a.GetInt64());
}

TEST(int_imult_int_signed) {
    Int a(10);
    Int result;
    result.IMult(&a, (int64_t)-3);
    ASSERT_TRUE(result.IsNegative());
    result.Neg();
    ASSERT_EQ(30, result.GetInt64());
}

TEST(int_imult_positive) {
    /* IMult with positive value should work like normal multiply */
    Int a(7);
    a.IMult((int64_t)6);
    ASSERT_EQ(42, a.GetInt64());
}

TEST(int_mult_mod_n) {
    /* MultModN(a, b, n) = (a*b) mod n */
    Int result;
    Int a(7);
    Int b(6);
    Int n(10);
    result.MultModN(&a, &b, &n);
    /* 7*6 = 42, 42 mod 10 = 2 */
    ASSERT_EQ(2, result.GetInt64());
}

TEST(int_mult_mod_n_large) {
    /* Larger MultModN */
    Int result;
    Int a(1000);
    Int b(1000);
    Int n(997);  /* prime */
    result.MultModN(&a, &b, &n);
    /* 1000*1000 = 1000000, 1000000 mod 997 = 1000000 - 1003*997 = 1000000 - 999991 = 9 */
    ASSERT_EQ(9, result.GetInt64());
}

TEST(int_get_size) {
    /* GetSize() returns number of significant 32-bit words */
    Int a(0);
    ASSERT_EQ(1, a.GetSize());  /* At least 1 */

    Int b(1);
    ASSERT_EQ(1, b.GetSize());

    Int c((uint64_t)0x100000000ULL);  /* 2^32, needs 2 dwords */
    ASSERT_EQ(2, c.GetSize());
}

TEST(int_get_bit_length_various) {
    /* More bit length tests */
    Int a(0);
    ASSERT_EQ(0, a.GetBitLength());

    Int b(1);
    ASSERT_EQ(1, b.GetBitLength());

    Int c(2);
    ASSERT_EQ(2, c.GetBitLength());

    Int d(128);  /* 2^7 */
    ASSERT_EQ(8, d.GetBitLength());

    Int e((uint64_t)0x100000000ULL);  /* 2^32 */
    ASSERT_EQ(33, e.GetBitLength());
}

TEST(int_mod) {
    /* Mod(n): this = this mod n */
    Int a(42);
    Int n(10);
    a.Mod(&n);
    ASSERT_EQ(2, a.GetInt64());
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
    RUN_TEST(int_construct_from_ptr);
    RUN_TEST(int_construct_from_null_ptr);

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
    RUN_TEST(int_imult_signed);
    RUN_TEST(int_imult_int_signed);
    RUN_TEST(int_imult_positive);
    RUN_TEST(int_mult_mod_n);
    RUN_TEST(int_mult_mod_n_large);

    TEST_SECTION("Division");
    RUN_TEST(int_div_basic);
    RUN_TEST(int_div_large);
    RUN_TEST(int_div_by_one);
    RUN_TEST(int_div_exact);
    RUN_TEST(int_div_256bit);
    RUN_TEST(int_div_smaller_dividend);
    RUN_TEST(int_div_equal);
    RUN_TEST(int_div_no_mod);

    TEST_SECTION("GCD");
    RUN_TEST(int_gcd_coprime);
    RUN_TEST(int_gcd_common);
    RUN_TEST(int_gcd_same);
    RUN_TEST(int_gcd_one);
    RUN_TEST(int_gcd_large);
    RUN_TEST(int_gcd_zero_this);
    RUN_TEST(int_gcd_zero_arg);

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
    RUN_TEST(int_is_strict_positive);
    RUN_TEST(int_is_greater_or_equal);
    RUN_TEST(int_is_lower_or_equal);

    TEST_SECTION("Bit Operations");
    RUN_TEST(int_shift_left);
    RUN_TEST(int_shift_right);
    RUN_TEST(int_get_bit);
    RUN_TEST(int_get_bit_length);
    RUN_TEST(int_get_bit_length_various);
    RUN_TEST(int_shiftl32bit);
    RUN_TEST(int_shiftl64bit);
    RUN_TEST(int_shiftr32bit);
    RUN_TEST(int_shiftr64bit);
    RUN_TEST(int_shiftl_various);
    RUN_TEST(int_shiftr_various);

    TEST_SECTION("Hex Conversion");
    RUN_TEST(int_set_base16);
    RUN_TEST(int_set_base16_large);
    RUN_TEST(int_get_base16);
    RUN_TEST(int_get_base16_roundtrip);

    TEST_SECTION("Base Conversion");
    RUN_TEST(int_set_base10_simple);
    RUN_TEST(int_set_base10_large);
    RUN_TEST(int_get_base10_simple);
    RUN_TEST(int_get_base10_zero);
    RUN_TEST(int_get_block_str);
    RUN_TEST(int_get_c64_str);
    RUN_TEST(int_get_base2);
    RUN_TEST(int_set_base_n);
    RUN_TEST(int_get_base_n);
    RUN_TEST(int_base10_roundtrip);

    TEST_SECTION("Negation and Absolute");
    RUN_TEST(int_neg);
    RUN_TEST(int_abs);

    TEST_SECTION("Set and Get");
    RUN_TEST(int_set);
    RUN_TEST(int_set_int32);
    RUN_TEST(int_set_int64);
    RUN_TEST(int_set_32bytes);
    RUN_TEST(int_set_32bytes_large);
    RUN_TEST(int_get_hi16_bytes);
    RUN_TEST(int_get_lo16_bytes);
    RUN_TEST(int_set_byte);
    RUN_TEST(int_set_dword);
    RUN_TEST(int_set_qword);

    TEST_SECTION("Miscellaneous");
    RUN_TEST(int_mask_byte);
    RUN_TEST(int_get_size);
    RUN_TEST(int_mod);

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
