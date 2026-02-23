/*
 * test_bsgs_ops.cpp - Unit tests for the BSGS Operations module
 *
 * Tests batch operations for Baby Step Giant Step algorithm:
 * - Batch context initialization and cleanup
 * - Memory allocation and alignment
 * - Bloom filter integration
 * - Batch point computation
 * - SIMD capability detection
 */

#include "test_framework.h"
#include "bsgs/bsgs_ops.h"
#include "bsgs/bsgs_fast.h"
#include "bloom/bloom.h"
#include <cstdlib>
#include <cstring>

/* ============================================================================
 * Batch Context Initialization Tests
 * ============================================================================ */

TEST(bsgs_batch_init) {
    bsgs_batch_ctx_t ctx;
    int result = bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    ASSERT_EQ(0, result);
    ASSERT_TRUE(ctx.initialized);
    ASSERT_EQ(BSGS_BATCH_SIZE, ctx.batch_size);
    ASSERT_EQ(BSGS_HALF_BATCH, ctx.half_batch);
    ASSERT_NOT_NULL(ctx.bloom_results);
    ASSERT_NOT_NULL(ctx.xpoint_raw);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_init_custom_size) {
    bsgs_batch_ctx_t ctx;
    int batch_size = 512;
    int result = bsgs_batch_init(&ctx, batch_size);

    ASSERT_EQ(0, result);
    ASSERT_TRUE(ctx.initialized);
    ASSERT_EQ(batch_size, ctx.batch_size);
    ASSERT_EQ(batch_size / 2, ctx.half_batch);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_init_null_context) {
    int result = bsgs_batch_init(NULL, BSGS_BATCH_SIZE);
    ASSERT_EQ(-1, result);
}

TEST(bsgs_batch_init_invalid_size) {
    bsgs_batch_ctx_t ctx;
    /* Batch size must be >= 64 */
    int result = bsgs_batch_init(&ctx, 32);
    ASSERT_EQ(-1, result);
}

TEST(bsgs_batch_init_minimum_size) {
    bsgs_batch_ctx_t ctx;
    /* Minimum valid batch size is 64 */
    int result = bsgs_batch_init(&ctx, 64);

    ASSERT_EQ(0, result);
    ASSERT_TRUE(ctx.initialized);
    ASSERT_EQ(64, ctx.batch_size);
    ASSERT_EQ(32, ctx.half_batch);

    bsgs_batch_free(&ctx);
}

/* ============================================================================
 * Batch Context Cleanup Tests
 * ============================================================================ */

TEST(bsgs_batch_free) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    bsgs_batch_free(&ctx);

    /* After free, all pointers should be NULL and initialized should be false */
    ASSERT_FALSE(ctx.initialized);
    ASSERT_NULL(ctx.bloom_results);
    ASSERT_NULL(ctx.xpoint_raw);
}

TEST(bsgs_batch_free_null) {
    /* Should not crash with NULL context */
    bsgs_batch_free(NULL);
    ASSERT_TRUE(1);  /* If we get here, it didn't crash */
}

TEST(bsgs_batch_free_uninitialized) {
    bsgs_batch_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* Should not crash with uninitialized context */
    bsgs_batch_free(&ctx);
    ASSERT_TRUE(1);
}

TEST(bsgs_batch_double_free) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    bsgs_batch_free(&ctx);
    /* Double free should be safe */
    bsgs_batch_free(&ctx);

    ASSERT_TRUE(1);  /* If we get here, it didn't crash */
}

/* ============================================================================
 * Memory Allocation Tests
 * ============================================================================ */

TEST(bsgs_batch_memory_alignment) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    /* Check that allocations are cache-line aligned (64 bytes) */
    uintptr_t bloom_addr = (uintptr_t)ctx.bloom_results;
    uintptr_t xpoint_addr = (uintptr_t)ctx.xpoint_raw;

    ASSERT_EQ(0, bloom_addr % CACHE_LINE_SIZE);
    ASSERT_EQ(0, xpoint_addr % CACHE_LINE_SIZE);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_memory_size) {
    bsgs_batch_ctx_t ctx;
    int batch_size = 256;
    bsgs_batch_init(&ctx, batch_size);

    /* Verify batch size is stored correctly */
    ASSERT_EQ(batch_size, ctx.batch_size);
    ASSERT_EQ(batch_size / 2, ctx.half_batch);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_multiple_contexts) {
    bsgs_batch_ctx_t ctx1, ctx2, ctx3;

    /* Initialize multiple contexts */
    ASSERT_EQ(0, bsgs_batch_init(&ctx1, 128));
    ASSERT_EQ(0, bsgs_batch_init(&ctx2, 256));
    ASSERT_EQ(0, bsgs_batch_init(&ctx3, 512));

    /* All should be properly initialized */
    ASSERT_TRUE(ctx1.initialized);
    ASSERT_TRUE(ctx2.initialized);
    ASSERT_TRUE(ctx3.initialized);

    /* Each should have correct sizes */
    ASSERT_EQ(128, ctx1.batch_size);
    ASSERT_EQ(256, ctx2.batch_size);
    ASSERT_EQ(512, ctx3.batch_size);

    /* Clean up */
    bsgs_batch_free(&ctx1);
    bsgs_batch_free(&ctx2);
    bsgs_batch_free(&ctx3);
}

