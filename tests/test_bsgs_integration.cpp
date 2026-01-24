/*
 * test_bsgs_integration.cpp - Integration tests for BSGS algorithm
 *
 * Tests the Baby Step Giant Step algorithm with known solutions
 * from the Bitcoin puzzle challenges.
 */

#include "test_framework.h"
#include "secp256k1/SECP256k1.h"
#include "secp256k1/Int.h"
#include "secp256k1/Point.h"
#include <cctype>   /* toupper */
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
 * Known Puzzle Solutions
 * These are solved puzzles from the Bitcoin Puzzle Challenge
 * https://privatekeys.pw/puzzles/bitcoin-puzzle-tx
 * ============================================================================ */

/* Puzzle #1: Private key = 1 */
static const char *PUZZLE_1_PRIVKEY = "0000000000000000000000000000000000000000000000000000000000000001";
static const char *PUZZLE_1_PUBKEY = "0279BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798";

/* Puzzle #2: Private key = 3 */
static const char *PUZZLE_2_PRIVKEY = "0000000000000000000000000000000000000000000000000000000000000003";
static const char *PUZZLE_2_PUBKEY = "02F9308A019258C31049344F85F89D5229B531C845836F99B08601F113BCE036F9";

/* Puzzle #3: Private key = 7 */
/* PUZZLE_3_PRIVKEY is not needed in tests - verification uses public key only */
static const char *PUZZLE_3_PUBKEY = "025CBDF0646E5DB4EAA398F365F2EA7A0E3D419B7E0330E39CE92BDDEDCAC4F9BC";

/* Puzzle #4: Private key = 8 (reserved for future tests) */
/* PUZZLE_4_PRIVKEY/PUBKEY not currently used but kept for reference */

/* Puzzle #5: Private key = 21 (0x15) */
static const char *PUZZLE_5_PRIVKEY = "0000000000000000000000000000000000000000000000000000000000000015";
static const char *PUZZLE_5_PUBKEY = "02352BBF4A4CDD12564F93FA332CE333301D9AD40271F8107181340AEF25BE59D5";

/* Test case 10: Private key = 593 (0x251)
 * Note: This is NOT puzzle #10 from the Bitcoin challenge, just a test key */
static const char *TEST_10_PRIVKEY = "0000000000000000000000000000000000000000000000000000000000000251";
static const char *TEST_10_PUBKEY = "0331CCDA339F29123A86C2995C6A9F49796D70A5955079B9616015F07BCDF8C39E";

/* Test case 15: Private key = 26867 (0x68F3)
 * Note: This is NOT puzzle #15 from the Bitcoin challenge, just a test key */
static const char *TEST_15_PRIVKEY = "00000000000000000000000000000000000000000000000000000000000068F3";
static const char *TEST_15_PUBKEY = "02FEA58FFCF49566F6E9E9350CF5BCA2861312F422966E8DB16094BEB14DC3DF2C";

/* ============================================================================
 * Key Generation Tests
 * ============================================================================ */

TEST(secp256k1_generate_pubkey_from_privkey_1) {
    setup_secp256k1();

    Int privKey;
    privKey.SetBase16(PUZZLE_1_PRIVKEY);

    Point pubKey = secp->ComputePublicKey(&privKey);

    /* Get compressed public key */
    char *pubKeyHex = secp->GetPublicKeyHex(true, pubKey);
    ASSERT_NOT_NULL(pubKeyHex);

    /* Compare (case-insensitive) */
    for (int i = 0; pubKeyHex[i]; i++) {
        pubKeyHex[i] = toupper(pubKeyHex[i]);
    }

    ASSERT_STR_EQ(PUZZLE_1_PUBKEY, pubKeyHex);
    free(pubKeyHex);
}

TEST(secp256k1_generate_pubkey_from_privkey_2) {
    setup_secp256k1();

    Int privKey;
    privKey.SetBase16(PUZZLE_2_PRIVKEY);

    Point pubKey = secp->ComputePublicKey(&privKey);

    char *pubKeyHex = secp->GetPublicKeyHex(true, pubKey);
    ASSERT_NOT_NULL(pubKeyHex);

    for (int i = 0; pubKeyHex[i]; i++) {
        pubKeyHex[i] = toupper(pubKeyHex[i]);
    }

    ASSERT_STR_EQ(PUZZLE_2_PUBKEY, pubKeyHex);
    free(pubKeyHex);
}

