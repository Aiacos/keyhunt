/*
 * test_bloom.cpp - Unit tests for the Bloom filter implementation
 *
 * Tests:
 * - Initialization
 * - Insert and check operations
 * - False positive rate verification
 * - Edge cases
 */

#include "test_framework.h"

extern "C" {
#include "bloom/bloom.h"
#include "bloom/bloom_simd.h"
}

#include <time.h>

/* ============================================================================
 * Initialization Tests
 * ============================================================================ */

TEST(bloom_init_basic) {
    struct bloom bf;
    int result = bloom_init2(&bf, 1000, 0.01);
    ASSERT_EQ(0, result);
    ASSERT_EQ(1000ULL, bf.entries);
    ASSERT_TRUE(bf.ready == 1);
    bloom_free(&bf);
}

TEST(bloom_init_different_sizes) {
    struct bloom bf;

    /* Small filter */
    ASSERT_EQ(0, bloom_init2(&bf, 100, 0.01));
    ASSERT_TRUE(bf.bytes > 0);
    bloom_free(&bf);

    /* Large filter */
    ASSERT_EQ(0, bloom_init2(&bf, 1000000, 0.001));
    ASSERT_TRUE(bf.bytes > 0);
    bloom_free(&bf);
}

TEST(bloom_init_invalid_entries) {
    struct bloom bf;
    /* Zero entries should fail */
    int result = bloom_init2(&bf, 0, 0.01);
    ASSERT_EQ(1, result);
}

TEST(bloom_init_invalid_error_rate) {
    struct bloom bf;

    /* Error rate of 0 should fail */
    int result = bloom_init2(&bf, 1000, 0);
    ASSERT_EQ(1, result);

    /* Error rate >= 1 should fail */
    result = bloom_init2(&bf, 1000, 1.0);
    ASSERT_EQ(1, result);

    /* Negative error rate should fail */
    result = bloom_init2(&bf, 1000, -0.01);
    ASSERT_EQ(1, result);
}

/* ============================================================================
 * Insert and Check Tests
 * ============================================================================ */

TEST(bloom_add_check_basic) {
    struct bloom bf;
    bloom_init2(&bf, 1000, 0.01);

    const char *key = "test_key_123";

    /* Initially should not be present */
    ASSERT_EQ(0, bloom_check(&bf, key, strlen(key)));

    /* Add the key */
    bloom_add(&bf, key, strlen(key));

    /* Now should be present */
    ASSERT_EQ(1, bloom_check(&bf, key, strlen(key)));

    bloom_free(&bf);
}

TEST(bloom_add_multiple) {
    struct bloom bf;
    bloom_init2(&bf, 1000, 0.01);

    const char *keys[] = {
        "key1", "key2", "key3", "key4", "key5",
        "alpha", "beta", "gamma", "delta", "epsilon"
    };
    int num_keys = sizeof(keys) / sizeof(keys[0]);

    /* Add all keys */
    for (int i = 0; i < num_keys; i++) {
        bloom_add(&bf, keys[i], strlen(keys[i]));
    }

    /* All keys should be found */
    for (int i = 0; i < num_keys; i++) {
        ASSERT_EQ(1, bloom_check(&bf, keys[i], strlen(keys[i])));
    }

    bloom_free(&bf);
}

TEST(bloom_check_not_present) {
    struct bloom bf;
    bloom_init2(&bf, 1000, 0.01);

    /* Add some keys */
    bloom_add(&bf, "key1", 4);
    bloom_add(&bf, "key2", 4);
    bloom_add(&bf, "key3", 4);

    /* Keys not added should (usually) not be found */
    /* Note: There's a small chance of false positives, but with
       large enough filter and different enough keys, it's unlikely */
    const char *not_added = "completely_different_key_xyz_123456";
    int found = bloom_check(&bf, not_added, strlen(not_added));
    /* We can't assert it's 0 due to false positives, but log it */
    if (found) {
        printf("    (Note: False positive detected, this is expected occasionally)\n");
    }

    bloom_free(&bf);
}