/* ============================================================================
 * Bloom Filter Integration Tests
 * ============================================================================ */

TEST(bsgs_batch_bloom_check_null_context) {
    struct bloom bloom_array[256];
    int result = bsgs_batch_bloom_check(NULL, bloom_array, 100);
    ASSERT_EQ(0, result);
}

TEST(bsgs_batch_bloom_check_uninitialized) {
    bsgs_batch_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    struct bloom bloom_array[256];

    int result = bsgs_batch_bloom_check(&ctx, bloom_array, 100);
    ASSERT_EQ(0, result);
}

TEST(bsgs_batch_bloom_check_null_bloom) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    int result = bsgs_batch_bloom_check(&ctx, NULL, 100);
    ASSERT_EQ(0, result);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_bloom_check_empty) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    /* Initialize bloom filters */
    struct bloom bloom_array[256];
    for (int i = 0; i < 256; i++) {
        bloom_init2(&bloom_array[i], 1000000, 0.000001);
    }

    /* Clear xpoint data */
    memset(ctx.xpoint_raw, 0, ctx.batch_size * 32);

    /* Check with empty bloom filters - should find no hits */
    int hits = bsgs_batch_bloom_check(&ctx, bloom_array, 10);
    ASSERT_EQ(0, hits);

    /* Clean up */
    for (int i = 0; i < 256; i++) {
        bloom_free(&bloom_array[i]);
    }
    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_bloom_check_with_hits) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    /* Initialize bloom filters */
    struct bloom bloom_array[256];
    for (int i = 0; i < 256; i++) {
        bloom_init2(&bloom_array[i], 1000000, 0.000001);
    }

    /* Add some test data to xpoint_raw and corresponding bloom filter */
    int num_test_points = 5;
    for (int i = 0; i < num_test_points; i++) {
        uint8_t *xpoint = ctx.xpoint_raw + i * 32;
        /* Create test xpoint data */
        xpoint[0] = i;  /* Bloom filter index */
        for (int j = 1; j < 32; j++) {
            xpoint[j] = (i * 10 + j) & 0xFF;
        }
        /* Add to corresponding bloom filter */
        bloom_add(&bloom_array[xpoint[0]], (char*)xpoint, 32);
    }

    /* Check - should find all 5 hits */
    int hits = bsgs_batch_bloom_check(&ctx, bloom_array, num_test_points);
    ASSERT_EQ(num_test_points, hits);

    /* Verify bloom_results array is set correctly */
    for (int i = 0; i < num_test_points; i++) {
        ASSERT_EQ(1, ctx.bloom_results[i]);
    }

    /* Clean up */
    for (int i = 0; i < 256; i++) {
        bloom_free(&bloom_array[i]);
    }
    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_bloom_check_zero_points) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    struct bloom bloom_array[256];
    for (int i = 0; i < 256; i++) {
        bloom_init2(&bloom_array[i], 1000000, 0.000001);
    }

    /* Check with 0 points */
    int hits = bsgs_batch_bloom_check(&ctx, bloom_array, 0);
    ASSERT_EQ(0, hits);

    for (int i = 0; i < 256; i++) {
        bloom_free(&bloom_array[i]);
    }
    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_bloom_check_partial_matches) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    /* Initialize bloom filters */
    struct bloom bloom_array[256];
    for (int i = 0; i < 256; i++) {
        bloom_init2(&bloom_array[i], 1000000, 0.000001);
    }

    /* Add 10 test points, but only add 5 to bloom filter */
    int num_test_points = 10;
    int num_added = 5;

    for (int i = 0; i < num_test_points; i++) {
        uint8_t *xpoint = ctx.xpoint_raw + i * 32;
        xpoint[0] = i;
        for (int j = 1; j < 32; j++) {
            xpoint[j] = (i * 10 + j) & 0xFF;
        }
        /* Only add first 5 to bloom filter */
        if (i < num_added) {
            bloom_add(&bloom_array[xpoint[0]], (char*)xpoint, 32);
        }
    }

    /* Check - should find 5 hits */
    int hits = bsgs_batch_bloom_check(&ctx, bloom_array, num_test_points);
    ASSERT_EQ(num_added, hits);

    /* Verify bloom_results array */
    for (int i = 0; i < num_added; i++) {
        ASSERT_EQ(1, ctx.bloom_results[i]);
    }
    for (int i = num_added; i < num_test_points; i++) {
        ASSERT_EQ(0, ctx.bloom_results[i]);
    }

    /* Clean up */
    for (int i = 0; i < 256; i++) {
        bloom_free(&bloom_array[i]);
    }
    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_bloom_check_all_filters) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    /* Initialize bloom filters */
    struct bloom bloom_array[256];
    for (int i = 0; i < 256; i++) {
        bloom_init2(&bloom_array[i], 1000000, 0.000001);
    }

    /* Add points distributed across all 256 bloom filters */
    int num_test_points = 256;
    for (int i = 0; i < num_test_points; i++) {
        uint8_t *xpoint = ctx.xpoint_raw + i * 32;
        xpoint[0] = i;  /* Use i as first byte to distribute across all filters */
        for (int j = 1; j < 32; j++) {
            xpoint[j] = (i + j) & 0xFF;
        }
        /* Add to corresponding bloom filter */
        bloom_add(&bloom_array[xpoint[0]], (char*)xpoint, 32);
    }

    /* Check - should find all 256 hits */
    int hits = bsgs_batch_bloom_check(&ctx, bloom_array, num_test_points);
    ASSERT_EQ(num_test_points, hits);

    /* Clean up */
    for (int i = 0; i < 256; i++) {
        bloom_free(&bloom_array[i]);
    }
    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_bloom_check_different_batch_sizes) {
    int batch_sizes[] = {64, 128, 256, 512};

    for (int b = 0; b < 4; b++) {
        bsgs_batch_ctx_t ctx;
        bsgs_batch_init(&ctx, batch_sizes[b]);

        struct bloom bloom_array[256];
        for (int i = 0; i < 256; i++) {
            bloom_init2(&bloom_array[i], 1000000, 0.000001);
        }

        /* Add a few test points */
        int num_test_points = 10;
        for (int i = 0; i < num_test_points; i++) {
            uint8_t *xpoint = ctx.xpoint_raw + i * 32;
            xpoint[0] = i;
            for (int j = 1; j < 32; j++) {
                xpoint[j] = (i * 10 + j) & 0xFF;
            }
            bloom_add(&bloom_array[xpoint[0]], (char*)xpoint, 32);
        }

        /* Check */
        int hits = bsgs_batch_bloom_check(&ctx, bloom_array, num_test_points);
        ASSERT_EQ(num_test_points, hits);

        /* Clean up */
        for (int i = 0; i < 256; i++) {
            bloom_free(&bloom_array[i]);
        }
        bsgs_batch_free(&ctx);
    }
}

