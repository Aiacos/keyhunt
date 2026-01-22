/*
 * Integrated Bloom Filter Benchmark
 * Tests the bloom_wrapper with realistic keyhunt workloads
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* Test both implementations */
#define USE_FAST_BLOOM 1
#include "../bloom/bloom_wrapper.h"

#define NUM_ENTRIES 1000000
#define NUM_CHECKS 10000000
#define WARMUP 100000

static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* Generate realistic RMD160 test data */
static void generate_rmd160_data(uint8_t *data, int count) {
    srand(12345);
    for (int i = 0; i < count * 20; i++) {
        data[i] = rand() & 0xFF;
    }
}

int main() {
    printf("=== Integrated Bloom Filter Benchmark ===\n");
    printf("=== Simulating keyhunt address lookup workload ===\n\n");

    /* Allocate test data */
    uint8_t *addresses = (uint8_t*)malloc(NUM_ENTRIES * 20);
    uint8_t *queries = (uint8_t*)malloc(NUM_CHECKS * 20);

    if (!addresses || !queries) {
        printf("Failed to allocate memory\n");
        return 1;
    }

    printf("Generating %d RMD160 addresses for bloom filter...\n", NUM_ENTRIES);
    generate_rmd160_data(addresses, NUM_ENTRIES);

    printf("Generating %d query addresses...\n", NUM_CHECKS);
    srand(54321);  /* Different seed for queries */
    generate_rmd160_data(queries, NUM_CHECKS);

    /* === Test Extended Bloom Filter (auto-selects fast when possible) === */
    printf("\n--- Testing bloom_extended (wrapper) ---\n");
    bloom_extended_t bloom_ext;

    printf("Initializing bloom filter for %d entries with 0.000001 error rate...\n", NUM_ENTRIES);
    if (bloom_ext_init(&bloom_ext, NUM_ENTRIES, 0.000001) != 0) {
        printf("Failed to initialize extended bloom filter\n");
        free(addresses);
        free(queries);
        return 1;
    }

    bloom_ext_print(&bloom_ext);
    printf("Using fast implementation: %s\n", bloom_ext_is_fast(&bloom_ext) ? "YES" : "NO");

    /* Populate bloom filter */
    printf("\nPopulating bloom filter with %d addresses...\n", NUM_ENTRIES);
    double start = get_time_ms();
    for (int i = 0; i < NUM_ENTRIES; i++) {
        bloom_ext_add(&bloom_ext, addresses + i * 20, 20);
    }
    double end = get_time_ms();
    printf("Population time: %.2f ms (%.2f M adds/sec)\n",
           end - start, NUM_ENTRIES / ((end - start) / 1000.0) / 1e6);

    /* Warmup */
    printf("\nWarming up...\n");
    int dummy = 0;
    for (int i = 0; i < WARMUP; i++) {
        dummy += bloom_ext_check(&bloom_ext, queries + (i % NUM_ENTRIES) * 20, 20);
    }
    printf("Warmup result (ignore): %d\n", dummy);

    /* === Benchmark: Generic check === */
    printf("\n=== Benchmark: bloom_ext_check (generic) ===\n");
    int hits_generic = 0;
    start = get_time_ms();
    for (int i = 0; i < NUM_CHECKS; i++) {
        hits_generic += bloom_ext_check(&bloom_ext, queries + (i % NUM_ENTRIES) * 20, 20);
    }
    end = get_time_ms();
    double time_generic = end - start;

    printf("Time: %.2f ms\n", time_generic);
    printf("Throughput: %.2f M checks/sec\n", NUM_CHECKS / (time_generic / 1000.0) / 1e6);
    printf("Hits (potential matches): %d\n", hits_generic);

    /* === Benchmark: Specialized RMD160 check === */
    printf("\n=== Benchmark: bloom_ext_check_rmd160 (specialized) ===\n");
    int hits_rmd160 = 0;
    start = get_time_ms();
    for (int i = 0; i < NUM_CHECKS; i++) {
        hits_rmd160 += bloom_ext_check_rmd160(&bloom_ext, queries + (i % NUM_ENTRIES) * 20);
    }
    end = get_time_ms();
    double time_rmd160 = end - start;

    printf("Time: %.2f ms\n", time_rmd160);
    printf("Throughput: %.2f M checks/sec\n", NUM_CHECKS / (time_rmd160 / 1000.0) / 1e6);
    printf("Hits (potential matches): %d\n", hits_rmd160);

    /* === Compare with original implementation === */
    printf("\n--- Testing original bloom filter for comparison ---\n");
    struct bloom bloom_orig;

    if (bloom_init2(&bloom_orig, NUM_ENTRIES, 0.000001) != 0) {
        printf("Failed to initialize original bloom filter\n");
        bloom_ext_free(&bloom_ext);
        free(addresses);
        free(queries);
        return 1;
    }

    bloom_print(&bloom_orig);

    /* Populate */
    for (int i = 0; i < NUM_ENTRIES; i++) {
        bloom_add(&bloom_orig, addresses + i * 20, 20);
    }

    /* Warmup */
    for (int i = 0; i < WARMUP; i++) {
        dummy += bloom_check(&bloom_orig, queries + (i % NUM_ENTRIES) * 20, 20);
    }

    /* Benchmark original */
    printf("\n=== Benchmark: bloom_check (original) ===\n");
    int hits_orig = 0;
    start = get_time_ms();
    for (int i = 0; i < NUM_CHECKS; i++) {
        hits_orig += bloom_check(&bloom_orig, queries + (i % NUM_ENTRIES) * 20, 20);
    }
    end = get_time_ms();
    double time_orig = end - start;

    printf("Time: %.2f ms\n", time_orig);
    printf("Throughput: %.2f M checks/sec\n", NUM_CHECKS / (time_orig / 1000.0) / 1e6);
    printf("Hits (potential matches): %d\n", hits_orig);

    /* === Summary === */
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                    PERFORMANCE SUMMARY                       ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Implementation          │ Time (ms) │ M checks/s │ Speedup  ║\n");
    printf("╠═════════════════════════╪═══════════╪════════════╪══════════╣\n");
    printf("║ Original bloom_check    │ %9.2f │ %10.2f │   1.00x  ║\n",
           time_orig, NUM_CHECKS / (time_orig / 1000.0) / 1e6);
    printf("║ Fast bloom_ext_check    │ %9.2f │ %10.2f │  %5.2fx  ║\n",
           time_generic, NUM_CHECKS / (time_generic / 1000.0) / 1e6, time_orig / time_generic);
    printf("║ Fast bloom_ext_rmd160   │ %9.2f │ %10.2f │  %5.2fx  ║\n",
           time_rmd160, NUM_CHECKS / (time_rmd160 / 1000.0) / 1e6, time_orig / time_rmd160);
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    if (bloom_ext_is_fast(&bloom_ext)) {
        printf("\n[✓] Fast bloom filter is active\n");
        printf("[✓] Expected keyhunt address mode speedup: %.1fx for bloom lookups\n",
               time_orig / time_generic);
    }

    /* Cleanup */
    bloom_ext_free(&bloom_ext);
    bloom_free(&bloom_orig);
    free(addresses);
    free(queries);

    return 0;
}