TEST(secp256k1_generate_pubkey_from_privkey_5) {
    setup_secp256k1();

    Int privKey;
    privKey.SetBase16(PUZZLE_5_PRIVKEY);

    Point pubKey = secp->ComputePublicKey(&privKey);

    char *pubKeyHex = secp->GetPublicKeyHex(true, pubKey);
    ASSERT_NOT_NULL(pubKeyHex);

    for (int i = 0; pubKeyHex[i]; i++) {
        pubKeyHex[i] = toupper(pubKeyHex[i]);
    }

    ASSERT_STR_EQ(PUZZLE_5_PUBKEY, pubKeyHex);
    free(pubKeyHex);
}

TEST(secp256k1_generate_pubkey_from_privkey_593) {
    setup_secp256k1();

    Int privKey;
    privKey.SetBase16(TEST_10_PRIVKEY);

    Point pubKey = secp->ComputePublicKey(&privKey);

    char *pubKeyHex = secp->GetPublicKeyHex(true, pubKey);
    ASSERT_NOT_NULL(pubKeyHex);

    for (int i = 0; pubKeyHex[i]; i++) {
        pubKeyHex[i] = toupper(pubKeyHex[i]);
    }

    ASSERT_STR_EQ(TEST_10_PUBKEY, pubKeyHex);
    free(pubKeyHex);
}

TEST(secp256k1_generate_pubkey_from_privkey_26867) {
    setup_secp256k1();

    Int privKey;
    privKey.SetBase16(TEST_15_PRIVKEY);

    Point pubKey = secp->ComputePublicKey(&privKey);

    char *pubKeyHex = secp->GetPublicKeyHex(true, pubKey);
    ASSERT_NOT_NULL(pubKeyHex);

    for (int i = 0; pubKeyHex[i]; i++) {
        pubKeyHex[i] = toupper(pubKeyHex[i]);
    }

    ASSERT_STR_EQ(TEST_15_PUBKEY, pubKeyHex);
    free(pubKeyHex);
}

/* ============================================================================
 * Public Key Parsing Tests
 * ============================================================================ */

TEST(secp256k1_parse_pubkey) {
    setup_secp256k1();

    Point pubKey;
    bool isCompressed = false;
    bool parsed = secp->ParsePublicKeyHex((char*)PUZZLE_1_PUBKEY, pubKey, isCompressed);
    ASSERT_TRUE(parsed);
    ASSERT_FALSE(pubKey.isZero());
}

TEST(secp256k1_parse_and_verify_pubkey) {
    setup_secp256k1();

    /* Parse the public key */
    Point pubKey;
    bool isCompressed = false;
    bool parsed = secp->ParsePublicKeyHex((char*)PUZZLE_5_PUBKEY, pubKey, isCompressed);
    ASSERT_TRUE(parsed);

    /* Generate from private key */
    Int privKey;
    privKey.SetBase16(PUZZLE_5_PRIVKEY);
    Point expected = secp->ComputePublicKey(&privKey);

    /* Compare X coordinates (sufficient for compressed) */
    ASSERT_TRUE(pubKey.x.IsEqual(&expected.x));
}

/* ============================================================================
 * Point Arithmetic Tests
 * ============================================================================ */

TEST(secp256k1_point_addition) {
    setup_secp256k1();

    /* G + 2G should equal 3G
     * Note: AddDirect is for adding DIFFERENT points.
     * For doubling (P + P), use DoubleDirect. */
    Point G = secp->G;

    Int two(2);
    Point twoG = secp->ComputePublicKey(&two);

    Point result = secp->AddDirect(G, twoG);

    Int three(3);
    Point expected = secp->ComputePublicKey(&three);

    ASSERT_TRUE(result.x.IsEqual(&expected.x));
    ASSERT_TRUE(result.y.IsEqual(&expected.y));
}

TEST(secp256k1_point_doubling) {
    setup_secp256k1();

    Point G = secp->G;
    Point doubled = secp->DoubleDirect(G);

    Int two(2);
    Point expected = secp->ComputePublicKey(&two);

    ASSERT_TRUE(doubled.x.IsEqual(&expected.x));
}

TEST(secp256k1_scalar_multiplication) {
    setup_secp256k1();

    /* 7 * G should give puzzle #3's public key */
    Int seven(7);
    Point result = secp->ComputePublicKey(&seven);

    Point expected;
    bool isCompressed = false;
    secp->ParsePublicKeyHex((char*)PUZZLE_3_PUBKEY, expected, isCompressed);

    ASSERT_TRUE(result.x.IsEqual(&expected.x));
}

