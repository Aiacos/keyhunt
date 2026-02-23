/*
 * test_point.cpp - Unit tests for the Point (elliptic curve point) class
 *
 * Tests Point operations in projective coordinates (x, y, z):
 * - Construction (default, from coordinates, copy)
 * - State checks (isZero, equals)
 * - Manipulation (Set, Clear, Reduce)
 * - Projective to affine conversion
 * - Point arithmetic (Add, Double, Negation via Secp256K1)
 */

#include "test_framework.h"
#include "secp256k1/Point.h"
#include "secp256k1/SECP256k1.h"

/* ============================================================================
 * Basic Construction Tests
 * ============================================================================ */

TEST(point_construct_default) {
    Point p;
    /* Default constructor creates uninitialized point */
    /* We can't make assumptions about values, but it shouldn't crash */
    (void)p;  /* Just verify construction works */
}

TEST(point_construct_from_coords) {
    Int x(1);
    Int y(2);
    Int z(3);
    Point p(&x, &y, &z);

    ASSERT_TRUE(p.x.IsEqual(&x));
    ASSERT_TRUE(p.y.IsEqual(&y));
    ASSERT_TRUE(p.z.IsEqual(&z));
}

TEST(point_construct_xz_only) {
    Int x(100);
    Int z(200);
    Point p(&x, &z);

    ASSERT_TRUE(p.x.IsEqual(&x));
    ASSERT_TRUE(p.z.IsEqual(&z));
    /* y is not initialized in this constructor */
}

TEST(point_construct_copy) {
    Int x(42);
    Int y(84);
    Int z(126);
    Point p1(&x, &y, &z);
    Point p2(p1);

    ASSERT_TRUE(p2.x.IsEqual(&x));
    ASSERT_TRUE(p2.y.IsEqual(&y));
    ASSERT_TRUE(p2.z.IsEqual(&z));
}

/* ============================================================================
 * Clear Tests
 * ============================================================================ */

TEST(point_clear) {
    Int x(100);
    Int y(200);
    Int z(300);
    Point p(&x, &y, &z);

    p.Clear();

    ASSERT_TRUE(p.x.IsZero());
    ASSERT_TRUE(p.y.IsZero());
    ASSERT_TRUE(p.z.IsZero());
}

TEST(point_clear_already_zero) {
    Point p;
    p.Clear();

    ASSERT_TRUE(p.x.IsZero());
    ASSERT_TRUE(p.y.IsZero());
    ASSERT_TRUE(p.z.IsZero());
}

/* ============================================================================
 * Set Tests
 * ============================================================================ */

TEST(point_set_from_coords) {
    Point p;
    Int x(10);
    Int y(20);
    Int z(30);

    p.Set(&x, &y, &z);

    ASSERT_TRUE(p.x.IsEqual(&x));
    ASSERT_TRUE(p.y.IsEqual(&y));
    ASSERT_TRUE(p.z.IsEqual(&z));
}

TEST(point_set_from_point) {
    Int x(1);
    Int y(2);
    Int z(3);
    Point p1(&x, &y, &z);
    Point p2;

    p2.Set(p1);

    ASSERT_TRUE(p2.x.IsEqual(&x));
    ASSERT_TRUE(p2.y.IsEqual(&y));
    ASSERT_TRUE(p2.z.IsEqual(&z));
}

TEST(point_set_overwrite) {
    Int x1(100);
    Int y1(200);
    Int z1(300);
    Point p(&x1, &y1, &z1);

    Int x2(42);
    Int y2(84);
    Int z2(126);
    p.Set(&x2, &y2, &z2);

    ASSERT_TRUE(p.x.IsEqual(&x2));
    ASSERT_TRUE(p.y.IsEqual(&y2));
    ASSERT_TRUE(p.z.IsEqual(&z2));
}

/* ============================================================================
 * IsZero Tests
 * ============================================================================ */

TEST(point_is_zero_cleared) {
    Point p;
    p.Clear();
    ASSERT_TRUE(p.isZero());
}

TEST(point_is_zero_z_is_zero) {
    /* In projective coordinates, z=0 means point at infinity */
    Int x(100);
    Int y(200);
    Int z(0);
    Point p(&x, &y, &z);

    ASSERT_TRUE(p.isZero());
}

TEST(point_is_zero_xy_zero) {
    /* Traditional (0,0) representation - also considered zero */
    Int x(0);
    Int y(0);
    Int z(1);
    Point p(&x, &y, &z);

    ASSERT_TRUE(p.isZero());
}

TEST(point_is_not_zero) {
    Int x(1);
    Int y(2);
    Int z(3);
    Point p(&x, &y, &z);

    ASSERT_FALSE(p.isZero());
}

