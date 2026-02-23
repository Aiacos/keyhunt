/*
 *  Copyright (c) 2012-2019, Jyri J. Virkki
 *  All rights reserved.
 *
 *  This file is under BSD license. See LICENSE file.
 */

/**
 * @file oldbloom.h
 * @brief DEPRECATED: Legacy bloom filter implementation
 *
 * This implementation is DEPRECATED and kept for backward compatibility only.
 * New code should use the modern bloom filter from bloom/bloom.h instead.
 *
 * == MIGRATION GUIDE ==
 *
 * The new bloom filter (bloom/bloom.h) provides:
 * - Better performance: Cache-aligned memory allocation, SIMD prefetching
 * - Memory pool support: Reduced allocation overhead for multiple filters
 * - No internal locking: Thread safety should be managed externally for better performance
 * - Smaller struct size: No mutex/checksum overhead
 *
 * To migrate from oldbloom to bloom:
 *
 * 1. Include the new header:
 *    - OLD: #include "oldbloom/oldbloom.h"
 *    - NEW: #include "bloom/bloom.h"
 *
 * 2. Update struct type:
 *    - OLD: struct oldbloom myfilter;
 *    - NEW: struct bloom myfilter;
 *
 * 3. Update function calls (remove "old" prefix):
 *    - OLD: oldbloom_init2(&filter, entries, error);
 *    - NEW: bloom_init2(&filter, entries, error);
 *
 *    - OLD: oldbloom_add(&filter, data, len);
 *    - NEW: bloom_add(&filter, data, len);
 *
 *    - OLD: oldbloom_check(&filter, data, len);
 *    - NEW: bloom_check(&filter, data, len);
 *
 *    - OLD: oldbloom_free(&filter);
 *    - NEW: bloom_free(&filter);
 *
 * 4. If using thread-safe operations, manage locking externally:
 *    - OLD: (automatic mutex in oldbloom_add)
 *    - NEW: pthread_mutex_lock(&my_mutex);
 *           bloom_add(&filter, data, len);
 *           pthread_mutex_unlock(&my_mutex);
 *
 * 5. For memory pools (multiple filters):
 *    - NEW: bloom_init_with_pool(&filter, entries, error, pool);
 *
 * @deprecated Use bloom/bloom.h for new code. This header will be removed in v4.0.
 */

#ifndef _OLDBLOOM_H
#define _OLDBLOOM_H

/* Mark entire API as deprecated */
#if defined(__GNUC__) || defined(__clang__)
#define OLDBLOOM_DEPRECATED __attribute__((deprecated("Use bloom/bloom.h instead")))
#else
#define OLDBLOOM_DEPRECATED
#endif

#include "../platform/platform.h"