TEST(bloom_binary_data) {
    struct bloom bf;
    bloom_init2(&bf, 1000, 0.01);

    /* Test with binary data (20-byte RIPEMD160 hash) */
    unsigned char hash1[20] = {0x01, 0x02, 0x03, 0x04, 0x05,
                                0x06, 0x07, 0x08, 0x09, 0x0A,
                                0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                                0x10, 0x11, 0x12, 0x13, 0x14};
    unsigned char hash2[20] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB,
                                0xFA, 0xF9, 0xF8, 0xF7, 0xF6,
                                0xF5, 0xF4, 0xF3, 0xF2, 0xF1,
                                0xF0, 0xEF, 0xEE, 0xED, 0xEC};

    bloom_add(&bf, hash1, 20);

    ASSERT_EQ(1, bloom_check(&bf, hash1, 20));
    /* hash2 should likely not be found (use it to suppress unused warning) */
    /* Not asserting because of false positive possibility */
    (void)bloom_check(&bf, hash2, 20);

    bloom_free(&bf);
}

/* ============================================================================
 * False Positive Rate Tests
 * ============================================================================ */

TEST(bloom_false_positive_rate) {
    /* Test that actual false positive rate is close to expected */
    const int num_entries = 10000;
    const double expected_fp_rate = 0.01;  /* 1% */
    const int num_tests = 10000;

    struct bloom bf;
    bloom_init2(&bf, num_entries, expected_fp_rate);

    /* Add entries */
    for (int i = 0; i < num_entries; i++) {
        char key[32];
        snprintf(key, sizeof(key), "inserted_key_%d", i);
        bloom_add(&bf, key, strlen(key));
    }

    /* Test with keys that were NOT inserted */
    int false_positives = 0;
    for (int i = 0; i < num_tests; i++) {
        char key[32];
        snprintf(key, sizeof(key), "not_inserted_%d", i + num_entries + 1000);
        if (bloom_check(&bf, key, strlen(key)) == 1) {
            false_positives++;
        }
    }

    double actual_fp_rate = (double)false_positives / num_tests;

    /* Allow up to 3x the expected rate due to statistical variance */
    double max_allowed_rate = expected_fp_rate * 3.0;

    printf("\n    (FP rate: %.4f, expected: %.4f, max: %.4f)\n",
           actual_fp_rate, expected_fp_rate, max_allowed_rate);

    ASSERT_TRUE(actual_fp_rate <= max_allowed_rate);

    bloom_free(&bf);
}

/* ============================================================================
 * Reset Test
 * ============================================================================ */

TEST(bloom_reset) {
    struct bloom bf;
    bloom_init2(&bf, 1000, 0.01);

    /* Add some keys */
    bloom_add(&bf, "key1", 4);
    bloom_add(&bf, "key2", 4);

    ASSERT_EQ(1, bloom_check(&bf, "key1", 4));

    /* Reset */
    bloom_reset(&bf);

    /* After reset, keys should not be found */
    ASSERT_EQ(0, bloom_check(&bf, "key1", 4));
    ASSERT_EQ(0, bloom_check(&bf, "key2", 4));

    bloom_free(&bf);
}

/* ============================================================================
 * Uninitialized Check Test
 * ============================================================================ */

TEST(bloom_check_uninitialized) {
    struct bloom bf;
    memset(&bf, 0, sizeof(bf));  /* Not initialized */

    /* Should return -1 for uninitialized bloom filter */
    int result = bloom_check(&bf, "test", 4);
    ASSERT_EQ(-1, result);
}

/* ============================================================================
 * Memory Pool Initialization Test
 * ============================================================================ */

TEST(bloom_init_with_pool) {
    struct bloom bf;

    /* Test without pool (NULL) - should work like bloom_init2 */
    int result = bloom_init_with_pool(&bf, 1000, 0.01, NULL);
    ASSERT_EQ(0, result);
    ASSERT_TRUE(bf.ready == 1);

    bloom_add(&bf, "test", 4);
    ASSERT_EQ(1, bloom_check(&bf, "test", 4));

    bloom_free(&bf);
}

/* ============================================================================
 * Stress Test
 * ============================================================================ */

TEST(bloom_stress_many_insertions) {
    struct bloom bf;
    bloom_init2(&bf, 100000, 0.001);

    /* Insert many entries */
    for (int i = 0; i < 50000; i++) {
        char key[32];
        snprintf(key, sizeof(key), "stress_key_%d", i);
        bloom_add(&bf, key, strlen(key));
    }

    /* Verify some of them */
    int found = 0;
    for (int i = 0; i < 1000; i++) {
        char key[32];
        snprintf(key, sizeof(key), "stress_key_%d", i);
        if (bloom_check(&bf, key, strlen(key)) == 1) {
            found++;
        }
    }

    /* All 1000 checked keys should be found (no false negatives) */
    ASSERT_EQ(1000, found);

    bloom_free(&bf);
}

