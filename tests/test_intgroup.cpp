/*
 * test_intgroup.cpp - Unit tests for the IntGroup (batch modular inversion) class
 *
 * Tests batch modular inversion using Montgomery's trick:
 * - Construction and basic operations
 * - Correctness of ModInv() and ModInvOptimized()
 * - Verification that a * a^-1 ≡ 1 (mod p)
 * - Batch operations with multiple sizes
 */

#include "test_framework.h"
#include "secp256k1/IntGroup.h"
#include "secp256k1/SECP256k1.h"
#include "secp256k1/Int.h"
#include <cstdlib>  /* free */

static Secp256K1 *secp = nullptr;

/* ============================================================================
 * Test Setup
 * ============================================================================ */

static void setup_secp256k1() {
    if (secp == NULL) {
        secp = new Secp256K1();
        secp->Init();
    }
}

static void cleanup_secp256k1() {
    if (secp != NULL) {
        delete secp;
        secp = NULL;
    }
}

/* ============================================================================
 * Construction Tests
 * ============================================================================ */

TEST(intgroup_construct_size_1) {
    IntGroup group(1);
    ASSERT_EQ(1, group.GetSize());
}

TEST(intgroup_construct_size_10) {
    IntGroup group(10);
    ASSERT_EQ(10, group.GetSize());
}

TEST(intgroup_construct_size_100) {
    IntGroup group(100);
    ASSERT_EQ(100, group.GetSize());
}

TEST(intgroup_construct_size_1024) {
    IntGroup group(1024);
    ASSERT_EQ(1024, group.GetSize());
}

/* ============================================================================
 * Basic Modular Inversion Tests
 * ============================================================================ */

TEST(intgroup_modinv) {
    setup_secp256k1();

    const int SIZE = 5;
    Int values[SIZE];
    Int original[SIZE];

    /* Initialize with test values */
    for (int i = 0; i < SIZE; i++) {
        values[i].SetInt64((i + 1) * 17);
        original[i].Set(&values[i]);
    }

    IntGroup group(SIZE);
    group.Set(values);
    group.ModInv();

    /* Verify: original[i] * inverse[i] ≡ 1 (mod p) for all i */
    Int result;
    for (int i = 0; i < SIZE; i++) {
        result.ModMul(&original[i], &values[i]);
        ASSERT_TRUE(result.IsOne());
    }
}

TEST(intgroup_modinv_single_element) {
    setup_secp256k1();

    Int values[1];
    values[0].SetInt64(42);

    Int original;
    original.Set(&values[0]);

    IntGroup group(1);
    group.Set(values);
    group.ModInv();

    /* Verify: original * inverse ≡ 1 (mod p) */
    Int result;
    result.ModMul(&original, &values[0]);
    ASSERT_TRUE(result.IsOne());
}

TEST(intgroup_modinv_two_elements) {
    setup_secp256k1();

    Int values[2];
    values[0].SetInt64(17);
    values[1].SetInt64(99);

    Int original[2];
    original[0].Set(&values[0]);
    original[1].Set(&values[1]);

    IntGroup group(2);
    group.Set(values);
    group.ModInv();

    /* Verify: original[i] * inverse[i] ≡ 1 (mod p) for all i */
    Int result;

    result.ModMul(&original[0], &values[0]);
    ASSERT_TRUE(result.IsOne());

    result.ModMul(&original[1], &values[1]);
    ASSERT_TRUE(result.IsOne());
}

TEST(intgroup_modinv_ten_elements) {
    setup_secp256k1();

    const int SIZE = 10;
    Int values[SIZE];
    Int original[SIZE];

    /* Initialize with different values */
    for (int i = 0; i < SIZE; i++) {
        values[i].SetInt64((i + 1) * 13);  /* 13, 26, 39, ... */
        original[i].Set(&values[i]);
    }

    IntGroup group(SIZE);
    group.Set(values);
    group.ModInv();

    /* Verify: original[i] * inverse[i] ≡ 1 (mod p) for all i */
    Int result;
    for (int i = 0; i < SIZE; i++) {
        result.ModMul(&original[i], &values[i]);
        ASSERT_TRUE(result.IsOne());
    }
}