#ifdef __cplusplus
extern "C" {
#endif


/** ***************************************************************************
 * Structure to keep track of one bloom filter.  Caller needs to
 * allocate this and pass it to the functions below. First call for
 * every struct must be to bloom_init().
 *
 */
struct oldbloom
{
  // These fields are part of the public interface of this structure.
  // Client code may read these values if desired. Client code MUST NOT
  // modify any of these.
  uint64_t entries;
  uint64_t bits;
  uint64_t bytes;
  uint8_t hashes;
  long double error;

  // Fields below are private to the implementation. These may go away or
  // change incompatibly at any moment. Client code MUST NOT access or rely
  // on these.
  uint8_t ready;
  uint8_t major;
  uint8_t minor;
  double bpe;
  uint8_t checksum[32];
  uint8_t checksum_backup[32];
  uint8_t *bf;
  platform_mutex_t mutex;
};
/*
Customs
*/
/*
int oldbloom_loadcustom(struct oldbloom * bloom, char * filename);
int oldbloom_savecustom(struct oldbloom * bloom, char * filename);
*/

/** ***************************************************************************
 * Initialize the bloom filter for use.
 *
 * The filter is initialized with a bit field and number of hash functions
 * according to the computations from the wikipedia entry:
 *     http://en.wikipedia.org/wiki/Bloom_filter
 *
 * Optimal number of bits is:
 *     bits = (entries * ln(error)) / ln(2)^2
 *
 * Optimal number of hash functions is:
 *     hashes = bpe * ln(2)
 *
 * Parameters:
 * -----------
 *     bloom   - Pointer to an allocated struct bloom (see above).
 *     entries - The expected number of entries which will be inserted.
 *               Must be at least 1000 (in practice, likely much larger).
 *     error   - Probability of collision (as long as entries are not
 *               exceeded).
 *
 * Return:
 * -------
 *     0 - on success
 *     1 - on failure
 *
 */
OLDBLOOM_DEPRECATED
int oldbloom_init2(struct oldbloom * bloom, uint64_t entries, long double error);


/**
 * DEPRECATED.
 * Kept for compatibility with libbloom v.1. To be removed in v3.0.
 *
 */
OLDBLOOM_DEPRECATED
int oldbloom_init(struct oldbloom * bloom, uint64_t entries, long double error);


/** ***************************************************************************
 * Check if the given element is in the bloom filter. Remember this may
 * return false positive if a collision occurred.
 *
 * Parameters:
 * -----------
 *     bloom  - Pointer to an allocated struct bloom (see above).
 *     buffer - Pointer to buffer containing element to check.
 *     len    - Size of 'buffer'.
 *
 * Return:
 * -------
 *     0 - element is not present
 *     1 - element is present (or false positive due to collision)
 *    -1 - bloom not initialized
 *
 */
OLDBLOOM_DEPRECATED
int oldbloom_check(struct oldbloom * bloom, const void * buffer, int len);


/** ***************************************************************************
 * Add the given element to the bloom filter.
 * The return code indicates if the element (or a collision) was already in,
 * so for the common check+add use case, no need to call check separately.
 *
 * Parameters:
 * -----------
 *     bloom  - Pointer to an allocated struct bloom (see above).
 *     buffer - Pointer to buffer containing element to add.
 *     len    - Size of 'buffer'.
 *
 * Return:
 * -------
 *     0 - element was not present and was added
 *     1 - element (or a collision) had already been added previously
 *    -1 - bloom not initialized
 *
 */
OLDBLOOM_DEPRECATED
int oldbloom_add(struct oldbloom * bloom, const void * buffer, int len);


/** ***************************************************************************
 * Print (to stdout) info about this bloom filter. Debugging aid.
 *
 */
OLDBLOOM_DEPRECATED
void oldbloom_print(struct oldbloom * bloom);


/** ***************************************************************************
 * Deallocate internal storage.
 *
 * Upon return, the bloom struct is no longer usable. You may call bloom_init
 * again on the same struct to reinitialize it again.
 *
 * Parameters:
 * -----------
 *     bloom  - Pointer to an allocated struct bloom (see above).
 *
 * Return: none
 *
 */
OLDBLOOM_DEPRECATED
void oldbloom_free(struct oldbloom * bloom);


/** ***************************************************************************
 * Erase internal storage.
 *
 * Erases all elements. Upon return, the bloom struct returns to its initial
 * (initialized) state.
 *
 * Parameters:
 * -----------
 *     bloom  - Pointer to an allocated struct bloom (see above).
 *
 * Return:
 *     0 - on success
 *     1 - on failure
 *
 */
OLDBLOOM_DEPRECATED
int oldbloom_reset(struct oldbloom * bloom);


/** ***************************************************************************
 * Save a bloom filter to a file.
 *
 * Parameters:
 * -----------
 *     bloom    - Pointer to an allocated struct bloom (see above).
 *     filename - Create (or overwrite) bloom data to this file.
 *
 * Return:
 *     0 - on success
 *     1 - on failure
 *
 */
//int oldbloom_save(struct oldbloom * bloom, char * filename);


/** ***************************************************************************
 * Load a bloom filter from a file.
 *
 * This functions loads a file previously saved with bloom_save().
 *
 * Parameters:
 * -----------
 *     bloom    - Pointer to an allocated struct bloom (see above).
 *     filename - Load bloom filter data from this file.
 *
 * Return:
 *     0   - on success
 *     > 0 - on failure
 *
 */
//int oldbloom_load(struct oldbloom * bloom, char * filename);


/** ***************************************************************************
 * Returns version string compiled into library.
 *
 * Return: version string
 *
 */
OLDBLOOM_DEPRECATED
const char * oldbloom_version();

#ifdef __cplusplus
}
#endif

#endif