/* ============================================================================
 * Edge Cases
 * ============================================================================ */

TEST(bloom_empty_key) {
    struct bloom bf;
    bloom_init2(&bf, 1000, 0.01);

    /* Empty key (length 0) */
    bloom_add(&bf, "", 0);
    ASSERT_EQ(1, bloom_check(&bf, "", 0));

    bloom_free(&bf);
}

TEST(bloom_single_byte_key) {
    struct bloom bf;
    bloom_init2(&bf, 1000, 0.01);

    char key = 'X';
    bloom_add(&bf, &key, 1);
    ASSERT_EQ(1, bloom_check(&bf, &key, 1));

    bloom_free(&bf);
}

TEST(bloom_duplicate_add) {
    struct bloom bf;
    bloom_init2(&bf, 1000, 0.01);

    /* Adding the same key multiple times should be fine */
    const char *key = "duplicate_key";
    bloom_add(&bf, key, strlen(key));
    bloom_add(&bf, key, strlen(key));
    bloom_add(&bf, key, strlen(key));

    ASSERT_EQ(1, bloom_check(&bf, key, strlen(key)));

    bloom_free(&bf);
}

/* ============================================================================
 * SIMD Batch Check Tests
 * ============================================================================ */

TEST(bloom_simd_batch_various_sizes) {
    struct bloom_simd bf;
    bloom_simd_init(&bf, 10000, 0.01);

    /* Prepare test data: 20-byte hashes */
    const int max_test_items = 1024;
    uint8_t test_hashes[max_test_items][20];
    const uint8_t *hash_ptrs[max_test_items];
    uint8_t results[max_test_items];

    /* Generate unique test hashes */
    for (int i = 0; i < max_test_items; i++) {
        for (int j = 0; j < 20; j++) {
            test_hashes[i][j] = (uint8_t)((i * 37 + j * 17) & 0xFF);
        }
        hash_ptrs[i] = test_hashes[i];
    }

    /* Add first half to bloom filter */
    const int num_inserted = max_test_items / 2;
    for (int i = 0; i < num_inserted; i++) {
        bloom_simd_add(&bf, test_hashes[i], 20);
    }

    /* Test various batch sizes */
    int test_sizes[] = {1, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
    int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

    for (int s = 0; s < num_sizes; s++) {
        int batch_size = test_sizes[s];
        if (batch_size > max_test_items) continue;

        /* Clear results */
        memset(results, 0, sizeof(results));

        /* Check batch */
        bloom_simd_check_rmd160_batch(&bf, hash_ptrs, batch_size, results);

        /* Verify results for inserted items (first half) */
        int correct_positives = 0;
        for (int i = 0; i < batch_size && i < num_inserted; i++) {
            if (results[i] == 1) {
                correct_positives++;
            }
        }

        /* All inserted items should be found (no false negatives) */
        int expected_positives = (batch_size < num_inserted) ? batch_size : num_inserted;
        ASSERT_EQ(expected_positives, correct_positives);

        /* Note: We don't check false positive rate here as it varies with batch size
         * and hash generation. The bloom_simd_batch_correctness test validates FP rate. */
    }

    bloom_simd_free(&bf);
}

TEST(bloom_simd_batch_edge_cases) {
    struct bloom_simd bf;
    bloom_simd_init(&bf, 1000, 0.01);

    /* Test single item batch */
    uint8_t hash1[20] = {0x01, 0x02, 0x03, 0x04, 0x05,
                         0x06, 0x07, 0x08, 0x09, 0x0A,
                         0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                         0x10, 0x11, 0x12, 0x13, 0x14};
    const uint8_t *hash_ptr = hash1;
    uint8_t result;

    bloom_simd_add(&bf, hash1, 20);
    bloom_simd_check_rmd160_batch(&bf, &hash_ptr, 1, &result);
    ASSERT_EQ(1, result);

    /* Test max batch size (BLOOM_BATCH_MAX = 1024) */
    const int max_batch = 1024;
    uint8_t hashes[max_batch][20];
    const uint8_t *hash_ptrs[max_batch];
    uint8_t results[max_batch];

    for (int i = 0; i < max_batch; i++) {
        for (int j = 0; j < 20; j++) {
            hashes[i][j] = (uint8_t)((i * 3 + j) & 0xFF);
        }
        hash_ptrs[i] = hashes[i];
        bloom_simd_add(&bf, hashes[i], 20);
    }

    memset(results, 0, sizeof(results));
    bloom_simd_check_rmd160_batch(&bf, hash_ptrs, max_batch, results);

    /* All items should be found */
    int found = 0;
    for (int i = 0; i < max_batch; i++) {
        if (results[i] == 1) {
            found++;
        }
    }
    ASSERT_EQ(max_batch, found);

    bloom_simd_free(&bf);
}

TEST(bloom_simd_batch_correctness) {
    struct bloom_simd bf;
    bloom_simd_init(&bf, 5000, 0.01);

    /* Create distinct sets of inserted and not-inserted hashes */
    const int batch_size = 100;
    uint8_t inserted_hashes[batch_size][20];
    uint8_t not_inserted_hashes[batch_size][20];
    const uint8_t *hash_ptrs[batch_size];
    uint8_t results[batch_size];

    /* Generate and insert first set */
    for (int i = 0; i < batch_size; i++) {
        for (int j = 0; j < 20; j++) {
            inserted_hashes[i][j] = (uint8_t)((i * 5 + j * 7) & 0xFF);
        }
        bloom_simd_add(&bf, inserted_hashes[i], 20);
    }

    /* Generate second set (NOT inserted) */
    for (int i = 0; i < batch_size; i++) {
        for (int j = 0; j < 20; j++) {
            not_inserted_hashes[i][j] = (uint8_t)((i * 11 + j * 13 + 128) & 0xFF);
        }
    }

    /* Batch check inserted hashes - all should be found */
    for (int i = 0; i < batch_size; i++) {
        hash_ptrs[i] = inserted_hashes[i];
    }
    memset(results, 0, sizeof(results));
    bloom_simd_check_rmd160_batch(&bf, hash_ptrs, batch_size, results);

    int found = 0;
    for (int i = 0; i < batch_size; i++) {
        if (results[i] == 1) {
            found++;
        }
    }
    /* No false negatives allowed */
    ASSERT_EQ(batch_size, found);

    /* Batch check not-inserted hashes - some may be false positives */
    for (int i = 0; i < batch_size; i++) {
        hash_ptrs[i] = not_inserted_hashes[i];
    }
    memset(results, 0, sizeof(results));
    bloom_simd_check_rmd160_batch(&bf, hash_ptrs, batch_size, results);

    int false_positives = 0;
    for (int i = 0; i < batch_size; i++) {
        if (results[i] == 1) {
            false_positives++;
        }
    }

    /* False positive rate should be within reasonable bounds */
    double fp_rate = (double)false_positives / batch_size;
    printf("\n    (Batch FP rate: %.4f, target: 0.01, max: 0.05)\n", fp_rate);
    ASSERT_TRUE(fp_rate <= 0.05);

    bloom_simd_free(&bf);
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

/* Exported function for test runner */
int run_bloom_tests(void) {
    TEST_INIT();

    TEST_SECTION("Initialization");
    RUN_TEST(bloom_init_basic);
    RUN_TEST(bloom_init_different_sizes);
    RUN_TEST(bloom_init_invalid_entries);
    RUN_TEST(bloom_init_invalid_error_rate);

    TEST_SECTION("Insert and Check");
    RUN_TEST(bloom_add_check_basic);
    RUN_TEST(bloom_add_multiple);
    RUN_TEST(bloom_check_not_present);
    RUN_TEST(bloom_binary_data);

    TEST_SECTION("False Positive Rate");
    RUN_TEST(bloom_false_positive_rate);

    TEST_SECTION("Reset");
    RUN_TEST(bloom_reset);

    TEST_SECTION("Error Handling");
    RUN_TEST(bloom_check_uninitialized);

    TEST_SECTION("Memory Pool");
    RUN_TEST(bloom_init_with_pool);

    TEST_SECTION("Stress Test");
    RUN_TEST(bloom_stress_many_insertions);

    TEST_SECTION("Edge Cases");
    RUN_TEST(bloom_empty_key);
    RUN_TEST(bloom_single_byte_key);
    RUN_TEST(bloom_duplicate_add);

    TEST_SECTION("SIMD Batch Operations");
    RUN_TEST(bloom_simd_batch_various_sizes);
    RUN_TEST(bloom_simd_batch_edge_cases);
    RUN_TEST(bloom_simd_batch_correctness);

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_bloom_tests();
}
#endif