TEST(intgroup_modinv_hundred_elements) {
    setup_secp256k1();

    const int SIZE = 100;
    Int *values = new Int[SIZE];
    Int *original = new Int[SIZE];

    /* Initialize with different values */
    for (int i = 0; i < SIZE; i++) {
        values[i].SetInt64((i + 1) * 7);  /* 7, 14, 21, ... */
        original[i].Set(&values[i]);
    }

    IntGroup group(SIZE);
    group.Set(values);
    group.ModInv();

    /* Verify: original[i] * inverse[i] ≡ 1 (mod p) for all i */
    Int result;
    for (int i = 0; i < SIZE; i++) {
        result.ModMul(&original[i], &values[i]);
        ASSERT_TRUE(result.IsOne());
    }

    delete[] values;
    delete[] original;
}

/* ============================================================================
 * Optimized Modular Inversion Tests
 * ============================================================================ */

TEST(intgroup_modinv_optimized_single) {
    setup_secp256k1();

    Int values[1];
    values[0].SetInt64(42);

    Int original;
    original.Set(&values[0]);

    IntGroup group(1);
    group.Set(values);
    group.ModInvOptimized();

    /* Verify: original * inverse ≡ 1 (mod p) */
    Int result;
    result.ModMul(&original, &values[0]);
    ASSERT_TRUE(result.IsOne());
}

TEST(intgroup_modinv_optimized_ten_elements) {
    setup_secp256k1();

    const int SIZE = 10;
    Int values[SIZE];
    Int original[SIZE];

    /* Initialize with different values */
    for (int i = 0; i < SIZE; i++) {
        values[i].SetInt64((i + 1) * 11);  /* 11, 22, 33, ... */
        original[i].Set(&values[i]);
    }

    IntGroup group(SIZE);
    group.Set(values);
    group.ModInvOptimized();

    /* Verify: original[i] * inverse[i] ≡ 1 (mod p) for all i */
    Int result;
    for (int i = 0; i < SIZE; i++) {
        result.ModMul(&original[i], &values[i]);
        ASSERT_TRUE(result.IsOne());
    }
}

TEST(intgroup_modinv_optimized_hundred_elements) {
    setup_secp256k1();

    const int SIZE = 100;
    Int *values = new Int[SIZE];
    Int *original = new Int[SIZE];

    /* Initialize with different values */
    for (int i = 0; i < SIZE; i++) {
        values[i].SetInt64((i + 1) * 5);  /* 5, 10, 15, ... */
        original[i].Set(&values[i]);
    }

    IntGroup group(SIZE);
    group.Set(values);
    group.ModInvOptimized();

    /* Verify: original[i] * inverse[i] ≡ 1 (mod p) for all i */
    Int result;
    for (int i = 0; i < SIZE; i++) {
        result.ModMul(&original[i], &values[i]);
        ASSERT_TRUE(result.IsOne());
    }

    delete[] values;
    delete[] original;
}

/* ============================================================================
 * Comparison Tests: ModInv() vs ModInvOptimized()
 * ============================================================================ */

TEST(intgroup_modinv_vs_optimized_same_result) {
    setup_secp256k1();

    const int SIZE = 20;
    Int values1[SIZE];
    Int values2[SIZE];

    /* Initialize both arrays with same values */
    for (int i = 0; i < SIZE; i++) {
        values1[i].SetInt64((i + 1) * 23);
        values2[i].Set(&values1[i]);
    }

    /* Run standard ModInv on first array */
    IntGroup group1(SIZE);
    group1.Set(values1);
    group1.ModInv();

    /* Run ModInvOptimized on second array */
    IntGroup group2(SIZE);
    group2.Set(values2);
    group2.ModInvOptimized();

    /* Verify both produce identical results */
    for (int i = 0; i < SIZE; i++) {
        ASSERT_TRUE(values1[i].IsEqual(&values2[i]));
    }
}

