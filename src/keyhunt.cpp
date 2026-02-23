/*
 * keyhunt.cpp - High-performance cryptocurrency private key search tool
 *
 * Develop by Alberto
 * email: albertobsd@gmail.com
 *
 * Search Modes:
 *   - MODE_ADDRESS (1): Search for Bitcoin addresses using bloom filters
 *   - MODE_RMD160 (3): Search for RIPEMD160 hashes directly
 *   - MODE_XPOINT (0): Search for public key X-coordinates (fastest)
 *   - MODE_BSGS (2): Baby Step Giant Step for known public keys
 *   - MODE_VANITY (6): Generate vanity addresses with specific prefixes
 *   - MODE_MINIKEYS (5): Search minikey format private keys
 *
 * Architecture:
 *   - Multi-threaded with SIMD optimizations (SSE2/AVX2/AVX-512)
 *   - Bloom filters for fast target lookup
 *   - Support for GPU acceleration (CUDA)
 *   - Distributed mode for multi-machine coordination
 *
 * See src/search/search_common.h for modular search declarations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <chrono>
#include <vector>
#include <algorithm>
#include <atomic>
#include <inttypes.h>
#ifndef _WIN64
// time.h already included above
#endif
#include "base58/libbase58.h"
#include "oldbloom/oldbloom.h"
#include "bloom/bloom.h"
#include "bloom/bloom_wrapper.h"
#include "sha3/sha3.h"
#include "core/util.h"
#include "core/workqueue.h"
#include "core/sysinfo.h"
#include "core/parameter_validator.h"
#include "gpu/gpu_backend.h"
#include "core/config.h"
#include "config/config.h"
#include "hybrid/adaptive_scheduler.h"
#include "wizard/wizard.h"
#include "benchmark.h"
#include "output.h"
#include "progress.h"
#include "cli.h"
#include "bsgs/bsgs_sort.h"
#include "sort/sort.h"
#include "crypto/address_util.h"
#include "crypto/bloom_init.h"
#include "io/io.h"

/*
 * NOTE: search/search_context.h and search/search_utils.h provide the
 * authoritative definitions of MODE_*, CRYPTO_*, SEARCH_* constants,
 * shared structs, and utility functions for extracted search modules.
 * keyhunt.cpp cannot yet include them directly because the extern
 * declarations and struct definitions in those headers were written for
 * the target (post-extraction) state and conflict with definitions that
 * still live here.  As functions are extracted out of keyhunt.cpp in
 * later subtasks, these includes will be enabled.
 *
 * Duplicate macro definitions (CRYPTO_NONE/BTC/ETH/ALL, MODE_*,
 * SEARCH_UNCOMPRESS/COMPRESS/BOTH) have already been removed from this
 * file; the canonical definitions now live in search_context.h.
 * Until keyhunt.cpp includes that header, the macros are picked up
 * transitively through config/config.h or the build's -Isrc path.
 */

#include "secp256k1/SECP256k1.h"
#include "secp256k1/Point.h"
#include "secp256k1/Int.h"
#include "secp256k1/IntGroup.h"
#include "secp256k1/Random.h"

#include "hash/sha256.h"
#include "hash/ripemd160.h"
#include "platform/platform.h"

#if defined(_WIN64) && !defined(__CYGWIN__)
#include "getopt.h"
#define strcasecmp _stricmp
#else
#include <unistd.h>
#include <sys/random.h>
#include <strings.h>
#ifdef __linux__
#include <sys/mman.h>
#endif
#endif

#ifdef __unix__
#ifdef __CYGWIN__
#else
#include <linux/random.h>
#endif
#endif

/* ============================================================================
 * Cache-Line Aligned Allocation
 * ============================================================================ */

/*
 * Allocate zero-initialized memory aligned to cache line boundaries (64 bytes).
 * This prevents false sharing between threads accessing adjacent array elements.
 * Uses aligned_alloc() which requires size to be a multiple of alignment.
 */
static inline void* aligned_calloc(size_t alignment, size_t count, size_t elem_size) {
    size_t total_size = count * elem_size;
    // Round up to multiple of alignment
    total_size = ((total_size + alignment - 1) / alignment) * alignment;
    void* ptr = aligned_alloc(alignment, total_size);
    if (ptr) {
        memset(ptr, 0, total_size);  // Zero-initialize like calloc
    }
    return ptr;
}

/* ============================================================================
 * Thread-Safe Random Number Generation
 * ============================================================================ */

/*
 * Thread-safe random number generator using thread-local state.
 * This replaces rand() which is not thread-safe.
 */
static thread_local unsigned int g_thread_rand_state = 0;
static thread_local bool g_thread_rand_initialized = false;

static inline void thread_rand_init(void) {
    if (!g_thread_rand_initialized) {
        /* Seed with time + thread ID for uniqueness */
        g_thread_rand_state = (unsigned int)(time(NULL) ^ (uintptr_t)pthread_self() ^ clock());
        g_thread_rand_initialized = true;
    }
}

/* Thread-safe replacement for rand() */
static inline int thread_rand(void) {
    thread_rand_init();
    /* Simple LCG (same as glibc rand) */
    g_thread_rand_state = g_thread_rand_state * 1103515245 + 12345;
    return (int)((g_thread_rand_state >> 16) & 0x7fff);
}

/* Thread-safe random in range [0, n) */
static inline int thread_rand_n(int n) {
    if (n <= 0) return 0;
    return thread_rand() % n;
}

/*
 * Mode, crypto, and search constants — canonical definitions are in
 * search/search_context.h.  Duplicated here with #ifndef guards until
 * keyhunt.cpp can include that header directly (blocked by struct/extern
 * conflicts that will be resolved as code is extracted in later subtasks).
 */
#ifndef CRYPTO_NONE
#define CRYPTO_NONE 0
#define CRYPTO_BTC 1
#define CRYPTO_ETH 2
#define CRYPTO_ALL 3
#endif

#ifndef MODE_XPOINT
#define MODE_XPOINT 0
#define MODE_ADDRESS 1
#define MODE_BSGS 2
#define MODE_RMD160 3
#define MODE_PUB2RMD 4
#define MODE_MINIKEYS 5
#define MODE_VANITY 6
#endif

#ifndef SEARCH_UNCOMPRESS
#define SEARCH_UNCOMPRESS 0
#define SEARCH_COMPRESS 1
#define SEARCH_BOTH 2
#endif

// NOTE: Global variables migration in progress to keyhunt_config_t (see src/config/config.h)
// Many variables still use global state until migration is complete

// Infrastructure globals
uint32_t THREADBPWORKLOAD = 0;
static bool g_avx2_available = false;
static keyhunt_ini_config_t g_config;
static bool g_config_loaded = false;
static const char *g_save_config_path = NULL;
static progress_state_t g_progress_state;
static bool g_progress_enabled = false;

// Work queue (used on all platforms now)
static WorkQueue<Int> g_workQueue;

// ---------------------------------------------------------------------------
// Lightweight internal profiler (enabled via KEYHUNT_PROFILE=1)
// ---------------------------------------------------------------------------

static inline bool env_truthy_kh(const char *name) {
	const char *v = getenv(name);
	if (!v || !*v) return false;
	if (v[0] == '0' && v[1] == '\0') return false;
	if ((v[0] == 'f' || v[0] == 'F') && (v[1] == 'a' || v[1] == 'A')) return false;
	if ((v[0] == 'n' || v[0] == 'N') && (v[1] == 'o' || v[1] == 'O')) return false;
	return true;
}

typedef struct {
	uint64_t ns_ec;
	uint64_t ns_hash;
	uint64_t ns_bloom;
	uint64_t ns_binsearch;
	uint64_t ns_write;
	uint64_t keys;
} profile_counters_t;

static bool g_profile_enabled = false;
static profile_counters_t *g_profile_counters = NULL;
static int g_profile_thread_count = 0;
static profile_counters_t g_profile_prev_agg;

static thread_local profile_counters_t *tls_prof = NULL;

static inline uint64_t profile_now_ns() {
	return platform_time_now_ns();
}

struct profile_scope_t {
	uint64_t start;
	uint64_t *target;
	explicit profile_scope_t(uint64_t *t) : start(0), target(t) {
		if (t) start = profile_now_ns();
	}
	~profile_scope_t() {
		if (target) {
			*target += (profile_now_ns() - start);
		}
	}
};

#define KH_PROF_PTR() ((__builtin_expect(g_profile_enabled, 0) && tls_prof) ? tls_prof : NULL)
#define KH_PROF_SCOPE(field) profile_scope_t _kh_prof_scope_##__LINE__(KH_PROF_PTR() ? &KH_PROF_PTR()->field : NULL)
#define KH_PROF_ADD_KEYS(n) do { profile_counters_t *p = KH_PROF_PTR(); if (p) p->keys += (uint64_t)(n); } while(0)

static inline void profile_init_threads(int nthreads) {
	if (!g_profile_enabled || nthreads <= 0 || g_profile_counters) return;
	g_profile_counters = (profile_counters_t *)calloc((size_t)nthreads, sizeof(profile_counters_t));
	if (!g_profile_counters) {
		output_warning("Profiling requested but allocation failed\n");
		g_profile_enabled = false;
		return;
	}
	g_profile_thread_count = nthreads;
	memset(&g_profile_prev_agg, 0, sizeof(g_profile_prev_agg));
}

static inline void profile_set_thread(int idx) {
	if (!g_profile_enabled || !g_profile_counters || idx < 0 || idx >= g_profile_thread_count) {
		tls_prof = NULL;
		return;
	}
	tls_prof = &g_profile_counters[idx];
}

static inline void profile_aggregate(profile_counters_t *out) {
	memset(out, 0, sizeof(*out));
	if (!g_profile_enabled || !g_profile_counters) return;
	for (int i = 0; i < g_profile_thread_count; i++) {
		out->ns_ec += g_profile_counters[i].ns_ec;
		out->ns_hash += g_profile_counters[i].ns_hash;
		out->ns_bloom += g_profile_counters[i].ns_bloom;
		out->ns_binsearch += g_profile_counters[i].ns_binsearch;
		out->ns_write += g_profile_counters[i].ns_write;
		out->keys += g_profile_counters[i].keys;
	}
}

static void append_profile_info(char *buffer, size_t bufferSize) {
	if (!g_profile_enabled || !g_profile_counters || bufferSize < 4) return;

	profile_counters_t cur;
	profile_aggregate(&cur);

	profile_counters_t delta;
	delta.ns_ec = cur.ns_ec - g_profile_prev_agg.ns_ec;
	delta.ns_hash = cur.ns_hash - g_profile_prev_agg.ns_hash;
	delta.ns_bloom = cur.ns_bloom - g_profile_prev_agg.ns_bloom;
	delta.ns_binsearch = cur.ns_binsearch - g_profile_prev_agg.ns_binsearch;
	delta.ns_write = cur.ns_write - g_profile_prev_agg.ns_write;
	delta.keys = cur.keys - g_profile_prev_agg.keys;
	g_profile_prev_agg = cur;

	const uint64_t total_ns = delta.ns_ec + delta.ns_hash + delta.ns_bloom + delta.ns_binsearch + delta.ns_write;
	if (delta.keys == 0 || total_ns == 0) return;

	const unsigned ec_pct = (unsigned)((delta.ns_ec * 100ULL) / total_ns);
	const unsigned hash_pct = (unsigned)((delta.ns_hash * 100ULL) / total_ns);
	const unsigned bloom_pct = (unsigned)((delta.ns_bloom * 100ULL) / total_ns);
	const unsigned bin_pct = (unsigned)((delta.ns_binsearch * 100ULL) / total_ns);
	const unsigned write_pct = (unsigned)((delta.ns_write * 100ULL) / total_ns);
	const uint64_t ns_per_key = total_ns / delta.keys;

	char addition[256];
	snprintf(addition, sizeof(addition),
	         " | prof %luns/key EC%u Hash%u Bloom%u Bin%u Write%u",
	         (unsigned long)ns_per_key, ec_pct, hash_pct, bloom_pct, bin_pct, write_pct);

	size_t len = strlen(buffer);
	char tail = 0;
	if (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
		tail = buffer[len - 1];
		buffer[len - 1] = '\0';
		len--;
	}
	size_t remaining = (len < bufferSize) ? bufferSize - len : 0;
	if (remaining > 1) {
		strncat(buffer, addition, remaining - 1);
		len = strlen(buffer);
	}
	if (tail != 0 && len + 1 < bufferSize) {
		buffer[len] = tail;
		buffer[len + 1] = '\0';
	}
}

// Append adaptive scheduler stats to status output (hybrid mode only)
static void append_adaptive_info(char *buffer, size_t bufferSize) {
	// Only show stats if adaptive scheduler is initialized (implies hybrid mode)
	if (bufferSize < 4) return;
	if (!g_adaptive_scheduler.initialized) return;

	double cpu_mkeys = 0.0, gpu_mkeys = 0.0;
	int cpu_pct = 0, gpu_pct = 0;
	adaptive_get_stats(&cpu_mkeys, &gpu_mkeys, &cpu_pct, &gpu_pct);

	// Only show if we have meaningful data
	if (cpu_mkeys < 0.1 && gpu_mkeys < 0.1) return;

	char addition[128];
	snprintf(addition, sizeof(addition),
	         " | adapt CPU=%.1fMk/s(%d%%) GPU=%.1fMk/s(%d%%)",
	         cpu_mkeys, cpu_pct, gpu_mkeys, gpu_pct);

	size_t len = strlen(buffer);
	char tail = 0;
	if (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
		tail = buffer[len - 1];
		buffer[len - 1] = '\0';
		len--;
	}
	size_t remaining = (len < bufferSize) ? bufferSize - len : 0;
	if (remaining > 1) {
		strncat(buffer, addition, remaining - 1);
		len = strlen(buffer);
	}
	if (tail != 0 && len + 1 < bufferSize) {
		buffer[len] = tail;
		buffer[len + 1] = '\0';
	}
}

// ============================================================================
// Work-stealing pool for GPU/CPU collaboration
// Both GPU and CPU threads pull work blocks from the same pool for dynamic
// load balancing. The faster processor (GPU) naturally gets more work.
// ============================================================================
	struct WorkPool {
		std::atomic<uint64_t> next_block;      // Next available block index
		uint64_t block_size;                    // Keys per block (immutable after init)
		Int range_base;                         // Starting point of range (IMMUTABLE after init)
		Int range_end;                          // End of range (IMMUTABLE after init)
		std::atomic<bool> enabled;              // Work pool is active
		std::atomic<bool> exhausted;            // All work has been taken
		std::atomic<bool> initialized;          // Set true after init() completes (barrier for readers)

		WorkPool() : next_block(0), block_size(0), enabled(false), exhausted(false), initialized(false) {}
	
		// Initialize work pool with a range
		// NOTE: range_base, range_end, and block_size are IMMUTABLE after init()
		void init(Int *start, Int *end, uint64_t blk_size) {
			// Set immutable fields first (before initialized barrier)
			range_base.Set(start);
			range_end.Set(end);
			block_size = blk_size;

			next_block.store(0, std::memory_order_release);
			exhausted.store(false, std::memory_order_release);
			enabled.store(true, std::memory_order_release);
			// Memory barrier: initialized must be set AFTER all other fields
			initialized.store(true, std::memory_order_release);
		}

	// Get next work block (thread-safe, lock-free)
	// Returns true if work was assigned, false if no more work
	// NOTE: range_base, range_end, block_size are IMMUTABLE after init(), safe to read without lock
		bool get_block(Int &start_out, Int &end_out) {
			// Check initialized barrier first (acquire to sync with init's release)
			if (!initialized.load(std::memory_order_acquire)) return false;
			if (!enabled.load(std::memory_order_acquire) ||
			    exhausted.load(std::memory_order_acquire)) return false;

			uint64_t block_idx = next_block.fetch_add(1, std::memory_order_acq_rel);

			// Calculate start = range_base + block_idx * block_size
			// Use base10 conversion to avoid signed overflow when block_idx > INT64_MAX.
			// NOTE: range_base and block_size are immutable after init(), safe to read
			char tmp[32];
			Int offset;
			snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)block_size);
			offset.SetBase10(tmp);
			Int mult;
			snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)block_idx);
			mult.SetBase10(tmp);
			offset.Mult(&mult);

			start_out.Set(&range_base);
			start_out.Add(&offset);
			// NOTE: range_end is immutable after init(), safe to read
			if (!start_out.IsLower(&range_end)) {
				exhausted.store(true, std::memory_order_release);
				return false;
			}

			// Calculate end = min(start + block_size, range_end)
			end_out.Set(&start_out);
			Int blk;
			snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)block_size);
			blk.SetBase10(tmp);
			end_out.Add(&blk);

			if (end_out.IsGreater(&range_end)) {
				end_out.Set(&range_end);
			}

			return true;
		}

		// Check if pool is exhausted
		bool is_exhausted() const {
			return exhausted.load(std::memory_order_acquire);
		}

		// Disable the pool
		void disable() {
			enabled.store(false, std::memory_order_release);
			exhausted.store(true, std::memory_order_release);
		}
	};

// Global work pool instance
static WorkPool g_work_pool;

struct checksumsha256	{
	char data[32];
	char backup[32];
};

/* struct bsgs_xvalue now defined in bsgs/bsgs_sort.h */

struct address_value	{
	uint8_t value[20];
};

struct tothread {
	int nt;     //Number thread
	char *rs;   //range start
	char *rpt;  //rng per thread
};

/*
 * thread_args - New unified thread argument structure
 *
 * This struct replaces the legacy tothread struct and provides a clean
 * way to pass configuration to thread functions. It contains:
 *   - config: Pointer to the global keyhunt_config_t struct
 *   - thread_id: Thread identifier (0-based index)
 *
 * Usage:
 *   thread_args args = { .config = &g_config, .thread_id = i };
 *   pthread_create(&tid, NULL, thread_func, &args);
 *
 * Migration: During the migration phase, both tothread and thread_args
 * coexist. Eventually, tothread will be removed.
 */
struct thread_args {
	keyhunt_config_t *config;   /* Pointer to global configuration */
	int thread_id;              /* Thread number (0-based) */
};

struct bPload	{
	uint32_t threadid;
	uint64_t from;
	uint64_t to;
	uint64_t counter;
	uint64_t workload;
	uint32_t aux;
	uint32_t finished;
};

#if defined(_WIN64) && !defined(__CYGWIN__)
#define PACK( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
PACK(struct publickey
{
	uint8_t parity;
	union {
		uint8_t data8[32];
		uint32_t data32[8];
		uint64_t data64[4];
	} X;
});
#else
struct __attribute__((__packed__)) publickey {
  uint8_t parity;
	union	{
		uint8_t data8[32];
		uint32_t data32[8];
		uint64_t data64[4];
	} X;
};
#endif

const char *Ccoinbuffer_default = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

char *Ccoinbuffer = (char*) Ccoinbuffer_default;
char *str_baseminikey = NULL;
char *raw_baseminikey = NULL;
char *minikeyN = NULL;
int minikey_n_limit;
	
const char *version = "0.2.230519 Satoshi Quest";

// Auto-tuned parameters (configured at startup based on system)
uint32_t CPU_GRP_SIZE = 1024;  // Batch size - 1024 is proven optimal
int OPTIMAL_THREADS = 0;       // Auto-detected optimal thread count
uint64_t OPTIMAL_N = 0;        // Auto-detected optimal N value
int OPTIMAL_KFACTOR = 0;       // Auto-detected optimal K factor

std::vector<Point> Gn;
Point _2Gn;

std::vector<Point> GSn;
Point _2GSn;

void menu();
void init_generator();

// Helper function to get mode name string for output module
static const char *get_mode_name(int mode) {
	switch (mode) {
		case MODE_XPOINT:   return "xpoint";
		case MODE_ADDRESS:  return "address";
		case MODE_BSGS:     return "bsgs";
		case MODE_RMD160:   return "rmd160";
		case MODE_PUB2RMD:  return "pub2rmd";
		case MODE_MINIKEYS: return "minikeys";
		case MODE_VANITY:   return "vanity";
		default:            return "unknown";
	}
}

#ifndef _WIN64
static void configure_work_queue(size_t threadCount);
static void shutdown_work_queue();
#endif
static bool acquire_base_key(Int &key);

void sleep_ms(int milliseconds);

/* bsgs_sort, bsgs_myheapsort, bsgs_insertionsort, bsgs_introsort, bsgs_swap,
   bsgs_heapify, bsgs_partition, and bsgs_searchbinary now declared in bsgs/bsgs_sort.h */

int bsgs_secondcheck(Int *start_range,uint32_t a,uint32_t k_index,Int *privatekey);
int bsgs_thirdcheck(Int *start_range,uint32_t a,uint32_t k_index,Int *privatekey);

bool vanityrmdmatch(unsigned char *rmdhash);
void writevanitykey(bool compress,Int *key);
int addvanity(char *target);
int minimum_same_bytes(unsigned char* A,unsigned char* B, int length);

/* writekey, writekeyeth, checkpointer moved to io/io.cpp */

// GPU Full Search helper functions (forward declarations)
static int gpu_upload_gtable_from_secp();
static int gpu_upload_targets_from_addressTable(int64_t count);
static int gpu_build_and_upload_bloom_from_addressTable(int64_t count);
static void gpu_found_callback(const uint8_t *privkey_be, int compressed, void *userdata);
static int gpu_run_full_search(Int *start_key, Int *end_key, Int *stride_val, int64_t target_count);

// Hybrid mode: GPU thread wrapper
typedef struct {
	Int start_key;
	Int end_key;
	Int stride;
	int64_t target_count;
	std::atomic<int> result{0};
	std::atomic<int> completed{0};
} gpu_hybrid_args_t;

static void *gpu_hybrid_thread(void *arg);

/* readFileAddress, readFileVanity, forceReadFileAddress, forceReadFileAddressEth,
   forceReadFileXPoint, processOneVanity, writeFileIfNeeded moved to io/io.cpp */

void calcualteindex(int i,Int *key);
#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process_minikeys(LPVOID vargp);  /* search/search_minikeys.cpp */
DWORD WINAPI thread_process_vanity(LPVOID vargp);
DWORD WINAPI thread_process(LPVOID vargp);
DWORD WINAPI thread_process_bsgs(LPVOID vargp);
DWORD WINAPI thread_process_bsgs_backward(LPVOID vargp);
DWORD WINAPI thread_process_bsgs_both(LPVOID vargp);
DWORD WINAPI thread_process_bsgs_random(LPVOID vargp);
DWORD WINAPI thread_process_bsgs_dance(LPVOID vargp);
DWORD WINAPI thread_bPload(LPVOID vargp);
DWORD WINAPI thread_bPload_2blooms(LPVOID vargp);
#else
void *thread_process_minikeys(void *vargp);  /* search/search_minikeys.cpp */
void *thread_process_vanity(void *vargp);
void *thread_process(void *vargp);
void *thread_process_bsgs(void *vargp);
void *thread_process_bsgs_backward(void *vargp);
void *thread_process_bsgs_both(void *vargp);
void *thread_process_bsgs_random(void *vargp);
void *thread_process_bsgs_dance(void *vargp);
void *thread_bPload(void *vargp);
void *thread_bPload_2blooms(void *vargp);
#endif

int THREADOUTPUT = 0;
char *bit_range_str_min;
char *bit_range_str_max;

const char *bsgs_modes[5] = {"sequential","backward","both","random","dance"};
const char *modes[7] = {"xpoint","address","bsgs","rmd160","pub2rmd","minikeys","vanity"};
const char *cryptos[3] = {"btc","eth","all"};
const char *publicsearch[3] = {"uncompress","compress","both"};
const char *default_fileName = "addresses.txt";

platform_thread_t *tid = NULL;
platform_mutex_t write_keys;
platform_mutex_t write_random;
platform_mutex_t bsgs_thread;
platform_mutex_t *bPload_mutex = NULL;

// NOTE: Thread runtime state variables migration in progress to runtime_state_t in keyhunt_config_t
uint64_t FINISHED_THREADS_COUNTER = 0;
uint64_t FINISHED_THREADS_BP = 0;
uint64_t THREADCYCLES = 0;
uint64_t THREADCOUNTER = 0;
std::atomic<uint64_t> FINISHED_ITEMS{0};
uint64_t OLDFINISHED_ITEMS = 0;

uint8_t byte_encode_crypto = 0x00;		/* Bitcoin  */

// NOTE: Vanity and bloom variables migration in progress to runtime_state_t in keyhunt_config_t
int vanity_rmd_targets = 0;
int vanity_rmd_total = 0;
int *vanity_rmd_limits = NULL;
uint8_t ***vanity_rmd_limit_values_A = NULL;
uint8_t ***vanity_rmd_limit_values_B = NULL;
int vanity_rmd_minimun_bytes_check_length = 0;
char **vanity_address_targets = NULL;
struct bloom *vanity_bloom = NULL;
bloom_extended_t bloom;

/* Pad shared counters to separate cache lines and reduce false sharing between threads. */
struct thread_counter {
	uint64_t value;
	uint8_t padding[56];
};

struct thread_flag {
	unsigned int value;
	uint8_t padding[60];
};

struct thread_counter *steps = NULL;
struct thread_flag *ends = NULL;
uint64_t N = 0;

// NOTE: Global FLAG* variables migration in progress to keyhunt_config_t (see MIGRATION_GUIDE.md)
// Search configuration flags
int FLAGSKIPCHECKSUM = 0;
int FLAGENDOMORPHISM = 0;
int FLAGBLOOMMULTIPLIER = 1;
int FLAGVANITY = 0;
int FLAGBASEMINIKEY = 0;
int FLAGBSGSMODE = 0;
int FLAGDEBUG = 0;
int FLAGQUIET = 0;
int FLAGMATRIX = 0;
int FLAGPROGRESSBAR = 0;
int KFACTOR = 1;
int MAXLENGTHADDRESS = 20;
int NTHREADS = 1;
int FLAGTHREADS = 0;
int FLAGSAVEREADFILE = 0;
int FLAGREADEDFILE1 = 0;
int FLAGREADEDFILE2 = 0;
int FLAGREADEDFILE3 = 0;
int FLAGREADEDFILE4 = 0;
int FLAGUPDATEFILE1 = 0;
int FLAGSTRIDE = 0;
int FLAGSEARCH = SEARCH_BOTH;
int FLAGBITRANGE = 0;
int FLAGRANGE = 0;
int FLAGFILE = 0;
int FLAGMODE = MODE_ADDRESS;
int FLAGCRYPTO = 0;
int FLAGRAWDATA = 0;
int FLAGRANDOM = 0;
int FLAG_N = 0;
int FLAGPRECALCUTED_P_FILE = 0;
int FLAGGPU = 0;
int FLAGGPU_FULL = 0;
std::atomic<int> FLAGGPU_HYBRID{0};
int DEBUGCOUNT = 0;

// GPU runtime state
std::atomic<uint64_t> g_gpu_keys_checked{0};
std::atomic<uint64_t> g_gpu_keys_checked_cur{0};
std::atomic<int> g_gpu_should_stop{0};
static int g_gpu_bloom_uploaded = 0;
int g_gpu_range_percent = 0;

// Range and stride variables
int bitrange = 0;
char *str_N = NULL;
char *range_start = NULL;
char *range_end = NULL;
char *str_stride = NULL;
Int stride;

// Runtime output control
Int OUTPUTSECONDS;

// Sequential max for work queue
uint64_t N_SEQUENTIAL_MAX = 4096;