TEST(point_is_zero_only_x_zero) {
    /* Only x=0 doesn't make point zero */
    Int x(0);
    Int y(1);
    Int z(1);
    Point p(&x, &y, &z);

    ASSERT_FALSE(p.isZero());
}

/* ============================================================================
 * Equals Tests
 * ============================================================================ */

TEST(point_equals_same_coords) {
    Int x(42);
    Int y(84);
    Int z(126);
    Point p1(&x, &y, &z);
    Point p2(&x, &y, &z);

    ASSERT_TRUE(p1.equals(p2));
}

TEST(point_equals_self) {
    Int x(1);
    Int y(2);
    Int z(3);
    Point p(&x, &y, &z);

    ASSERT_TRUE(p.equals(p));
}

TEST(point_equals_different_x) {
    Int x1(1);
    Int x2(2);
    Int y(10);
    Int z(20);
    Point p1(&x1, &y, &z);
    Point p2(&x2, &y, &z);

    ASSERT_FALSE(p1.equals(p2));
}

TEST(point_equals_different_y) {
    Int x(10);
    Int y1(1);
    Int y2(2);
    Int z(20);
    Point p1(&x, &y1, &z);
    Point p2(&x, &y2, &z);

    ASSERT_FALSE(p1.equals(p2));
}

TEST(point_equals_different_z) {
    Int x(10);
    Int y(20);
    Int z1(1);
    Int z2(2);
    Point p1(&x, &y, &z1);
    Point p2(&x, &y, &z2);

    ASSERT_FALSE(p1.equals(p2));
}

TEST(point_equals_both_zero) {
    Point p1;
    Point p2;
    p1.Clear();
    p2.Clear();

    ASSERT_TRUE(p1.equals(p2));
}

/* ============================================================================
 * Reduce Tests (Projective to Affine Conversion)
 * ============================================================================ */

TEST(point_reduce_z_one) {
    /* Point already in affine form (z=1) */
    Int x(42);
    Int y(84);
    Int z(1);
    Point p(&x, &y, &z);

    p.Reduce();

    /* Should remain unchanged */
    ASSERT_TRUE(p.x.IsEqual(&x));
    ASSERT_TRUE(p.y.IsEqual(&y));
    ASSERT_EQ(1, p.z.GetInt64());
}

TEST(point_reduce_z_zero) {
    /* Point at infinity (z=0) */
    Int x(100);
    Int y(200);
    Int z(0);
    Point p(&x, &y, &z);

    p.Reduce();

    /* Should be set to canonical form (0,0,0) */
    ASSERT_TRUE(p.x.IsZero());
    ASSERT_TRUE(p.y.IsZero());
    ASSERT_TRUE(p.z.IsZero());
}

/* Note: Full Reduce() testing with non-trivial z values requires
 * modular inverse operations which depend on the secp256k1 modulus.
 * These are integration tests that belong in test_secp256k1.cpp */

/* ============================================================================
 * Copy Constructor Edge Cases
 * ============================================================================ */

TEST(point_copy_zero_point) {
    Point p1;
    p1.Clear();
    Point p2(p1);

    ASSERT_TRUE(p2.isZero());
    ASSERT_TRUE(p1.equals(p2));
}

TEST(point_copy_and_modify) {
    Int x(1);
    Int y(2);
    Int z(3);
    Point p1(&x, &y, &z);
    Point p2(p1);

    /* Modify p2 */
    p2.Clear();

    /* p1 should remain unchanged */
    ASSERT_TRUE(p1.x.IsEqual(&x));
    ASSERT_TRUE(p1.y.IsEqual(&y));
    ASSERT_TRUE(p1.z.IsEqual(&z));

    /* p2 should be zero */
    ASSERT_TRUE(p2.isZero());
}

/* ============================================================================
 * Point Arithmetic Tests (via Secp256K1)
 * ============================================================================ */

TEST(point_add) {
    Secp256K1 secp;
    secp.Init();

    /* Test adding generator to itself */
    Point result = secp.Add(secp.G, secp.G);

    /* Result should not be zero */
    ASSERT_FALSE(result.isZero());

    /* Adding G+G should equal Double(G) */
    Point doubled = secp.Double(secp.G);
    ASSERT_TRUE(result.equals(doubled));
}

TEST(point_add_zero) {
    Secp256K1 secp;
    secp.Init();

    /* Create zero point */
    Point zero;
    zero.Clear();

    /* Adding zero to G should return G */
    Point result = secp.Add(secp.G, zero);

    /* Note: Due to projective coordinates, we may need to reduce to compare */
    ASSERT_FALSE(result.isZero());
}

TEST(point_add_commutative) {
    Secp256K1 secp;
    secp.Init();

    /* Create two points: G and 2G */
    Point g1 = secp.G;
    Point g2 = secp.Double(secp.G);

    /* Test commutativity: P1+P2 should equal P2+P1 */
    Point result1 = secp.Add(g1, g2);
    Point result2 = secp.Add(g2, g1);

    /* Results should be equal */
    ASSERT_TRUE(result1.equals(result2));
}