/* ============================================================================
 * BSGS Algorithm Test (Mini Version)
 *
 * This is a simplified BSGS test that searches a small range.
 * The full BSGS implementation is in bsgs_ops.cpp.
 * ============================================================================ */

TEST(bsgs_mini_search) {
    setup_secp256k1();

    /*
     * Simple BSGS search in range [1, 100] for puzzle #3 (key = 7)
     *
     * BSGS Algorithm:
     * 1. Baby steps: Store G, 2G, 3G, ..., mG where m = sqrt(range)
     * 2. Giant steps: Check P - i*m*G for i = 0, 1, 2, ...
     * 3. If match found, key = j + i*m
     */

    Point targetPubKey;
    bool isCompressed = false;
    secp->ParsePublicKeyHex((char*)PUZZLE_3_PUBKEY, targetPubKey, isCompressed);

    (void)isCompressed;  /* Suppress unused warning */
    int m = 10;  /* sqrt(100) */

    /* Baby steps: compute and store j*G for j = 0..m-1 */
    Point *baby_steps = new Point[m];
    Point current;
    current.Clear();

    for (int j = 0; j < m; j++) {
        if (j == 0) {
            current.Clear();  /* Point at infinity */
        } else {
            Int jInt(j);
            current = secp->ComputePublicKey(&jInt);
        }
        baby_steps[j] = current;
    }

    /* Precompute m*G */
    Int mInt(m);
    Point mG = secp->ComputePublicKey(&mInt);

    /* Giant steps: check P - i*m*G */
    bool found = false;
    int found_key = 0;

    Point P = targetPubKey;
    for (int i = 0; i < m && !found; i++) {
        /* Check if P matches any baby step */
        for (int j = 0; j < m && !found; j++) {
            if (j == 0) {
                /* baby_steps[0] is point at infinity, P would need to be zero */
                if (P.isZero()) {
                    found_key = i * m + j;
                    found = true;
                }
            } else {
                if (P.x.IsEqual(&baby_steps[j].x)) {
                    /* Found! key = j + i*m */
                    found_key = j + i * m;
                    found = true;
                }
            }
        }

        if (!found) {
            /* P = P - m*G */
            Point negMG = secp->Negation(mG);
            P = secp->AddDirect(P, negMG);
        }
    }

    delete[] baby_steps;

    ASSERT_TRUE(found);
    ASSERT_EQ(7, found_key);
}

/* ============================================================================
 * Hash160 Tests (RIPEMD160(SHA256(pubkey)))
 * ============================================================================ */

TEST(secp256k1_get_hash160) {
    setup_secp256k1();

    Int privKey;
    privKey.SetBase16(PUZZLE_1_PRIVKEY);

    Point pubKey = secp->ComputePublicKey(&privKey);

    /* Get Hash160 of compressed public key */
    unsigned char hash160[20];
    secp->GetHash160(P2PKH, true, pubKey, hash160);

    /* Verify it's not all zeros */
    bool all_zero = true;
    for (int i = 0; i < 20; i++) {
        if (hash160[i] != 0) {
            all_zero = false;
            break;
        }
    }
    ASSERT_FALSE(all_zero);
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

/* Exported function for test runner */
int run_bsgs_tests(void) {
    TEST_INIT();

    TEST_SECTION("Public Key Generation");
    RUN_TEST(secp256k1_generate_pubkey_from_privkey_1);
    RUN_TEST(secp256k1_generate_pubkey_from_privkey_2);
    RUN_TEST(secp256k1_generate_pubkey_from_privkey_5);
    RUN_TEST(secp256k1_generate_pubkey_from_privkey_593);
    RUN_TEST(secp256k1_generate_pubkey_from_privkey_26867);

    TEST_SECTION("Public Key Parsing");
    RUN_TEST(secp256k1_parse_pubkey);
    RUN_TEST(secp256k1_parse_and_verify_pubkey);

    TEST_SECTION("Point Arithmetic");
    RUN_TEST(secp256k1_point_addition);
    RUN_TEST(secp256k1_point_doubling);
    RUN_TEST(secp256k1_scalar_multiplication);

    TEST_SECTION("Mini BSGS");
    RUN_TEST(bsgs_mini_search);

    TEST_SECTION("Hash160");
    RUN_TEST(secp256k1_get_hash160);

    cleanup_secp256k1();

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_bsgs_tests();
}
#endif
