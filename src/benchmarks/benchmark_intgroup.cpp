/*
 * Benchmark for IntGroup::ModInv() implementations
 * Compares original vs optimized batch modular inversion
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "../secp256k1/Int.h"
#include "../secp256k1/IntGroup.h"

#define BATCH_SIZES_COUNT 6
static const int BATCH_SIZES[BATCH_SIZES_COUNT] = {32, 64, 128, 256, 512, 1024};
#define ITERATIONS_PER_BATCH 1000
#define WARMUP_ITERATIONS 100

static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* Generate random test values in the secp256k1 field */
static void generate_test_ints(Int *ints, int count) {
    srand(42);  /* Fixed seed for reproducibility */
    for (int i = 0; i < count; i++) {
        /* Generate random 256-bit values */
        for (int j = 0; j < 4; j++) {
            ints[i].bits64[j] = ((uint64_t)rand() << 32) | rand();
        }
        ints[i].bits64[4] = 0;

        /* Ensure non-zero to avoid division by zero in modular inverse */
        if (ints[i].IsZero()) {
            ints[i].bits64[0] = 1;
        }
    }
}

/* Verify that two Int arrays match */
static bool verify_results(Int *a, Int *b, int count) {
    for (int i = 0; i < count; i++) {
        if (a[i].bits64[0] != b[i].bits64[0] ||
            a[i].bits64[1] != b[i].bits64[1] ||
            a[i].bits64[2] != b[i].bits64[2] ||
            a[i].bits64[3] != b[i].bits64[3]) {
            return false;
        }
    }
    return true;
}

/* Clone Int array */
static void clone_ints(Int *dst, Int *src, int count) {
    for (int i = 0; i < count; i++) {
        dst[i].Set(&src[i]);
    }
}

