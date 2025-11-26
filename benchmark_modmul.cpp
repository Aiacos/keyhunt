/*
 * Benchmark for ModMulK1 implementations
 * Compares original C++ implementation vs optimized assembly
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "secp256k1/Int.h"
#include "secp256k1/IntMod_asm.h"

#define ITERATIONS 10000000
#define WARMUP_ITERATIONS 100000

static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* Test values - typical secp256k1 field elements */
static const uint64_t test_a[4] = {
    0x9C47D08FFB10D4B8ULL,
    0xFD17B448A6855419ULL,
    0x5DA4FBFC0E1108A8ULL,
    0x483ADA7726A3C465ULL
};

static const uint64_t test_b[4] = {
    0x47AE96FFB10D4B8CULL,
    0xA1B2C3D4E5F67890ULL,
    0x1234567890ABCDEFULL,
    0x79BE667EF9DCBBACULL
};

int main() {
    printf("=== ModMulK1 Benchmark ===\n\n");

    Int a, b, result_orig;
    uint64_t result_asm[5] = {0};

    /* Initialize test values */
    memcpy(a.bits64, test_a, 32);
    a.bits64[4] = 0;
    memcpy(b.bits64, test_b, 32);
    b.bits64[4] = 0;

    printf("Test values:\n");
    printf("  a = %016llx %016llx %016llx %016llx\n",
           (unsigned long long)a.bits64[3], (unsigned long long)a.bits64[2],
           (unsigned long long)a.bits64[1], (unsigned long long)a.bits64[0]);
    printf("  b = %016llx %016llx %016llx %016llx\n",
           (unsigned long long)b.bits64[3], (unsigned long long)b.bits64[2],
           (unsigned long long)b.bits64[1], (unsigned long long)b.bits64[0]);
    printf("\n");

    /* === Warmup === */
    printf("Warming up...\n");
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        result_orig.ModMulK1(&a, &b);
        ModMulK1_asm(a.bits64, b.bits64, result_asm);
    }

    /* === Correctness check === */
    result_orig.ModMulK1(&a, &b);
    ModMulK1_asm(a.bits64, b.bits64, result_asm);

    printf("Results:\n");
    printf("  Original: %016llx %016llx %016llx %016llx\n",
           (unsigned long long)result_orig.bits64[3], (unsigned long long)result_orig.bits64[2],
           (unsigned long long)result_orig.bits64[1], (unsigned long long)result_orig.bits64[0]);
    printf("  Assembly: %016llx %016llx %016llx %016llx\n",
           (unsigned long long)result_asm[3], (unsigned long long)result_asm[2],
           (unsigned long long)result_asm[1], (unsigned long long)result_asm[0]);

    bool match = (result_orig.bits64[0] == result_asm[0] &&
                  result_orig.bits64[1] == result_asm[1] &&
                  result_orig.bits64[2] == result_asm[2] &&
                  result_orig.bits64[3] == result_asm[3]);
    printf("  Match: %s\n\n", match ? "YES ✓" : "NO ✗");

    if (!match) {
        printf("ERROR: Results don't match! Aborting.\n");
        return 1;
    }

    /* === Benchmark original implementation === */
    printf("Benchmarking original ModMulK1 (%d iterations)...\n", ITERATIONS);
    double start = get_time_ms();
    for (int i = 0; i < ITERATIONS; i++) {
        result_orig.ModMulK1(&a, &b);
        /* Prevent optimization */
        a.bits64[0] ^= result_orig.bits64[0] & 1;
    }
    double end = get_time_ms();
    double time_orig = end - start;
    double ops_per_sec_orig = ITERATIONS / (time_orig / 1000.0);

    /* Reset a */
    memcpy(a.bits64, test_a, 32);
    a.bits64[4] = 0;

    /* === Benchmark assembly implementation === */
    printf("Benchmarking assembly ModMulK1 (%d iterations)...\n", ITERATIONS);
    start = get_time_ms();
    for (int i = 0; i < ITERATIONS; i++) {
        ModMulK1_asm(a.bits64, b.bits64, result_asm);
        /* Prevent optimization */
        a.bits64[0] ^= result_asm[0] & 1;
    }
    end = get_time_ms();
    double time_asm = end - start;
    double ops_per_sec_asm = ITERATIONS / (time_asm / 1000.0);

    /* Reset a */
    memcpy(a.bits64, test_a, 32);
    a.bits64[4] = 0;

    /* === Benchmark ModSquareK1 === */
    printf("Benchmarking original ModSquareK1 (%d iterations)...\n", ITERATIONS);
    start = get_time_ms();
    for (int i = 0; i < ITERATIONS; i++) {
        result_orig.ModSquareK1(&a);
        a.bits64[0] ^= result_orig.bits64[0] & 1;
    }
    end = get_time_ms();
    double time_sq_orig = end - start;
    double ops_per_sec_sq_orig = ITERATIONS / (time_sq_orig / 1000.0);

    memcpy(a.bits64, test_a, 32);
    a.bits64[4] = 0;

    printf("Benchmarking assembly ModSquareK1 (%d iterations)...\n", ITERATIONS);
    start = get_time_ms();
    for (int i = 0; i < ITERATIONS; i++) {
        ModSquareK1_asm(a.bits64, result_asm);
        a.bits64[0] ^= result_asm[0] & 1;
    }
    end = get_time_ms();
    double time_sq_asm = end - start;
    double ops_per_sec_sq_asm = ITERATIONS / (time_sq_asm / 1000.0);

    /* Verify square results match */
    memcpy(a.bits64, test_a, 32);
    a.bits64[4] = 0;
    result_orig.ModSquareK1(&a);
    ModSquareK1_asm(a.bits64, result_asm);
    bool sq_match = (result_orig.bits64[0] == result_asm[0] &&
                     result_orig.bits64[1] == result_asm[1] &&
                     result_orig.bits64[2] == result_asm[2] &&
                     result_orig.bits64[3] == result_asm[3]);

    /* === Results === */
    printf("\n=== RESULTS ===\n\n");
    printf("ModMulK1:\n");
    printf("  Original:  %.2f ms (%.2f M ops/sec)\n", time_orig, ops_per_sec_orig / 1e6);
    printf("  Assembly:  %.2f ms (%.2f M ops/sec)\n", time_asm, ops_per_sec_asm / 1e6);
    printf("  Speedup:   %.2fx\n", time_orig / time_asm);
    printf("\n");
    printf("ModSquareK1:\n");
    printf("  Original:  %.2f ms (%.2f M ops/sec)\n", time_sq_orig, ops_per_sec_sq_orig / 1e6);
    printf("  Assembly:  %.2f ms (%.2f M ops/sec)\n", time_sq_asm, ops_per_sec_sq_asm / 1e6);
    printf("  Speedup:   %.2fx\n", time_sq_orig / time_sq_asm);
    printf("  Results match: %s\n", sq_match ? "YES ✓" : "NO ✗");
    printf("\n");

    return 0;
}
