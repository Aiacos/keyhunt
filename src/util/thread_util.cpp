/*
 * thread_util.cpp - Thread utility functions extracted from keyhunt.cpp
 *
 * These functions are self-contained with no keyhunt.cpp dependencies.
 */

#include "thread_util.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <malloc.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

/* ============================================================================
 * Aligned Memory Allocation
 * ============================================================================ */

void* aligned_calloc(size_t alignment, size_t count, size_t elem_size) {
    if (count != 0 && elem_size > SIZE_MAX / count) return NULL;
    size_t total_size = count * elem_size;
    // Round up to multiple of alignment
    total_size = ((total_size + alignment - 1) / alignment) * alignment;
#if defined(_WIN32) || defined(_WIN64)
    void* ptr = _aligned_malloc(total_size, alignment);
#else
    void* ptr = aligned_alloc(alignment, total_size);
#endif
    if (ptr) {
        memset(ptr, 0, total_size);  // Zero-initialize like calloc
    }
    return ptr;
}

void aligned_free(void *ptr) {
#if defined(_WIN32) || defined(_WIN64)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

/* ============================================================================
 * Thread-Safe Random Number Generation
 * ============================================================================ */

static thread_local unsigned int g_thread_rand_state = 0;
static thread_local bool g_thread_rand_initialized = false;

static inline void thread_rand_init_internal(void) {
    if (!g_thread_rand_initialized) {
        /* Seed with time + thread ID for uniqueness */
#if defined(_WIN32) || defined(_WIN64)
        g_thread_rand_state = (unsigned int)(time(NULL) ^ GetCurrentThreadId() ^ clock());
#else
        g_thread_rand_state = (unsigned int)(time(NULL) ^ (uintptr_t)pthread_self() ^ clock());
#endif
        g_thread_rand_initialized = true;
    }
}

int thread_rand(void) {
    thread_rand_init_internal();
    /* Simple LCG (same as glibc rand) */
    g_thread_rand_state = g_thread_rand_state * 1103515245 + 12345;
    return (int)((g_thread_rand_state >> 16) & 0x7fff);
}

int thread_rand_n(int n) {
    if (n <= 0) return 0;
    return thread_rand() % n;
}

/* ============================================================================
 * Cross-Platform Sleep
 * ============================================================================ */

void sleep_ms(int milliseconds) {
#if defined(_WIN64) && !defined(__CYGWIN__)
    Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#else
    if (milliseconds >= 1000)
      sleep(milliseconds / 1000);
    usleep((milliseconds % 1000) * 1000);
#endif
}
