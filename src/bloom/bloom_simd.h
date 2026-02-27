/*
 * SIMD-optimized Bloom Filter implementation
 * Based on "Performance-Optimal Filtering" (Lang et al., VLDB 2019)
 *
 * Key optimizations:
 * - Cache-sectorized layout: bits spread across cache line
 * - SIMD parallel bit testing using AVX2/AVX-512
 * - Batch checking for multiple items
 * - Reduced branch mispredictions
 *
 * Performance: ~10-22x faster than traditional bloom filters
 */

#ifndef BLOOM_SIMD_H
#define BLOOM_SIMD_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Maximum batch size for stack-allocated arrays in batch check functions.
 * Matches CPU_GRP_SIZE (1024) which is the standard batch size used throughout
 * the codebase. Batches larger than this will fall back to dynamic allocation.
 */
#ifndef BLOOM_BATCH_MAX
#define BLOOM_BATCH_MAX 1024
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Check CPU SIMD support
int bloom_simd_avx2_available(void);
int bloom_simd_avx512_available(void);

/*
 * Cache-sectorized Bloom Filter structure
 *
 * Instead of spreading k hash bits across the entire filter,
 * we confine them to a single cache line (64 bytes = 512 bits).
 * This ensures only one cache miss per lookup.
 */
struct bloom_simd {
    uint8_t *bf;              // Filter data (64-byte aligned)
    uint64_t bytes;           // Total bytes in filter
    uint64_t sectors;         // Number of 64-byte sectors
    uint64_t sector_mask;     // Mask for sector index (sectors-1, power of 2)
    uint8_t k;                // Number of hash functions (bits per sector)
    uint8_t ready;            // Initialization flag
    double error;             // Target false positive rate
    uint64_t entries;         // Expected number of entries
};

/*
 * Initialize a cache-sectorized bloom filter
 *
 * @param bloom     Bloom filter structure to initialize
 * @param entries   Expected number of entries
 * @param error     Target false positive rate (0 < error < 1)
 * @return          0 on success, 1 on failure
 */
int bloom_simd_init(struct bloom_simd *bloom, uint64_t entries, double error);

/*
 * Free bloom filter memory
 */
void bloom_simd_free(struct bloom_simd *bloom);

/*
 * Reset bloom filter (clear all bits)
 */
int bloom_simd_reset(struct bloom_simd *bloom);

/*
 * Add an item to the bloom filter
 *
 * @param bloom     Initialized bloom filter
 * @param buffer    Item data
 * @param len       Item length in bytes
 * @return          0 if new, 1 if possibly already present
 */
int bloom_simd_add(struct bloom_simd *bloom, const void *buffer, int len);

/*
 * Check if an item might be in the bloom filter (standard interface)
 *
 * @param bloom     Initialized bloom filter
 * @param buffer    Item data
 * @param len       Item length in bytes
 * @return          0 if definitely not present, 1 if possibly present
 */
int bloom_simd_check(struct bloom_simd *bloom, const void *buffer, int len);

/*
 * AVX2-optimized check (4 items at once)
 * Returns bitmap of potentially present items (bit i = result for item i)
 */
uint32_t bloom_simd_check_avx2_batch4(
    struct bloom_simd *bloom,
    const void *buffer0, const void *buffer1,
    const void *buffer2, const void *buffer3,
    int len);

/*
 * AVX2-optimized check for 8 items at once
 * Returns bitmap of potentially present items
 */
uint32_t bloom_simd_check_avx2_batch8(
    struct bloom_simd *bloom,
    const void **buffers,
    int len);

/*
 * AVX-512 optimized check for 16 items at once
 * Returns bitmap of potentially present items
 */
uint32_t bloom_simd_check_avx512_batch16(
    struct bloom_simd *bloom,
    const void **buffers,
    int len);

/*
 * Optimized check for 20-byte RIPEMD160 hashes (common in Bitcoin)
 * Uses pre-computed hash values for speed
 */
int bloom_simd_check_rmd160(struct bloom_simd *bloom, const uint8_t *hash20);

/*
 * Batch check for multiple 20-byte hashes
 * Writes results to 'results' array (1 = possibly present, 0 = not present)
 *
 * @param bloom     Initialized bloom filter
 * @param hashes    Array of 20-byte hash pointers
 * @param count     Number of hashes to check
 * @param results   Output array (must have 'count' elements)
 */
void bloom_simd_check_rmd160_batch(
    struct bloom_simd *bloom,
    const uint8_t **hashes,
    int count,
    uint8_t *results);

/*
 * Print bloom filter statistics
 */
void bloom_simd_print(struct bloom_simd *bloom);

#ifdef __cplusplus
}
#endif

#endif // BLOOM_SIMD_H
