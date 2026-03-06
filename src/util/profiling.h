/*
 * profiling.h - Performance profiling infrastructure extracted from keyhunt.cpp
 *
 * Provides:
 * - profile_counters_t: Per-thread profiling counters
 * - profile_init_threads(): Allocate per-thread counters
 * - profile_set_thread(): Set thread-local profiling pointer
 * - profile_aggregate(): Aggregate all thread counters
 * - append_profile_info(): Format profiling stats into output buffer
 * - Profiling macros: KH_PROF_SCOPE, KH_PROF_ADD_KEYS, KH_PROF_PTR
 *
 * Usage:
 *   Set g_profile_enabled = true before calling profile_init_threads().
 *   Call profile_set_thread(idx) at the start of each worker thread.
 *   Use KH_PROF_SCOPE(field) for scoped timing measurements.
 */

#ifndef KEYHUNT_PROFILING_H
#define KEYHUNT_PROFILING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Profile Counters
 * ============================================================================ */

typedef struct {
	uint64_t ns_ec;         /* Elliptic curve operations */
	uint64_t ns_hash;       /* Hash computations */
	uint64_t ns_bloom;      /* Bloom filter checks */
	uint64_t ns_binsearch;  /* Binary search lookups */
	uint64_t ns_write;      /* Key write operations */
	uint64_t keys;          /* Total keys processed */
} profile_counters_t;

/* Global profiling enabled flag */
extern bool g_profile_enabled;

/* Thread-local pointer to current thread's counters */
extern thread_local profile_counters_t *tls_prof;

/* ============================================================================
 * Profiling Functions
 * ============================================================================ */

/*
 * Initialize per-thread profiling counter arrays.
 * No-op if profiling is disabled or already initialized.
 *
 * Parameters:
 *   nthreads - Number of worker threads
 */
void profile_init_threads(int nthreads);

/*
 * Set the thread-local profiling pointer for the current thread.
 * Must be called at the start of each worker thread.
 *
 * Parameters:
 *   idx - Thread index (0-based, must be < nthreads from profile_init_threads)
 */
void profile_set_thread(int idx);

/*
 * Aggregate counters from all threads into a single output.
 *
 * Parameters:
 *   out - Output counter struct (zeroed then filled)
 */
void profile_aggregate(profile_counters_t *out);

/*
 * Append profiling statistics to a status output buffer.
 * Shows per-key timing and percentage breakdown by category.
 * No-op if profiling is disabled.
 *
 * Parameters:
 *   buffer     - Output buffer to append to
 *   bufferSize - Total buffer capacity
 */
void append_profile_info(char *buffer, size_t bufferSize);

#ifdef __cplusplus
}

/* ============================================================================
 * Profiling Macros (C++ only, use RAII scope guard)
 * ============================================================================ */

#include "../platform/platform_time.h"

struct kh_profile_scope_t {
	uint64_t start;
	uint64_t *target;
	explicit kh_profile_scope_t(uint64_t *t) : start(0), target(t) {
		if (t) start = platform_time_now_ns();
	}
	~kh_profile_scope_t() {
		if (target) *target += (platform_time_now_ns() - start);
	}
};

#define KH_PROF_PTR() ((__builtin_expect(g_profile_enabled, 0) && tls_prof) ? tls_prof : NULL)
#define KH_PROF_SCOPE(field) kh_profile_scope_t _kh_prof_scope_##__LINE__(KH_PROF_PTR() ? &KH_PROF_PTR()->field : NULL)
#define KH_PROF_ADD_KEYS(n) do { profile_counters_t *p = KH_PROF_PTR(); if (p) p->keys += (uint64_t)(n); } while(0)

#endif /* __cplusplus */

#endif /* KEYHUNT_PROFILING_H */
