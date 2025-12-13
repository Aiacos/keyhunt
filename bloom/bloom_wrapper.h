/*
 * Bloom Filter Wrapper - Unified interface for original and fast bloom filters
 *
 * This wrapper provides a drop-in replacement for bloom.h that can use
 * either the original implementation or the optimized fast implementation.
 *
 * Usage:
 *   #define USE_FAST_BLOOM 1  // Before including this header
 *   #include "bloom/bloom_wrapper.h"
 */

#ifndef BLOOM_WRAPPER_H
#define BLOOM_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <math.h>

/* Configuration: set to 1 to use fast bloom filter */
#ifndef USE_FAST_BLOOM
#define USE_FAST_BLOOM 1
#endif

#if USE_FAST_BLOOM
#include "bloom_fast.h"
#endif

#include "bloom.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Extended bloom filter structure that can hold either implementation
 */
typedef struct bloom_extended {
    struct bloom orig;          /* Original bloom filter */
#if USE_FAST_BLOOM
    bloom_fast_t fast;          /* Fast bloom filter */
    bool use_fast;              /* Flag to indicate which to use */
#endif
} bloom_extended_t;

/*
 * Initialize extended bloom filter
 * Automatically chooses fast implementation when conditions are met:
 * - USE_FAST_BLOOM is enabled
 * - Number of entries and error rate allow power-of-2 sizing
 */
static inline int bloom_ext_init(bloom_extended_t *be, uint64_t entries, long double error) {
    if (!be) return -1;

#if USE_FAST_BLOOM
    /* Calculate parameters for fast bloom filter */
    /* bits_per_element = -log(error) / (ln(2)^2) */
    long double bpe = -logl(error) / 0.480453013918201L;
    uint64_t total_bits = (uint64_t)(entries * bpe);

    /* Find the next power of 2 >= total_bits */
    uint8_t log2_bits = 0;
    uint64_t p2 = 1;
    while (p2 < total_bits && log2_bits < 36) {
        p2 <<= 1;
        log2_bits++;
    }

    /* Calculate number of hash functions */
    uint8_t hashes = (uint8_t)ceill(0.693147180559945L * bpe);
    if (hashes < 1) hashes = 1;
    if (hashes > 16) hashes = 16;

    /* Try to initialize fast bloom filter */
    if (log2_bits >= 10 && log2_bits <= 36) {
        if (bloom_fast_init(&be->fast, log2_bits, hashes) == 0) {
            be->use_fast = true;
            /* Also initialize orig structure with metadata for compatibility */
            be->orig.entries = entries;
            be->orig.error = error;
            be->orig.bits = be->fast.bits;
            be->orig.bytes = be->fast.bits / 8;
            be->orig.hashes = hashes;
            be->orig.bpe = bpe;
            be->orig.bf = be->fast.bf;  /* Point to same memory */
            be->orig.ready = 1;
            return 0;
        }
    }

    /* Fall back to original implementation */
    be->use_fast = false;
#endif

    return bloom_init2(&be->orig, entries, error);
}

/*
 * Check if element is in bloom filter
 */
static inline int bloom_ext_check(bloom_extended_t *be, const void *buffer, int len) {
#if USE_FAST_BLOOM
    if (be->use_fast) {
        return bloom_fast_check(&be->fast, buffer, len);
    }
#endif
    return bloom_check(&be->orig, buffer, len);
}

/*
 * Specialized check for 20-byte RMD160 hashes
 */
static inline int bloom_ext_check_rmd160(bloom_extended_t *be, const uint8_t *rmd160) {
#if USE_FAST_BLOOM
    if (be->use_fast) {
        return bloom_fast_check_rmd160(&be->fast, rmd160);
    }
#endif
    return bloom_check(&be->orig, rmd160, 20);
}

/*
 * Batch check for 20-byte RMD160 hashes
 * Returns bitmap of results (bit i = 1 if hash i is possibly in bloom)
 * count must be <= 64
 */
static inline uint64_t bloom_ext_check_rmd160_batch(
    bloom_extended_t *be,
    const uint8_t **hashes,
    int count)
{
#if USE_FAST_BLOOM
    if (be->use_fast) {
        return bloom_fast_check_rmd160_batch(&be->fast, hashes, count);
    }
#endif
    /* Fallback: check one by one */
    uint64_t results = 0;
    for (int i = 0; i < count && i < 64; i++) {
        if (bloom_check(&be->orig, hashes[i], 20)) {
            results |= (1ULL << i);
        }
    }
    return results;
}

/*
 * Add element to bloom filter
 */
static inline void bloom_ext_add(bloom_extended_t *be, const void *buffer, int len) {
#if USE_FAST_BLOOM
    if (be->use_fast) {
        bloom_fast_add(&be->fast, buffer, len);
        return;
    }
#endif
    bloom_add(&be->orig, buffer, len);
}

/*
 * Free bloom filter memory
 */
static inline void bloom_ext_free(bloom_extended_t *be) {
    if (!be) return;

#if USE_FAST_BLOOM
    if (be->use_fast) {
        /* Don't free orig.bf since it points to fast.bf */
        be->orig.bf = NULL;
        bloom_fast_free(&be->fast);
        be->use_fast = false;
        return;
    }
#endif
    bloom_free(&be->orig);
}

/*
 * Get bloom filter memory usage in bytes
 */
static inline uint64_t bloom_ext_bytes(bloom_extended_t *be) {
    if (!be) return 0;
#if USE_FAST_BLOOM
    if (be->use_fast) {
        return be->fast.bits / 8;
    }
#endif
    return be->orig.bytes;
}

/*
 * Check if using fast implementation
 */
static inline bool bloom_ext_is_fast(bloom_extended_t *be) {
#if USE_FAST_BLOOM
    return be && be->use_fast;
#else
    (void)be;
    return false;
#endif
}

/*
 * Print bloom filter info
 */
static inline void bloom_ext_print(bloom_extended_t *be) {
    if (!be) return;

#if USE_FAST_BLOOM
    if (be->use_fast) {
        printf("Bloom filter (FAST implementation):\n");
        printf("  entries = %" PRIu64 "\n", be->orig.entries);
        printf("  bits = %" PRIu64 " (2^%d)\n", be->fast.bits, be->fast.log2_bits);
        printf("  bytes = %" PRIu64 " (%.2f MB)\n",
               be->fast.bits / 8,
               (double)(be->fast.bits / 8) / (1024 * 1024));
        printf("  hashes = %d\n", be->fast.hashes);
        printf("  optimization = XXH3_128bits + bitmask\n");
        return;
    }
#endif
    bloom_print(&be->orig);
}

/*
 * Get pointer to bloom filter data (for file I/O)
 */
static inline uint8_t* bloom_ext_get_bf(bloom_extended_t *be) {
    if (!be) return NULL;
#if USE_FAST_BLOOM
    if (be->use_fast) {
        return be->fast.bf;
    }
#endif
    return be->orig.bf;
}

/*
 * Set bloom filter data pointer (for file I/O - use with caution)
 */
static inline void bloom_ext_set_bf(bloom_extended_t *be, uint8_t *bf) {
    if (!be) return;
#if USE_FAST_BLOOM
    if (be->use_fast) {
        be->fast.bf = bf;
        be->orig.bf = bf;  /* Keep in sync */
        return;
    }
#endif
    be->orig.bf = bf;
}

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_WRAPPER_H */