TEST(intgroup_modinv_vs_optimized_large_batch) {
    setup_secp256k1();

    const int SIZE = 256;
    Int *values1 = new Int[SIZE];
    Int *values2 = new Int[SIZE];

    /* Initialize both arrays with same values */
    for (int i = 0; i < SIZE; i++) {
        values1[i].SetInt64((i + 1) * 3);
        values2[i].Set(&values1[i]);
    }

    /* Run standard ModInv on first array */
    IntGroup group1(SIZE);
    group1.Set(values1);
    group1.ModInv();

    /* Run ModInvOptimized on second array */
    IntGroup group2(SIZE);
    group2.Set(values2);
    group2.ModInvOptimized();

    /* Verify both produce identical results */
    for (int i = 0; i < SIZE; i++) {
        ASSERT_TRUE(values1[i].IsEqual(&values2[i]));
    }

    delete[] values1;
    delete[] values2;
}

/* ============================================================================
 * Large Value Tests (256-bit integers)
 * ============================================================================ */

TEST(intgroup_modinv_large_values) {
    setup_secp256k1();

    const int SIZE = 5;
    Int values[SIZE];
    Int original[SIZE];

    /* Initialize with large hex values */
    values[0].SetBase16("123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
    values[1].SetBase16("FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA987654321");
    values[2].SetBase16("1111111111111111111111111111111111111111111111111111111111111111");
    values[3].SetBase16("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    values[4].SetBase16("5555555555555555555555555555555555555555555555555555555555555555");

    /* Save original values */
    for (int i = 0; i < SIZE; i++) {
        original[i].Set(&values[i]);
    }

    IntGroup group(SIZE);
    group.Set(values);
    group.ModInv();

    /* Verify: original[i] * inverse[i] ≡ 1 (mod p) for all i */
    Int result;
    for (int i = 0; i < SIZE; i++) {
        result.ModMul(&original[i], &values[i]);
        ASSERT_TRUE(result.IsOne());
    }
}

TEST(intgroup_modinv_optimized_large_values) {
    setup_secp256k1();

    const int SIZE = 5;
    Int values[SIZE];
    Int original[SIZE];

    /* Initialize with large hex values */
    values[0].SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
    values[1].SetBase16("800000000000000000000000000000000000000000000000000000000000000");
    values[2].SetBase16("123456789ABCDEF123456789ABCDEF123456789ABCDEF123456789ABCDEF1");
    values[3].SetBase16("987654321FEDCBA987654321FEDCBA987654321FEDCBA987654321FEDCBA9");
    values[4].SetBase16("DEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEF");

    /* Save original values */
    for (int i = 0; i < SIZE; i++) {
        original[i].Set(&values[i]);
    }

    IntGroup group(SIZE);
    group.Set(values);
    group.ModInvOptimized();

    /* Verify: original[i] * inverse[i] ≡ 1 (mod p) for all i */
    Int result;
    for (int i = 0; i < SIZE; i++) {
        result.ModMul(&original[i], &values[i]);
        ASSERT_TRUE(result.IsOne());
    }
}

/* ============================================================================
 * Modular Arithmetic Correctness Tests (IntMod Operations)
 * ============================================================================ */

TEST(modular_arithmetic_modadd_basic) {
    setup_secp256k1();

    Int a, b, result, expected;

    /* Test: (5 + 7) mod p = 12 mod p */
    a.SetInt64(5);
    b.SetInt64(7);
    result.Set(&a);
    result.ModAdd(&b);
    expected.SetInt64(12);
    ASSERT_TRUE(result.IsEqual(&expected));
}

TEST(modular_arithmetic_modadd_two_operands) {
    setup_secp256k1();

    Int a, b, result, expected;

    /* Test: ModAdd(a, b) where result = (a + b) mod p */
    a.SetInt64(100);
    b.SetInt64(200);
    result.ModAdd(&a, &b);
    expected.SetInt64(300);
    ASSERT_TRUE(result.IsEqual(&expected));
}

TEST(modular_arithmetic_modadd_uint64) {
    setup_secp256k1();

    Int a, result, expected;

    /* Test: (50 + 25) mod p */
    a.SetInt64(50);
    result.Set(&a);
    result.ModAdd(25);
    expected.SetInt64(75);
    ASSERT_TRUE(result.IsEqual(&expected));
}

TEST(modular_arithmetic_modadd_overflow) {
    setup_secp256k1();

    Int a, b, result;

    /* Test: Addition that exceeds P should wrap correctly */
    /* Use P - 1 to test overflow handling */
    a.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2E");  /* P - 1 */
    b.SetInt64(10);
    result.Set(&a);
    result.ModAdd(&b);

    /* Result should wrap around: (P - 1 + 10) mod P = 9 */
    Int expected;
    expected.SetInt64(9);
    ASSERT_TRUE(result.IsEqual(&expected));
}

TEST(modular_arithmetic_moddouble_basic) {
    setup_secp256k1();

    Int a, result, expected;

    /* Test: (7 * 2) mod p = 14 mod p */
    a.SetInt64(7);
    result.Set(&a);
    result.ModDouble();
    expected.SetInt64(14);
    ASSERT_TRUE(result.IsEqual(&expected));
}

TEST(modular_arithmetic_moddouble_large) {
    setup_secp256k1();

    Int a, result, expected;

    /* Test: Doubling a large value */
    a.SetBase16("123456789ABCDEF123456789ABCDEF123456789ABCDEF123456789ABCDEF1");
    result.Set(&a);
    result.ModDouble();

    /* Verify by adding to itself */
    expected.Set(&a);
    expected.ModAdd(&a);
    ASSERT_TRUE(result.IsEqual(&expected));
}

TEST(modular_arithmetic_moddouble_overflow) {
    setup_secp256k1();

    Int a, result;

    /* Test: Doubling a value near P/2 */
    a.SetBase16("7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF7FFFFE17");
    result.Set(&a);
    result.ModDouble();

    /* Result should be less than P (wrap occurs) */
    Int p;
    p.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F");
    ASSERT_TRUE(result.IsLower(&p));
}

TEST(modular_arithmetic_modsub_basic) {
    setup_secp256k1();

    Int a, b, result, expected;

    /* Test: (10 - 3) mod p = 7 mod p */
    a.SetInt64(10);
    b.SetInt64(3);
    result.Set(&a);
    result.ModSub(&b);
    expected.SetInt64(7);
    ASSERT_TRUE(result.IsEqual(&expected));
}

TEST(modular_arithmetic_modsub_two_operands) {
    setup_secp256k1();

    Int a, b, result, expected;

    /* Test: ModSub(a, b) where result = (a - b) mod p */
    a.SetInt64(500);
    b.SetInt64(100);
    result.ModSub(&a, &b);
    expected.SetInt64(400);
    ASSERT_TRUE(result.IsEqual(&expected));
}

TEST(modular_arithmetic_modsub_uint64) {
    setup_secp256k1();

    Int a, result, expected;

    /* Test: (100 - 25) mod p */
    a.SetInt64(100);
    result.Set(&a);
    result.ModSub(25);
    expected.SetInt64(75);
    ASSERT_TRUE(result.IsEqual(&expected));
}

TEST(modular_arithmetic_modsub_underflow) {
    setup_secp256k1();

    Int a, b, result;

    /* Test: Subtraction that goes negative should wrap to P + (a - b) */
    a.SetInt64(5);
    b.SetInt64(10);
    result.Set(&a);
    result.ModSub(&b);

    /* Result should be P + (5 - 10) = P - 5 */
    Int p;
    p.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F");
    Int expected;
    expected.Set(&p);
    expected.Sub(5);
    ASSERT_TRUE(result.IsEqual(&expected));
}

TEST(modular_arithmetic_modsub_large_values) {
    setup_secp256k1();

    Int a, b, result;

    /* Test: Subtraction of large values */
    a.SetBase16("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    b.SetBase16("5555555555555555555555555555555555555555555555555555555555555555");
    result.Set(&a);
    result.ModSub(&b);

    /* Verify by adding back */
    Int verify;
    verify.Set(&result);
    verify.ModAdd(&b);
    ASSERT_TRUE(verify.IsEqual(&a));
}

TEST(modular_arithmetic_modneg_basic) {
    setup_secp256k1();

    Int a, result;

    /* Test: -5 mod p = P - 5 */
    a.SetInt64(5);
    result.Set(&a);
    result.ModNeg();

    Int p;
    p.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F");
    Int expected;
    expected.Set(&p);
    expected.Sub(5);
    ASSERT_TRUE(result.IsEqual(&expected));
}

TEST(modular_arithmetic_modneg_zero) {
    setup_secp256k1();

    Int a, result;

    /* Test: -0 mod p = P (but should be normalized to P) */
    a.SetInt64(0);
    result.Set(&a);
    result.ModNeg();

    /* Result should be P */
    Int p;
    p.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F");
    ASSERT_TRUE(result.IsEqual(&p));
}

TEST(modular_arithmetic_modneg_large) {
    setup_secp256k1();

    Int a, result;

    /* Test: Negation of large value */
    a.SetBase16("123456789ABCDEF123456789ABCDEF123456789ABCDEF123456789ABCDEF1");
    result.Set(&a);
    result.ModNeg();

    /* Verify: a + (-a) = 0 mod p */
    Int verify;
    verify.Set(&a);
    verify.ModAdd(&result);

    /* verify should be 0 or P (both represent 0 mod p) */
    Int p;
    p.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F");
    ASSERT_TRUE(verify.IsZero() || verify.IsEqual(&p));
}

TEST(modular_arithmetic_modneg_double_negation) {
    setup_secp256k1();

    Int a, result, original;

    /* Test: -(-a) = a mod p */
    a.SetInt64(12345);
    original.Set(&a);
    result.Set(&a);
    result.ModNeg();
    result.ModNeg();

    ASSERT_TRUE(result.IsEqual(&original));
}

TEST(modular_arithmetic_add_sub_inverse) {
    setup_secp256k1();

    Int a, b, result, original;

    /* Test: (a + b) - b = a mod p */
    a.SetInt64(777);
    b.SetInt64(888);
    original.Set(&a);

    result.Set(&a);
    result.ModAdd(&b);
    result.ModSub(&b);

    ASSERT_TRUE(result.IsEqual(&original));
}

TEST(modular_arithmetic_properties_commutativity) {
    setup_secp256k1();

    Int a, b, result1, result2;

    /* Test: a + b = b + a (mod p) */
    a.SetInt64(123);
    b.SetInt64(456);

    result1.ModAdd(&a, &b);
    result2.ModAdd(&b, &a);

    ASSERT_TRUE(result1.IsEqual(&result2));
}

TEST(modular_arithmetic_properties_associativity) {
    setup_secp256k1();

    Int a, b, c, result1, result2, temp;

    /* Test: (a + b) + c = a + (b + c) (mod p) */
    a.SetInt64(111);
    b.SetInt64(222);
    c.SetInt64(333);

    /* Calculate (a + b) + c */
    result1.ModAdd(&a, &b);
    result1.ModAdd(&c);

    /* Calculate a + (b + c) */
    temp.ModAdd(&b, &c);
    result2.Set(&a);
    result2.ModAdd(&temp);

    ASSERT_TRUE(result1.IsEqual(&result2));
}

TEST(modular_arithmetic_properties_identity) {
    setup_secp256k1();

    Int a, result, zero;

    /* Test: a + 0 = a (mod p) */
    a.SetInt64(99999);
    zero.SetInt64(0);

    result.Set(&a);
    result.ModAdd(&zero);

    ASSERT_TRUE(result.IsEqual(&a));
}

TEST(modular_arithmetic_properties_additive_inverse) {
    setup_secp256k1();

    Int a, neg_a, result;

    /* Test: a + (-a) = 0 (mod p) */
    a.SetInt64(54321);
    neg_a.Set(&a);
    neg_a.ModNeg();

    result.Set(&a);
    result.ModAdd(&neg_a);

    /* Result should be 0 or P (both represent 0 mod p) */
    Int p;
    p.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F");
    ASSERT_TRUE(result.IsZero() || result.IsEqual(&p));
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

/* Exported function for test runner */
int run_intgroup_tests(void) {
    TEST_INIT();

    /* Initialize secp256k1 once for all tests */
    setup_secp256k1();

    TEST_SECTION("Construction");
    RUN_TEST(intgroup_construct_size_1);
    RUN_TEST(intgroup_construct_size_10);
    RUN_TEST(intgroup_construct_size_100);
    RUN_TEST(intgroup_construct_size_1024);

    TEST_SECTION("Basic Modular Inversion");
    RUN_TEST(intgroup_modinv);
    RUN_TEST(intgroup_modinv_single_element);
    RUN_TEST(intgroup_modinv_two_elements);
    RUN_TEST(intgroup_modinv_ten_elements);
    RUN_TEST(intgroup_modinv_hundred_elements);

    TEST_SECTION("Optimized Modular Inversion");
    RUN_TEST(intgroup_modinv_optimized_single);
    RUN_TEST(intgroup_modinv_optimized_ten_elements);
    RUN_TEST(intgroup_modinv_optimized_hundred_elements);

    TEST_SECTION("ModInv vs ModInvOptimized Comparison");
    RUN_TEST(intgroup_modinv_vs_optimized_same_result);
    RUN_TEST(intgroup_modinv_vs_optimized_large_batch);

    TEST_SECTION("Large Value Tests");
    RUN_TEST(intgroup_modinv_large_values);
    RUN_TEST(intgroup_modinv_optimized_large_values);

    TEST_SECTION("Modular Arithmetic Correctness (IntMod Operations)");
    RUN_TEST(modular_arithmetic_modadd_basic);
    RUN_TEST(modular_arithmetic_modadd_two_operands);
    RUN_TEST(modular_arithmetic_modadd_uint64);
    RUN_TEST(modular_arithmetic_modadd_overflow);
    RUN_TEST(modular_arithmetic_moddouble_basic);
    RUN_TEST(modular_arithmetic_moddouble_large);
    RUN_TEST(modular_arithmetic_moddouble_overflow);
    RUN_TEST(modular_arithmetic_modsub_basic);
    RUN_TEST(modular_arithmetic_modsub_two_operands);
    RUN_TEST(modular_arithmetic_modsub_uint64);
    RUN_TEST(modular_arithmetic_modsub_underflow);
    RUN_TEST(modular_arithmetic_modsub_large_values);
    RUN_TEST(modular_arithmetic_modneg_basic);
    RUN_TEST(modular_arithmetic_modneg_zero);
    RUN_TEST(modular_arithmetic_modneg_large);
    RUN_TEST(modular_arithmetic_modneg_double_negation);
    RUN_TEST(modular_arithmetic_add_sub_inverse);
    RUN_TEST(modular_arithmetic_properties_commutativity);
    RUN_TEST(modular_arithmetic_properties_associativity);
    RUN_TEST(modular_arithmetic_properties_identity);
    RUN_TEST(modular_arithmetic_properties_additive_inverse);

    cleanup_secp256k1();

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_intgroup_tests();
}
#endif
