#ifndef SEARCH_UTILS_H
#define SEARCH_UTILS_H

/*
 * search_utils.h — Shared inline helper functions for search modules.
 *
 * All functions are declared `inline` (not `static inline`) so they can
 * be included from multiple compilation units without ODR violations.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN64) && !defined(__CYGWIN__)
#include <windows.h>
#include <malloc.h>
#else
#include <sys/time.h>
#endif

/* ------------------------------------------------------------------ */
/*  Memory helpers                                                     */
/* ------------------------------------------------------------------ */

/**
 * Allocate zero-initialised memory with the requested alignment.
 * Falls back to regular calloc when aligned allocation is unavailable.
 * Returns NULL on failure.
 */
inline void *aligned_calloc(size_t alignment, size_t nmemb, size_t size) {
	size_t total = nmemb * size;
	void *ptr = nullptr;
#if defined(_WIN64) && !defined(__CYGWIN__)
	ptr = _aligned_malloc(total, alignment);
	if (ptr) memset(ptr, 0, total);
#else
	if (posix_memalign(&ptr, alignment, total) != 0)
		return nullptr;
	memset(ptr, 0, total);
#endif
	return ptr;
}

/* ------------------------------------------------------------------ */
/*  Big-endian load helpers                                            */
/* ------------------------------------------------------------------ */

/** Load a 64-bit value from big-endian bytes. */
inline uint64_t load_u64_be(const uint8_t *p) {
	return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
	       ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
	       ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
	       ((uint64_t)p[6] <<  8) | ((uint64_t)p[7]);
}

/** Load a 32-bit value from big-endian bytes. */
inline uint32_t load_u32_be(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] <<  8) | ((uint32_t)p[3]);
}

/* ------------------------------------------------------------------ */
/*  Hash / comparison helpers                                          */
/* ------------------------------------------------------------------ */

/**
 * Compare two 20-byte RIPEMD-160 hashes.
 * Returns 0 if equal, negative if a < b, positive if a > b.
 */
inline int cmp_hash20(const uint8_t *a, const uint8_t *b) {
	return memcmp(a, b, 20);
}

/* ------------------------------------------------------------------ */
/*  Arithmetic helpers                                                 */
/* ------------------------------------------------------------------ */

/**
 * If *val >= sub, subtract sub from *val and return true.
 * Otherwise leave *val unchanged and return false.
 */
inline bool sub_u64_if_fits(uint64_t *val, uint64_t sub) {
	if (*val >= sub) {
		*val -= sub;
		return true;
	}
	return false;
}

/**
 * Compute (a - b) and store in *out if the result fits in a single uint64_t
 * (i.e., upper 192 bits of the difference are zero). Returns true on success.
 * Requires secp256k1/Int.h.
 */
#ifdef BIGINTH  /* Only available when secp256k1/Int.h has been included */
inline bool int_sub_to_u64(const Int &a, const Int &b, uint64_t *out) {
	if (!out) return false;
	uint64_t d0 = a.bits64[0] - b.bits64[0];
	uint64_t borrow = (a.bits64[0] < b.bits64[0]) ? 1ULL : 0ULL;
	for (int i = 1; i < NB64BLOCK; i++) {
		const uint64_t ai = a.bits64[i];
		const uint64_t bi = b.bits64[i];
		const uint64_t bi_borrow = bi + borrow;
		const uint64_t di = ai - bi_borrow;
		if (di != 0) return false;
		borrow = (ai < bi_borrow) ? 1ULL : 0ULL;
	}
	if (borrow) return false;
	*out = d0;
	return true;
}
#endif

/* ------------------------------------------------------------------ */
/*  Per-thread PRNG  (xoshiro256**)                                    */
/* ------------------------------------------------------------------ */

struct thread_rand_state {
	uint64_t s[4];
};

/** Rotate left helper for xoshiro. */
inline uint64_t _rotl64(uint64_t x, int k) {
	return (x << k) | (x >> (64 - k));
}

/** Initialise a per-thread PRNG state from a seed. */
inline void thread_rand_init(struct thread_rand_state *st, uint64_t seed) {
	/* SplitMix64 to expand a single seed into 4 state words */
	for (int i = 0; i < 4; i++) {
		seed += 0x9e3779b97f4a7c15ULL;
		uint64_t z = seed;
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
		z = z ^ (z >> 31);
		st->s[i] = z;
	}
}

/** Generate a random 64-bit value (xoshiro256**). */
inline uint64_t thread_rand(struct thread_rand_state *st) {
	const uint64_t result = _rotl64(st->s[1] * 5, 7) * 9;
	const uint64_t t = st->s[1] << 17;

	st->s[2] ^= st->s[0];
	st->s[3] ^= st->s[1];
	st->s[1] ^= st->s[2];
	st->s[0] ^= st->s[3];

	st->s[2] ^= t;
	st->s[3] = _rotl64(st->s[3], 45);

	return result;
}

/** Generate a random value in [0, n). */
inline uint64_t thread_rand_n(struct thread_rand_state *st, uint64_t n) {
	if (n == 0) return 0;
	return thread_rand(st) % n;
}

/* ------------------------------------------------------------------ */
/*  Environment helpers                                                */
/* ------------------------------------------------------------------ */

/** Return true if the environment variable `name` is set to a truthy value. */
inline bool env_truthy_kh(const char *name) {
	const char *v = getenv(name);
	if (!v) return false;
	return (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' ||
	        v[0] == 't' || v[0] == 'T');
}

/* ------------------------------------------------------------------ */
/*  Profiling helpers                                                  */
/*  NOTE: keyhunt.cpp defines its own profiling system with a          */
/*  different profile_counters_t layout.  This section is opt-in       */
/*  (#define SEARCH_UTILS_PROFILING before including) to avoid         */
/*  name collisions.                                                   */
/* ------------------------------------------------------------------ */

#ifdef SEARCH_UTILS_PROFILING

struct su_profile_counters_t {
	uint64_t hash_ops;
	uint64_t bloom_checks;
	uint64_t ec_ops;
	uint64_t elapsed_ns;
};

#ifndef SEARCH_UTILS_MAX_THREADS
#define SEARCH_UTILS_MAX_THREADS 256
#endif

inline thread_local int _su_profile_thread_idx = -1;
inline su_profile_counters_t _su_profile_counters[SEARCH_UTILS_MAX_THREADS] = {};
inline int _su_profile_num_threads = 0;

inline uint64_t su_profile_now_ns(void) {
#if defined(_WIN64) && !defined(__CYGWIN__)
	LARGE_INTEGER freq, cnt;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&cnt);
	return (uint64_t)((double)cnt.QuadPart / freq.QuadPart * 1e9);
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

inline void su_profile_init_threads(int n) {
	if (n > SEARCH_UTILS_MAX_THREADS) n = SEARCH_UTILS_MAX_THREADS;
	_su_profile_num_threads = n;
	memset(_su_profile_counters, 0, sizeof(su_profile_counters_t) * n);
}

inline void su_profile_set_thread(int idx) {
	_su_profile_thread_idx = idx;
}

inline void su_profile_aggregate(su_profile_counters_t *out) {
	memset(out, 0, sizeof(*out));
	for (int i = 0; i < _su_profile_num_threads; i++) {
		out->hash_ops     += _su_profile_counters[i].hash_ops;
		out->bloom_checks += _su_profile_counters[i].bloom_checks;
		out->ec_ops       += _su_profile_counters[i].ec_ops;
		out->elapsed_ns   += _su_profile_counters[i].elapsed_ns;
	}
}

#endif /* SEARCH_UTILS_PROFILING */

#endif /* SEARCH_UTILS_H */