// NOTE: Functions below need refactoring to accept config parameter (future work)
// Currently using extern references to globals until complete migration

		static inline bool cpu_use_y_parity_for_compressed_btc() {
		// Unify CPU-only and HYBRID behavior for BTC compressed-only search:
		// compute the real Y parity and hash only the actual compressed prefix (02 or 03).
		//
		// This avoids doing 2x hash work (02+03) and aligns CPU with GPU FULL/HYBRID behavior.
		//
		// Overrides:
		//   KEYHUNT_CPU_USE_Y=0|1            (global CPU behavior, default=1)
		//   KEYHUNT_HYBRID_CPU_USE_Y=0|1     (when in HYBRID+FULL, default=1)
		// TODO: Accept config parameter instead of using extern globals
		extern int FLAGMODE, FLAGCRYPTO, FLAGENDOMORPHISM, FLAGSEARCH;
		if (!((FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160) &&
			  FLAGCRYPTO == CRYPTO_BTC &&
			  !FLAGENDOMORPHISM &&
			  FLAGSEARCH == SEARCH_COMPRESS)) {
			return false;
		}

		extern std::atomic<int> FLAGGPU_HYBRID;
		extern int FLAGGPU_FULL;
		if (FLAGGPU_HYBRID && FLAGGPU_FULL == 1) {
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

		static inline uint64_t gpu_keys_checked_total_u64() {
			// In static-range modes the backend updates g_gpu_keys_checked directly.
			// In work-stealing, g_gpu_keys_checked_cur is the in-progress block counter; we
			// aggregate it with a release/acquire pair so readers never observe a decreasing total.
			// TODO: Accept config parameter instead of using extern globals
			extern WorkPool g_work_pool;
			extern std::atomic<uint64_t> g_gpu_keys_checked;
			extern std::atomic<uint64_t> g_gpu_keys_checked_cur;
			if (!g_work_pool.enabled.load(std::memory_order_acquire)) {
				return g_gpu_keys_checked.load(std::memory_order_acquire);
			}
			uint64_t cur = g_gpu_keys_checked_cur.load(std::memory_order_acquire);
			uint64_t base = g_gpu_keys_checked.load(std::memory_order_acquire);
			return base + cur;
		}

// System info and GPU backend info
system_info_t g_sysinfo;
gpu_backend_info_t g_gpu_backend_info;

// BSGS configuration and buffers
uint64_t BSGS_XVALUE_RAM = 0;
uint64_t BSGS_BUFFERXPOINTLENGTH = 0;
uint64_t BSGS_BUFFERREGISTERLENGTH = 0;
uint64_t bloom_bP_totalbytes = 0;
uint64_t bloom_bP2_totalbytes = 0;
uint64_t bloom_bP3_totalbytes = 0;
uint64_t bsgs_m = 0;
uint64_t bsgs_m2 = 0;
uint64_t bsgs_m3 = 0;
uint64_t bsgs_aux = 0;
uint32_t bsgs_point_number = 0;

static int hybrid_get_gpu_range_percent_default(int cpu_threads) {
	const char *env = getenv("KEYHUNT_HYBRID_GPU_PERCENT");
	if (env && *env) {
		int v = atoi(env);
		if (v >= 1 && v <= 99) return v;
	}
	// TODO: Accept config parameter instead of using extern g_gpu_range_percent
	extern int g_gpu_range_percent;
	if (cpu_threads <= 0) return g_gpu_range_percent;

	// Heuristic split based on SM count vs CPU threads.
	// Goal: avoid the CPU tail becoming the bottleneck in static split.
	extern gpu_backend_info_t g_gpu_backend_info;
	const int sms = g_gpu_backend_info.multiprocessors;
	if (sms > 0) {
		const double ratio = ((double)sms * 5.0) / (double)cpu_threads;  // empirical scale
		const double pct = (ratio / (ratio + 1.0)) * 100.0;
		int v = (int)(pct + 0.5);
		if (v < 50) v = 50;
		if (v > 99) v = 99;
		return v;
	}
	return g_gpu_range_percent;
}

/*
BSGS Variables
NOTE: Many BSGS runtime state variables are kept as module-scoped for now.
They will be migrated to runtime_state_t in a future refactoring phase.

Configuration variables (bsgs_m, bloom_bP_totalbytes, bsgs_aux, bsgs_point_number)
have been migrated to bsgs_config_t and runtime_state_t.

Runtime algorithm state (bPtable, bloom_bP*, addressTable, checksums, mutexes)
remain as module-scoped until BSGS algorithm is fully encapsulated.
*/
int *bsgs_found;
std::vector<Point> OriginalPointsBSGS;
bool *OriginalPointsBSGScompressed;

uint64_t bytes;
char checksum[32],checksum_backup[32];
char buffer_bloom_file[1024];
bsgs_xvalue *bPtable;  // From bsgs/bsgs_sort.h
struct address_value *addressTable;

struct oldbloom oldbloom_bP;

// BSGS bloom filters use extended wrapper to enable fast bloom
bloom_extended_t *bloom_bP;
bloom_extended_t *bloom_bPx2nd; //2nd Bloom filter check
bloom_extended_t *bloom_bPx3rd; //3rd Bloom filter check

struct checksumsha256 *bloom_bP_checksums;
struct checksumsha256 *bloom_bPx2nd_checksums;
struct checksumsha256 *bloom_bPx3rd_checksums;

platform_mutex_t *bloom_bP_mutex;
platform_mutex_t *bloom_bPx2nd_mutex;
platform_mutex_t *bloom_bPx3rd_mutex;

// NOTE: BSGS config variables migrated to bsgs_config_t:
// bloom_bP_totalbytes->bsgs.bloom_bp_bytes, bsgs_m->bsgs.m_value
// bsgs_m2->bsgs.m2_value, bsgs_m3->bsgs.m3_value
// bsgs_aux->bsgs.aux_value, bsgs_point_number->bsgs.point_number

const char *str_limits_prefixs[7] = {"Mkeys/s","Gkeys/s","Tkeys/s","Pkeys/s","Ekeys/s","Zkeys/s","Ykeys/s"};
const char *str_limits[7] = {"1000000","1000000000","1000000000000","1000000000000000","1000000000000000000","1000000000000000000000","1000000000000000000000000"};
Int int_limits[7];

static void initialize_rate_limits() {
	static int initialized = 0;
	if (initialized) {
		return;
	}
	for (int j = 0; j < 7; j++) {
		int_limits[j].SetBase10((char*)str_limits[j]);
	}
	initialized = 1;
}




Int BSGS_GROUP_SIZE;
Int BSGS_CURRENT;
Int BSGS_R;
Int BSGS_AUX;
Int BSGS_N;
Int BSGS_N_double;
Int BSGS_M;					//M is squareroot(N)
Int BSGS_M_double;
Int BSGS_M2;				//M2 is M/32
Int BSGS_M2_double;			//M2_double is M2 * 2
Int BSGS_M3;				//M3 is M2/32
Int BSGS_M3_double;			//M3_double is M3 * 2

Int ONE;
Int ZERO;
Int MPZAUX;

Point BSGS_P;			//Original P is actually G, but this P value change over time for calculations
Point BSGS_MP;			//MP values this is m * P
Point BSGS_MP2;			//MP2 values this is m2 * P
Point BSGS_MP3;			//MP3 values this is m3 * P

Point BSGS_MP_double;			//MP2 values this is m2 * P * 2
Point BSGS_MP2_double;			//MP2 values this is m2 * P * 2
Point BSGS_MP3_double;			//MP3 values this is m3 * P * 2


std::vector<Point> BSGS_AMP2;
std::vector<Point> BSGS_AMP3;

Point point_temp,point_temp2;	//Temp value for some process

Int n_range_start;
Int n_range_end;
Int n_range_diff;
Int n_range_aux;

static bool g_rangeProgressEnabled = false;
Int g_rangeProgressStart;
Int g_rangeProgressEnd;
Int g_rangeProgressSpan;

static void format_keys_per_second(Int &rate, char *out, size_t outSize) {
	if (outSize == 0) {
		return;
	}
	char *raw = rate.GetBase10();
	if (raw == NULL) {
		snprintf(out, outSize, "? keys/s");
		return;
	}

	if (rate.IsLower(&int_limits[0])) {
		snprintf(out, outSize, "%s keys/s", raw);
		free(raw);
		return;
	}

	int idx = 0;
	while (idx < 6 && !rate.IsLower(&int_limits[idx + 1])) {
		idx++;
	}

	Int scaled;
	scaled.Set(&rate);
	scaled.Div(&int_limits[idx]);
	char *scaledStr = scaled.GetBase10();
	if (scaledStr == NULL) {
		snprintf(out, outSize, "%s keys/s", raw);
		free(raw);
		return;
	}

	snprintf(out, outSize, "~%s %s (%s keys/s)", scaledStr, str_limits_prefixs[idx], raw);
	free(raw);
	free(scaledStr);
}

static bool span_u64_from_range(Int &start, Int &end, uint64_t &out) {
	// Compute (end - start) if it fits in uint64_t. Return false otherwise.
	// Assumes start/end are non-negative and end >= start in normal usage.
	uint64_t d0 = end.bits64[0] - start.bits64[0];
	uint64_t borrow = (end.bits64[0] < start.bits64[0]) ? 1ULL : 0ULL;
	for (int i = 1; i < NB64BLOCK; i++) {
		const uint64_t ei = end.bits64[i];
		const uint64_t si = start.bits64[i];
		const uint64_t si_borrow = si + borrow;
		const uint64_t di = ei - si_borrow;
		if (di != 0) return false;
		borrow = (ei < si_borrow) ? 1ULL : 0ULL;
	}
	if (borrow) return false;
	out = d0;
	return true;
}

// Cleanup function for general resources to prevent memory leaks
static void cleanup_general_resources(void) {
	// Free addressTable if allocated
	if (addressTable != NULL) {
		free(addressTable);
		addressTable = NULL;
	}
	// Note: secp is cleaned up separately as it's declared after this function
}

// Cleanup function for BSGS resources to prevent memory leaks
static void cleanup_bsgs_resources(void) {
	// Cleanup general resources first
	cleanup_general_resources();

	// Only cleanup BSGS specific resources if BSGS mode was used
	if (!FLAGBSGSMODE) return;

	// Free bPtable
	if (bPtable != NULL) {
		free(bPtable);
		bPtable = NULL;
	}

	// Free bloom filters (256 elements each)
	if (bloom_bP != NULL) {
		for (int i = 0; i < 256; i++) {
			bloom_ext_free(&bloom_bP[i]);
		}
		free(bloom_bP);
		bloom_bP = NULL;
	}

	if (bloom_bPx2nd != NULL) {
		for (int i = 0; i < 256; i++) {
			bloom_ext_free(&bloom_bPx2nd[i]);
		}
		free(bloom_bPx2nd);
		bloom_bPx2nd = NULL;
	}

	if (bloom_bPx3rd != NULL) {
		for (int i = 0; i < 256; i++) {
			bloom_ext_free(&bloom_bPx3rd[i]);
		}
		free(bloom_bPx3rd);
		bloom_bPx3rd = NULL;
	}

	// Free checksums
	if (bloom_bP_checksums != NULL) {
		free(bloom_bP_checksums);
		bloom_bP_checksums = NULL;
	}
	if (bloom_bPx2nd_checksums != NULL) {
		free(bloom_bPx2nd_checksums);
		bloom_bPx2nd_checksums = NULL;
	}
	if (bloom_bPx3rd_checksums != NULL) {
		free(bloom_bPx3rd_checksums);
		bloom_bPx3rd_checksums = NULL;
	}

	// Free mutexes
	if (bloom_bP_mutex != NULL) {
		for (int i = 0; i < 256; i++) {
			platform_mutex_destroy(&bloom_bP_mutex[i]);
		}
		free(bloom_bP_mutex);
		bloom_bP_mutex = NULL;
	}
	if (bloom_bPx2nd_mutex != NULL) {
		for (int i = 0; i < 256; i++) {
			platform_mutex_destroy(&bloom_bPx2nd_mutex[i]);
		}
		free(bloom_bPx2nd_mutex);
		bloom_bPx2nd_mutex = NULL;
	}
	if (bloom_bPx3rd_mutex != NULL) {
		for (int i = 0; i < 256; i++) {
			platform_mutex_destroy(&bloom_bPx3rd_mutex[i]);
		}
		free(bloom_bPx3rd_mutex);
		bloom_bPx3rd_mutex = NULL;
	}

	// Clear vectors
	BSGS_AMP2.clear();
	BSGS_AMP2.shrink_to_fit();
	BSGS_AMP3.clear();
	BSGS_AMP3.shrink_to_fit();
	GSn.clear();
	GSn.shrink_to_fit();
}

Int lambda,lambda2,beta,beta2;

Secp256K1 *secp;

#ifndef _WIN64
	static void configure_work_queue(size_t threadCount) {
		if (FLAGRANDOM) {
			g_workQueue.shutdown();
			return;
		}
		// NOTE: chunk size must match the per-thread sequential loop (N_SEQUENTIAL_MAX),
		// otherwise CPU threads would overlap/skip work.
		uint64_t chunk = N_SEQUENTIAL_MAX;
		size_t prefetch = std::max<size_t>(threadCount * 4, static_cast<size_t>(64));
		g_workQueue.configure(&n_range_start, &n_range_end, chunk, prefetch);
		g_workQueue.start();
	}

static void shutdown_work_queue() {
	g_workQueue.shutdown();
}
#endif

static void maybe_adjust_cpu_sequential_max(size_t threadCount,
                                            Int &cpuStart,
                                            Int &rangeEnd,
                                            const char *env_override,
                                            const char *tag) {
	if (FLAG_N) {
		return;
	}
	if (threadCount == 0) {
		return;
	}
	uint64_t span = 0;
	if (!span_u64_from_range(cpuStart, rangeEnd, span) || span == 0) {
		return;
	}

	// If the span already contains plenty of chunks, keep the default larger N to avoid extra setup overhead.
	{
		const uint64_t min_blocks = (uint64_t)threadCount * 4ULL;
		if (N_SEQUENTIAL_MAX > 0 && (span / N_SEQUENTIAL_MAX) >= min_blocks) {
			return;
		}
	}

	const char *env = (env_override && env_override[0]) ? getenv(env_override) : NULL;
	if (env && env[0]) {
		uint64_t forced = 0;
		if (env[0] == '0' && env[1] == 'x') {
			forced = strtoull(env + 2, NULL, 16);
		} else {
			forced = strtoull(env, NULL, 10);
		}
		if (forced >= 1024 && (forced % 1024ULL) == 0) {
			if (forced != N_SEQUENTIAL_MAX) {
				N_SEQUENTIAL_MAX = forced;
				output_info("%s: forced CPU N to 0x%llx via %s\n",
				       tag ? tag : "CPU",
				       (unsigned long long)N_SEQUENTIAL_MAX,
				       env_override ? env_override : "ENV");
			}
		}
		return;
	}

	const uint64_t blocks_per_thread = 16ULL;
	uint64_t desired = (span + (uint64_t)threadCount * blocks_per_thread - 1ULL) /
	                   ((uint64_t)threadCount * blocks_per_thread);

	const uint64_t kAlign = 1024ULL;
	const uint64_t kMin = 8ULL * kAlign;                 // 8k keys
	const uint64_t kMax = 256ULL * 1024ULL * kAlign;     // 256M keys

	if (desired < kMin) desired = kMin;
	if (desired > kMax) desired = kMax;
	desired = ((desired + kAlign - 1) / kAlign) * kAlign;

	if (desired < N_SEQUENTIAL_MAX) {
		N_SEQUENTIAL_MAX = desired;
		output_info("%s: adjusted CPU N to 0x%llx for better thread utilization\n",
		       tag ? tag : "CPU",
		       (unsigned long long)N_SEQUENTIAL_MAX);
	}
}

static void initialize_range_progress_tracker() {
	// Always set range bounds for writekey validation (prevents out-of-range key writes)
	g_rangeProgressStart.Set(&n_range_start);
	g_rangeProgressEnd.Set(&n_range_end);

	if (!FLAGPROGRESSBAR) {
		g_rangeProgressEnabled = false;
		g_rangeProgressSpan.SetInt32(0);
		return;
	}
	if (FLAGMODE == MODE_BSGS) {
		g_rangeProgressEnabled = false;
		g_rangeProgressSpan.SetInt32(0);
		return;
	}
	if (n_range_start.IsGreaterOrEqual(&n_range_end)) {
		g_rangeProgressEnabled = false;
		g_rangeProgressSpan.SetInt32(0);
		return;
	}
	g_rangeProgressSpan.Set(&n_range_end);
	g_rangeProgressSpan.Sub(&n_range_start);
	g_rangeProgressEnabled = !g_rangeProgressSpan.IsZero();
}

static void format_hex_position(Int &value, char *out, size_t outSize) {
	if (outSize == 0) {
		return;
	}
	char *hex = value.GetBase16();
	if (hex == NULL) {
		snprintf(out, outSize, "0x0");
		return;
	}
	size_t hexLen = strlen(hex);
	if (hexLen <= 12) {
		snprintf(out, outSize, "0x%s", hex);
	} else {
		snprintf(out, outSize, "0x%.6s..%s", hex, hex + (hexLen > 6 ? hexLen - 6 : 0));
	}
	free(hex);
}

static bool snapshot_range_next_key(Int &out) {
	if (!g_rangeProgressEnabled) {
		return false;
	}
	platform_mutex_lock(&write_random);
	out.Set(&n_range_start);
	platform_mutex_unlock(&write_random);
	return true;
}

static bool capture_progress_metrics(int &permille, char *position, size_t positionSize) {
	if (!g_rangeProgressEnabled || positionSize == 0) {
		return false;
	}

	Int consumed;
	Int nextKey;

	const bool count_based_progress = (FLAGRANDOM || FLAGGPU_HYBRID || FLAGGPU_FULL);
	if (count_based_progress) {
		// Count-based progress (random mode and any GPU-assisted mode):
		// use total checked keys vs range size. This is also the only safe choice in HYBRID,
		// because CPU/GPU advance different sub-ranges.
		Int total_checked;
		total_checked.SetInt32(0);

		if (steps != NULL) {
			Int thread_total;
			for (int j = 0; j < NTHREADS; j++) {
				thread_total.Set(&BSGS_N);  // BSGS_N is DEBUGCOUNT in non-BSGS modes
				thread_total.Mult(steps[j].value);
				total_checked.Add(&thread_total);
			}
		}

			if (FLAGGPU_HYBRID || FLAGGPU_FULL) {
				uint64_t gpu_total_u64 = gpu_keys_checked_total_u64();
				char tmp[64];
				snprintf(tmp, sizeof(tmp), "%" PRIu64, gpu_total_u64);
				Int gpu_total;
				gpu_total.SetBase10(tmp);
				total_checked.Add(&gpu_total);
			}

		// Apply multipliers for endomorphism.
		if (FLAGENDOMORPHISM) {
			if (FLAGMODE == MODE_XPOINT) {
				total_checked.Mult(3);
			} else {
				total_checked.Mult(6);
			}
		}

		consumed.Set(&total_checked);

		if (g_rangeProgressSpan.IsZero()) {
			permille = 0;
		} else {
			Int scaled;
			scaled.Set(&consumed);
			scaled.Mult(1000);
			scaled.Div(&g_rangeProgressSpan);
			permille = static_cast<int>(scaled.GetInt32());
			if (permille > 1000) {
				permille = 1000;
			} else if (permille < 0) {
				permille = 0;
			}
		}

		char *hex_consumed = consumed.GetBase16();
		if (hex_consumed != NULL) {
			snprintf(position, positionSize, "~%s checked", hex_consumed);
			free(hex_consumed);
		} else {
			snprintf(position, positionSize, "checked");
		}
	} else {
		// Sequential mode: use next key position
		if (!snapshot_range_next_key(nextKey)) {
			return false;
		}
		if (nextKey.IsLower(&g_rangeProgressStart)) {
			nextKey.Set(&g_rangeProgressStart);
		}
		consumed.Set(&nextKey);
		consumed.Sub(&g_rangeProgressStart);
		if (consumed.IsNegative()) {
			consumed.SetInt32(0);
		}
		if (consumed.IsGreater(&g_rangeProgressSpan)) {
			consumed.Set(&g_rangeProgressSpan);
			nextKey.Set(&g_rangeProgressEnd);
		}
		if (g_rangeProgressSpan.IsZero()) {
			permille = 1000;
		} else {
			Int scaled;
			scaled.Set(&consumed);
			scaled.Mult(1000);
			scaled.Div(&g_rangeProgressSpan);
			permille = static_cast<int>(scaled.GetInt32());
			if (permille > 1000) {
				permille = 1000;
			} else if (permille < 0) {
				permille = 0;
			}
		}
		format_hex_position(nextKey, position, positionSize);
	}

	return true;
}

static void append_progress_info(char *buffer, size_t bufferSize) {
	if (!g_rangeProgressEnabled || bufferSize < 4) {
		return;
	}
	int permille = 0;
	char position[48];
	if (!capture_progress_metrics(permille, position, sizeof(position))) {
		return;
	}
	const int segments = 20;
	int filled = (permille * segments) / 1000;
	int remainder = (permille * segments) % 1000;
	char bar[segments + 1];
	for (int i = 0; i < segments; ++i) {
		if (i < filled) {
			bar[i] = '=';
		} else if (i == filled && remainder > 0 && filled < segments) {
			bar[i] = '>';
		} else {
			bar[i] = '.';
		}
	}
	if (filled >= segments) {
		bar[segments - 1] = '=';
	}
	bar[segments] = '\0';
	int percent = permille / 10;
	int tenths = permille % 10;
	char addition[160];
	snprintf(addition, sizeof(addition), " | [%s] %d.%d%% @ %s", bar, percent, tenths, position);
	size_t len = strlen(buffer);
	char tail = 0;
	if (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
		tail = buffer[len - 1];
		buffer[len - 1] = '\0';
		len--;
	}
	size_t remaining = (len < bufferSize) ? bufferSize - len : 0;
	if (remaining > 1) {
		strncat(buffer, addition, remaining - 1);
		len = strlen(buffer);
	}
	if (tail != 0 && len + 1 < bufferSize) {
		buffer[len] = tail;
		buffer[len + 1] = '\0';
	}
}

#ifndef _WIN64
typedef struct {
	int period_seconds;
	std::atomic<int> *stop_flag;
} gpu_full_stats_args_t;

static void *gpu_full_stats_thread(void *arg) {
	gpu_full_stats_args_t *args = (gpu_full_stats_args_t *)arg;
	if (args == NULL || args->stop_flag == NULL) {
		return NULL;
	}
	const int period = args->period_seconds;
	if (period <= 0) {
		return NULL;
	}

	uint64_t prev_total = 0;
	uint64_t seconds = 0;
	while (!args->stop_flag->load(std::memory_order_acquire)) {
		sleep_ms(1000);
		seconds++;
		if (args->stop_flag->load(std::memory_order_acquire)) {
			break;
		}
		if ((seconds % (uint64_t)period) != 0) {
			continue;
		}

		uint64_t total_u64 = g_gpu_keys_checked.load(std::memory_order_relaxed);
		uint64_t delta_u64 = total_u64 - prev_total;

		Int total_i;
		Int delta_i;
		Int rate_i;
		{
			char tmp[64];
			snprintf(tmp, sizeof(tmp), "%" PRIu64, total_u64);
			total_i.SetBase10(tmp);
			snprintf(tmp, sizeof(tmp), "%" PRIu64, delta_u64);
			delta_i.SetBase10(tmp);
		}

		rate_i.Set(&delta_i);
		Int period_i;
		period_i.SetInt32(period);
		rate_i.Div(&period_i);

		char gpu_rate_str[128];
		format_keys_per_second(rate_i, gpu_rate_str, sizeof(gpu_rate_str));

		char *str_total = total_i.GetBase10();
		char seconds_buf[32];
		snprintf(seconds_buf, sizeof(seconds_buf), "%" PRIu64, seconds);

		char buffer[512];
		snprintf(buffer, sizeof(buffer),
		         "[+] Total %s keys in %s seconds (last %d s): CPU 0 keys/s | GPU %s | TOTAL %s\n",
		         str_total ? str_total : "?", seconds_buf, period, gpu_rate_str, gpu_rate_str);
		append_progress_info(buffer, sizeof(buffer));
		append_profile_info(buffer, sizeof(buffer));
		append_adaptive_info(buffer, sizeof(buffer));
		printf("%s", buffer);
		fflush(stdout);

		if (str_total) free(str_total);
		prev_total = total_u64;
	}

	return NULL;
}
#endif

// Thread-local block cache for work-stealing mode
// Each CPU thread caches a block from the work pool to reduce contention
static thread_local Int cpu_cached_block_start;
static thread_local Int cpu_cached_block_end;
static thread_local bool cpu_cached_block_valid = false;

static bool acquire_base_key(Int &key) {
	// Work pool mode for hybrid (work-stealing)
	if (g_work_pool.enabled.load(std::memory_order_acquire)) {
		// Check if we have a valid cached block with remaining work
		if (!cpu_cached_block_valid || !cpu_cached_block_start.IsLower(&cpu_cached_block_end)) {
			// Get a new block from the work pool
			if (!g_work_pool.get_block(cpu_cached_block_start, cpu_cached_block_end)) {
				return false;  // No more work available
			}
			cpu_cached_block_valid = true;
		}
		// IMPORTANT: Check block boundary BEFORE returning the key
		// This ensures we never return a key outside the assigned block
		if (!cpu_cached_block_start.IsLower(&cpu_cached_block_end)) {
			// Current position already at or past block end, need new block
			cpu_cached_block_valid = false;
			return acquire_base_key(key);  // Retry with new block
		}
		// Return next key from cached block
		key.Set(&cpu_cached_block_start);
		cpu_cached_block_start.Add(N_SEQUENTIAL_MAX);
		return true;
	}

#ifndef _WIN64
	if (!FLAGRANDOM && g_workQueue.enabled()) {
		return g_workQueue.pop(key);
	}
#endif
	if (FLAGRANDOM)	{
		key.Rand(&n_range_start,&n_range_end);
		return true;
	}
	platform_mutex_lock(&write_random);
	bool hasWork = n_range_start.IsLower(&n_range_end);
	if(hasWork)	{
		key.Set(&n_range_start);
		n_range_start.Add(N_SEQUENTIAL_MAX);
	}
	platform_mutex_unlock(&write_random);
	return hasWork;
}

static void process_rmd160_batch_btc_simple(Int &key_mpz, Point *pts, uint64_t &count) {
	const bool wantCompressed = (FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH);
	const bool wantUncompressed = (FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH);
	const bool haveYForCompressed = (wantUncompressed || cpu_use_y_parity_for_compressed_btc());

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
					// Only use GPU hash-only offload in HASH mode; in FULL/HYBRID it would contend with the GPU search.
					if (FLAGGPU == 1 && FLAGGPU_FULL == 0 && gpu_backend_available()) {
						KH_PROF_SCOPE(ns_hash);
						alignas(32) uint8_t x32_be[CPU_GRP_SIZE * 32];
						for (size_t idx = 0; idx < CPU_GRP_SIZE; ++idx) {
							pts[idx].x.Get32Bytes(x32_be + idx * 32);
						}
					if (gpu_hash160_fromX_batch(x32_be, CPU_GRP_SIZE,
							(uint8_t*)hashCompressed02[0], (uint8_t*)hashCompressed03[0]) != 0) {
						// Fallback to CPU path if GPU hashing fails for any reason.
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
			// AVX-512 path: process 16 hashes at a time
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
			// AVX2 path: process 8 hashes at a time
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
			// SSE2 fallback: process 4 hashes at a time
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
	// Note: stride is a global variable, no need to capture it
	auto computeKeyAtIndex = [&key_mpz](Int &out, size_t idx) {
		Int offset;
		offset.SetInt64((int64_t)idx);
		offset.Mult(&stride);  // stride is global
		out.Set(&key_mpz);
		out.Add(&offset);
	};

			for (size_t base = 0; base < CPU_GRP_SIZE; base += BATCH_SIZE) {
				const size_t batchEnd = (base + BATCH_SIZE > CPU_GRP_SIZE) ? CPU_GRP_SIZE : base + BATCH_SIZE;
				const int batchCount = (int)(batchEnd - base);

				if (wantCompressed) {
					if (haveYForCompressed) {
						// Y is available: compute only the actual compressed hash (single parity)
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
								hits = bloom_ext_check_rmd160_strided(&bloom, (const uint8_t*)hashCompressed02[base], HASH_STRIDE, batchCount);
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
						// Y is not computed: check both parities from X (02 and 03)
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
								hits02 = bloom_ext_check_rmd160_strided(&bloom, (const uint8_t*)hashCompressed02[base], HASH_STRIDE, batchCount);
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
								hits03 = bloom_ext_check_rmd160_strided(&bloom, (const uint8_t*)hashCompressed03[base], HASH_STRIDE, batchCount);
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
				// Batch bloom check for uncompressed hashes
				uint64_t hitsU;
				{
					KH_PROF_SCOPE(ns_bloom);
					hitsU = bloom_ext_check_rmd160_strided(&bloom, (const uint8_t*)hashUncompressed[base], HASH_STRIDE, batchCount);
				}
				// Process hits
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

static bool gpu_selftest_hash160_fromX() {
	const size_t kCount = 16;
	alignas(32) uint8_t x32_be[kCount * 32];
	Int xs[kCount];

	for (size_t i = 0; i < kCount; ++i) {
		uint8_t bytes[32];
		for (size_t j = 0; j < 32; ++j) {
			bytes[j] = (uint8_t)((i * 17 + j) & 0xFF);
		}
		xs[i].Set32Bytes(bytes);
		memcpy(x32_be + i * 32, bytes, 32);
	}

	alignas(32) uint8_t cpu02[kCount][20];
	alignas(32) uint8_t cpu03[kCount][20];
	for (size_t i = 0; i < kCount; i += 4) {
		secp->GetHash160_fromX(P2PKH, 0x02, &xs[i], &xs[i + 1], &xs[i + 2], &xs[i + 3],
			cpu02[i], cpu02[i + 1], cpu02[i + 2], cpu02[i + 3]);
		secp->GetHash160_fromX(P2PKH, 0x03, &xs[i], &xs[i + 1], &xs[i + 2], &xs[i + 3],
			cpu03[i], cpu03[i + 1], cpu03[i + 2], cpu03[i + 3]);
	}

	alignas(32) uint8_t gpu02[kCount][20];
	alignas(32) uint8_t gpu03[kCount][20];
	if (gpu_hash160_fromX_batch(x32_be, kCount, gpu02[0], gpu03[0]) != 0) {
		output_error("GPU self-test failed: CUDA hash160 call failed\n");
		return false;
	}

	for (size_t i = 0; i < kCount; ++i) {
		if (memcmp(cpu02[i], gpu02[i], 20) != 0) {
			output_error("GPU self-test failed: mismatch prefix 02 at index %zu\n", i);
			return false;
		}
		if (memcmp(cpu03[i], gpu03[i], 20) != 0) {
			output_error("GPU self-test failed: mismatch prefix 03 at index %zu\n", i);
			return false;
		}
	}

	return true;
}

int main(int argc, char **argv)	{
	char buffer[2048];
	char rawvalue[32];
	struct tothread *tt;	//tothread
	Tokenizer t{};	//tokenizer
	Tokenizer tokenizerbsgs{};	//tokenizer
	char *fileName = NULL;
	char *hextemp = NULL;
	char *aux = NULL;
	char *aux2 = NULL;
	char *pointx_str = NULL;
	char *pointy_str = NULL;
	char *str_seconds = NULL;
	char *str_total = NULL;
	char *str_pretotal = NULL;
	char *str_divpretotal = NULL;
	char *bPload_threads_available;
	FILE *fd,*fd_aux1,*fd_aux2,*fd_aux3;
	uint64_t i,BASE,PERTHREAD_R,itemsbloom,itemsbloom2,itemsbloom3;
	uint32_t finished;
	int readed,continue_flag,check_flag,c,salir,index_value,j;
	Int total,pretotal,debugcount_mpz,seconds,div_pretotal,int_aux,int_r,int_q,int58;
	struct bPload *bPload_temp_ptr;
	size_t rsize;

	// Hybrid mode variables (GPU + CPU parallel)
	platform_thread_t gpu_thread_id = 0;
	gpu_hybrid_args_t gpu_hybrid_args = {};
	int gpu_hybrid_started = 0;

	int s;
	platform_mutex_init(&write_keys);
	platform_mutex_init(&write_random);
	platform_mutex_init(&bsgs_thread);

	srand(time(NULL));

	// Register cleanup function for memory leak prevention
	atexit(cleanup_bsgs_resources);

	secp = new Secp256K1();
	secp->Init();
	OUTPUTSECONDS.SetInt32(30);
	ZERO.SetInt32(0);
	ONE.SetInt32(1);
	BSGS_GROUP_SIZE.SetInt32(CPU_GRP_SIZE);
	
#if defined(_WIN64) && !defined(__CYGWIN__)
	//Any windows secure random source goes here
	rseed(clock() + time(NULL) + thread_rand());
#else
	unsigned long rseedvalue;
	int bytes_read = getrandom(&rseedvalue, sizeof(unsigned long), GRND_NONBLOCK);
	if(bytes_read > 0)	{
		rseed(rseedvalue);
		/*
		In any case that seed is for a failsafe RNG, the default source on linux is getrandom function
		See https://www.2uo.de/myths-about-urandom/
		*/
	}
	else	{
		/*
		 * Fallback: Use time-based seed if getrandom() fails
		 * This can happen in containers, restricted environments, or systems with low entropy
		 */
		output_warning("getrandom() failed (bytes_read=%d), using fallback RNG\n", bytes_read);
		rseed(clock() + time(NULL) + thread_rand() * thread_rand());
	}
#endif
	// Pre-scan for -q flag to initialize output correctly from the start
	// This prevents early messages from being shown in quiet mode
	bool early_quiet = false;
	for (int qi = 1; qi < argc; qi++) {
		if (strcmp(argv[qi], "-q") == 0) {
			early_quiet = true;
			break;
		}
	}
	output_init(early_quiet ? OUTPUT_MINIMAL : OUTPUT_NORMAL);

	output_success("Version %s, developed by AlbertoBSD\n",version);

	g_profile_enabled = env_truthy_kh("KEYHUNT_PROFILE");
	if (g_profile_enabled) {
		output_info("Profiling enabled (KEYHUNT_PROFILE=1)\n");
	}

	// -------------------------------------------------------------------------
	// Initialize unified configuration structure
	// -------------------------------------------------------------------------
	keyhunt_config_t config;
	kh_config_init(&config);

	// Auto-detect system configuration and optimize parameters
	// (sysinfo is now a global variable for memory checks)

	// Check if user wants to skip system detection (useful for problematic systems)
	if (getenv("KEYHUNT_SKIP_SYSINFO")) {
		output_warning("Skipping system detection (KEYHUNT_SKIP_SYSINFO set)\n");
		output_info("Using safe default parameters\n");
		memset(&g_sysinfo, 0, sizeof(g_sysinfo));
		// Safe defaults
		g_sysinfo.cpu_physical_cores = 4;
		g_sysinfo.cpu_logical_cores = 8;
		g_sysinfo.cache_l1_size = 32;
		g_sysinfo.cache_l2_size = 256;
		g_sysinfo.cache_l3_size = 8192;
		g_sysinfo.ram_total = 8192;
		g_sysinfo.ram_available = 4096;
		g_sysinfo.has_avx2 = false;
		g_sysinfo.has_avx512 = false;
		g_sysinfo.has_sha_ni = false;
		g_sysinfo.recommended_threads = 8;
		g_sysinfo.recommended_batch_size = 1024;
		g_sysinfo.recommended_workload = 8192;
		g_sysinfo.recommended_n = 0x10000000000ULL;
		g_sysinfo.recommended_kfactor = 1024;
		} else {
			sysinfo_init(&g_sysinfo);
		}
		// Probe CUDA backend (if built)
		gpu_backend_init(&g_gpu_backend_info);

		// Detect AVX2 support for enhanced performance
		g_avx2_available = ripemd160_avx2_available();
	if (g_avx2_available) {
		output_success("AVX2 detected: Using optimized 8-way parallel RIPEMD160\n");
	} else {
		output_info("AVX2 not available: Using SSE2 4-way parallel RIPEMD160\n");
	}

	// Store auto-tuned recommendations (don't apply yet - wait for user args)
	OPTIMAL_THREADS = g_sysinfo.recommended_threads;
	OPTIMAL_N = g_sysinfo.recommended_n;
	OPTIMAL_KFACTOR = g_sysinfo.recommended_kfactor;

	// Keep CPU_GRP_SIZE at proven optimal value of 1024
	// Testing showed that larger values (2048, 4096) actually hurt performance
	// due to increased ModInv overhead and worse cache behavior
	CPU_GRP_SIZE = 1024;
	output_info("Using CPU_GRP_SIZE: %u (proven optimal)\n", CPU_GRP_SIZE);

	// -------------------------------------------------------------------------
	// Help and wizard mode check (before anything else)
	// -------------------------------------------------------------------------
	for (int ai = 1; ai < argc; ai++) {
		if (strcmp(argv[ai], "--help") == 0) {
			menu();
		}
		if (strcmp(argv[ai], "--wizard") == 0 || strcmp(argv[ai], "-W") == 0) {
			int result = wizard_run();
			exit(result < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
		}
		// Non-interactive wizard client mode (spawned by server)
		if (strcmp(argv[ai], "--wizard-client") == 0 && ai + 1 < argc) {
			const char *host_port = argv[ai + 1];
			int result = wizard_client_run_auto(host_port);
			exit(result < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
		}
		// Benchmark mode
		if (strcmp(argv[ai], "--benchmark") == 0) {
			benchmark_result_t bench_result;
			benchmark_run(&bench_result, 15);  // 15 second benchmark
			benchmark_print_results(&bench_result, 66);  // Default to puzzle 66
			exit(EXIT_SUCCESS);
		}
	}

	// -------------------------------------------------------------------------
	// Configuration file handling (before getopt so CLI can override)
	// -------------------------------------------------------------------------
	config_init(&g_config);

	// Pre-parse for --config and --save-config (long options)
	// Also remove them from argv so getopt doesn't choke on them
	const char *config_file_arg = NULL;
	int new_argc = 1;  // Keep argv[0]
	for (int ai = 1; ai < argc; ai++) {
		if (strcmp(argv[ai], "--config") == 0 && ai + 1 < argc) {
			config_file_arg = argv[ai + 1];
			ai++;  // Skip next arg too
			continue;
		} else if (strncmp(argv[ai], "--config=", 9) == 0) {
			config_file_arg = argv[ai] + 9;
			continue;
		} else if (strcmp(argv[ai], "--save-config") == 0) {
			g_save_config_path = "keyhunt.conf";  // Default
			continue;
		} else if (strncmp(argv[ai], "--save-config=", 14) == 0) {
			g_save_config_path = argv[ai] + 14;
			continue;
		}
		// Keep this argument
		argv[new_argc++] = argv[ai];
	}
	argc = new_argc;
	argv[argc] = NULL;  // Null-terminate

	// Load config file (explicit or default)
	if (config_file_arg) {
		if (config_load(&g_config, config_file_arg) == 0) {
			output_success("Loaded configuration from '%s'\n", config_file_arg);
			g_config_loaded = true;
		} else {
			output_error("Failed to load config file: %s\n", config_file_arg);
			exit(EXIT_FAILURE);
		}
	} else {
		// Try default config file (silently)
		if (config_load_default(&g_config) == 0) {
			output_success("Loaded configuration from 'keyhunt.conf'\n");
			g_config_loaded = true;
		}
	}

	// Apply config values as defaults (will be overridden by CLI args)
	if (g_config_loaded) {
		// Threads
		if (g_config.threads_set && g_config.threads > 0) {
			NTHREADS = g_config.threads;
			FLAGTHREADS = 1;  // Mark as user-specified
		}
		// GPU mode
		if (g_config.gpu_set) {
			if (g_config.gpu_enabled == 0) {
				FLAGGPU = 0;
				FLAGGPU_FULL = 0;
			} else if (g_config.gpu_enabled > 0) {
				FLAGGPU = 1;
				FLAGGPU_FULL = 1;
			}
		}
		// Mode
		if (g_config.mode_set) {
			if (strcasecmp(g_config.mode, "hybrid") == 0) {
				FLAGGPU = 1;
				FLAGGPU_FULL = 1;
				FLAGGPU_HYBRID.store(1, std::memory_order_relaxed);
			} else if (strcasecmp(g_config.mode, "gpu") == 0) {
				FLAGGPU = 1;
				FLAGGPU_FULL = 1;
			} else if (strcasecmp(g_config.mode, "cpu") == 0) {
				FLAGGPU = 0;
				FLAGGPU_FULL = 0;
			}
		}
		// Memory limit
		if (g_config.memory_limit_set) {
			// Store for later use in BSGS memory checks
			// (Memory limit is used in validate_bsgs_params)
		}
	}

	// Only show recommendations if user wants
	if (argc == 1 || (argc == 2 && strcmp(argv[1], "-h") == 0)) {
		sysinfo_print(&g_sysinfo);
	}

	while ((c = getopt(argc, argv, "deh6MqRSB:b:c:C:E:f:I:k:l:m:N:n:p:r:s:t:v:G:8:z:P")) != -1) {
		switch(c) {
			case 'h':
				menu();
			break;
			case '6':
				FLAGSKIPCHECKSUM = 1;
				output_warning("Skipping checksums on files\n");
			break;
			case 'B':
				index_value = indexOf(optarg,bsgs_modes,5);
				if(index_value >= 0 && index_value <= 4)	{
					FLAGBSGSMODE = index_value;
					//output_success("BSGS mode %s\n",optarg);
				}
				else	{
					output_warning("Ignoring unknow bsgs mode %s\n",optarg);
				}
			break;
			case 'b':
				bitrange = strtol(optarg,NULL,10);
				if(bitrange > 0 && bitrange <=256 )	{
					MPZAUX.Set(&ONE);
					MPZAUX.ShiftL(bitrange-1);
					bit_range_str_min = MPZAUX.GetBase16();
					checkpointer((void *)bit_range_str_min,__FILE__,"malloc","bit_range_str_min" ,__LINE__ -1);
					MPZAUX.Set(&ONE);
					MPZAUX.ShiftL(bitrange);
					if(MPZAUX.IsGreater(&secp->order))	{
						MPZAUX.Set(&secp->order);
					}
					bit_range_str_max = MPZAUX.GetBase16();
					checkpointer((void *)bit_range_str_max,__FILE__,"malloc","bit_range_str_min" ,__LINE__ -1);
					FLAGBITRANGE = 1;
				}
				else	{
					output_error("invalid bits param: %s.\n",optarg);
				}
			break;
			case 'c':
				index_value = indexOf(optarg,cryptos,3);
				switch(index_value) {
					case 0: //btc
						FLAGCRYPTO = CRYPTO_BTC;
					break;
					case 1: //eth
						FLAGCRYPTO = CRYPTO_ETH;
						output_success("Setting search for ETH adddress.\n");
					break;
					/*
					case 2: //all
						FLAGCRYPTO = CRYPTO_ALL;
					break;
					*/
					default:
						FLAGCRYPTO = CRYPTO_NONE;
						output_error("Unknown crypto value %s\n",optarg);
						exit(EXIT_FAILURE);
					break;
				}
			break;
			case 'C':
				if(strlen(optarg) == 22)	{
					FLAGBASEMINIKEY = 1;
					str_baseminikey = (char*) malloc(23);
					checkpointer((void *)str_baseminikey,__FILE__,"malloc","str_baseminikey" ,__LINE__ - 1);
					raw_baseminikey = (char*) malloc(23);
					checkpointer((void *)raw_baseminikey,__FILE__,"malloc","raw_baseminikey" ,__LINE__ - 1);
					strncpy(str_baseminikey,optarg,22);
					str_baseminikey[22] = '\0';
					for(i = 0; i< 21; i++)	{
						if(strchr(Ccoinbuffer,str_baseminikey[i+1]) != NULL)	{
							raw_baseminikey[i] = (int)(strchr(Ccoinbuffer,str_baseminikey[i+1]) - Ccoinbuffer) % 58;
						}
						else	{
							output_error("invalid character in minikey\n");
							exit(EXIT_FAILURE);
						}
						
					}
					raw_baseminikey[21] = '\0';
				}
				else	{
					output_error("Invalid Minikey length %zu : %s\n",strlen(optarg),optarg);
					exit(EXIT_FAILURE);
				}
				
			break;
			case 'd':
				FLAGDEBUG = 1;
				output_success("Flag DEBUG enabled\n");
			break;
			case 'e':
				FLAGENDOMORPHISM = 1;
				output_success("Endomorphism enabled\n");
				lambda.SetBase16("5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
				lambda2.SetBase16("ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");
				beta.SetBase16("7ae96a2b657c07106e64479eac3434e99cf0497512f58995c1396c28719501ee");
				beta2.SetBase16("851695d49a83f8ef919bb86153cbcb16630fb68aed0a766a3ec693d68e6afa40");
			break;
			case 'f':
				FLAGFILE = 1;
				fileName = optarg;
			break;
			case 'G':
				if (optarg) {
					if (strcasecmp(optarg, "off") == 0 || strcasecmp(optarg, "no") == 0 || strcmp(optarg, "0") == 0) {
						FLAGGPU = 0;
						FLAGGPU_FULL = 0;
					} else if (strcasecmp(optarg, "auto") == 0) {
						FLAGGPU = -1;  // Will be resolved later based on availability
						FLAGGPU_FULL = -1;  // Auto-detect: use full if available
					} else if (strcasecmp(optarg, "hash") == 0) {
						FLAGGPU = 1;
						FLAGGPU_FULL = 0;  // Hash-only mode
						output_success("GPU hash-only mode (CPU generates points, GPU hashes)\n");
					} else if (strcasecmp(optarg, "full") == 0 || strcasecmp(optarg, "on") == 0 || strcasecmp(optarg, "yes") == 0 || strcmp(optarg, "1") == 0) {
						FLAGGPU = 1;
						FLAGGPU_FULL = 1;  // Full GPU mode
						output_success("GPU full mode (ECC + hash160 + matching on GPU)\n");
					} else if (strcasecmp(optarg, "hybrid") == 0) {
						FLAGGPU = 1;
						FLAGGPU_FULL = 1;
						FLAGGPU_HYBRID.store(1, std::memory_order_relaxed);  // Hybrid mode: GPU + CPU in parallel
						output_success("GPU hybrid mode (GPU + CPU in parallel for maximum throughput)\n");
					} else {
						output_warning("Invalid -G value '%s', use: off|auto|hash|full|hybrid\n", optarg);
					}
				}
			break;
				case 'I':
					FLAGSTRIDE = 1;
					str_stride = optarg;
			break;
			case 'k':
				KFACTOR = (int)strtol(optarg,NULL,10);
				if(KFACTOR <= 0)	{
					KFACTOR = 1;
				}
				output_success("K factor %i\n",KFACTOR);
			break;

			case 'l':
				switch(indexOf(optarg,publicsearch,3)) {
					case SEARCH_UNCOMPRESS:
						FLAGSEARCH = SEARCH_UNCOMPRESS;
						output_success("Search uncompress only\n");
					break;
					case SEARCH_COMPRESS:
						FLAGSEARCH = SEARCH_COMPRESS;
						output_success("Search compress only\n");
					break;
					case SEARCH_BOTH:
						FLAGSEARCH = SEARCH_BOTH;
						output_success("Search both compress and uncompress\n");
					break;
				}
			break;
			case 'M':
				FLAGMATRIX = 1;
				output_success("Matrix screen\n");
			break;
			case 'P':
				FLAGPROGRESSBAR = 1;
				output_success("Segmented progress indicator enabled\n");
			break;
			case 'm':
				switch(indexOf(optarg,modes,7)) {
					case MODE_XPOINT: //xpoint
						FLAGMODE = MODE_XPOINT;
						output_success("Mode xpoint\n");
					break;
					case MODE_ADDRESS: //address
						FLAGMODE = MODE_ADDRESS;
						output_success("Mode address\n");
					break;
					case MODE_BSGS:
						FLAGMODE = MODE_BSGS;
						//output_success("Mode BSGS\n");
					break;
					case MODE_RMD160:
						FLAGMODE = MODE_RMD160;
						FLAGCRYPTO = CRYPTO_BTC;
						output_success("Mode rmd160\n");
					break;
					case MODE_PUB2RMD:
						FLAGMODE = MODE_PUB2RMD;
						output_success("Mode pub2rmd was removed\n");
						exit(0);
					break;
					case MODE_MINIKEYS:
						FLAGMODE = MODE_MINIKEYS;
						output_success("Mode minikeys\n");
					break;
					case MODE_VANITY:
						FLAGMODE = MODE_VANITY;
						output_success("Mode vanity\n");
						if(vanity_bloom == NULL){
							vanity_bloom = (struct bloom*) calloc(1,sizeof(struct bloom));
							checkpointer((void *)vanity_bloom,__FILE__,"calloc","vanity_bloom" ,__LINE__ -1);
						}
					break;
					default:
						output_error("Unknown mode value %s\n",optarg);
						exit(EXIT_FAILURE);
					break;
				}
			break;
			case 'n':
				FLAG_N = 1;
				str_N = optarg;
			break;
			case 'q':
				FLAGQUIET	= 1;
				/* Message suppressed - quiet mode means minimal output */
			break;
			case 'R':
				output_success("Random mode\n");
				FLAGRANDOM = 1;
				FLAGBSGSMODE =  3;
			break;
			case 'r':
				if(optarg != NULL)	{
					stringtokenizer(optarg,&t);
					switch(t.n)	{
						case 1:
							range_start = nextToken(&t);
							if(isValidHex(range_start)) {
								FLAGRANGE = 1;
								range_end = secp->order.GetBase16();
							}
							else	{
								output_error("Invalid hexstring : %s.\n",range_start);
							}
						break;
						case 2:
							range_start = nextToken(&t);
							range_end	 = nextToken(&t);
							if(isValidHex(range_start) && isValidHex(range_end)) {
									FLAGRANGE = 1;
							}
							else	{
								if(isValidHex(range_start)) {
									output_error("Invalid hexstring : %s\n",range_start);
								}
								else	{
									output_error("Invalid hexstring : %s\n",range_end);
								}
							}
						break;
						default:
							output_error("Unknown number of Range Params: %i\n",t.n);
						break;
					}
				}
			break;
			case 's':
				OUTPUTSECONDS.SetBase10(optarg);
				if(OUTPUTSECONDS.IsLower(&ZERO))	{
					OUTPUTSECONDS.SetInt32(30);
				}
				if(OUTPUTSECONDS.IsZero())	{
					output_success("Turn off stats output\n");
				}
				else	{
					hextemp = OUTPUTSECONDS.GetBase10();
					output_success("Stats output every %s seconds\n",hextemp);
					free(hextemp);
				}
			break;
			case 'S':
				FLAGSAVEREADFILE = 1;
			break;
			case 't':
				NTHREADS = strtol(optarg,NULL,10);
				if(NTHREADS <= 0)	{
					NTHREADS = 1;
				}
				FLAGTHREADS = 1;
				output_success((NTHREADS > 1) ? "Threads : %u (user-specified)\n": "Thread : %u (user-specified)\n",NTHREADS);
			break;
			case 'v':
				FLAGVANITY = 1;
				if(vanity_bloom == NULL){
					vanity_bloom = (struct bloom*) calloc(1,sizeof(struct bloom));
					checkpointer((void *)vanity_bloom,__FILE__,"calloc","vanity_bloom" ,__LINE__ -1);
				}
				if(isValidBase58String(optarg))	{
					if(addvanity(optarg) > 0)	{
						output_success("Added Vanity search : %s\n",optarg);
					}
					else	{
						output_success("Vanity search \"%s\" was NOT Added\n",optarg);
					}
				}
				else {
					output_success("The string \"%s\" is not Valid Base58\n",optarg);
				}
				
			break;
			case '8':
				if(strlen(optarg) == 58)	{
					Ccoinbuffer = optarg; 
					output_success("Base58 for Minikeys %s\n",Ccoinbuffer);
				}
				else	{
					output_error("The base58 alphabet must be 58 characters long.\n");
					exit(EXIT_FAILURE);
				}
			break;
			case 'z':
				FLAGBLOOMMULTIPLIER= strtol(optarg,NULL,10);
				if(FLAGBLOOMMULTIPLIER <= 0)	{
					FLAGBLOOMMULTIPLIER = 1;
				}
				output_success("Bloom Size Multiplier %i\n",FLAGBLOOMMULTIPLIER);
			break;
			default:
				output_error("Unknown option -%c\n",c);
				exit(EXIT_FAILURE);
			break;
		}

		}

	// Initialize output system based on quiet mode setting
	output_init(FLAGQUIET ? OUTPUT_MINIMAL : OUTPUT_NORMAL);

	// ========== Parameter Validation and Auto-Tuning ==========
	// Validate user parameters against hardware capabilities
	// Auto-correct dangerous values, warn about suboptimal ones
	{
		// Parse user's N value if provided (needed for validation)
		uint64_t user_n_value = 0;
		if (FLAG_N && str_N) {
			if (str_N[0] == '0' && str_N[1] == 'x') {
				user_n_value = strtoull(str_N + 2, NULL, 16);
			} else {
				user_n_value = strtoull(str_N, NULL, 10);
			}
		}

		// If user didn't specify -t, treat default (1) as "auto" for validation.
		int threads_to_validate = FLAGTHREADS ? NTHREADS : 0;

		// Run comprehensive parameter validation
		// This will auto-correct dangerous values and warn about suboptimal ones
		uint32_t batch_size = CPU_GRP_SIZE;
		bool validation_ok = validate_all_parameters(
			&threads_to_validate,   // in/out: may be corrected
			&user_n_value,          // in/out: may be corrected
			&KFACTOR,               // in/out: may be corrected
			&batch_size,            // in/out: may be corrected
			&g_sysinfo,
			true                    // auto_correct = true
		);

		// Apply validated parameters
		NTHREADS = threads_to_validate;
		CPU_GRP_SIZE = batch_size;

		// Update N value if it was corrected
		if (FLAG_N && user_n_value != 0) {
			// Convert back to string for later parsing
			char corrected_n[32];
			snprintf(corrected_n, sizeof(corrected_n), "0x%llx", (unsigned long long)user_n_value);
			str_N = strdup(corrected_n);
		}

		// If N wasn't specified and we're in BSGS mode, use recommended
		if (!FLAG_N && FLAGMODE == MODE_BSGS && OPTIMAL_N > 0) {
			char auto_n[32];
			snprintf(auto_n, sizeof(auto_n), "0x%llx", (unsigned long long)OPTIMAL_N);
			str_N = strdup(auto_n);
			FLAG_N = 1;
		}

		if (!validation_ok) {
			output_warning("Some parameters were auto-corrected for safety\n");
		}
	}
	// ========== End Parameter Validation ==========

	// ========== Display Configuration Summary ==========
	{
		const char *mode_name = (FLAGMODE >= 0 && FLAGMODE < 7) ? modes[FLAGMODE] : "unknown";
		const char *gpu_name = NULL;
		if (FLAGGPU || FLAGGPU_HYBRID) {
			gpu_name = g_gpu_backend_info.name[0] ? g_gpu_backend_info.name : "GPU";
		}
		output_banner(version, mode_name, NTHREADS, gpu_name, bitrange);
	}

	// ========== Save Configuration (if requested) ==========
	if (g_save_config_path) {
		// Update config with current (validated) values
		if (NTHREADS > 0) {
			g_config.threads = NTHREADS;
			g_config.threads_set = true;
		}

		// GPU mode
		if (FLAGGPU_HYBRID) {
			strncpy(g_config.mode, "hybrid", sizeof(g_config.mode) - 1);
		} else if (FLAGGPU && FLAGGPU_FULL) {
			strncpy(g_config.mode, "gpu", sizeof(g_config.mode) - 1);
		} else {
			strncpy(g_config.mode, "cpu", sizeof(g_config.mode) - 1);
		}
		g_config.mode_set = true;

		g_config.gpu_enabled = FLAGGPU ? 1 : 0;
		g_config.gpu_set = true;

		g_config.batch_size = CPU_GRP_SIZE;
		g_config.batch_size_set = true;

		// Save
		if (config_save(&g_config, g_save_config_path) == 0) {
			output_success("Configuration saved. You can now use it with: --config %s\n", g_save_config_path);
		} else {
			output_error("Failed to save configuration to %s. Check disk space and permissions.\n", g_save_config_path);
		}
	}
	// ========== End Save Configuration ==========

	if(  FLAGBSGSMODE == MODE_BSGS && FLAGENDOMORPHISM)	{
		output_error("Endomorphism doesn't work with BSGS\n");
		exit(EXIT_FAILURE);
	}


	if(  FLAGBSGSMODE == MODE_BSGS  && FLAGSTRIDE)	{
		output_error("Stride doesn't work with BSGS\n");
		exit(EXIT_FAILURE);
	}
	if(FLAGSTRIDE)	{
		if(str_stride[0] == '0' && str_stride[1] == 'x')	{
			stride.SetBase16(str_stride+2);
		}
		else{
			stride.SetBase10(str_stride);
		}
		output_success("Stride : %s\n",stride.GetBase10());
	}
	else	{
		FLAGSTRIDE = 1;
		stride.Set(&ONE);
	}
	init_generator();
	if(FLAGMODE == MODE_BSGS )	{
		output_success("Mode BSGS %s\n",bsgs_modes[FLAGBSGSMODE]);
	}
	
	if(FLAGFILE == 0) {
		fileName =(char*) default_fileName;
	}
		
		if(FLAGMODE == MODE_ADDRESS && FLAGCRYPTO == CRYPTO_NONE) {	//When none crypto is defined the default search is for Bitcoin
			FLAGCRYPTO = CRYPTO_BTC;
			output_success("Setting search for btc adddress\n");
		}
		if(FLAGMODE == MODE_RMD160 && FLAGCRYPTO == CRYPTO_NONE) {	// Default rmd160 search is Bitcoin HASH160 (same pipeline as address mode)
			FLAGCRYPTO = CRYPTO_BTC;
			output_success("Setting search for btc rmd160\n");
		}

	// ============================================================================
	// GPU Mode Resolution and Validation
	// ============================================================================
			{
				int gpu_available = gpu_backend_available();
				const bool wantCompressed = (FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH);
				const bool wantUncompressed = (FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH);
				const int mode_supports_gpu_full = (FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_ADDRESS) &&
										FLAGCRYPTO == CRYPTO_BTC &&
										!FLAGENDOMORPHISM &&
										(wantCompressed || wantUncompressed);
				const int mode_supports_gpu_hash = (FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_ADDRESS) &&
										FLAGCRYPTO == CRYPTO_BTC &&
										!FLAGENDOMORPHISM &&
										wantCompressed && !wantUncompressed;

		// Show GPU backend status
		if (FLAGGPU != 0 || FLAGGPU_FULL != 0) {
			if (gpu_available) {
				output_success("CUDA backend: %s (%d MPs, %lu MB VRAM)\n",
					g_gpu_backend_info.name[0] ? g_gpu_backend_info.name : "NVIDIA GPU",
					g_gpu_backend_info.multiprocessors,
					(unsigned long)g_gpu_backend_info.vram_mb);
			} else {
#ifdef HAVE_CUDA_BACKEND
				output_warning("CUDA backend compiled but no GPU detected\n");
#else
				output_warning("CUDA backend not compiled (nvcc not found at build time)\n");
				output_info("To enable GPU: install CUDA toolkit, ensure 'nvcc' is in PATH, rebuild\n");
#endif
			}
		}

			// Resolve auto mode (-G auto)
			if (FLAGGPU == -1 || FLAGGPU_FULL == -1) {
				if (gpu_available && mode_supports_gpu_full) {
					// Auto: prefer full GPU mode if available
					FLAGGPU = 1;
					FLAGGPU_FULL = 1;
					output_success("GPU auto: using full mode (ECC + hash160 + matching on GPU)\n");
				} else {
				FLAGGPU = 0;
				FLAGGPU_FULL = 0;
					if (!gpu_available) {
						output_info("GPU auto: falling back to CPU (no GPU available)\n");
					} else if (!mode_supports_gpu_full) {
						output_info("GPU auto: falling back to CPU (mode not supported)\n");
					}
				}
			}

				// Validate explicit GPU requests
				if ((FLAGGPU == 1 || FLAGGPU_FULL == 1) && !gpu_available) {
				output_warning("GPU requested but not available, falling back to CPU\n");
				FLAGGPU = 0;
				FLAGGPU_FULL = 0;
			}

			// GPU backends currently assume stride == 1 for correctness/performance.
			// If the user specified a different stride, fall back to CPU.
			if ((FLAGGPU == 1 || FLAGGPU_FULL == 1) && !stride.IsOne()) {
				output_warning("GPU mode requires stride=1 (-I 1). Falling back to CPU.\n");
				FLAGGPU = 0;
				FLAGGPU_FULL = 0;
				FLAGGPU_HYBRID.store(0, std::memory_order_relaxed);
			}

				// Validate mode support
				if (FLAGGPU_FULL == 1 && !mode_supports_gpu_full) {
					output_warning("GPU FULL not supported for this mode/options, using CPU\n");
					FLAGGPU = 0;
					FLAGGPU_FULL = 0;
				}
				if (FLAGGPU == 1 && FLAGGPU_FULL == 0 && !mode_supports_gpu_hash) {
					// If the user asked for HASH mode but also requested uncompressed, upgrade to FULL when possible.
					if (wantUncompressed && gpu_available && mode_supports_gpu_full) {
						output_info("GPU HASH mode does not support uncompressed; upgrading to GPU FULL\n");
						FLAGGPU_FULL = 1;
					} else {
						output_warning("GPU HASH not supported for this mode/options, using CPU\n");
						FLAGGPU = 0;
						FLAGGPU_FULL = 0;
					}
				}

		// Show final GPU mode
		if (FLAGGPU_FULL == 1) {
			output_success("GPU mode: FULL (secp256k1 + SHA256 + RIPEMD160 + matching on GPU)\n");
		} else if (FLAGGPU == 1) {
			output_success("GPU mode: HASH (CPU generates points, GPU computes hash160)\n");
		}

		// GPU self-test if requested
		if (FLAGGPU == 1 && getenv("KEYHUNT_GPU_SELFTEST")) {
			if (!gpu_selftest_hash160_fromX()) {
				output_warning("Disabling GPU due to failed self-test\n");
				FLAGGPU = 0;
				FLAGGPU_FULL = 0;
			} else {
				printf("[✓] GPU self-test passed\n");
			}
		}

			// Cap threads for GPU mode
			if ((FLAGGPU == 1 || FLAGGPU_FULL == 1) && !FLAGTHREADS) {
				int gpu_threads = 0;
				if (FLAGGPU_HYBRID) {
					// Hybrid benefits from more CPU threads (hashing is HT-friendly), but keep 1 core for the GPU thread/driver.
					gpu_threads = g_sysinfo.cpu_logical_cores > 0 ? g_sysinfo.cpu_logical_cores : g_sysinfo.recommended_threads;
					if (gpu_threads > 1) gpu_threads -= 1;
				} else {
					gpu_threads = g_sysinfo.cpu_physical_cores > 0 ? g_sysinfo.cpu_physical_cores : g_sysinfo.recommended_threads;
				}
				if (gpu_threads > 0 && gpu_threads < NTHREADS) {
					NTHREADS = gpu_threads;
					output_info("GPU active: using %d CPU threads\n", NTHREADS);
				}
			}
	}
	if(FLAGRANGE) {
		n_range_start.SetBase16(range_start);
		// `-r start:end` is user-facing and treated as inclusive for `end`.
		// Internally we keep `n_range_end` as an exclusive upper bound (like Int::Rand()).
		n_range_end.SetBase16(range_end);
		if(n_range_start.IsZero())	{
			n_range_start.AddOne();
		}
		if(n_range_end.IsZero())	{
			output_error("End range can't be zero\nFallback to random mode!\n");
			FLAGRANGE = 0;
		}
		if(FLAGRANGE)	{
			if( n_range_start.IsGreater(&n_range_end)) {
				output_warning("Opps, start range can't be great than end range. Swapping them\n");
				n_range_aux.Set(&n_range_start);
				n_range_start.Set(&n_range_end);
				n_range_end.Set(&n_range_aux);
				if(n_range_start.IsZero())	{
					n_range_start.AddOne();
				}
			}
			if(  n_range_start.IsLower(&secp->order) &&  n_range_end.IsLowerOrEqual(&secp->order) )	{
				// Convert inclusive end -> exclusive end, clamped to curve order.
				if (n_range_end.IsLower(&secp->order)) {
					n_range_end.AddOne();
				} else {
					// If user specified order, clamp: valid keys are [1, order-1].
					n_range_end.Set(&secp->order);
				}
				n_range_diff.Set(&n_range_end);
				n_range_diff.Sub(&n_range_start);
			}
			else	{
				output_error("Start and End range can't be great than N\nFallback to random mode!\n");
				FLAGRANGE = 0;
			}
		}
	}
	if(FLAGMODE != MODE_BSGS && FLAGMODE != MODE_MINIKEYS)	{
		BSGS_N.SetInt32(DEBUGCOUNT);
		if(FLAGRANGE == 0 && FLAGBITRANGE == 0)	{
			n_range_start.SetInt32(1);
			n_range_end.Set(&secp->order);
			n_range_diff.Set(&n_range_end);
			n_range_diff.Sub(&n_range_start);
		}
		else	{
			if(FLAGBITRANGE)	{
				n_range_start.SetBase16(bit_range_str_min);
				n_range_end.SetBase16(bit_range_str_max);
				n_range_diff.Set(&n_range_end);
				n_range_diff.Sub(&n_range_start);
			}
			else	{
				if(FLAGRANGE == 0)	{
					output_warning("WTF!\n");
				}
			}
		}
	}
	N = 0;

	if(FLAGMODE != MODE_BSGS )	{
		// Apply auto-tuned N if user didn't specify -n
		if(!FLAG_N && OPTIMAL_N > 0) {
			N_SEQUENTIAL_MAX = OPTIMAL_N;
			output_info("Using auto-tuned N value: 0x%llx\n", (unsigned long long)OPTIMAL_N);
		}
		else if(FLAG_N){
			int base = 10;
			const char *num = str_N;
			if (num[0] == '0' && (num[1] == 'x' || num[1] == 'X')) {
				base = 16;
			}
			errno = 0;
			char *endp = NULL;
			unsigned long long parsed = strtoull(num, &endp, base);
			if (errno != 0 || endp == num || (endp && *endp != '\0')) {
				output_error("Invalid -n value: %s\n", str_N);
				FLAG_N = 0;
				N_SEQUENTIAL_MAX = 0x100000000;
			} else {
				N_SEQUENTIAL_MAX = (uint64_t)parsed;
			}
			
			if(N_SEQUENTIAL_MAX < 1024)	{
				output_info("n value need to be equal or great than 1024, back to defaults\n");
				FLAG_N = 0;
				N_SEQUENTIAL_MAX = 0x100000000;
			}
			if(N_SEQUENTIAL_MAX % 1024 != 0)	{
				output_info("n value need to be multiplier of  1024\n");
				FLAG_N = 0;
				N_SEQUENTIAL_MAX = 0x100000000;
			}
		}
		else {
			// No user param and no auto-tuning: use default
			N_SEQUENTIAL_MAX = 0x100000000;
		}
		output_success("N = 0x%llx\n",(unsigned long long)N_SEQUENTIAL_MAX);
		if(FLAGMODE == MODE_MINIKEYS)	{
			BSGS_N.SetInt32(DEBUGCOUNT);
			if(FLAGBASEMINIKEY)	{
				output_success("Base Minikey : %s\n",str_baseminikey);
			}
			minikeyN = (char*) malloc(22);
			checkpointer((void *)minikeyN,__FILE__,"malloc","minikeyN" ,__LINE__ -1);
			i =0;
			int58.SetInt32(58);
			int_aux.SetInt64(N_SEQUENTIAL_MAX);
			int_aux.Mult(253);	
			/* We get approximately one valid mini key for each 256 candidates mini keys since this is only statistics we multiply N_SEQUENTIAL_MAX by 253 to ensure not missed one one candidate minikey between threads... in this approach we repeat from 1 to 3 candidates in each N_SEQUENTIAL_MAX cycle IF YOU FOUND some other workaround please let me know */
			i = 20;
			salir = 0;
			do	{
				if(!int_aux.IsZero())	{
					int_r.Set(&int_aux);
					int_r.Mod(&int58);
					int_q.Set(&int_aux);
					minikeyN[i] = (uint8_t)int_r.GetInt64();
					int_q.Sub(&int_r);
					int_q.Div(&int58);
					int_aux.Set(&int_q);
					i--;
				}
				else	{
					salir =1;
				}
			}while(!salir && i > 0);
			minikey_n_limit = 21 -i;
		}
		else	{
			if(FLAGBITRANGE)	{	// Bit Range
				output_success("Bit Range %i\n",bitrange);
			}
			else	{
				output_success("Range \n");
			}
		}
		if(FLAGMODE != MODE_MINIKEYS)	{
			hextemp = n_range_start.GetBase16();
			output_success("-- from : 0x%s\n",hextemp);
			free(hextemp);
			if (FLAGRANGE) {
				Int end_inclusive;
				end_inclusive.Set(&n_range_end);
				end_inclusive.SubOne();
				hextemp = end_inclusive.GetBase16();
			} else {
				hextemp = n_range_end.GetBase16();
			}
			output_success("-- to   : 0x%s\n",hextemp);
			free(hextemp);
		}

			initialize_range_progress_tracker();

			// Improve CPU thread utilization on small finite ranges when N wasn't explicitly specified.
			// This avoids the common case where N_SEQUENTIAL_MAX is larger than the entire range and only 1 CPU thread gets work.
			if (!FLAGGPU_HYBRID && !g_work_pool.enabled && !FLAGRANDOM &&
			    (FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_XPOINT || FLAGMODE == MODE_VANITY)) {
				maybe_adjust_cpu_sequential_max((size_t)NTHREADS, n_range_start, n_range_end,
				                                "KEYHUNT_CPU_N", "CPU");
			}

		switch(FLAGMODE)	{
			case MODE_MINIKEYS:
			case MODE_RMD160:
			case MODE_ADDRESS:
			case MODE_XPOINT:
				if(!readFileAddress(fileName))	{
					output_error("Unexpected error\n");
					exit(EXIT_FAILURE);
				}
			break;
			case MODE_VANITY:
				if(!readFileVanity(fileName))	{
					output_error("Unexpected error\n");
					exit(EXIT_FAILURE);
				}
			break;
		}
		
		if(FLAGMODE != MODE_VANITY && !FLAGREADEDFILE1)	{
			output_success("Sorting data ...");
			_sort(addressTable,N);
			printf(" done! %" PRIu64 " values were loaded and sorted\n",N);
			writeFileIfNeeded(fileName);
		}

			// GPU Full Search initialization (upload G table and targets)
			if (FLAGGPU_FULL == 1) {
				output_success("Initializing GPU full search...\n");

				// Upload precomputed G table to GPU
				if (gpu_upload_gtable_from_secp() == 0) {
					output_success("G table uploaded to GPU (8192 points)\n");
				} else {
					output_error("Failed to upload G table to GPU\n");
					FLAGGPU_FULL = 0;
					FLAGGPU = 0;
				}

				// Upload targets to GPU
				if (FLAGGPU_FULL && gpu_upload_targets_from_addressTable(N) == 0) {
					output_success("Targets uploaded to GPU (%" PRIu64 " hashes)\n", N);
					// Optional: build a GPU-specific bloom filter to reduce target searches for large N.
					g_gpu_bloom_uploaded = 0;
					if (N > 32) {
						if (gpu_build_and_upload_bloom_from_addressTable(N) == 0) {
							g_gpu_bloom_uploaded = 1;
							output_success("GPU bloom uploaded (accelerates matching for large target sets)\n");
						} else {
							output_warning("GPU bloom upload failed; continuing without GPU bloom\n");
						}
					}
				} else if (FLAGGPU_FULL) {
					output_error("Failed to upload targets to GPU\n");
					FLAGGPU_FULL = 0;
					FLAGGPU = 0;
				}
			}
		}

	// =========================================================================
	// Initialize progress tracking system
	// =========================================================================
	if (progress_init() != 0) {
		output_warning("Failed to initialize progress system (~/.keyhunt/progress). Progress will NOT be saved.\n");
	} else {
		char *range_start_hex = n_range_start.GetBase16();
		char *range_end_hex = n_range_end.GetBase16();

		int create_result = progress_create(&g_progress_state, get_mode_name(FLAGMODE),
		                    fileName, bitrange,
		                    range_start_hex ? range_start_hex : "0",
		                    range_end_hex ? range_end_hex : "0");
		if (create_result != 0) {
			output_warning("Failed to create progress file. Progress will NOT be saved.\n");
		} else {
			g_progress_enabled = true;
			g_progress_state.is_random_mode = (FLAGRANDOM != 0);
			g_progress_state.thread_count = NTHREADS;
			output_info("Progress tracking enabled (auto-saves every 60s)\n");
		}

		if (range_start_hex) free(range_start_hex);
		if (range_end_hex) free(range_end_hex);
	}

	if(FLAGMODE == MODE_BSGS )	{
		output_success("Opening file %s\n",fileName);
		fd = fopen(fileName,"rb");
		if(fd == NULL)	{
			output_error("Can't open file %s\n",fileName);
			exit(EXIT_FAILURE);
		}
		aux = (char*) malloc(1024);
		checkpointer((void *)aux,__FILE__,"malloc","aux" ,__LINE__ - 1);
		while(fgets(aux,1022,fd) != NULL)	{
			trim(aux," \t\n\r");
			if(strlen(aux) >= 128)	{	//Length of a full address in hexadecimal without 04
					N++;
			}else	{
				if(strlen(aux) >= 66)	{
					N++;
				}
			}
		}
		if(N == 0)	{
			output_error("There is no valid data in the file\n");
			exit(EXIT_FAILURE);
		}
		bsgs_found = (int*) calloc(N,sizeof(int));
		checkpointer((void *)bsgs_found,__FILE__,"calloc","bsgs_found" ,__LINE__ -1 );
		OriginalPointsBSGS.resize(N);
		OriginalPointsBSGScompressed = (bool*) malloc(N*sizeof(bool));
		checkpointer((void *)OriginalPointsBSGScompressed,__FILE__,"malloc","OriginalPointsBSGScompressed" ,__LINE__ -1 );
		pointx_str = (char*) malloc(65);
		checkpointer((void *)pointx_str,__FILE__,"malloc","pointx_str" ,__LINE__ -1 );
		pointy_str = (char*) malloc(65);
		checkpointer((void *)pointy_str,__FILE__,"malloc","pointy_str" ,__LINE__ -1 );
		fseek(fd,0,SEEK_SET);
		i = 0;
		while(fgets(aux,1022,fd) != NULL)	{
			trim(aux," \t\n\r");
			if(strlen(aux) >= 66)	{
				stringtokenizer(aux,&tokenizerbsgs);
				aux2 = nextToken(&tokenizerbsgs);
				memset(pointx_str,0,65);
				memset(pointy_str,0,65);
				switch(strlen(aux2))	{
					case 66:	//Compress

						if(secp->ParsePublicKeyHex(aux2,OriginalPointsBSGS[i],OriginalPointsBSGScompressed[i]))	{
							i++;
						}
						else	{
							N--;
						}

					break;
					case 130:	//With the 04

						if(secp->ParsePublicKeyHex(aux2,OriginalPointsBSGS[i],OriginalPointsBSGScompressed[i]))	{
							i++;
						}
						else	{
							N--;
						}

					break;
					default:
						printf("Invalid length: %s\n",aux2);
						N--;
					break;
				}
				freetokenizer(&tokenizerbsgs);
			}
		}
		fclose(fd);
		bsgs_point_number = N;
		if(bsgs_point_number > 0)	{
			output_success("Added %u points from file\n",bsgs_point_number);
		}
		else	{
			output_error("The file don't have any valid publickeys\n");
			exit(EXIT_FAILURE);
		}
		BSGS_N.SetInt32(0);
		BSGS_M.SetInt32(0);
		

		BSGS_M.SetInt64(bsgs_m);

		// Apply auto-tuning for BSGS mode if user didn't specify params
		if(!FLAG_N && OPTIMAL_N > 0) {
			BSGS_N.SetInt64(OPTIMAL_N);
			output_info("Using auto-tuned N value: 0x%llx\n", (unsigned long long)OPTIMAL_N);
		}
		else if(FLAG_N)	{	//Custom N by the -n param

			/* Here we need to validate if the given string is a valid hexadecimal number or a base 10 number*/

			/* Now the conversion*/
			if(str_N[0] == '0' && str_N[1] == 'x' )	{	/*We expected a hexadecimal value after 0x  -> str_N +2 */
				BSGS_N.SetBase16((char*)(str_N+2));
			}
			else	{
				BSGS_N.SetBase10(str_N);
			}

		}
		else	{	//Default N
			BSGS_N.SetInt64((uint64_t)0x100000000000);
		}

		// Apply auto-tuned KFACTOR if user didn't specify -k
		if(KFACTOR == 1 && OPTIMAL_KFACTOR > 0) {
			KFACTOR = OPTIMAL_KFACTOR;
			output_info("Using auto-tuned K factor: %d\n", OPTIMAL_KFACTOR);
		}

	bsgs_recalculate_with_new_params:
		// Label for auto-adjustment: recalculate all BSGS parameters

		if(BSGS_N.HasSqrt())	{	//If the root is exact
			BSGS_M.Set(&BSGS_N);
			BSGS_M.ModSqrt();
		}
		else	{
			output_error("-n param doesn't have exact square root\n");
			exit(EXIT_FAILURE);
		}

		BSGS_AUX.Set(&BSGS_M);
		BSGS_AUX.Mod(&BSGS_GROUP_SIZE);	
		
		if(!BSGS_AUX.IsZero()){ //If M is not divisible by  BSGS_GROUP_SIZE (1024) 
			hextemp = BSGS_GROUP_SIZE.GetBase10();
			output_error("M value is not divisible by %s\n",hextemp);
			exit(EXIT_FAILURE);
		}

		bsgs_m = BSGS_M.GetInt64();

		if(FLAGRANGE || FLAGBITRANGE)	{
			if(FLAGBITRANGE)	{	// Bit Range
				n_range_start.SetBase16(bit_range_str_min);
				n_range_end.SetBase16(bit_range_str_max);

				n_range_diff.Set(&n_range_end);
				n_range_diff.Sub(&n_range_start);
				output_success("Bit Range %i\n",bitrange);
				output_success("-- from : 0x%s\n",bit_range_str_min);
				output_success("-- to   : 0x%s\n",bit_range_str_max);
			}
			else	{
				output_success("Range \n");
				output_success("-- from : 0x%s\n",range_start);
				output_success("-- to   : 0x%s\n",range_end);
			}
		}
		else	{	//Random start

			n_range_start.SetInt32(1);
			n_range_end.Set(&secp->order);
			n_range_diff.Rand(&n_range_start,&n_range_end);
			n_range_start.Set(&n_range_diff);
		}
		BSGS_CURRENT.Set(&n_range_start);


		if(n_range_diff.IsLower(&BSGS_N) )	{
			output_error("the given range is small\n");
			exit(EXIT_FAILURE);
		}
		
		/*
	M	2199023255552
		109951162777.6
	M2	109951162778
		5497558138.9
	M3	5497558139
		*/

		BSGS_M.Mult((uint64_t)KFACTOR);
		BSGS_AUX.SetInt32(32);
		BSGS_R.Set(&BSGS_M);
		BSGS_R.Mod(&BSGS_AUX);
		BSGS_M2.Set(&BSGS_M);
		BSGS_M2.Div(&BSGS_AUX);

		if(!BSGS_R.IsZero())	{ /* If BSGS_M modulo 32 is not 0*/
			BSGS_M2.AddOne();
		}
		
		BSGS_M_double.SetInt32(2);
		BSGS_M_double.Mult(&BSGS_M);
		
		
		BSGS_M2_double.SetInt32(2);
		BSGS_M2_double.Mult(&BSGS_M2);
		
		BSGS_R.Set(&BSGS_M2);
		BSGS_R.Mod(&BSGS_AUX);
		
		BSGS_M3.Set(&BSGS_M2);
		BSGS_M3.Div(&BSGS_AUX);
		
		if(!BSGS_R.IsZero())	{ /* If BSGS_M2 modulo 32 is not 0*/
			BSGS_M3.AddOne();
		}
		
		BSGS_M3_double.SetInt32(2);
		BSGS_M3_double.Mult(&BSGS_M3);
		
		bsgs_m2 =  BSGS_M2.GetInt64();
		bsgs_m3 =  BSGS_M3.GetInt64();
		
		BSGS_AUX.Set(&BSGS_N);
		BSGS_AUX.Div(&BSGS_M);
		
		BSGS_R.Set(&BSGS_N);
		BSGS_R.Mod(&BSGS_M);

		if(!BSGS_R.IsZero())	{ /* if BSGS_N modulo BSGS_M is not 0*/
			BSGS_N.Set(&BSGS_M);
			BSGS_N.Mult(&BSGS_AUX);
		}

		bsgs_m = BSGS_M.GetInt64();
		bsgs_aux = BSGS_AUX.GetInt64();
		
		
		BSGS_N_double.SetInt32(2);
		BSGS_N_double.Mult(&BSGS_N);

		
		hextemp = BSGS_N.GetBase16();
		output_success("N = 0x%s\n",hextemp);
		free(hextemp);
		if(((uint64_t)(bsgs_m/256)) > 10000)	{
			itemsbloom = (uint64_t)(bsgs_m / 256);
			if(bsgs_m % 256 != 0 )	{
				itemsbloom++;
			}
		}
		else{
			itemsbloom = 1000;
		}
		
		if(((uint64_t)(bsgs_m2/256)) > 1000)	{
			itemsbloom2 = (uint64_t)(bsgs_m2 / 256);
			if(bsgs_m2 % 256 != 0)	{
				itemsbloom2++;
			}
		}
		else	{
			itemsbloom2 = 1000;
		}
		
		if(((uint64_t)(bsgs_m3/256)) > 1000)	{
			itemsbloom3 = (uint64_t)(bsgs_m3/256);
			if(bsgs_m3 % 256 != 0 )	{
				itemsbloom3++;
			}
		}
		else	{
			itemsbloom3 = 1000;
		}

		// ==================================================================
		// BSGS Memory Check: Validate parameters against available RAM
		// ==================================================================
		{
			// Calculate required memory for BSGS
			// bloom1: bsgs_m * 3.5 bytes per element
			// bloom2: bsgs_m2 * 3.5 bytes (1/32 of bloom1)
			// bloom3: bsgs_m3 * 3.5 bytes (1/1024 of bloom1)
			// bP_table: (bsgs_m2 * 16) bytes for Point structures

			uint64_t bloom1_bytes = (uint64_t)((double)bsgs_m * 3.5);
			uint64_t bloom2_bytes = (uint64_t)((double)bsgs_m2 * 3.5);
			uint64_t bloom3_bytes = (uint64_t)((double)bsgs_m3 * 3.5);
			uint64_t bp_table_bytes = bsgs_m2 * 16;  // sizeof(Point) ≈ 16 bytes per element

			uint64_t total_required_mb = (bloom1_bytes + bloom2_bytes + bloom3_bytes + bp_table_bytes) / (1024 * 1024);
			uint64_t available_ram_mb = g_sysinfo.ram_available;

			// Safety margin: require 80% available RAM
			uint64_t safe_limit_mb = (available_ram_mb * 80) / 100;

			if(total_required_mb > safe_limit_mb) {
				fprintf(stderr,"\n");
				output_warning("========================================================\n");
				output_warning("INSUFFICIENT MEMORY FOR BSGS PARAMETERS\n");
				output_warning("========================================================\n");
				output_warning("Required RAM:  %" PRIu64 " MB (~%.1f GB)\n", total_required_mb, (double)total_required_mb/1024);
				output_warning("Available RAM: %" PRIu64 " MB (~%.1f GB)\n", available_ram_mb, (double)available_ram_mb/1024);
				output_warning("Safe limit:    %" PRIu64 " MB (80%% of available)\n", safe_limit_mb);
				output_warning("\n");
				output_warning("Current parameters:\n");
				output_warning("  N = 0x%" PRIx64 "\n", BSGS_N.GetInt64());
				output_warning("  K = %i\n", KFACTOR);
				output_warning("  M = %" PRIu64 " (sqrt(N))\n", bsgs_m/KFACTOR);
				output_warning("  M * K = %" PRIu64 " elements\n", bsgs_m);
				output_warning("\n");
				output_warning("AUTO-ADJUSTING PARAMETERS...\n");
				output_warning("--------------------------------------------------------\n");

				// Find optimal N values that would fit
				struct {
					uint64_t n;
					int k;
				} suggestions[] = {
					{0x40000000000ULL, 2048},   // ~15 GB
					{0x10000000000ULL, 2048},   // ~7.5 GB
					{0x10000000000ULL, 1024},   // ~3.7 GB
					{0x4000000000ULL, 1024},    // ~1.8 GB
				};

				uint64_t new_n = 0;
				int new_k = 0;

				for(int i = 0; i < 4; i++) {
					uint64_t test_m = (uint64_t)sqrt((double)suggestions[i].n);
					uint64_t test_mk = test_m * suggestions[i].k;
					uint64_t test_bloom1 = (uint64_t)((double)test_mk * 3.5);
					uint64_t test_bloom2 = test_bloom1 / 32;
					uint64_t test_bloom3 = test_bloom1 / 1024;
					uint64_t test_bp = (test_mk / 32) * 16;
					uint64_t test_total_mb = (test_bloom1 + test_bloom2 + test_bloom3 + test_bp) / (1024 * 1024);

					if(test_total_mb <= safe_limit_mb) {
						new_n = suggestions[i].n;
						new_k = suggestions[i].k;
						output_info("Auto-adjusted to: N = 0x%" PRIx64 ", K = %d\n", new_n, new_k);
						output_info("New RAM requirement: %" PRIu64 " MB (~%.1f GB)\n",
							test_total_mb, (double)test_total_mb/1024);
						break;
					}
				}

				if(new_n == 0) {
					// Even smallest config doesn't fit
					output_error("ERROR: Insufficient RAM even for minimum configuration\n");
					output_error("Minimum requires: ~1.8 GB, Available: %" PRIu64 " MB\n", available_ram_mb);
					output_error("Cannot continue.\n");
					output_error("========================================================\n");
					exit(EXIT_FAILURE);
				}

				// Apply new parameters
				KFACTOR = new_k;
				BSGS_N.SetInt64(new_n);

				// Must recalculate all BSGS values - go back to start of BSGS calculations
				output_info("Recalculating with optimized parameters...\n");
				output_warning("========================================================\n\n");

				// Recalculate from scratch
				goto bsgs_recalculate_with_new_params;
			}
			else {
				// Parameters OK - show memory usage
				output_info("Memory check: %" PRIu64 " MB required, %" PRIu64 " MB available (%.1f%% used)\n",
					total_required_mb, available_ram_mb,
					(double)total_required_mb * 100.0 / (double)available_ram_mb);
			}
		}
		// ==================================================================

			output_success("Bloom filter for %" PRIu64 " elements ",bsgs_m);
			bloom_bP = (bloom_extended_t*)calloc(256,sizeof(bloom_extended_t));
			checkpointer((void *)bloom_bP,__FILE__,"calloc","bloom_bP" ,__LINE__ -1 );
			bloom_bP_checksums = (struct checksumsha256*)calloc(256,sizeof(struct checksumsha256));
			checkpointer((void *)bloom_bP_checksums,__FILE__,"calloc","bloom_bP_checksums" ,__LINE__ -1 );
		
		bloom_bP_mutex = (platform_mutex_t*) calloc(256,sizeof(platform_mutex_t));
		checkpointer((void *)bloom_bP_mutex,__FILE__,"calloc","bloom_bP_mutex" ,__LINE__ -1 );
		

		fflush(stdout);
		bloom_bP_totalbytes = 0;
			for(i=0; i< 256; i++)	{
				platform_mutex_init(&bloom_bP_mutex[i]);
				if(bloom_ext_init(&bloom_bP[i],itemsbloom,0.000001)	!= 0){
					output_error("error bloom_init _ [%" PRIu64 "]\n",i);
					exit(EXIT_FAILURE);
				}
				bloom_bP_totalbytes += bloom_ext_bytes(&bloom_bP[i]);
				//if(FLAGDEBUG) bloom_print(&bloom_bP[i]);
			}
		printf(": %.2f MB\n",(float)((float)(uint64_t)bloom_bP_totalbytes/(float)(uint64_t)1048576));


		output_success("Bloom filter for %" PRIu64 " elements ",bsgs_m2);
		
		bloom_bPx2nd_mutex = (platform_mutex_t*) calloc(256,sizeof(platform_mutex_t));
		checkpointer((void *)bloom_bPx2nd_mutex,__FILE__,"calloc","bloom_bPx2nd_mutex" ,__LINE__ -1 );
			bloom_bPx2nd = (bloom_extended_t*)calloc(256,sizeof(bloom_extended_t));
			checkpointer((void *)bloom_bPx2nd,__FILE__,"calloc","bloom_bPx2nd" ,__LINE__ -1 );
			bloom_bPx2nd_checksums = (struct checksumsha256*) calloc(256,sizeof(struct checksumsha256));
			checkpointer((void *)bloom_bPx2nd_checksums,__FILE__,"calloc","bloom_bPx2nd_checksums" ,__LINE__ -1 );
			bloom_bP2_totalbytes = 0;
			for(i=0; i< 256; i++)	{
			platform_mutex_init(&bloom_bPx2nd_mutex[i]);
				if(bloom_ext_init(&bloom_bPx2nd[i],itemsbloom2,0.000001)	!= 0){
					output_error("error bloom_init _ [%" PRIu64 "]\n",i);
					exit(EXIT_FAILURE);
				}
				bloom_bP2_totalbytes += bloom_ext_bytes(&bloom_bPx2nd[i]);
				//if(FLAGDEBUG) bloom_print(&bloom_bPx2nd[i]);
			}
		printf(": %.2f MB\n",(float)((float)(uint64_t)bloom_bP2_totalbytes/(float)(uint64_t)1048576));
		

		bloom_bPx3rd_mutex = (platform_mutex_t*) calloc(256,sizeof(platform_mutex_t));
		checkpointer((void *)bloom_bPx3rd_mutex,__FILE__,"calloc","bloom_bPx3rd_mutex" ,__LINE__ -1 );
			bloom_bPx3rd = (bloom_extended_t*)calloc(256,sizeof(bloom_extended_t));
			checkpointer((void *)bloom_bPx3rd,__FILE__,"calloc","bloom_bPx3rd" ,__LINE__ -1 );
			bloom_bPx3rd_checksums = (struct checksumsha256*) calloc(256,sizeof(struct checksumsha256));
			checkpointer((void *)bloom_bPx3rd_checksums,__FILE__,"calloc","bloom_bPx3rd_checksums" ,__LINE__ -1 );
		
		output_success("Bloom filter for %" PRIu64 " elements ",bsgs_m3);
		bloom_bP3_totalbytes = 0;
		for(i=0; i< 256; i++)	{
			platform_mutex_init(&bloom_bPx3rd_mutex[i]);
				if(bloom_ext_init(&bloom_bPx3rd[i],itemsbloom3,0.000001)	!= 0){
					output_error("error bloom_init [%" PRIu64 "]\n",i);
					exit(EXIT_FAILURE);
				}
				bloom_bP3_totalbytes += bloom_ext_bytes(&bloom_bPx3rd[i]);
				//if(FLAGDEBUG) bloom_print(&bloom_bPx3rd[i]);
			}
		printf(": %.2f MB\n",(float)((float)(uint64_t)bloom_bP3_totalbytes/(float)(uint64_t)1048576));
		//if(FLAGDEBUG) printf("[D] bloom_bP3_totalbytes : %" PRIu64 "\n",bloom_bP3_totalbytes);




		BSGS_MP = secp->ComputePublicKey(&BSGS_M);
		BSGS_MP_double = secp->ComputePublicKey(&BSGS_M_double);
		BSGS_MP2 = secp->ComputePublicKey(&BSGS_M2);
		BSGS_MP2_double = secp->ComputePublicKey(&BSGS_M2_double);
		BSGS_MP3 = secp->ComputePublicKey(&BSGS_M3);
		BSGS_MP3_double = secp->ComputePublicKey(&BSGS_M3_double);
		
		BSGS_AMP2.resize(32);
		BSGS_AMP3.resize(32);
		GSn.resize(CPU_GRP_SIZE/2);

		i= 0;


		/* New aMP table just to keep the same code of JLP */
		/* Auxiliar Points to speed up calculations for the main bloom filter check */
		Point bsP = secp->Negation(BSGS_MP_double);
		Point g = bsP;
		GSn[0] = g;

		g = secp->DoubleDirect(g);
		GSn[1] = g;

		for(size_t i = 2; i < CPU_GRP_SIZE / 2; i++) {
			g = secp->AddDirect(g,bsP);
			GSn[i] = g;
		}
		
		/* For next center point */
		_2GSn = secp->DoubleDirect(GSn[CPU_GRP_SIZE / 2 - 1]);
				
		i = 0;
		point_temp.Set(BSGS_MP2);
		BSGS_AMP2[0] = secp->Negation(point_temp);
		BSGS_AMP2[0].Reduce();
		point_temp.Set(BSGS_MP2_double);
		point_temp = secp->Negation(point_temp);
		point_temp.Reduce();
		
		for(i = 1; i < 32; i++)	{
			BSGS_AMP2[i] = secp->AddDirect(BSGS_AMP2[i-1],point_temp);
			BSGS_AMP2[i].Reduce();
		}
		
		i  = 0;
		point_temp.Set(BSGS_MP3);
		BSGS_AMP3[0] = secp->Negation(point_temp);
		BSGS_AMP3[0].Reduce();
		point_temp.Set(BSGS_MP3_double);
		point_temp = secp->Negation(point_temp);
		point_temp.Reduce();

		for(i = 1; i < 32; i++)	{
			BSGS_AMP3[i] = secp->AddDirect(BSGS_AMP3[i-1],point_temp);
			BSGS_AMP3[i].Reduce();
		}

		bytes = (uint64_t)bsgs_m3 * (uint64_t) sizeof(bsgs_xvalue);
		output_success("Allocating %.2f MB for %" PRIu64  " bP Points\n",(double)(bytes/1048576),bsgs_m3);

		bPtable = (bsgs_xvalue*) malloc(bytes);
		checkpointer((void *)bPtable,__FILE__,"malloc","bPtable" ,__LINE__ -1 );
		memset(bPtable,0,bytes);
		
		if(FLAGSAVEREADFILE)	{
			/*Reading file for 1st bloom filter */

				snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_11_%" PRIu64 ".blm",bsgs_m);
				fd_aux1 = fopen(buffer_bloom_file,"rb");
				if(fd_aux1 != NULL)	{
					output_success("Reading bloom filter from file %s ",buffer_bloom_file);
					fflush(stdout);
					for(i = 0; i < 256;i++)	{
						struct bloom tmp_bloom;
						readed = fread(&tmp_bloom,sizeof(struct bloom),1,fd_aux1);
						if(readed != 1)	{
							output_error("Error reading the file %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}

						// Reallocate bloom filter to exactly match the cache (avoids size mismatch / memory corruption).
						bloom_ext_free(&bloom_bP[i]);
						bloom_bP[i].orig = tmp_bloom;
						bloom_bP[i].orig.bf = NULL;
						const bool cache_fast = (bloom_bP[i].orig.major == BLOOM_EXT_FAST_MAJOR && bloom_bP[i].orig.minor == BLOOM_EXT_FAST_MINOR);
#if defined(_WIN64) && !defined(__CYGWIN__)
						if (cache_fast) {
							bloom_bP[i].orig.bf = (uint8_t*)_aligned_malloc(bloom_bP[i].orig.bytes, 64);
						} else {
							bloom_bP[i].orig.bf = (uint8_t*)malloc(bloom_bP[i].orig.bytes);
						}
#else
						if (cache_fast) {
							void *ptr = NULL;
							if (posix_memalign(&ptr, 64, bloom_bP[i].orig.bytes) != 0) ptr = NULL;
							bloom_bP[i].orig.bf = (uint8_t*)ptr;
						} else {
							bloom_bP[i].orig.bf = (uint8_t*)malloc(bloom_bP[i].orig.bytes);
						}
#endif
						if (!bloom_bP[i].orig.bf) {
							output_error("Error allocating memory for bloom cache %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}

						readed = fread(bloom_bP[i].orig.bf,bloom_bP[i].orig.bytes,1,fd_aux1);
						if(readed != 1)	{
							output_error("Error reading the file %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}
						bloom_ext_sync_from_orig(&bloom_bP[i]);
						readed = fread(&bloom_bP_checksums[i],sizeof(struct checksumsha256),1,fd_aux1);
					if(readed != 1)	{
						output_error("Error reading the file %s\n",buffer_bloom_file);
						exit(EXIT_FAILURE);
						}
						if(FLAGSKIPCHECKSUM == 0)	{
							sha256((uint8_t*)bloom_bP[i].orig.bf,bloom_bP[i].orig.bytes,(uint8_t*)rawvalue);
							if(memcmp(bloom_bP_checksums[i].data,rawvalue,32) != 0 || memcmp(bloom_bP_checksums[i].backup,rawvalue,32) != 0 )	{	/* Verification */
								output_error("Error checksum file mismatch! %s\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
					}
					if(i % 64 == 0 )	{
						printf(".");
						fflush(stdout);
					}
				}
				printf(" Done!\n");
				fclose(fd_aux1);
				memset(buffer_bloom_file,0,1024);
				snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_3_%" PRIu64 ".blm",bsgs_m);
				fd_aux1 = fopen(buffer_bloom_file,"rb");
				if(fd_aux1 != NULL)	{
					output_warning("Unused file detected %s you can delete it without worry\n", buffer_bloom_file);
					fclose(fd_aux1);
				}
				FLAGREADEDFILE1 = 1;
			}
				else	{
					FLAGREADEDFILE1 = 0;
				}
			
			/*Reading file for 2nd bloom filter */
				snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_12_%" PRIu64 ".blm",bsgs_m2);
				fd_aux2 = fopen(buffer_bloom_file,"rb");
				if(fd_aux2 != NULL)	{
					output_success("Reading bloom filter from file %s ",buffer_bloom_file);
					fflush(stdout);
					for(i = 0; i < 256;i++)	{
						struct bloom tmp_bloom;
						readed = fread(&tmp_bloom,sizeof(struct bloom),1,fd_aux2);
						if(readed != 1)	{
							output_error("Error reading the file %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}

						bloom_ext_free(&bloom_bPx2nd[i]);
						bloom_bPx2nd[i].orig = tmp_bloom;
						bloom_bPx2nd[i].orig.bf = NULL;
						const bool cache_fast = (bloom_bPx2nd[i].orig.major == BLOOM_EXT_FAST_MAJOR && bloom_bPx2nd[i].orig.minor == BLOOM_EXT_FAST_MINOR);
#if defined(_WIN64) && !defined(__CYGWIN__)
						if (cache_fast) {
							bloom_bPx2nd[i].orig.bf = (uint8_t*)_aligned_malloc(bloom_bPx2nd[i].orig.bytes, 64);
						} else {
							bloom_bPx2nd[i].orig.bf = (uint8_t*)malloc(bloom_bPx2nd[i].orig.bytes);
						}
#else
						if (cache_fast) {
							void *ptr = NULL;
							if (posix_memalign(&ptr, 64, bloom_bPx2nd[i].orig.bytes) != 0) ptr = NULL;
							bloom_bPx2nd[i].orig.bf = (uint8_t*)ptr;
						} else {
							bloom_bPx2nd[i].orig.bf = (uint8_t*)malloc(bloom_bPx2nd[i].orig.bytes);
						}
#endif
						if (!bloom_bPx2nd[i].orig.bf) {
							output_error("Error allocating memory for bloom cache %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}

						readed = fread(bloom_bPx2nd[i].orig.bf,bloom_bPx2nd[i].orig.bytes,1,fd_aux2);
						if(readed != 1)	{
							output_error("Error reading the file %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}
						bloom_ext_sync_from_orig(&bloom_bPx2nd[i]);
						readed = fread(&bloom_bPx2nd_checksums[i],sizeof(struct checksumsha256),1,fd_aux2);
					if(readed != 1)	{
						output_error("Error reading the file %s\n",buffer_bloom_file);
						exit(EXIT_FAILURE);
					}
						memset(rawvalue,0,32);
						if(FLAGSKIPCHECKSUM == 0)	{								
							sha256((uint8_t*)bloom_bPx2nd[i].orig.bf,bloom_bPx2nd[i].orig.bytes,(uint8_t*)rawvalue);
							if(memcmp(bloom_bPx2nd_checksums[i].data,rawvalue,32) != 0 || memcmp(bloom_bPx2nd_checksums[i].backup,rawvalue,32) != 0 )	{		/* Verification */
								output_error("Error checksum file mismatch! %s\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
						}
					}
					if(i % 64 == 0)	{
						printf(".");
						fflush(stdout);
					}
				}
				fclose(fd_aux2);
				printf(" Done!\n");
				memset(buffer_bloom_file,0,1024);
				snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_5_%" PRIu64 ".blm",bsgs_m2);
				fd_aux2 = fopen(buffer_bloom_file,"rb");
				if(fd_aux2 != NULL)	{
					output_warning("Unused file detected %s you can delete it without worry\n", buffer_bloom_file);
					fclose(fd_aux2);
				}
				memset(buffer_bloom_file,0,1024);
				snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_1_%" PRIu64 ".blm",bsgs_m2);
				fd_aux2 = fopen(buffer_bloom_file,"rb");
				if(fd_aux2 != NULL)	{
					output_warning("Unused file detected %s you can delete it without worry\n", buffer_bloom_file);
					fclose(fd_aux2);
				}
				FLAGREADEDFILE2 = 1;
			}
			else	{	
				FLAGREADEDFILE2 = 0;
			}
			
			/*Reading file for bPtable */
			snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_2_%" PRIu64 ".tbl",bsgs_m3);
			fd_aux3 = fopen(buffer_bloom_file,"rb");
			if(fd_aux3 != NULL)	{
				output_success("Reading bP Table from file %s .",buffer_bloom_file);
				fflush(stdout);
				rsize = fread(bPtable,bytes,1,fd_aux3);
				if(rsize != 1)	{
					output_error("Error reading the file %s\n",buffer_bloom_file);
					exit(EXIT_FAILURE);
				}
				rsize = fread(checksum,32,1,fd_aux3);
				if(rsize != 1)	{
					output_error("Error reading the file %s\n",buffer_bloom_file);
					exit(EXIT_FAILURE);
				}
				if(FLAGSKIPCHECKSUM == 0)	{
					sha256((uint8_t*)bPtable,bytes,(uint8_t*)checksum_backup);
					if(memcmp(checksum,checksum_backup,32) != 0)	{
						output_error("Error checksum file mismatch! %s\n",buffer_bloom_file);
						exit(EXIT_FAILURE);
					}
				}
				printf("... Done!\n");
				fclose(fd_aux3);
				FLAGREADEDFILE3 = 1;
			}
			else	{
				FLAGREADEDFILE3 = 0;
			}
			
			/*Reading file for 3rd bloom filter */
				snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_13_%" PRIu64 ".blm",bsgs_m3);
				fd_aux2 = fopen(buffer_bloom_file,"rb");
				if(fd_aux2 != NULL)	{
					output_success("Reading bloom filter from file %s ",buffer_bloom_file);
					fflush(stdout);
					for(i = 0; i < 256;i++)	{
						struct bloom tmp_bloom;
						readed = fread(&tmp_bloom,sizeof(struct bloom),1,fd_aux2);
						if(readed != 1)	{
							output_error("Error reading the file %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}

						bloom_ext_free(&bloom_bPx3rd[i]);
						bloom_bPx3rd[i].orig = tmp_bloom;
						bloom_bPx3rd[i].orig.bf = NULL;
						const bool cache_fast = (bloom_bPx3rd[i].orig.major == BLOOM_EXT_FAST_MAJOR && bloom_bPx3rd[i].orig.minor == BLOOM_EXT_FAST_MINOR);
#if defined(_WIN64) && !defined(__CYGWIN__)
						if (cache_fast) {
							bloom_bPx3rd[i].orig.bf = (uint8_t*)_aligned_malloc(bloom_bPx3rd[i].orig.bytes, 64);
						} else {
							bloom_bPx3rd[i].orig.bf = (uint8_t*)malloc(bloom_bPx3rd[i].orig.bytes);
						}
#else
						if (cache_fast) {
							void *ptr = NULL;
							if (posix_memalign(&ptr, 64, bloom_bPx3rd[i].orig.bytes) != 0) ptr = NULL;
							bloom_bPx3rd[i].orig.bf = (uint8_t*)ptr;
						} else {
							bloom_bPx3rd[i].orig.bf = (uint8_t*)malloc(bloom_bPx3rd[i].orig.bytes);
						}
#endif
						if (!bloom_bPx3rd[i].orig.bf) {
							output_error("Error allocating memory for bloom cache %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}

						readed = fread(bloom_bPx3rd[i].orig.bf,bloom_bPx3rd[i].orig.bytes,1,fd_aux2);
						if(readed != 1)	{
							output_error("Error reading the file %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}
						bloom_ext_sync_from_orig(&bloom_bPx3rd[i]);
						readed = fread(&bloom_bPx3rd_checksums[i],sizeof(struct checksumsha256),1,fd_aux2);
					if(readed != 1)	{
						output_error("Error reading the file %s\n",buffer_bloom_file);
						exit(EXIT_FAILURE);
					}
						memset(rawvalue,0,32);
						if(FLAGSKIPCHECKSUM == 0)	{							
							sha256((uint8_t*)bloom_bPx3rd[i].orig.bf,bloom_bPx3rd[i].orig.bytes,(uint8_t*)rawvalue);
							if(memcmp(bloom_bPx3rd_checksums[i].data,rawvalue,32) != 0 || memcmp(bloom_bPx3rd_checksums[i].backup,rawvalue,32) != 0 )	{		/* Verification */
								output_error("Error checksum file mismatch! %s\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
					}
					if(i % 64 == 0)	{
						printf(".");
						fflush(stdout);
					}
				}
				fclose(fd_aux2);
				printf(" Done!\n");
				FLAGREADEDFILE4 = 1;
			}
			else	{
				FLAGREADEDFILE4 = 0;
			}
			
		}
		
		if(!FLAGREADEDFILE1 || !FLAGREADEDFILE2 || !FLAGREADEDFILE3 || !FLAGREADEDFILE4)	{
			if(FLAGREADEDFILE1 == 1)	{
				/* 
					We need just to make File 2 to File 4 this is
					- Second bloom filter 5%
					- third  bloom fitler 0.25 %
					- bp Table 0.25 %
				*/
				output_info("We need to recalculate some files, don't worry this is only 3%% of the previous work\n");
				FINISHED_THREADS_COUNTER = 0;
				FINISHED_THREADS_BP = 0;
				FINISHED_ITEMS.store(0, std::memory_order_relaxed);
				salir = 0;
				BASE = 0;
				THREADCOUNTER = 0;
				if(THREADBPWORKLOAD >= bsgs_m2)	{
					THREADBPWORKLOAD = bsgs_m2;
				}
				THREADCYCLES = bsgs_m2 / THREADBPWORKLOAD;
				PERTHREAD_R = bsgs_m2 % THREADBPWORKLOAD;
				if(PERTHREAD_R != 0)	{
					THREADCYCLES++;
				}
				
				printf("\r[+] processing %lu/%lu bP points : %i%%\r",FINISHED_ITEMS.load(std::memory_order_relaxed),bsgs_m,(int) (((double)FINISHED_ITEMS.load(std::memory_order_relaxed)/(double)bsgs_m)*100));
				fflush(stdout);

				tid = (platform_thread_t *) calloc(NTHREADS, sizeof(platform_thread_t));
				checkpointer((void *)tid,__FILE__,"calloc","tid" ,__LINE__ -1 );
				bPload_mutex = (platform_mutex_t*) calloc(NTHREADS,sizeof(platform_mutex_t));
				checkpointer((void *)bPload_mutex,__FILE__,"calloc","bPload_mutex" ,__LINE__ -1 );
				bPload_temp_ptr = (struct bPload*) calloc(NTHREADS,sizeof(struct bPload));
				checkpointer((void *)bPload_temp_ptr,__FILE__,"calloc","bPload_temp_ptr" ,__LINE__ -1 );
				bPload_threads_available = (char*) calloc(NTHREADS,sizeof(char));
				checkpointer((void *)bPload_threads_available,__FILE__,"calloc","bPload_threads_available" ,__LINE__ -1 );
				
				memset(bPload_threads_available,1,NTHREADS);
				
				for(j = 0; j < NTHREADS; j++)	{
					platform_mutex_init(&bPload_mutex[j]);
				}
				
				do	{
					for(j = 0; j < NTHREADS && !salir; j++)	{

						if(bPload_threads_available[j] && !salir)	{
							bPload_threads_available[j] = 0;
							bPload_temp_ptr[j].from = BASE;
							bPload_temp_ptr[j].threadid = j;
							bPload_temp_ptr[j].finished = 0;
							if( THREADCOUNTER < THREADCYCLES-1)	{
								bPload_temp_ptr[j].to = BASE + THREADBPWORKLOAD;
								bPload_temp_ptr[j].workload = THREADBPWORKLOAD;
							}
							else	{
								bPload_temp_ptr[j].to = BASE + THREADBPWORKLOAD + PERTHREAD_R;
								bPload_temp_ptr[j].workload = THREADBPWORKLOAD + PERTHREAD_R;
								salir = 1;
							}
							s = platform_thread_create(&tid[j], thread_bPload_2blooms, (void*) &bPload_temp_ptr[j]);
							if (s == 0) {
								platform_thread_detach(tid[j]);
							}
							BASE+=THREADBPWORKLOAD;
							THREADCOUNTER++;
						}
					}

					{
						uint64_t current_items = FINISHED_ITEMS.load(std::memory_order_relaxed);
						if(OLDFINISHED_ITEMS != current_items)	{
							int percent = (bsgs_m2 > 0) ? (int)(((double)current_items/(double)bsgs_m2)*100) : 0;
							printf("\r[+] processing %lu/%lu bP points : %i%%\r",current_items,bsgs_m2,percent);
							fflush(stdout);
							OLDFINISHED_ITEMS = current_items;
						}
					}
					
					for(j = 0 ; j < NTHREADS ; j++)	{

						platform_mutex_lock(&bPload_mutex[j]);
						finished = bPload_temp_ptr[j].finished;
						platform_mutex_unlock(&bPload_mutex[j]);
						if(finished)	{
							bPload_temp_ptr[j].finished = 0;
							bPload_threads_available[j] = 1;
							FINISHED_ITEMS.fetch_add(bPload_temp_ptr[j].workload, std::memory_order_relaxed);
							FINISHED_THREADS_COUNTER++;
						}
					}
				}while(FINISHED_THREADS_COUNTER < THREADCYCLES);
				printf("\r[+] processing %lu/%lu bP points : 100%%     \n",bsgs_m2,bsgs_m2);
				
				free(tid);
				free(bPload_mutex);
				free(bPload_temp_ptr);
				free(bPload_threads_available);
			}
			else{	
				/* We need just to do all the files 
					- first  bllom filter 100% 
					- Second bloom filter 5%
					- third  bloom fitler 0.25 %
					- bp Table 0.25 %
				*/
				FINISHED_THREADS_COUNTER = 0;
				FINISHED_THREADS_BP = 0;
				FINISHED_ITEMS.store(0, std::memory_order_relaxed);
				salir = 0;
				BASE = 0;
				THREADCOUNTER = 0;
				if(THREADBPWORKLOAD >= bsgs_m)	{
					THREADBPWORKLOAD = bsgs_m;
				}
				THREADCYCLES = bsgs_m / THREADBPWORKLOAD;
				PERTHREAD_R = bsgs_m % THREADBPWORKLOAD;
				//if(FLAGDEBUG) printf("[D] THREADCYCLES: %lu\n",THREADCYCLES);
				if(PERTHREAD_R != 0)	{
					THREADCYCLES++;
					//if(FLAGDEBUG) printf("[D] PERTHREAD_R: %lu\n",PERTHREAD_R);
				}
				
				printf("\r[+] processing %lu/%lu bP points : %i%%\r",FINISHED_ITEMS.load(std::memory_order_relaxed),bsgs_m,(int) (((double)FINISHED_ITEMS.load(std::memory_order_relaxed)/(double)bsgs_m)*100));
				fflush(stdout);

				tid = (platform_thread_t *) calloc(NTHREADS, sizeof(platform_thread_t));
				checkpointer((void *)tid,__FILE__,"calloc","tid" ,__LINE__ -1 );
				bPload_mutex = (platform_mutex_t*) calloc(NTHREADS,sizeof(platform_mutex_t));
				checkpointer((void *)bPload_mutex,__FILE__,"calloc","bPload_mutex" ,__LINE__ -1 );
				
				bPload_temp_ptr = (struct bPload*) calloc(NTHREADS,sizeof(struct bPload));
				checkpointer((void *)bPload_temp_ptr,__FILE__,"calloc","bPload_temp_ptr" ,__LINE__ -1 );
				bPload_threads_available = (char*) calloc(NTHREADS,sizeof(char));
				checkpointer((void *)bPload_threads_available,__FILE__,"calloc","bPload_threads_available" ,__LINE__ -1 );
				

				memset(bPload_threads_available,1,NTHREADS);
				
				for(j = 0; j < NTHREADS; j++)	{
					platform_mutex_init(&bPload_mutex[j]);
				}
				
				do	{
					for(j = 0; j < NTHREADS && !salir; j++)	{

						if(bPload_threads_available[j] && !salir)	{
							bPload_threads_available[j] = 0;
							bPload_temp_ptr[j].from = BASE;
							bPload_temp_ptr[j].threadid = j;
							bPload_temp_ptr[j].finished = 0;
							if( THREADCOUNTER < THREADCYCLES-1)	{
								bPload_temp_ptr[j].to = BASE + THREADBPWORKLOAD;
								bPload_temp_ptr[j].workload = THREADBPWORKLOAD;
							}
							else	{
								bPload_temp_ptr[j].to = BASE + THREADBPWORKLOAD + PERTHREAD_R;
								bPload_temp_ptr[j].workload = THREADBPWORKLOAD + PERTHREAD_R;
								salir = 1;
								//if(FLAGDEBUG) printf("[D] Salir OK\n");
							}
							//if(FLAGDEBUG) output_info("%lu to %lu\n",bPload_temp_ptr[i].from,bPload_temp_ptr[i].to);
							s = platform_thread_create(&tid[j], thread_bPload, (void*) &bPload_temp_ptr[j]);
							if (s == 0) {
								platform_thread_detach(tid[j]);
							}
							BASE+=THREADBPWORKLOAD;
							THREADCOUNTER++;
						}
					}
					{
						uint64_t current_items = FINISHED_ITEMS.load(std::memory_order_relaxed);
						if(OLDFINISHED_ITEMS != current_items)	{
							printf("\r[+] processing %lu/%lu bP points : %i%%\r",current_items,bsgs_m,(int) (((double)current_items/(double)bsgs_m)*100));
							fflush(stdout);
							OLDFINISHED_ITEMS = current_items;
						}
					}
					
					for(j = 0 ; j < NTHREADS ; j++)	{

						platform_mutex_lock(&bPload_mutex[j]);
						finished = bPload_temp_ptr[j].finished;
						platform_mutex_unlock(&bPload_mutex[j]);
						if(finished)	{
							bPload_temp_ptr[j].finished = 0;
							bPload_threads_available[j] = 1;
							FINISHED_ITEMS.fetch_add(bPload_temp_ptr[j].workload, std::memory_order_relaxed);
							FINISHED_THREADS_COUNTER++;
						}
					}
					
				}while(FINISHED_THREADS_COUNTER < THREADCYCLES);
				printf("\r[+] processing %lu/%lu bP points : 100%%     \n",bsgs_m,bsgs_m);
				
				free(tid);
				free(bPload_mutex);
				free(bPload_temp_ptr);
				free(bPload_threads_available);
			}
		}
		
		if(!FLAGREADEDFILE1 || !FLAGREADEDFILE2 || !FLAGREADEDFILE4)	{
			output_success("Making checkums .. ");
			fflush(stdout);
		}	
			if(!FLAGREADEDFILE1)	{
				for(i = 0; i < 256 ; i++)	{
					sha256((uint8_t*)bloom_bP[i].orig.bf, bloom_bP[i].orig.bytes,(uint8_t*) bloom_bP_checksums[i].data);
					memcpy(bloom_bP_checksums[i].backup,bloom_bP_checksums[i].data,32);
				}
				printf(".");
			}
			if(!FLAGREADEDFILE2)	{
				for(i = 0; i < 256 ; i++)	{
					sha256((uint8_t*)bloom_bPx2nd[i].orig.bf, bloom_bPx2nd[i].orig.bytes,(uint8_t*) bloom_bPx2nd_checksums[i].data);
					memcpy(bloom_bPx2nd_checksums[i].backup,bloom_bPx2nd_checksums[i].data,32);
				}
				printf(".");
			}
			if(!FLAGREADEDFILE4)	{
				for(i = 0; i < 256 ; i++)	{
					sha256((uint8_t*)bloom_bPx3rd[i].orig.bf, bloom_bPx3rd[i].orig.bytes,(uint8_t*) bloom_bPx3rd_checksums[i].data);
					memcpy(bloom_bPx3rd_checksums[i].backup,bloom_bPx3rd_checksums[i].data,32);
				}
				printf(".");
			}
		if(!FLAGREADEDFILE1 || !FLAGREADEDFILE2 || !FLAGREADEDFILE4)	{
			printf(" done\n");
			fflush(stdout);
		}	
		if(!FLAGREADEDFILE3)	{
			output_success("Sorting %lu elements... ",bsgs_m3);
			fflush(stdout);
			bsgs_sort(bPtable,bsgs_m3);
			sha256((uint8_t*)bPtable, bytes,(uint8_t*) checksum);
			memcpy(checksum_backup,checksum,32);
			printf("Done!\n");
			fflush(stdout);
		}
		if(FLAGSAVEREADFILE || FLAGUPDATEFILE1 )	{
				if(!FLAGREADEDFILE1 || FLAGUPDATEFILE1)	{
					snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_11_%" PRIu64 ".blm",bsgs_m);
					
					if(FLAGUPDATEFILE1)	{
						output_warning("Updating old file into a new one\n");
					}
				
				/* Writing file for 1st bloom filter */
				
					fd_aux1 = fopen(buffer_bloom_file,"wb");
					if(fd_aux1 != NULL)	{
						output_success("Writing bloom filter to file %s ",buffer_bloom_file);
						fflush(stdout);
						for(i = 0; i < 256;i++)	{
							readed = fwrite(&bloom_bP[i].orig,sizeof(struct bloom),1,fd_aux1);
							if(readed != 1)	{
								output_error("Error writing the file %s please delete it\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
							readed = fwrite(bloom_bP[i].orig.bf,bloom_bP[i].orig.bytes,1,fd_aux1);
							if(readed != 1)	{
								output_error("Error writing the file %s please delete it\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
						readed = fwrite(&bloom_bP_checksums[i],sizeof(struct checksumsha256),1,fd_aux1);
						if(readed != 1)	{
							output_error("Error writing the file %s please delete it\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}
						if(i % 64 == 0)	{
							printf(".");
							fflush(stdout);
						}
					}
					printf(" Done!\n");
					fclose(fd_aux1);
				}
				else	{
					output_error("Error can't create the file %s\n",buffer_bloom_file);
					exit(EXIT_FAILURE);
				}
			}
				if(!FLAGREADEDFILE2  )	{
					
					snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_12_%" PRIu64 ".blm",bsgs_m2);
									
					/* Writing file for 2nd bloom filter */
					fd_aux2 = fopen(buffer_bloom_file,"wb");
					if(fd_aux2 != NULL)	{
						output_success("Writing bloom filter to file %s ",buffer_bloom_file);
						fflush(stdout);
						for(i = 0; i < 256;i++)	{
							readed = fwrite(&bloom_bPx2nd[i].orig,sizeof(struct bloom),1,fd_aux2);
							if(readed != 1)	{
								output_error("Error writing the file %s\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
							readed = fwrite(bloom_bPx2nd[i].orig.bf,bloom_bPx2nd[i].orig.bytes,1,fd_aux2);
							if(readed != 1)	{
								output_error("Error writing the file %s\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
						readed = fwrite(&bloom_bPx2nd_checksums[i],sizeof(struct checksumsha256),1,fd_aux2);
						if(readed != 1)	{
							output_error("Error writing the file %s please delete it\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}
						if(i % 64 == 0)	{
							printf(".");
							fflush(stdout);
						}
					}
					printf(" Done!\n");
					fclose(fd_aux2);	
				}
				else	{
					output_error("Error can't create the file %s\n",buffer_bloom_file);
					exit(EXIT_FAILURE);
				}
			}
			
			if(!FLAGREADEDFILE3)	{
				/* Writing file for bPtable */
				snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_2_%" PRIu64 ".tbl",bsgs_m3);
				fd_aux3 = fopen(buffer_bloom_file,"wb");
				if(fd_aux3 != NULL)	{
					output_success("Writing bP Table to file %s .. ",buffer_bloom_file);
					fflush(stdout);
					readed = fwrite(bPtable,bytes,1,fd_aux3);
					if(readed != 1)	{
						output_error("Error writing the file %s\n",buffer_bloom_file);
						exit(EXIT_FAILURE);
					}
					readed = fwrite(checksum,32,1,fd_aux3);
					if(readed != 1)	{
						output_error("Error writing the file %s\n",buffer_bloom_file);
						exit(EXIT_FAILURE);
					}
					printf("Done!\n");
					fclose(fd_aux3);	
				}
				else	{
					output_error("Error can't create the file %s\n",buffer_bloom_file);
					exit(EXIT_FAILURE);
				}
			}
				if(!FLAGREADEDFILE4)	{
					snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_13_%" PRIu64 ".blm",bsgs_m3);
									
					/* Writing file for 3rd bloom filter */
					fd_aux2 = fopen(buffer_bloom_file,"wb");
					if(fd_aux2 != NULL)	{
						output_success("Writing bloom filter to file %s ",buffer_bloom_file);
						fflush(stdout);
						for(i = 0; i < 256;i++)	{
							readed = fwrite(&bloom_bPx3rd[i].orig,sizeof(struct bloom),1,fd_aux2);
							if(readed != 1)	{
								output_error("Error writing the file %s\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
							readed = fwrite(bloom_bPx3rd[i].orig.bf,bloom_bPx3rd[i].orig.bytes,1,fd_aux2);
							if(readed != 1)	{
								output_error("Error writing the file %s\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
						readed = fwrite(&bloom_bPx3rd_checksums[i],sizeof(struct checksumsha256),1,fd_aux2);
						if(readed != 1)	{
							output_error("Error writing the file %s please delete it\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}
						if(i % 64 == 0)	{
							printf(".");
							fflush(stdout);
						}
					}
					printf(" Done!\n");
					fclose(fd_aux2);
				}
				else	{
					output_error("Error can't create the file %s\n",buffer_bloom_file);
					exit(EXIT_FAILURE);
				}
			}
		}


		i = 0;

		// Apply auto-tuned thread count ONLY if user didn't specify -t
		if (!FLAGTHREADS && NTHREADS == 1 && OPTIMAL_THREADS > 0) {
			NTHREADS = OPTIMAL_THREADS;
			output_info("Using auto-tuned thread count: %d (optimal for %d physical cores)\n",
			       NTHREADS, g_sysinfo.cpu_physical_cores);
		}

		steps = (struct thread_counter *) aligned_calloc(64, NTHREADS, sizeof(struct thread_counter));
		checkpointer((void *)steps,__FILE__,"aligned_calloc","steps" ,__LINE__ -1 );
		ends = (struct thread_flag *) aligned_calloc(64, NTHREADS, sizeof(struct thread_flag));
		checkpointer((void *)ends,__FILE__,"aligned_calloc","ends" ,__LINE__ -1 );
		tid = (platform_thread_t *) calloc(NTHREADS, sizeof(platform_thread_t));
		checkpointer((void *)tid,__FILE__,"calloc","tid" ,__LINE__ -1 );
#ifndef _WIN64
		if(FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_XPOINT || FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_VANITY) {
			configure_work_queue(NTHREADS);
		}
		else {
			shutdown_work_queue();
		}
#endif
		
		profile_init_threads((int)NTHREADS);
		for(j= 0;j < NTHREADS; j++)	{
			tt = (tothread*) malloc(sizeof(struct tothread));
			checkpointer((void *)tt,__FILE__,"malloc","tt" ,__LINE__ -1 );
			tt->nt = j;
			steps[j].value = 0;
			s = 0;
			switch(FLAGBSGSMODE)	{
				case 0:
					s = platform_thread_create(&tid[j], thread_process_bsgs, (void *)tt);
				break;
				case 1:
					s = platform_thread_create(&tid[j], thread_process_bsgs_backward, (void *)tt);
				break;
				case 2:
					s = platform_thread_create(&tid[j], thread_process_bsgs_both, (void *)tt);
				break;
				case 3:
					s = platform_thread_create(&tid[j], thread_process_bsgs_random, (void *)tt);
				break;
				case 4:
					s = platform_thread_create(&tid[j], thread_process_bsgs_dance, (void *)tt);
				break;
			}
			if(s != 0)	{
				output_error("thread thread_process\n");
				exit(EXIT_FAILURE);
			}
		}
		free(aux);
	}
		if(FLAGMODE != MODE_BSGS)	{
			initialize_rate_limits();
			// Apply auto-tuned thread count ONLY if user didn't specify -t
			if (!FLAGTHREADS && NTHREADS == 1 && OPTIMAL_THREADS > 0) {
				NTHREADS = OPTIMAL_THREADS;
				output_info("Using auto-tuned thread count: %d\n", NTHREADS);
			}
		steps = (struct thread_counter *) aligned_calloc(64, NTHREADS, sizeof(struct thread_counter));
		checkpointer((void *)steps,__FILE__,"aligned_calloc","steps" ,__LINE__ -1 );
		ends = (struct thread_flag *) aligned_calloc(64, NTHREADS, sizeof(struct thread_flag));
		checkpointer((void *)ends,__FILE__,"aligned_calloc","ends" ,__LINE__ -1 );
		tid = (platform_thread_t *) calloc(NTHREADS, sizeof(platform_thread_t));
		checkpointer((void *)tid,__FILE__,"calloc","tid" ,__LINE__ -1 );
#ifndef _WIN64
			// IMPORTANT: delay work-queue startup until after GPU FULL/HYBRID handling.
			// The work-queue producer mutates `n_range_start` while enqueuing blocks, which would
			// otherwise corrupt the GPU start range and/or the hybrid split.
			shutdown_work_queue();
#endif

			// ============================================================================
			// GPU Full Search Mode (ECC + hash160 + matching entirely on GPU)
			// ============================================================================
			if (FLAGGPU_FULL && !FLAGGPU_HYBRID && (FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160)) {
				output_success("Running GPU full search mode...\n");
	
					// Reset stats
					g_gpu_keys_checked.store(0, std::memory_order_release);
					g_gpu_keys_checked_cur.store(0, std::memory_order_release);
					g_gpu_should_stop.store(0, std::memory_order_release);

#ifndef _WIN64
				platform_thread_t gpu_stats_tid;
				int gpu_stats_started = 0;
				std::atomic<int> gpu_stats_stop{0};
				gpu_full_stats_args_t gpu_stats_args;
				memset(&gpu_stats_args, 0, sizeof(gpu_stats_args));
				if (OUTPUTSECONDS.IsGreater(&ZERO)) {
					gpu_stats_args.period_seconds = OUTPUTSECONDS.GetInt32();
					gpu_stats_args.stop_flag = &gpu_stats_stop;
					if (gpu_stats_args.period_seconds > 0) {
						if (platform_thread_create(&gpu_stats_tid, gpu_full_stats_thread, &gpu_stats_args) == 0) {
							gpu_stats_started = 1;
						}
					}
				}
#endif

				// Run GPU search
				int gpu_result = gpu_run_full_search(&n_range_start, &n_range_end, &stride, N);

#ifndef _WIN64
				gpu_stats_stop.store(1, std::memory_order_release);
				if (gpu_stats_started) {
					platform_thread_join(gpu_stats_tid, NULL);
				}
#endif
	
				if (gpu_result >= 0) {
					// GPU search completed successfully
					output_success("GPU search finished. Keys found: %d\n", gpu_result);
					output_success("Total keys checked: %" PRIu64 "\n", g_gpu_keys_checked.load(std::memory_order_acquire));

				// Cleanup and exit
#ifndef _WIN64
				shutdown_work_queue();
#endif
				gpu_backend_shutdown();
				output_success("Done!\n");
				return 0;
			} else {
				// GPU search failed, fall back to CPU
				output_warning("GPU search failed, falling back to CPU threads\n");
				FLAGGPU_FULL = 0;
			}
		}

		// ============================================================================
		// GPU Hybrid Mode (GPU + CPU in parallel with STATIC SPLIT)
		// GPU gets g_gpu_range_percent% of range, CPU uses normal fast algorithm
		// ============================================================================
			if (FLAGGPU_HYBRID.load(std::memory_order_relaxed) && (FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160)) {
				if (!gpu_backend_available()) {
					output_warning("GPU not available for hybrid mode, falling back to CPU-only\n");
					FLAGGPU_HYBRID.store(0, std::memory_order_release);
					} else {
						// Initialize adaptive scheduler for throughput tracking
						// Use sysinfo hybrid ratio for initial CPU/GPU split
						float initial_cpu_ratio = 1.0f - (g_gpu_range_percent / 100.0f);
						adaptive_init(initial_cpu_ratio, 0, 0);  // Range tracking done separately
						output_info("Adaptive scheduler initialized (CPU=%.0f%%, GPU=%.0f%%)\n",
						       initial_cpu_ratio * 100.0f, (1.0f - initial_cpu_ratio) * 100.0f);

						const char *ws = getenv("KEYHUNT_HYBRID_WORK_STEAL");
						const bool want_work_steal = (ws && *ws && atoi(ws) != 0);
						const bool can_work_steal = want_work_steal && !FLAGRANDOM && stride.IsOne();
						if (want_work_steal && !can_work_steal) {
							output_warning("HYBRID: work-stealing requires non-random mode and stride=1; using static split\n");
						}
						if (can_work_steal) {
							uint64_t block_size = 0x100000000ULL;  // 4G keys
							const char *bs = getenv("KEYHUNT_HYBRID_BLOCK_SIZE");
							if (bs && *bs) {
								if (bs[0] == '0' && (bs[1] == 'x' || bs[1] == 'X')) {
									block_size = strtoull(bs + 2, NULL, 16);
								} else {
									block_size = strtoull(bs, NULL, 10);
								}
							}
							if (block_size < 1024ULL) block_size = 1024ULL;
							block_size = (block_size / 1024ULL) * 1024ULL;

							output_success("Running GPU+CPU hybrid mode (work-stealing)...\n");
							output_info("HYBRID: work-stealing enabled (block size: 0x%llx, override: KEYHUNT_HYBRID_BLOCK_SIZE)\n",
							       (unsigned long long)block_size);

							g_work_pool.init(&n_range_start, &n_range_end, block_size);

							// Setup GPU thread (range args ignored in work-stealing mode)
							gpu_hybrid_args.start_key.Set(&n_range_start);
							gpu_hybrid_args.end_key.Set(&n_range_end);
							gpu_hybrid_args.stride.Set(&stride);
							gpu_hybrid_args.target_count = N;
							gpu_hybrid_args.result.store(0, std::memory_order_release);
							gpu_hybrid_args.completed.store(0, std::memory_order_release);

								// Reset GPU stats
								g_gpu_keys_checked.store(0, std::memory_order_release);
								g_gpu_keys_checked_cur.store(0, std::memory_order_release);
								g_gpu_should_stop.store(0, std::memory_order_release);

							int err = platform_thread_create(&gpu_thread_id, gpu_hybrid_thread, &gpu_hybrid_args);
							if (err != 0) {
								output_warning("Failed to start GPU thread, falling back to CPU-only\n");
								g_work_pool.disable();
								FLAGGPU_HYBRID.store(0, std::memory_order_release);
							} else {
								gpu_hybrid_started = 1;
								output_success("GPU thread started, CPU uses normal fast algorithm\n");
							}
						} else {
							output_success("Running GPU+CPU hybrid mode (static split)...\n");

					// Auto-tune the split unless user overrides with KEYHUNT_HYBRID_GPU_PERCENT.
					{
						const char *env = getenv("KEYHUNT_HYBRID_GPU_PERCENT");
						if (!(env && *env)) {
							int tuned = hybrid_get_gpu_range_percent_default(NTHREADS);
							if (tuned != g_gpu_range_percent) {
								g_gpu_range_percent = tuned;
								output_info("HYBRID: auto split GPU %d%% / CPU %d%% (override: KEYHUNT_HYBRID_GPU_PERCENT)\n",
								       g_gpu_range_percent, 100 - g_gpu_range_percent);
							}
						}
					}
	
					// Calculate range split: GPU gets g_gpu_range_percent% of range.
					// Ranges are treated as [start, end) (end is exclusive) throughout keyhunt.
					Int range_diff, gpu_portion, gpu_range_end, cpu_range_start;
					range_diff.Set(&n_range_end);
				range_diff.Sub(&n_range_start);

				// GPU gets g_gpu_range_percent% of the range
				gpu_portion.Set(&range_diff);
				gpu_portion.Mult(g_gpu_range_percent);
				Int divisor;
				divisor.SetInt32(100);
				gpu_portion.Div(&divisor);

				// GPU range: n_range_start to (n_range_start + gpu_portion)
				gpu_range_end.Set(&n_range_start);
				gpu_range_end.Add(&gpu_portion);

				// CPU range: starts at GPU end (no +1, end is exclusive)
				cpu_range_start.Set(&gpu_range_end);

				output_success("GPU handles %d%% of range, CPU handles %d%%\n",
					   g_gpu_range_percent, 100 - g_gpu_range_percent);

				// Print ranges in inclusive form for readability.
				char *hextemp = n_range_start.GetBase16();
				output_success("GPU range: 0x%s", hextemp);
				free(hextemp);
				{
					Int gpu_end_inclusive;
					gpu_end_inclusive.Set(&gpu_range_end);
					if (gpu_end_inclusive.IsGreater(&n_range_start)) {
						gpu_end_inclusive.SubOne();
					}
					hextemp = gpu_end_inclusive.GetBase16();
					printf(" - 0x%s\n", hextemp);
					free(hextemp);
				}
				hextemp = cpu_range_start.GetBase16();
				output_success("CPU range: 0x%s", hextemp);
				free(hextemp);
				{
					Int cpu_end_inclusive;
					cpu_end_inclusive.Set(&n_range_end);
					if (cpu_end_inclusive.IsGreater(&cpu_range_start)) {
						cpu_end_inclusive.SubOne();
					}
					hextemp = cpu_end_inclusive.GetBase16();
					printf(" - 0x%s\n", hextemp);
					free(hextemp);
				}

				// Setup GPU thread arguments with its portion of the range
				gpu_hybrid_args.start_key.Set(&n_range_start);
				gpu_hybrid_args.end_key.Set(&gpu_range_end);
				gpu_hybrid_args.stride.Set(&stride);
				gpu_hybrid_args.target_count = N;
			gpu_hybrid_args.result.store(0, std::memory_order_release);
			gpu_hybrid_args.completed.store(0, std::memory_order_release);

				// Reset GPU stats
				g_gpu_keys_checked.store(0, std::memory_order_release);
				g_gpu_keys_checked_cur.store(0, std::memory_order_release);
				g_gpu_should_stop.store(0, std::memory_order_release);

			// Start GPU thread (with its fixed range)
			int err = platform_thread_create(&gpu_thread_id, gpu_hybrid_thread, &gpu_hybrid_args);
			if (err != 0) {
				output_warning("Failed to start GPU thread, falling back to CPU-only\n");
				FLAGGPU_HYBRID.store(0, std::memory_order_release);
					} else {
						gpu_hybrid_started = 1;
						// Update n_range_start for CPU threads - they use normal algorithm
						n_range_start.Set(&cpu_range_start);
							maybe_adjust_cpu_sequential_max((size_t)NTHREADS, cpu_range_start, n_range_end,
							                                "KEYHUNT_HYBRID_CPU_N", "HYBRID");
							output_success("GPU thread started, CPU uses normal fast algorithm\n");
						}
							}
						}  // End of else (GPU available)
				}

			// ============================================================================
			// CPU Thread Mode (fall-through or default)
			// ============================================================================
#ifndef _WIN64
				if (g_work_pool.enabled) {
					shutdown_work_queue();
				} else if(FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_XPOINT || FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_VANITY) {
					configure_work_queue((size_t)NTHREADS);
				} else {
					shutdown_work_queue();
				}
#endif
			profile_init_threads((int)NTHREADS);
			for(j= 0;j < NTHREADS; j++)	{
				tt = (tothread*) malloc(sizeof(struct tothread));
				checkpointer((void *)tt,__FILE__,"malloc","tt" ,__LINE__ -1 );
				tt->nt = j;
			steps[j].value = 0;
			s = 0;
			switch(FLAGMODE)	{
				case MODE_ADDRESS:
				case MODE_XPOINT:
				case MODE_RMD160:
					s = platform_thread_create(&tid[j], thread_process, (void *)tt);
				break;
				case MODE_MINIKEYS:
					s = platform_thread_create(&tid[j], thread_process_minikeys, (void *)tt);
				break;
				case MODE_VANITY:
					s = platform_thread_create(&tid[j], thread_process_vanity, (void *)tt);
				break;
			}
			if(s != 0)	{
				output_error("pthread_create thread_process\n");
				exit(EXIT_FAILURE);
			}
		}
	}
	
		initialize_rate_limits();
	
		continue_flag = 1;
		total.SetInt32(0);
		pretotal.SetInt32(0);
		debugcount_mpz.Set(&BSGS_N);
		seconds.SetInt32(0);
		Int prev_cpu_total;
		prev_cpu_total.SetInt32(0);
		uint64_t prev_gpu_total_u64 = 0;
		do	{
			sleep_ms(1000);
			seconds.AddOne();
			check_flag = 1;
		for(j = 0; j <NTHREADS && check_flag; j++) {
			check_flag &= ends[j].value;
		}
		if(check_flag)	{
			continue_flag = 0;
		}
		if(OUTPUTSECONDS.IsGreater(&ZERO) ){
			MPZAUX.Set(&seconds);
			MPZAUX.Mod(&OUTPUTSECONDS);
				if(MPZAUX.IsZero()) {
					platform_mutex_lock(&bsgs_thread);
					if (FLAGGPU_HYBRID && gpu_hybrid_started) {
						Int cpu_total;
						cpu_total.SetInt32(0);
						for (j = 0; j < NTHREADS; j++) {
							pretotal.Set(&debugcount_mpz);
							pretotal.Mult(steps[j].value);
							cpu_total.Add(&pretotal);
						}

							uint64_t gpu_total_u64 = gpu_keys_checked_total_u64();
						Int gpu_total;
						{
							char tmp[64];
							snprintf(tmp, sizeof(tmp), "%" PRIu64, gpu_total_u64);
							gpu_total.SetBase10(tmp);
						}

						Int cpu_delta;
						cpu_delta.Set(&cpu_total);
						cpu_delta.Sub(&prev_cpu_total);
						uint64_t gpu_delta_u64 = gpu_total_u64 - prev_gpu_total_u64;
						Int gpu_delta;
						{
							char tmp[64];
							snprintf(tmp, sizeof(tmp), "%" PRIu64, gpu_delta_u64);
							gpu_delta.SetBase10(tmp);
						}

						Int period;
						period.Set(&OUTPUTSECONDS);
						if (period.IsZero()) {
							period.SetInt32(1);
						}

						Int overall_total;
						overall_total.Set(&cpu_total);
						overall_total.Add(&gpu_total);

						// Report to adaptive scheduler for throughput tracking
						{
							// Convert Int deltas to uint64_t for adaptive scheduler
							// (Safe truncation - deltas per period are typically < 2^64)
							uint64_t cpu_delta_u64 = cpu_delta.IsPositive() ?
								strtoull(cpu_delta.GetBase10(), NULL, 10) : 0;
							uint64_t period_ms = period.IsPositive() ?
								strtoull(period.GetBase10(), NULL, 10) * 1000 : 1000;

							if (cpu_delta_u64 > 0) {
								adaptive_report_work(WORKER_CPU, cpu_delta_u64, period_ms);
							}
							// Note: GPU reporting is handled in gpu_hybrid_thread
						}

						Int cpu_rate;
						cpu_rate.Set(&cpu_delta);
						cpu_rate.Div(&period);
						Int gpu_rate;
						gpu_rate.Set(&gpu_delta);
						gpu_rate.Div(&period);
						Int overall_rate;
						overall_rate.Set(&cpu_delta);
						overall_rate.Add(&gpu_delta);
						overall_rate.Div(&period);

						char cpu_rate_str[128];
						char gpu_rate_str[128];
						char overall_rate_str[128];
						format_keys_per_second(cpu_rate, cpu_rate_str, sizeof(cpu_rate_str));
						format_keys_per_second(gpu_rate, gpu_rate_str, sizeof(gpu_rate_str));
						format_keys_per_second(overall_rate, overall_rate_str, sizeof(overall_rate_str));

						str_seconds = seconds.GetBase10();
						str_total = overall_total.GetBase10();
						char *str_period = period.GetBase10();

							const bool line_mode = (FLAGMATRIX || FLAGQUIET || FLAGGPU_HYBRID);
							if (line_mode) {
								snprintf(buffer, sizeof(buffer),
									"[+] Total %s keys in %s seconds (last %s s): CPU %s | GPU %s | TOTAL %s\n",
									str_total ? str_total : "?", str_seconds ? str_seconds : "?",
									str_period ? str_period : "?",
									cpu_rate_str, gpu_rate_str, overall_rate_str);
							} else {
								snprintf(buffer, sizeof(buffer),
									"\r[+] Total %s keys in %s seconds (last %s s): CPU %s | GPU %s | TOTAL %s\r",
									str_total ? str_total : "?", str_seconds ? str_seconds : "?",
									str_period ? str_period : "?",
									cpu_rate_str, gpu_rate_str, overall_rate_str);
							}

						append_progress_info(buffer, sizeof(buffer));
						append_profile_info(buffer, sizeof(buffer));
						append_adaptive_info(buffer, sizeof(buffer));
						printf("%s", buffer);
						fflush(stdout);
						THREADOUTPUT = 0;

						// Show visual progress bar if range progress is enabled
						if (g_rangeProgressEnabled) {
							int permille = 0;
							char pos[48];
							if (capture_progress_metrics(permille, pos, sizeof(pos))) {
								double percent = permille / 10.0;
								// Convert Int overall_rate to double for Mkeys/s
								char *rate_str = overall_rate.GetBase10();
								double speed_mkeys = strtod(rate_str ? rate_str : "0", NULL) / 1000000.0;
								if (rate_str) free(rate_str);
								uint64_t keys_checked = strtoull(str_total ? str_total : "0", NULL, 10);
								// Calculate ETA: remaining keys / speed
								int secs = atoi(str_seconds ? str_seconds : "0");
								double remaining_ratio = (1000.0 - permille) / 1000.0;
								double total_estimate = (permille > 0) ? (secs / (permille / 1000.0)) : 0;
								int eta_seconds = (int)(total_estimate * remaining_ratio);
								output_progress(percent, speed_mkeys, keys_checked, eta_seconds);
								printf("\n");
							}
						}

						// Update progress tracking for GPU hybrid mode
						if (g_progress_enabled) {
							static bool progress_save_warned = false;
							char *current_hex = n_range_start.GetBase16();
							int update_result = progress_update(&g_progress_state, current_hex, strtoull(str_total ? str_total : "0", NULL, 10));
							if (update_result != 0 && !progress_save_warned) {
								output_warning("Failed to save progress. Check disk space and permissions.\n");
								progress_save_warned = true;
							}
							if (current_hex) free(current_hex);
						}

						prev_cpu_total.Set(&cpu_total);
						prev_gpu_total_u64 = gpu_total_u64;

						if (str_seconds) free(str_seconds);
						if (str_total) free(str_total);
						if (str_period) free(str_period);
					} else {
						total.SetInt32(0);
						for(j = 0; j < NTHREADS; j++) {
							pretotal.Set(&debugcount_mpz);
							pretotal.Mult(steps[j].value);
							total.Add(&pretotal);
						}

						if(FLAGENDOMORPHISM)	{
							if(FLAGMODE == MODE_XPOINT)	{
								total.Mult(3);
							}
							else	{
								total.Mult(6);
							}
						}

						pretotal.Set(&total);
						pretotal.Div(&seconds);
						str_seconds = seconds.GetBase10();
						str_pretotal = pretotal.GetBase10();
						str_total = total.GetBase10();

							const bool line_mode = (FLAGMATRIX || FLAGQUIET || FLAGGPU_HYBRID);
							if(pretotal.IsLower(&int_limits[0]))	{
								if(line_mode)	{
									snprintf(buffer,sizeof(buffer),"[+] Total %s keys in %s seconds: %s keys/s\n",str_total,str_seconds,str_pretotal);
								}
								else	{
									snprintf(buffer,sizeof(buffer),"\r[+] Total %s keys in %s seconds: %s keys/s\r",str_total,str_seconds,str_pretotal);
								}
							}
							else	{
							i = 0;
							salir = 0;
							while( i < 6 && !salir)	{
								if(pretotal.IsLower(&int_limits[i+1]))	{
									salir = 1;
								}
								else	{
									i++;
								}
							}

							div_pretotal.Set(&pretotal);
							div_pretotal.Div(&int_limits[salir ? i : i-1]);
							str_divpretotal = div_pretotal.GetBase10();
								if(line_mode)	{
									snprintf(buffer,sizeof(buffer),"[+] Total %s keys in %s seconds: ~%s %s (%s keys/s)\n",str_total,str_seconds,str_divpretotal,str_limits_prefixs[salir ? i : i-1],str_pretotal);
								}
								else	{
									if(THREADOUTPUT == 1)	{
										snprintf(buffer,sizeof(buffer),"\r[+] Total %s keys in %s seconds: ~%s %s (%s keys/s)\r",str_total,str_seconds,str_divpretotal,str_limits_prefixs[salir ? i : i-1],str_pretotal);
								}
								else	{
									snprintf(buffer,sizeof(buffer),"\r[+] Total %s keys in %s seconds: ~%s %s (%s keys/s)\r",str_total,str_seconds,str_divpretotal,str_limits_prefixs[salir ? i : i-1],str_pretotal);
								}
							}
							free(str_divpretotal);

						}
						append_progress_info(buffer, sizeof(buffer));
						append_profile_info(buffer, sizeof(buffer));
						append_adaptive_info(buffer, sizeof(buffer));
						printf("%s",buffer);
						fflush(stdout);
						THREADOUTPUT = 0;

						// Show visual progress bar if range progress is enabled
						if (g_rangeProgressEnabled) {
							int permille = 0;
							char pos[48];
							if (capture_progress_metrics(permille, pos, sizeof(pos))) {
								double percent = permille / 10.0;
								// Use str_pretotal (keys/s) to calculate Mkeys/s
								double speed_mkeys = strtod(str_pretotal ? str_pretotal : "0", NULL) / 1000000.0;
								uint64_t keys_checked = strtoull(str_total ? str_total : "0", NULL, 10);
								// Calculate ETA: remaining keys / speed
								int secs = atoi(str_seconds ? str_seconds : "0");
								double remaining_ratio = (1000.0 - permille) / 1000.0;
								double total_estimate = (permille > 0) ? (secs / (permille / 1000.0)) : 0;
								int eta_seconds = (int)(total_estimate * remaining_ratio);
								output_progress(percent, speed_mkeys, keys_checked, eta_seconds);
								printf("\n");
							}
						}

						// Update progress tracking (auto-saves every 60s)
						if (g_progress_enabled) {
							static bool progress_save_warned_cpu = false;
							char *current_hex = n_range_start.GetBase16();
							int update_result = progress_update(&g_progress_state, current_hex, strtoull(str_total ? str_total : "0", NULL, 10));
							if (update_result != 0 && !progress_save_warned_cpu) {
								output_warning("Failed to save progress. Check disk space and permissions.\n");
								progress_save_warned_cpu = true;
							}
							if (current_hex) free(current_hex);
						}

						free(str_seconds);
						free(str_pretotal);
						free(str_total);
					}
					platform_mutex_unlock(&bsgs_thread);
				}
			}
		}while(continue_flag);

		// Wait for GPU thread if hybrid mode was started
		if (FLAGGPU_HYBRID && gpu_hybrid_started) {
			printf("\n[+] Waiting for GPU thread to complete...\n");
			platform_thread_join(gpu_thread_id, NULL);

			output_success("GPU thread finished. Result: %d keys found\n", gpu_hybrid_args.result.load(std::memory_order_acquire));
			output_success("GPU keys checked: %" PRIu64 "\n", gpu_keys_checked_total_u64());

			// Print final adaptive scheduler stats
			if (g_adaptive_scheduler.initialized) {
				double cpu_mkeys = 0.0, gpu_mkeys = 0.0;
				int cpu_pct = 0, gpu_pct = 0;
				adaptive_get_stats(&cpu_mkeys, &gpu_mkeys, &cpu_pct, &gpu_pct);
				output_success("Final adaptive stats: CPU=%.1f Mkeys/s (%d%%), GPU=%.1f Mkeys/s (%d%%)\n",
				       cpu_mkeys, cpu_pct, gpu_mkeys, gpu_pct);
				output_success("Optimal ratio for next run: CPU=%d%%, GPU=%d%%\n", cpu_pct, gpu_pct);
			}

			// Cleanup adaptive scheduler
			adaptive_cleanup();

			// Cleanup GPU
			gpu_backend_shutdown();
			if (g_work_pool.enabled) {
				g_work_pool.disable();
			}
		}

	// Mark progress as complete (removes progress file)
	if (g_progress_enabled) {
		progress_complete(&g_progress_state);
		output_info("Progress tracking completed\n");
	}

	printf("\nEnd\n");
#ifndef _WIN64
	shutdown_work_queue();
#endif
	platform_mutex_destroy(&write_keys);
	platform_mutex_destroy(&write_random);
	platform_mutex_destroy(&bsgs_thread);
}

/* pubkeytopubaddress_dst, rmd160toaddress_dst, pubkeytopubaddress
 * moved to crypto/address_util.cpp */

/* cmp_hash20, load_u64_be, load_u32_be moved to sort/sort.cpp */

static inline bool sub_u64_if_fits(const Int &a, const Int &b, uint64_t *out) {
	// Compute (a - b) if it fits in uint64_t. Return false otherwise.
	// Assumes Int represents non-negative values here.
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

/* searchbinary moved to sort/sort.cpp */

/* thread_process_minikeys moved to search/search_minikeys.cpp */

#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process(LPVOID vargp) {
#else
void *thread_process(void *vargp)	{
#endif
	struct tothread *tt;
	struct thread_args *targs = NULL;
	keyhunt_config_t *cfg = NULL;

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
	int r,thread_number,continue_flag = 1,k;
	char *hextemp = NULL;

	char publickeyhashrmd160[20];
	char publickeyhashrmd160_uncompress[4][20];
	char rawvalue[32];

	char publickeyhashrmd160_endomorphism[12][4][20];

	/* Extract thread_number and config (if available) */
	tt = (struct tothread *)vargp;
	thread_number = tt->nt;

	/* Config-aware variables - use config if available, else fall back to globals */
	int local_mode = FLAGMODE;
	int local_search = FLAGSEARCH;
	int local_crypto = FLAGCRYPTO;
	bool local_endomorphism = FLAGENDOMORPHISM != 0;
	bool local_random = FLAGRANDOM != 0;
	bool local_quiet = FLAGQUIET != 0;
	bool local_matrix = FLAGMATRIX != 0;

	bool calculate_y = local_search == SEARCH_UNCOMPRESS || local_search == SEARCH_BOTH || local_crypto == CRYPTO_ETH;
	calculate_y = calculate_y || cpu_use_y_parity_for_compressed_btc();
	Int key_mpz,keyfound;
	Int key_center;
	Int stride_half;
	Int stride_four;

	free(tt);
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
						if (sub_u64_if_fits(range_end_local, key_mpz, &rem_u64) && rem_u64 < block_limit) {
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
					THREADOUTPUT = 1;
				}
			}
				do {
					// Compute the initial center point once; subsequent centers are advanced by +GRP_SIZE*G at the loop tail.
					if (count == 0) {
						key_center.Set(&key_mpz);
						key_center.Add(&stride_half);
						startP = secp->ComputePublicKey(&key_center);
					}

				profile_counters_t *prof = KH_PROF_PTR();
				const uint64_t ec_start = prof ? profile_now_ns() : 0;

				for(i = 0; i < hLength; i++) {
					dx[i].ModSub(&Gn[i].x,&startP.x);
				}

				dx[i].ModSub(&Gn[i].x,&startP.x);  // For the first point
				dx[i + 1].ModSub(&_2Gn.x,&startP.x); // For the next center point

				grp->ModInvOptimized();  // Use 8x unrolled version

				pts[CPU_GRP_SIZE / 2] = startP;

				for(i = 0; i<hLength; i++) {
					// Prefetch next Gn element for better cache utilization
					if (i + 4 < hLength) {
						__builtin_prefetch(&Gn[i + 4], 0, 3);
						__builtin_prefetch(&dx[i + 4], 0, 3);
					}

					pp = startP;
					pn = startP;

					// P = startP + i*G
					dy.ModSub(&Gn[i].y,&pp.y);

					_s.ModMulK1(&dy,&dx[i]);        // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
					_p.ModSquareK1(&_s);            // _p = pow2(s)

					pp.x.ModNeg();
					pp.x.ModAdd(&_p);
					pp.x.ModSub(&Gn[i].x);           // rx = pow2(s) - p1.x - p2.x;

					if(calculate_y)	{
						pp.y.ModSub(&Gn[i].x,&pp.x);
						pp.y.ModMulK1(&_s);
						pp.y.ModSub(&Gn[i].y);           // ry = - p2.y - s*(ret.x-p2.x);
					}

					// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
					dyn.Set(&Gn[i].y);
					dyn.ModNeg();
					dyn.ModSub(&pn.y);

					_s.ModMulK1(&dyn,&dx[i]);      // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
					_p.ModSquareK1(&_s);            // _p = pow2(s)
					pn.x.ModNeg();
					pn.x.ModAdd(&_p);
					pn.x.ModSub(&Gn[i].x);          // rx = pow2(s) - p1.x - p2.x;

					if(calculate_y)	{
						pn.y.ModSub(&Gn[i].x,&pn.x);
						pn.y.ModMulK1(&_s);
						pn.y.ModAdd(&Gn[i].y);          // ry = - p2.y - s*(ret.x-p2.x);
					}

					pp_offset = CPU_GRP_SIZE / 2 + (i + 1);
					pn_offset = CPU_GRP_SIZE / 2 - (i + 1);

					pts[pp_offset] = pp;
					pts[pn_offset] = pn;
					
					if(local_endomorphism)	{
						/*
							Q = (x,y)
							For any point Q
							Q*lambda = (x*beta mod p ,y)
							Q*lambda is a Scalar Multiplication
							x*beta is just a Multiplication (Very fast)
						*/
						
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
				/*
					Half point for endomorphism because pts[CPU_GRP_SIZE / 2] was not calcualte in the previous cycle
				*/
				if(local_endomorphism)	{
					if( calculate_y  )	{

						endomorphism_beta[CPU_GRP_SIZE / 2].y.Set(&pts[CPU_GRP_SIZE / 2].y);
						endomorphism_beta2[CPU_GRP_SIZE / 2].y.Set(&pts[CPU_GRP_SIZE / 2].y);
					}
					endomorphism_beta[CPU_GRP_SIZE / 2].x.ModMulK1(&pts[CPU_GRP_SIZE / 2].x, &beta);
					endomorphism_beta2[CPU_GRP_SIZE / 2].x.ModMulK1(&pts[CPU_GRP_SIZE / 2].x, &beta2);
				}

				// First point (startP - (GRP_SZIE/2)*G)
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
				
				/*
					First point for endomorphism because pts[0] was not calcualte previously
				*/
				if(local_endomorphism)	{
					if( calculate_y  )	{
						endomorphism_beta[0].y.Set(&pn.y);
						endomorphism_beta2[0].y.Set(&pn.y);
					}
					endomorphism_beta[0].x.ModMulK1(&pn.x, &beta);
					endomorphism_beta2[0].x.ModMulK1(&pn.x, &beta2);
				}
								
				if (prof) prof->ns_ec += (profile_now_ns() - ec_start);
				if((local_mode == MODE_RMD160 || local_mode == MODE_ADDRESS) && local_crypto == CRYPTO_BTC && !local_endomorphism) {
					process_rmd160_batch_btc_simple(key_mpz, pts, count);
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
										generate_binaddress_eth(endomorphism_beta[(4*j)+k],(uint8_t*)publickeyhashrmd160_endomorphism[4][k]);
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
												r = bloom_ext_check_rmd160(&bloom, (uint8_t*)publickeyhashrmd160_endomorphism[l][k]);
												if(r) {
													r = searchbinary(addressTable,publickeyhashrmd160_endomorphism[l][k],N);
													if(r) {
														keyfound.SetInt32(k);
														keyfound.Mult(&stride);
														keyfound.Add(&key_mpz);
														publickey = secp->ComputePublicKey(&keyfound);
														switch(l)	{
															case 0:	//Original point, prefix 02
																if(publickey.y.IsOdd())	{	//if the current publickey is odd that means, we need to negate the keyfound to get the correct key
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
																// else we dont need to chage the current keyfound because it already have prefix 02
															break;
															case 1:	//Original point, prefix 03
																if(publickey.y.IsEven())	{	//if the current publickey is even that means, we need to negate the keyfound to get the correct key
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
																// else we dont need to chage the current keyfound because it already have prefix 03
															break;
															case 2:	//Beta point, prefix 02
																keyfound.ModMulK1order(&lambda);
																if(publickey.y.IsOdd())	{	//if the current publickey is odd that means, we need to negate the keyfound to get the correct key
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
																// else we dont need to chage the current keyfound because it already have prefix 02
															break;
															case 3:	//Beta point, prefix 03											
																keyfound.ModMulK1order(&lambda);
																if(publickey.y.IsEven())	{	//if the current publickey is even that means, we need to negate the keyfound to get the correct key
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
																// else we dont need to chage the current keyfound because it already have prefix 02
															break;
															case 4:	//Beta^2 point, prefix 02
																keyfound.ModMulK1order(&lambda2);
																if(publickey.y.IsOdd())	{	//if the current publickey is odd that means, we need to negate the keyfound to get the correct key
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
																// else we dont need to chage the current keyfound because it already have prefix 02
															break;
															case 5:	//Beta^2 point, prefix 03
																keyfound.ModMulK1order(&lambda2);
																if(publickey.y.IsEven())	{	//if the current publickey is even that means, we need to negate the keyfound to get the correct key
																	keyfound.Neg();
																	keyfound.Add(&secp->order);
																}
																// else we dont need to chage the current keyfound because it already have prefix 02
															break;
														}
														writekey(true,&keyfound);
													}
												}
											}
										}
										else	{
											for(l = 0;l < 2; l++)	{
												r = bloom_ext_check_rmd160(&bloom, (uint8_t*)publickeyhashrmd160_endomorphism[l][k]);
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
											for(l = 6;l < 12; l++)	{	//We check the array from 6 to 12(excluded) because we save the uncompressed information there
												r = bloom_ext_check_rmd160(&bloom, (uint8_t*)publickeyhashrmd160_endomorphism[l][k]);	//Check in Bloom filter
												if(r) {
													r = searchbinary(addressTable,publickeyhashrmd160_endomorphism[l][k],N);		//Check in Array using Binary search
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
											r = bloom_ext_check_rmd160(&bloom, (uint8_t*)publickeyhashrmd160_uncompress[k]);
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
											r = bloom_ext_check_rmd160(&bloom, (uint8_t*)publickeyhashrmd160_endomorphism[l][k]);
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
										r = bloom_ext_check_rmd160(&bloom, (uint8_t*)publickeyhashrmd160_uncompress[k]);
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
									r = bloom_ext_check(&bloom,rawvalue,MAXLENGTHADDRESS);
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
									r = bloom_ext_check(&bloom,rawvalue,MAXLENGTHADDRESS);
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
									r = bloom_ext_check(&bloom,rawvalue,MAXLENGTHADDRESS);
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
									r = bloom_ext_check(&bloom,rawvalue,MAXLENGTHADDRESS);
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
				/*
				if(FLAGDEBUG) {
					printf("\n[D] thread_process %i\n",__LINE__ -1 );
					fflush(stdout);
				}
				*/

				steps[thread_number].value++;

				// Next start point (startP + GRP_SIZE*G)
				pp = startP;
				dy.ModSub(&_2Gn.y,&pp.y);

				_s.ModMulK1(&dy,&dx[i + 1]);
				_p.ModSquareK1(&_s);

				pp.x.ModNeg();
				pp.x.ModAdd(&_p);
				pp.x.ModSub(&_2Gn.x);

				//The Y value for the next start point always need to be calculated
				pp.y.ModSub(&_2Gn.x,&pp.x);
				pp.y.ModMulK1(&_s);
				pp.y.ModSub(&_2Gn.y);
				startP = pp;
				}while(count < block_limit && continue_flag);
			}
		} while(continue_flag);
		ends[thread_number].value = 1;
		return NULL;
	}


/*
 * thread_process_vanity - VANITY mode search thread
 *
 * Generates vanity addresses with custom prefixes (e.g., 1LOVE..., 1Pizza...).
 * Uses batch EC operations with SIMD-optimized hashing for high throughput.
 *
 * Optimization features:
 * - Batch public key computation (CPU_GRP_SIZE points at a time)
 * - SIMD hash functions (AVX512/AVX2/SSE)
 * - Endomorphism for 6x key checking per EC operation
 * - Bloom filter for multi-prefix matching
 */
#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process_vanity(LPVOID vargp) {
#else
void *thread_process_vanity(void *vargp)	{
#endif
	struct tothread *tt;
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
	Point pp;	//point positive
	Point pn;	//point negative
	int l,pp_offset,pn_offset,i,hLength = (CPU_GRP_SIZE / 2 - 1);
	uint64_t j,count;
	Point R,temporal,publickey;
	int thread_number,continue_flag = 1,k;
	char *hextemp = NULL;
	char publickeyhashrmd160[20];
	char publickeyhashrmd160_uncompress[4][20];
	
	char publickeyhashrmd160_endomorphism[12][4][20];
	
	Int key_mpz,keyfound;
	Int key_center;
	Int stride_half;
	Int stride_four;
	tt = (struct tothread *)vargp;
	thread_number = tt->nt;
	free(tt);
	profile_set_thread(thread_number);
	grp->Set(dx);
	stride_half.SetInt32(CPU_GRP_SIZE / 2);
	stride_half.Mult(&stride);
	stride_four.SetInt32(4);
	stride_four.Mult(&stride);
	
	
	//if FLAGENDOMORPHISM  == 1 and only compress search is enabled then there is no need to calculate the Y value value					
	
	bool calculate_y = FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH;
	
	/*
	if(FLAGDEBUG && thread_number == 0)	{
		printf("[D] vanity_rmd_targets = %i          fillllll\n",vanity_rmd_targets);
		printf("[D] vanity_rmd_total = %i\n",vanity_rmd_total);
		for(i =0; i < vanity_rmd_targets;i++)	{
			printf("[D] vanity_rmd_limits[%li] = %i\n",i,vanity_rmd_limits[i]);
			
		}
		printf("[D] vanity_rmd_minimun_bytes_check_length = %i\n",vanity_rmd_minimun_bytes_check_length);
	}
	*/
	

	do {
		if(!acquire_base_key(key_mpz))	{
			continue_flag = 0;
		}
			if(continue_flag)	{
					count = 0;
					uint64_t block_limit = N_SEQUENTIAL_MAX;
					if (!FLAGRANDOM && stride.IsOne()) {
						Int range_end_local;
						if (g_work_pool.enabled && cpu_cached_block_valid) {
							range_end_local.Set(&cpu_cached_block_end);
						} else {
							range_end_local.Set(&n_range_end);
						}
						uint64_t rem_u64 = 0;
						if (sub_u64_if_fits(range_end_local, key_mpz, &rem_u64) && rem_u64 < block_limit) {
							block_limit = rem_u64;
						}
				}
				if(FLAGMATRIX)	{
						hextemp = key_mpz.GetBase16();
						printf("Base key: %s thread %i\n",hextemp,thread_number);
						fflush(stdout);
					free(hextemp);
			}
			else	{
				if(FLAGQUIET == 0)	{
					hextemp = key_mpz.GetBase16();
					printf("\rBase key: %s     \r",hextemp);
					fflush(stdout);
					free(hextemp);
					THREADOUTPUT = 1;
				}
			}
				do {
					if (count == 0) {
						key_center.Set(&key_mpz);
						key_center.Add(&stride_half);
						startP = secp->ComputePublicKey(&key_center);
					}

				for(i = 0; i < hLength; i++) {
					dx[i].ModSub(&Gn[i].x,&startP.x);
				}
			
				dx[i].ModSub(&Gn[i].x,&startP.x);  // For the first point
				dx[i + 1].ModSub(&_2Gn.x,&startP.x); // For the next center point
				grp->ModInvOptimized();  // Use 8x unrolled version

				pts[CPU_GRP_SIZE / 2] = startP;

				for(i = 0; i<hLength; i++) {
					// Prefetch next Gn element for better cache utilization
					if (i + 4 < hLength) {
						__builtin_prefetch(&Gn[i + 4], 0, 3);
						__builtin_prefetch(&dx[i + 4], 0, 3);
					}

					pp = startP;
					pn = startP;

					// P = startP + i*G
					dy.ModSub(&Gn[i].y,&pp.y);

					_s.ModMulK1(&dy,&dx[i]);        // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
					_p.ModSquareK1(&_s);            // _p = pow2(s)

					pp.x.ModNeg();
					pp.x.ModAdd(&_p);
					pp.x.ModSub(&Gn[i].x);           // rx = pow2(s) - p1.x - p2.x;
					
					if(calculate_y)	{
						pp.y.ModSub(&Gn[i].x,&pp.x);
						pp.y.ModMulK1(&_s);
						pp.y.ModSub(&Gn[i].y);           // ry = - p2.y - s*(ret.x-p2.x);
					}

					// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
					dyn.Set(&Gn[i].y);
					dyn.ModNeg();
					dyn.ModSub(&pn.y);

					_s.ModMulK1(&dyn,&dx[i]);      // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
					_p.ModSquareK1(&_s);            // _p = pow2(s)
					pn.x.ModNeg();
					pn.x.ModAdd(&_p);
					pn.x.ModSub(&Gn[i].x);          // rx = pow2(s) - p1.x - p2.x;

					if( calculate_y  )	{
						pn.y.ModSub(&Gn[i].x,&pn.x);
						pn.y.ModMulK1(&_s);
						pn.y.ModAdd(&Gn[i].y);          // ry = - p2.y - s*(ret.x-p2.x);
					}
					pp_offset = CPU_GRP_SIZE / 2 + (i + 1);
					pn_offset = CPU_GRP_SIZE / 2 - (i + 1);

					pts[pp_offset] = pp;
					pts[pn_offset] = pn;
					
					if(FLAGENDOMORPHISM)	{
						/*
							Q = (x,y)
							For any point Q
							Q*lambda = (x*beta mod p ,y)
							Q*lambda is a Scalar Multiplication
							x*beta is just a Multiplication (Very fast)
						*/
						
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
				/*
					Half point for endomorphism because pts[CPU_GRP_SIZE / 2] was not calcualte in the previous cycle
				*/
				if(FLAGENDOMORPHISM)	{
					if( calculate_y  )	{

						endomorphism_beta[CPU_GRP_SIZE / 2].y.Set(&pts[CPU_GRP_SIZE / 2].y);
						endomorphism_beta2[CPU_GRP_SIZE / 2].y.Set(&pts[CPU_GRP_SIZE / 2].y);
					}
					endomorphism_beta[CPU_GRP_SIZE / 2].x.ModMulK1(&pts[CPU_GRP_SIZE / 2].x, &beta);
					endomorphism_beta2[CPU_GRP_SIZE / 2].x.ModMulK1(&pts[CPU_GRP_SIZE / 2].x, &beta2);
				}
				
				// First point (startP - (GRP_SZIE/2)*G)
				pn = startP;
				dyn.Set(&Gn[i].y);
				dyn.ModNeg();
				dyn.ModSub(&pn.y);

				_s.ModMulK1(&dyn,&dx[i]);
				_p.ModSquareK1(&_s);

				pn.x.ModNeg();
				pn.x.ModAdd(&_p);
				pn.x.ModSub(&Gn[i].x);
				
				if(calculate_y )	{
					pn.y.ModSub(&Gn[i].x,&pn.x);
					pn.y.ModMulK1(&_s);
					pn.y.ModAdd(&Gn[i].y);
				}
				pts[0] = pn;
				
				/*
					First point for endomorphism because pts[0] was not calcualte previously
				*/
				if(FLAGENDOMORPHISM)	{
					if( calculate_y  )	{
						endomorphism_beta[0].y.Set(&pn.y);
						endomorphism_beta2[0].y.Set(&pn.y);
					}
					endomorphism_beta[0].x.ModMulK1(&pn.x, &beta);
					endomorphism_beta2[0].x.ModMulK1(&pn.x, &beta2);
				}
				
					if(!FLAGENDOMORPHISM && FLAGCRYPTO == CRYPTO_BTC)	{
						const bool wantCompressed = (FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH);
						const bool wantUncompressed = (FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH);

						alignas(32) char hashCompressed02[CPU_GRP_SIZE][20];
						alignas(32) char hashCompressed03[CPU_GRP_SIZE][20];
						alignas(32) char hashUncompressed[CPU_GRP_SIZE][20];

						if (wantCompressed) {
							if (g_sysinfo.has_avx512) {
								for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 16) {
									secp->GetHash160_fromX_AVX512(P2PKH, 0x02,
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
										(uint8_t*)hashCompressed02[idx + 14], (uint8_t*)hashCompressed02[idx + 15]);
									secp->GetHash160_fromX_AVX512(P2PKH, 0x03,
										&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
										&pts[idx + 4].x, &pts[idx + 5].x, &pts[idx + 6].x, &pts[idx + 7].x,
										&pts[idx + 8].x, &pts[idx + 9].x, &pts[idx + 10].x, &pts[idx + 11].x,
										&pts[idx + 12].x, &pts[idx + 13].x, &pts[idx + 14].x, &pts[idx + 15].x,
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
									secp->GetHash160_fromX_AVX2(P2PKH, 0x02,
										&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
										&pts[idx + 4].x, &pts[idx + 5].x, &pts[idx + 6].x, &pts[idx + 7].x,
										(uint8_t*)hashCompressed02[idx], (uint8_t*)hashCompressed02[idx + 1],
										(uint8_t*)hashCompressed02[idx + 2], (uint8_t*)hashCompressed02[idx + 3],
										(uint8_t*)hashCompressed02[idx + 4], (uint8_t*)hashCompressed02[idx + 5],
										(uint8_t*)hashCompressed02[idx + 6], (uint8_t*)hashCompressed02[idx + 7]);
									secp->GetHash160_fromX_AVX2(P2PKH, 0x03,
										&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
										&pts[idx + 4].x, &pts[idx + 5].x, &pts[idx + 6].x, &pts[idx + 7].x,
										(uint8_t*)hashCompressed03[idx], (uint8_t*)hashCompressed03[idx + 1],
										(uint8_t*)hashCompressed03[idx + 2], (uint8_t*)hashCompressed03[idx + 3],
										(uint8_t*)hashCompressed03[idx + 4], (uint8_t*)hashCompressed03[idx + 5],
										(uint8_t*)hashCompressed03[idx + 6], (uint8_t*)hashCompressed03[idx + 7]);
								}
							} else {
								for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 4) {
									secp->GetHash160_fromX(P2PKH, 0x02,
										&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
										(uint8_t*)hashCompressed02[idx], (uint8_t*)hashCompressed02[idx + 1],
										(uint8_t*)hashCompressed02[idx + 2], (uint8_t*)hashCompressed02[idx + 3]);
									secp->GetHash160_fromX(P2PKH, 0x03,
										&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
										(uint8_t*)hashCompressed03[idx], (uint8_t*)hashCompressed03[idx + 1],
										(uint8_t*)hashCompressed03[idx + 2], (uint8_t*)hashCompressed03[idx + 3]);
								}
							}
						}

						if (wantUncompressed) {
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

						Int keyCurrent;
						keyCurrent.Set(&key_mpz);
						for (size_t idx = 0; idx < CPU_GRP_SIZE; ++idx) {
							if (wantCompressed) {
								if (vanityrmdmatch((unsigned char*)hashCompressed02[idx])) {
									Int candidate(keyCurrent);
									publickey = secp->ComputePublicKey(&candidate);
									if(publickey.y.IsOdd()) {
										candidate.Neg();
										candidate.Add(&secp->order);
									}
									writevanitykey(true,&candidate);
								}
								if (vanityrmdmatch((unsigned char*)hashCompressed03[idx])) {
									Int candidate(keyCurrent);
									publickey = secp->ComputePublicKey(&candidate);
									if(publickey.y.IsEven()) {
										candidate.Neg();
										candidate.Add(&secp->order);
									}
									writevanitykey(true,&candidate);
								}
							}

							if (wantUncompressed) {
								if (vanityrmdmatch((unsigned char*)hashUncompressed[idx])) {
									Int candidate(keyCurrent);
									writevanitykey(false,&candidate);
								}
							}

							keyCurrent.Add(&stride);
						}
						key_mpz.Set(&keyCurrent);
						count += CPU_GRP_SIZE;
					}
					else	{
						for(j = 0; j < CPU_GRP_SIZE/4;j++)	{
					if(FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH ){
						if(FLAGENDOMORPHISM)	{
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
					if(FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH)	{
						if(FLAGENDOMORPHISM)	{
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
					for(k = 0; k < 4;k++)	{
						if(FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH ){
							if(FLAGENDOMORPHISM)	{
								for(l = 0;l < 6; l++)	{
									if(vanityrmdmatch((uint8_t*)publickeyhashrmd160_endomorphism[l][k]))	{
										// Here the given publickeyhashrmd160 match againts one of the vanity targets
										// We need to check which of the cases is it.

										keyfound.SetInt32(k);
										keyfound.Mult(&stride);
										keyfound.Add(&key_mpz);
										publickey = secp->ComputePublicKey(&keyfound);
										
										switch(l)	{
											case 0:	//Original point, prefix 02
												if(publickey.y.IsOdd())	{	//if the current publickey is odd that means, we need to negate the keyfound to get the correct key
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
												// else we dont need to chage the current keyfound because it already have prefix 02
											break;
											case 1:	//Original point, prefix 03
												if(publickey.y.IsEven())	{	//if the current publickey is even that means, we need to negate the keyfound to get the correct key
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
												// else we dont need to chage the current keyfound because it already have prefix 03
											break;
											case 2:	//Beta point, prefix 02
												keyfound.ModMulK1order(&lambda);
												if(publickey.y.IsOdd())	{	//if the current publickey is odd that means, we need to negate the keyfound to get the correct key
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
												// else we dont need to chage the current keyfound because it already have prefix 02
											break;
											case 3:	//Beta point, prefix 03											
												keyfound.ModMulK1order(&lambda);
												if(publickey.y.IsEven())	{	//if the current publickey is even that means, we need to negate the keyfound to get the correct key
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
												// else we dont need to chage the current keyfound because it already have prefix 02
											break;
											case 4:	//Beta^2 point, prefix 02
												keyfound.ModMulK1order(&lambda2);
												if(publickey.y.IsOdd())	{	//if the current publickey is odd that means, we need to negate the keyfound to get the correct key
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
												// else we dont need to chage the current keyfound because it already have prefix 02
											break;
											case 5:	//Beta^2 point, prefix 03
												keyfound.ModMulK1order(&lambda2);
												if(publickey.y.IsEven())	{	//if the current publickey is even that means, we need to negate the keyfound to get the correct key
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
												// else we dont need to chage the current keyfound because it already have prefix 02
											break;
										}
										writevanitykey(true,&keyfound);
									}
								}
							}
							else	{
								for(l = 0;l < 2; l++)	{
									if(vanityrmdmatch((uint8_t*)publickeyhashrmd160_endomorphism[l][k]))	{
										keyfound.SetInt32(k);
										keyfound.Mult(&stride);
										keyfound.Add(&key_mpz);
										
										publickey = secp->ComputePublicKey(&keyfound);
										secp->GetHash160(P2PKH,true,publickey,(uint8_t*)publickeyhashrmd160);
										if(memcmp(publickeyhashrmd160_endomorphism[l][k],publickeyhashrmd160,20) != 0){
											keyfound.Neg();
											keyfound.Add(&secp->order);
											//if(FLAGDEBUG) printf("[D] Key need to be negated\n");
										}
										writevanitykey(true,&keyfound);
									}
								}									
							}
						}
						if(FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH)	{
							if(FLAGENDOMORPHISM)	{
								for(l = 6;l < 12; l++)	{
									if(vanityrmdmatch((uint8_t*)publickeyhashrmd160_endomorphism[l][k]))	{
										// Here the given publickeyhashrmd160 match againts one of the vanity targets
										// We need to check which of the cases is it.

										//rmd160toaddress_dst(publickeyhashrmd160_endomorphism[l][k],address);
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
										writevanitykey(false,&keyfound);
									}
								}

							}
							else	{
								if(vanityrmdmatch((uint8_t*)publickeyhashrmd160_uncompress[k]))	{
									keyfound.SetInt32(k);
									keyfound.Mult(&stride);
									keyfound.Add(&key_mpz);
									writevanitykey(false,&keyfound);
								}
							}
						}
						
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
				
				//The Y value for the next start point always need to be calculated
				pp.y.ModSub(&_2Gn.x,&pp.x);
				pp.y.ModMulK1(&_s);
				pp.y.ModSub(&_2Gn.y);
				startP = pp;
				}while(count < block_limit && continue_flag);
			}
		} while(continue_flag);
		ends[thread_number].value = 1;
		return NULL;
	}

/* Sorting functions (_swap, _sort, _introsort, _insertionsort, _partition,
 * _heapify, _myheapsort) moved to sort/sort.cpp */

/* ============================================================================
 * BSGS Sorting and Searching Functions
 * ============================================================================
 * These functions have been migrated to src/bsgs/bsgs_sort.cpp
 * Implementations removed to avoid duplicate symbols during linking
 * See bsgs/bsgs_sort.h for function declarations
 * ============================================================================ */

#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process_bsgs(LPVOID vargp) {
#else
void *thread_process_bsgs(void *vargp)	{
#endif
	// File-related variables
	FILE* filekey;
	struct tothread* tt;

	// Character variables
	uint8_t xpoint_raw[16];
	char *aux_c, *hextemp;

	// Integer variables
	Int base_key, keyfound;
	IntGroup* grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Int dx[CPU_GRP_SIZE / 2 + 1];
	Int dy, dyn, _s, _p, intaux;

	// Point variables
	Point base_point, point_aux, point_found, offset_point;
	Point startP;
	Point pp, pn;
	Point pts[CPU_GRP_SIZE];

	// Unsigned integer variables
	uint32_t k, l, r, salir, thread_number, cycles;

	// Other variables
	int hLength = (CPU_GRP_SIZE / 2 - 1);
	grp->Set(dx);

	tt = (struct tothread *)vargp;
	thread_number = tt->nt;
	free(tt);
	profile_set_thread((int)thread_number);
	
	cycles = bsgs_aux / 1024;
	if(bsgs_aux % 1024 != 0)	{
		cycles++;
	}

	intaux.Set(&BSGS_M_double);
	intaux.Mult(CPU_GRP_SIZE/2);
	intaux.Add(&BSGS_M);
	offset_point = secp->ComputePublicKey(&intaux);
		
	do	{	
	/*
		We do this in an atomic pthread_mutex operation to not affect others threads
		so BSGS_CURRENT is never the same between threads
	*/
platform_mutex_lock(&bsgs_thread);

		base_key.Set(&BSGS_CURRENT);	/* we need to set our base_key to the current BSGS_CURRENT value*/
		BSGS_CURRENT.Add(&BSGS_N_double);		/*Then add 2*BSGS_N to BSGS_CURRENT*/
		/*
		BSGS_CURRENT.Add(&BSGS_N);		//Then add BSGS_N to BSGS_CURRENT
		BSGS_CURRENT.Add(&BSGS_N);		//Then add BSGS_N to BSGS_CURRENT
		*/
		
platform_mutex_unlock(&bsgs_thread);

		if(base_key.IsGreaterOrEqual(&n_range_end))
			break;
		
		if(FLAGMATRIX)	{
			aux_c = base_key.GetBase16();
			output_success("Thread 0x%s \n",aux_c);
			fflush(stdout);
			free(aux_c);
		}
		else	{
			if(FLAGQUIET == 0){
				aux_c = base_key.GetBase16();
				printf("\r[+] Thread 0x%s   \r",aux_c);
				fflush(stdout);
				free(aux_c);
				THREADOUTPUT = 1;
			}
		}
		base_point = secp->ComputePublicKey(&base_key);
		point_aux = secp->AddDirect(base_point, offset_point);
		point_aux = secp->Negation(point_aux);
		for(k = 0; k < bsgs_point_number ; k++)	{
			if(bsgs_found[k] == 0)	{
				startP  = secp->AddDirect(OriginalPointsBSGS[k],point_aux);
				uint32_t j = 0;
				while( j < cycles && bsgs_found[k]== 0 )	{
					int i;
					for(i = 0; i < hLength; i++) {
						dx[i].ModSub(&GSn[i].x,&startP.x);
					}
					dx[i].ModSub(&GSn[i].x,&startP.x);  // For the first point
					dx[i+1].ModSub(&_2GSn.x,&startP.x); // For the next center point
					// Grouped ModInv
					grp->ModInvOptimized();  // Use 8x unrolled version
					/*
					We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
					We compute key in the positive and negative way from the center of the group
					*/
					// center point
					pts[CPU_GRP_SIZE / 2] = startP;
					for(i = 0; i<hLength; i++) {
						pp = startP;
						pn = startP;

						// P = startP + i*G
						dy.ModSub(&GSn[i].y,&pp.y);

						_s.ModMulK1(&dy,&dx[i]);        // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
						_p.ModSquareK1(&_s);            // _p = pow2(s)

						pp.x.ModNeg();
						pp.x.ModAdd(&_p);
						pp.x.ModSub(&GSn[i].x);           // rx = pow2(s) - p1.x - p2.x;
						// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
						dyn.Set(&GSn[i].y);
						dyn.ModNeg();
						dyn.ModSub(&pn.y);

						_s.ModMulK1(&dyn,&dx[i]);       // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
						_p.ModSquareK1(&_s);            // _p = pow2(s)

						pn.x.ModNeg();
						pn.x.ModAdd(&_p);
						pn.x.ModSub(&GSn[i].x);          // rx = pow2(s) - p1.x - p2.x;

						pts[CPU_GRP_SIZE / 2 + (i + 1)] = pp;
						pts[CPU_GRP_SIZE / 2 - (i + 1)] = pn;
					}
					// First point (startP - (GRP_SZIE/2)*G)
					pn = startP;
					dyn.Set(&GSn[i].y);
					dyn.ModNeg();
					dyn.ModSub(&pn.y);

					_s.ModMulK1(&dyn,&dx[i]);
					_p.ModSquareK1(&_s);

					pn.x.ModNeg();
					pn.x.ModAdd(&_p);
					pn.x.ModSub(&GSn[i].x);

					pts[0] = pn;
					for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k]== 0; i++) {
						pts[i].x.GetHi16Bytes(xpoint_raw);
						r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
						if(r) {
							r = bsgs_secondcheck(&base_key,((j*1024) + i),k,&keyfound);
							if(r)	{
								hextemp = keyfound.GetBase16();
								output_success("Thread Key found privkey %s   \n",hextemp);
								point_found = secp->ComputePublicKey(&keyfound);
								aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
								output_success("Publickey %s\n",aux_c);
platform_mutex_lock(&write_keys);

								filekey = fopen("KEYFOUNDKEYFOUND.txt","a");
								if(filekey != NULL)	{
									fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
									fclose(filekey);
								}
								free(hextemp);
								free(aux_c);
platform_mutex_unlock(&write_keys);
								bsgs_found[k] = 1;
								salir = 1;
								for(l = 0; l < bsgs_point_number && salir; l++)	{
									salir &= bsgs_found[l];
								}
								if(salir)	{
									printf("All points were found\n");
									exit(EXIT_FAILURE);
								}
							} //End if second check
						}//End if first check
					}// For for pts variable
					// Next start point (startP += (bsSize*GRP_SIZE).G)
					pp = startP;
					dy.ModSub(&_2GSn.y,&pp.y);

					_s.ModMulK1(&dy,&dx[i + 1]);
					_p.ModSquareK1(&_s);

					pp.x.ModNeg();
					pp.x.ModAdd(&_p);
					pp.x.ModSub(&_2GSn.x);

					pp.y.ModSub(&_2GSn.x,&pp.x);
					pp.y.ModMulK1(&_s);
					pp.y.ModSub(&_2GSn.y);
					startP = pp;
					
					j++;
				} // end while
			}// End if 
		}
		steps[thread_number].value+=2;
	}while(1);
	ends[thread_number].value = 1;
	return NULL;
}

#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process_bsgs_random(LPVOID vargp) {
#else
void *thread_process_bsgs_random(void *vargp)	{
#endif

	FILE *filekey;
	struct tothread *tt;
	uint8_t xpoint_raw[16];
	char *aux_c,*hextemp;
	Int base_key,keyfound,n_range_random;
	Point base_point,point_aux,point_found,offset_point;
	uint32_t l,k,r,salir,thread_number,cycles;
	
	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Point startP;
	
	int hLength = (CPU_GRP_SIZE / 2 - 1);
	
	Int dx[CPU_GRP_SIZE / 2 + 1];
	Point pts[CPU_GRP_SIZE];

	Int dy;
	Int dyn;
	Int _s;
	Int _p;
	Int intaux;
	Point pp;
	Point pn;
	grp->Set(dx);


	tt = (struct tothread *)vargp;
	thread_number = tt->nt;
	free(tt);
	profile_set_thread((int)thread_number);
	
	cycles = bsgs_aux / 1024;
	if(bsgs_aux % 1024 != 0)	{
		cycles++;
	}
	
	intaux.Set(&BSGS_M_double);
	intaux.Mult(CPU_GRP_SIZE/2);
	intaux.Add(&BSGS_M);
	offset_point = secp->ComputePublicKey(&intaux);

	do	{
		
	
	/*          | Start Range	| End Range     |
		None	| 1             | EC.N          |
		-b	bit | Min bit value | Max bit value |
		-r	A:B | A             | B             |
	*/
platform_mutex_lock(&bsgs_thread);

		base_key.Rand(&n_range_start,&n_range_end);
platform_mutex_unlock(&bsgs_thread);

		if(FLAGMATRIX)	{
				aux_c = base_key.GetBase16();
				output_success("Thread 0x%s  \n",aux_c);
				fflush(stdout);
				free(aux_c);
		}
		else{
			if(FLAGQUIET == 0){
				aux_c = base_key.GetBase16();
				printf("\r[+] Thread 0x%s  \r",aux_c);
				fflush(stdout);
				free(aux_c);
				THREADOUTPUT = 1;
			}
		}
		base_point = secp->ComputePublicKey(&base_key);
		point_aux = secp->AddDirect(base_point, offset_point);
		point_aux = secp->Negation(point_aux);


		/* We need to test individually every point in BSGS_Q */
		for(k = 0; k < bsgs_point_number ; k++)	{
			if(bsgs_found[k] == 0)	{			
				startP  = secp->AddDirect(OriginalPointsBSGS[k],point_aux);
				uint32_t j = 0;
				while( j < cycles && bsgs_found[k]== 0 )	{
				
					int i;
					for(i = 0; i < hLength; i++) {
						dx[i].ModSub(&GSn[i].x,&startP.x);
					}
					dx[i].ModSub(&GSn[i].x,&startP.x);  // For the first point
					dx[i+1].ModSub(&_2GSn.x,&startP.x); // For the next center point

					// Grouped ModInv
					grp->ModInvOptimized();  // Use 8x unrolled version
					
					/*
					We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
					We compute key in the positive and negative way from the center of the group
					*/

					// center point
					pts[CPU_GRP_SIZE / 2] = startP;
					
					for(i = 0; i<hLength; i++) {

						pp = startP;
						pn = startP;

						// P = startP + i*G
						dy.ModSub(&GSn[i].y,&pp.y);

						_s.ModMulK1(&dy,&dx[i]);        // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
						_p.ModSquareK1(&_s);            // _p = pow2(s)

						pp.x.ModNeg();
						pp.x.ModAdd(&_p);
						pp.x.ModSub(&GSn[i].x);           // rx = pow2(s) - p1.x - p2.x;

						// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
						dyn.Set(&GSn[i].y);
						dyn.ModNeg();
						dyn.ModSub(&pn.y);

						_s.ModMulK1(&dyn,&dx[i]);       // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
						_p.ModSquareK1(&_s);            // _p = pow2(s)

						pn.x.ModNeg();
						pn.x.ModAdd(&_p);
						pn.x.ModSub(&GSn[i].x);          // rx = pow2(s) - p1.x - p2.x;

						pts[CPU_GRP_SIZE / 2 + (i + 1)] = pp;
						pts[CPU_GRP_SIZE / 2 - (i + 1)] = pn;

					}

					// First point (startP - (GRP_SZIE/2)*G)
					pn = startP;
					dyn.Set(&GSn[i].y);
					dyn.ModNeg();
					dyn.ModSub(&pn.y);

					_s.ModMulK1(&dyn,&dx[i]);
					_p.ModSquareK1(&_s);

					pn.x.ModNeg();
					pn.x.ModAdd(&_p);
					pn.x.ModSub(&GSn[i].x);

					pts[0] = pn;

					for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k]== 0; i++) {
						pts[i].x.GetHi16Bytes(xpoint_raw);
						r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
						if(r) {
							r = bsgs_secondcheck(&base_key,((j*1024) + i),k,&keyfound);
							if(r)	{
								hextemp = keyfound.GetBase16();
								output_success("Thread Key found privkey %s    \n",hextemp);
								point_found = secp->ComputePublicKey(&keyfound);
								aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
								output_success("Publickey %s\n",aux_c);
platform_mutex_lock(&write_keys);

								filekey = fopen("KEYFOUNDKEYFOUND.txt","a");
								if(filekey != NULL)	{
									fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
									fclose(filekey);
								}
								free(hextemp);
								free(aux_c);
platform_mutex_unlock(&write_keys);

								bsgs_found[k] = 1;
								salir = 1;
								for(l = 0; l < bsgs_point_number && salir; l++)	{
									salir &= bsgs_found[l];
								}
								if(salir)	{
									printf("All points were found\n");
									exit(EXIT_FAILURE);
								}
							} //End if second check
						}//End if first check
						
					}// For for pts variable
					
					// Next start point (startP += (bsSize*GRP_SIZE).G)
					
					pp = startP;
					dy.ModSub(&_2GSn.y,&pp.y);

					_s.ModMulK1(&dy,&dx[i + 1]);
					_p.ModSquareK1(&_s);

					pp.x.ModNeg();
					pp.x.ModAdd(&_p);
					pp.x.ModSub(&_2GSn.x);

					pp.y.ModSub(&_2GSn.x,&pp.x);
					pp.y.ModMulK1(&_s);
					pp.y.ModSub(&_2GSn.y);
					startP = pp;
					
					j++;
					
				}	//End While
			}	//End if
		} // End for with k bsgs_point_number

		steps[thread_number].value+=2;
	}while(1);
	ends[thread_number].value = 1;
	return NULL;
}

/* bsgs_secondcheck and bsgs_thirdcheck definitions moved to search/search_bsgs.cpp */

void sleep_ms(int milliseconds)	{ // cross-platform sleep function
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


void init_generator()	{
	Point G = secp->ComputePublicKey(&stride);
	Point g;
	g.Set(G);
	Gn.resize(CPU_GRP_SIZE / 2);
	Gn[0] = g;
	g = secp->DoubleDirect(g);
	Gn[1] = g;
	for(size_t i = 2; i < CPU_GRP_SIZE / 2; i++) {
		g = secp->AddDirect(g,G);
		Gn[i] = g;
	}
	_2Gn = secp->DoubleDirect(Gn[CPU_GRP_SIZE / 2 - 1]);
}

#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_bPload(LPVOID vargp) {
#else
void *thread_bPload(void *vargp)	{
#endif

	char rawvalue[32];
	struct bPload *tt;
	uint64_t i_counter,j,nbStep,to;
	
	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Point startP;
	Int dx[CPU_GRP_SIZE / 2 + 1];
	Point pts[CPU_GRP_SIZE];
	Int dy,dyn,_s,_p;
	Point pp,pn;
	
	int i,bloom_bP_index,hLength = (CPU_GRP_SIZE / 2 - 1) ,threadid;
	tt = (struct bPload *)vargp;
	Int km((uint64_t)(tt->from + 1));
	threadid = tt->threadid;
	//if(FLAGDEBUG) printf("[D] thread %i from %" PRIu64 " to %" PRIu64 "\n",threadid,tt->from,tt->to);
	
	i_counter = tt->from;

	nbStep = (tt->to - tt->from) / CPU_GRP_SIZE;
	
	if( ((tt->to - tt->from) % CPU_GRP_SIZE )  != 0)	{
		nbStep++;
	}
	//if(FLAGDEBUG) printf("[D] thread %i nbStep %" PRIu64 "\n",threadid,nbStep);
	to = tt->to;
	
	km.Add((uint64_t)(CPU_GRP_SIZE / 2));
	startP = secp->ComputePublicKey(&km);
	grp->Set(dx);
	for(uint64_t s=0;s<nbStep;s++) {
		for(i = 0; i < hLength; i++) {
			dx[i].ModSub(&Gn[i].x,&startP.x);
		}
		dx[i].ModSub(&Gn[i].x,&startP.x); // For the first point
		dx[i + 1].ModSub(&_2Gn.x,&startP.x);// For the next center point
		// Grouped ModInv
		grp->ModInvOptimized();  // Use 8x unrolled version

		// We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
		// We compute key in the positive and negative way from the center of the group
		// center point
		
		pts[CPU_GRP_SIZE / 2] = startP;	//Center point

		for(i = 0; i<hLength; i++) {
			pp = startP;
			pn = startP;

			// P = startP + i*G
			dy.ModSub(&Gn[i].y,&pp.y);

			_s.ModMulK1(&dy,&dx[i]);        // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
			_p.ModSquareK1(&_s);            // _p = pow2(s)

			pp.x.ModNeg();
			pp.x.ModAdd(&_p);
			pp.x.ModSub(&Gn[i].x);           // rx = pow2(s) - p1.x - p2.x;

			// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
			dyn.Set(&Gn[i].y);
			dyn.ModNeg();
			dyn.ModSub(&pn.y);

			_s.ModMulK1(&dyn,&dx[i]);      // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
			_p.ModSquareK1(&_s);            // _p = pow2(s)

			pn.x.ModNeg();
			pn.x.ModAdd(&_p);
			pn.x.ModSub(&Gn[i].x);          // rx = pow2(s) - p1.x - p2.x;

			pts[CPU_GRP_SIZE / 2 + (i + 1)] = pp;
			pts[CPU_GRP_SIZE / 2 - (i + 1)] = pn;
		}

		// First point (startP - (GRP_SZIE/2)*G)
		pn = startP;
		dyn.Set(&Gn[i].y);
		dyn.ModNeg();
		dyn.ModSub(&pn.y);

		_s.ModMulK1(&dyn,&dx[i]);
		_p.ModSquareK1(&_s);

		pn.x.ModNeg();
		pn.x.ModAdd(&_p);
		pn.x.ModSub(&Gn[i].x);

		pts[0] = pn;
		for(j=0;j<CPU_GRP_SIZE;j++)	{
			pts[j].x.Get32Bytes((unsigned char*)rawvalue);
			bloom_bP_index = (uint8_t)rawvalue[0];
			/*
			if(FLAGDEBUG){
				tohex_dst(rawvalue,32,hexraw);
				printf("%i : %s : %i\n",i_counter,hexraw,bloom_bP_index);
			}
			*/
			if(i_counter < bsgs_m3)	{
				if(!FLAGREADEDFILE3)	{
					memcpy(&bPtable[i_counter].value, rawvalue+16, 8);  // Copy 8 bytes to uint64_t
					bPtable[i_counter].index = i_counter;
				}
				if(!FLAGREADEDFILE4)	{
					platform_mutex_lock(&bloom_bPx3rd_mutex[bloom_bP_index]);
					bloom_ext_add(&bloom_bPx3rd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
					platform_mutex_unlock(&bloom_bPx3rd_mutex[bloom_bP_index]);
				}
			}
			if(i_counter < bsgs_m2 && !FLAGREADEDFILE2)	{
				platform_mutex_lock(&bloom_bPx2nd_mutex[bloom_bP_index]);
bloom_ext_add(&bloom_bPx2nd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
				platform_mutex_unlock(&bloom_bPx2nd_mutex[bloom_bP_index]);	
			}
			if(i_counter < to && !FLAGREADEDFILE1 )	{
			platform_mutex_lock(&bloom_bP_mutex[bloom_bP_index]);
bloom_ext_add(&bloom_bP[bloom_bP_index], rawvalue ,BSGS_BUFFERXPOINTLENGTH);
				platform_mutex_unlock(&bloom_bP_mutex[bloom_bP_index]);
			}
			i_counter++;
		}
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
	}
	delete grp;
	platform_mutex_lock(&bPload_mutex[threadid]);
	tt->finished = 1;
	platform_mutex_unlock(&bPload_mutex[threadid]);
#ifndef _WIN64
	pthread_exit(NULL);
#endif
	return NULL;
}

#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_bPload_2blooms(LPVOID vargp) {
#else
void *thread_bPload_2blooms(void *vargp)	{
#endif
	char rawvalue[32];
	struct bPload *tt;
	uint64_t i_counter,j,nbStep; //,to;
	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Point startP;
	Int dx[CPU_GRP_SIZE / 2 + 1];
	Point pts[CPU_GRP_SIZE];
	Int dy,dyn,_s,_p;
	Point pp,pn;
	int i,bloom_bP_index,hLength = (CPU_GRP_SIZE / 2 - 1) ,threadid;
	tt = (struct bPload *)vargp;
	Int km((uint64_t)(tt->from +1 ));
	threadid = tt->threadid;
	
	i_counter = tt->from;

	nbStep = (tt->to - (tt->from)) / CPU_GRP_SIZE;
	
	if( ((tt->to - (tt->from)) % CPU_GRP_SIZE )  != 0)	{
		nbStep++;
	}
	//if(FLAGDEBUG) printf("[D] thread %i nbStep %" PRIu64 "\n",threadid,nbStep);
	//to = tt->to;
	
	km.Add((uint64_t)(CPU_GRP_SIZE / 2));
	startP = secp->ComputePublicKey(&km);
	grp->Set(dx);
	for(uint64_t s=0;s<nbStep;s++) {
		for(i = 0; i < hLength; i++) {
			dx[i].ModSub(&Gn[i].x,&startP.x);
		}
		dx[i].ModSub(&Gn[i].x,&startP.x); // For the first point
		dx[i + 1].ModSub(&_2Gn.x,&startP.x);// For the next center point
		// Grouped ModInv
		grp->ModInvOptimized();  // Use 8x unrolled version

		// We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
		// We compute key in the positive and negative way from the center of the group
		// center point
		
		pts[CPU_GRP_SIZE / 2] = startP;	//Center point

		for(i = 0; i<hLength; i++) {
			pp = startP;
			pn = startP;

			// P = startP + i*G
			dy.ModSub(&Gn[i].y,&pp.y);

			_s.ModMulK1(&dy,&dx[i]);        // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
			_p.ModSquareK1(&_s);            // _p = pow2(s)

			pp.x.ModNeg();
			pp.x.ModAdd(&_p);
			pp.x.ModSub(&Gn[i].x);           // rx = pow2(s) - p1.x - p2.x;

			// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
			dyn.Set(&Gn[i].y);
			dyn.ModNeg();
			dyn.ModSub(&pn.y);

			_s.ModMulK1(&dyn,&dx[i]);      // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
			_p.ModSquareK1(&_s);            // _p = pow2(s)

			pn.x.ModNeg();
			pn.x.ModAdd(&_p);
			pn.x.ModSub(&Gn[i].x);          // rx = pow2(s) - p1.x - p2.x;

			pts[CPU_GRP_SIZE / 2 + (i + 1)] = pp;
			pts[CPU_GRP_SIZE / 2 - (i + 1)] = pn;
		}

		// First point (startP - (GRP_SZIE/2)*G)
		pn = startP;
		dyn.Set(&Gn[i].y);
		dyn.ModNeg();
		dyn.ModSub(&pn.y);

		_s.ModMulK1(&dyn,&dx[i]);
		_p.ModSquareK1(&_s);

		pn.x.ModNeg();
		pn.x.ModAdd(&_p);
		pn.x.ModSub(&Gn[i].x);

		pts[0] = pn;
		for(j=0;j<CPU_GRP_SIZE;j++)	{
			pts[j].x.Get32Bytes((unsigned char*)rawvalue);
			bloom_bP_index = (uint8_t)rawvalue[0];
			if(i_counter < bsgs_m3)	{
				if(!FLAGREADEDFILE3)	{
					memcpy(&bPtable[i_counter].value, rawvalue+16, 8);  // Copy 8 bytes to uint64_t
					bPtable[i_counter].index = i_counter;
				}
				if(!FLAGREADEDFILE4)	{
					platform_mutex_lock(&bloom_bPx3rd_mutex[bloom_bP_index]);
					bloom_ext_add(&bloom_bPx3rd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
					platform_mutex_unlock(&bloom_bPx3rd_mutex[bloom_bP_index]);
				}
			}
			if(i_counter < bsgs_m2 && !FLAGREADEDFILE2)	{
					platform_mutex_lock(&bloom_bPx2nd_mutex[bloom_bP_index]);
bloom_ext_add(&bloom_bPx2nd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
					platform_mutex_unlock(&bloom_bPx2nd_mutex[bloom_bP_index]);			
			}
			i_counter++;
		}
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
	}
	delete grp;
	platform_mutex_lock(&bPload_mutex[threadid]);
	tt->finished = 1;
	platform_mutex_unlock(&bPload_mutex[threadid]);
#ifndef _WIN64
	pthread_exit(NULL);
#endif
	return NULL;
}

/* This function perform the KECCAK Opetation*/
/* KECCAK_256, generate_binaddress_eth moved to crypto/address_util.cpp */

#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process_bsgs_dance(LPVOID vargp) {
#else
void *thread_process_bsgs_dance(void *vargp)	{
#endif

	Point pts[CPU_GRP_SIZE];
	Int dx[CPU_GRP_SIZE / 2 + 1];
	Point pp,pn,startP,base_point,point_aux,point_found,offset_point;
	FILE *filekey;
	struct tothread *tt;
	uint8_t xpoint_raw[16];
	char *aux_c,*hextemp;
	Int base_key,keyfound,dy,dyn,_s,_p,intaux;
	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	uint32_t k,l,r,salir,thread_number,entrar,cycles;
	int hLength = (CPU_GRP_SIZE / 2 - 1);	

	grp->Set(dx);
	
	tt = (struct tothread *)vargp;
	thread_number = tt->nt;
	free(tt);
	profile_set_thread((int)thread_number);
	
	cycles = bsgs_aux / 1024;
	if(bsgs_aux % 1024 != 0)	{
		cycles++;
	}
	
	intaux.Set(&BSGS_M_double);
	intaux.Mult(CPU_GRP_SIZE/2);
	intaux.Add(&BSGS_M);
	offset_point = secp->ComputePublicKey(&intaux);
	
	entrar = 1;
	
	
	/*
		while base_key is less than n_range_end then:
	*/
	do	{
		r = thread_rand_n(3);
platform_mutex_lock(&bsgs_thread);
	switch(r)	{
		case 0:	//TOP
			if(n_range_end.IsGreater(&BSGS_CURRENT))	{
				/*
					n_range_end.Sub(&BSGS_N);
					n_range_end.Sub(&BSGS_N);
				*/
					n_range_end.Sub(&BSGS_N_double);
					if(n_range_end.IsLower(&BSGS_CURRENT))	{
						base_key.Set(&BSGS_CURRENT);
					}
					else	{
						base_key.Set(&n_range_end);
					}
			}
			else	{
				entrar = 0;
			}
		break;
		case 1: //BOTTOM
			if(BSGS_CURRENT.IsLower(&n_range_end))	{
				base_key.Set(&BSGS_CURRENT);
				//BSGS_N_double
				BSGS_CURRENT.Add(&BSGS_N_double);
				/*
				BSGS_CURRENT.Add(&BSGS_N);
				BSGS_CURRENT.Add(&BSGS_N);
				*/
			}
			else	{
				entrar = 0;
			}
		break;
		case 2: //random - middle
			base_key.Rand(&BSGS_CURRENT,&n_range_end);
		break;
	}
platform_mutex_unlock(&bsgs_thread);

		if(entrar == 0)
			break;
			
		if(FLAGMATRIX)	{
			aux_c = base_key.GetBase16();
			output_success("Thread 0x%s \n",aux_c);
			fflush(stdout);
			free(aux_c);
		}
		else	{
			if(FLAGQUIET == 0){
				aux_c = base_key.GetBase16();
				printf("\r[+] Thread 0x%s   \r",aux_c);
				fflush(stdout);
				free(aux_c);
				THREADOUTPUT = 1;
			}
		}
		
		base_point = secp->ComputePublicKey(&base_key);
		point_aux = secp->AddDirect(base_point, offset_point);
		point_aux = secp->Negation(point_aux);
		
		for(k = 0; k < bsgs_point_number ; k++)	{
			if(bsgs_found[k] == 0)	{
				startP  = secp->AddDirect(OriginalPointsBSGS[k],point_aux);
				uint32_t j = 0;
				while( j < cycles && bsgs_found[k]== 0 )	{
				
					int i;
					
					for(i = 0; i < hLength; i++) {
						dx[i].ModSub(&GSn[i].x,&startP.x);
					}
					dx[i].ModSub(&GSn[i].x,&startP.x);  // For the first point
					dx[i+1].ModSub(&_2GSn.x,&startP.x); // For the next center point

					// Grouped ModInv
					grp->ModInvOptimized();  // Use 8x unrolled version
					
					/*
					We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
					We compute key in the positive and negative way from the center of the group
					*/

					// center point
					pts[CPU_GRP_SIZE / 2] = startP;
					
					for(i = 0; i<hLength; i++) {

						pp = startP;
						pn = startP;

						// P = startP + i*G
						dy.ModSub(&GSn[i].y,&pp.y);

						_s.ModMulK1(&dy,&dx[i]);        // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
						_p.ModSquareK1(&_s);            // _p = pow2(s)

						pp.x.ModNeg();
						pp.x.ModAdd(&_p);
						pp.x.ModSub(&GSn[i].x);           // rx = pow2(s) - p1.x - p2.x;

						// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
						dyn.Set(&GSn[i].y);
						dyn.ModNeg();
						dyn.ModSub(&pn.y);

						_s.ModMulK1(&dyn,&dx[i]);       // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
						_p.ModSquareK1(&_s);            // _p = pow2(s)

						pn.x.ModNeg();
						pn.x.ModAdd(&_p);
						pn.x.ModSub(&GSn[i].x);          // rx = pow2(s) - p1.x - p2.x;

						pts[CPU_GRP_SIZE / 2 + (i + 1)] = pp;
						pts[CPU_GRP_SIZE / 2 - (i + 1)] = pn;

					}

					// First point (startP - (GRP_SZIE/2)*G)
					pn = startP;
					dyn.Set(&GSn[i].y);
					dyn.ModNeg();
					dyn.ModSub(&pn.y);

					_s.ModMulK1(&dyn,&dx[i]);
					_p.ModSquareK1(&_s);

					pn.x.ModNeg();
					pn.x.ModAdd(&_p);
					pn.x.ModSub(&GSn[i].x);

					pts[0] = pn;

					for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k]== 0; i++) {
						pts[i].x.GetHi16Bytes(xpoint_raw);
						r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
						if(r) {
							r = bsgs_secondcheck(&base_key,((j*1024) + i),k,&keyfound);
							if(r)	{
								hextemp = keyfound.GetBase16();
								output_success("Thread Key found privkey %s   \n",hextemp);
								point_found = secp->ComputePublicKey(&keyfound);
								aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
								output_success("Publickey %s\n",aux_c);
platform_mutex_lock(&write_keys);

								filekey = fopen("KEYFOUNDKEYFOUND.txt","a");
								if(filekey != NULL)	{
									fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
									fclose(filekey);
								}
								free(hextemp);
								free(aux_c);
platform_mutex_unlock(&write_keys);

								bsgs_found[k] = 1;
								salir = 1;
								for(l = 0; l < bsgs_point_number && salir; l++)	{
									salir &= bsgs_found[l];
								}
								if(salir)	{
									printf("All points were found\n");
									exit(EXIT_FAILURE);
								}
							} //End if second check
						}//End if first check
						
					}// For for pts variable
					
					// Next start point (startP += (bsSize*GRP_SIZE).G)
					
					pp = startP;
					dy.ModSub(&_2GSn.y,&pp.y);

					_s.ModMulK1(&dy,&dx[i + 1]);
					_p.ModSquareK1(&_s);

					pp.x.ModNeg();
					pp.x.ModAdd(&_p);
					pp.x.ModSub(&_2GSn.x);

					pp.y.ModSub(&_2GSn.x,&pp.x);
					pp.y.ModMulK1(&_s);
					pp.y.ModSub(&_2GSn.y);
					startP = pp;
					
					j++;
				}//while all the aMP points
			}// End if 
		}
		steps[thread_number].value+=2;
	}while(1);
	ends[thread_number].value = 1;
	return NULL;
}

#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process_bsgs_backward(LPVOID vargp) {
#else
void *thread_process_bsgs_backward(void *vargp)	{
#endif
	FILE *filekey;
	struct tothread *tt;
	uint8_t xpoint_raw[16];
	char *aux_c,*hextemp;
	Int base_key,keyfound;
	Point base_point,point_aux,point_found,offset_point;
	uint32_t k,l,r,salir,thread_number,entrar,cycles;
	
	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Point startP;
	
	int hLength = (CPU_GRP_SIZE / 2 - 1);
	
	Int dx[CPU_GRP_SIZE / 2 + 1];
	Point pts[CPU_GRP_SIZE];

	Int dy;
	Int dyn;
	Int _s;
	Int _p;
	Int intaux;
	Point pp;
	Point pn;
	grp->Set(dx);

	tt = (struct tothread *)vargp;
	thread_number = tt->nt;
	free(tt);
	profile_set_thread((int)thread_number);

	cycles = bsgs_aux / 1024;
	if(bsgs_aux % 1024 != 0)	{
		cycles++;
	}
	
	intaux.Set(&BSGS_M_double);
	intaux.Mult(CPU_GRP_SIZE/2);
	intaux.Add(&BSGS_M);
	offset_point = secp->ComputePublicKey(&intaux);
	
	entrar = 1;
	/*
		while base_key is less than n_range_end then:
	*/
	do	{
		
platform_mutex_lock(&bsgs_thread);
		if(n_range_end.IsGreater(&n_range_start))	{
			n_range_end.Sub(&BSGS_N_double);
			if(n_range_end.IsLower(&n_range_start))	{
				base_key.Set(&n_range_start);
			}
			else	{
				base_key.Set(&n_range_end);
			}
		}
		else	{
			entrar = 0;
		}
platform_mutex_unlock(&bsgs_thread);
		if(entrar == 0)
			break;
		
		if(FLAGMATRIX)	{
			aux_c = base_key.GetBase16();
			output_success("Thread 0x%s \n",aux_c);
			fflush(stdout);
			free(aux_c);
		}
		else	{
			if(FLAGQUIET == 0){
				aux_c = base_key.GetBase16();
				printf("\r[+] Thread 0x%s   \r",aux_c);
				fflush(stdout);
				free(aux_c);
				THREADOUTPUT = 1;
			}
		}
		
		base_point = secp->ComputePublicKey(&base_key);
		point_aux = secp->AddDirect(base_point, offset_point);
		point_aux = secp->Negation(point_aux);
		
		for(k = 0; k < bsgs_point_number ; k++)	{
			if(bsgs_found[k] == 0)	{
				startP  = secp->AddDirect(OriginalPointsBSGS[k],point_aux);
				uint32_t j = 0;
				while( j < cycles && bsgs_found[k]== 0 )	{
					int i;
					for(i = 0; i < hLength; i++) {
						dx[i].ModSub(&GSn[i].x,&startP.x);
					}
					dx[i].ModSub(&GSn[i].x,&startP.x);  // For the first point
					dx[i+1].ModSub(&_2GSn.x,&startP.x); // For the next center point

					// Grouped ModInv
					grp->ModInvOptimized();  // Use 8x unrolled version
					
					/*
					We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
					We compute key in the positive and negative way from the center of the group
					*/

					// center point
					pts[CPU_GRP_SIZE / 2] = startP;
					
					for(i = 0; i<hLength; i++) {

						pp = startP;
						pn = startP;

						// P = startP + i*G
						dy.ModSub(&GSn[i].y,&pp.y);

						_s.ModMulK1(&dy,&dx[i]);        // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
						_p.ModSquareK1(&_s);            // _p = pow2(s)

						pp.x.ModNeg();
						pp.x.ModAdd(&_p);
						pp.x.ModSub(&GSn[i].x);           // rx = pow2(s) - p1.x - p2.x;

						// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
						dyn.Set(&GSn[i].y);
						dyn.ModNeg();
						dyn.ModSub(&pn.y);

						_s.ModMulK1(&dyn,&dx[i]);       // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
						_p.ModSquareK1(&_s);            // _p = pow2(s)

						pn.x.ModNeg();
						pn.x.ModAdd(&_p);
						pn.x.ModSub(&GSn[i].x);          // rx = pow2(s) - p1.x - p2.x;

						pts[CPU_GRP_SIZE / 2 + (i + 1)] = pp;
						pts[CPU_GRP_SIZE / 2 - (i + 1)] = pn;

					}

					// First point (startP - (GRP_SZIE/2)*G)
					pn = startP;
					dyn.Set(&GSn[i].y);
					dyn.ModNeg();
					dyn.ModSub(&pn.y);

					_s.ModMulK1(&dyn,&dx[i]);
					_p.ModSquareK1(&_s);

					pn.x.ModNeg();
					pn.x.ModAdd(&_p);
					pn.x.ModSub(&GSn[i].x);

					pts[0] = pn;

					for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k]== 0; i++) {
						pts[i].x.GetHi16Bytes(xpoint_raw);
						r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
						if(r) {
							r = bsgs_secondcheck(&base_key,((j*1024) + i),k,&keyfound);
							if(r)	{
								hextemp = keyfound.GetBase16();
								output_success("Thread Key found privkey %s   \n",hextemp);
								point_found = secp->ComputePublicKey(&keyfound);
								aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
								output_success("Publickey %s\n",aux_c);
platform_mutex_lock(&write_keys);

								filekey = fopen("KEYFOUNDKEYFOUND.txt","a");
								if(filekey != NULL)	{
									fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
									fclose(filekey);
								}
								free(hextemp);
								free(aux_c);
platform_mutex_unlock(&write_keys);

								bsgs_found[k] = 1;
								salir = 1;
								for(l = 0; l < bsgs_point_number && salir; l++)	{
									salir &= bsgs_found[l];
								}
								if(salir)	{
									printf("All points were found\n");
									exit(EXIT_FAILURE);
								}
							} //End if second check
						}//End if first check
						
					}// For for pts variable
					
					// Next start point (startP += (bsSize*GRP_SIZE).G)
					
					pp = startP;
					dy.ModSub(&_2GSn.y,&pp.y);

					_s.ModMulK1(&dy,&dx[i + 1]);
					_p.ModSquareK1(&_s);

					pp.x.ModNeg();
					pp.x.ModAdd(&_p);
					pp.x.ModSub(&_2GSn.x);

					pp.y.ModSub(&_2GSn.x,&pp.x);
					pp.y.ModMulK1(&_s);
					pp.y.ModSub(&_2GSn.y);
					startP = pp;
					j++;
				}//while all the aMP points
			}// End if 
		}
		steps[thread_number].value+=2;
	}while(1);
	ends[thread_number].value = 1;
	return NULL;
}

#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process_bsgs_both(LPVOID vargp) {
#else
void *thread_process_bsgs_both(void *vargp)	{
#endif
	FILE *filekey;
	struct tothread *tt;
	uint8_t xpoint_raw[16];
	char *aux_c,*hextemp;
	Int base_key,keyfound;
	Point base_point,point_aux,point_found,offset_point;
	uint32_t k,l,r,salir,thread_number,entrar,cycles;
	
	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Point startP;
	
	int hLength = (CPU_GRP_SIZE / 2 - 1);
	
	Int dx[CPU_GRP_SIZE / 2 + 1];
	Point pts[CPU_GRP_SIZE];

	Int dy;
	Int dyn;
	Int _s;
	Int _p;
	Int intaux;
	Point pp;
	Point pn;
	grp->Set(dx);

	
	tt = (struct tothread *)vargp;
	thread_number = tt->nt;
	free(tt);
	profile_set_thread((int)thread_number);
	
	cycles = bsgs_aux / 1024;
	if(bsgs_aux % 1024 != 0)	{
		cycles++;
	}
	intaux.Set(&BSGS_M_double);
	intaux.Mult(CPU_GRP_SIZE/2);
	intaux.Add(&BSGS_M);
	offset_point = secp->ComputePublicKey(&intaux);
	
	entrar = 1;
	
	
	/*
		while BSGS_CURRENT is less than n_range_end 
	*/
	do	{

		r = thread_rand_n(2);
platform_mutex_lock(&bsgs_thread);
		switch(r)	{
			case 0:	//TOP
				if(n_range_end.IsGreater(&BSGS_CURRENT))	{
						n_range_end.Sub(&BSGS_N_double);
						/*
						n_range_end.Sub(&BSGS_N);
						n_range_end.Sub(&BSGS_N);
						*/
						if(n_range_end.IsLower(&BSGS_CURRENT))	{
							base_key.Set(&BSGS_CURRENT);
						}
						else	{
							base_key.Set(&n_range_end);
						}
				}
				else	{
					entrar = 0;
				}
			break;
			case 1: //BOTTOM
				if(BSGS_CURRENT.IsLower(&n_range_end))	{
					base_key.Set(&BSGS_CURRENT);
					//BSGS_N_double
					BSGS_CURRENT.Add(&BSGS_N_double);
					/*
					BSGS_CURRENT.Add(&BSGS_N);
					BSGS_CURRENT.Add(&BSGS_N);
					*/
				}
				else	{
					entrar = 0;
				}
			break;
		}
platform_mutex_unlock(&bsgs_thread);

		if(entrar == 0)
			break;

		
		if(FLAGMATRIX)	{
			aux_c = base_key.GetBase16();
			output_success("Thread 0x%s \n",aux_c);
			fflush(stdout);
			free(aux_c);
		}
		else	{
			if(FLAGQUIET == 0){
				aux_c = base_key.GetBase16();
				printf("\r[+] Thread 0x%s   \r",aux_c);
				fflush(stdout);
				free(aux_c);
				THREADOUTPUT = 1;
			}
		}
		
		base_point = secp->ComputePublicKey(&base_key);
		point_aux = secp->AddDirect(base_point, offset_point);
		point_aux = secp->Negation(point_aux);
		
		for(k = 0; k < bsgs_point_number ; k++)	{
			if(bsgs_found[k] == 0)	{
					startP  = secp->AddDirect(OriginalPointsBSGS[k],point_aux);
					uint32_t j = 0;
					while( j < cycles && bsgs_found[k]== 0 )	{
						int i;
						for(i = 0; i < hLength; i++) {
							dx[i].ModSub(&GSn[i].x,&startP.x);
						}
						dx[i].ModSub(&GSn[i].x,&startP.x);  // For the first point
						dx[i+1].ModSub(&_2GSn.x,&startP.x); // For the next center point

						// Grouped ModInv
						grp->ModInvOptimized();  // Use 8x unrolled version
						
						/*
						We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
						We compute key in the positive and negative way from the center of the group
						*/

						// center point
						pts[CPU_GRP_SIZE / 2] = startP;
						
						for(i = 0; i<hLength; i++) {

							pp = startP;
							pn = startP;

							// P = startP + i*G
							dy.ModSub(&GSn[i].y,&pp.y);

							_s.ModMulK1(&dy,&dx[i]);        // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
							_p.ModSquareK1(&_s);            // _p = pow2(s)

							pp.x.ModNeg();
							pp.x.ModAdd(&_p);
							pp.x.ModSub(&GSn[i].x);           // rx = pow2(s) - p1.x - p2.x;

							// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
							dyn.Set(&GSn[i].y);
							dyn.ModNeg();
							dyn.ModSub(&pn.y);

							_s.ModMulK1(&dyn,&dx[i]);       // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
							_p.ModSquareK1(&_s);            // _p = pow2(s)

							pn.x.ModNeg();
							pn.x.ModAdd(&_p);
							pn.x.ModSub(&GSn[i].x);          // rx = pow2(s) - p1.x - p2.x;

							pts[CPU_GRP_SIZE / 2 + (i + 1)] = pp;
							pts[CPU_GRP_SIZE / 2 - (i + 1)] = pn;

						}

						// First point (startP - (GRP_SZIE/2)*G)
						pn = startP;
						dyn.Set(&GSn[i].y);
						dyn.ModNeg();
						dyn.ModSub(&pn.y);

						_s.ModMulK1(&dyn,&dx[i]);
						_p.ModSquareK1(&_s);

						pn.x.ModNeg();
						pn.x.ModAdd(&_p);
						pn.x.ModSub(&GSn[i].x);

						pts[0] = pn;

						for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k]== 0; i++) {
							pts[i].x.GetHi16Bytes(xpoint_raw);
							r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
							if(r) {
								r = bsgs_secondcheck(&base_key,((j*1024) + i),k,&keyfound);
								if(r)	{
									hextemp = keyfound.GetBase16();
									output_success("Thread Key found privkey %s   \n",hextemp);
									point_found = secp->ComputePublicKey(&keyfound);
									aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
									output_success("Publickey %s\n",aux_c);
platform_mutex_lock(&write_keys);

									filekey = fopen("KEYFOUNDKEYFOUND.txt","a");
									if(filekey != NULL)	{
										fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
										fclose(filekey);
									}
									free(hextemp);
									free(aux_c);
platform_mutex_unlock(&write_keys);

									bsgs_found[k] = 1;
									salir = 1;
									for(l = 0; l < bsgs_point_number && salir; l++)	{
										salir &= bsgs_found[l];
									}
									if(salir)	{
										printf("All points were found\n");
										exit(EXIT_FAILURE);
									}
								} //End if second check
							}//End if first check
							
						}// For for pts variable
						
						// Next start point (startP += (bsSize*GRP_SIZE).G)
						
						pp = startP;
						dy.ModSub(&_2GSn.y,&pp.y);

						_s.ModMulK1(&dy,&dx[i + 1]);
						_p.ModSquareK1(&_s);

						pp.x.ModNeg();
						pp.x.ModAdd(&_p);
						pp.x.ModSub(&_2GSn.x);

						pp.y.ModSub(&_2GSn.x,&pp.x);
						pp.y.ModMulK1(&_s);
						pp.y.ModSub(&_2GSn.y);
						startP = pp;
						
						j++;
					}//while all the aMP points
			}// End if 
		}
			steps[thread_number].value+=2;	
	}while(1);
	ends[thread_number].value = 1;
	return NULL;
}


/* set_minikey, increment_minikey_index, increment_minikey_N
 * moved to search/search_minikeys.cpp */

/* BUFFMINIKEY, sha256sse_22, BUFFMINIKEYCHECK, sha256sse_23
 * moved to crypto/address_util.cpp */

void menu() {
	printf("\n");
	printf("keyhunt - High-performance cryptocurrency private key search tool\n");
	printf("\n");
	printf("USAGE:\n");
	printf("  keyhunt -m <mode> -f <file> [options]\n");
	printf("\n");
	printf("MODES (-m):\n");
	printf("  address     Search for Bitcoin addresses using bloom filters (default)\n");
	printf("  rmd160      Search for RIPEMD160 hashes directly\n");
	printf("  xpoint      Search for public key X-coordinates (fastest for known pubkeys)\n");
	printf("  bsgs        Baby Step Giant Step algorithm for known public keys\n");
	printf("  vanity      Generate vanity addresses with specific prefixes\n");
	printf("\n");
	printf("REQUIRED OPTIONS:\n");
	printf("  -f <file>   Input file with addresses, xpoints, or public keys\n");
	printf("  -m <mode>   Search mode (see MODES above)\n");
	printf("\n");
	printf("COMMON OPTIONS:\n");
	printf("  -h, --help  Show this help message\n");
	printf("  -t <num>    Number of threads (default: auto-detect CPU cores)\n");
	printf("  -b <bits>   Bit range for puzzle solving (e.g., 66 for puzzle #66)\n");
	printf("  -r <range>  Search range as START:END in hex (e.g., 1:FFFFFFFF)\n");
	printf("  -R          Random search mode (default behavior)\n");
	printf("  -q          Quiet mode - suppress thread output\n");
	printf("  -s <secs>   Stats output interval in seconds (0 to disable)\n");
	printf("  -l <type>   Address type: compress, uncompress, both\n");
	printf("  -c <crypto> Cryptocurrency: btc, eth (only with -m address)\n");
	printf("  -e          Enable endomorphism (6x speed for full curve search)\n");
	printf("  -I <stride> Stride value for sequential search\n");
	printf("  -P          Show segmented range progress indicator\n");
	printf("  -M          Matrix display mode (slower but cool looking)\n");
	printf("\n");
	printf("BSGS OPTIONS:\n");
	printf("  -n <value>  N value - larger N uses more RAM but faster search\n");
	printf("  -k <value>  K factor multiplier for M (more RAM, more speed)\n");
	printf("  -B <mode>   BSGS search pattern: sequential, backward, both, random, dance\n");
	printf("  -S          Save/load BSGS data (bloom filters and bP tables)\n");
	printf("  -6          Skip SHA256 checksum verification on data files\n");
	printf("\n");
	printf("VANITY OPTIONS:\n");
	printf("  -v <prefix> Vanity address prefix to search for\n");
	printf("\n");
	printf("MINIKEY OPTIONS:\n");
	printf("  -C <base>   Set 22-character minikey base (e.g., SRPqx8QiwnW4WNWnTVa2W5)\n");
	printf("  -8 <alpha>  Set custom Base58 alphabet for minikeys\n");
	printf("\n");
	printf("GPU OPTIONS:\n");
	printf("  -G <mode>   GPU mode: auto, on, off (default: off)\n");
	printf("\n");
	printf("ADVANCED OPTIONS:\n");
	printf("  -z <mult>   Bloom filter size multiplier (>= 1)\n");
	printf("  -W, --wizard          Interactive setup wizard\n");
	printf("  --wizard-client <hp>  Non-interactive client mode (host:port)\n");
	printf("  --config <file>       Load configuration from file\n");
	printf("  --save-config <file>  Save current configuration to file\n");
	printf("\n");
	printf("QUICK START EXAMPLES:\n");
	printf("\n");
	printf("  # Search for Bitcoin addresses in a 32-bit range:\n");
	printf("  ./keyhunt -m address -f targets.txt -r 1:FFFFFFFF\n");
	printf("\n");
	printf("  # Solve puzzle #66 with RIPEMD160 hashes:\n");
	printf("  ./keyhunt -m rmd160 -f puzzle66.rmd -b 66 -l compress -R -q -t 8\n");
	printf("\n");
	printf("  # BSGS mode for known public key:\n");
	printf("  ./keyhunt -m bsgs -f pubkey.txt -b 125 -q -S -R\n");
	printf("\n");
	printf("  # Search for vanity address starting with '1ABC':\n");
	printf("  ./keyhunt -m vanity -v 1ABC -t 4\n");
	printf("\n");
	printf("For more information, see: https://github.com/albertobsd/keyhunt\n");
	printf("\n");
	printf("Developed by AlbertoBSD\n");
	printf("Tips BTC: 1Coffee1jV4gB5gaXfHgSHDz9xx9QSECVW\n");
	printf("Thanks to Iceland for ideas and contributions.\n");
	printf("Tips to Iceland: bc1q39meky2mn5qjq704zz0nnkl0v7kj4uz6r529at\n");
	printf("\n");
	exit(EXIT_SUCCESS);
}

bool vanityrmdmatch(unsigned char *rmdhash)	{
	bool r = false;
	int i,j,cmpA,cmpB,result;
	result = bloom_check(vanity_bloom,rmdhash,vanity_rmd_minimun_bytes_check_length);
	switch(result)	{
		case -1:
			output_error("Bloom is not initialized\n");
			exit(EXIT_FAILURE);
		break;
		case 1:
			for(i = 0; i < vanity_rmd_targets && !r;i++)	{
				for(j = 0; j < vanity_rmd_limits[i] && !r; j++)	{
					cmpA = memcmp(vanity_rmd_limit_values_A[i][j],rmdhash,20);
					cmpB = memcmp(vanity_rmd_limit_values_B[i][j],rmdhash,20);
					if(cmpA <= 0 && cmpB >= 0)	{
						//if(FLAGDEBUG ) printf("\n\n[D] cmpA = %i, cmpB = %i \n\n",cmpA,cmpB);
						r = true;
					}
				}
			}
		break;
		default:
			r = false;
		break;
	}
	return r;
}

void writevanitykey(bool compressed,Int *key)	{
	Point publickey;
	FILE *keys;
	char *hextemp,*hexrmd,public_key_hex[131],address[50],rmdhash[20];
	hextemp = key->GetBase16();
	publickey = secp->ComputePublicKey(key);
	secp->GetPublicKeyHex(compressed,publickey,public_key_hex);
	
	secp->GetHash160(P2PKH,compressed,publickey,(uint8_t*)rmdhash);
	hexrmd = tohex(rmdhash,20);
	rmd160toaddress_dst(rmdhash,address);
	
platform_mutex_lock(&write_keys);
	keys = fopen("VANITYKEYFOUND.txt","a+");
	if(keys != NULL)	{
		fprintf(keys,"Vanity Private Key: %s\npubkey: %s\nAddress %s\nrmd160 %s\n",hextemp,public_key_hex,address,hexrmd);
		fclose(keys);
	}
	printf("\nVanity Private Key: %s\npubkey: %s\nAddress %s\nrmd160 %s\n",hextemp,public_key_hex,address,hexrmd);
	
platform_mutex_unlock(&write_keys);
	free(hextemp);
	free(hexrmd);
}


int addvanity(char *target)	{
	unsigned char raw_value_A[50],raw_value_B[50];
	char target_copy[50];
	int stringsize,targetsize,j,r = 0;
	size_t raw_value_length;
	int values_A_size = 0,values_B_size = 0,minimun_bytes;
	raw_value_length = 50;
	targetsize = strlen(target);
	stringsize = targetsize;
	memset(raw_value_A,0,50);
	memset(target_copy,0,50);
	if(targetsize >= 30 )	{
		return 0;
	}
	memcpy(target_copy,target,targetsize);
	j = 0;
	vanity_address_targets = (char**)  realloc(vanity_address_targets,(vanity_rmd_targets+1) * sizeof(char*));
	vanity_address_targets[vanity_rmd_targets] = NULL;
	checkpointer((void *)vanity_address_targets,__FILE__,"realloc","vanity_address_targets" ,__LINE__ -1 );
	vanity_rmd_limits = (int*) realloc(vanity_rmd_limits,(vanity_rmd_targets+1) * sizeof(int));
	vanity_rmd_limits[vanity_rmd_targets] = 0;
	checkpointer((void *)vanity_rmd_limits,__FILE__,"realloc","vanity_rmd_limits" ,__LINE__ -1 );
	vanity_rmd_limit_values_A = (uint8_t***)realloc(vanity_rmd_limit_values_A,(vanity_rmd_targets+1) * sizeof(unsigned char *));
	checkpointer((void *)vanity_rmd_limit_values_A,__FILE__,"realloc","vanity_rmd_limit_values_A" ,__LINE__ -1 );
	vanity_rmd_limit_values_A[vanity_rmd_targets] = NULL;
	vanity_rmd_limit_values_B = (uint8_t***)realloc(vanity_rmd_limit_values_B,(vanity_rmd_targets+1) * sizeof(unsigned char *));
	checkpointer((void *)vanity_rmd_limit_values_B,__FILE__,"realloc","vanity_rmd_limit_values_B" ,__LINE__ -1 );
	vanity_rmd_limit_values_B[vanity_rmd_targets] = NULL;
	do	{
		raw_value_length = 50;
		b58tobin(raw_value_A,&raw_value_length,target_copy,stringsize);
		if(raw_value_length < 25)	{
			target_copy[stringsize] = '1';
			stringsize++;
		}
		if(raw_value_length == 25)	{
			b58tobin(raw_value_A,&raw_value_length,target_copy,stringsize);
			
			vanity_rmd_limit_values_A[vanity_rmd_targets] = (uint8_t**)realloc(vanity_rmd_limit_values_A[vanity_rmd_targets],(j+1) * sizeof(unsigned char *));
			checkpointer((void *)vanity_rmd_limit_values_A[vanity_rmd_targets],__FILE__,"realloc","vanity_rmd_limit_values_A" ,__LINE__ -1 );
			vanity_rmd_limit_values_A[vanity_rmd_targets][j] = (uint8_t*)calloc(20,1);
			checkpointer((void *)vanity_rmd_limit_values_A[vanity_rmd_targets][j],__FILE__,"realloc","vanity_rmd_limit_values_A" ,__LINE__ -1 );
			
			memcpy(vanity_rmd_limit_values_A[vanity_rmd_targets][j] ,raw_value_A +1,20);
			
			j++;	
			values_A_size = j;
			target_copy[stringsize] = '1';
			stringsize++;
		}	
	}while(raw_value_length <= 25);
	
	stringsize = targetsize;
	memset(raw_value_B,0,50);
	memset(target_copy,0,50);
	memcpy(target_copy,target,targetsize);

	j = 0;
	do	{
		raw_value_length = 50;
		b58tobin(raw_value_B,&raw_value_length,target_copy,stringsize);
		if(raw_value_length < 25)	{
			target_copy[stringsize] = 'z';
			stringsize++;
		}
		if(raw_value_length == 25)	{
			
			b58tobin(raw_value_B,&raw_value_length,target_copy,stringsize);
			vanity_rmd_limit_values_B[vanity_rmd_targets] = (uint8_t**)realloc(vanity_rmd_limit_values_B[vanity_rmd_targets],(j+1) * sizeof(unsigned char *));
			checkpointer((void *)vanity_rmd_limit_values_B[vanity_rmd_targets],__FILE__,"realloc","vanity_rmd_limit_values_B" ,__LINE__ -1 );
			checkpointer((void *)vanity_rmd_limit_values_B[vanity_rmd_targets],__FILE__,"realloc","vanity_rmd_limit_values_B" ,__LINE__ -1 );
			vanity_rmd_limit_values_B[vanity_rmd_targets][j] = (uint8_t*)calloc(20,1);
			checkpointer((void *)vanity_rmd_limit_values_B[vanity_rmd_targets][j],__FILE__,"calloc","vanity_rmd_limit_values_B" ,__LINE__ -1 );
			memcpy(vanity_rmd_limit_values_B[vanity_rmd_targets][j],raw_value_B+1,20);
			
			j++;				
			values_B_size = j;
			
			target_copy[stringsize] = 'z';
			stringsize++;
		}
	}while(raw_value_length <= 25);
	
	if(values_A_size >= 1 && values_B_size >= 1)	{
		if(values_A_size != values_B_size)	{
			if(values_A_size > values_B_size)
				r = values_B_size;
			else
				r = values_A_size;
		}
		else	{
			r = values_A_size;
		}
		for(j = 0; j < r; j++)	{
			minimun_bytes =  minimum_same_bytes(vanity_rmd_limit_values_A[vanity_rmd_targets][j],vanity_rmd_limit_values_B[vanity_rmd_targets][j],20);
			if(minimun_bytes < vanity_rmd_minimun_bytes_check_length)	{
				vanity_rmd_minimun_bytes_check_length = minimun_bytes;
			}
		}
		vanity_address_targets[vanity_rmd_targets] = (char*) calloc(targetsize+1,sizeof(char));
		checkpointer((void *)vanity_address_targets[vanity_rmd_targets],__FILE__,"calloc","vanity_address_targets" ,__LINE__ -1 );
		memcpy(vanity_address_targets[vanity_rmd_targets],target,targetsize+1);	// +1 to copy the null character
		vanity_rmd_limits[vanity_rmd_targets] = r;
		vanity_rmd_total+=r;
		vanity_rmd_targets++;
	}
	else	{
		for(j = 0; j < values_A_size;j++)	{
			free(vanity_rmd_limit_values_A[vanity_rmd_targets][j]);
		}
		free(vanity_rmd_limit_values_A[vanity_rmd_targets]);
		vanity_rmd_limit_values_A[vanity_rmd_targets] = NULL;
		
		for(j = 0; j < values_B_size;j++)	{
			free(vanity_rmd_limit_values_B[vanity_rmd_targets][j]);
		}
		free(vanity_rmd_limit_values_B[vanity_rmd_targets]);
		vanity_rmd_limit_values_B[vanity_rmd_targets] = NULL;
		r = 0;
	}
	return r;
}


/*
A and B are binary o string data pointers
length the max lenght to check.

Caller must by sure that the pointer are valid and have at least length bytes readebles witout causing overflow
*/
int minimum_same_bytes(unsigned char* A,unsigned char* B, int length) {
    int minBytes = 0; // Assume initially that all bytes are the same
	if(A == NULL || B  == NULL)	{	// In case of some NULL pointer
		return 0;
	}
    for (int i = 0; i < length; i++) {
        if (A[i] != B[i]) {
            break; // Exit the loop since we found a mismatch
        }
        minBytes++; // Update the minimum number of bytes where data is the same
    }

    return minBytes;
}

// ============================================================================
// GPU Full Search Helper Functions
// ============================================================================

// Upload precomputed G table to GPU (256*32 points)
static int gpu_upload_gtable_from_secp() {
	if (!gpu_backend_available()) return 1;

	// GTable has 256*32 = 8192 points
	// Each point needs X and Y (64 bytes total, big-endian)
	const size_t GTABLE_POINTS = 256 * 32;
	uint8_t *gtable_data = (uint8_t*)malloc(GTABLE_POINTS * 64);
	if (!gtable_data) return 1;

	// Export the precomputed table directly (avoids 8192 scalar computations).
	extern Secp256K1 *secp;
	secp->ExportGTable(gtable_data);

	int result = gpu_upload_gtable(gtable_data, GTABLE_POINTS);
	free(gtable_data);
	return result;
}

// Upload targets from addressTable to GPU
static int gpu_upload_targets_from_addressTable(int64_t count) {
	if (!gpu_backend_available() || count <= 0) return 1;

	// addressTable is struct address_value* with 20-byte values
	extern struct address_value *addressTable;

	// Targets are already stored as contiguous 20-byte entries, upload directly.
	return gpu_upload_targets((const uint8_t*)addressTable, (size_t)count);
}

// Build a GPU-side bloom filter that matches the CUDA bloom_check() logic and upload it.
// This reduces expensive target searches when target_count is large.
static int gpu_build_and_upload_bloom_from_addressTable(int64_t count) {
	if (!gpu_backend_available() || count <= 32) return 1;  // Not beneficial for very small N

	extern struct address_value *addressTable;

	// 4 hashes are unrolled in the CUDA bloom_check and are the fastest choice.
	const int num_hashes = 4;
	const uint32_t GOLDEN = 0x9E3779B9u;

	// Target bits-per-element tuned for low false-positive rate without excessive VRAM.
	const uint64_t bits_per_element = 12;
	uint64_t desired_bits = (uint64_t)count * bits_per_element;

	// Minimum size to keep indexing efficient and word-aligned.
	if (desired_bits < (1ULL << 16)) desired_bits = (1ULL << 16);  // 64K bits = 8 KB

	// Round up to power-of-two bits (allows fast masking on GPU).
	auto next_pow2_u64 = [](uint64_t v) -> uint64_t {
		if (v <= 1) return 1;
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v |= v >> 32;
		return v + 1;
	};

	uint64_t bloom_bits = next_pow2_u64(desired_bits);
	// CUDA bloom_check uses 32-bit indices; cap to 2^32 bits (512 MB) for safety.
	if (bloom_bits > (1ULL << 32)) bloom_bits = (1ULL << 32);
	if (bloom_bits < 64) bloom_bits = 64;

	size_t bloom_bytes = (size_t)(bloom_bits / 8);
	// Ensure 64-bit word access is safe.
	if ((bloom_bytes & 7) != 0) {
		bloom_bytes = (bloom_bytes + 7) & ~(size_t)7;
		bloom_bits = (uint64_t)bloom_bytes * 8;
	}

	uint8_t *bloom = (uint8_t*)calloc(bloom_bytes, 1);
	if (!bloom) return 1;

	uint64_t *bloom64 = (uint64_t*)bloom;
	uint32_t mask = (uint32_t)(bloom_bits - 1);

	for (int64_t i = 0; i < count; i++) {
		const uint8_t *h = addressTable[i].value;

		uint32_t h0 = ((uint32_t)h[0] << 8) | (uint32_t)h[1];
		uint32_t h1 = ((uint32_t)h[2] << 8) | (uint32_t)h[3];
		uint32_t h2 = ((uint32_t)h[4] << 8) | (uint32_t)h[5];
		uint32_t h3 = ((uint32_t)h[6] << 8) | (uint32_t)h[7];

		uint32_t idx0 = (h0 * GOLDEN) & mask;
		uint32_t idx1 = (h1 * GOLDEN) & mask;
		uint32_t idx2 = (h2 * GOLDEN) & mask;
		uint32_t idx3 = (h3 * GOLDEN) & mask;

		bloom64[idx0 >> 6] |= (1ULL << (idx0 & 63));
		bloom64[idx1 >> 6] |= (1ULL << (idx1 & 63));
		bloom64[idx2 >> 6] |= (1ULL << (idx2 & 63));
		bloom64[idx3 >> 6] |= (1ULL << (idx3 & 63));
	}

	int rc = gpu_upload_bloom(bloom, bloom_bytes, num_hashes);
	free(bloom);
	return rc;
}

// Callback for found keys from GPU search
static void gpu_found_callback(const uint8_t *privkey_be, int compressed, void *userdata) {
	(void)userdata;

	// Convert big-endian privkey to Int
	Int key;
	key.Set32Bytes((unsigned char*)privkey_be);

	// Use existing writekey function
	writekey(compressed ? true : false, &key);
}

// GPU hybrid thread function with work-stealing
static void *gpu_hybrid_thread(void *arg) {
	gpu_hybrid_args_t *args = (gpu_hybrid_args_t *)arg;
	int total_found = 0;
	uint64_t blocks_processed = 0;
	uint64_t last_report_time = adaptive_time_ms();
	uint64_t keys_since_last_report = 0;

	// Two modes:
	// 1) Work-stealing: GPU pulls blocks from shared pool (g_work_pool.enabled=true)
	// 2) Static split: GPU scans the fixed [start_key, end_key] range once
	if (!g_work_pool.enabled) {
		printf("[GPU] Static-range thread started\n");
		uint64_t start_time = adaptive_time_ms();
		int found = gpu_run_full_search(&args->start_key, &args->end_key, &args->stride, args->target_count);
		if (found > 0) total_found = found;
		uint64_t elapsed = adaptive_time_ms() - start_time;
		uint64_t keys = g_gpu_keys_checked.load(std::memory_order_acquire);
		adaptive_report_work(WORKER_GPU, keys, elapsed);
		args->result.store(total_found, std::memory_order_release);
		args->completed.store(1, std::memory_order_release);
		printf("[GPU] Static-range thread completed: %d keys found\n", total_found);
		return NULL;
	}

	printf("[GPU] Work-stealing thread started\n");

	// Loop: pull work blocks from shared pool until exhausted
	while (!g_gpu_should_stop && g_work_pool.enabled) {
		Int block_start, block_end;

		// Try to get a work block
		if (!g_work_pool.get_block(block_start, block_end)) {
			// No more work available
			break;
		}

		(void)adaptive_time_ms();  // Could track block timing in future

		// Run GPU search on this block
		int found = gpu_run_full_search(&block_start, &block_end, &args->stride, args->target_count);
		if (found > 0) {
			total_found += found;
		}
		blocks_processed++;

		// Track keys for adaptive scheduler
		uint64_t block_keys = g_work_pool.block_size;
		keys_since_last_report += block_keys;

		// Report to adaptive scheduler periodically (every ~500ms or 5 blocks)
		uint64_t now = adaptive_time_ms();
		if ((now - last_report_time >= 500) || (blocks_processed % 5 == 0)) {
			uint64_t elapsed = now - last_report_time;
			if (elapsed > 0 && keys_since_last_report > 0) {
				adaptive_report_work(WORKER_GPU, keys_since_last_report, elapsed);
				keys_since_last_report = 0;
				last_report_time = now;
			}
		}

		// Brief status every 10 blocks
		if (blocks_processed % 10 == 0) {
			printf("[GPU] Processed %lu blocks, total found: %d\n",
				   (unsigned long)blocks_processed, total_found);
		}
	}

	printf("[GPU] Work-stealing thread completed: %lu blocks, %d keys found\n",
		   (unsigned long)blocks_processed, total_found);

	args->result.store(total_found, std::memory_order_release);
	args->completed.store(1, std::memory_order_release);

	return NULL;
}

	// Run full GPU search with CPU fallback
	static int gpu_run_full_search(Int *start_key, Int *end_key, Int *stride_val, int64_t target_count) {
	if (!gpu_backend_available()) {
		output_warning("GPU not available, cannot run full GPU search\n");
		return -1;
	}

	// Prepare search configuration
	gpu_search_config_t config;
	memset(&config, 0, sizeof(config));

	// Convert start key to big-endian bytes
	start_key->Get32Bytes(config.start_key);
	end_key->Get32Bytes(config.end_key);
		stride_val->Get32Bytes(config.stride);

		config.target_count = target_count;
		config.search_compressed = (FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH) ? 1 : 0;
		config.search_uncompressed = (FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH) ? 1 : 0;
			// Use GPU-side bloom only when it was uploaded and the target set is large enough to benefit.
			config.use_bloom = (g_gpu_bloom_uploaded && target_count > 32) ? 1 : 0;

	config.callback = gpu_found_callback;
	config.callback_userdata = NULL;

			if (g_work_pool.enabled) {
				g_gpu_keys_checked_cur.store(0, std::memory_order_release);
				// Note: reinterpret_cast is safe for lock-free atomics (same memory representation)
				config.keys_checked = reinterpret_cast<volatile uint64_t*>(&g_gpu_keys_checked_cur);
			} else {
				config.keys_checked = reinterpret_cast<volatile uint64_t*>(&g_gpu_keys_checked);
			}
		config.should_stop = reinterpret_cast<volatile int*>(&g_gpu_should_stop);
		config.quiet = (FLAGQUIET != 0) || (FLAGGPU_HYBRID != 0) || OUTPUTSECONDS.IsGreater(&ZERO);

		output_success("Starting GPU full search (ECC + hash160 + matching on GPU)\n");
		output_success("Target count: %" PRId64 ", using %s\n",
			target_count,
			target_count == 1 ? "direct comparison" :
				(config.use_bloom ? "GPU bloom + binary search" : "binary search"));
		output_success("Search mode: %s\n",
			(config.search_compressed && config.search_uncompressed) ? "compressed + uncompressed" :
			(config.search_compressed ? "compressed only" :
				(config.search_uncompressed ? "uncompressed only" : "none")));

					int found = gpu_full_search(&config);
				if (g_work_pool.enabled) {
					uint64_t done = g_gpu_keys_checked_cur.load(std::memory_order_acquire);
					g_gpu_keys_checked.fetch_add(done, std::memory_order_release);
					g_gpu_keys_checked_cur.store(0, std::memory_order_release);
				}
				return found;
		}

/* checkpointer moved to io/io.cpp */

/* writekey, writekeyeth, processOneVanity moved to io/io.cpp */

/* isBase58, isValidBase58String moved to crypto/address_util.cpp */


/* readFileVanity moved to io/io.cpp */

/* readFileAddress, forceReadFileAddress, forceReadFileAddressEth,
   forceReadFileXPoint, writeFileIfNeeded moved to io/io.cpp */

/* initBloomFilter, initBloomFilterExt moved to crypto/bloom_init.cpp */

/* calcualteindex definition moved to search/search_bsgs.cpp */
