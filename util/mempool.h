/*
 * mempool.h - Fast memory pool for hot-path allocations
 *
 * Provides arena-style allocation with instant reset capability.
 * Ideal for temporary allocations that follow a batch pattern.
 */

#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cache line size for alignment */
#define MEMPOOL_CACHE_LINE 64

/* Memory pool structure */
typedef struct {
    uint8_t *pool;          /* Base pool memory */
    size_t pool_size;       /* Total pool size */
    size_t offset;          /* Current allocation offset */
    size_t peak_usage;      /* Track peak usage for sizing */
    bool owns_memory;       /* Did we allocate the pool? */
} mem_pool_t;

/**
 * Initialize a memory pool with given size
 * @param p Pool to initialize
 * @param size Pool size in bytes
 * @return true on success, false on allocation failure
 */
bool mempool_init(mem_pool_t *p, size_t size);

/**
 * Initialize a memory pool using externally allocated memory
 * @param p Pool to initialize
 * @param buffer External memory buffer
 * @param size Buffer size
 */
void mempool_init_external(mem_pool_t *p, void *buffer, size_t size);

/**
 * Allocate memory from pool (cache-line aligned)
 * @param p Pool to allocate from
 * @param size Allocation size
 * @return Pointer to allocated memory, or NULL if pool exhausted
 */
void* mempool_alloc(mem_pool_t *p, size_t size);

/**
 * Allocate memory with specific alignment
 * @param p Pool to allocate from
 * @param size Allocation size
 * @param alignment Alignment requirement (must be power of 2)
 * @return Aligned pointer, or NULL if pool exhausted
 */
void* mempool_alloc_aligned(mem_pool_t *p, size_t size, size_t alignment);

/**
 * Reset pool to initial state (instant "free all")
 * @param p Pool to reset
 */
void mempool_reset(mem_pool_t *p);

/**
 * Get current usage statistics
 * @param p Pool
 * @param used Output: current bytes used
 * @param peak Output: peak bytes used
 */
void mempool_stats(const mem_pool_t *p, size_t *used, size_t *peak);

/**
 * Destroy pool and free memory
 * @param p Pool to destroy
 */
void mempool_destroy(mem_pool_t *p);

/**
 * Allocate cache-line aligned memory (standalone, not from pool)
 * @param size Size to allocate
 * @return Cache-line aligned pointer
 */
void* aligned_malloc_cacheline(size_t size);

/**
 * Free cache-line aligned memory
 * @param ptr Pointer from aligned_malloc_cacheline
 */
void aligned_free_cacheline(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* MEMPOOL_H */
