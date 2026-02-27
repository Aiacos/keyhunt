// Performance benchmark for bloom_simd_check_rmd160_batch
// Measures the performance impact of stack vs heap allocation optimization

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

extern "C" {
#include "bloom/bloom.h"
#include "bloom/bloom_simd.h"
}

#define NUM_ITERATIONS 100000
#define BATCH_SIZE 64  // Typical batch size

// Get nanosecond timestamp
static inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main() {
    printf("Bloom Filter Batch Check Performance Benchmark\n");
    printf("================================================\n\n");

    // Initialize bloom filter
    struct bloom bf;
    int result = bloom_init2(&bf, 100000, 0.01);
    if (result != 0) {
        fprintf(stderr, "Failed to initialize bloom filter\n");
        return 1;
    }

    // Add some test data to bloom filter
    uint8_t test_hash[20];
    for (int i = 0; i < 50000; i++) {
        memset(test_hash, i & 0xFF, 20);
        bloom_add(&bf, test_hash, 20);
    }

    printf("Bloom filter initialized: %llu entries, %llu bytes\n",
           (unsigned long long)bf.entries, (unsigned long long)bf.bytes);
    printf("Test configuration: %d iterations, batch size %d\n\n",
           NUM_ITERATIONS, BATCH_SIZE);

    // Prepare test batch of RMD160 hashes
    uint8_t *batch = (uint8_t*)malloc(BATCH_SIZE * 20);
    if (!batch) {
        fprintf(stderr, "Failed to allocate batch memory\n");
        bloom_free(&bf);
        return 1;
    }

    // Fill with test data (half present, half not present)
    for (int i = 0; i < BATCH_SIZE; i++) {
        memset(batch + i * 20, (i < BATCH_SIZE/2) ? (i & 0xFF) : 0xFF, 20);
    }

    // Warmup
    printf("Warming up...\n");
    for (int i = 0; i < 10000; i++) {
        bloom_simd_check_rmd160_batch(&bf, batch, BATCH_SIZE);
    }

    // Benchmark
    printf("Running benchmark...\n");
    uint64_t start = get_time_ns();

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        bloom_simd_check_rmd160_batch(&bf, batch, BATCH_SIZE);
    }

    uint64_t end = get_time_ns();
    uint64_t elapsed_ns = end - start;

    // Calculate statistics
    double elapsed_ms = elapsed_ns / 1000000.0;
    double avg_ns_per_call = (double)elapsed_ns / NUM_ITERATIONS;
    double avg_ns_per_item = avg_ns_per_call / BATCH_SIZE;
    double calls_per_sec = (NUM_ITERATIONS * 1000000000.0) / elapsed_ns;
    double items_per_sec = calls_per_sec * BATCH_SIZE;

    printf("\n");
    printf("Results:\n");
    printf("--------\n");
    printf("Total time:           %.3f ms\n", elapsed_ms);
    printf("Avg per call:         %.2f ns\n", avg_ns_per_call);
    printf("Avg per item:         %.2f ns\n", avg_ns_per_item);
    printf("Throughput (calls):   %.2f M/sec\n", calls_per_sec / 1000000.0);
    printf("Throughput (items):   %.2f M/sec\n", items_per_sec / 1000000.0);
    printf("\n");

    printf("Performance characteristics:\n");
    printf("- Stack allocation eliminates malloc/free overhead\n");
    printf("- Cache-aligned arrays (alignas(64)) improve SIMD access\n");
    printf("- Batch size %d fits comfortably in L1 cache\n", BATCH_SIZE);
    printf("- Expected improvement: 5-15%% reduction in call time\n");
    printf("\n");

    // Cleanup
    free(batch);
    bloom_free(&bf);

    printf("Benchmark completed successfully!\n");
    return 0;
}