TEST(bsgs_batch_bloom_check_sequential_calls) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    struct bloom bloom_array[256];
    for (int i = 0; i < 256; i++) {
        bloom_init2(&bloom_array[i], 1000000, 0.000001);
    }

    /* First batch of points */
    for (int i = 0; i < 5; i++) {
        uint8_t *xpoint = ctx.xpoint_raw + i * 32;
        xpoint[0] = i;
        for (int j = 1; j < 32; j++) {
            xpoint[j] = (i * 10 + j) & 0xFF;
        }
        bloom_add(&bloom_array[xpoint[0]], (char*)xpoint, 32);
    }

    int hits1 = bsgs_batch_bloom_check(&ctx, bloom_array, 5);
    ASSERT_EQ(5, hits1);

    /* Second batch with different data */
    for (int i = 0; i < 3; i++) {
        uint8_t *xpoint = ctx.xpoint_raw + i * 32;
        xpoint[0] = i + 10;
        for (int j = 1; j < 32; j++) {
            xpoint[j] = ((i + 10) * 10 + j) & 0xFF;
        }
        bloom_add(&bloom_array[xpoint[0]], (char*)xpoint, 32);
    }

    int hits2 = bsgs_batch_bloom_check(&ctx, bloom_array, 3);
    ASSERT_EQ(3, hits2);

    /* Clean up */
    for (int i = 0; i < 256; i++) {
        bloom_free(&bloom_array[i]);
    }
    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_bloom_check_no_matches) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    /* Initialize bloom filters */
    struct bloom bloom_array[256];
    for (int i = 0; i < 256; i++) {
        bloom_init2(&bloom_array[i], 1000000, 0.000001);
    }

    /* Add some data to bloom filters */
    for (int i = 0; i < 10; i++) {
        unsigned char data[32];
        data[0] = i;
        for (int j = 1; j < 32; j++) {
            data[j] = (i + j) & 0xFF;
        }
        bloom_add(&bloom_array[data[0]], (char*)data, 32);
    }

    /* Add different data to xpoint_raw that won't match */
    int num_test_points = 10;
    for (int i = 0; i < num_test_points; i++) {
        uint8_t *xpoint = ctx.xpoint_raw + i * 32;
        xpoint[0] = i + 100;  /* Different first byte */
        for (int j = 1; j < 32; j++) {
            xpoint[j] = ((i + 100) * 20 + j) & 0xFF;
        }
    }

    /* Check - should find 0 hits (or very few due to false positives) */
    int hits = bsgs_batch_bloom_check(&ctx, bloom_array, num_test_points);

    /* Allow for some false positives but should be very rare */
    ASSERT_TRUE(hits <= 2);

    /* Clean up */
    for (int i = 0; i < 256; i++) {
        bloom_free(&bloom_array[i]);
    }
    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_bloom_check_single_point) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    struct bloom bloom_array[256];
    for (int i = 0; i < 256; i++) {
        bloom_init2(&bloom_array[i], 1000000, 0.000001);
    }

    /* Add single test point */
    uint8_t *xpoint = ctx.xpoint_raw;
    xpoint[0] = 42;
    for (int j = 1; j < 32; j++) {
        xpoint[j] = j & 0xFF;
    }
    bloom_add(&bloom_array[xpoint[0]], (char*)xpoint, 32);

    /* Check with 1 point */
    int hits = bsgs_batch_bloom_check(&ctx, bloom_array, 1);
    ASSERT_EQ(1, hits);
    ASSERT_EQ(1, ctx.bloom_results[0]);

    /* Clean up */
    for (int i = 0; i < 256; i++) {
        bloom_free(&bloom_array[i]);
    }
    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_bloom_check_results_array_cleared) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    struct bloom bloom_array[256];
    for (int i = 0; i < 256; i++) {
        bloom_init2(&bloom_array[i], 1000000, 0.000001);
    }

    /* Set bloom_results to all 1s initially */
    memset(ctx.bloom_results, 1, ctx.batch_size);

    /* Add no points to bloom filter, add some to xpoint_raw */
    int num_test_points = 10;
    for (int i = 0; i < num_test_points; i++) {
        uint8_t *xpoint = ctx.xpoint_raw + i * 32;
        xpoint[0] = i;
        for (int j = 1; j < 32; j++) {
            xpoint[j] = (i * 10 + j) & 0xFF;
        }
        /* Don't add to bloom filter */
    }

    /* Check - should find 0 hits */
    int hits = bsgs_batch_bloom_check(&ctx, bloom_array, num_test_points);
    ASSERT_EQ(0, hits);

    /* bloom_results should be cleared (0) for all checked points */
    for (int i = 0; i < num_test_points; i++) {
        ASSERT_EQ(0, ctx.bloom_results[i]);
    }

    /* Clean up */
    for (int i = 0; i < 256; i++) {
        bloom_free(&bloom_array[i]);
    }
    bsgs_batch_free(&ctx);
}

