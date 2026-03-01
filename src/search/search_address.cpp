/*
 * search_address.cpp - ADDRESS, RMD160, and XPOINT mode search implementation
 *
 * MIGRATION STATUS: Config-wired (thread_args)
 *
 * This file contains the main CPU search thread and batch processing helpers:
 * - thread_process(): Main search thread for ADDRESS, RMD160, XPOINT modes
 * - process_rmd160_batch_btc_simple(): Optimized SIMD batch processing for BTC
 * - cpu_use_y_parity_for_compressed_btc(): Y parity optimization check
 *
 * The thread_process() function generates batches of public keys using the
 * group law optimization (Shamir's trick) and checks them against target
 * addresses/hashes using bloom filters and binary search.
 *
 * Dependencies:
 * - secp256k1 library for elliptic curve operations
 * - bloom filter for fast target lookups
 * - SIMD hash functions (SHA256, RIPEMD160)
 *
 * See search_common.h for shared declarations.
 */

#include "search_common.h"
/* Prevent bsgs_xvalue redefinition: search_common.h already defines it
 * conditionally, so ensure bsgs_sort.h's guard is set before search_context.h
 * tries to include it again. */
#ifndef BSGS_SORT_H
#define BSGS_SORT_H
#endif
#include "search_context.h"
#include "search_utils.h"
#include "search_xpoint.h"
#include "search_rmd160.h"
#include "../output.h"
#include "../io/io.h"
#include "../gpu/gpu_backend.h"
#include "../core/sysinfo.h"
#include "../core/workpool.h"
#include "../sort/sort.h"
/* WorkQueue is only used in acquire_base_key() which remains in keyhunt.cpp */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <atomic>
#include <time.h>

/* ============================================================================
 * External Dependencies (defined in keyhunt.cpp)
 *
 * Infrastructure externs that remain (not config-mapped):
 * - g_sysinfo: hardware detection structure
 * - g_work_pool / cpu_cached_block_*: work-stealing infrastructure
 * - g_profile_enabled / tls_prof / profile_set_thread: profiling
 * - acquire_base_key: key acquisition from work pool
 * ============================================================================ */

extern system_info_t g_sysinfo;

/* Work pool / work queue (defined in keyhunt.cpp) */
extern WorkPool g_work_pool;

/* Thread-local block cache for work-stealing */
extern thread_local Int cpu_cached_block_start;
extern thread_local Int cpu_cached_block_end;
extern thread_local bool cpu_cached_block_valid;

/* ============================================================================
 * Profiling Support
 *
 * These mirror the profiling macros defined in keyhunt.cpp.
 * We need local copies since the originals are file-static.
 * The profile_counters_t matches keyhunt.cpp's profile_counters_t
 * layout (which differs from search_utils.h's version).
 * ============================================================================ */

typedef struct {
	uint64_t ns_ec;
	uint64_t ns_hash;
	uint64_t ns_bloom;
	uint64_t ns_binsearch;
	uint64_t ns_write;
	uint64_t keys;
} profile_counters_t;

extern bool g_profile_enabled;
extern thread_local profile_counters_t *tls_prof;

