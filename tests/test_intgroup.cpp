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
        result.ModMulK1(&original[i], &values[i]);
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
    result.ModMulK1(&original, &values[0]);
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

    result.ModMulK1(&original[0], &values[0]);
    ASSERT_TRUE(result.IsOne());

    result.ModMulK1(&original[1], &values[1]);
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
        result.ModMulK1(&original[i], &values[i]);
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
        result.ModMulK1(&original[i], &values[i]);
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
    result.ModMulK1(&original, &values[0]);
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
        result.ModMulK1(&original[i], &values[i]);
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
        result.ModMulK1(&original[i], &values[i]);
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
        result.ModMulK1(&original[i], &values[i]);
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
        result.ModMulK1(&original[i], &values[i]);
        ASSERT_TRUE(result.IsOne());
    }
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
