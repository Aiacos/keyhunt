/*
 * mempool.c - Fast memory pool implementation
 */

#include "mempool.h"
#include <stdlib.h>
#include <string.h>

bool mempool_init(mem_pool_t *p, size_t size) {
    if (!p || size == 0) return false;

    /* Align total size to cache line */
    size = (size + MEMPOOL_CACHE_LINE - 1) & ~(MEMPOOL_CACHE_LINE - 1);

    p->pool = (uint8_t *)aligned_malloc_cacheline(size);
    if (!p->pool) return false;

    p->pool_size = size;
    p->offset = 0;
    p->peak_usage = 0;
    p->owns_memory = true;

    return true;
}

void mempool_init_external(mem_pool_t *p, void *buffer, size_t size) {
    if (!p || !buffer || size == 0) return;

    p->pool = (uint8_t *)buffer;
    p->pool_size = size;
    p->offset = 0;
    p->peak_usage = 0;
    p->owns_memory = false;
}

void* mempool_alloc(mem_pool_t *p, size_t size) {
    return mempool_alloc_aligned(p, size, MEMPOOL_CACHE_LINE);
}

void* mempool_alloc_aligned(mem_pool_t *p, size_t size, size_t alignment) {
    if (!p || !p->pool || size == 0) return NULL;

    /* Ensure alignment is power of 2 */
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        alignment = MEMPOOL_CACHE_LINE;
    }

    /* Align current offset */
    size_t aligned_offset = (p->offset + alignment - 1) & ~(alignment - 1);

    /* Check if we have enough space */
    if (aligned_offset + size > p->pool_size) {
        return NULL;  /* Pool exhausted */
    }

    void *ptr = p->pool + aligned_offset;
    p->offset = aligned_offset + size;

    /* Track peak usage */
    if (p->offset > p->peak_usage) {
        p->peak_usage = p->offset;
    }

    return ptr;
}

void mempool_reset(mem_pool_t *p) {
    if (p) {
        p->offset = 0;
    }
}

void mempool_stats(const mem_pool_t *p, size_t *used, size_t *peak) {
    if (used) *used = p ? p->offset : 0;
    if (peak) *peak = p ? p->peak_usage : 0;
}

void mempool_destroy(mem_pool_t *p) {
    if (p) {
        if (p->owns_memory && p->pool) {
            aligned_free_cacheline(p->pool);
        }
        p->pool = NULL;
        p->pool_size = 0;
        p->offset = 0;
    }
}

void* aligned_malloc_cacheline(size_t size) {
    if (size == 0) return NULL;

    /* Align size up to cache line */
    size = (size + MEMPOOL_CACHE_LINE - 1) & ~(MEMPOOL_CACHE_LINE - 1);

#if defined(_WIN32) || defined(_WIN64)
    return _aligned_malloc(size, MEMPOOL_CACHE_LINE);
#else
    void *ptr = NULL;
    if (posix_memalign(&ptr, MEMPOOL_CACHE_LINE, size) != 0) {
        return NULL;
    }
    return ptr;
#endif
}

void aligned_free_cacheline(void *ptr) {
    if (!ptr) return;

#if defined(_WIN32) || defined(_WIN64)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}
