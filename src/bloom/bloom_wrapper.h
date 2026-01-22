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

#define BLOOM_EXT_FAST_MAJOR 250
#define BLOOM_EXT_FAST_MINOR 1

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
    if (!be) return 1;
    if (entries == 0 || error <= 0 || error >= 1) return 1;

#if USE_FAST_BLOOM
    /* Calculate parameters for fast bloom filter */
    /* bits_per_element = -log(error) / (ln(2)^2) */
    long double bpe = -logl(error) / 0.480453013918201L;

    /* Choose a lower k (fewer random probes) and grow the bit-array to keep the
     * requested error rate. For keyhunt workloads this is typically faster than
     * using the theoretical optimal k, because memory latency dominates. */
    const long double ln2 = 0.693147180559945L;
    uint8_t hashes = (uint8_t)ceill(ln2 * bpe);
    if (hashes < 1) hashes = 1;
    if (hashes > 12) hashes = 12;        /* speed-oriented cap for fast bloom */
    if (hashes > 16) hashes = 16;        /* hard cap for bloom_fast_t */

    uint8_t log2_bits = 0;
    uint64_t p2 = 0;

    /* Compute required m (bits) for chosen k: m = -k*n / ln(1 - p^(1/k)) */
    long double kld = (long double)hashes;
    long double root = powl(error, 1.0L / kld);
    long double denom = logl(1.0L - root); /* negative */
    if (isfinite(denom) && denom < 0) {
        long double m_req = -(kld * (long double)entries) / denom;
        if (m_req < 1024.0L) m_req = 1024.0L;

        uint64_t p2_tmp = 1;
        uint8_t log2_tmp = 0;
        while ((long double)p2_tmp < m_req && log2_tmp < 36) {
            p2_tmp <<= 1;
            log2_tmp++;
        }
        if (log2_tmp < 10) log2_tmp = 10;
        if (log2_tmp <= 36) {
            log2_bits = log2_tmp;
            p2 = 1ULL << log2_bits;
        }
    }

    /* Fallback sizing (original method) if the computed target couldn't be represented. */
    if (p2 == 0) {
        uint64_t total_bits = (uint64_t)(entries * bpe);
        uint64_t p2_tmp = 1;
        uint8_t log2_tmp = 0;
        while (p2_tmp < total_bits && log2_tmp < 36) {
            p2_tmp <<= 1;
            log2_tmp++;
        }
        if (log2_tmp < 10) log2_tmp = 10;
        if (log2_tmp > 36) log2_tmp = 36;
        log2_bits = log2_tmp;
        p2 = 1ULL << log2_bits;
        hashes = (uint8_t)ceill(ln2 * bpe);
        if (hashes < 1) hashes = 1;
        if (hashes > 16) hashes = 16;
    }

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
            be->orig.major = BLOOM_EXT_FAST_MAJOR;
            be->orig.minor = BLOOM_EXT_FAST_MINOR;
            return 0;
        }
    }

    /* Fall back to original implementation */
    be->use_fast = false;
#endif

    return bloom_init2(&be->orig, entries, error);
}

static inline uint8_t bloom_ext_log2_u64(uint64_t v) {
    uint8_t n = 0;
    while (v > 1) { v >>= 1; n++; }
    return n;
}

/* Reconstruct be->fast/be->use_fast from be->orig metadata (used when loading caches). */
static inline void bloom_ext_sync_from_orig(bloom_extended_t *be) {
    if (!be) return;
#if USE_FAST_BLOOM
    if (be->orig.major == BLOOM_EXT_FAST_MAJOR && be->orig.minor == BLOOM_EXT_FAST_MINOR &&
        be->orig.bf != NULL && be->orig.bits != 0 &&
        (be->orig.bits & (be->orig.bits - 1)) == 0 &&
        be->orig.hashes >= 1 && be->orig.hashes <= 16) {
        be->use_fast = true;
        be->fast.bf = be->orig.bf;
        be->fast.bits = be->orig.bits;
        be->fast.mask = be->fast.bits - 1;
        be->fast.hashes = be->orig.hashes;
        be->fast.log2_bits = bloom_ext_log2_u64(be->fast.bits);
        be->fast.ready = true;
        return;
    }
    be->use_fast = false;
    be->fast.ready = false;
    be->fast.bf = NULL;
#else
    (void)be;
#endif
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
 * Strided batch check for contiguous 20-byte hashes (count <= 64).
 */
static inline uint64_t bloom_ext_check_rmd160_strided(
    bloom_extended_t *be,
    const uint8_t *base,
    size_t stride,
    int count)
{
    if (!be || !base || stride < 20 || count <= 0) return 0;
#if USE_FAST_BLOOM
    if (be->use_fast) {
        return bloom_fast_check_rmd160_strided(&be->fast, base, stride, count);
    }
#endif
    uint64_t results = 0;
    const int limit = (count > 64) ? 64 : count;
    for (int i = 0; i < limit; i++) {
        if (bloom_check(&be->orig, base + (size_t)i * stride, 20)) {
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