/* ============================================================================
 * X-Point Extraction Tests
 * ============================================================================ */

TEST(bsgs_batch_extract_xpoints_basic) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    /* This function is a stub in the current implementation */
    /* It should not crash when called */
    bsgs_batch_extract_xpoints(&ctx, 100);

    ASSERT_TRUE(1);  /* If we get here, it didn't crash */

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_extract_xpoints_zero) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    bsgs_batch_extract_xpoints(&ctx, 0);

    ASSERT_TRUE(1);

    bsgs_batch_free(&ctx);
}

/* ============================================================================
 * SIMD Capability Tests
 * ============================================================================ */

TEST(bsgs_ops_simd_available) {
    int simd = bsgs_fast_simd_available();

    /* Return value should be 0 (no SIMD), 1 (AVX2), or 2 (AVX-512) */
    ASSERT_TRUE(simd >= 0 && simd <= 2);
}

TEST(bsgs_ops_print_caps) {
    /* Should not crash when called */
    bsgs_fast_print_caps();
    ASSERT_TRUE(1);
}

/* ============================================================================
 * Batch Size Configuration Tests
 * ============================================================================ */

TEST(bsgs_batch_size_power_of_two) {
    /* Test various power-of-2 batch sizes */
    int sizes[] = {64, 128, 256, 512, 1024, 2048};

    for (int i = 0; i < 6; i++) {
        bsgs_batch_ctx_t ctx;
        int result = bsgs_batch_init(&ctx, sizes[i]);

        ASSERT_EQ(0, result);
        ASSERT_EQ(sizes[i], ctx.batch_size);
        ASSERT_EQ(sizes[i] / 2, ctx.half_batch);

        bsgs_batch_free(&ctx);
    }
}

TEST(bsgs_batch_size_non_power_of_two) {
    /* Non-power-of-2 sizes should still work */
    int sizes[] = {100, 200, 300, 500, 1000};

    for (int i = 0; i < 5; i++) {
        bsgs_batch_ctx_t ctx;
        int result = bsgs_batch_init(&ctx, sizes[i]);

        ASSERT_EQ(0, result);
        ASSERT_EQ(sizes[i], ctx.batch_size);
        ASSERT_EQ(sizes[i] / 2, ctx.half_batch);

        bsgs_batch_free(&ctx);
    }
}

TEST(bsgs_batch_size_large) {
    /* Test with a large batch size */
    bsgs_batch_ctx_t ctx;
    int result = bsgs_batch_init(&ctx, 8192);

    ASSERT_EQ(0, result);
    ASSERT_EQ(8192, ctx.batch_size);
    ASSERT_NOT_NULL(ctx.bloom_results);
    ASSERT_NOT_NULL(ctx.xpoint_raw);

    bsgs_batch_free(&ctx);
}

/* ============================================================================
 * Constants Verification Tests
 * ============================================================================ */

TEST(bsgs_constants) {
    /* Verify that constants are defined with expected values */
    ASSERT_EQ(1024, BSGS_BATCH_SIZE);
    ASSERT_EQ(512, BSGS_HALF_BATCH);
    ASSERT_EQ(64, CACHE_LINE_SIZE);
    ASSERT_EQ(8, PREFETCH_DISTANCE);
}

TEST(bsgs_half_batch_calculation) {
    /* Verify half_batch is always batch_size / 2 */
    bsgs_batch_ctx_t ctx;

    int sizes[] = {64, 128, 256, 512, 1024};
    for (int i = 0; i < 5; i++) {
        bsgs_batch_init(&ctx, sizes[i]);
        ASSERT_EQ(sizes[i] / 2, ctx.half_batch);
        bsgs_batch_free(&ctx);
    }
}

/* ============================================================================
 * Edge Case Tests
 * ============================================================================ */