/* High-resolution monotonic time in nanoseconds */
static inline uint64_t kh_profile_now_ns(void) {
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

struct kh_profile_scope_t {
	uint64_t start;
	uint64_t *target;
	explicit kh_profile_scope_t(uint64_t *t) : start(0), target(t) {
		if (t) start = kh_profile_now_ns();
	}
	~kh_profile_scope_t() {
		if (target) *target += (kh_profile_now_ns() - start);
	}
};

#define KH_PROF_PTR() ((__builtin_expect(g_profile_enabled, 0) && tls_prof) ? tls_prof : NULL)
#define KH_PROF_SCOPE(field) kh_profile_scope_t _kh_prof_scope_##__LINE__(KH_PROF_PTR() ? &KH_PROF_PTR()->field : NULL)
#define KH_PROF_ADD_KEYS(n) do { profile_counters_t *p = KH_PROF_PTR(); if (p) p->keys += (uint64_t)(n); } while(0)

/* profile_set_thread: sets tls_prof for the current thread. Defined in keyhunt.cpp. */
extern void profile_set_thread(int idx);

/* int_sub_to_u64: defined in search_utils.h (requires BIGINTH) */

/* ============================================================================
 * cpu_use_y_parity_for_compressed_btc
 *
 * Determine whether to use Y parity optimization for compressed-only BTC
 * search. This avoids computing hashes for both 02/03 prefixes when we
 * can determine the actual parity from the Y coordinate.
 *
 * Parameters are passed explicitly instead of reading extern globals.
 * ============================================================================ */

static inline bool cpu_use_y_parity_for_compressed_btc(
	int flagmode, int flagcrypto, int flagendomorphism,
	int flagsearch, bool gpu_hybrid, bool gpu_full) {

	if (!((flagmode == MODE_ADDRESS || flagmode == MODE_RMD160) &&
		  flagcrypto == CRYPTO_BTC &&
		  !flagendomorphism &&
		  flagsearch == SEARCH_COMPRESS)) {
		return false;
	}

	if (gpu_hybrid && gpu_full) {
		const char *env = getenv("KEYHUNT_HYBRID_CPU_USE_Y");
		if (env && *env) {
			return atoi(env) != 0;
		}
		return true;
	}

		const char *env = getenv("KEYHUNT_CPU_USE_Y");
		if (env && *env) {
			return atoi(env) != 0;
		}
		return true;
	}

/* ============================================================================
 * acquire_base_key (extern, defined in keyhunt.cpp)
 * ============================================================================ */

extern bool acquire_base_key(Int &key);

/* ============================================================================
 * process_rmd160_batch_btc_simple
 *
 * Optimized batch processing for RMD160/ADDRESS mode with BTC crypto
 * when endomorphism is disabled. Uses AVX512/AVX2/SSE2 SIMD paths
 * for hashing and batch bloom filter checking for target matching.
 *
 * Config-wired: receives keyhunt_config_t* for bloom, addressTable, etc.
 * ============================================================================ */

static void process_rmd160_batch_btc_simple(keyhunt_config_t *config, Int &key_mpz, Point *pts, uint64_t &count) {
	/* Extract config-mapped state */
	int FLAGSEARCH = (config->search.key_format == KEYTYPE_COMPRESSED) ? SEARCH_COMPRESS :
	                 (config->search.key_format == KEYTYPE_UNCOMPRESSED) ? SEARCH_UNCOMPRESS :
	                 SEARCH_BOTH;
	uint64_t N = (uint64_t)config->runtime.address_count;
	bloom_extended_t *bloom = (bloom_extended_t *)config->runtime.bloom_filter;
	Int &stride = *(Int *)config->runtime.stride;
	struct address_value *addressTable = (struct address_value *)config->runtime.address_table;
	bool g_avx2_available = config->autotune.has_avx2;
	int FLAGGPU = config->gpu.enabled;
	bool FLAGGPU_FULL = config->gpu.full_mode;

	const bool wantCompressed = (FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH);
	const bool wantUncompressed = (FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH);
	const bool haveYForCompressed = (wantUncompressed || cpu_use_y_parity_for_compressed_btc(
		(int)config->search.mode, (int)config->search.crypto_type,
		config->search.endomorphism ? 1 : 0, FLAGSEARCH,
		config->gpu.hybrid_mode, FLAGGPU_FULL));

	if(!wantCompressed && !wantUncompressed) {
		return;
	}

	alignas(32) char hashCompressed02[CPU_GRP_SIZE][20];
	alignas(32) char hashCompressed03[CPU_GRP_SIZE][20];
	alignas(32) char hashUncompressed[CPU_GRP_SIZE][20];

				if (wantCompressed) {
					if (haveYForCompressed) {
						KH_PROF_SCOPE(ns_hash);
						// Y is available: compute only the actual compressed hash (single parity)
						if (g_sysinfo.has_avx512) {
							for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 16) {
								secp->GetHash160_AVX512(P2PKH, true,
									pts[idx], pts[idx + 1], pts[idx + 2], pts[idx + 3],
									pts[idx + 4], pts[idx + 5], pts[idx + 6], pts[idx + 7],
									pts[idx + 8], pts[idx + 9], pts[idx + 10], pts[idx + 11],
									pts[idx + 12], pts[idx + 13], pts[idx + 14], pts[idx + 15],
									(uint8_t*)hashCompressed02[idx], (uint8_t*)hashCompressed02[idx + 1],
									(uint8_t*)hashCompressed02[idx + 2], (uint8_t*)hashCompressed02[idx + 3],
									(uint8_t*)hashCompressed02[idx + 4], (uint8_t*)hashCompressed02[idx + 5],
									(uint8_t*)hashCompressed02[idx + 6], (uint8_t*)hashCompressed02[idx + 7],
									(uint8_t*)hashCompressed02[idx + 8], (uint8_t*)hashCompressed02[idx + 9],
									(uint8_t*)hashCompressed02[idx + 10], (uint8_t*)hashCompressed02[idx + 11],
									(uint8_t*)hashCompressed02[idx + 12], (uint8_t*)hashCompressed02[idx + 13],
									(uint8_t*)hashCompressed02[idx + 14], (uint8_t*)hashCompressed02[idx + 15]);
							}
						} else if (g_avx2_available) {
							for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 8) {
								secp->GetHash160_AVX2(P2PKH, true,
									pts[idx], pts[idx + 1], pts[idx + 2], pts[idx + 3],
									pts[idx + 4], pts[idx + 5], pts[idx + 6], pts[idx + 7],
									(uint8_t*)hashCompressed02[idx], (uint8_t*)hashCompressed02[idx + 1],
									(uint8_t*)hashCompressed02[idx + 2], (uint8_t*)hashCompressed02[idx + 3],
									(uint8_t*)hashCompressed02[idx + 4], (uint8_t*)hashCompressed02[idx + 5],
									(uint8_t*)hashCompressed02[idx + 6], (uint8_t*)hashCompressed02[idx + 7]);
							}
						} else {
							for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 4) {
								secp->GetHash160(P2PKH, true,
									pts[idx], pts[idx + 1], pts[idx + 2], pts[idx + 3],
									(uint8_t*)hashCompressed02[idx], (uint8_t*)hashCompressed02[idx + 1],
									(uint8_t*)hashCompressed02[idx + 2], (uint8_t*)hashCompressed02[idx + 3]);
							}
						}
					} else {
					// Compressed-only: Y is not computed, so check both parities from X.
					if (FLAGGPU == 1 && FLAGGPU_FULL == 0 && gpu_backend_available()) {
						KH_PROF_SCOPE(ns_hash);
						alignas(32) uint8_t x32_be[CPU_GRP_SIZE * 32];
						for (size_t idx = 0; idx < CPU_GRP_SIZE; ++idx) {
							pts[idx].x.Get32Bytes(x32_be + idx * 32);
						}
					if (gpu_hash160_fromX_batch(x32_be, CPU_GRP_SIZE,
							(uint8_t*)hashCompressed02[0], (uint8_t*)hashCompressed03[0]) != 0) {
						output_warning("GPU hash160 failed; falling back to CPU.\n");
						goto cpu_compress_only_hash;
					}
				} else {
cpu_compress_only_hash:
				KH_PROF_SCOPE(ns_hash);
				if (g_sysinfo.has_avx512) {
					for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 16) {
						secp->GetHash160_fromX_02_03_AVX512(P2PKH,
							&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
							&pts[idx + 4].x, &pts[idx + 5].x, &pts[idx + 6].x, &pts[idx + 7].x,
							&pts[idx + 8].x, &pts[idx + 9].x, &pts[idx + 10].x, &pts[idx + 11].x,
							&pts[idx + 12].x, &pts[idx + 13].x, &pts[idx + 14].x, &pts[idx + 15].x,
							(uint8_t*)hashCompressed02[idx], (uint8_t*)hashCompressed02[idx + 1],
							(uint8_t*)hashCompressed02[idx + 2], (uint8_t*)hashCompressed02[idx + 3],
							(uint8_t*)hashCompressed02[idx + 4], (uint8_t*)hashCompressed02[idx + 5],
							(uint8_t*)hashCompressed02[idx + 6], (uint8_t*)hashCompressed02[idx + 7],
							(uint8_t*)hashCompressed02[idx + 8], (uint8_t*)hashCompressed02[idx + 9],
							(uint8_t*)hashCompressed02[idx + 10], (uint8_t*)hashCompressed02[idx + 11],
							(uint8_t*)hashCompressed02[idx + 12], (uint8_t*)hashCompressed02[idx + 13],
							(uint8_t*)hashCompressed02[idx + 14], (uint8_t*)hashCompressed02[idx + 15],
							(uint8_t*)hashCompressed03[idx], (uint8_t*)hashCompressed03[idx + 1],
							(uint8_t*)hashCompressed03[idx + 2], (uint8_t*)hashCompressed03[idx + 3],
							(uint8_t*)hashCompressed03[idx + 4], (uint8_t*)hashCompressed03[idx + 5],
							(uint8_t*)hashCompressed03[idx + 6], (uint8_t*)hashCompressed03[idx + 7],
							(uint8_t*)hashCompressed03[idx + 8], (uint8_t*)hashCompressed03[idx + 9],
							(uint8_t*)hashCompressed03[idx + 10], (uint8_t*)hashCompressed03[idx + 11],
							(uint8_t*)hashCompressed03[idx + 12], (uint8_t*)hashCompressed03[idx + 13],
							(uint8_t*)hashCompressed03[idx + 14], (uint8_t*)hashCompressed03[idx + 15]);
					}
				} else if (g_avx2_available) {
					for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 8) {
						secp->GetHash160_fromX_02_03_AVX2(P2PKH,
							&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
							&pts[idx + 4].x, &pts[idx + 5].x, &pts[idx + 6].x, &pts[idx + 7].x,
							(uint8_t*)hashCompressed02[idx], (uint8_t*)hashCompressed02[idx + 1],
							(uint8_t*)hashCompressed02[idx + 2], (uint8_t*)hashCompressed02[idx + 3],
							(uint8_t*)hashCompressed02[idx + 4], (uint8_t*)hashCompressed02[idx + 5],
							(uint8_t*)hashCompressed02[idx + 6], (uint8_t*)hashCompressed02[idx + 7],
							(uint8_t*)hashCompressed03[idx], (uint8_t*)hashCompressed03[idx + 1],
							(uint8_t*)hashCompressed03[idx + 2], (uint8_t*)hashCompressed03[idx + 3],
							(uint8_t*)hashCompressed03[idx + 4], (uint8_t*)hashCompressed03[idx + 5],
						(uint8_t*)hashCompressed03[idx + 6], (uint8_t*)hashCompressed03[idx + 7]);
					}
				} else {
					for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 4) {
						secp->GetHash160_fromX_02_03(P2PKH,
							&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
							(uint8_t*)hashCompressed02[idx], (uint8_t*)hashCompressed02[idx + 1],
							(uint8_t*)hashCompressed02[idx + 2], (uint8_t*)hashCompressed02[idx + 3],
							(uint8_t*)hashCompressed03[idx], (uint8_t*)hashCompressed03[idx + 1],
							(uint8_t*)hashCompressed03[idx + 2], (uint8_t*)hashCompressed03[idx + 3]);
					}
				}
				}
			}
		}

	if (wantUncompressed) {
		KH_PROF_SCOPE(ns_hash);
		if (g_sysinfo.has_avx512) {
			for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 16) {
				secp->GetHash160_AVX512(P2PKH, false,
					pts[idx], pts[idx + 1], pts[idx + 2], pts[idx + 3],
					pts[idx + 4], pts[idx + 5], pts[idx + 6], pts[idx + 7],
					pts[idx + 8], pts[idx + 9], pts[idx + 10], pts[idx + 11],
					pts[idx + 12], pts[idx + 13], pts[idx + 14], pts[idx + 15],
					(uint8_t*)hashUncompressed[idx], (uint8_t*)hashUncompressed[idx + 1],
					(uint8_t*)hashUncompressed[idx + 2], (uint8_t*)hashUncompressed[idx + 3],
					(uint8_t*)hashUncompressed[idx + 4], (uint8_t*)hashUncompressed[idx + 5],
					(uint8_t*)hashUncompressed[idx + 6], (uint8_t*)hashUncompressed[idx + 7],
					(uint8_t*)hashUncompressed[idx + 8], (uint8_t*)hashUncompressed[idx + 9],
					(uint8_t*)hashUncompressed[idx + 10], (uint8_t*)hashUncompressed[idx + 11],
					(uint8_t*)hashUncompressed[idx + 12], (uint8_t*)hashUncompressed[idx + 13],
					(uint8_t*)hashUncompressed[idx + 14], (uint8_t*)hashUncompressed[idx + 15]);
			}
		} else if (g_avx2_available) {
			for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 8) {
				secp->GetHash160_AVX2(P2PKH,false,
					pts[idx], pts[idx + 1], pts[idx + 2], pts[idx + 3],
					pts[idx + 4], pts[idx + 5], pts[idx + 6], pts[idx + 7],
					(uint8_t*)hashUncompressed[idx], (uint8_t*)hashUncompressed[idx + 1],
					(uint8_t*)hashUncompressed[idx + 2], (uint8_t*)hashUncompressed[idx + 3],
					(uint8_t*)hashUncompressed[idx + 4], (uint8_t*)hashUncompressed[idx + 5],
					(uint8_t*)hashUncompressed[idx + 6], (uint8_t*)hashUncompressed[idx + 7]);
			}
		} else {
			for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 4) {
				secp->GetHash160(P2PKH,false,
					pts[idx], pts[idx + 1], pts[idx + 2], pts[idx + 3],
					(uint8_t*)hashUncompressed[idx], (uint8_t*)hashUncompressed[idx + 1],
					(uint8_t*)hashUncompressed[idx + 2], (uint8_t*)hashUncompressed[idx + 3]);
			}
		}
	}

	Point publickey;
	const bool direct_single_target = (N == 1);
	const uint8_t *single_target = direct_single_target ? addressTable[0].value : nullptr;

	// Batch bloom filter checking - process 64 hashes at a time
	const size_t BATCH_SIZE = 64;
	const size_t HASH_STRIDE = 20;

	// Helper lambda to compute key at index using O(1) multiplication
	auto computeKeyAtIndex = [&key_mpz, &stride](Int &out, size_t idx) {
		Int offset;
		offset.SetInt64((int64_t)idx);
		offset.Mult(&stride);
		out.Set(&key_mpz);
		out.Add(&offset);
	};

			for (size_t base = 0; base < CPU_GRP_SIZE; base += BATCH_SIZE) {
				const size_t batchEnd = (base + BATCH_SIZE > CPU_GRP_SIZE) ? CPU_GRP_SIZE : base + BATCH_SIZE;
				const int batchCount = (int)(batchEnd - base);

				if (wantCompressed) {
					if (haveYForCompressed) {
						if (direct_single_target) {
							for (size_t idx = base; idx < batchEnd; ++idx) {
								if (memcmp(hashCompressed02[idx], single_target, 20) == 0) {
									Int candidate;
									computeKeyAtIndex(candidate, idx);
									writekey(true, &candidate);
								}
							}
						} else {
							uint64_t hits;
							{
								KH_PROF_SCOPE(ns_bloom);
								hits = bloom_ext_check_rmd160_strided(bloom, (const uint8_t*)hashCompressed02[base], HASH_STRIDE, batchCount);
							}
							while (hits) {
								int i = __builtin_ctzll(hits);
								hits &= hits - 1;
								size_t idx = base + (size_t)i;
								int found;
								{
									KH_PROF_SCOPE(ns_binsearch);
									found = searchbinary(addressTable, hashCompressed02[idx], N);
								}
								if (found) {
									KH_PROF_SCOPE(ns_write);
									Int candidate;
									computeKeyAtIndex(candidate, idx);
									writekey(true, &candidate);
								}
							}
						}
					} else {
						if (direct_single_target) {
							for (size_t idx = base; idx < batchEnd; ++idx) {
								if (memcmp(hashCompressed02[idx], single_target, 20) == 0) {
									Int candidate;
									computeKeyAtIndex(candidate, idx);
									publickey = secp->ComputePublicKey(&candidate);
									if (publickey.y.IsOdd()) {
										candidate.Neg();
										candidate.Add(&secp->order);
									}
									writekey(true, &candidate);
								}
								if (memcmp(hashCompressed03[idx], single_target, 20) == 0) {
									Int candidate;
									computeKeyAtIndex(candidate, idx);
									publickey = secp->ComputePublicKey(&candidate);
									if (publickey.y.IsEven()) {
										candidate.Neg();
										candidate.Add(&secp->order);
									}
									writekey(true, &candidate);
								}
							}
						} else {
							uint64_t hits02;
							{
								KH_PROF_SCOPE(ns_bloom);
								hits02 = bloom_ext_check_rmd160_strided(bloom, (const uint8_t*)hashCompressed02[base], HASH_STRIDE, batchCount);
							}
							while (hits02) {
								int i = __builtin_ctzll(hits02);
								hits02 &= hits02 - 1;
								size_t idx = base + (size_t)i;
								int found;
								{
									KH_PROF_SCOPE(ns_binsearch);
									found = searchbinary(addressTable, hashCompressed02[idx], N);
								}
								if (found) {
									KH_PROF_SCOPE(ns_write);
									Int candidate;
									computeKeyAtIndex(candidate, idx);
									publickey = secp->ComputePublicKey(&candidate);
									if (publickey.y.IsOdd()) {
										candidate.Neg();
										candidate.Add(&secp->order);
									}
									writekey(true, &candidate);
								}
							}
							uint64_t hits03;
							{
								KH_PROF_SCOPE(ns_bloom);
								hits03 = bloom_ext_check_rmd160_strided(bloom, (const uint8_t*)hashCompressed03[base], HASH_STRIDE, batchCount);
							}
							while (hits03) {
								int i = __builtin_ctzll(hits03);
								hits03 &= hits03 - 1;
								size_t idx = base + (size_t)i;
								int found;
								{
									KH_PROF_SCOPE(ns_binsearch);
									found = searchbinary(addressTable, hashCompressed03[idx], N);
								}
								if (found) {
									KH_PROF_SCOPE(ns_write);
									Int candidate;
									computeKeyAtIndex(candidate, idx);
									publickey = secp->ComputePublicKey(&candidate);
									if (publickey.y.IsEven()) {
										candidate.Neg();
										candidate.Add(&secp->order);
									}
									writekey(true, &candidate);
								}
							}
						}
					}
				}

			if (wantUncompressed) {
				if (direct_single_target) {
					for (size_t idx = base; idx < batchEnd; ++idx) {
						if (memcmp(hashUncompressed[idx], single_target, 20) == 0) {
						Int candidate;
						computeKeyAtIndex(candidate, idx);
						writekey(false, &candidate);
					}
				}
			} else {
				uint64_t hitsU;
				{
					KH_PROF_SCOPE(ns_bloom);
					hitsU = bloom_ext_check_rmd160_strided(bloom, (const uint8_t*)hashUncompressed[base], HASH_STRIDE, batchCount);
				}
				while (hitsU) {
					int i = __builtin_ctzll(hitsU);
					hitsU &= hitsU - 1;
					size_t idx = base + i;
					int found;
					{
						KH_PROF_SCOPE(ns_binsearch);
						found = searchbinary(addressTable, hashUncompressed[idx], N);
					}
					if (found) {
						KH_PROF_SCOPE(ns_write);
						Int candidate;
						computeKeyAtIndex(candidate, idx);
						writekey(false, &candidate);
					}
				}
			}
		}
	}

	// Advance key_mpz by CPU_GRP_SIZE * stride
	Int strideTotal;
	strideTotal.SetInt32(CPU_GRP_SIZE);
	strideTotal.Mult(&stride);
	key_mpz.Add(&strideTotal);
	count += CPU_GRP_SIZE;
	KH_PROF_ADD_KEYS(CPU_GRP_SIZE);
}

