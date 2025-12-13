/*
 * Fast Bloom Filter - Optimized for keyhunt BSGS mode
 *
 * Key optimizations:
 * - Single xxhash call (split into multiple indices via bit manipulation)
 * - Power-of-2 bit array for fast modulo via bitmask
 * - Cache-line aligned memory access
 * - Batch checking with prefetching
 * - Early exit on first miss
 */

#ifndef BLOOM_FAST_H
#define BLOOM_FAST_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "../xxhash/xxhash.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cache line size */
#define BLOOM_CACHE_LINE 64

/* Fast bloom filter structure */
typedef struct bloom_fast {
    uint8_t *bf;            /* Bit array (cache-aligned) */
    uint64_t bits;          /* Total bits (power of 2) */
    uint64_t mask;          /* Bitmask for fast modulo */
    uint8_t hashes;         /* Number of hash functions (k) */
    uint8_t log2_bits;      /* log2(bits) for bit extraction */
    bool ready;
} bloom_fast_t;

/*
 * Initialize fast bloom filter
 * bits_log2: log2 of desired bit array size (e.g., 24 for 16M bits = 2MB)
 * hashes: number of hash functions (typically 7-10)
 */
static inline int bloom_fast_init(bloom_fast_t *bf, uint8_t bits_log2, uint8_t hashes) {
    if (!bf || bits_log2 < 10 || bits_log2 > 36 || hashes < 1 || hashes > 16) {
        return -1;
    }

    memset(bf, 0, sizeof(bloom_fast_t));

    bf->log2_bits = bits_log2;
    bf->bits = 1ULL << bits_log2;
    bf->mask = bf->bits - 1;
    bf->hashes = hashes;

    /* Allocate cache-aligned memory */
    size_t bytes = bf->bits / 8;
#ifdef _WIN64
    bf->bf = (uint8_t*)_aligned_malloc(bytes, BLOOM_CACHE_LINE);
#else
    if (posix_memalign((void**)&bf->bf, BLOOM_CACHE_LINE, bytes) != 0) {
        bf->bf = NULL;
    }
#endif

    if (!bf->bf) {
        return -1;
    }

    memset(bf->bf, 0, bytes);
    bf->ready = true;
    return 0;
}

/*
 * Free bloom filter
 */
static inline void bloom_fast_free(bloom_fast_t *bf) {
    if (bf && bf->bf) {
#ifdef _WIN64
        _aligned_free(bf->bf);
#else
        free(bf->bf);
#endif
        bf->bf = NULL;
    }
    if (bf) {
        bf->ready = false;
    }
}

/*
 * Fast check using single hash split into multiple indices
 * Uses xxhash128 to get 128 bits, then extracts multiple indices
 */
static inline int bloom_fast_check(bloom_fast_t *bf, const void *data, int len) {
    if (!bf || !bf->ready) return -1;

    /* Get 128-bit hash */
    XXH128_hash_t h = XXH3_128bits(data, len);

    /* Extract indices from the hash
     * We use different bit ranges for each hash function
     * This avoids multiple hash calls while maintaining good distribution
     */
    uint64_t h1 = h.low64;
    uint64_t h2 = h.high64;

    /* Generate k indices using h1 + i*h2 (Kirsch-Mitzenmacher optimization) */
    for (uint8_t i = 0; i < bf->hashes; i++) {
        uint64_t idx = (h1 + (uint64_t)i * h2) & bf->mask;
        uint64_t byte_idx = idx >> 3;
        uint8_t bit_mask = 1 << (idx & 7);

        if (!(bf->bf[byte_idx] & bit_mask)) {
            return 0;  /* Early exit on first miss */
        }
    }

    return 1;  /* All bits set - potential match */
}

/*
 * Add element to bloom filter
 */
static inline void bloom_fast_add(bloom_fast_t *bf, const void *data, int len) {
    if (!bf || !bf->ready) return;

    XXH128_hash_t h = XXH3_128bits(data, len);
    uint64_t h1 = h.low64;
    uint64_t h2 = h.high64;

    for (uint8_t i = 0; i < bf->hashes; i++) {
        uint64_t idx = (h1 + (uint64_t)i * h2) & bf->mask;
        uint64_t byte_idx = idx >> 3;
        uint8_t bit_mask = 1 << (idx & 7);
        bf->bf[byte_idx] |= bit_mask;
    }
}

/*
 * Batch check - check multiple items with prefetching
 * Returns bitmap of results (bit i = result of item i)
 */
static inline uint64_t bloom_fast_check_batch(
    bloom_fast_t *bf,
    const void **data,
    const int *lens,
    int count)
{
    if (!bf || !bf->ready || count > 64) return 0;

    uint64_t results = 0;

    /* First pass: compute hashes and prefetch */
    XXH128_hash_t hashes[64];
    for (int i = 0; i < count; i++) {
        hashes[i] = XXH3_128bits(data[i], lens[i]);

        /* Prefetch first byte location for each item */
        uint64_t idx = hashes[i].low64 & bf->mask;
        __builtin_prefetch(&bf->bf[idx >> 3], 0, 0);
    }

    /* Second pass: check all bits */
    for (int i = 0; i < count; i++) {
        uint64_t h1 = hashes[i].low64;
        uint64_t h2 = hashes[i].high64;
        bool found = true;

        for (uint8_t j = 0; j < bf->hashes && found; j++) {
            uint64_t idx = (h1 + (uint64_t)j * h2) & bf->mask;
            uint64_t byte_idx = idx >> 3;
            uint8_t bit_mask = 1 << (idx & 7);

            if (!(bf->bf[byte_idx] & bit_mask)) {
                found = false;
            }
        }

        if (found) {
            results |= (1ULL << i);
        }
    }

    return results;
}

/*
 * Fast inline check for single 20-byte hash (RMD160)
 * Optimized specifically for keyhunt address checking
 */
static inline int bloom_fast_check_rmd160(bloom_fast_t *bf, const uint8_t *rmd160) {
    if (!bf || !bf->ready) return -1;

    /* Direct hash of 20 bytes */
    XXH128_hash_t h = XXH3_128bits(rmd160, 20);
    uint64_t h1 = h.low64;
    uint64_t h2 = h.high64;

    /* Unrolled fast path (common k >= 7), but keep correctness for k < 7 */
    const uint8_t k = bf->hashes;
    #define CHECK_BIT(i) do { \
        uint64_t idx = (h1 + (uint64_t)(i) * h2) & bf->mask; \
        if (!(bf->bf[idx >> 3] & (1 << (idx & 7)))) return 0; \
    } while(0)

    if (k >= 1) CHECK_BIT(0);
    if (k >= 2) CHECK_BIT(1);
    if (k >= 3) CHECK_BIT(2);
    if (k >= 4) CHECK_BIT(3);
    if (k >= 5) CHECK_BIT(4);
    if (k >= 6) CHECK_BIT(5);
    if (k >= 7) CHECK_BIT(6);

    /* Handle additional hashes if needed */
    for (uint8_t i = 7; i < k; i++) {
        uint64_t idx = (h1 + (uint64_t)i * h2) & bf->mask;
        if (!(bf->bf[idx >> 3] & (1 << (idx & 7)))) {
            return 0;
        }
    }

    #undef CHECK_BIT

    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_FAST_H */