TEST(bsgs_batch_bloom_check_max_batch) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    struct bloom bloom_array[256];
    for (int i = 0; i < 256; i++) {
        bloom_init2(&bloom_array[i], 1000000, 0.000001);
    }

    /* Check with full batch size */
    memset(ctx.xpoint_raw, 0, ctx.batch_size * 32);
    int hits = bsgs_batch_bloom_check(&ctx, bloom_array, ctx.batch_size);

    /* With empty data and empty bloom, should be 0 hits */
    ASSERT_EQ(0, hits);

    for (int i = 0; i < 256; i++) {
        bloom_free(&bloom_array[i]);
    }
    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_reuse) {
    bsgs_batch_ctx_t ctx;

    /* Initialize, use, free, and re-initialize */
    bsgs_batch_init(&ctx, 128);
    ASSERT_TRUE(ctx.initialized);
    ASSERT_EQ(128, ctx.batch_size);
    bsgs_batch_free(&ctx);

    /* Re-initialize with different size */
    bsgs_batch_init(&ctx, 256);
    ASSERT_TRUE(ctx.initialized);
    ASSERT_EQ(256, ctx.batch_size);
    bsgs_batch_free(&ctx);
}

/* ============================================================================
 * Batch Point Computation Tests
 * ============================================================================ */

TEST(bsgs_batch_compute_points_null_context) {
    /* Should not crash with NULL context */
    Point startP, GSn, _2GSn;
    bsgs_batch_compute_points(NULL, &startP, &GSn, &_2GSn, 512);
    ASSERT_TRUE(1);  /* If we get here, it didn't crash */
}

TEST(bsgs_batch_compute_points_uninitialized) {
    bsgs_batch_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    Point startP, GSn, _2GSn;
    /* Should not crash with uninitialized context */
    bsgs_batch_compute_points(&ctx, &startP, &GSn, &_2GSn, 512);
    ASSERT_TRUE(1);
}

TEST(bsgs_batch_compute_points_null_startP) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    Point GSn, _2GSn;
    /* Should not crash with NULL startP */
    bsgs_batch_compute_points(&ctx, NULL, &GSn, &_2GSn, 512);
    ASSERT_TRUE(1);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_compute_points_null_GSn) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    Point startP, _2GSn;
    /* Should not crash with NULL GSn */
    bsgs_batch_compute_points(&ctx, &startP, NULL, &_2GSn, 512);
    ASSERT_TRUE(1);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_compute_points_null_2GSn) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    Point startP, GSn;
    /* Should not crash with NULL _2GSn */
    bsgs_batch_compute_points(&ctx, &startP, &GSn, NULL, 512);
    ASSERT_TRUE(1);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_compute_points_zero_length) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    Point startP, GSn, _2GSn;
    /* Should handle zero length */
    bsgs_batch_compute_points(&ctx, &startP, &GSn, &_2GSn, 0);
    ASSERT_TRUE(1);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_compute_points_small_length) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    Point startP, GSn, _2GSn;
    /* Test with small hLength */
    bsgs_batch_compute_points(&ctx, &startP, &GSn, &_2GSn, 16);
    ASSERT_TRUE(1);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_compute_points_typical_length) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    Point startP, GSn, _2GSn;
    /* Test with typical hLength (CPU_GRP_SIZE/2 - 1) */
    int typical_length = 512;  /* Common value for CPU_GRP_SIZE=1024 */
    bsgs_batch_compute_points(&ctx, &startP, &GSn, &_2GSn, typical_length);
    ASSERT_TRUE(1);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_compute_points_large_length) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    Point startP, GSn, _2GSn;
    /* Test with large hLength */
    bsgs_batch_compute_points(&ctx, &startP, &GSn, &_2GSn, 2048);
    ASSERT_TRUE(1);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_compute_points_negative_length) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    Point startP, GSn, _2GSn;
    /* Should handle negative length gracefully */
    bsgs_batch_compute_points(&ctx, &startP, &GSn, &_2GSn, -1);
    ASSERT_TRUE(1);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_compute_points_multiple_calls) {
    bsgs_batch_ctx_t ctx;
    bsgs_batch_init(&ctx, BSGS_BATCH_SIZE);

    Point startP, GSn, _2GSn;

    /* Multiple sequential calls should not crash */
    bsgs_batch_compute_points(&ctx, &startP, &GSn, &_2GSn, 256);
    bsgs_batch_compute_points(&ctx, &startP, &GSn, &_2GSn, 512);
    bsgs_batch_compute_points(&ctx, &startP, &GSn, &_2GSn, 128);

    ASSERT_TRUE(1);

    bsgs_batch_free(&ctx);
}

TEST(bsgs_batch_compute_points_different_batch_sizes) {
    Point startP, GSn, _2GSn;
    int batch_sizes[] = {64, 128, 256, 512, 1024};

    for (int i = 0; i < 5; i++) {
        bsgs_batch_ctx_t ctx;
        bsgs_batch_init(&ctx, batch_sizes[i]);

        /* Test with each batch size */
        bsgs_batch_compute_points(&ctx, &startP, &GSn, &_2GSn, batch_sizes[i] / 2 - 1);

        bsgs_batch_free(&ctx);
    }

    ASSERT_TRUE(1);
}

/* ============================================================================
 * BSGS Performance Counter Tests
 * ============================================================================ */

