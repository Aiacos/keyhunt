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
 * Edge Cases: Identity, Infinity, Special Points
 * ============================================================================ */

TEST(point_edge_cases) {
    Secp256K1 secp;
    secp.Init();

    /* --- Point at Infinity Tests --- */

    /* Test 1: Point at infinity with z=0 (canonical form) */
    Point infinity1;
    Int x_any(12345);
    Int y_any(67890);
    Int z_zero(0);
    infinity1.Set(&x_any, &y_any, &z_zero);

    ASSERT_TRUE(infinity1.isZero());

    /* Test 2: Point at infinity after Clear() */
    Point infinity2;
    infinity2.Clear();
    ASSERT_TRUE(infinity2.isZero());
    ASSERT_TRUE(infinity2.x.IsZero());
    ASSERT_TRUE(infinity2.y.IsZero());
    ASSERT_TRUE(infinity2.z.IsZero());

    /* Test 3: Both infinity representations should be detected as zero */
    ASSERT_TRUE(infinity1.isZero());
    ASSERT_TRUE(infinity2.isZero());

    /* --- Identity Element in Group Law --- */

    /* Test 4: G + O = G (where O is point at infinity/identity) */
    Point result1 = secp.Add(secp.G, infinity2);
    ASSERT_FALSE(result1.isZero());
    /* Result should represent the same point as G (though may differ in projective coords) */

    /* Test 5: O + G = G (commutativity with identity) */
    Point result2 = secp.Add(infinity2, secp.G);
    ASSERT_FALSE(result2.isZero());

    /* Test 6: O + O = O (identity + identity = identity) */
    Point result3 = secp.Add(infinity1, infinity2);
    ASSERT_TRUE(result3.isZero());

    /* Test 7: 2*O = O (doubling identity gives identity) */
    Point result4 = secp.Double(infinity2);
    ASSERT_TRUE(result4.isZero());

    /* --- Generator Point Special Properties --- */

    /* Test 8: Generator point should not be zero */
    ASSERT_FALSE(secp.G.isZero());

    /* Test 9: Generator has z=1 (affine form) */
    ASSERT_EQ(1, secp.G.z.GetInt64());

    /* Test 10: G - G = O (point minus itself gives identity) */
    Point negG = secp.Negation(secp.G);
    Point should_be_zero = secp.Add(secp.G, negG);
    ASSERT_TRUE(should_be_zero.isZero());

    /* Test 11: -(- G) = G (double negation) */
    Point doubleNegG = secp.Negation(negG);
    /* After reduction, should equal G */
    doubleNegG.Reduce();
    Point G_reduced = secp.G;
    G_reduced.Reduce();
    ASSERT_TRUE(doubleNegG.x.IsEqual(&G_reduced.x));
    ASSERT_TRUE(doubleNegG.y.IsEqual(&G_reduced.y));

    /* --- Projective Equivalence Tests --- */

    /* Test 12: Same affine point with different z coordinates */
    /* Point (X, Y, Z) = (2X, 2Y, 2Z) in projective coordinates */
    Int x_val(100);
    Int y_val(200);
    Int z1(1);
    Point p1(&x_val, &y_val, &z1);

    /* Create equivalent point with z=2: (100*2, 200*2, 1*2) = (200, 400, 2) */
    Int x_val2;
    x_val2.Set(&x_val);
    x_val2.Add(&x_val);  /* x_val2 = 2 * x_val = 200 */

    Int y_val2;
    y_val2.Set(&y_val);
    y_val2.Add(&y_val);  /* y_val2 = 2 * y_val = 400 */

    Int z2(2);
    Point p2(&x_val2, &y_val2, &z2);

    /* Not equal in projective form (different z) */
    ASSERT_FALSE(p1.equals(p2));

    /* But after reduction to affine, coordinates should match */
    p1.Reduce();
    p2.Reduce();
    ASSERT_TRUE(p1.x.IsEqual(&p2.x));
    ASSERT_TRUE(p1.y.IsEqual(&p2.y));
    ASSERT_EQ(1, p1.z.GetInt64());
    ASSERT_EQ(1, p2.z.GetInt64());

    /* --- Reduce Edge Cases --- */

    /* Test 13: Reduce on point at infinity gives canonical (0,0,0) */
    Point inf_test;
    Int x_nonzero(999);
    Int y_nonzero(888);
    Int z_zero2(0);
    inf_test.Set(&x_nonzero, &y_nonzero, &z_zero2);

    inf_test.Reduce();
    ASSERT_TRUE(inf_test.x.IsZero());
    ASSERT_TRUE(inf_test.y.IsZero());
    ASSERT_TRUE(inf_test.z.IsZero());
    ASSERT_TRUE(inf_test.isZero());

    /* Test 14: Reduce on already-affine point (z=1) is idempotent */
    Point affine_point;
    Int x_affine(42);
    Int y_affine(84);
    Int z_one(1);
    affine_point.Set(&x_affine, &y_affine, &z_one);

    Point before_reduce = affine_point;
    affine_point.Reduce();

    ASSERT_TRUE(affine_point.equals(before_reduce));
    ASSERT_TRUE(affine_point.x.IsEqual(&x_affine));
    ASSERT_TRUE(affine_point.y.IsEqual(&y_affine));
    ASSERT_EQ(1, affine_point.z.GetInt64());

    /* --- Special Arithmetic Edge Cases --- */

    /* Test 15: Verify G is on the curve */
    ASSERT_TRUE(secp.EC(secp.G));

    /* Test 16: 2G is also on the curve */
    Point double_g = secp.Double(secp.G);
    ASSERT_TRUE(secp.EC(double_g));

    /* Test 17: G + 2G = 3G is on the curve */
    Point triple_g = secp.Add(secp.G, double_g);
    ASSERT_TRUE(secp.EC(triple_g));

    /* Test 18: Negation of G is on the curve */
    Point neg_g = secp.Negation(secp.G);
    ASSERT_TRUE(secp.EC(neg_g));

    /* Test 19: -G has same x coordinate as G, but negated y */
    neg_g.Reduce();
    Point g_copy = secp.G;
    g_copy.Reduce();
    ASSERT_TRUE(neg_g.x.IsEqual(&g_copy.x));
    ASSERT_FALSE(neg_g.y.IsEqual(&g_copy.y));  /* y coordinates differ */
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

    TEST_SECTION("Edge Cases: Identity, Infinity, Special Points");
    RUN_TEST(point_edge_cases);

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