int main() {
    printf("=== IntGroup::ModInv() Benchmark ===\n\n");

    /* Print test configuration */
    printf("Configuration:\n");
    printf("  Batch sizes: ");
    for (int i = 0; i < BATCH_SIZES_COUNT; i++) {
        printf("%d%s", BATCH_SIZES[i], (i < BATCH_SIZES_COUNT - 1) ? ", " : "\n");
    }
    printf("  Iterations per batch: %d\n", ITERATIONS_PER_BATCH);
    printf("  Warmup iterations: %d\n\n", WARMUP_ITERATIONS);

    /* Results summary table header */
    printf("=== PERFORMANCE SUMMARY ===\n\n");
    printf("%-10s  %-12s  %-12s  %-12s  %-12s\n",
           "Batch", "Original", "Optimized", "Speedup", "Status");
    printf("%-10s  %-12s  %-12s  %-12s  %-12s\n",
           "Size", "(ms)", "(ms)", "", "");
    printf("----------  ------------  ------------  ------------  ------------\n");

    /* Test each batch size */
    for (int bs_idx = 0; bs_idx < BATCH_SIZES_COUNT; bs_idx++) {
        int batch_size = BATCH_SIZES[bs_idx];

        /* Allocate test data */
        Int *test_data = new Int[batch_size];
        Int *work_orig = new Int[batch_size];
        Int *work_opt = new Int[batch_size];

        /* Generate test data */
        generate_test_ints(test_data, batch_size);

        /* Create IntGroup objects */
        IntGroup group_orig(batch_size);
        IntGroup group_opt(batch_size);

        /* === Warmup === */
        for (int w = 0; w < WARMUP_ITERATIONS; w++) {
            clone_ints(work_orig, test_data, batch_size);
            group_orig.Set(work_orig);
            group_orig.ModInv();

            clone_ints(work_opt, test_data, batch_size);
            group_opt.Set(work_opt);
            group_opt.ModInvOptimized();
        }

        /* === Correctness check === */
        clone_ints(work_orig, test_data, batch_size);
        group_orig.Set(work_orig);
        group_orig.ModInv();

        clone_ints(work_opt, test_data, batch_size);
        group_opt.Set(work_opt);
        group_opt.ModInvOptimized();

        bool match = verify_results(work_orig, work_opt, batch_size);

        /* === Benchmark original implementation === */
        double start = get_time_ms();
        for (int i = 0; i < ITERATIONS_PER_BATCH; i++) {
            clone_ints(work_orig, test_data, batch_size);
            group_orig.Set(work_orig);
            group_orig.ModInv();

            /* Prevent optimization - modify one bit of test data */
            test_data[0].bits64[0] ^= work_orig[0].bits64[0] & 1;
        }
        double end = get_time_ms();
        double time_orig = end - start;

        /* Reset test data */
        generate_test_ints(test_data, batch_size);

        /* === Benchmark optimized implementation === */
        start = get_time_ms();
        for (int i = 0; i < ITERATIONS_PER_BATCH; i++) {
            clone_ints(work_opt, test_data, batch_size);
            group_opt.Set(work_opt);
            group_opt.ModInvOptimized();

            /* Prevent optimization - modify one bit of test data */
            test_data[0].bits64[0] ^= work_opt[0].bits64[0] & 1;
        }
        end = get_time_ms();
        double time_opt = end - start;

        /* Calculate speedup */
        double speedup = time_orig / time_opt;

        /* Print results for this batch size */
        printf("%-10d  %10.2f    %10.2f    %10.2fx    %s\n",
               batch_size, time_orig, time_opt, speedup,
               match ? "✓" : "✗ FAIL");

        /* Cleanup */
        delete[] test_data;
        delete[] work_orig;
        delete[] work_opt;

        if (!match) {
            printf("\nERROR: Results don't match for batch size %d! Aborting.\n", batch_size);
            return 1;
        }
    }

    printf("\n=== DETAILED ANALYSIS ===\n\n");

    /* Detailed analysis for largest batch size */
    int batch_size = BATCH_SIZES[BATCH_SIZES_COUNT - 1];
    printf("Detailed timing for batch size %d:\n\n", batch_size);

    Int *test_data = new Int[batch_size];
    Int *work_orig = new Int[batch_size];
    Int *work_opt = new Int[batch_size];

    generate_test_ints(test_data, batch_size);

    IntGroup group_orig(batch_size);
    IntGroup group_opt(batch_size);

    /* Warmup */
    for (int w = 0; w < WARMUP_ITERATIONS; w++) {
        clone_ints(work_orig, test_data, batch_size);
        group_orig.Set(work_orig);
        group_orig.ModInv();

        clone_ints(work_opt, test_data, batch_size);
        group_opt.Set(work_opt);
        group_opt.ModInvOptimized();
    }

    /* Original implementation */
    const int detailed_iters = ITERATIONS_PER_BATCH * 2;
    double start = get_time_ms();
    for (int i = 0; i < detailed_iters; i++) {
        clone_ints(work_orig, test_data, batch_size);
        group_orig.Set(work_orig);
        group_orig.ModInv();
        test_data[0].bits64[0] ^= work_orig[0].bits64[0] & 1;
    }
    double end = get_time_ms();
    double time_orig = end - start;
    double inversions_per_sec_orig = (detailed_iters * batch_size) / (time_orig / 1000.0);

    /* Reset test data */
    generate_test_ints(test_data, batch_size);

    /* Optimized implementation */
    start = get_time_ms();
    for (int i = 0; i < detailed_iters; i++) {
        clone_ints(work_opt, test_data, batch_size);
        group_opt.Set(work_opt);
        group_opt.ModInvOptimized();
        test_data[0].bits64[0] ^= work_opt[0].bits64[0] & 1;
    }
    end = get_time_ms();
    double time_opt = end - start;
    double inversions_per_sec_opt = (detailed_iters * batch_size) / (time_opt / 1000.0);

    printf("Original ModInv():\n");
    printf("  Total time:       %.2f ms\n", time_orig);
    printf("  Time per batch:   %.4f ms\n", time_orig / detailed_iters);
    printf("  Inversions/sec:   %.2f M/sec\n", inversions_per_sec_orig / 1e6);
    printf("\n");

    printf("Optimized ModInvOptimized():\n");
    printf("  Total time:       %.2f ms\n", time_opt);
    printf("  Time per batch:   %.4f ms\n", time_opt / detailed_iters);
    printf("  Inversions/sec:   %.2f M/sec\n", inversions_per_sec_opt / 1e6);
    printf("\n");

    printf("Performance Gain:\n");
    printf("  Speedup:          %.2fx\n", time_orig / time_opt);
    printf("  Time reduction:   %.1f%%\n", ((time_orig - time_opt) / time_orig) * 100.0);
    printf("  Throughput gain:  %.1f%%\n", ((inversions_per_sec_opt - inversions_per_sec_orig) / inversions_per_sec_orig) * 100.0);
    printf("\n");

    /* Cleanup */
    delete[] test_data;
    delete[] work_orig;
    delete[] work_opt;

    printf("=== OPTIMIZATION SUMMARY ===\n\n");
    printf("The optimized ModInvOptimized() implementation uses:\n");
    printf("  - 8x loop unrolling (vs 4x in original)\n");
    printf("  - Aggressive prefetching (16-24 elements ahead)\n");
    printf("  - Improved backward pass with 8-element prefetching\n");
    printf("  - Better cache line utilization with 64-byte alignment\n");
    printf("\n");
    printf("Expected improvements in real-world BSGS usage:\n");
    printf("  - Faster batch point operations\n");
    printf("  - Better CPU pipeline utilization\n");
    printf("  - Reduced memory latency through prefetching\n");
    printf("\n");

    return 0;
}