TEST(bsgs_fast_init_cleanup) {
    /* Initialize the fast BSGS module */
    int result = bsgs_fast_init();
    ASSERT_EQ(0, result);

    /* Cleanup should not crash */
    bsgs_fast_cleanup();
    ASSERT_TRUE(1);
}

TEST(bsgs_fast_double_init) {
    /* First initialization */
    int result1 = bsgs_fast_init();
    ASSERT_EQ(0, result1);

    /* Second initialization should be safe */
    int result2 = bsgs_fast_init();
    ASSERT_EQ(0, result2);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_cleanup_without_init) {
    /* Cleanup without init should not crash */
    bsgs_fast_cleanup();
    ASSERT_TRUE(1);
}

TEST(bsgs_fast_double_cleanup) {
    bsgs_fast_init();

    /* First cleanup */
    bsgs_fast_cleanup();

    /* Second cleanup should be safe */
    bsgs_fast_cleanup();

    ASSERT_TRUE(1);
}

TEST(bsgs_fast_get_stats_basic) {
    bsgs_fast_init();

    bsgs_perf_stats_t stats;
    bsgs_fast_get_stats(&stats);

    /* Stats structure should be populated */
    /* Initial values should be zero or valid */
    ASSERT_TRUE(stats.total_points_checked >= 0);
    ASSERT_TRUE(stats.bloom_hits >= 0);
    ASSERT_TRUE(stats.bloom_false_positives >= 0);
    ASSERT_TRUE(stats.second_checks >= 0);
    ASSERT_TRUE(stats.keys_found >= 0);
    ASSERT_TRUE(stats.time_in_modinv_ms >= 0.0);
    ASSERT_TRUE(stats.time_in_bloom_ms >= 0.0);
    ASSERT_TRUE(stats.time_in_point_calc_ms >= 0.0);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_get_stats_null) {
    bsgs_fast_init();

    /* Should not crash with NULL stats pointer */
    bsgs_fast_get_stats(NULL);
    ASSERT_TRUE(1);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_reset_stats) {
    bsgs_fast_init();

    /* Increment some counters */
    bsgs_fast_inc_points_checked(100);
    bsgs_fast_inc_bloom_hits(10);

    /* Get stats to verify they're non-zero */
    bsgs_perf_stats_t stats_before;
    bsgs_fast_get_stats(&stats_before);

    /* Reset stats */
    bsgs_fast_reset_stats();

    /* Get stats again - should be reset to zero */
    bsgs_perf_stats_t stats_after;
    bsgs_fast_get_stats(&stats_after);

    ASSERT_EQ(0, stats_after.total_points_checked);
    ASSERT_EQ(0, stats_after.bloom_hits);
    ASSERT_EQ(0, stats_after.bloom_false_positives);
    ASSERT_EQ(0, stats_after.second_checks);
    ASSERT_EQ(0, stats_after.keys_found);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_inc_points_checked) {
    bsgs_fast_init();
    bsgs_fast_reset_stats();

    /* Increment points checked counter */
    bsgs_fast_inc_points_checked(100);

    bsgs_perf_stats_t stats;
    bsgs_fast_get_stats(&stats);

    ASSERT_EQ(100, stats.total_points_checked);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_inc_points_checked_multiple) {
    bsgs_fast_init();
    bsgs_fast_reset_stats();

    /* Multiple increments should accumulate */
    bsgs_fast_inc_points_checked(50);
    bsgs_fast_inc_points_checked(30);
    bsgs_fast_inc_points_checked(20);

    bsgs_perf_stats_t stats;
    bsgs_fast_get_stats(&stats);

    ASSERT_EQ(100, stats.total_points_checked);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_inc_points_checked_large) {
    bsgs_fast_init();
    bsgs_fast_reset_stats();

    /* Test with large value */
    uint64_t large_value = 1000000000ULL;
    bsgs_fast_inc_points_checked(large_value);

    bsgs_perf_stats_t stats;
    bsgs_fast_get_stats(&stats);

    ASSERT_EQ(large_value, stats.total_points_checked);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_inc_points_checked_zero) {
    bsgs_fast_init();
    bsgs_fast_reset_stats();

    /* Increment by zero should not change counter */
    bsgs_fast_inc_points_checked(0);

    bsgs_perf_stats_t stats;
    bsgs_fast_get_stats(&stats);

    ASSERT_EQ(0, stats.total_points_checked);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_inc_bloom_hits) {
    bsgs_fast_init();
    bsgs_fast_reset_stats();

    /* Increment bloom hits counter */
    bsgs_fast_inc_bloom_hits(10);

    bsgs_perf_stats_t stats;
    bsgs_fast_get_stats(&stats);

    ASSERT_EQ(10, stats.bloom_hits);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_inc_bloom_hits_multiple) {
    bsgs_fast_init();
    bsgs_fast_reset_stats();

    /* Multiple increments should accumulate */
    bsgs_fast_inc_bloom_hits(5);
    bsgs_fast_inc_bloom_hits(3);
    bsgs_fast_inc_bloom_hits(2);

    bsgs_perf_stats_t stats;
    bsgs_fast_get_stats(&stats);

    ASSERT_EQ(10, stats.bloom_hits);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_inc_bloom_hits_zero) {
    bsgs_fast_init();
    bsgs_fast_reset_stats();

    /* Increment by zero should not change counter */
    bsgs_fast_inc_bloom_hits(0);

    bsgs_perf_stats_t stats;
    bsgs_fast_get_stats(&stats);

    ASSERT_EQ(0, stats.bloom_hits);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_inc_bloom_hits_negative) {
    bsgs_fast_init();
    bsgs_fast_reset_stats();

    /* Test with negative value (should be handled gracefully) */
    bsgs_fast_inc_bloom_hits(-5);

    bsgs_perf_stats_t stats;
    bsgs_fast_get_stats(&stats);

    /* Implementation should handle this gracefully */
    ASSERT_TRUE(stats.bloom_hits >= 0);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_simd_available) {
    bsgs_fast_init();

    int simd = bsgs_fast_simd_available();

    /* Return value should be 0 (none), 1 (AVX2), or 2 (AVX-512) */
    ASSERT_TRUE(simd >= 0 && simd <= 2);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_simd_available_without_init) {
    /* Should work even without initialization */
    int simd = bsgs_fast_simd_available();
    ASSERT_TRUE(simd >= 0 && simd <= 2);
}