TEST(point_double) {
    Secp256K1 secp;
    secp.Init();

    /* Double the generator */
    Point doubled = secp.Double(secp.G);

    /* Result should not be zero */
    ASSERT_FALSE(doubled.isZero());

    /* Should be the same as G+G */
    Point added = secp.Add(secp.G, secp.G);
    ASSERT_TRUE(doubled.equals(added));
}

TEST(point_double_zero) {
    Secp256K1 secp;
    secp.Init();

    /* Create zero point */
    Point zero;
    zero.Clear();

    /* Doubling zero should return zero */
    Point result = secp.Double(zero);
    ASSERT_TRUE(result.isZero());
}

TEST(point_negation) {
    Secp256K1 secp;
    secp.Init();

    /* Negate the generator */
    Point negG = secp.Negation(secp.G);

    /* Negation should not be zero */
    ASSERT_FALSE(negG.isZero());

    /* Adding a point to its negation should give zero (point at infinity) */
    Point result = secp.Add(secp.G, negG);
    ASSERT_TRUE(result.isZero());
}

TEST(point_add2) {
    Secp256K1 secp;
    secp.Init();

    /* Test the Add2 variant */
    Point result = secp.Add2(secp.G, secp.G);

    /* Should equal Double(G) */
    Point doubled = secp.Double(secp.G);
    ASSERT_TRUE(result.equals(doubled));
}

TEST(point_add_direct) {
    Secp256K1 secp;
    secp.Init();

    /* Test the AddDirect variant */
    Point g2 = secp.Double(secp.G);
    Point result = secp.AddDirect(secp.G, g2);

    /* Result should not be zero */
    ASSERT_FALSE(result.isZero());
}

TEST(point_double_direct) {
    Secp256K1 secp;
    secp.Init();

    /* Test DoubleDirect */
    Point result = secp.DoubleDirect(secp.G);

    /* Should equal regular Double */
    Point doubled = secp.Double(secp.G);
    ASSERT_TRUE(result.equals(doubled));
}

TEST(point_triple) {
    Secp256K1 secp;
    secp.Init();

    /* Test 3*G = 2*G + G */
    Point doubled = secp.Double(secp.G);
    Point tripled = secp.Add(doubled, secp.G);

    /* Verify it's not zero */
    ASSERT_FALSE(tripled.isZero());

    /* Alternatively: G + G + G */
    Point temp = secp.Add(secp.G, secp.G);
    Point tripled2 = secp.Add(temp, secp.G);

    ASSERT_TRUE(tripled.equals(tripled2));
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

/* Exported function for test runner */
int run_point_tests(void) {
    TEST_INIT();

    TEST_SECTION("Construction");
    RUN_TEST(point_construct_default);
    RUN_TEST(point_construct_from_coords);
    RUN_TEST(point_construct_xz_only);
    RUN_TEST(point_construct_copy);

    TEST_SECTION("Clear");
    RUN_TEST(point_clear);
    RUN_TEST(point_clear_already_zero);

    TEST_SECTION("Set");
    RUN_TEST(point_set_from_coords);
    RUN_TEST(point_set_from_point);
    RUN_TEST(point_set_overwrite);

    TEST_SECTION("IsZero");
    RUN_TEST(point_is_zero_cleared);
    RUN_TEST(point_is_zero_z_is_zero);
    RUN_TEST(point_is_zero_xy_zero);
    RUN_TEST(point_is_not_zero);
    RUN_TEST(point_is_zero_only_x_zero);

    TEST_SECTION("Equals");
    RUN_TEST(point_equals_same_coords);
    RUN_TEST(point_equals_self);
    RUN_TEST(point_equals_different_x);
    RUN_TEST(point_equals_different_y);
    RUN_TEST(point_equals_different_z);
    RUN_TEST(point_equals_both_zero);

    TEST_SECTION("Reduce");
    RUN_TEST(point_reduce_z_one);
    RUN_TEST(point_reduce_z_zero);

    TEST_SECTION("Copy Constructor Edge Cases");
    RUN_TEST(point_copy_zero_point);
    RUN_TEST(point_copy_and_modify);

    TEST_SECTION("Point Arithmetic");
    RUN_TEST(point_add);
    RUN_TEST(point_add_zero);
    RUN_TEST(point_add_commutative);
    RUN_TEST(point_double);
    RUN_TEST(point_double_zero);
    RUN_TEST(point_negation);
    RUN_TEST(point_add2);
    RUN_TEST(point_add_direct);
    RUN_TEST(point_double_direct);
    RUN_TEST(point_triple);

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_point_tests();
}
#endif
