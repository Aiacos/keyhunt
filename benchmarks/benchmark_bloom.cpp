/*
 * Benchmark for Bloom Filter implementations
 * Compares original vs optimized fast bloom filter
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "../src/bloom/bloom.h"
#include "../src/bloom/bloom_fast.h"

#define ITERATIONS 10000000
#define WARMUP_ITERATIONS 100000
#define NUM_ENTRIES 1000000  /* 1M entries for realistic test */

static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* Generate random 20-byte RMD160 hashes for testing */
static void generate_test_data(uint8_t *data, int count) {
    srand(42);  /* Fixed seed for reproducibility */
    for (int i = 0; i < count * 20; i++) {
        data[i] = rand() & 0xFF;
    }
}

int main() {
    printf("=== Bloom Filter Benchmark ===\n\n");

    /* Allocate test data */
    uint8_t *test_hashes = (uint8_t*)malloc(ITERATIONS * 20);
    if (!test_hashes) {
        printf("Failed to allocate test data\n");
        return 1;
    }

    printf("Generating %d test RMD160 hashes...\n", ITERATIONS);
    generate_test_data(test_hashes, ITERATIONS);

    /* === Initialize original bloom filter === */
    struct bloom bloom_orig;
    printf("Initializing original bloom filter (1M entries, 0.0001 error)...\n");
    if (bloom_init2(&bloom_orig, NUM_ENTRIES, 0.0001) != 0) {
        printf("Failed to initialize original bloom filter\n");
        free(test_hashes);
        return 1;
    }
    bloom_print(&bloom_orig);

    /* === Initialize fast bloom filter === */
    /* Calculate equivalent size: 1M entries with 0.0001 error needs ~19 bits per entry */
    /* Total bits ~= 19M bits = 2^24.2, round to 2^25 = 32M bits = 4MB */
    bloom_fast_t bloom_fast;
    printf("\nInitializing fast bloom filter (2^25 bits = 4MB, 7 hashes)...\n");
    if (bloom_fast_init(&bloom_fast, 25, 7) != 0) {
        printf("Failed to initialize fast bloom filter\n");
        bloom_free(&bloom_orig);
        free(test_hashes);
        return 1;
    }
    printf("  bits = %lu\n", bloom_fast.bits);
    printf("  bytes = %lu (%lu MB)\n", bloom_fast.bits / 8, bloom_fast.bits / 8 / 1024 / 1024);
    printf("  hashes = %d\n", bloom_fast.hashes);

    /* === Populate bloom filters === */
    printf("\nPopulating bloom filters with first %d entries...\n", NUM_ENTRIES);
    for (int i = 0; i < NUM_ENTRIES; i++) {
        bloom_add(&bloom_orig, test_hashes + i * 20, 20);
        bloom_fast_add(&bloom_fast, test_hashes + i * 20, 20);
    }

    /* === Warmup === */
    printf("Warming up...\n");
    int dummy = 0;
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        dummy += bloom_check(&bloom_orig, test_hashes + (i % NUM_ENTRIES) * 20, 20);
        dummy += bloom_fast_check(&bloom_fast, test_hashes + (i % NUM_ENTRIES) * 20, 20);
    }
    printf("Warmup result (ignore): %d\n\n", dummy);

    /* === Benchmark original bloom_check === */
    printf("Benchmarking original bloom_check (%d iterations)...\n", ITERATIONS);
    int hits_orig = 0;
    double start = get_time_ms();
    for (int i = 0; i < ITERATIONS; i++) {
        hits_orig += bloom_check(&bloom_orig, test_hashes + (i % NUM_ENTRIES) * 20, 20);
    }
    double end = get_time_ms();
    double time_orig = end - start;
    double checks_per_sec_orig = ITERATIONS / (time_orig / 1000.0);

    /* === Benchmark fast bloom_check === */
    printf("Benchmarking fast bloom_fast_check (%d iterations)...\n", ITERATIONS);
    int hits_fast = 0;
    start = get_time_ms();
    for (int i = 0; i < ITERATIONS; i++) {
        hits_fast += bloom_fast_check(&bloom_fast, test_hashes + (i % NUM_ENTRIES) * 20, 20);
    }
    end = get_time_ms();
    double time_fast = end - start;
    double checks_per_sec_fast = ITERATIONS / (time_fast / 1000.0);

    /* === Benchmark specialized RMD160 check === */
    printf("Benchmarking bloom_fast_check_rmd160 (%d iterations)...\n", ITERATIONS);
    int hits_rmd = 0;
    start = get_time_ms();
    for (int i = 0; i < ITERATIONS; i++) {
        hits_rmd += bloom_fast_check_rmd160(&bloom_fast, test_hashes + (i % NUM_ENTRIES) * 20);
    }
    end = get_time_ms();
    double time_rmd = end - start;
    double checks_per_sec_rmd = ITERATIONS / (time_rmd / 1000.0);

    /* === Results === */
    printf("\n=== RESULTS ===\n\n");
    printf("Original bloom_check:\n");
    printf("  Time:       %.2f ms\n", time_orig);
    printf("  Throughput: %.2f M checks/sec\n", checks_per_sec_orig / 1e6);
    printf("  Hits:       %d\n", hits_orig);
    printf("\n");

    printf("Fast bloom_fast_check:\n");
    printf("  Time:       %.2f ms\n", time_fast);
    printf("  Throughput: %.2f M checks/sec\n", checks_per_sec_fast / 1e6);
    printf("  Hits:       %d\n", hits_fast);
    printf("  Speedup:    %.2fx\n", time_orig / time_fast);
    printf("\n");

    printf("Specialized bloom_fast_check_rmd160:\n");
    printf("  Time:       %.2f ms\n", time_rmd);
    printf("  Throughput: %.2f M checks/sec\n", checks_per_sec_rmd / 1e6);
    printf("  Hits:       %d\n", hits_rmd);
    printf("  Speedup:    %.2fx\n", time_orig / time_rmd);
    printf("\n");

    /* === Correctness check === */
    printf("=== CORRECTNESS CHECK ===\n");
    int mismatches = 0;
    for (int i = 0; i < 10000; i++) {
        int r_orig = bloom_check(&bloom_orig, test_hashes + (i % NUM_ENTRIES) * 20, 20);
        int r_fast = bloom_fast_check(&bloom_fast, test_hashes + (i % NUM_ENTRIES) * 20, 20);
        /* Note: Different hash functions may give different results due to different bit patterns */
        /* But both should return 1 for known entries */
        if (i < NUM_ENTRIES) {
            if (r_orig != 1) mismatches++;
            if (r_fast != 1) mismatches++;
        }
    }
    printf("Mismatches (should be 0 for first %d entries): %d\n", NUM_ENTRIES, mismatches);

    /* Cleanup */
    bloom_free(&bloom_orig);
    bloom_fast_free(&bloom_fast);
    free(test_hashes);

    return 0;
}