TEST(bsgs_fast_print_caps) {
    bsgs_fast_init();

    /* Should not crash when called */
    bsgs_fast_print_caps();
    ASSERT_TRUE(1);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_print_caps_without_init) {
    /* Should not crash even without initialization */
    bsgs_fast_print_caps();
    ASSERT_TRUE(1);
}

TEST(bsgs_fast_stats_persistence) {
    bsgs_fast_init();
    bsgs_fast_reset_stats();

    /* Set some stats */
    bsgs_fast_inc_points_checked(1000);
    bsgs_fast_inc_bloom_hits(50);

    /* Get stats multiple times - should remain consistent */
    bsgs_perf_stats_t stats1, stats2, stats3;
    bsgs_fast_get_stats(&stats1);
    bsgs_fast_get_stats(&stats2);
    bsgs_fast_get_stats(&stats3);

    ASSERT_EQ(stats1.total_points_checked, stats2.total_points_checked);
    ASSERT_EQ(stats2.total_points_checked, stats3.total_points_checked);
    ASSERT_EQ(stats1.bloom_hits, stats2.bloom_hits);
    ASSERT_EQ(stats2.bloom_hits, stats3.bloom_hits);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_stats_after_reset) {
    bsgs_fast_init();

    /* Set some stats */
    bsgs_fast_inc_points_checked(500);
    bsgs_fast_inc_bloom_hits(25);

    /* Reset and verify all counters are zero */
    bsgs_fast_reset_stats();

    bsgs_perf_stats_t stats;
    bsgs_fast_get_stats(&stats);

    ASSERT_EQ(0, stats.total_points_checked);
    ASSERT_EQ(0, stats.bloom_hits);
    ASSERT_EQ(0, stats.bloom_false_positives);
    ASSERT_EQ(0, stats.second_checks);
    ASSERT_EQ(0, stats.keys_found);
    ASSERT_EQ(0.0, stats.time_in_modinv_ms);
    ASSERT_EQ(0.0, stats.time_in_bloom_ms);
    ASSERT_EQ(0.0, stats.time_in_point_calc_ms);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_stats_boundary_values) {
    bsgs_fast_init();
    bsgs_fast_reset_stats();

    /* Test with boundary values */
    uint64_t max_value = UINT64_MAX;
    bsgs_fast_inc_points_checked(max_value);

    bsgs_perf_stats_t stats;
    bsgs_fast_get_stats(&stats);

    /* Should handle large values */
    ASSERT_TRUE(stats.total_points_checked > 0);

    bsgs_fast_cleanup();
}

TEST(bsgs_fast_multiple_init_cleanup_cycles) {
    /* Test multiple init/cleanup cycles */
    for (int i = 0; i < 3; i++) {
        int result = bsgs_fast_init();
        ASSERT_EQ(0, result);

        bsgs_fast_inc_points_checked(100);

        bsgs_perf_stats_t stats;
        bsgs_fast_get_stats(&stats);
        ASSERT_TRUE(stats.total_points_checked >= 0);

        bsgs_fast_cleanup();
    }

    ASSERT_TRUE(1);
}

