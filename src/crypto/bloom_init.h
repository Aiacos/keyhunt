/*
 * bloom_init.h - Bloom filter initialization and memory validation
 *
 * Provides initialization functions for both standard and extended bloom
 * filters used across all search modes (ADDRESS, RMD160, XPOINT, VANITY).
 *
 * Memory safety:
 * - Validates bloom filter size against available system RAM
 * - Uses 80% safety margin to prevent OOM crashes
 * - Provides actionable suggestions when memory is insufficient
 *
 * Two bloom filter variants:
 * - Standard (struct bloom): Used by VANITY mode
 * - Extended (bloom_extended_t): Used by ADDRESS, RMD160, XPOINT modes
 *   Provides ~2x speedup via XXH3 + bitmask optimization
 */

#ifndef CRYPTO_BLOOM_INIT_H
#define CRYPTO_BLOOM_INIT_H

#include <stdint.h>
#include <stdbool.h>

#include "../bloom/bloom.h"
#include "../bloom/bloom_wrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Bloom Filter Initialization
 * ============================================================================ */

/*
 * Initialize a standard bloom filter with memory validation.
 *
 * Allocates and configures a bloom filter sized for the given number of items.
 * For small item counts (<= 10000), a minimum size of 10000 is used.
 * The actual number of elements is scaled by FLAGBLOOMMULTIPLIER.
 *
 * Performs a memory check against available system RAM (80% safety margin).
 * If insufficient memory, prints diagnostic information and frees the filter.
 *
 * Parameters:
 *   bloom_arg   - Pointer to bloom filter structure to initialize
 *   items_bloom - Number of items to be stored in the filter
 *
 * Returns:
 *   true if initialization succeeded and memory check passed, false otherwise
 */
bool initBloomFilter(struct bloom *bloom_arg, uint64_t items_bloom);

/*
 * Initialize an extended (fast) bloom filter with memory validation.
 *
 * Similar to initBloomFilter but uses the extended bloom filter wrapper
 * which provides ~2x speedup on lookups via XXH3 hashing and bitmask
 * optimization.
 *
 * Parameters:
 *   bloom_arg   - Pointer to extended bloom filter structure to initialize
 *   items_bloom - Number of items to be stored in the filter
 *
 * Returns:
 *   true if initialization succeeded and memory check passed, false otherwise
 */
bool initBloomFilterExt(bloom_extended_t *bloom_arg, uint64_t items_bloom);

#ifdef __cplusplus
}
#endif

#endif /* CRYPTO_BLOOM_INIT_H */