/* ============================================================================
 * thread_process - Main search thread for ADDRESS, RMD160, XPOINT modes
 *
 * This is the primary CPU search thread. It generates batches of public keys
 * using the group law optimization (Shamir's trick) and checks them against
 * target addresses/hashes using bloom filters and binary search.
 *
 * Config-wired: receives thread_args with keyhunt_config_t* pointer.
 * ============================================================================ */

platform_thread_return_t PLATFORM_THREAD_CALL thread_process(void *vargp) {
	/* Extract config and thread ID from thread_args */
	thread_args *args = (thread_args *)vargp;
	keyhunt_config_t *config = args->config;
	int thread_number = args->thread_id;
	delete args;

	/* ---- Config-derived local variables ---- */

	/* Search config (immutable during search) */
	int local_mode = (int)config->search.mode;
	int local_search = (config->search.key_format == KEYTYPE_COMPRESSED) ? SEARCH_COMPRESS :
	                   (config->search.key_format == KEYTYPE_UNCOMPRESSED) ? SEARCH_UNCOMPRESS :
	                   SEARCH_BOTH;
	int local_crypto = (int)config->search.crypto_type;
	bool local_endomorphism = config->search.endomorphism;
	bool local_random = config->search.random_mode;
	bool local_quiet = config->search.quiet_mode;
	bool local_matrix = config->search.matrix_mode;

	/* Runtime state */
	Secp256K1 *secp = (Secp256K1 *)config->runtime.secp;
	bloom_extended_t *bloom = (bloom_extended_t *)config->runtime.bloom_filter;
	struct address_value *addressTable = (struct address_value *)config->runtime.address_table;
	int64_t N = config->runtime.address_count;
	uint64_t N_SEQUENTIAL_MAX = config->runtime.sequential_max;
	struct thread_counter *steps = (struct thread_counter *)config->runtime.thread_counters;
	struct thread_flag *ends = (struct thread_flag *)config->runtime.thread_flags;
	int MAXLENGTHADDRESS = config->runtime.max_address_length;

	/* Generator points */
	std::vector<Point> &Gn = *(std::vector<Point> *)config->runtime.generator_points;
	Point &_2Gn = *(Point *)config->runtime.generator_point_2;

	/* Range and stride */
	Int &stride = *(Int *)config->runtime.stride;
	Int &n_range_end = *(Int *)config->runtime.range_end;

	/* Endomorphism constants */
	Int &lambda = *(Int *)config->runtime.endo_lambda;
	Int &lambda2 = *(Int *)config->runtime.endo_lambda2;
	Int &beta = *(Int *)config->runtime.endo_beta;
	Int &beta2 = *(Int *)config->runtime.endo_beta2;

	/* GPU flags (used by cpu_use_y_parity_for_compressed_btc) */
	bool FLAGGPU_FULL = config->gpu.full_mode;
	bool FLAGGPU_HYBRID = config->gpu.hybrid_mode;

	/* ---- End config-derived variables ---- */

	Point pts[CPU_GRP_SIZE];
	Point endomorphism_beta[CPU_GRP_SIZE];
	Point endomorphism_beta2[CPU_GRP_SIZE];
	Point endomorphism_negeted_point[4];

	Int dx[CPU_GRP_SIZE / 2 + 1];
	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Point startP;
	Int dy;
	Int dyn;
	Int _s;
	Int _p;
	Point pp;
	Point pn;
	int i,l,pp_offset,pn_offset,hLength = (CPU_GRP_SIZE / 2 - 1);
	uint64_t j,count;
	Point R,temporal,publickey;
	int r,continue_flag = 1,k;
	char *hextemp = NULL;

	char publickeyhashrmd160[20];
	char publickeyhashrmd160_uncompress[4][20];
	char rawvalue[32];

	char publickeyhashrmd160_endomorphism[12][4][20];

	bool calculate_y = local_search == SEARCH_UNCOMPRESS || local_search == SEARCH_BOTH || local_crypto == CRYPTO_ETH;
	calculate_y = calculate_y || cpu_use_y_parity_for_compressed_btc(
		local_mode, local_crypto, local_endomorphism ? 1 : 0, local_search,
		FLAGGPU_HYBRID, FLAGGPU_FULL);
	Int key_mpz,keyfound;
	Int key_center;
	Int stride_half;
	Int stride_four;

	profile_set_thread(thread_number);
	grp->Set(dx);
	stride_half.SetInt32(CPU_GRP_SIZE / 2);
	stride_half.Mult(&stride);
	stride_four.SetInt32(4);
	stride_four.Mult(&stride);

	do {
		if(!acquire_base_key(key_mpz))	{
			continue_flag = 0;
		}
			if(continue_flag)	{
					count = 0;
					uint64_t block_limit = N_SEQUENTIAL_MAX;
					if (!local_random && stride.IsOne()) {
						Int range_end_local;
						if (g_work_pool.enabled && cpu_cached_block_valid) {
							range_end_local.Set(&cpu_cached_block_end);
						} else {
							range_end_local.Set(&n_range_end);
						}
						uint64_t rem_u64 = 0;
						if (int_sub_to_u64(range_end_local, key_mpz, &rem_u64) && rem_u64 < block_limit) {
							block_limit = rem_u64;
						}
				}
				if(local_matrix)	{
						hextemp = key_mpz.GetBase16();
						printf("Base key: %s thread %i\n",hextemp,thread_number);
						fflush(stdout);
					free(hextemp);
			}
			else	{
				if(!local_quiet){
					hextemp = key_mpz.GetBase16();
					printf("\rBase key: %s     \r",hextemp);
					fflush(stdout);
					free(hextemp);
					THREADOUTPUT.store(1, std::memory_order_release);
				}
			}
				do {
					// Compute the initial center point once
					if (count == 0) {
						key_center.Set(&key_mpz);
						key_center.Add(&stride_half);
						startP = secp->ComputePublicKey(&key_center);
					}

				profile_counters_t *prof = KH_PROF_PTR();
				const uint64_t ec_start = prof ? kh_profile_now_ns() : 0;

				for(i = 0; i < hLength; i++) {
					dx[i].ModSub(&Gn[i].x,&startP.x);
				}

				dx[i].ModSub(&Gn[i].x,&startP.x);  // For the first point
				dx[i + 1].ModSub(&_2Gn.x,&startP.x); // For the next center point

				grp->ModInvOptimized();  // Use 8x unrolled version

				pts[CPU_GRP_SIZE / 2] = startP;

				for(i = 0; i<hLength; i++) {
					if (i + 4 < hLength) {
						__builtin_prefetch(&Gn[i + 4], 0, 3);
						__builtin_prefetch(&dx[i + 4], 0, 3);
					}

					pp = startP;
					pn = startP;

					dy.ModSub(&Gn[i].y,&pp.y);

					_s.ModMulK1(&dy,&dx[i]);
					_p.ModSquareK1(&_s);

					pp.x.ModNeg();
					pp.x.ModAdd(&_p);
					pp.x.ModSub(&Gn[i].x);

					if(calculate_y)	{
						pp.y.ModSub(&Gn[i].x,&pp.x);
						pp.y.ModMulK1(&_s);
						pp.y.ModSub(&Gn[i].y);
					}

					dyn.Set(&Gn[i].y);
					dyn.ModNeg();
					dyn.ModSub(&pn.y);

					_s.ModMulK1(&dyn,&dx[i]);
					_p.ModSquareK1(&_s);
					pn.x.ModNeg();
					pn.x.ModAdd(&_p);
					pn.x.ModSub(&Gn[i].x);

					if(calculate_y)	{
						pn.y.ModSub(&Gn[i].x,&pn.x);
						pn.y.ModMulK1(&_s);
						pn.y.ModAdd(&Gn[i].y);
					}

					pp_offset = CPU_GRP_SIZE / 2 + (i + 1);
					pn_offset = CPU_GRP_SIZE / 2 - (i + 1);

					pts[pp_offset] = pp;
					pts[pn_offset] = pn;

					if(local_endomorphism)	{
						if( calculate_y  )	{
							endomorphism_beta[pp_offset].y.Set(&pp.y);
							endomorphism_beta[pn_offset].y.Set(&pn.y);
							endomorphism_beta2[pp_offset].y.Set(&pp.y);
							endomorphism_beta2[pn_offset].y.Set(&pn.y);
						}
						endomorphism_beta[pp_offset].x.ModMulK1(&pp.x, &beta);
						endomorphism_beta[pn_offset].x.ModMulK1(&pn.x, &beta);
						endomorphism_beta2[pp_offset].x.ModMulK1(&pp.x, &beta2);
						endomorphism_beta2[pn_offset].x.ModMulK1(&pn.x, &beta2);
					}
				}

				if(local_endomorphism)	{
					if( calculate_y  )	{
						endomorphism_beta[CPU_GRP_SIZE / 2].y.Set(&pts[CPU_GRP_SIZE / 2].y);
						endomorphism_beta2[CPU_GRP_SIZE / 2].y.Set(&pts[CPU_GRP_SIZE / 2].y);
					}
					endomorphism_beta[CPU_GRP_SIZE / 2].x.ModMulK1(&pts[CPU_GRP_SIZE / 2].x, &beta);
					endomorphism_beta2[CPU_GRP_SIZE / 2].x.ModMulK1(&pts[CPU_GRP_SIZE / 2].x, &beta2);
				}

				// First point (startP - (GRP_SIZE/2)*G)
				pn = startP;
				dyn.Set(&Gn[i].y);
				dyn.ModNeg();
				dyn.ModSub(&pn.y);

				_s.ModMulK1(&dyn,&dx[i]);
				_p.ModSquareK1(&_s);

				pn.x.ModNeg();
				pn.x.ModAdd(&_p);
				pn.x.ModSub(&Gn[i].x);

				if(calculate_y)	{
					pn.y.ModSub(&Gn[i].x,&pn.x);
					pn.y.ModMulK1(&_s);
					pn.y.ModAdd(&Gn[i].y);
				}

				pts[0] = pn;

				if(local_endomorphism)	{
					if( calculate_y  )	{
						endomorphism_beta[0].y.Set(&pn.y);
						endomorphism_beta2[0].y.Set(&pn.y);
					}
					endomorphism_beta[0].x.ModMulK1(&pn.x, &beta);
					endomorphism_beta2[0].x.ModMulK1(&pn.x, &beta2);
				}

				if (prof) prof->ns_ec += (kh_profile_now_ns() - ec_start);
				if((local_mode == MODE_RMD160 || local_mode == MODE_ADDRESS) && local_crypto == CRYPTO_BTC && !local_endomorphism) {
					process_rmd160_batch_btc_simple(config, key_mpz, pts, count);
				}
				else {
				for(j = 0; j < CPU_GRP_SIZE/4;j++){
					switch(local_mode)	{
						case MODE_RMD160:
						case MODE_ADDRESS:
							if(local_crypto == CRYPTO_BTC){

								if(local_search == SEARCH_COMPRESS || local_search == SEARCH_BOTH ){
									if(local_endomorphism)	{
										secp->GetHash160_fromX(P2PKH,0x02,&pts[(j*4)].x,&pts[(j*4)+1].x,&pts[(j*4)+2].x,&pts[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[0][0],(uint8_t*)publickeyhashrmd160_endomorphism[0][1],(uint8_t*)publickeyhashrmd160_endomorphism[0][2],(uint8_t*)publickeyhashrmd160_endomorphism[0][3]);
										secp->GetHash160_fromX(P2PKH,0x03,&pts[(j*4)].x,&pts[(j*4)+1].x,&pts[(j*4)+2].x,&pts[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[1][0],(uint8_t*)publickeyhashrmd160_endomorphism[1][1],(uint8_t*)publickeyhashrmd160_endomorphism[1][2],(uint8_t*)publickeyhashrmd160_endomorphism[1][3]);

										secp->GetHash160_fromX(P2PKH,0x02,&endomorphism_beta[(j*4)].x,&endomorphism_beta[(j*4)+1].x,&endomorphism_beta[(j*4)+2].x,&endomorphism_beta[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[2][0],(uint8_t*)publickeyhashrmd160_endomorphism[2][1],(uint8_t*)publickeyhashrmd160_endomorphism[2][2],(uint8_t*)publickeyhashrmd160_endomorphism[2][3]);
										secp->GetHash160_fromX(P2PKH,0x03,&endomorphism_beta[(j*4)].x,&endomorphism_beta[(j*4)+1].x,&endomorphism_beta[(j*4)+2].x,&endomorphism_beta[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[3][0],(uint8_t*)publickeyhashrmd160_endomorphism[3][1],(uint8_t*)publickeyhashrmd160_endomorphism[3][2],(uint8_t*)publickeyhashrmd160_endomorphism[3][3]);

										secp->GetHash160_fromX(P2PKH,0x02,&endomorphism_beta2[(j*4)].x,&endomorphism_beta2[(j*4)+1].x,&endomorphism_beta2[(j*4)+2].x,&endomorphism_beta2[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[4][0],(uint8_t*)publickeyhashrmd160_endomorphism[4][1],(uint8_t*)publickeyhashrmd160_endomorphism[4][2],(uint8_t*)publickeyhashrmd160_endomorphism[4][3]);
										secp->GetHash160_fromX(P2PKH,0x03,&endomorphism_beta2[(j*4)].x,&endomorphism_beta2[(j*4)+1].x,&endomorphism_beta2[(j*4)+2].x,&endomorphism_beta2[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[5][0],(uint8_t*)publickeyhashrmd160_endomorphism[5][1],(uint8_t*)publickeyhashrmd160_endomorphism[5][2],(uint8_t*)publickeyhashrmd160_endomorphism[5][3]);
									}
									else	{
										secp->GetHash160_fromX(P2PKH,0x02,&pts[(j*4)].x,&pts[(j*4)+1].x,&pts[(j*4)+2].x,&pts[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[0][0],(uint8_t*)publickeyhashrmd160_endomorphism[0][1],(uint8_t*)publickeyhashrmd160_endomorphism[0][2],(uint8_t*)publickeyhashrmd160_endomorphism[0][3]);
										secp->GetHash160_fromX(P2PKH,0x03,&pts[(j*4)].x,&pts[(j*4)+1].x,&pts[(j*4)+2].x,&pts[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[1][0],(uint8_t*)publickeyhashrmd160_endomorphism[1][1],(uint8_t*)publickeyhashrmd160_endomorphism[1][2],(uint8_t*)publickeyhashrmd160_endomorphism[1][3]);
									}

								}
								if(local_search == SEARCH_UNCOMPRESS || local_search == SEARCH_BOTH){
									if(local_endomorphism)	{
										for(l = 0; l < 4; l++)	{
											endomorphism_negeted_point[l] = secp->Negation(pts[(j*4)+l]);
										}
										secp->GetHash160(P2PKH,false, pts[(j*4)], pts[(j*4)+1], pts[(j*4)+2], pts[(j*4)+3],(uint8_t*)publickeyhashrmd160_endomorphism[6][0],(uint8_t*)publickeyhashrmd160_endomorphism[6][1],(uint8_t*)publickeyhashrmd160_endomorphism[6][2],(uint8_t*)publickeyhashrmd160_endomorphism[6][3]);
										secp->GetHash160(P2PKH,false,endomorphism_negeted_point[0] ,endomorphism_negeted_point[1],endomorphism_negeted_point[2],endomorphism_negeted_point[3],(uint8_t*)publickeyhashrmd160_endomorphism[7][0],(uint8_t*)publickeyhashrmd160_endomorphism[7][1],(uint8_t*)publickeyhashrmd160_endomorphism[7][2],(uint8_t*)publickeyhashrmd160_endomorphism[7][3]);
										for(l = 0; l < 4; l++)	{
											endomorphism_negeted_point[l] = secp->Negation(endomorphism_beta[(j*4)+l]);
										}
										secp->GetHash160(P2PKH,false,endomorphism_beta[(j*4)],  endomorphism_beta[(j*4)+1], endomorphism_beta[(j*4)+2], endomorphism_beta[(j*4)+3] ,(uint8_t*)publickeyhashrmd160_endomorphism[8][0],(uint8_t*)publickeyhashrmd160_endomorphism[8][1],(uint8_t*)publickeyhashrmd160_endomorphism[8][2],(uint8_t*)publickeyhashrmd160_endomorphism[8][3]);
										secp->GetHash160(P2PKH,false,endomorphism_negeted_point[0],endomorphism_negeted_point[1],endomorphism_negeted_point[2],endomorphism_negeted_point[3],(uint8_t*)publickeyhashrmd160_endomorphism[9][0],(uint8_t*)publickeyhashrmd160_endomorphism[9][1],(uint8_t*)publickeyhashrmd160_endomorphism[9][2],(uint8_t*)publickeyhashrmd160_endomorphism[9][3]);

										for(l = 0; l < 4; l++)	{
											endomorphism_negeted_point[l] = secp->Negation(endomorphism_beta2[(j*4)+l]);
										}
										secp->GetHash160(P2PKH,false, endomorphism_beta2[(j*4)],  endomorphism_beta2[(j*4)+1] ,  endomorphism_beta2[(j*4)+2] ,  endomorphism_beta2[(j*4)+3] ,(uint8_t*)publickeyhashrmd160_endomorphism[10][0],(uint8_t*)publickeyhashrmd160_endomorphism[10][1],(uint8_t*)publickeyhashrmd160_endomorphism[10][2],(uint8_t*)publickeyhashrmd160_endomorphism[10][3]);
										secp->GetHash160(P2PKH,false, endomorphism_negeted_point[0], endomorphism_negeted_point[1],   endomorphism_negeted_point[2],endomorphism_negeted_point[3],(uint8_t*)publickeyhashrmd160_endomorphism[11][0],(uint8_t*)publickeyhashrmd160_endomorphism[11][1],(uint8_t*)publickeyhashrmd160_endomorphism[11][2],(uint8_t*)publickeyhashrmd160_endomorphism[11][3]);

									}
									else	{
										secp->GetHash160(P2PKH,false,pts[(j*4)],pts[(j*4)+1],pts[(j*4)+2],pts[(j*4)+3],(uint8_t*)publickeyhashrmd160_uncompress[0],(uint8_t*)publickeyhashrmd160_uncompress[1],(uint8_t*)publickeyhashrmd160_uncompress[2],(uint8_t*)publickeyhashrmd160_uncompress[3]);

									}
								}
							}
							else if(local_crypto == CRYPTO_ETH){
								if(local_endomorphism)	{
									for(k = 0; k < 4;k++)	{
										endomorphism_negeted_point[k] = secp->Negation(pts[(j*4)+k]);
										generate_binaddress_eth(pts[(4*j)+k],(uint8_t*)publickeyhashrmd160_endomorphism[0][k]);
										generate_binaddress_eth(endomorphism_negeted_point[k],(uint8_t*)publickeyhashrmd160_endomorphism[1][k]);
										endomorphism_negeted_point[k] = secp->Negation(endomorphism_beta[(j*4)+k]);
										generate_binaddress_eth(endomorphism_beta[(4*j)+k],(uint8_t*)publickeyhashrmd160_endomorphism[2][k]);
										generate_binaddress_eth(endomorphism_negeted_point[k],(uint8_t*)publickeyhashrmd160_endomorphism[3][k]);
										endomorphism_negeted_point[k] = secp->Negation(endomorphism_beta2[(j*4)+k]);
										generate_binaddress_eth(endomorphism_beta2[(4*j)+k],(uint8_t*)publickeyhashrmd160_endomorphism[4][k]);
										generate_binaddress_eth(endomorphism_negeted_point[k],(uint8_t*)publickeyhashrmd160_endomorphism[5][k]);
									}
								}
								else	{
									for(k = 0; k < 4;k++)	{
										generate_binaddress_eth(pts[(4*j)+k],(uint8_t*)publickeyhashrmd160_uncompress[k]);
									}
								}

							}
						break;
					}


					switch(local_mode)	{
						case MODE_RMD160:
						case MODE_ADDRESS:
							if( local_crypto  == CRYPTO_BTC) {

								for(k = 0; k < 4;k++)	{
									if(local_search == SEARCH_COMPRESS || local_search == SEARCH_BOTH){
										if(local_endomorphism)	{
											for(l = 0;l < 6; l++)	{
												r = bloom_ext_check_rmd160(bloom, (uint8_t*)publickeyhashrmd160_endomorphism[l][k]);
												if(r) {
													r = searchbinary(addressTable,publickeyhashrmd160_endomorphism[l][k],N);
													if(r) {
														keyfound.SetInt32(k);
														keyfound.Mult(&stride);
														keyfound.Add(&key_mpz);
														publickey = secp->ComputePublicKey(&keyfound);
														switch(l)	{
															case 0:
																if(publickey.y.IsOdd())	{
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
															break;
															case 1:
																if(publickey.y.IsEven())	{
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
															break;
															case 2:
																keyfound.ModMulK1order(&lambda);
																if(publickey.y.IsOdd())	{
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
															break;
															case 3:
																keyfound.ModMulK1order(&lambda);
																if(publickey.y.IsEven())	{
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
															break;
															case 4:
																keyfound.ModMulK1order(&lambda2);
																if(publickey.y.IsOdd())	{
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
															break;
															case 5:
																keyfound.ModMulK1order(&lambda2);
																if(publickey.y.IsEven())	{
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
															break;
														}
														writekey(true,&keyfound);
													}
												}
											}
										}
										else	{
											for(l = 0;l < 2; l++)	{
												r = bloom_ext_check_rmd160(bloom, (uint8_t*)publickeyhashrmd160_endomorphism[l][k]);
												if(r) {
													r = searchbinary(addressTable,publickeyhashrmd160_endomorphism[l][k],N);
													if(r) {
														keyfound.SetInt32(k);
														keyfound.Mult(&stride);
														keyfound.Add(&key_mpz);

														publickey = secp->ComputePublicKey(&keyfound);
														secp->GetHash160(P2PKH,true,publickey,(uint8_t*)publickeyhashrmd160);
														if(memcmp(publickeyhashrmd160_endomorphism[l][k],publickeyhashrmd160,20) != 0)	{
															keyfound.Neg();
															keyfound.Add(&secp->order);
														}
														writekey(true,&keyfound);
													}
												}
											}
										}
									}

									if(local_search == SEARCH_UNCOMPRESS || local_search == SEARCH_BOTH)	{
										if(local_endomorphism)	{
											for(l = 6;l < 12; l++)	{
												r = bloom_ext_check_rmd160(bloom, (uint8_t*)publickeyhashrmd160_endomorphism[l][k]);
												if(r) {
													r = searchbinary(addressTable,publickeyhashrmd160_endomorphism[l][k],N);
													if(r) {
														keyfound.SetInt32(k);
														keyfound.Mult(&stride);
														keyfound.Add(&key_mpz);
														switch(l)	{
															case 6:
															case 7:
																publickey = secp->ComputePublicKey(&keyfound);
																secp->GetHash160(P2PKH,false,publickey,(uint8_t*)publickeyhashrmd160_uncompress[0]);
																if(memcmp(publickeyhashrmd160_endomorphism[l][k],publickeyhashrmd160_uncompress[0],20) != 0){
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
															break;
															case 8:
															case 9:
																keyfound.ModMulK1order(&lambda);
																publickey = secp->ComputePublicKey(&keyfound);
																secp->GetHash160(P2PKH,false,publickey,(uint8_t*)publickeyhashrmd160_uncompress[0]);
																if(memcmp(publickeyhashrmd160_endomorphism[l][k],publickeyhashrmd160_uncompress[0],20) != 0){
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
															break;
															case 10:
															case 11:
																keyfound.ModMulK1order(&lambda2);
																publickey = secp->ComputePublicKey(&keyfound);
																secp->GetHash160(P2PKH,false,publickey,(uint8_t*)publickeyhashrmd160_uncompress[0]);
																if(memcmp(publickeyhashrmd160_endomorphism[l][k],publickeyhashrmd160_uncompress[0],20) != 0){
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
															break;
														}
														writekey(false,&keyfound);
													}
												}
											}
										}
										else	{
											r = bloom_ext_check_rmd160(bloom, (uint8_t*)publickeyhashrmd160_uncompress[k]);
											if(r) {
												r = searchbinary(addressTable,publickeyhashrmd160_uncompress[k],N);
												if(r) {
													keyfound.SetInt32(k);
													keyfound.Mult(&stride);
													keyfound.Add(&key_mpz);
													writekey(false,&keyfound);
												}
											}
										}
									}
								}
							}
							else if( local_crypto == CRYPTO_ETH) {
								if(local_endomorphism)	{
									for(k = 0; k < 4;k++)	{
										for(l = 0;l < 6; l++)	{
											r = bloom_ext_check_rmd160(bloom, (uint8_t*)publickeyhashrmd160_endomorphism[l][k]);
											if(r) {
												r = searchbinary(addressTable,publickeyhashrmd160_endomorphism[l][k],N);
												if(r) {
													keyfound.SetInt32(k);
													keyfound.Mult(&stride);
													keyfound.Add(&key_mpz);
													switch(l)	{
														case 0:
														case 1:
															publickey = secp->ComputePublicKey(&keyfound);
															generate_binaddress_eth(publickey,(uint8_t*)publickeyhashrmd160_uncompress[0]);
															if(memcmp(publickeyhashrmd160_endomorphism[l][k],publickeyhashrmd160_uncompress[0],20) != 0){
																keyfound.Neg();
																keyfound.Add(&secp->order);
															}
														break;
														case 2:
														case 3:
															keyfound.ModMulK1order(&lambda);
															publickey = secp->ComputePublicKey(&keyfound);
															generate_binaddress_eth(publickey,(uint8_t*)publickeyhashrmd160_uncompress[0]);
															if(memcmp(publickeyhashrmd160_endomorphism[l][k],publickeyhashrmd160_uncompress[0],20) != 0){
																keyfound.Neg();
																keyfound.Add(&secp->order);
															}
														break;
														case 4:
														case 5:
															keyfound.ModMulK1order(&lambda2);
															publickey = secp->ComputePublicKey(&keyfound);
															generate_binaddress_eth(publickey,(uint8_t*)publickeyhashrmd160_uncompress[0]);
															if(memcmp(publickeyhashrmd160_endomorphism[l][k],publickeyhashrmd160_uncompress[0],20) != 0){
																keyfound.Neg();
																keyfound.Add(&secp->order);
															}
														break;
													}
													writekeyeth(&keyfound);
												}
											}
										}
									}
								}
								else	{
									for(k = 0; k < 4;k++)	{
										r = bloom_ext_check_rmd160(bloom, (uint8_t*)publickeyhashrmd160_uncompress[k]);
										if(r) {
											r = searchbinary(addressTable,publickeyhashrmd160_uncompress[k],N);
											if(r) {
												keyfound.SetInt32(k);
												keyfound.Mult(&stride);
												keyfound.Add(&key_mpz);
												writekeyeth(&keyfound);
											}
										}
									}
								}
							}
						break;
						case MODE_XPOINT:
							for(k = 0; k < 4;k++)	{
								if(local_endomorphism)	{
									pts[(4*j)+k].x.Get32Bytes((unsigned char *)rawvalue);
									r = bloom_ext_check(bloom,rawvalue,MAXLENGTHADDRESS);
									if(r) {
										r = searchbinary(addressTable,rawvalue,N);
										if(r) {
											keyfound.SetInt32(k);
											keyfound.Mult(&stride);
											keyfound.Add(&key_mpz);

											writekey(false,&keyfound);
										}
									}
									endomorphism_beta[(j*4)+k].x.Get32Bytes((unsigned char *)rawvalue);
									r = bloom_ext_check(bloom,rawvalue,MAXLENGTHADDRESS);
									if(r) {
										r = searchbinary(addressTable,rawvalue,N);
										if(r) {
											keyfound.SetInt32(k);
											keyfound.Mult(&stride);
											keyfound.Add(&key_mpz);
											keyfound.ModMulK1order(&lambda);

											writekey(false,&keyfound);
										}
									}

									endomorphism_beta2[(j*4)+k].x.Get32Bytes((unsigned char *)rawvalue);
									r = bloom_ext_check(bloom,rawvalue,MAXLENGTHADDRESS);
									if(r) {
										r = searchbinary(addressTable,rawvalue,N);
										if(r) {
											keyfound.SetInt32(k);
											keyfound.Mult(&stride);
											keyfound.Add(&key_mpz);
											keyfound.ModMulK1order(&lambda2);
											writekey(false,&keyfound);
										}
									}
								}
								else	{
									pts[(4*j)+k].x.Get32Bytes((unsigned char *)rawvalue);
									r = bloom_ext_check(bloom,rawvalue,MAXLENGTHADDRESS);
									if(r) {
										r = searchbinary(addressTable,rawvalue,N);
										if(r) {
											keyfound.SetInt32(k);
											keyfound.Mult(&stride);
											keyfound.Add(&key_mpz);

											writekey(false,&keyfound);
										}
									}
								}
							}
						break;
					}
						count+=4;
						key_mpz.Add(&stride_four);
				}
				}

				steps[thread_number].value++;

				// Next start point (startP + GRP_SIZE*G)
				pp = startP;
				dy.ModSub(&_2Gn.y,&pp.y);

				_s.ModMulK1(&dy,&dx[i + 1]);
				_p.ModSquareK1(&_s);

				pp.x.ModNeg();
				pp.x.ModAdd(&_p);
				pp.x.ModSub(&_2Gn.x);

				pp.y.ModSub(&_2Gn.x,&pp.x);
				pp.y.ModMulK1(&_s);
				pp.y.ModSub(&_2Gn.y);
				startP = pp;
				}while(count < block_limit && continue_flag);
			}
		} while(continue_flag);
		ends[thread_number].value = 1;
		delete grp;
		return (platform_thread_return_t)0;
	}