TEST(bsgs_fast_stats_counters_independent) {
    bsgs_fast_init();
    bsgs_fast_reset_stats();

    /* Increment only points_checked */
    bsgs_fast_inc_points_checked(100);

    bsgs_perf_stats_t stats;
    bsgs_fast_get_stats(&stats);

    /* Only points_checked should be non-zero */
    ASSERT_EQ(100, stats.total_points_checked);
    ASSERT_EQ(0, stats.bloom_hits);

    /* Now increment only bloom_hits */
    bsgs_fast_inc_bloom_hits(10);
    bsgs_fast_get_stats(&stats);

    /* Both should be non-zero now */
    ASSERT_EQ(100, stats.total_points_checked);
    ASSERT_EQ(10, stats.bloom_hits);

    bsgs_fast_cleanup();
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

/* Exported function for test runner */
int run_bsgs_ops_tests(void) {
    TEST_INIT();

    TEST_SECTION("Batch Context Initialization");
    RUN_TEST(bsgs_batch_init);
    RUN_TEST(bsgs_batch_init_custom_size);
    RUN_TEST(bsgs_batch_init_null_context);
    RUN_TEST(bsgs_batch_init_invalid_size);
    RUN_TEST(bsgs_batch_init_minimum_size);

    TEST_SECTION("Batch Context Cleanup");
    RUN_TEST(bsgs_batch_free);
    RUN_TEST(bsgs_batch_free_null);
    RUN_TEST(bsgs_batch_free_uninitialized);
    RUN_TEST(bsgs_batch_double_free);

    TEST_SECTION("Memory Allocation");
    RUN_TEST(bsgs_batch_memory_alignment);
    RUN_TEST(bsgs_batch_memory_size);
    RUN_TEST(bsgs_batch_multiple_contexts);

    TEST_SECTION("Bloom Filter Integration");
    RUN_TEST(bsgs_batch_bloom_check_null_context);
    RUN_TEST(bsgs_batch_bloom_check_uninitialized);
    RUN_TEST(bsgs_batch_bloom_check_null_bloom);
    RUN_TEST(bsgs_batch_bloom_check_empty);
    RUN_TEST(bsgs_batch_bloom_check_with_hits);
    RUN_TEST(bsgs_batch_bloom_check_zero_points);
    RUN_TEST(bsgs_batch_bloom_check_partial_matches);
    RUN_TEST(bsgs_batch_bloom_check_all_filters);
    RUN_TEST(bsgs_batch_bloom_check_different_batch_sizes);
    RUN_TEST(bsgs_batch_bloom_check_sequential_calls);
    RUN_TEST(bsgs_batch_bloom_check_no_matches);
    RUN_TEST(bsgs_batch_bloom_check_single_point);
    RUN_TEST(bsgs_batch_bloom_check_results_array_cleared);

    TEST_SECTION("X-Point Extraction");
    RUN_TEST(bsgs_batch_extract_xpoints_basic);
    RUN_TEST(bsgs_batch_extract_xpoints_zero);

    TEST_SECTION("SIMD Capabilities");
    RUN_TEST(bsgs_ops_simd_available);
    RUN_TEST(bsgs_ops_print_caps);

    TEST_SECTION("Batch Size Configuration");
    RUN_TEST(bsgs_batch_size_power_of_two);
    RUN_TEST(bsgs_batch_size_non_power_of_two);
    RUN_TEST(bsgs_batch_size_large);

    TEST_SECTION("Constants Verification");
    RUN_TEST(bsgs_constants);
    RUN_TEST(bsgs_half_batch_calculation);

    TEST_SECTION("Edge Cases");
    RUN_TEST(bsgs_batch_bloom_check_max_batch);
    RUN_TEST(bsgs_batch_reuse);

    TEST_SECTION("Batch Point Computation");
    RUN_TEST(bsgs_batch_compute_points_null_context);
    RUN_TEST(bsgs_batch_compute_points_uninitialized);
    RUN_TEST(bsgs_batch_compute_points_null_startP);
    RUN_TEST(bsgs_batch_compute_points_null_GSn);
    RUN_TEST(bsgs_batch_compute_points_null_2GSn);
    RUN_TEST(bsgs_batch_compute_points_zero_length);
    RUN_TEST(bsgs_batch_compute_points_small_length);
    RUN_TEST(bsgs_batch_compute_points_typical_length);
    RUN_TEST(bsgs_batch_compute_points_large_length);
    RUN_TEST(bsgs_batch_compute_points_negative_length);
    RUN_TEST(bsgs_batch_compute_points_multiple_calls);
    RUN_TEST(bsgs_batch_compute_points_different_batch_sizes);

    TEST_SECTION("BSGS Performance Counters");
    RUN_TEST(bsgs_fast_init_cleanup);
    RUN_TEST(bsgs_fast_double_init);
    RUN_TEST(bsgs_fast_cleanup_without_init);
    RUN_TEST(bsgs_fast_double_cleanup);
    RUN_TEST(bsgs_fast_get_stats_basic);
    RUN_TEST(bsgs_fast_get_stats_null);
    RUN_TEST(bsgs_fast_reset_stats);
    RUN_TEST(bsgs_fast_inc_points_checked);
    RUN_TEST(bsgs_fast_inc_points_checked_multiple);
    RUN_TEST(bsgs_fast_inc_points_checked_large);
    RUN_TEST(bsgs_fast_inc_points_checked_zero);
    RUN_TEST(bsgs_fast_inc_bloom_hits);
    RUN_TEST(bsgs_fast_inc_bloom_hits_multiple);
    RUN_TEST(bsgs_fast_inc_bloom_hits_zero);
    RUN_TEST(bsgs_fast_inc_bloom_hits_negative);
    RUN_TEST(bsgs_fast_simd_available);
    RUN_TEST(bsgs_fast_simd_available_without_init);
    RUN_TEST(bsgs_fast_print_caps);
    RUN_TEST(bsgs_fast_print_caps_without_init);
    RUN_TEST(bsgs_fast_stats_persistence);
    RUN_TEST(bsgs_fast_stats_after_reset);
    RUN_TEST(bsgs_fast_stats_boundary_values);
    RUN_TEST(bsgs_fast_multiple_init_cleanup_cycles);
    RUN_TEST(bsgs_fast_stats_counters_independent);

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_bsgs_ops_tests();
}
#endif
