/*
Develop by Alberto
email: albertobsd@gmail.com
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
#include <time.h>
#endif
#include "base58/libbase58.h"
#include "oldbloom/oldbloom.h"
#include "bloom/bloom.h"
#include "bloom/bloom_wrapper.h"
#include "sha3/sha3.h"
#include "util.h"
#include "workqueue.h"
#include "sysinfo.h"
#include "parameter_validator.h"
#include "gpu/gpu_backend.h"

#include "secp256k1/SECP256k1.h"
#include "secp256k1/Point.h"
#include "secp256k1/Int.h"
#include "secp256k1/IntGroup.h"
#include "secp256k1/Random.h"

#include "hash/sha256.h"
#include "hash/ripemd160.h"

#if defined(_WIN64) && !defined(__CYGWIN__)
#include "getopt.h"
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <unistd.h>
#include <pthread.h>
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

#define CRYPTO_NONE 0
#define CRYPTO_BTC 1
#define CRYPTO_ETH 2
#define CRYPTO_ALL 3

#define MODE_XPOINT 0
#define MODE_ADDRESS 1
#define MODE_BSGS 2
#define MODE_RMD160 3
#define MODE_PUB2RMD 4
#define MODE_MINIKEYS 5
#define MODE_VANITY 6

#define SEARCH_UNCOMPRESS 0
#define SEARCH_COMPRESS 1
#define SEARCH_BOTH 2

uint32_t  THREADBPWORKLOAD = 1048576;

// AVX2 support detection
static bool g_avx2_available = false;

#ifndef _WIN64
static WorkQueue<Int> g_workQueue;
#endif

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
#if defined(_WIN64) && !defined(__CYGWIN__)
	return 0;
#else
	struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
	clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
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
		fprintf(stderr, "[W] Profiling requested but allocation failed\n");
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

// ============================================================================
// Work-stealing pool for GPU/CPU collaboration
// Both GPU and CPU threads pull work blocks from the same pool for dynamic
// load balancing. The faster processor (GPU) naturally gets more work.
// ============================================================================
	struct WorkPool {
		std::atomic<uint64_t> next_block;      // Next available block index
		uint64_t block_size;                    // Keys per block
		Int range_base;                         // Starting point of range
		Int range_end;                          // End of range
		volatile bool enabled;                  // Work pool is active
		volatile bool exhausted;                // All work has been taken
	
		WorkPool() : next_block(0), block_size(0), enabled(false), exhausted(false) {}
	
		// Initialize work pool with a range
		void init(Int *start, Int *end, uint64_t blk_size) {
			range_base.Set(start);
			range_end.Set(end);
			block_size = blk_size;
	
			next_block.store(0, std::memory_order_release);
			exhausted = false;
			enabled = true;
		}

	// Get next work block (thread-safe, lock-free)
	// Returns true if work was assigned, false if no more work
		bool get_block(Int &start_out, Int &end_out) {
			if (!enabled || exhausted) return false;
	
			uint64_t block_idx = next_block.fetch_add(1, std::memory_order_acq_rel);
	
				// Calculate start = range_base + block_idx * block_size
				// Use base10 conversion to avoid signed overflow when block_idx > INT64_MAX.
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
			if (!start_out.IsLower(&range_end)) {
				exhausted = true;
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
			return exhausted;
		}

		// Disable the pool
		void disable() {
			enabled = false;
			exhausted = true;
		}
	};

// Global work pool instance
static WorkPool g_work_pool;

struct checksumsha256	{
	char data[32];
	char backup[32];
};

struct bsgs_xvalue	{
	uint64_t value;    // 8 bytes instead of 6 - same memory due to padding, faster comparison
	uint64_t index;
};

struct address_value	{
	uint8_t value[20];
};

struct tothread {
	int nt;     //Number thread
	char *rs;   //range start
	char *rpt;  //rng per thread
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

#ifndef _WIN64
static void configure_work_queue(size_t threadCount);
static void shutdown_work_queue();
#endif
static bool acquire_base_key(Int &key);

int searchbinary(struct address_value *buffer,char *data,int64_t array_length);
void sleep_ms(int milliseconds);

void _sort(struct address_value *arr,int64_t N);
void _insertionsort(struct address_value *arr, int64_t n);
void _introsort(struct address_value *arr,uint32_t depthLimit, int64_t n);
void _swap(struct address_value *a,struct address_value *b);
int64_t _partition(struct address_value *arr, int64_t n);
void _myheapsort(struct address_value	*arr, int64_t n);
void _heapify(struct address_value *arr, int64_t n, int64_t i);

void bsgs_sort(struct bsgs_xvalue *arr,int64_t n);
void bsgs_myheapsort(struct bsgs_xvalue *arr, int64_t n);
void bsgs_insertionsort(struct bsgs_xvalue *arr, int64_t n);
void bsgs_introsort(struct bsgs_xvalue *arr,uint32_t depthLimit, int64_t n);
void bsgs_swap(struct bsgs_xvalue *a,struct bsgs_xvalue *b);
void bsgs_heapify(struct bsgs_xvalue *arr, int64_t n, int64_t i);
int64_t bsgs_partition(struct bsgs_xvalue *arr, int64_t n);

int bsgs_searchbinary(struct bsgs_xvalue *arr,char *data,int64_t array_length,uint64_t *r_value);
int bsgs_secondcheck(Int *start_range,uint32_t a,uint32_t k_index,Int *privatekey);
int bsgs_thirdcheck(Int *start_range,uint32_t a,uint32_t k_index,Int *privatekey);

void sha256sse_22(uint8_t *src0, uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *dst0, uint8_t *dst1, uint8_t *dst2, uint8_t *dst3);
void sha256sse_23(uint8_t *src0, uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *dst0, uint8_t *dst1, uint8_t *dst2, uint8_t *dst3);

bool vanityrmdmatch(unsigned char *rmdhash);
void writevanitykey(bool compress,Int *key);
int addvanity(char *target);
int minimum_same_bytes(unsigned char* A,unsigned char* B, int length);

void writekey(bool compressed,Int *key);
void writekeyeth(Int *key);

void checkpointer(void *ptr,const char *file,const char *function,const  char *name,int line);

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
	volatile int result;
	volatile int completed;
} gpu_hybrid_args_t;

static void *gpu_hybrid_thread(void *arg);

bool isBase58(char c);
bool isValidBase58String(char *str);

bool readFileAddress(char *fileName);
bool readFileVanity(char *fileName);
bool forceReadFileAddress(char *fileName);
bool forceReadFileAddressEth(char *fileName);
bool forceReadFileXPoint(char *fileName);
bool processOneVanity();

bool initBloomFilter(struct bloom *bloom_arg,uint64_t items_bloom);
bool initBloomFilterExt(bloom_extended_t *bloom_arg, uint64_t items_bloom);

void writeFileIfNeeded(const char *fileName);

void calcualteindex(int i,Int *key);
#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process_vanity(LPVOID vargp);
DWORD WINAPI thread_process_minikeys(LPVOID vargp);
DWORD WINAPI thread_process(LPVOID vargp);
DWORD WINAPI thread_process_bsgs(LPVOID vargp);
DWORD WINAPI thread_process_bsgs_backward(LPVOID vargp);
DWORD WINAPI thread_process_bsgs_both(LPVOID vargp);
DWORD WINAPI thread_process_bsgs_random(LPVOID vargp);
DWORD WINAPI thread_process_bsgs_dance(LPVOID vargp);
DWORD WINAPI thread_bPload(LPVOID vargp);
DWORD WINAPI thread_bPload_2blooms(LPVOID vargp);
#else
void *thread_process_vanity(void *vargp);
void *thread_process_minikeys(void *vargp);	
void *thread_process(void *vargp);
void *thread_process_bsgs(void *vargp);
void *thread_process_bsgs_backward(void *vargp);
void *thread_process_bsgs_both(void *vargp);
void *thread_process_bsgs_random(void *vargp);
void *thread_process_bsgs_dance(void *vargp);
void *thread_bPload(void *vargp);
void *thread_bPload_2blooms(void *vargp);
#endif

char *pubkeytopubaddress(char *pkey,int length);
void pubkeytopubaddress_dst(char *pkey,int length,char *dst);
void rmd160toaddress_dst(char *rmd,char *dst);
void set_minikey(char *buffer,char *rawbuffer,int length);
bool increment_minikey_index(char *buffer,char *rawbuffer,int index);
void increment_minikey_N(char *rawbuffer);
	
void KECCAK_256(uint8_t *source, size_t size,uint8_t *dst);
void generate_binaddress_eth(Point &publickey,unsigned char *dst_address);

int THREADOUTPUT = 0;
char *bit_range_str_min;
char *bit_range_str_max;

const char *bsgs_modes[5] = {"sequential","backward","both","random","dance"};
const char *modes[7] = {"xpoint","address","bsgs","rmd160","pub2rmd","minikeys","vanity"};
const char *cryptos[3] = {"btc","eth","all"};
const char *publicsearch[3] = {"uncompress","compress","both"};
const char *default_fileName = "addresses.txt";

#if defined(_WIN64) && !defined(__CYGWIN__)
HANDLE* tid = NULL;
HANDLE write_keys;
HANDLE write_random;
HANDLE bsgs_thread;
HANDLE *bPload_mutex = NULL;
#else
pthread_t *tid = NULL;
pthread_mutex_t write_keys;
pthread_mutex_t write_random;
pthread_mutex_t bsgs_thread;
pthread_mutex_t *bPload_mutex = NULL;
#endif

uint64_t FINISHED_THREADS_COUNTER = 0;
uint64_t FINISHED_THREADS_BP = 0;
uint64_t THREADCYCLES = 0;
uint64_t THREADCOUNTER = 0;
uint64_t FINISHED_ITEMS = 0;
uint64_t OLDFINISHED_ITEMS = -1;

uint8_t byte_encode_crypto = 0x00;		/* Bitcoin  */


int vanity_rmd_targets = 0;
int vanity_rmd_total = 0;
int *vanity_rmd_limits = NULL;
uint8_t ***vanity_rmd_limit_values_A = NULL,***vanity_rmd_limit_values_B = NULL;
int vanity_rmd_minimun_bytes_check_length = 999999;
char **vanity_address_targets = NULL;
struct bloom *vanity_bloom = NULL;

bloom_extended_t bloom;  /* Fast bloom filter wrapper */

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

uint64_t N_SEQUENTIAL_MAX = 0x100000000;
uint64_t DEBUGCOUNT = 0x400;
uint64_t u64range;

Int OUTPUTSECONDS;

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
int MAXLENGTHADDRESS = -1;
int NTHREADS = 1;
int FLAGTHREADS = 0;

int FLAGSAVEREADFILE = 0;
int FLAGREADEDFILE1 = 0;
int FLAGREADEDFILE2 = 0;
int FLAGREADEDFILE3 = 0;
int FLAGREADEDFILE4 = 0;
int FLAGUPDATEFILE1 = 0;


int FLAGSTRIDE = 0;
int FLAGSEARCH = 2;
int FLAGBITRANGE = 0;
int FLAGRANGE = 0;
int FLAGFILE = 0;
int FLAGMODE = MODE_ADDRESS;
int FLAGCRYPTO = 0;
int FLAGRAWDATA	= 0;
int FLAGRANDOM = 0;
int FLAG_N = 0;
int FLAGPRECALCUTED_P_FILE = 0;
// GPU usage flag: 0=off, 1=on, -1=auto
int FLAGGPU = 0;
// GPU full search mode: 0=off (hash-only), 1=full ECC+hash+match on GPU
int FLAGGPU_FULL = 0;
// GPU hybrid mode: 1=run GPU+CPU in parallel for maximum throughput
int FLAGGPU_HYBRID = 0;
		// Volatile stats for GPU search
		volatile uint64_t g_gpu_keys_checked = 0;
		volatile uint64_t g_gpu_keys_checked_cur = 0;
		volatile int g_gpu_should_stop = 0;
	// True if we uploaded a GPU-side bloom filter for targets (full mode).
	static int g_gpu_bloom_uploaded = 0;
	// Hybrid mode range split (GPU gets gpu_range_split% of the total range)
	int g_gpu_range_percent = 80;  // Default: GPU gets 80% of range

		static inline bool cpu_use_y_parity_for_compressed_btc() {
		// Unify CPU-only and HYBRID behavior for BTC compressed-only search:
		// compute the real Y parity and hash only the actual compressed prefix (02 or 03).
		//
		// This avoids doing 2x hash work (02+03) and aligns CPU with GPU FULL/HYBRID behavior.
		//
		// Overrides:
		//   KEYHUNT_CPU_USE_Y=0|1            (global CPU behavior, default=1)
		//   KEYHUNT_HYBRID_CPU_USE_Y=0|1     (when in HYBRID+FULL, default=1)
		if (!((FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160) &&
			  FLAGCRYPTO == CRYPTO_BTC &&
			  !FLAGENDOMORPHISM &&
			  FLAGSEARCH == SEARCH_COMPRESS)) {
			return false;
		}

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
			if (!g_work_pool.enabled) {
				return __atomic_load_n(&g_gpu_keys_checked, __ATOMIC_ACQUIRE);
			}
			uint64_t cur = __atomic_load_n(&g_gpu_keys_checked_cur, __ATOMIC_ACQUIRE);
			uint64_t base = __atomic_load_n(&g_gpu_keys_checked, __ATOMIC_ACQUIRE);
			return base + cur;
		}

int bitrange;
char *str_N;
char *range_start;
char *range_end;
char *str_stride;
Int stride;

uint64_t BSGS_XVALUE_RAM = 8;  // Optimized: 8 bytes for uint64_t comparison

// Global system information (for memory checks across all modes)
system_info_t g_sysinfo;
gpu_backend_info_t g_gpu_backend_info;
// Only the first 16 bytes of X are used in BSGS bloom filters to reduce hash cost.
uint64_t BSGS_BUFFERXPOINTLENGTH = 16;
uint64_t BSGS_BUFFERREGISTERLENGTH = 36;

static int hybrid_get_gpu_range_percent_default(int cpu_threads) {
	const char *env = getenv("KEYHUNT_HYBRID_GPU_PERCENT");
	if (env && *env) {
		int v = atoi(env);
		if (v >= 1 && v <= 99) return v;
	}
	if (cpu_threads <= 0) return g_gpu_range_percent;

	// Heuristic split based on SM count vs CPU threads.
	// Goal: avoid the CPU tail becoming the bottleneck in static split.
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
*/
int *bsgs_found;
std::vector<Point> OriginalPointsBSGS;
bool *OriginalPointsBSGScompressed;

uint64_t bytes;
char checksum[32],checksum_backup[32];
char buffer_bloom_file[1024];
struct bsgs_xvalue *bPtable;
struct address_value *addressTable;

struct oldbloom oldbloom_bP;

// BSGS bloom filters use extended wrapper to enable fast bloom
bloom_extended_t *bloom_bP;
bloom_extended_t *bloom_bPx2nd; //2nd Bloom filter check
bloom_extended_t *bloom_bPx3rd; //3rd Bloom filter check

struct checksumsha256 *bloom_bP_checksums;
struct checksumsha256 *bloom_bPx2nd_checksums;
struct checksumsha256 *bloom_bPx3rd_checksums;

#if defined(_WIN64) && !defined(__CYGWIN__)
std::vector<HANDLE> bloom_bP_mutex;
std::vector<HANDLE> bloom_bPx2nd_mutex;
std::vector<HANDLE> bloom_bPx3rd_mutex;
#else
pthread_mutex_t *bloom_bP_mutex;
pthread_mutex_t *bloom_bPx2nd_mutex;
pthread_mutex_t *bloom_bPx3rd_mutex;
#endif




uint64_t bloom_bP_totalbytes = 0;
uint64_t bloom_bP2_totalbytes = 0;
uint64_t bloom_bP3_totalbytes = 0;
uint64_t bsgs_m = 4194304;
uint64_t bsgs_m2;
uint64_t bsgs_m3;
uint64_t bsgs_aux;
uint32_t bsgs_point_number;

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
				printf("[I] %s: forced CPU N to 0x%llx via %s\n",
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
		printf("[I] %s: adjusted CPU N to 0x%llx for better thread utilization\n",
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
#if defined(_WIN64) && !defined(__CYGWIN__)
	WaitForSingleObject(write_random, INFINITE);
	out.Set(&n_range_start);
	ReleaseMutex(write_random);
	return true;
#else
	if (!FLAGRANDOM && g_workQueue.enabled()) {
		if (g_workQueue.snapshot_next_start(out)) {
			return true;
		}
	}
	pthread_mutex_lock(&write_random);
	out.Set(&n_range_start);
	pthread_mutex_unlock(&write_random);
	return true;
#endif
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
	volatile int *stop_flag;
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
	while (!*(args->stop_flag)) {
		sleep_ms(1000);
		seconds++;
		if (*(args->stop_flag)) {
			break;
		}
		if ((seconds % (uint64_t)period) != 0) {
			continue;
		}

		uint64_t total_u64 = g_gpu_keys_checked;
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
	if (g_work_pool.enabled) {
		// Check if we have a valid cached block with remaining work
		if (!cpu_cached_block_valid || !cpu_cached_block_start.IsLower(&cpu_cached_block_end)) {
			// Get a new block from the work pool
			if (!g_work_pool.get_block(cpu_cached_block_start, cpu_cached_block_end)) {
				return false;  // No more work available
			}
			cpu_cached_block_valid = true;
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
#if defined(_WIN64) && !defined(__CYGWIN__)
	WaitForSingleObject(write_random, INFINITE);
	bool hasWork = n_range_start.IsLower(&n_range_end);
	if(hasWork)	{
		key.Set(&n_range_start);
		n_range_start.Add(N_SEQUENTIAL_MAX);
	}
	ReleaseMutex(write_random);
	return hasWork;
#else
	pthread_mutex_lock(&write_random);
	bool hasWork = n_range_start.IsLower(&n_range_end);
	if(hasWork)	{
		key.Set(&n_range_start);
		n_range_start.Add(N_SEQUENTIAL_MAX);
	}
	pthread_mutex_unlock(&write_random);
	return hasWork;
#endif
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
						fprintf(stderr, "[W] GPU hash160 failed; falling back to CPU.\n");
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
		fprintf(stderr, "[E] GPU self-test failed: CUDA hash160 call failed\n");
		return false;
	}

	for (size_t i = 0; i < kCount; ++i) {
		if (memcmp(cpu02[i], gpu02[i], 20) != 0) {
			fprintf(stderr, "[E] GPU self-test failed: mismatch prefix 02 at index %zu\n", i);
			return false;
		}
		if (memcmp(cpu03[i], gpu03[i], 20) != 0) {
			fprintf(stderr, "[E] GPU self-test failed: mismatch prefix 03 at index %zu\n", i);
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
	pthread_t gpu_thread_id = 0;
	gpu_hybrid_args_t gpu_hybrid_args = {};
	int gpu_hybrid_started = 0;

#if defined(_WIN64) && !defined(__CYGWIN__)
	DWORD s;
	write_keys = CreateMutex(NULL, FALSE, NULL);
	write_random = CreateMutex(NULL, FALSE, NULL);
	bsgs_thread = CreateMutex(NULL, FALSE, NULL);
#else
	pthread_mutex_init(&write_keys,NULL);
	pthread_mutex_init(&write_random,NULL);
	pthread_mutex_init(&bsgs_thread,NULL);
	int s;
#endif

	srand(time(NULL));

	secp = new Secp256K1();
	secp->Init();
	OUTPUTSECONDS.SetInt32(30);
	ZERO.SetInt32(0);
	ONE.SetInt32(1);
	BSGS_GROUP_SIZE.SetInt32(CPU_GRP_SIZE);
	
#if defined(_WIN64) && !defined(__CYGWIN__)
	//Any windows secure random source goes here
	rseed(clock() + time(NULL) + rand());
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
		fprintf(stderr,"[W] Warning: getrandom() failed (bytes_read=%d), using fallback RNG\n", bytes_read);
		rseed(clock() + time(NULL) + rand()*rand());
	}
#endif
	
	
	
	printf("[+] Version %s, developed by AlbertoBSD\n",version);

	g_profile_enabled = env_truthy_kh("KEYHUNT_PROFILE");
	if (g_profile_enabled) {
		fprintf(stderr, "[I] Profiling enabled (KEYHUNT_PROFILE=1)\n");
	}

	// Auto-detect system configuration and optimize parameters
	// (sysinfo is now a global variable for memory checks)

	// Check if user wants to skip system detection (useful for problematic systems)
	if (getenv("KEYHUNT_SKIP_SYSINFO")) {
		fprintf(stderr,"[W] Skipping system detection (KEYHUNT_SKIP_SYSINFO set)\n");
		fprintf(stderr,"[I] Using safe default parameters\n");
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
		printf("[+] AVX2 detected: Using optimized 8-way parallel RIPEMD160\n");
	} else {
		printf("[I] AVX2 not available: Using SSE2 4-way parallel RIPEMD160\n");
	}

	// Store auto-tuned recommendations (don't apply yet - wait for user args)
	OPTIMAL_THREADS = g_sysinfo.recommended_threads;
	OPTIMAL_N = g_sysinfo.recommended_n;
	OPTIMAL_KFACTOR = g_sysinfo.recommended_kfactor;

	// Keep CPU_GRP_SIZE at proven optimal value of 1024
	// Testing showed that larger values (2048, 4096) actually hurt performance
	// due to increased ModInv overhead and worse cache behavior
	CPU_GRP_SIZE = 1024;
	fprintf(stderr,"[I] Using CPU_GRP_SIZE: %u (proven optimal)\n", CPU_GRP_SIZE);

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
				fprintf(stderr,"[W] Skipping checksums on files\n");
			break;
			case 'B':
				index_value = indexOf(optarg,bsgs_modes,5);
				if(index_value >= 0 && index_value <= 4)	{
					FLAGBSGSMODE = index_value;
					//printf("[+] BSGS mode %s\n",optarg);
				}
				else	{
					fprintf(stderr,"[W] Ignoring unknow bsgs mode %s\n",optarg);
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
					fprintf(stderr,"[E] invalid bits param: %s.\n",optarg);
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
						printf("[+] Setting search for ETH adddress.\n");
					break;
					/*
					case 2: //all
						FLAGCRYPTO = CRYPTO_ALL;
					break;
					*/
					default:
						FLAGCRYPTO = CRYPTO_NONE;
						fprintf(stderr,"[E] Unknow crypto value %s\n",optarg);
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
							fprintf(stderr,"[E] invalid character in minikey\n");
							exit(EXIT_FAILURE);
						}
						
					}
					raw_baseminikey[21] = '\0';
				}
				else	{
					fprintf(stderr,"[E] Invalid Minikey length %zu : %s\n",strlen(optarg),optarg);
					exit(EXIT_FAILURE);
				}
				
			break;
			case 'd':
				FLAGDEBUG = 1;
				printf("[+] Flag DEBUG enabled\n");
			break;
			case 'e':
				FLAGENDOMORPHISM = 1;
				printf("[+] Endomorphism enabled\n");
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
						printf("[+] GPU hash-only mode (CPU generates points, GPU hashes)\n");
					} else if (strcasecmp(optarg, "full") == 0 || strcasecmp(optarg, "on") == 0 || strcasecmp(optarg, "yes") == 0 || strcmp(optarg, "1") == 0) {
						FLAGGPU = 1;
						FLAGGPU_FULL = 1;  // Full GPU mode
						printf("[+] GPU full mode (ECC + hash160 + matching on GPU)\n");
					} else if (strcasecmp(optarg, "hybrid") == 0) {
						FLAGGPU = 1;
						FLAGGPU_FULL = 1;
						FLAGGPU_HYBRID = 1;  // Hybrid mode: GPU + CPU in parallel
						printf("[+] GPU hybrid mode (GPU + CPU in parallel for maximum throughput)\n");
					} else {
						fprintf(stderr,"[W] Invalid -G value '%s', use: off|auto|hash|full|hybrid\n", optarg);
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
				printf("[+] K factor %i\n",KFACTOR);
			break;

			case 'l':
				switch(indexOf(optarg,publicsearch,3)) {
					case SEARCH_UNCOMPRESS:
						FLAGSEARCH = SEARCH_UNCOMPRESS;
						printf("[+] Search uncompress only\n");
					break;
					case SEARCH_COMPRESS:
						FLAGSEARCH = SEARCH_COMPRESS;
						printf("[+] Search compress only\n");
					break;
					case SEARCH_BOTH:
						FLAGSEARCH = SEARCH_BOTH;
						printf("[+] Search both compress and uncompress\n");
					break;
				}
			break;
			case 'M':
				FLAGMATRIX = 1;
				printf("[+] Matrix screen\n");
			break;
			case 'P':
				FLAGPROGRESSBAR = 1;
				printf("[+] Segmented progress indicator enabled\n");
			break;
			case 'm':
				switch(indexOf(optarg,modes,7)) {
					case MODE_XPOINT: //xpoint
						FLAGMODE = MODE_XPOINT;
						printf("[+] Mode xpoint\n");
					break;
					case MODE_ADDRESS: //address
						FLAGMODE = MODE_ADDRESS;
						printf("[+] Mode address\n");
					break;
					case MODE_BSGS:
						FLAGMODE = MODE_BSGS;
						//printf("[+] Mode BSGS\n");
					break;
					case MODE_RMD160:
						FLAGMODE = MODE_RMD160;
						FLAGCRYPTO = CRYPTO_BTC;
						printf("[+] Mode rmd160\n");
					break;
					case MODE_PUB2RMD:
						FLAGMODE = MODE_PUB2RMD;
						printf("[+] Mode pub2rmd was removed\n");
						exit(0);
					break;
					case MODE_MINIKEYS:
						FLAGMODE = MODE_MINIKEYS;
						printf("[+] Mode minikeys\n");
					break;
					case MODE_VANITY:
						FLAGMODE = MODE_VANITY;
						printf("[+] Mode vanity\n");
						if(vanity_bloom == NULL){
							vanity_bloom = (struct bloom*) calloc(1,sizeof(struct bloom));
							checkpointer((void *)vanity_bloom,__FILE__,"calloc","vanity_bloom" ,__LINE__ -1);
						}
					break;
					default:
						fprintf(stderr,"[E] Unknow mode value %s\n",optarg);
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
				printf("[+] Quiet thread output\n");
			break;
			case 'R':
				printf("[+] Random mode\n");
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
								fprintf(stderr,"[E] Invalid hexstring : %s.\n",range_start);
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
									fprintf(stderr,"[E] Invalid hexstring : %s\n",range_start);
								}
								else	{
									fprintf(stderr,"[E] Invalid hexstring : %s\n",range_end);
								}
							}
						break;
						default:
							printf("[E] Unknow number of Range Params: %i\n",t.n);
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
					printf("[+] Turn off stats output\n");
				}
				else	{
					hextemp = OUTPUTSECONDS.GetBase10();
					printf("[+] Stats output every %s seconds\n",hextemp);
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
				printf((NTHREADS > 1) ? "[+] Threads : %u (user-specified)\n": "[+] Thread : %u (user-specified)\n",NTHREADS);
			break;
			case 'v':
				FLAGVANITY = 1;
				if(vanity_bloom == NULL){
					vanity_bloom = (struct bloom*) calloc(1,sizeof(struct bloom));
					checkpointer((void *)vanity_bloom,__FILE__,"calloc","vanity_bloom" ,__LINE__ -1);
				}
				if(isValidBase58String(optarg))	{
					if(addvanity(optarg) > 0)	{
						printf("[+] Added Vanity search : %s\n",optarg);
					}
					else	{
						printf("[+] Vanity search \"%s\" was NOT Added\n",optarg);
					}
				}
				else {
					fprintf(stderr,"[+] The string \"%s\" is not Valid Base58\n",optarg);
				}
				
			break;
			case '8':
				if(strlen(optarg) == 58)	{
					Ccoinbuffer = optarg; 
					printf("[+] Base58 for Minikeys %s\n",Ccoinbuffer);
				}
				else	{
					fprintf(stderr,"[E] The base58 alphabet must be 58 characters long.\n");
					exit(EXIT_FAILURE);
				}
			break;
			case 'z':
				FLAGBLOOMMULTIPLIER= strtol(optarg,NULL,10);
				if(FLAGBLOOMMULTIPLIER <= 0)	{
					FLAGBLOOMMULTIPLIER = 1;
				}
				printf("[+] Bloom Size Multiplier %i\n",FLAGBLOOMMULTIPLIER);
			break;
			default:
				fprintf(stderr,"[E] Unknow opcion -%c\n",c);
				exit(EXIT_FAILURE);
			break;
		}

		}

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
			fprintf(stderr, "[W] Some parameters were auto-corrected for safety\n");
		}
	}
	// ========== End Parameter Validation ==========

	if(  FLAGBSGSMODE == MODE_BSGS && FLAGENDOMORPHISM)	{
		fprintf(stderr,"[E] Endomorphism doesn't work with BSGS\n");
		exit(EXIT_FAILURE);
	}
	
	
	if(  FLAGBSGSMODE == MODE_BSGS  && FLAGSTRIDE)	{
		fprintf(stderr,"[E] Stride doesn't work with BSGS\n");
		exit(EXIT_FAILURE);
	}
	if(FLAGSTRIDE)	{
		if(str_stride[0] == '0' && str_stride[1] == 'x')	{
			stride.SetBase16(str_stride+2);
		}
		else{
			stride.SetBase10(str_stride);
		}
		printf("[+] Stride : %s\n",stride.GetBase10());
	}
	else	{
		FLAGSTRIDE = 1;
		stride.Set(&ONE);
	}
	init_generator();
	if(FLAGMODE == MODE_BSGS )	{
		printf("[+] Mode BSGS %s\n",bsgs_modes[FLAGBSGSMODE]);
	}
	
	if(FLAGFILE == 0) {
		fileName =(char*) default_fileName;
	}
		
		if(FLAGMODE == MODE_ADDRESS && FLAGCRYPTO == CRYPTO_NONE) {	//When none crypto is defined the default search is for Bitcoin
			FLAGCRYPTO = CRYPTO_BTC;
			printf("[+] Setting search for btc adddress\n");
		}
		if(FLAGMODE == MODE_RMD160 && FLAGCRYPTO == CRYPTO_NONE) {	// Default rmd160 search is Bitcoin HASH160 (same pipeline as address mode)
			FLAGCRYPTO = CRYPTO_BTC;
			printf("[+] Setting search for btc rmd160\n");
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
				printf("[+] CUDA backend: %s (%d MPs, %lu MB VRAM)\n",
					g_gpu_backend_info.name[0] ? g_gpu_backend_info.name : "NVIDIA GPU",
					g_gpu_backend_info.multiprocessors,
					(unsigned long)g_gpu_backend_info.vram_mb);
			} else {
#ifdef HAVE_CUDA_BACKEND
				fprintf(stderr, "[W] CUDA backend compiled but no GPU detected\n");
#else
				fprintf(stderr, "[W] CUDA backend not compiled (nvcc not found at build time)\n");
				fprintf(stderr, "[I] To enable GPU: install CUDA toolkit, ensure 'nvcc' is in PATH, rebuild\n");
#endif
			}
		}

			// Resolve auto mode (-G auto)
			if (FLAGGPU == -1 || FLAGGPU_FULL == -1) {
				if (gpu_available && mode_supports_gpu_full) {
					// Auto: prefer full GPU mode if available
					FLAGGPU = 1;
					FLAGGPU_FULL = 1;
					printf("[+] GPU auto: using full mode (ECC + hash160 + matching on GPU)\n");
				} else {
				FLAGGPU = 0;
				FLAGGPU_FULL = 0;
					if (!gpu_available) {
						fprintf(stderr, "[I] GPU auto: falling back to CPU (no GPU available)\n");
					} else if (!mode_supports_gpu_full) {
						fprintf(stderr, "[I] GPU auto: falling back to CPU (mode not supported)\n");
					}
				}
			}

				// Validate explicit GPU requests
				if ((FLAGGPU == 1 || FLAGGPU_FULL == 1) && !gpu_available) {
				fprintf(stderr, "[W] GPU requested but not available, falling back to CPU\n");
				FLAGGPU = 0;
				FLAGGPU_FULL = 0;
			}

			// GPU backends currently assume stride == 1 for correctness/performance.
			// If the user specified a different stride, fall back to CPU.
			if ((FLAGGPU == 1 || FLAGGPU_FULL == 1) && !stride.IsOne()) {
				fprintf(stderr, "[W] GPU mode requires stride=1 (-I 1). Falling back to CPU.\n");
				FLAGGPU = 0;
				FLAGGPU_FULL = 0;
				FLAGGPU_HYBRID = 0;
			}

				// Validate mode support
				if (FLAGGPU_FULL == 1 && !mode_supports_gpu_full) {
					fprintf(stderr, "[W] GPU FULL not supported for this mode/options, using CPU\n");
					FLAGGPU = 0;
					FLAGGPU_FULL = 0;
				}
				if (FLAGGPU == 1 && FLAGGPU_FULL == 0 && !mode_supports_gpu_hash) {
					// If the user asked for HASH mode but also requested uncompressed, upgrade to FULL when possible.
					if (wantUncompressed && gpu_available && mode_supports_gpu_full) {
						fprintf(stderr, "[I] GPU HASH mode does not support uncompressed; upgrading to GPU FULL\n");
						FLAGGPU_FULL = 1;
					} else {
						fprintf(stderr, "[W] GPU HASH not supported for this mode/options, using CPU\n");
						FLAGGPU = 0;
						FLAGGPU_FULL = 0;
					}
				}

		// Show final GPU mode
		if (FLAGGPU_FULL == 1) {
			printf("[+] GPU mode: FULL (secp256k1 + SHA256 + RIPEMD160 + matching on GPU)\n");
		} else if (FLAGGPU == 1) {
			printf("[+] GPU mode: HASH (CPU generates points, GPU computes hash160)\n");
		}

		// GPU self-test if requested
		if (FLAGGPU == 1 && getenv("KEYHUNT_GPU_SELFTEST")) {
			if (!gpu_selftest_hash160_fromX()) {
				fprintf(stderr, "[W] Disabling GPU due to failed self-test\n");
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
					printf("[I] GPU active: using %d CPU threads\n", NTHREADS);
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
			fprintf(stderr,"[E] End range can't be zero\nFallback to random mode!\n");
			FLAGRANGE = 0;
		}
		if(FLAGRANGE)	{
			if( n_range_start.IsGreater(&n_range_end)) {
				fprintf(stderr,"[W] Opps, start range can't be great than end range. Swapping them\n");
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
				fprintf(stderr,"[E] Start and End range can't be great than N\nFallback to random mode!\n");
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
					fprintf(stderr,"[W] WTF!\n");
				}
			}
		}
	}
	N = 0;

	if(FLAGMODE != MODE_BSGS )	{
		// Apply auto-tuned N if user didn't specify -n
		if(!FLAG_N && OPTIMAL_N > 0) {
			N_SEQUENTIAL_MAX = OPTIMAL_N;
			printf("[I] Using auto-tuned N value: 0x%llx\n", (unsigned long long)OPTIMAL_N);
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
				fprintf(stderr,"[E] Invalid -n value: %s\n", str_N);
				FLAG_N = 0;
				N_SEQUENTIAL_MAX = 0x100000000;
			} else {
				N_SEQUENTIAL_MAX = (uint64_t)parsed;
			}
			
			if(N_SEQUENTIAL_MAX < 1024)	{
				fprintf(stderr,"[I] n value need to be equal or great than 1024, back to defaults\n");
				FLAG_N = 0;
				N_SEQUENTIAL_MAX = 0x100000000;
			}
			if(N_SEQUENTIAL_MAX % 1024 != 0)	{
				fprintf(stderr,"[I] n value need to be multiplier of  1024\n");
				FLAG_N = 0;
				N_SEQUENTIAL_MAX = 0x100000000;
			}
		}
		else {
			// No user param and no auto-tuning: use default
			N_SEQUENTIAL_MAX = 0x100000000;
		}
		printf("[+] N = 0x%llx\n",(unsigned long long)N_SEQUENTIAL_MAX);
		if(FLAGMODE == MODE_MINIKEYS)	{
			BSGS_N.SetInt32(DEBUGCOUNT);
			if(FLAGBASEMINIKEY)	{
				printf("[+] Base Minikey : %s\n",str_baseminikey);
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
				printf("[+] Bit Range %i\n",bitrange);
			}
			else	{
				printf("[+] Range \n");
			}
		}
		if(FLAGMODE != MODE_MINIKEYS)	{
			hextemp = n_range_start.GetBase16();
			printf("[+] -- from : 0x%s\n",hextemp);
			free(hextemp);
			if (FLAGRANGE) {
				Int end_inclusive;
				end_inclusive.Set(&n_range_end);
				end_inclusive.SubOne();
				hextemp = end_inclusive.GetBase16();
			} else {
				hextemp = n_range_end.GetBase16();
			}
			printf("[+] -- to   : 0x%s\n",hextemp);
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
					fprintf(stderr,"[E] Unenexpected error\n");
					exit(EXIT_FAILURE);
				}
			break;
			case MODE_VANITY:
				if(!readFileVanity(fileName))	{
					fprintf(stderr,"[E] Unenexpected error\n");
					exit(EXIT_FAILURE);
				}
			break;
		}
		
		if(FLAGMODE != MODE_VANITY && !FLAGREADEDFILE1)	{
			printf("[+] Sorting data ...");
			_sort(addressTable,N);
			printf(" done! %" PRIu64 " values were loaded and sorted\n",N);
			writeFileIfNeeded(fileName);
		}

			// GPU Full Search initialization (upload G table and targets)
			if (FLAGGPU_FULL == 1) {
				printf("[+] Initializing GPU full search...\n");

				// Upload precomputed G table to GPU
				if (gpu_upload_gtable_from_secp() == 0) {
					printf("[+] G table uploaded to GPU (8192 points)\n");
				} else {
					fprintf(stderr, "[E] Failed to upload G table to GPU\n");
					FLAGGPU_FULL = 0;
					FLAGGPU = 0;
				}

				// Upload targets to GPU
				if (FLAGGPU_FULL && gpu_upload_targets_from_addressTable(N) == 0) {
					printf("[+] Targets uploaded to GPU (%" PRIu64 " hashes)\n", N);
					// Optional: build a GPU-specific bloom filter to reduce target searches for large N.
					g_gpu_bloom_uploaded = 0;
					if (N > 32) {
						if (gpu_build_and_upload_bloom_from_addressTable(N) == 0) {
							g_gpu_bloom_uploaded = 1;
							printf("[+] GPU bloom uploaded (accelerates matching for large target sets)\n");
						} else {
							fprintf(stderr, "[W] GPU bloom upload failed; continuing without GPU bloom\n");
						}
					}
				} else if (FLAGGPU_FULL) {
					fprintf(stderr, "[E] Failed to upload targets to GPU\n");
					FLAGGPU_FULL = 0;
					FLAGGPU = 0;
				}
			}
		}
	
	if(FLAGMODE == MODE_BSGS )	{
		printf("[+] Opening file %s\n",fileName);
		fd = fopen(fileName,"rb");
		if(fd == NULL)	{
			fprintf(stderr,"[E] Can't open file %s\n",fileName);
			exit(EXIT_FAILURE);
		}
		aux = (char*) malloc(1024);
		checkpointer((void *)aux,__FILE__,"malloc","aux" ,__LINE__ - 1);
		while(!feof(fd))	{
			if(fgets(aux,1022,fd) == aux)	{
				trim(aux," \t\n\r");
				if(strlen(aux) >= 128)	{	//Length of a full address in hexadecimal without 04
						N++;
				}else	{
					if(strlen(aux) >= 66)	{
						N++;
					}
				}
			}
		}
		if(N == 0)	{
			fprintf(stderr,"[E] There is no valid data in the file\n");
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
		while(!feof(fd))	{
			if(fgets(aux,1022,fd) == aux)	{
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
		}
		fclose(fd);
		bsgs_point_number = N;
		if(bsgs_point_number > 0)	{
			printf("[+] Added %u points from file\n",bsgs_point_number);
		}
		else	{
			fprintf(stderr,"[E] The file don't have any valid publickeys\n");
			exit(EXIT_FAILURE);
		}
		BSGS_N.SetInt32(0);
		BSGS_M.SetInt32(0);
		

		BSGS_M.SetInt64(bsgs_m);

		// Apply auto-tuning for BSGS mode if user didn't specify params
		if(!FLAG_N && OPTIMAL_N > 0) {
			BSGS_N.SetInt64(OPTIMAL_N);
			printf("[I] Using auto-tuned N value: 0x%llx\n", (unsigned long long)OPTIMAL_N);
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
			printf("[I] Using auto-tuned K factor: %d\n", OPTIMAL_KFACTOR);
		}

	bsgs_recalculate_with_new_params:
		// Label for auto-adjustment: recalculate all BSGS parameters

		if(BSGS_N.HasSqrt())	{	//If the root is exact
			BSGS_M.Set(&BSGS_N);
			BSGS_M.ModSqrt();
		}
		else	{
			fprintf(stderr,"[E] -n param doesn't have exact square root\n");
			exit(EXIT_FAILURE);
		}

		BSGS_AUX.Set(&BSGS_M);
		BSGS_AUX.Mod(&BSGS_GROUP_SIZE);	
		
		if(!BSGS_AUX.IsZero()){ //If M is not divisible by  BSGS_GROUP_SIZE (1024) 
			hextemp = BSGS_GROUP_SIZE.GetBase10();
			fprintf(stderr,"[E] M value is not divisible by %s\n",hextemp);
			exit(EXIT_FAILURE);
		}

		bsgs_m = BSGS_M.GetInt64();

		if(FLAGRANGE || FLAGBITRANGE)	{
			if(FLAGBITRANGE)	{	// Bit Range
				n_range_start.SetBase16(bit_range_str_min);
				n_range_end.SetBase16(bit_range_str_max);

				n_range_diff.Set(&n_range_end);
				n_range_diff.Sub(&n_range_start);
				printf("[+] Bit Range %i\n",bitrange);
				printf("[+] -- from : 0x%s\n",bit_range_str_min);
				printf("[+] -- to   : 0x%s\n",bit_range_str_max);
			}
			else	{
				printf("[+] Range \n");
				printf("[+] -- from : 0x%s\n",range_start);
				printf("[+] -- to   : 0x%s\n",range_end);
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
			fprintf(stderr,"[E] the given range is small\n");
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
		printf("[+] N = 0x%s\n",hextemp);
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
				fprintf(stderr,"[W] ========================================================\n");
				fprintf(stderr,"[W] INSUFFICIENT MEMORY FOR BSGS PARAMETERS\n");
				fprintf(stderr,"[W] ========================================================\n");
				fprintf(stderr,"[W] Required RAM:  %" PRIu64 " MB (~%.1f GB)\n", total_required_mb, (double)total_required_mb/1024);
				fprintf(stderr,"[W] Available RAM: %" PRIu64 " MB (~%.1f GB)\n", available_ram_mb, (double)available_ram_mb/1024);
				fprintf(stderr,"[W] Safe limit:    %" PRIu64 " MB (80%% of available)\n", safe_limit_mb);
				fprintf(stderr,"[W]\n");
				fprintf(stderr,"[W] Current parameters:\n");
				fprintf(stderr,"[W]   N = 0x%" PRIx64 "\n", BSGS_N.GetInt64());
				fprintf(stderr,"[W]   K = %i\n", KFACTOR);
				fprintf(stderr,"[W]   M = %" PRIu64 " (sqrt(N))\n", bsgs_m/KFACTOR);
				fprintf(stderr,"[W]   M * K = %" PRIu64 " elements\n", bsgs_m);
				fprintf(stderr,"[W]\n");
				fprintf(stderr,"[W] AUTO-ADJUSTING PARAMETERS...\n");
				fprintf(stderr,"[W] --------------------------------------------------------\n");

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
						fprintf(stderr,"[I] Auto-adjusted to: N = 0x%" PRIx64 ", K = %d\n", new_n, new_k);
						fprintf(stderr,"[I] New RAM requirement: %" PRIu64 " MB (~%.1f GB)\n",
							test_total_mb, (double)test_total_mb/1024);
						break;
					}
				}

				if(new_n == 0) {
					// Even smallest config doesn't fit
					fprintf(stderr,"[E] ERROR: Insufficient RAM even for minimum configuration\n");
					fprintf(stderr,"[E] Minimum requires: ~1.8 GB, Available: %" PRIu64 " MB\n", available_ram_mb);
					fprintf(stderr,"[E] Cannot continue.\n");
					fprintf(stderr,"[E] ========================================================\n");
					exit(EXIT_FAILURE);
				}

				// Apply new parameters
				KFACTOR = new_k;
				BSGS_N.SetInt64(new_n);

				// Must recalculate all BSGS values - go back to start of BSGS calculations
				fprintf(stderr,"[I] Recalculating with optimized parameters...\n");
				fprintf(stderr,"[W] ========================================================\n\n");

				// Recalculate from scratch
				goto bsgs_recalculate_with_new_params;
			}
			else {
				// Parameters OK - show memory usage
				fprintf(stderr,"[I] Memory check: %" PRIu64 " MB required, %" PRIu64 " MB available (%.1f%% used)\n",
					total_required_mb, available_ram_mb,
					(double)total_required_mb * 100.0 / (double)available_ram_mb);
			}
		}
		// ==================================================================

			printf("[+] Bloom filter for %" PRIu64 " elements ",bsgs_m);
			bloom_bP = (bloom_extended_t*)calloc(256,sizeof(bloom_extended_t));
			checkpointer((void *)bloom_bP,__FILE__,"calloc","bloom_bP" ,__LINE__ -1 );
			bloom_bP_checksums = (struct checksumsha256*)calloc(256,sizeof(struct checksumsha256));
			checkpointer((void *)bloom_bP_checksums,__FILE__,"calloc","bloom_bP_checksums" ,__LINE__ -1 );
		
#if defined(_WIN64) && !defined(__CYGWIN__)
		bloom_bP_mutex = (HANDLE*) calloc(256,sizeof(HANDLE));
		
#else
		bloom_bP_mutex = (pthread_mutex_t*) calloc(256,sizeof(pthread_mutex_t));
#endif
		checkpointer((void *)bloom_bP_mutex,__FILE__,"calloc","bloom_bP_mutex" ,__LINE__ -1 );
		

		fflush(stdout);
		bloom_bP_totalbytes = 0;
			for(i=0; i< 256; i++)	{
#if defined(_WIN64) && !defined(__CYGWIN__)
				bloom_bP_mutex[i] = CreateMutex(NULL, FALSE, NULL);
#else
				pthread_mutex_init(&bloom_bP_mutex[i],NULL);
#endif
				if(bloom_ext_init(&bloom_bP[i],itemsbloom,0.000001)	!= 0){
					fprintf(stderr,"[E] error bloom_init _ [%" PRIu64 "]\n",i);
					exit(EXIT_FAILURE);
				}
				bloom_bP_totalbytes += bloom_ext_bytes(&bloom_bP[i]);
				//if(FLAGDEBUG) bloom_print(&bloom_bP[i]);
			}
		printf(": %.2f MB\n",(float)((float)(uint64_t)bloom_bP_totalbytes/(float)(uint64_t)1048576));


		printf("[+] Bloom filter for %" PRIu64 " elements ",bsgs_m2);
		
#if defined(_WIN64) && !defined(__CYGWIN__)
		bloom_bPx2nd_mutex = (HANDLE*) calloc(256,sizeof(HANDLE));
#else
		bloom_bPx2nd_mutex = (pthread_mutex_t*) calloc(256,sizeof(pthread_mutex_t));
#endif
		checkpointer((void *)bloom_bPx2nd_mutex,__FILE__,"calloc","bloom_bPx2nd_mutex" ,__LINE__ -1 );
			bloom_bPx2nd = (bloom_extended_t*)calloc(256,sizeof(bloom_extended_t));
			checkpointer((void *)bloom_bPx2nd,__FILE__,"calloc","bloom_bPx2nd" ,__LINE__ -1 );
			bloom_bPx2nd_checksums = (struct checksumsha256*) calloc(256,sizeof(struct checksumsha256));
			checkpointer((void *)bloom_bPx2nd_checksums,__FILE__,"calloc","bloom_bPx2nd_checksums" ,__LINE__ -1 );
			bloom_bP2_totalbytes = 0;
			for(i=0; i< 256; i++)	{
#if defined(_WIN64) && !defined(__CYGWIN__)
			bloom_bPx2nd_mutex[i] = CreateMutex(NULL, FALSE, NULL);
#else
			pthread_mutex_init(&bloom_bPx2nd_mutex[i],NULL);
#endif
				if(bloom_ext_init(&bloom_bPx2nd[i],itemsbloom2,0.000001)	!= 0){
					fprintf(stderr,"[E] error bloom_init _ [%" PRIu64 "]\n",i);
					exit(EXIT_FAILURE);
				}
				bloom_bP2_totalbytes += bloom_ext_bytes(&bloom_bPx2nd[i]);
				//if(FLAGDEBUG) bloom_print(&bloom_bPx2nd[i]);
			}
		printf(": %.2f MB\n",(float)((float)(uint64_t)bloom_bP2_totalbytes/(float)(uint64_t)1048576));
		

#if defined(_WIN64) && !defined(__CYGWIN__)
		bloom_bPx3rd_mutex = (HANDLE*) calloc(256,sizeof(HANDLE));
#else
		bloom_bPx3rd_mutex = (pthread_mutex_t*) calloc(256,sizeof(pthread_mutex_t));
#endif
		checkpointer((void *)bloom_bPx3rd_mutex,__FILE__,"calloc","bloom_bPx3rd_mutex" ,__LINE__ -1 );
			bloom_bPx3rd = (bloom_extended_t*)calloc(256,sizeof(bloom_extended_t));
			checkpointer((void *)bloom_bPx3rd,__FILE__,"calloc","bloom_bPx3rd" ,__LINE__ -1 );
			bloom_bPx3rd_checksums = (struct checksumsha256*) calloc(256,sizeof(struct checksumsha256));
			checkpointer((void *)bloom_bPx3rd_checksums,__FILE__,"calloc","bloom_bPx3rd_checksums" ,__LINE__ -1 );
		
		printf("[+] Bloom filter for %" PRIu64 " elements ",bsgs_m3);
		bloom_bP3_totalbytes = 0;
		for(i=0; i< 256; i++)	{
#if defined(_WIN64) && !defined(__CYGWIN__)
			bloom_bPx3rd_mutex[i] = CreateMutex(NULL, FALSE, NULL);
#else
			pthread_mutex_init(&bloom_bPx3rd_mutex[i],NULL);
#endif
				if(bloom_ext_init(&bloom_bPx3rd[i],itemsbloom3,0.000001)	!= 0){
					fprintf(stderr,"[E] error bloom_init [%" PRIu64 "]\n",i);
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

		bytes = (uint64_t)bsgs_m3 * (uint64_t) sizeof(struct bsgs_xvalue);
		printf("[+] Allocating %.2f MB for %" PRIu64  " bP Points\n",(double)(bytes/1048576),bsgs_m3);
		
		bPtable = (struct bsgs_xvalue*) malloc(bytes);
		checkpointer((void *)bPtable,__FILE__,"malloc","bPtable" ,__LINE__ -1 );
		memset(bPtable,0,bytes);
		
		if(FLAGSAVEREADFILE)	{
			/*Reading file for 1st bloom filter */

				snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_11_%" PRIu64 ".blm",bsgs_m);
				fd_aux1 = fopen(buffer_bloom_file,"rb");
				if(fd_aux1 != NULL)	{
					printf("[+] Reading bloom filter from file %s ",buffer_bloom_file);
					fflush(stdout);
					for(i = 0; i < 256;i++)	{
						struct bloom tmp_bloom;
						readed = fread(&tmp_bloom,sizeof(struct bloom),1,fd_aux1);
						if(readed != 1)	{
							fprintf(stderr,"[E] Error reading the file %s\n",buffer_bloom_file);
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
							fprintf(stderr,"[E] Error allocating memory for bloom cache %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}

						readed = fread(bloom_bP[i].orig.bf,bloom_bP[i].orig.bytes,1,fd_aux1);
						if(readed != 1)	{
							fprintf(stderr,"[E] Error reading the file %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}
						bloom_ext_sync_from_orig(&bloom_bP[i]);
						readed = fread(&bloom_bP_checksums[i],sizeof(struct checksumsha256),1,fd_aux1);
					if(readed != 1)	{
						fprintf(stderr,"[E] Error reading the file %s\n",buffer_bloom_file);
						exit(EXIT_FAILURE);
						}
						if(FLAGSKIPCHECKSUM == 0)	{
							sha256((uint8_t*)bloom_bP[i].orig.bf,bloom_bP[i].orig.bytes,(uint8_t*)rawvalue);
							if(memcmp(bloom_bP_checksums[i].data,rawvalue,32) != 0 || memcmp(bloom_bP_checksums[i].backup,rawvalue,32) != 0 )	{	/* Verification */
								fprintf(stderr,"[E] Error checksum file mismatch! %s\n",buffer_bloom_file);
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
					printf("[W] Unused file detected %s you can delete it without worry\n",buffer_bloom_file);
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
					printf("[+] Reading bloom filter from file %s ",buffer_bloom_file);
					fflush(stdout);
					for(i = 0; i < 256;i++)	{
						struct bloom tmp_bloom;
						readed = fread(&tmp_bloom,sizeof(struct bloom),1,fd_aux2);
						if(readed != 1)	{
							fprintf(stderr,"[E] Error reading the file %s\n",buffer_bloom_file);
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
							fprintf(stderr,"[E] Error allocating memory for bloom cache %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}

						readed = fread(bloom_bPx2nd[i].orig.bf,bloom_bPx2nd[i].orig.bytes,1,fd_aux2);
						if(readed != 1)	{
							fprintf(stderr,"[E] Error reading the file %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}
						bloom_ext_sync_from_orig(&bloom_bPx2nd[i]);
						readed = fread(&bloom_bPx2nd_checksums[i],sizeof(struct checksumsha256),1,fd_aux2);
					if(readed != 1)	{
						fprintf(stderr,"[E] Error reading the file %s\n",buffer_bloom_file);
						exit(EXIT_FAILURE);
					}
						memset(rawvalue,0,32);
						if(FLAGSKIPCHECKSUM == 0)	{								
							sha256((uint8_t*)bloom_bPx2nd[i].orig.bf,bloom_bPx2nd[i].orig.bytes,(uint8_t*)rawvalue);
							if(memcmp(bloom_bPx2nd_checksums[i].data,rawvalue,32) != 0 || memcmp(bloom_bPx2nd_checksums[i].backup,rawvalue,32) != 0 )	{		/* Verification */
								fprintf(stderr,"[E] Error checksum file mismatch! %s\n",buffer_bloom_file);
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
					printf("[W] Unused file detected %s you can delete it without worry\n",buffer_bloom_file);
					fclose(fd_aux2);
				}
				memset(buffer_bloom_file,0,1024);
				snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_1_%" PRIu64 ".blm",bsgs_m2);
				fd_aux2 = fopen(buffer_bloom_file,"rb");
				if(fd_aux2 != NULL)	{
					printf("[W] Unused file detected %s you can delete it without worry\n",buffer_bloom_file);
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
				printf("[+] Reading bP Table from file %s .",buffer_bloom_file);
				fflush(stdout);
				rsize = fread(bPtable,bytes,1,fd_aux3);
				if(rsize != 1)	{
					fprintf(stderr,"[E] Error reading the file %s\n",buffer_bloom_file);
					exit(EXIT_FAILURE);
				}
				rsize = fread(checksum,32,1,fd_aux3);
				if(rsize != 1)	{
					fprintf(stderr,"[E] Error reading the file %s\n",buffer_bloom_file);
					exit(EXIT_FAILURE);
				}
				if(FLAGSKIPCHECKSUM == 0)	{
					sha256((uint8_t*)bPtable,bytes,(uint8_t*)checksum_backup);
					if(memcmp(checksum,checksum_backup,32) != 0)	{
						fprintf(stderr,"[E] Error checksum file mismatch! %s\n",buffer_bloom_file);
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
					printf("[+] Reading bloom filter from file %s ",buffer_bloom_file);
					fflush(stdout);
					for(i = 0; i < 256;i++)	{
						struct bloom tmp_bloom;
						readed = fread(&tmp_bloom,sizeof(struct bloom),1,fd_aux2);
						if(readed != 1)	{
							fprintf(stderr,"[E] Error reading the file %s\n",buffer_bloom_file);
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
							fprintf(stderr,"[E] Error allocating memory for bloom cache %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}

						readed = fread(bloom_bPx3rd[i].orig.bf,bloom_bPx3rd[i].orig.bytes,1,fd_aux2);
						if(readed != 1)	{
							fprintf(stderr,"[E] Error reading the file %s\n",buffer_bloom_file);
							exit(EXIT_FAILURE);
						}
						bloom_ext_sync_from_orig(&bloom_bPx3rd[i]);
						readed = fread(&bloom_bPx3rd_checksums[i],sizeof(struct checksumsha256),1,fd_aux2);
					if(readed != 1)	{
						fprintf(stderr,"[E] Error reading the file %s\n",buffer_bloom_file);
						exit(EXIT_FAILURE);
					}
						memset(rawvalue,0,32);
						if(FLAGSKIPCHECKSUM == 0)	{							
							sha256((uint8_t*)bloom_bPx3rd[i].orig.bf,bloom_bPx3rd[i].orig.bytes,(uint8_t*)rawvalue);
							if(memcmp(bloom_bPx3rd_checksums[i].data,rawvalue,32) != 0 || memcmp(bloom_bPx3rd_checksums[i].backup,rawvalue,32) != 0 )	{		/* Verification */
								fprintf(stderr,"[E] Error checksum file mismatch! %s\n",buffer_bloom_file);
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
				printf("[I] We need to recalculate some files, don't worry this is only 3%% of the previous work\n");
				FINISHED_THREADS_COUNTER = 0;
				FINISHED_THREADS_BP = 0;
				FINISHED_ITEMS = 0;
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
				
				printf("\r[+] processing %lu/%lu bP points : %i%%\r",FINISHED_ITEMS,bsgs_m,(int) (((double)FINISHED_ITEMS/(double)bsgs_m)*100));
				fflush(stdout);
				
#if defined(_WIN64) && !defined(__CYGWIN__)
				tid = (HANDLE*)calloc(NTHREADS, sizeof(HANDLE));
				checkpointer((void *)tid,__FILE__,"calloc","tid" ,__LINE__ -1 );
				bPload_mutex = (HANDLE*) calloc(NTHREADS,sizeof(HANDLE));
#else
				tid = (pthread_t *) calloc(NTHREADS,sizeof(pthread_t));
				bPload_mutex = (pthread_mutex_t*) calloc(NTHREADS,sizeof(pthread_mutex_t));
#endif
				checkpointer((void *)bPload_mutex,__FILE__,"calloc","bPload_mutex" ,__LINE__ -1 );
				bPload_temp_ptr = (struct bPload*) calloc(NTHREADS,sizeof(struct bPload));
				checkpointer((void *)bPload_temp_ptr,__FILE__,"calloc","bPload_temp_ptr" ,__LINE__ -1 );
				bPload_threads_available = (char*) calloc(NTHREADS,sizeof(char));
				checkpointer((void *)bPload_threads_available,__FILE__,"calloc","bPload_threads_available" ,__LINE__ -1 );
				
				memset(bPload_threads_available,1,NTHREADS);
				
				for(j = 0; j < NTHREADS; j++)	{
#if defined(_WIN64) && !defined(__CYGWIN__)
					bPload_mutex[j] = CreateMutex(NULL, FALSE, NULL);
#else
					pthread_mutex_init(&bPload_mutex[j],NULL);
#endif
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
#if defined(_WIN64) && !defined(__CYGWIN__)
							tid[j] = CreateThread(NULL, 0, thread_bPload_2blooms, (void*) &bPload_temp_ptr[j], 0, &s);
#else
							s = pthread_create(&tid[j],NULL,thread_bPload_2blooms,(void*) &bPload_temp_ptr[j]);
							pthread_detach(tid[j]);
#endif
							BASE+=THREADBPWORKLOAD;
							THREADCOUNTER++;
						}
					}

					if(OLDFINISHED_ITEMS != FINISHED_ITEMS)	{
						printf("\r[+] processing %lu/%lu bP points : %i%%\r",FINISHED_ITEMS,bsgs_m2,(int) (((double)FINISHED_ITEMS/(double)bsgs_m2)*100));
						fflush(stdout);
						OLDFINISHED_ITEMS = FINISHED_ITEMS;
					}
					
					for(j = 0 ; j < NTHREADS ; j++)	{

#if defined(_WIN64) && !defined(__CYGWIN__)
						WaitForSingleObject(bPload_mutex[j], INFINITE);
						finished = bPload_temp_ptr[j].finished;
						ReleaseMutex(bPload_mutex[j]);
#else
						pthread_mutex_lock(&bPload_mutex[j]);
						finished = bPload_temp_ptr[j].finished;
						pthread_mutex_unlock(&bPload_mutex[j]);
#endif
						if(finished)	{
							bPload_temp_ptr[j].finished = 0;
							bPload_threads_available[j] = 1;
							FINISHED_ITEMS += bPload_temp_ptr[j].workload;
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
				FINISHED_ITEMS = 0;
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
				
				printf("\r[+] processing %lu/%lu bP points : %i%%\r",FINISHED_ITEMS,bsgs_m,(int) (((double)FINISHED_ITEMS/(double)bsgs_m)*100));
				fflush(stdout);
				
#if defined(_WIN64) && !defined(__CYGWIN__)
				tid = (HANDLE*)calloc(NTHREADS, sizeof(HANDLE));
				bPload_mutex = (HANDLE*) calloc(NTHREADS,sizeof(HANDLE));
#else
				tid = (pthread_t *) calloc(NTHREADS,sizeof(pthread_t));
				bPload_mutex = (pthread_mutex_t*) calloc(NTHREADS,sizeof(pthread_mutex_t));
#endif
				checkpointer((void *)tid,__FILE__,"calloc","tid" ,__LINE__ -1 );
				checkpointer((void *)bPload_mutex,__FILE__,"calloc","bPload_mutex" ,__LINE__ -1 );
				
				bPload_temp_ptr = (struct bPload*) calloc(NTHREADS,sizeof(struct bPload));
				checkpointer((void *)bPload_temp_ptr,__FILE__,"calloc","bPload_temp_ptr" ,__LINE__ -1 );
				bPload_threads_available = (char*) calloc(NTHREADS,sizeof(char));
				checkpointer((void *)bPload_threads_available,__FILE__,"calloc","bPload_threads_available" ,__LINE__ -1 );
				

				memset(bPload_threads_available,1,NTHREADS);
				
				for(j = 0; j < NTHREADS; j++)	{
#if defined(_WIN64) && !defined(__CYGWIN__)
					bPload_mutex = CreateMutex(NULL, FALSE, NULL);
#else
					pthread_mutex_init(&bPload_mutex[j],NULL);
#endif
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
							//if(FLAGDEBUG) printf("[I] %lu to %lu\n",bPload_temp_ptr[i].from,bPload_temp_ptr[i].to);
#if defined(_WIN64) && !defined(__CYGWIN__)
							tid[j] = CreateThread(NULL, 0, thread_bPload, (void*) &bPload_temp_ptr[j], 0, &s);
#else
							s = pthread_create(&tid[j],NULL,thread_bPload,(void*) &bPload_temp_ptr[j]);
							pthread_detach(tid[j]);
#endif
							BASE+=THREADBPWORKLOAD;
							THREADCOUNTER++;
						}
					}
					if(OLDFINISHED_ITEMS != FINISHED_ITEMS)	{
						printf("\r[+] processing %lu/%lu bP points : %i%%\r",FINISHED_ITEMS,bsgs_m,(int) (((double)FINISHED_ITEMS/(double)bsgs_m)*100));
						fflush(stdout);
						OLDFINISHED_ITEMS = FINISHED_ITEMS;
					}
					
					for(j = 0 ; j < NTHREADS ; j++)	{

#if defined(_WIN64) && !defined(__CYGWIN__)
						WaitForSingleObject(bPload_mutex[j], INFINITE);
						finished = bPload_temp_ptr[j].finished;
						ReleaseMutex(bPload_mutex[j]);
#else
						pthread_mutex_lock(&bPload_mutex[j]);
						finished = bPload_temp_ptr[j].finished;
						pthread_mutex_unlock(&bPload_mutex[j]);
#endif
						if(finished)	{
							bPload_temp_ptr[j].finished = 0;
							bPload_threads_available[j] = 1;
							FINISHED_ITEMS += bPload_temp_ptr[j].workload;
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
			printf("[+] Making checkums .. ");
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
			printf("[+] Sorting %lu elements... ",bsgs_m3);
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
						printf("[W] Updating old file into a new one\n");
					}
				
				/* Writing file for 1st bloom filter */
				
					fd_aux1 = fopen(buffer_bloom_file,"wb");
					if(fd_aux1 != NULL)	{
						printf("[+] Writing bloom filter to file %s ",buffer_bloom_file);
						fflush(stdout);
						for(i = 0; i < 256;i++)	{
							readed = fwrite(&bloom_bP[i].orig,sizeof(struct bloom),1,fd_aux1);
							if(readed != 1)	{
								fprintf(stderr,"[E] Error writing the file %s please delete it\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
							readed = fwrite(bloom_bP[i].orig.bf,bloom_bP[i].orig.bytes,1,fd_aux1);
							if(readed != 1)	{
								fprintf(stderr,"[E] Error writing the file %s please delete it\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
						readed = fwrite(&bloom_bP_checksums[i],sizeof(struct checksumsha256),1,fd_aux1);
						if(readed != 1)	{
							fprintf(stderr,"[E] Error writing the file %s please delete it\n",buffer_bloom_file);
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
					fprintf(stderr,"[E] Error can't create the file %s\n",buffer_bloom_file);
					exit(EXIT_FAILURE);
				}
			}
				if(!FLAGREADEDFILE2  )	{
					
					snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_12_%" PRIu64 ".blm",bsgs_m2);
									
					/* Writing file for 2nd bloom filter */
					fd_aux2 = fopen(buffer_bloom_file,"wb");
					if(fd_aux2 != NULL)	{
						printf("[+] Writing bloom filter to file %s ",buffer_bloom_file);
						fflush(stdout);
						for(i = 0; i < 256;i++)	{
							readed = fwrite(&bloom_bPx2nd[i].orig,sizeof(struct bloom),1,fd_aux2);
							if(readed != 1)	{
								fprintf(stderr,"[E] Error writing the file %s\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
							readed = fwrite(bloom_bPx2nd[i].orig.bf,bloom_bPx2nd[i].orig.bytes,1,fd_aux2);
							if(readed != 1)	{
								fprintf(stderr,"[E] Error writing the file %s\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
						readed = fwrite(&bloom_bPx2nd_checksums[i],sizeof(struct checksumsha256),1,fd_aux2);
						if(readed != 1)	{
							fprintf(stderr,"[E] Error writing the file %s please delete it\n",buffer_bloom_file);
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
					fprintf(stderr,"[E] Error can't create the file %s\n",buffer_bloom_file);
					exit(EXIT_FAILURE);
				}
			}
			
			if(!FLAGREADEDFILE3)	{
				/* Writing file for bPtable */
				snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_2_%" PRIu64 ".tbl",bsgs_m3);
				fd_aux3 = fopen(buffer_bloom_file,"wb");
				if(fd_aux3 != NULL)	{
					printf("[+] Writing bP Table to file %s .. ",buffer_bloom_file);
					fflush(stdout);
					readed = fwrite(bPtable,bytes,1,fd_aux3);
					if(readed != 1)	{
						fprintf(stderr,"[E] Error writing the file %s\n",buffer_bloom_file);
						exit(EXIT_FAILURE);
					}
					readed = fwrite(checksum,32,1,fd_aux3);
					if(readed != 1)	{
						fprintf(stderr,"[E] Error writing the file %s\n",buffer_bloom_file);
						exit(EXIT_FAILURE);
					}
					printf("Done!\n");
					fclose(fd_aux3);	
				}
				else	{
					fprintf(stderr,"[E] Error can't create the file %s\n",buffer_bloom_file);
					exit(EXIT_FAILURE);
				}
			}
				if(!FLAGREADEDFILE4)	{
					snprintf(buffer_bloom_file,1024,"keyhunt_bsgs_13_%" PRIu64 ".blm",bsgs_m3);
									
					/* Writing file for 3rd bloom filter */
					fd_aux2 = fopen(buffer_bloom_file,"wb");
					if(fd_aux2 != NULL)	{
						printf("[+] Writing bloom filter to file %s ",buffer_bloom_file);
						fflush(stdout);
						for(i = 0; i < 256;i++)	{
							readed = fwrite(&bloom_bPx3rd[i].orig,sizeof(struct bloom),1,fd_aux2);
							if(readed != 1)	{
								fprintf(stderr,"[E] Error writing the file %s\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
							readed = fwrite(bloom_bPx3rd[i].orig.bf,bloom_bPx3rd[i].orig.bytes,1,fd_aux2);
							if(readed != 1)	{
								fprintf(stderr,"[E] Error writing the file %s\n",buffer_bloom_file);
								exit(EXIT_FAILURE);
							}
						readed = fwrite(&bloom_bPx3rd_checksums[i],sizeof(struct checksumsha256),1,fd_aux2);
						if(readed != 1)	{
							fprintf(stderr,"[E] Error writing the file %s please delete it\n",buffer_bloom_file);
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
					fprintf(stderr,"[E] Error can't create the file %s\n",buffer_bloom_file);
					exit(EXIT_FAILURE);
				}
			}
		}


		i = 0;

		// Apply auto-tuned thread count ONLY if user didn't specify -t
		if (!FLAGTHREADS && NTHREADS == 1 && OPTIMAL_THREADS > 0) {
			NTHREADS = OPTIMAL_THREADS;
			printf("[I] Using auto-tuned thread count: %d (optimal for %d physical cores)\n",
			       NTHREADS, g_sysinfo.cpu_physical_cores);
		}

		steps = (struct thread_counter *) calloc(NTHREADS,sizeof(struct thread_counter));
		checkpointer((void *)steps,__FILE__,"calloc","steps" ,__LINE__ -1 );
		ends = (struct thread_flag *) calloc(NTHREADS,sizeof(struct thread_flag));
		checkpointer((void *)ends,__FILE__,"calloc","ends" ,__LINE__ -1 );
#if defined(_WIN64) && !defined(__CYGWIN__)
		tid = (HANDLE*)calloc(NTHREADS, sizeof(HANDLE));
#else
		tid = (pthread_t *) calloc(NTHREADS,sizeof(pthread_t));
#endif
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
#if defined(_WIN64) && !defined(__CYGWIN__)
				case 0:
					tid[j] = CreateThread(NULL, 0, thread_process_bsgs, (void*)tt, 0, &s);
					break;
				case 1:
					tid[j] = CreateThread(NULL, 0, thread_process_bsgs_backward, (void*)tt, 0, &s);
					break;
				case 2:
					tid[j] = CreateThread(NULL, 0, thread_process_bsgs_both, (void*)tt, 0, &s);
					break;
				case 3:
					tid[j] = CreateThread(NULL, 0, thread_process_bsgs_random, (void*)tt, 0, &s);
					break;
				case 4:
					tid[j] = CreateThread(NULL, 0, thread_process_bsgs_dance, (void*)tt, 0, &s);
					break;
				}
#else
				case 0:
					s = pthread_create(&tid[j],NULL,thread_process_bsgs,(void *)tt);
				break;
				case 1:
					s = pthread_create(&tid[j],NULL,thread_process_bsgs_backward,(void *)tt);
				break;
				case 2:
					s = pthread_create(&tid[j],NULL,thread_process_bsgs_both,(void *)tt);
				break;
				case 3:
					s = pthread_create(&tid[j],NULL,thread_process_bsgs_random,(void *)tt);
				break;
				case 4:
					s = pthread_create(&tid[j],NULL,thread_process_bsgs_dance,(void *)tt);
				break;
#endif
			}
#if defined(_WIN64) && !defined(__CYGWIN__)
			if (tid[j] == NULL) {
#else
			if(s != 0)	{
#endif
				fprintf(stderr,"[E] thread thread_process\n");
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
				printf("[I] Using auto-tuned thread count: %d\n", NTHREADS);
			}
		steps = (struct thread_counter *) calloc(NTHREADS,sizeof(struct thread_counter));
		checkpointer((void *)steps,__FILE__,"calloc","steps" ,__LINE__ -1 );
		ends = (struct thread_flag *) calloc(NTHREADS,sizeof(struct thread_flag));
		checkpointer((void *)ends,__FILE__,"calloc","ends" ,__LINE__ -1 );
#if defined(_WIN64) && !defined(__CYGWIN__)
		tid = (HANDLE*)calloc(NTHREADS, sizeof(HANDLE));
#else
		tid = (pthread_t *) calloc(NTHREADS,sizeof(pthread_t));
#endif
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
				printf("[+] Running GPU full search mode...\n");
	
					// Reset stats
					__atomic_store_n(&g_gpu_keys_checked, 0, __ATOMIC_RELEASE);
					__atomic_store_n(&g_gpu_keys_checked_cur, 0, __ATOMIC_RELEASE);
					g_gpu_should_stop = 0;

#ifndef _WIN64
				pthread_t gpu_stats_tid;
				int gpu_stats_started = 0;
				volatile int gpu_stats_stop = 0;
				gpu_full_stats_args_t gpu_stats_args;
				memset(&gpu_stats_args, 0, sizeof(gpu_stats_args));
				if (OUTPUTSECONDS.IsGreater(&ZERO)) {
					gpu_stats_args.period_seconds = OUTPUTSECONDS.GetInt32();
					gpu_stats_args.stop_flag = &gpu_stats_stop;
					if (gpu_stats_args.period_seconds > 0) {
						if (pthread_create(&gpu_stats_tid, NULL, gpu_full_stats_thread, &gpu_stats_args) == 0) {
							gpu_stats_started = 1;
						}
					}
				}
#endif
	
				// Run GPU search
				int gpu_result = gpu_run_full_search(&n_range_start, &n_range_end, &stride, N);

#ifndef _WIN64
				gpu_stats_stop = 1;
				if (gpu_stats_started) {
					pthread_join(gpu_stats_tid, NULL);
				}
#endif
	
				if (gpu_result >= 0) {
					// GPU search completed successfully
					printf("[+] GPU search finished. Keys found: %d\n", gpu_result);
					printf("[+] Total keys checked: %" PRIu64 "\n", g_gpu_keys_checked);

				// Cleanup and exit
#ifndef _WIN64
				shutdown_work_queue();
#endif
				gpu_backend_shutdown();
				printf("[+] Done!\n");
				return 0;
			} else {
				// GPU search failed, fall back to CPU
				fprintf(stderr, "[W] GPU search failed, falling back to CPU threads\n");
				FLAGGPU_FULL = 0;
			}
		}

		// ============================================================================
		// GPU Hybrid Mode (GPU + CPU in parallel with STATIC SPLIT)
		// GPU gets g_gpu_range_percent% of range, CPU uses normal fast algorithm
		// ============================================================================
			if (FLAGGPU_HYBRID && (FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160)) {
				if (!gpu_backend_available()) {
					fprintf(stderr, "[W] GPU not available for hybrid mode, falling back to CPU-only\n");
					FLAGGPU_HYBRID = 0;
					} else {
						const char *ws = getenv("KEYHUNT_HYBRID_WORK_STEAL");
						const bool want_work_steal = (ws && *ws && atoi(ws) != 0);
						const bool can_work_steal = want_work_steal && !FLAGRANDOM && stride.IsOne();
						if (want_work_steal && !can_work_steal) {
							fprintf(stderr, "[W] HYBRID: work-stealing requires non-random mode and stride=1; using static split\n");
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

							printf("[+] Running GPU+CPU hybrid mode (work-stealing)...\n");
							printf("[I] HYBRID: work-stealing enabled (block size: 0x%llx, override: KEYHUNT_HYBRID_BLOCK_SIZE)\n",
							       (unsigned long long)block_size);

							g_work_pool.init(&n_range_start, &n_range_end, block_size);

							// Setup GPU thread (range args ignored in work-stealing mode)
							gpu_hybrid_args.start_key.Set(&n_range_start);
							gpu_hybrid_args.end_key.Set(&n_range_end);
							gpu_hybrid_args.stride.Set(&stride);
							gpu_hybrid_args.target_count = N;
							gpu_hybrid_args.result = 0;
							gpu_hybrid_args.completed = 0;

								// Reset GPU stats
								__atomic_store_n(&g_gpu_keys_checked, 0, __ATOMIC_RELEASE);
								__atomic_store_n(&g_gpu_keys_checked_cur, 0, __ATOMIC_RELEASE);
								g_gpu_should_stop = 0;

							int err = pthread_create(&gpu_thread_id, NULL, gpu_hybrid_thread, &gpu_hybrid_args);
							if (err != 0) {
								fprintf(stderr, "[W] Failed to start GPU thread, falling back to CPU-only\n");
								g_work_pool.disable();
								FLAGGPU_HYBRID = 0;
							} else {
								gpu_hybrid_started = 1;
								printf("[+] GPU thread started, CPU uses normal fast algorithm\n");
							}
						} else {
							printf("[+] Running GPU+CPU hybrid mode (static split)...\n");

					// Auto-tune the split unless user overrides with KEYHUNT_HYBRID_GPU_PERCENT.
					{
						const char *env = getenv("KEYHUNT_HYBRID_GPU_PERCENT");
						if (!(env && *env)) {
							int tuned = hybrid_get_gpu_range_percent_default(NTHREADS);
							if (tuned != g_gpu_range_percent) {
								g_gpu_range_percent = tuned;
								printf("[I] HYBRID: auto split GPU %d%% / CPU %d%% (override: KEYHUNT_HYBRID_GPU_PERCENT)\n",
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

				printf("[+] GPU handles %d%% of range, CPU handles %d%%\n",
					   g_gpu_range_percent, 100 - g_gpu_range_percent);

				// Print ranges in inclusive form for readability.
				char *hextemp = n_range_start.GetBase16();
				printf("[+] GPU range: 0x%s", hextemp);
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
				printf("[+] CPU range: 0x%s", hextemp);
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
			gpu_hybrid_args.result = 0;
			gpu_hybrid_args.completed = 0;

				// Reset GPU stats
				__atomic_store_n(&g_gpu_keys_checked, 0, __ATOMIC_RELEASE);
				__atomic_store_n(&g_gpu_keys_checked_cur, 0, __ATOMIC_RELEASE);
				g_gpu_should_stop = 0;

			// Start GPU thread (with its fixed range)
			int err = pthread_create(&gpu_thread_id, NULL, gpu_hybrid_thread, &gpu_hybrid_args);
			if (err != 0) {
				fprintf(stderr, "[W] Failed to start GPU thread, falling back to CPU-only\n");
				FLAGGPU_HYBRID = 0;
					} else {
						gpu_hybrid_started = 1;
						// Update n_range_start for CPU threads - they use normal algorithm
						n_range_start.Set(&cpu_range_start);
							maybe_adjust_cpu_sequential_max((size_t)NTHREADS, cpu_range_start, n_range_end,
							                                "KEYHUNT_HYBRID_CPU_N", "HYBRID");
							printf("[+] GPU thread started, CPU uses normal fast algorithm\n");
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
#if defined(_WIN64) && !defined(__CYGWIN__)
				case MODE_ADDRESS:
				case MODE_XPOINT:
				case MODE_RMD160:
					tid[j] = CreateThread(NULL, 0, thread_process, (void*)tt, 0, &s);
				break;
				case MODE_MINIKEYS:
					tid[j] = CreateThread(NULL, 0, thread_process_minikeys, (void*)tt, 0, &s);
				break;
				case MODE_VANITY:
					tid[j] = CreateThread(NULL, 0, thread_process_vanity, (void*)tt, 0, &s);
				break;
#else
				case MODE_ADDRESS:
				case MODE_XPOINT:
				case MODE_RMD160:
					s = pthread_create(&tid[j],NULL,thread_process,(void *)tt);
				break;
				case MODE_MINIKEYS:
					s = pthread_create(&tid[j],NULL,thread_process_minikeys,(void *)tt);
				break;
				case MODE_VANITY:
					s = pthread_create(&tid[j],NULL,thread_process_vanity,(void *)tt);
				break;
#endif
			}
			if(s != 0)	{
				fprintf(stderr,"[E] pthread_create thread_process\n");
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
	#ifdef _WIN64
					WaitForSingleObject(bsgs_thread, INFINITE);
	#else
					pthread_mutex_lock(&bsgs_thread);
	#endif
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
						printf("%s", buffer);
						fflush(stdout);
						THREADOUTPUT = 0;

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
									sprintf(buffer,"[+] Total %s keys in %s seconds: %s keys/s\n",str_total,str_seconds,str_pretotal);
								}
								else	{
									sprintf(buffer,"\r[+] Total %s keys in %s seconds: %s keys/s\r",str_total,str_seconds,str_pretotal);
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
									sprintf(buffer,"[+] Total %s keys in %s seconds: ~%s %s (%s keys/s)\n",str_total,str_seconds,str_divpretotal,str_limits_prefixs[salir ? i : i-1],str_pretotal);
								}
								else	{
									if(THREADOUTPUT == 1)	{
										sprintf(buffer,"\r[+] Total %s keys in %s seconds: ~%s %s (%s keys/s)\r",str_total,str_seconds,str_divpretotal,str_limits_prefixs[salir ? i : i-1],str_pretotal);
								}
								else	{
									sprintf(buffer,"\r[+] Total %s keys in %s seconds: ~%s %s (%s keys/s)\r",str_total,str_seconds,str_divpretotal,str_limits_prefixs[salir ? i : i-1],str_pretotal);
								}
							}
							free(str_divpretotal);

						}
						append_progress_info(buffer, sizeof(buffer));
						append_profile_info(buffer, sizeof(buffer));
						printf("%s",buffer);
						fflush(stdout);
						THREADOUTPUT = 0;

						free(str_seconds);
						free(str_pretotal);
						free(str_total);
					}
	#ifdef _WIN64
					ReleaseMutex(bsgs_thread);
	#else
					pthread_mutex_unlock(&bsgs_thread);
	#endif
				}
			}
		}while(continue_flag);

		// Wait for GPU thread if hybrid mode was started
		if (FLAGGPU_HYBRID && gpu_hybrid_started) {
			printf("\n[+] Waiting for GPU thread to complete...\n");
			pthread_join(gpu_thread_id, NULL);

			printf("[+] GPU thread finished. Result: %d keys found\n", gpu_hybrid_args.result);
			printf("[+] GPU keys checked: %" PRIu64 "\n", gpu_keys_checked_total_u64());

			// Cleanup GPU
			gpu_backend_shutdown();
			if (g_work_pool.enabled) {
				g_work_pool.disable();
			}
		}

	printf("\nEnd\n");
#ifndef _WIN64
	shutdown_work_queue();
#endif
#ifdef _WIN64
	CloseHandle(write_keys);
	CloseHandle(write_random);
	CloseHandle(bsgs_thread);
#endif
}

void pubkeytopubaddress_dst(char *pkey,int length,char *dst)	{
	char digest[60];
	size_t pubaddress_size = 40;
	sha256((uint8_t*)pkey, length,(uint8_t*) digest);
	ripemd160_32((const unsigned char*)digest,(unsigned char*)(digest+1));
	digest[0] = 0;
	sha256((uint8_t*)digest, 21,(uint8_t*) digest+21);
	sha256((uint8_t*)digest+21, 32,(uint8_t*) digest+21);
	if(!b58enc(dst,&pubaddress_size,digest,25)){
		fprintf(stderr,"error b58enc\n");
	}
}

void rmd160toaddress_dst(char *rmd,char *dst){
	char digest[60];
	size_t pubaddress_size = 40;
	digest[0] = byte_encode_crypto;
	memcpy(digest+1,rmd,20);
	sha256((uint8_t*)digest, 21,(uint8_t*) digest+21);
	sha256((uint8_t*)digest+21, 32,(uint8_t*) digest+21);
	if(!b58enc(dst,&pubaddress_size,digest,25)){
		fprintf(stderr,"error b58enc\n");
	}
}


char *pubkeytopubaddress(char *pkey,int length)	{
	char *pubaddress = (char*) calloc(MAXLENGTHADDRESS+10,1);
	char *digest = (char*) calloc(60,1);
	size_t pubaddress_size = MAXLENGTHADDRESS+10;
	checkpointer((void *)pubaddress,__FILE__,"malloc","pubaddress" ,__LINE__ -1 );
	checkpointer((void *)digest,__FILE__,"malloc","digest" ,__LINE__ -1 );
	//digest [000...0]
 	sha256((uint8_t*)pkey, length,(uint8_t*) digest);
	//digest [SHA256 32 bytes+000....0]
	ripemd160_32((const unsigned char*)digest,(unsigned char*)(digest+1));
	//digest [? +RMD160 20 bytes+????000....0]
	digest[0] = 0;
	//digest [0 +RMD160 20 bytes+????000....0]
	sha256((uint8_t*)digest, 21,(uint8_t*) digest+21);
	//digest [0 +RMD160 20 bytes+SHA256 32 bytes+....0]
	sha256((uint8_t*)digest+21, 32,(uint8_t*) digest+21);
	//digest [0 +RMD160 20 bytes+SHA256 32 bytes+....0]
	if(!b58enc(pubaddress,&pubaddress_size,digest,25)){
		fprintf(stderr,"error b58enc\n");
	}
	free(digest);
	return pubaddress;	// pubaddress need to be free by te caller funtion
}

static inline uint64_t load_u64_be(const void *p) {
	uint64_t v;
	memcpy(&v, p, sizeof(v));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	v = __builtin_bswap64(v);
#endif
	return v;
}

static inline uint32_t load_u32_be(const void *p) {
	uint32_t v;
	memcpy(&v, p, sizeof(v));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	v = __builtin_bswap32(v);
#endif
	return v;
}

static inline int cmp_hash20(const uint8_t *a, const uint8_t *b) {
	const uint64_t a0 = load_u64_be(a);
	const uint64_t b0 = load_u64_be(b);
	if (a0 < b0) return -1;
	if (a0 > b0) return 1;
	const uint64_t a1 = load_u64_be(a + 8);
	const uint64_t b1 = load_u64_be(b + 8);
	if (a1 < b1) return -1;
	if (a1 > b1) return 1;
	const uint32_t a2 = load_u32_be(a + 16);
	const uint32_t b2 = load_u32_be(b + 16);
	if (a2 < b2) return -1;
	if (a2 > b2) return 1;
	return 0;
}

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

int searchbinary(struct address_value *buffer,char *data,int64_t array_length) {
	if (array_length <= 0) return 0;
	int64_t lo = 0;
	int64_t hi = array_length; // exclusive
	while (lo < hi) {
		const int64_t mid = lo + ((hi - lo) >> 1);
		const int rcmp = cmp_hash20((const uint8_t*)data, (const uint8_t*)buffer[mid].value);
		if (rcmp == 0) return 1;
		if (rcmp < 0) hi = mid;
		else lo = mid + 1;
	}
	return 0;
}

#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process_minikeys(LPVOID vargp) {
#else
void *thread_process_minikeys(void *vargp)	{
#endif
	FILE *keys;
	Point publickey[4];
	Int key_mpz[4];
	struct tothread *tt;
	uint64_t count;
	char publickeyhashrmd160_uncompress[4][20];
	char public_key_uncompressed_hex[131];
	char address[4][40],minikey[4][24],minikeys[8][24],buffer_b58[21],minikey2check[24],rawvalue[4][32];
	char *hextemp,*rawbuffer;
	int r,thread_number,continue_flag = 1,k,j,count_valid;
	Int counter;
	tt = (struct tothread *)vargp;
	thread_number = tt->nt;
	free(tt);
	profile_set_thread(thread_number);
	rawbuffer = (char*) &counter.bits64;
	count_valid = 0;
	for(k = 0; k < 4; k++)	{
		minikey[k][0] = 'S';
		minikey[k][22] = '?';
		minikey[k][23] = 0x00;
	}
	minikey2check[0] = 'S';
	minikey2check[22] = '?';
	minikey2check[23] = 0x00;
	
	do	{
		if(FLAGRANDOM)	{
			counter.Rand(256);
			for(k = 0; k < 21; k++)	{
				buffer_b58[k] =(uint8_t)((uint8_t) rawbuffer[k] % 58);
			}
		}
		else	{
			if(FLAGBASEMINIKEY)	{
#if defined(_WIN64) && !defined(__CYGWIN__)
				WaitForSingleObject(write_random, INFINITE);
				memcpy(buffer_b58,raw_baseminikey,21);
				increment_minikey_N(raw_baseminikey);
				ReleaseMutex(write_random);
#else
				pthread_mutex_lock(&write_random);
				memcpy(buffer_b58,raw_baseminikey,21);
				increment_minikey_N(raw_baseminikey);
				pthread_mutex_unlock(&write_random);
#endif
			}
			else	{
#if defined(_WIN64) && !defined(__CYGWIN__)
				WaitForSingleObject(write_random, INFINITE);
#else
				pthread_mutex_lock(&write_random);
#endif
				if(raw_baseminikey == NULL){
					raw_baseminikey = (char *) malloc(22);
					checkpointer((void *)raw_baseminikey,__FILE__,"malloc","raw_baseminikey" ,__LINE__ -1 );
					counter.Rand(256);
					for(k = 0; k < 21; k++)	{
						raw_baseminikey[k] =(uint8_t)((uint8_t) rawbuffer[k] % 58);
					}
					memcpy(buffer_b58,raw_baseminikey,21);
					increment_minikey_N(raw_baseminikey);

				}
				else	{
					memcpy(buffer_b58,raw_baseminikey,21);
					increment_minikey_N(raw_baseminikey);
				}
#if defined(_WIN64) && !defined(__CYGWIN__)				
				ReleaseMutex(write_random);
#else
				pthread_mutex_unlock(&write_random);
#endif
				
			}
		}
		set_minikey(minikey2check+1,buffer_b58,21);
		if(continue_flag)	{
			count = 0;
			if(FLAGMATRIX)	{
					printf("[+] Base minikey: %s     \n",minikey2check);
					fflush(stdout);
			}
			else	{
				if(!FLAGQUIET)	{
					printf("\r[+] Base minikey: %s     \r",minikey2check);
					fflush(stdout);
				}
			}
			do {
				for(j = 0;j<256; j++)	{
					
					if(count_valid > 0)	{
						for(k = 0; k < count_valid ; k++)	{
							memcpy(minikeys[k],minikeys[4+k],22);
						}
					}
					do	{
						increment_minikey_index(minikey2check+1,buffer_b58,20);
						memcpy(minikey[0]+1,minikey2check+1,21);
						increment_minikey_index(minikey2check+1,buffer_b58,20);
						memcpy(minikey[1]+1,minikey2check+1,21);
						increment_minikey_index(minikey2check+1,buffer_b58,20);
						memcpy(minikey[2]+1,minikey2check+1,21);
						increment_minikey_index(minikey2check+1,buffer_b58,20);
						memcpy(minikey[3]+1,minikey2check+1,21);
						
						sha256sse_23((uint8_t*)minikey[0],(uint8_t*)minikey[1],(uint8_t*)minikey[2],(uint8_t*)minikey[3],(uint8_t*)rawvalue[0],(uint8_t*)rawvalue[1],(uint8_t*)rawvalue[2],(uint8_t*)rawvalue[3]);
						for(k = 0; k < 4; k++){
							if(rawvalue[k][0] == 0x00)	{
								memcpy(minikeys[count_valid],minikey[k],22);
								count_valid++;
							}
						}
					}while(count_valid < 4);
					count_valid-=4;				
					sha256sse_22((uint8_t*)minikeys[0],(uint8_t*)minikeys[1],(uint8_t*)minikeys[2],(uint8_t*)minikeys[3],(uint8_t*)rawvalue[0],(uint8_t*)rawvalue[1],(uint8_t*)rawvalue[2],(uint8_t*)rawvalue[3]);
					
					for(k = 0; k < 4; k++)	{
						key_mpz[k].Set32Bytes((uint8_t*)rawvalue[k]);
						publickey[k] = secp->ComputePublicKey(&key_mpz[k]);
					}
					
					secp->GetHash160(P2PKH,false,publickey[0],publickey[1],publickey[2],publickey[3],(uint8_t*)publickeyhashrmd160_uncompress[0],(uint8_t*)publickeyhashrmd160_uncompress[1],(uint8_t*)publickeyhashrmd160_uncompress[2],(uint8_t*)publickeyhashrmd160_uncompress[3]);
					
					for(k = 0; k < 4; k++)	{
						r = bloom_ext_check_rmd160(&bloom, (uint8_t*)publickeyhashrmd160_uncompress[k]);
						if(r) {
							r = searchbinary(addressTable,publickeyhashrmd160_uncompress[k],N);
							if(r) {
								/* hit */
								hextemp = key_mpz[k].GetBase16();
								secp->GetPublicKeyHex(false,publickey[k],public_key_uncompressed_hex);
#if defined(_WIN64) && !defined(__CYGWIN__)
								WaitForSingleObject(write_keys, INFINITE);
#else
								pthread_mutex_lock(&write_keys);
#endif
							
								keys = fopen("KEYFOUNDKEYFOUND.txt","a+");
								rmd160toaddress_dst(publickeyhashrmd160_uncompress[k],address[k]);
								minikeys[k][22] = '\0';
								if(keys != NULL)	{
									fprintf(keys,"Private Key: %s\npubkey: %s\nminikey: %s\naddress: %s\n",hextemp,public_key_uncompressed_hex,minikeys[k],address[k]);
									fclose(keys);
								}
								printf("\nHIT!! Private Key: %s\npubkey: %s\nminikey: %s\naddress: %s\n",hextemp,public_key_uncompressed_hex,minikeys[k],address[k]);
#if defined(_WIN64) && !defined(__CYGWIN__)
								ReleaseMutex(write_keys);
#else
								pthread_mutex_unlock(&write_keys);
#endif
								
								free(hextemp);
							}
						}
					}
				}
				steps[thread_number].value++;
				count+=1024;
			}while(count < N_SEQUENTIAL_MAX && continue_flag);
		}
	}while(continue_flag);
	return NULL;
}


#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process(LPVOID vargp) {
#else
void *thread_process(void *vargp)	{
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
	
	bool calculate_y = FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH || FLAGCRYPTO  == CRYPTO_ETH;
	calculate_y = calculate_y || cpu_use_y_parity_for_compressed_btc();
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
				if(FLAGQUIET == 0){
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
				
				if(calculate_y)	{
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
								
				if (prof) prof->ns_ec += (profile_now_ns() - ec_start);
				if((FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_ADDRESS) && FLAGCRYPTO == CRYPTO_BTC && !FLAGENDOMORPHISM) {
					process_rmd160_batch_btc_simple(key_mpz, pts, count);
				}
				else {
				for(j = 0; j < CPU_GRP_SIZE/4;j++){
					switch(FLAGMODE)	{
						case MODE_RMD160:
						case MODE_ADDRESS:
							if(FLAGCRYPTO == CRYPTO_BTC){
								
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
								if(FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH){
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
							}								
							else if(FLAGCRYPTO == CRYPTO_ETH){
								if(FLAGENDOMORPHISM)	{
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


					switch(FLAGMODE)	{
						case MODE_RMD160:
						case MODE_ADDRESS:
							if( FLAGCRYPTO  == CRYPTO_BTC) {
								
								for(k = 0; k < 4;k++)	{
									if(FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH){
										if(FLAGENDOMORPHISM)	{
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

									if(FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH)	{
										if(FLAGENDOMORPHISM)	{
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
							else if( FLAGCRYPTO == CRYPTO_ETH) {
								if(FLAGENDOMORPHISM)	{
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
								if(FLAGENDOMORPHISM)	{
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

void _swap(struct address_value *a,struct address_value *b)	{
	struct address_value t;
	t  = *a;
	*a = *b;
	*b =  t;
}

void _sort(struct address_value *arr,int64_t n)	{
	uint32_t depthLimit = ((uint32_t) ceil(log(n))) * 2;
	_introsort(arr,depthLimit,n);
}

void _introsort(struct address_value *arr,uint32_t depthLimit, int64_t n) {
	int64_t p;
	if(n > 1)	{
		if(n <= 16) {
			_insertionsort(arr,n);
		}
		else	{
			if(depthLimit == 0) {
				_myheapsort(arr,n);
			}
			else	{
				p = _partition(arr,n);
				if(p > 0) _introsort(arr , depthLimit-1 , p);
				if(p < n) _introsort(&arr[p+1],depthLimit-1,n-(p+1));
			}
		}
	}
}

void _insertionsort(struct address_value *arr, int64_t n) {
	int64_t j;
	int64_t i;
	struct address_value key;
	for(i = 1; i < n ; i++ ) {
		key = arr[i];
		j= i-1;
		while(j >= 0 && memcmp(arr[j].value,key.value,20) > 0) {
			arr[j+1] = arr[j];
			j--;
		}
		arr[j+1] = key;
	}
}

int64_t _partition(struct address_value *arr, int64_t n)	{
	struct address_value pivot;
	int64_t r,left,right;
	r = n/2;
	pivot = arr[r];
	left = 0;
	right = n-1;
	do {
		while(left	< right && memcmp(arr[left].value,pivot.value,20) <= 0 )	{
			left++;
		}
		while(right >= left && memcmp(arr[right].value,pivot.value,20) > 0)	{
			right--;
		}
		if(left < right)	{
			if(left == r || right == r)	{
				if(left == r)	{
					r = right;
				}
				if(right == r)	{
					r = left;
				}
			}
			_swap(&arr[right],&arr[left]);
		}
	}while(left < right);
	if(right != r)	{
		_swap(&arr[right],&arr[r]);
	}
	return right;
}

void _heapify(struct address_value *arr, int64_t n, int64_t i) {
	int64_t largest = i;
	int64_t l = 2 * i + 1;
	int64_t r = 2 * i + 2;
	if (l < n && memcmp(arr[l].value,arr[largest].value,20) > 0)
		largest = l;
	if (r < n && memcmp(arr[r].value,arr[largest].value,20) > 0)
		largest = r;
	if (largest != i) {
		_swap(&arr[i],&arr[largest]);
		_heapify(arr, n, largest);
	}
}

void _myheapsort(struct address_value	*arr, int64_t n)	{
	int64_t i;
	for ( i = (n / 2) - 1; i >=	0; i--)	{
		_heapify(arr, n, i);
	}
	for ( i = n - 1; i > 0; i--) {
		_swap(&arr[0] , &arr[i]);
		_heapify(arr, i, 0);
	}
}

/*	OK	*/
void bsgs_swap(struct bsgs_xvalue *a,struct bsgs_xvalue *b)	{
	struct bsgs_xvalue t;
	t	= *a;
	*a = *b;
	*b =	t;
}

/*	OK	*/
void bsgs_sort(struct bsgs_xvalue *arr,int64_t n)	{
	uint32_t depthLimit = ((uint32_t) ceil(log(n))) * 2;
	bsgs_introsort(arr,depthLimit,n);
}

/*	OK	*/
void bsgs_introsort(struct bsgs_xvalue *arr,uint32_t depthLimit, int64_t n) {
	int64_t p;
	if(n > 1)	{
		if(n <= 16) {
			bsgs_insertionsort(arr,n);
		}
		else	{
			if(depthLimit == 0) {
				bsgs_myheapsort(arr,n);
			}
			else	{
				p = bsgs_partition(arr,n);
				if(p > 0) bsgs_introsort(arr , depthLimit-1 , p);
				if(p < n) bsgs_introsort(&arr[p+1],depthLimit-1,n-(p+1));
			}
		}
	}
}

/*	OK - Optimized with uint64_t comparison	*/
void bsgs_insertionsort(struct bsgs_xvalue *arr, int64_t n) {
	int64_t j;
	int64_t i;
	struct bsgs_xvalue key;
	for(i = 1; i < n ; i++ ) {
		key = arr[i];
		j= i-1;
		while(j >= 0 && arr[j].value > key.value) {
			arr[j+1] = arr[j];
			j--;
		}
		arr[j+1] = key;
	}
}

/*	Optimized with uint64_t comparison	*/
int64_t bsgs_partition(struct bsgs_xvalue *arr, int64_t n)	{
	struct bsgs_xvalue pivot;
	int64_t r,left,right;
	r = n/2;
	pivot = arr[r];
	left = 0;
	right = n-1;
	do {
		while(left < right && arr[left].value <= pivot.value) {
			left++;
		}
		while(right >= left && arr[right].value > pivot.value) {
			right--;
		}
		if(left < right)	{
			if(left == r || right == r)	{
				if(left == r)	{
					r = right;
				}
				if(right == r)	{
					r = left;
				}
			}
			bsgs_swap(&arr[right],&arr[left]);
		}
	}while(left < right);
	if(right != r)	{
		bsgs_swap(&arr[right],&arr[r]);
	}
	return right;
}

/*	Optimized with uint64_t comparison	*/
void bsgs_heapify(struct bsgs_xvalue *arr, int64_t n, int64_t i) {
	int64_t largest = i;
	int64_t l = 2 * i + 1;
	int64_t r = 2 * i + 2;
	if (l < n && arr[l].value > arr[largest].value)
		largest = l;
	if (r < n && arr[r].value > arr[largest].value)
		largest = r;
	if (largest != i) {
		bsgs_swap(&arr[i],&arr[largest]);
		bsgs_heapify(arr, n, largest);
	}
}

void bsgs_myheapsort(struct bsgs_xvalue	*arr, int64_t n)	{
	int64_t i;
	for ( i = (n / 2) - 1; i >=	0; i--)	{
		bsgs_heapify(arr, n, i);
	}
	for ( i = n - 1; i > 0; i--) {
		bsgs_swap(&arr[0] , &arr[i]);
		bsgs_heapify(arr, i, 0);
	}
}

/*	Optimized with uint64_t comparison	*/
int bsgs_searchbinary(struct bsgs_xvalue *buffer,char *data,int64_t array_length,uint64_t *r_value) {
	if (array_length <= 0) return 0;
	int64_t lo = 0;
	int64_t hi = array_length; // exclusive
	int r = 0;

	// Load search key as uint64_t (bytes 16-23 of X coordinate)
	uint64_t search_key;
	memcpy(&search_key, data + 16, 8);

	while (lo < hi) {
		const int64_t mid = lo + ((hi - lo) >> 1);
		const uint64_t table_value = buffer[mid].value;
		if (search_key == table_value) {
			*r_value = buffer[mid].index;
			r = 1;
			break;
		}
		if (search_key < table_value) hi = mid;
		else lo = mid + 1;
	}
	return r;
}

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
#if defined(_WIN64) && !defined(__CYGWIN__)
		WaitForSingleObject(bsgs_thread, INFINITE);
#else
		pthread_mutex_lock(&bsgs_thread);
#endif

		base_key.Set(&BSGS_CURRENT);	/* we need to set our base_key to the current BSGS_CURRENT value*/
		BSGS_CURRENT.Add(&BSGS_N_double);		/*Then add 2*BSGS_N to BSGS_CURRENT*/
		/*
		BSGS_CURRENT.Add(&BSGS_N);		//Then add BSGS_N to BSGS_CURRENT
		BSGS_CURRENT.Add(&BSGS_N);		//Then add BSGS_N to BSGS_CURRENT
		*/
		
#if defined(_WIN64) && !defined(__CYGWIN__)
		ReleaseMutex(bsgs_thread);
#else
		pthread_mutex_unlock(&bsgs_thread);
#endif

		if(base_key.IsGreaterOrEqual(&n_range_end))
			break;
		
		if(FLAGMATRIX)	{
			aux_c = base_key.GetBase16();
			printf("[+] Thread 0x%s \n",aux_c);
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
#if 0
  pp.y.ModSub(&GSn[i].x,&pp.x);
  pp.y.ModMulK1(&_s);
  pp.y.ModSub(&GSn[i].y);           // ry = - p2.y - s*(ret.x-p2.x);  
#endif
						// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
						dyn.Set(&GSn[i].y);
						dyn.ModNeg();
						dyn.ModSub(&pn.y);

						_s.ModMulK1(&dyn,&dx[i]);       // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
						_p.ModSquareK1(&_s);            // _p = pow2(s)

						pn.x.ModNeg();
						pn.x.ModAdd(&_p);
						pn.x.ModSub(&GSn[i].x);          // rx = pow2(s) - p1.x - p2.x;

#if 0
  pn.y.ModSub(&GSn[i].x,&pn.x);
  pn.y.ModMulK1(&_s);
  pn.y.ModAdd(&GSn[i].y);          // ry = - p2.y - s*(ret.x-p2.x);  
#endif

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

#if 0
pn.y.ModSub(&GSn[i].x,&pn.x);
pn.y.ModMulK1(&_s);
pn.y.ModAdd(&GSn[i].y);
#endif
					pts[0] = pn;
					for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k]== 0; i++) {
						pts[i].x.GetHi16Bytes(xpoint_raw);
						r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
						if(r) {
							r = bsgs_secondcheck(&base_key,((j*1024) + i),k,&keyfound);
							if(r)	{
								hextemp = keyfound.GetBase16();
								printf("[+] Thread Key found privkey %s   \n",hextemp);
								point_found = secp->ComputePublicKey(&keyfound);
								aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
								printf("[+] Publickey %s\n",aux_c);
#if defined(_WIN64) && !defined(__CYGWIN__)
								WaitForSingleObject(write_keys, INFINITE);
#else
								pthread_mutex_lock(&write_keys);
#endif

								filekey = fopen("KEYFOUNDKEYFOUND.txt","a");
								if(filekey != NULL)	{
									fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
									fclose(filekey);
								}
								free(hextemp);
								free(aux_c);
#if defined(_WIN64) && !defined(__CYGWIN__)
				ReleaseMutex(write_keys);
#else
				pthread_mutex_unlock(&write_keys);
#endif
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
#if defined(_WIN64) && !defined(__CYGWIN__)
		WaitForSingleObject(bsgs_thread, INFINITE);
#else
		pthread_mutex_lock(&bsgs_thread);
#endif

		base_key.Rand(&n_range_start,&n_range_end);
#if defined(_WIN64) && !defined(__CYGWIN__)
		ReleaseMutex(bsgs_thread);
#else
		pthread_mutex_unlock(&bsgs_thread);
#endif

		if(FLAGMATRIX)	{
				aux_c = base_key.GetBase16();
				printf("[+] Thread 0x%s  \n",aux_c);
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
						
#if 0
  pp.y.ModSub(&GSn[i].x,&pp.x);
  pp.y.ModMulK1(&_s);
  pp.y.ModSub(&GSn[i].y);           // ry = - p2.y - s*(ret.x-p2.x);  
#endif

						// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
						dyn.Set(&GSn[i].y);
						dyn.ModNeg();
						dyn.ModSub(&pn.y);

						_s.ModMulK1(&dyn,&dx[i]);       // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
						_p.ModSquareK1(&_s);            // _p = pow2(s)

						pn.x.ModNeg();
						pn.x.ModAdd(&_p);
						pn.x.ModSub(&GSn[i].x);          // rx = pow2(s) - p1.x - p2.x;

#if 0
  pn.y.ModSub(&GSn[i].x,&pn.x);
  pn.y.ModMulK1(&_s);
  pn.y.ModAdd(&GSn[i].y);          // ry = - p2.y - s*(ret.x-p2.x);  
#endif


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

#if 0
pn.y.ModSub(&GSn[i].x,&pn.x);
pn.y.ModMulK1(&_s);
pn.y.ModAdd(&GSn[i].y);
#endif

					pts[0] = pn;

					for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k]== 0; i++) {
						pts[i].x.GetHi16Bytes(xpoint_raw);
						r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
						if(r) {
							r = bsgs_secondcheck(&base_key,((j*1024) + i),k,&keyfound);
							if(r)	{
								hextemp = keyfound.GetBase16();
								printf("[+] Thread Key found privkey %s    \n",hextemp);
								point_found = secp->ComputePublicKey(&keyfound);
								aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
								printf("[+] Publickey %s\n",aux_c);
#if defined(_WIN64) && !defined(__CYGWIN__)
								WaitForSingleObject(write_keys, INFINITE);
#else
								pthread_mutex_lock(&write_keys);
#endif

								filekey = fopen("KEYFOUNDKEYFOUND.txt","a");
								if(filekey != NULL)	{
									fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
									fclose(filekey);
								}
								free(hextemp);
								free(aux_c);
#if defined(_WIN64) && !defined(__CYGWIN__)
								ReleaseMutex(write_keys);
#else
								pthread_mutex_unlock(&write_keys);
#endif

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


/*
	The bsgs_secondcheck function is made to perform a second BSGS search in a Range of less size.
	This funtion is made with the especific purpouse to USE a smaller bPtable in RAM.
*/
int bsgs_secondcheck(Int *start_range,uint32_t a,uint32_t k_index,Int *privatekey)	{
	int i = 0,found = 0,r = 0;
	Int base_key;
	Point base_point,point_aux;
	Point BSGS_Q, BSGS_S,BSGS_Q_AMP;
	uint8_t xpoint_raw[16];


	base_key.Set(&BSGS_M_double);
	base_key.Mult((uint64_t) a);
	base_key.Add(start_range);

	base_point = secp->ComputePublicKey(&base_key);
	point_aux = secp->Negation(base_point);

	/*
		BSGS_S = Q - base_key
				 Q is the target Key
		base_key is the Start range + a*BSGS_M
	*/
	BSGS_S = secp->AddDirect(OriginalPointsBSGS[k_index],point_aux);
	BSGS_Q.Set(BSGS_S);
	do {
		BSGS_Q_AMP = secp->AddDirect(BSGS_Q,BSGS_AMP2[i]);
		BSGS_S.Set(BSGS_Q_AMP);
		BSGS_S.x.GetHi16Bytes(xpoint_raw);
		r = bloom_ext_check(&bloom_bPx2nd[(uint8_t)xpoint_raw[0]], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
		if(r)	{
			found = bsgs_thirdcheck(&base_key,i,k_index,privatekey);
		}
		i++;
	}while(i < 32 && !found);
	return found;
}

int bsgs_thirdcheck(Int *start_range,uint32_t a,uint32_t k_index,Int *privatekey)	{
	uint64_t j = 0;
	int i = 0,found = 0,r = 0;
	Int base_key,calculatedkey;
	Point base_point,point_aux;
	Point BSGS_Q, BSGS_S,BSGS_Q_AMP;
	uint8_t xpoint_raw[32];

	base_key.SetInt32(a);
	base_key.Mult(&BSGS_M2_double);
	base_key.Add(start_range);

	base_point = secp->ComputePublicKey(&base_key);
	point_aux = secp->Negation(base_point);
	
	BSGS_S = secp->AddDirect(OriginalPointsBSGS[k_index],point_aux);
	BSGS_Q.Set(BSGS_S);
	
	do {
		BSGS_Q_AMP = secp->AddDirect(BSGS_Q,BSGS_AMP3[i]);
		BSGS_S.Set(BSGS_Q_AMP);
		BSGS_S.x.GetHi16Bytes(xpoint_raw);
		r = bloom_ext_check(&bloom_bPx3rd[(uint8_t)xpoint_raw[0]], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
		if(r)	{
			BSGS_S.x.GetLo16Bytes(xpoint_raw + 16);
			r = bsgs_searchbinary(bPtable, (char*)xpoint_raw, bsgs_m3, &j);
			if(r)	{
				calcualteindex(i,&calculatedkey);
				privatekey->Set(&calculatedkey);
				privatekey->Add((uint64_t)(j+1));
				privatekey->Add(&base_key);
				point_aux = secp->ComputePublicKey(privatekey);
				if(point_aux.x.IsEqual(&OriginalPointsBSGS[k_index].x))	{
					found = 1;
				}
				else	{
					calcualteindex(i,&calculatedkey);
					privatekey->Set(&calculatedkey);
					privatekey->Sub((uint64_t)(j+1));
					privatekey->Add(&base_key);
					point_aux = secp->ComputePublicKey(privatekey);
					if(point_aux.x.IsEqual(&OriginalPointsBSGS[k_index].x))	{
						found = 1;
					}
				}
			}
		}
		else	{
			/*
				For some reason the AddDirect don't return 000000... value when the publickeys are the negated values from each other
				Why JLP?
				This is is an special case
			*/
			if(BSGS_Q.x.IsEqual(&BSGS_AMP3[i].x))	{
				calcualteindex(i,&calculatedkey);
				privatekey->Set(&calculatedkey);
				privatekey->Add(&base_key);
				found = 1;
			}
		}
		i++;
	}while(i < 32 && !found);
	return found;
}

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

#if 0
			pp.y.ModSub(&Gn[i].x,&pp.x);
			pp.y.ModMulK1(&_s);
			pp.y.ModSub(&Gn[i].y);           // ry = - p2.y - s*(ret.x-p2.x);
#endif

			// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
			dyn.Set(&Gn[i].y);
			dyn.ModNeg();
			dyn.ModSub(&pn.y);

			_s.ModMulK1(&dyn,&dx[i]);      // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
			_p.ModSquareK1(&_s);            // _p = pow2(s)

			pn.x.ModNeg();
			pn.x.ModAdd(&_p);
			pn.x.ModSub(&Gn[i].x);          // rx = pow2(s) - p1.x - p2.x;

#if 0
			pn.y.ModSub(&Gn[i].x,&pn.x);
			pn.y.ModMulK1(&_s);
			pn.y.ModAdd(&Gn[i].y);          // ry = - p2.y - s*(ret.x-p2.x);
#endif

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

#if 0
		pn.y.ModSub(&Gn[i].x,&pn.x);
		pn.y.ModMulK1(&_s);
		pn.y.ModAdd(&Gn[i].y);
#endif

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
#if defined(_WIN64) && !defined(__CYGWIN__)
					WaitForSingleObject(bloom_bPx3rd_mutex[bloom_bP_index], INFINITE);
					bloom_ext_add(&bloom_bPx3rd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
					ReleaseMutex(bloom_bPx3rd_mutex[bloom_bP_index]);
#else
					pthread_mutex_lock(&bloom_bPx3rd_mutex[bloom_bP_index]);
					bloom_ext_add(&bloom_bPx3rd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
					pthread_mutex_unlock(&bloom_bPx3rd_mutex[bloom_bP_index]);
#endif
				}
			}
			if(i_counter < bsgs_m2 && !FLAGREADEDFILE2)	{
#if defined(_WIN64) && !defined(__CYGWIN__)
				WaitForSingleObject(bloom_bPx2nd_mutex[bloom_bP_index], INFINITE);
bloom_ext_add(&bloom_bPx2nd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
				ReleaseMutex(bloom_bPx2nd_mutex[bloom_bP_index]);
#else
				pthread_mutex_lock(&bloom_bPx2nd_mutex[bloom_bP_index]);
bloom_ext_add(&bloom_bPx2nd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
				pthread_mutex_unlock(&bloom_bPx2nd_mutex[bloom_bP_index]);
#endif	
			}
			if(i_counter < to && !FLAGREADEDFILE1 )	{
#if defined(_WIN64) && !defined(__CYGWIN__)
				WaitForSingleObject(bloom_bP_mutex[bloom_bP_index], INFINITE);
bloom_ext_add(&bloom_bP[bloom_bP_index], rawvalue ,BSGS_BUFFERXPOINTLENGTH);
				ReleaseMutex(bloom_bP_mutex[bloom_bP_index);
#else
				pthread_mutex_lock(&bloom_bP_mutex[bloom_bP_index]);
bloom_ext_add(&bloom_bP[bloom_bP_index], rawvalue ,BSGS_BUFFERXPOINTLENGTH);
				pthread_mutex_unlock(&bloom_bP_mutex[bloom_bP_index]);
#endif
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
#if defined(_WIN64) && !defined(__CYGWIN__)
	WaitForSingleObject(bPload_mutex[threadid], INFINITE);
	tt->finished = 1;
	ReleaseMutex(bPload_mutex[threadid]);
#else	
	pthread_mutex_lock(&bPload_mutex[threadid]);
	tt->finished = 1;
	pthread_mutex_unlock(&bPload_mutex[threadid]);
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

#if 0
			pp.y.ModSub(&Gn[i].x,&pp.x);
			pp.y.ModMulK1(&_s);
			pp.y.ModSub(&Gn[i].y);           // ry = - p2.y - s*(ret.x-p2.x);
#endif

			// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
			dyn.Set(&Gn[i].y);
			dyn.ModNeg();
			dyn.ModSub(&pn.y);

			_s.ModMulK1(&dyn,&dx[i]);      // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
			_p.ModSquareK1(&_s);            // _p = pow2(s)

			pn.x.ModNeg();
			pn.x.ModAdd(&_p);
			pn.x.ModSub(&Gn[i].x);          // rx = pow2(s) - p1.x - p2.x;

#if 0
			pn.y.ModSub(&Gn[i].x,&pn.x);
			pn.y.ModMulK1(&_s);
			pn.y.ModAdd(&Gn[i].y);          // ry = - p2.y - s*(ret.x-p2.x);
#endif

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

#if 0
		pn.y.ModSub(&Gn[i].x,&pn.x);
		pn.y.ModMulK1(&_s);
		pn.y.ModAdd(&Gn[i].y);
#endif

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
#if defined(_WIN64) && !defined(__CYGWIN__)
					WaitForSingleObject(bloom_bPx3rd_mutex[bloom_bP_index], INFINITE);
					bloom_ext_add(&bloom_bPx3rd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
					ReleaseMutex(bloom_bPx3rd_mutex[bloom_bP_index]);
#else
					pthread_mutex_lock(&bloom_bPx3rd_mutex[bloom_bP_index]);
					bloom_ext_add(&bloom_bPx3rd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
					pthread_mutex_unlock(&bloom_bPx3rd_mutex[bloom_bP_index]);
#endif
				}
			}
			if(i_counter < bsgs_m2 && !FLAGREADEDFILE2)	{
#if defined(_WIN64) && !defined(__CYGWIN__)
					WaitForSingleObject(bloom_bPx2nd_mutex[bloom_bP_index], INFINITE);
bloom_ext_add(&bloom_bPx2nd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
					ReleaseMutex(bloom_bPx2nd_mutex[bloom_bP_index]);
#else
					pthread_mutex_lock(&bloom_bPx2nd_mutex[bloom_bP_index]);
bloom_ext_add(&bloom_bPx2nd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
					pthread_mutex_unlock(&bloom_bPx2nd_mutex[bloom_bP_index]);
#endif			
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
#if defined(_WIN64) && !defined(__CYGWIN__)
	WaitForSingleObject(bPload_mutex[threadid], INFINITE);
	tt->finished = 1;
	ReleaseMutex(bPload_mutex[threadid]);
#else	
	pthread_mutex_lock(&bPload_mutex[threadid]);
	tt->finished = 1;
	pthread_mutex_unlock(&bPload_mutex[threadid]);
	pthread_exit(NULL);
#endif
	return NULL;
}

/* This function perform the KECCAK Opetation*/
void KECCAK_256(uint8_t *source, size_t size,uint8_t *dst)	{
	SHA3_256_CTX ctx;
	SHA3_256_Init(&ctx);
	SHA3_256_Update(&ctx,source,size);
	KECCAK_256_Final(dst,&ctx);
}

/* This function takes in two parameters:

publickey: a reference to a Point object representing a public key.
dst_address: a pointer to an unsigned char array where the generated binary address will be stored.
The function is designed to generate a binary address for Ethereum using the given public key.
It first extracts the x and y coordinates of the public key as 32-byte arrays, and concatenates them
to form a 64-byte array called bin_publickey. Then, it applies the KECCAK-256 hashing algorithm to
bin_publickey to generate the binary address, which is stored in dst_address. */

void generate_binaddress_eth(Point &publickey,unsigned char *dst_address)	{
	unsigned char bin_publickey[64];
	publickey.x.Get32Bytes(bin_publickey);
	publickey.y.Get32Bytes(bin_publickey+32);
	KECCAK_256(bin_publickey, 64, bin_publickey);
	memcpy(dst_address,bin_publickey+12,20);
}

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
		r = rand() % 3;
#if defined(_WIN64) && !defined(__CYGWIN__)
	WaitForSingleObject(bsgs_thread, INFINITE);
#else
	pthread_mutex_lock(&bsgs_thread);
#endif
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
#if defined(_WIN64) && !defined(__CYGWIN__)
	ReleaseMutex(bsgs_thread);
#else
	pthread_mutex_unlock(&bsgs_thread);
#endif

		if(entrar == 0)
			break;
			
		if(FLAGMATRIX)	{
			aux_c = base_key.GetBase16();
			printf("[+] Thread 0x%s \n",aux_c);
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
						
#if 0
  pp.y.ModSub(&GSn[i].x,&pp.x);
  pp.y.ModMulK1(&_s);
  pp.y.ModSub(&GSn[i].y);           // ry = - p2.y - s*(ret.x-p2.x);  
#endif

						// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
						dyn.Set(&GSn[i].y);
						dyn.ModNeg();
						dyn.ModSub(&pn.y);

						_s.ModMulK1(&dyn,&dx[i]);       // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
						_p.ModSquareK1(&_s);            // _p = pow2(s)

						pn.x.ModNeg();
						pn.x.ModAdd(&_p);
						pn.x.ModSub(&GSn[i].x);          // rx = pow2(s) - p1.x - p2.x;

#if 0
  pn.y.ModSub(&GSn[i].x,&pn.x);
  pn.y.ModMulK1(&_s);
  pn.y.ModAdd(&GSn[i].y);          // ry = - p2.y - s*(ret.x-p2.x);  
#endif


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

#if 0
pn.y.ModSub(&GSn[i].x,&pn.x);
pn.y.ModMulK1(&_s);
pn.y.ModAdd(&GSn[i].y);
#endif

					pts[0] = pn;

					for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k]== 0; i++) {
						pts[i].x.GetHi16Bytes(xpoint_raw);
						r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
						if(r) {
							r = bsgs_secondcheck(&base_key,((j*1024) + i),k,&keyfound);
							if(r)	{
								hextemp = keyfound.GetBase16();
								printf("[+] Thread Key found privkey %s   \n",hextemp);
								point_found = secp->ComputePublicKey(&keyfound);
								aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
								printf("[+] Publickey %s\n",aux_c);
#if defined(_WIN64) && !defined(__CYGWIN__)
								WaitForSingleObject(write_keys, INFINITE);
#else
								pthread_mutex_lock(&write_keys);
#endif

								filekey = fopen("KEYFOUNDKEYFOUND.txt","a");
								if(filekey != NULL)	{
									fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
									fclose(filekey);
								}
								free(hextemp);
								free(aux_c);
#if defined(_WIN64) && !defined(__CYGWIN__)
								ReleaseMutex(write_keys);
#else
								pthread_mutex_unlock(&write_keys);
#endif

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
		
#if defined(_WIN64) && !defined(__CYGWIN__)
		WaitForSingleObject(bsgs_thread, INFINITE);
#else
		pthread_mutex_lock(&bsgs_thread);
#endif
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
#if defined(_WIN64) && !defined(__CYGWIN__)
		ReleaseMutex(bsgs_thread);
#else
		pthread_mutex_unlock(&bsgs_thread);
#endif
		if(entrar == 0)
			break;
		
		if(FLAGMATRIX)	{
			aux_c = base_key.GetBase16();
			printf("[+] Thread 0x%s \n",aux_c);
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
						
#if 0
  pp.y.ModSub(&GSn[i].x,&pp.x);
  pp.y.ModMulK1(&_s);
  pp.y.ModSub(&GSn[i].y);           // ry = - p2.y - s*(ret.x-p2.x);  
#endif

						// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
						dyn.Set(&GSn[i].y);
						dyn.ModNeg();
						dyn.ModSub(&pn.y);

						_s.ModMulK1(&dyn,&dx[i]);       // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
						_p.ModSquareK1(&_s);            // _p = pow2(s)

						pn.x.ModNeg();
						pn.x.ModAdd(&_p);
						pn.x.ModSub(&GSn[i].x);          // rx = pow2(s) - p1.x - p2.x;

#if 0
  pn.y.ModSub(&GSn[i].x,&pn.x);
  pn.y.ModMulK1(&_s);
  pn.y.ModAdd(&GSn[i].y);          // ry = - p2.y - s*(ret.x-p2.x);  
#endif


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

#if 0
pn.y.ModSub(&GSn[i].x,&pn.x);
pn.y.ModMulK1(&_s);
pn.y.ModAdd(&GSn[i].y);
#endif

					pts[0] = pn;

					for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k]== 0; i++) {
						pts[i].x.GetHi16Bytes(xpoint_raw);
						r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
						if(r) {
							r = bsgs_secondcheck(&base_key,((j*1024) + i),k,&keyfound);
							if(r)	{
								hextemp = keyfound.GetBase16();
								printf("[+] Thread Key found privkey %s   \n",hextemp);
								point_found = secp->ComputePublicKey(&keyfound);
								aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
								printf("[+] Publickey %s\n",aux_c);
#if defined(_WIN64) && !defined(__CYGWIN__)
								WaitForSingleObject(write_keys, INFINITE);
#else
								pthread_mutex_lock(&write_keys);
#endif

								filekey = fopen("KEYFOUNDKEYFOUND.txt","a");
								if(filekey != NULL)	{
									fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
									fclose(filekey);
								}
								free(hextemp);
								free(aux_c);
#if defined(_WIN64) && !defined(__CYGWIN__)
								ReleaseMutex(write_keys);
#else
								pthread_mutex_unlock(&write_keys);
#endif

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

		r = rand() % 2;
#if defined(_WIN64) && !defined(__CYGWIN__)
		WaitForSingleObject(bsgs_thread, INFINITE);
#else
		pthread_mutex_lock(&bsgs_thread);
#endif
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
#if defined(_WIN64) && !defined(__CYGWIN__)
		ReleaseMutex(bsgs_thread);
#else
		pthread_mutex_unlock(&bsgs_thread);
#endif

		if(entrar == 0)
			break;

		
		if(FLAGMATRIX)	{
			aux_c = base_key.GetBase16();
			printf("[+] Thread 0x%s \n",aux_c);
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
							
#if 0
	  pp.y.ModSub(&GSn[i].x,&pp.x);
	  pp.y.ModMulK1(&_s);
	  pp.y.ModSub(&GSn[i].y);           // ry = - p2.y - s*(ret.x-p2.x);  
#endif

							// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
							dyn.Set(&GSn[i].y);
							dyn.ModNeg();
							dyn.ModSub(&pn.y);

							_s.ModMulK1(&dyn,&dx[i]);       // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
							_p.ModSquareK1(&_s);            // _p = pow2(s)

							pn.x.ModNeg();
							pn.x.ModAdd(&_p);
							pn.x.ModSub(&GSn[i].x);          // rx = pow2(s) - p1.x - p2.x;

#if 0
	  pn.y.ModSub(&GSn[i].x,&pn.x);
	  pn.y.ModMulK1(&_s);
	  pn.y.ModAdd(&GSn[i].y);          // ry = - p2.y - s*(ret.x-p2.x);  
#endif


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

#if 0
	pn.y.ModSub(&GSn[i].x,&pn.x);
	pn.y.ModMulK1(&_s);
	pn.y.ModAdd(&GSn[i].y);
#endif

						pts[0] = pn;

						for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k]== 0; i++) {
							pts[i].x.GetHi16Bytes(xpoint_raw);
							r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
							if(r) {
								r = bsgs_secondcheck(&base_key,((j*1024) + i),k,&keyfound);
								if(r)	{
									hextemp = keyfound.GetBase16();
									printf("[+] Thread Key found privkey %s   \n",hextemp);
									point_found = secp->ComputePublicKey(&keyfound);
									aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
									printf("[+] Publickey %s\n",aux_c);
#if defined(_WIN64) && !defined(__CYGWIN__)
									WaitForSingleObject(write_keys, INFINITE);
#else
									pthread_mutex_lock(&write_keys);
#endif

									filekey = fopen("KEYFOUNDKEYFOUND.txt","a");
									if(filekey != NULL)	{
										fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
										fclose(filekey);
									}
									free(hextemp);
									free(aux_c);
#if defined(_WIN64) && !defined(__CYGWIN__)
									ReleaseMutex(write_keys);
#else
									pthread_mutex_unlock(&write_keys);
#endif

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


/* This function takes in three parameters:

buffer: a pointer to a char array where the minikey will be stored.
rawbuffer: a pointer to a char array that contains the raw data.
length: an integer representing the length of the raw data.
The function is designed to convert the raw data using a lookup table (Ccoinbuffer) and store the result in the buffer. 
*/
void set_minikey(char *buffer,char *rawbuffer,int length)	{
	for(int i = 0;  i < length; i++)	{
		buffer[i] = Ccoinbuffer[(uint8_t)rawbuffer[i]];
	}
}

/* This function takes in three parameters:

buffer: a pointer to a char array where the minikey will be stored.
rawbuffer: a pointer to a char array that contains the raw data.
index: an integer representing the index of the raw data array to be incremented.
The function is designed to increment the value at the specified index in the raw data array,
and update the corresponding value in the buffer using a lookup table (Ccoinbuffer).
If the value at the specified index exceeds 57, it is reset to 0x00 and the function recursively
calls itself to increment the value at the previous index, unless the index is already 0, in which
case the function returns false. The function returns true otherwise. 
*/

bool increment_minikey_index(char *buffer,char *rawbuffer,int index)	{
	if(rawbuffer[index] < 57){
		rawbuffer[index]++;
		buffer[index] = Ccoinbuffer[(uint8_t)rawbuffer[index]];
	}
	else	{
		rawbuffer[index] = 0x00;
		buffer[index] = Ccoinbuffer[0];
		if(index>0)	{
			return increment_minikey_index(buffer,rawbuffer,index-1);
		}
		else	{
			return false;
		}
	}
	return true;
}

/* This function takes in a single parameter:

rawbuffer: a pointer to a char array that contains the raw data.
The function is designed to increment the values in the raw data array
using a lookup table (minikeyN), while also handling carry-over to the
previous element in the array if necessary. The maximum number of iterations
is limited by minikey_n_limit. 


*/
void increment_minikey_N(char *rawbuffer)	{
	int i = 20,j = 0;
	while( i > 0 && j < minikey_n_limit)	{
		rawbuffer[i] = rawbuffer[i] + minikeyN[i];
		if(rawbuffer[i] > 57)	{	 // Handling carry-over if value exceeds 57
			rawbuffer[i] = rawbuffer[i] % 58;
			rawbuffer[i-1]++;
		}
		i--;
		j++;
	}
}


#define BUFFMINIKEY(buff,src) \
(buff)[ 0] = (uint32_t)src[ 0] << 24 | (uint32_t)src[ 1] << 16 | (uint32_t)src[ 2] << 8 | (uint32_t)src[ 3]; \
(buff)[ 1] = (uint32_t)src[ 4] << 24 | (uint32_t)src[ 5] << 16 | (uint32_t)src[ 6] << 8 | (uint32_t)src[ 7]; \
(buff)[ 2] = (uint32_t)src[ 8] << 24 | (uint32_t)src[ 9] << 16 | (uint32_t)src[10] << 8 | (uint32_t)src[11]; \
(buff)[ 3] = (uint32_t)src[12] << 24 | (uint32_t)src[13] << 16 | (uint32_t)src[14] << 8 | (uint32_t)src[15]; \
(buff)[ 4] = (uint32_t)src[16] << 24 | (uint32_t)src[17] << 16 | (uint32_t)src[18] << 8 | (uint32_t)src[19]; \
(buff)[ 5] = (uint32_t)src[20] << 24 | (uint32_t)src[21] << 16 | 0x8000; \
(buff)[ 6] = 0; \
(buff)[ 7] = 0; \
(buff)[ 8] = 0; \
(buff)[ 9] = 0; \
(buff)[10] = 0; \
(buff)[11] = 0; \
(buff)[12] = 0; \
(buff)[13] = 0; \
(buff)[14] = 0; \
(buff)[15] = 0xB0;	//176 bits => 22 BYTES


void sha256sse_22(uint8_t *src0, uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *dst0, uint8_t *dst1, uint8_t *dst2, uint8_t *dst3)	{
  uint32_t b0[16];
  uint32_t b1[16];
  uint32_t b2[16];
  uint32_t b3[16];
  BUFFMINIKEY(b0, src0);
  BUFFMINIKEY(b1, src1);
  BUFFMINIKEY(b2, src2);
  BUFFMINIKEY(b3, src3);
  sha256sse_1B(b0, b1, b2, b3, dst0, dst1, dst2, dst3);
}


#define BUFFMINIKEYCHECK(buff,src) \
(buff)[ 0] = (uint32_t)src[ 0] << 24 | (uint32_t)src[ 1] << 16 | (uint32_t)src[ 2] << 8 | (uint32_t)src[ 3]; \
(buff)[ 1] = (uint32_t)src[ 4] << 24 | (uint32_t)src[ 5] << 16 | (uint32_t)src[ 6] << 8 | (uint32_t)src[ 7]; \
(buff)[ 2] = (uint32_t)src[ 8] << 24 | (uint32_t)src[ 9] << 16 | (uint32_t)src[10] << 8 | (uint32_t)src[11]; \
(buff)[ 3] = (uint32_t)src[12] << 24 | (uint32_t)src[13] << 16 | (uint32_t)src[14] << 8 | (uint32_t)src[15]; \
(buff)[ 4] = (uint32_t)src[16] << 24 | (uint32_t)src[17] << 16 | (uint32_t)src[18] << 8 | (uint32_t)src[19]; \
(buff)[ 5] = (uint32_t)src[20] << 24 | (uint32_t)src[21] << 16 | (uint32_t)src[22] << 8 | 0x80; \
(buff)[ 6] = 0; \
(buff)[ 7] = 0; \
(buff)[ 8] = 0; \
(buff)[ 9] = 0; \
(buff)[10] = 0; \
(buff)[11] = 0; \
(buff)[12] = 0; \
(buff)[13] = 0; \
(buff)[14] = 0; \
(buff)[15] = 0xB8;	//184 bits => 23 BYTES

void sha256sse_23(uint8_t *src0, uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *dst0, uint8_t *dst1, uint8_t *dst2, uint8_t *dst3)	{
  uint32_t b0[16];
  uint32_t b1[16];
  uint32_t b2[16];
  uint32_t b3[16];
  BUFFMINIKEYCHECK(b0, src0);
  BUFFMINIKEYCHECK(b1, src1);
  BUFFMINIKEYCHECK(b2, src2);
  BUFFMINIKEYCHECK(b3, src3);
  sha256sse_1B(b0, b1, b2, b3, dst0, dst1, dst2, dst3);
}

void menu() {
	printf("\nUsage:\n");
	printf("-h          show this help\n");
	printf("-B Mode     BSGS now have some modes <sequential, backward, both, random, dance>\n");
	printf("-b bits     For some puzzles you only need some numbers of bits in the test keys.\n");
	printf("-c crypto   Search for specific crypto. <btc, eth> valid only w/ -m address\n");
	printf("-C mini     Set the minikey Base only 22 character minikeys, ex: SRPqx8QiwnW4WNWnTVa2W5\n");
	printf("-8 alpha    Set the bas58 alphabet for minikeys\n");
	printf("-e          Enable endomorphism search (Only for address, rmd160 and vanity)\n");
	printf("-G mode     GPU usage for rmd160/address <auto,on,off> (default off)\n");
	printf("-f file     Specify file name with addresses or xpoints or uncompressed public keys\n");
	printf("-I stride   Stride for xpoint, rmd160 and address, this option don't work with bsgs\n");
	printf("-k value    Use this only with bsgs mode, k value is factor for M, more speed but more RAM use wisely\n");
	printf("-l look     What type of address/hash160 are you looking for <compress, uncompress, both> Only for rmd160 and address\n");
	printf("-m mode     mode of search for cryptos. (bsgs, xpoint, rmd160, address, vanity) default: address\n");
	printf("-M          Matrix screen, feel like a h4x0r, but performance will dropped\n");
	printf("-P          Enable segmented range progress indicator (non-random address/xpoint/rmd160/vanity)\n");
	printf("-n number   Check for N sequential numbers before the random chosen, this only works with -R option\n");
	printf("            Use -n to set the N for the BSGS process. Bigger N more RAM needed\n");
	printf("-q          Quiet the thread output\n");
	printf("-r SR:EN    StarRange:EndRange, the end range can be omitted for search from start range to N-1 ECC value\n");
	printf("-R          Random, this is the default behavior\n");
	printf("-s ns       Number of seconds for the stats output, 0 to omit output.\n");
	printf("-S          S is for SAVING in files BSGS data (Bloom filters and bPtable)\n");
	printf("-6          to skip sha256 Checksum on data files");
	printf("-t tn       Threads number, must be a positive integer\n");
	printf("-v value    Search for vanity Address, only with -m vanity\n");
	printf("-z value    Bloom size multiplier, only address,rmd160,vanity, xpoint, value >= 1\n");
	printf("\nExample:\n\n");
	printf("./keyhunt -m rmd160 -f tests/unsolvedpuzzles.rmd -b 66 -l compress -R -q -t 8\n\n");
	printf("This line runs the program with 8 threads from the range 20000000000000000 to 40000000000000000 without stats output\n\n");
	printf("Developed by AlbertoBSD\tTips BTC: 1Coffee1jV4gB5gaXfHgSHDz9xx9QSECVW\n");
	printf("Thanks to Iceland always helping and sharing his ideas.\nTips to Iceland: bc1q39meky2mn5qjq704zz0nnkl0v7kj4uz6r529at\n\n");
	exit(EXIT_FAILURE);
}

bool vanityrmdmatch(unsigned char *rmdhash)	{
	bool r = false;
	int i,j,cmpA,cmpB,result;
	result = bloom_check(vanity_bloom,rmdhash,vanity_rmd_minimun_bytes_check_length);
	switch(result)	{
		case -1:
			fprintf(stderr,"[E] Bloom is not initialized\n");
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
	
#if defined(_WIN64) && !defined(__CYGWIN__)
	WaitForSingleObject(write_keys, INFINITE);
#else
	pthread_mutex_lock(&write_keys);
#endif
	keys = fopen("VANITYKEYFOUND.txt","a+");
	if(keys != NULL)	{
		fprintf(keys,"Vanity Private Key: %s\npubkey: %s\nAddress %s\nrmd160 %s\n",hextemp,public_key_hex,address,hexrmd);
		fclose(keys);
	}
	printf("\nVanity Private Key: %s\npubkey: %s\nAddress %s\nrmd160 %s\n",hextemp,public_key_hex,address,hexrmd);
	
#if defined(_WIN64) && !defined(__CYGWIN__)
	ReleaseMutex(write_keys);
#else
	pthread_mutex_unlock(&write_keys);
#endif
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

	// Two modes:
	// 1) Work-stealing: GPU pulls blocks from shared pool (g_work_pool.enabled=true)
	// 2) Static split: GPU scans the fixed [start_key, end_key] range once
	if (!g_work_pool.enabled) {
		printf("[GPU] Static-range thread started\n");
		int found = gpu_run_full_search(&args->start_key, &args->end_key, &args->stride, args->target_count);
		if (found > 0) total_found = found;
		args->result = total_found;
		args->completed = 1;
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

		// Run GPU search on this block
		int found = gpu_run_full_search(&block_start, &block_end, &args->stride, args->target_count);
		if (found > 0) {
			total_found += found;
		}
		blocks_processed++;

		// Brief status every 10 blocks
		if (blocks_processed % 10 == 0) {
			printf("[GPU] Processed %lu blocks, total found: %d\n",
				   (unsigned long)blocks_processed, total_found);
		}
	}

	printf("[GPU] Work-stealing thread completed: %lu blocks, %d keys found\n",
		   (unsigned long)blocks_processed, total_found);

	args->result = total_found;
	args->completed = 1;

	return NULL;
}

	// Run full GPU search with CPU fallback
	static int gpu_run_full_search(Int *start_key, Int *end_key, Int *stride_val, int64_t target_count) {
	if (!gpu_backend_available()) {
		fprintf(stderr, "[W] GPU not available, cannot run full GPU search\n");
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
				__atomic_store_n(&g_gpu_keys_checked_cur, 0, __ATOMIC_RELEASE);
				config.keys_checked = &g_gpu_keys_checked_cur;
			} else {
				config.keys_checked = &g_gpu_keys_checked;
			}
		config.should_stop = &g_gpu_should_stop;
		config.quiet = (FLAGQUIET != 0) || (FLAGGPU_HYBRID != 0) || OUTPUTSECONDS.IsGreater(&ZERO);

		printf("[+] Starting GPU full search (ECC + hash160 + matching on GPU)\n");
		printf("[+] Target count: %" PRId64 ", using %s\n",
			target_count,
			target_count == 1 ? "direct comparison" :
				(config.use_bloom ? "GPU bloom + binary search" : "binary search"));
		printf("[+] Search mode: %s\n",
			(config.search_compressed && config.search_uncompressed) ? "compressed + uncompressed" :
			(config.search_compressed ? "compressed only" :
				(config.search_uncompressed ? "uncompressed only" : "none")));

					int found = gpu_full_search(&config);
				if (g_work_pool.enabled) {
					uint64_t done = __atomic_load_n(&g_gpu_keys_checked_cur, __ATOMIC_ACQUIRE);
					__atomic_fetch_add(&g_gpu_keys_checked, done, __ATOMIC_RELEASE);
					__atomic_store_n(&g_gpu_keys_checked_cur, 0, __ATOMIC_RELEASE);
				}
				return found;
		}

void checkpointer(void *ptr,const char *file,const char *function,const  char *name,int line)	{
	if(ptr == NULL)	{
		fprintf(stderr,"[E] error in file %s, %s pointer %s on line %i\n",file,function,name,line); 
		exit(EXIT_FAILURE);
	}
}

void writekey(bool compressed,Int *key)	{
	// Range validation: skip keys outside the original search range
	// This prevents false positives from key negation producing out-of-range keys
	// Use g_rangeProgressStart/End which store the original unchanged range bounds
	if (key->IsLower(&g_rangeProgressStart) || key->IsGreaterOrEqual(&g_rangeProgressEnd)) {
		return;
	}

	Point publickey;
	FILE *keys;
	char *hextemp,*hexrmd,public_key_hex[132],address[50],rmdhash[20];
	memset(address,0,50);
	memset(public_key_hex,0,132);
	hextemp = key->GetBase16();
	publickey = secp->ComputePublicKey(key);
	secp->GetPublicKeyHex(compressed,publickey,public_key_hex);
	secp->GetHash160(P2PKH,compressed,publickey,(uint8_t*)rmdhash);
	hexrmd = tohex(rmdhash,20);
	rmd160toaddress_dst(rmdhash,address);

#if defined(_WIN64) && !defined(__CYGWIN__)
	WaitForSingleObject(write_keys, INFINITE);
#else
	pthread_mutex_lock(&write_keys);
#endif
	keys = fopen("KEYFOUNDKEYFOUND.txt","a+");
	if(keys != NULL)	{
		fprintf(keys,"Private Key: %s\npubkey: %s\nAddress %s\nrmd160 %s\n",hextemp,public_key_hex,address,hexrmd);
		fclose(keys);
	}
	printf("\nHit! Private Key: %s\npubkey: %s\nAddress %s\nrmd160 %s\n",hextemp,public_key_hex,address,hexrmd);
	
#if defined(_WIN64) && !defined(__CYGWIN__)
	ReleaseMutex(write_keys);
#else
	pthread_mutex_unlock(&write_keys);
#endif
	free(hextemp);
	free(hexrmd);
}

void writekeyeth(Int *key)	{
	// Range validation: skip keys outside the original search range
	if (key->IsLower(&g_rangeProgressStart) || key->IsGreaterOrEqual(&g_rangeProgressEnd)) {
		return;
	}

	Point publickey;
	FILE *keys;
	char *hextemp,address[43],hash[20];
	hextemp = key->GetBase16();
	publickey = secp->ComputePublicKey(key);
	generate_binaddress_eth(publickey,(unsigned char*)hash);
	address[0] = '0';
	address[1] = 'x';
	tohex_dst(hash,20,address+2);

#if defined(_WIN64) && !defined(__CYGWIN__)
	WaitForSingleObject(write_keys, INFINITE);
#else
	pthread_mutex_lock(&write_keys);
#endif
	keys = fopen("KEYFOUNDKEYFOUND.txt","a+");
	if(keys != NULL)	{
		fprintf(keys,"Private Key: %s\naddress: %s\n",hextemp,address);
		fclose(keys);
	}
	printf("\n Hit!!!! Private Key: %s\naddress: %s\n",hextemp,address);
#if defined(_WIN64) && !defined(__CYGWIN__)
	ReleaseMutex(write_keys);
#else
	pthread_mutex_unlock(&write_keys);
#endif
	free(hextemp);
}

bool isBase58(char c) {
    // Define the base58 set
    const char base58Set[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    // Check if the character is in the base58 set
    return strchr(base58Set, c) != NULL;
}

bool isValidBase58String(char *str)	{
	int len = strlen(str);
	bool continuar = true;
	for (int i = 0; i < len && continuar; i++) {
		continuar = isBase58(str[i]);
	}
	return continuar;
}

bool processOneVanity()	{
	int i,k;
	if(vanity_rmd_targets == 0)	{
		fprintf(stderr,"[E] There aren't any vanity targets\n");
		return false;
	}

	if(!initBloomFilter(vanity_bloom, vanity_rmd_total))
		return false;
	
	for(i = 0; i < vanity_rmd_targets;i++)	{
		for(k = 0; k < vanity_rmd_limits[i]; k++)	{
			bloom_add(vanity_bloom, vanity_rmd_limit_values_A[i][k] ,vanity_rmd_minimun_bytes_check_length);
		}
	}
	return true;
}


bool readFileVanity(char *fileName)	{
	FILE *fileDescriptor;
	int i,k,len;
	char aux[100],*hextemp;

	fileDescriptor = fopen(fileName,"r");
	if(fileDescriptor == NULL)	{
		if(vanity_rmd_targets == 0)	{
			fprintf(stderr,"[E] There aren't any vanity targets\n");
			return false;
		}
	}
	else	{
		while(!feof(fileDescriptor))	{
			hextemp = fgets(aux,100,fileDescriptor);
			if(hextemp == aux)	{
				trim(aux," \t\n\r");
				len = strlen(aux);
				if(len > 0 && len < 36){
					if(isValidBase58String(aux))	{
						addvanity(aux);
					}
					else	{
						fprintf(stderr,"[E] the string \"%s\" is not valid Base58, omiting it\n",aux);
					}
				}
			}
		}
		fclose(fileDescriptor);
	}
	
	N = vanity_rmd_total;
	if(!initBloomFilter(vanity_bloom,N))
		return false;
	
	for(i = 0; i < vanity_rmd_targets ; i++)	{
		for(k = 0; k < vanity_rmd_limits[i]; k++)	{
			bloom_add(vanity_bloom, vanity_rmd_limit_values_A[i][k] ,vanity_rmd_minimun_bytes_check_length);
		}
	}
	return true;
}

bool readFileAddress(char *fileName)	{
	FILE *fileDescriptor;
	char fileBloomName[30];	/* Actually it is Bloom and Table but just to keep the variable name short*/
	uint8_t checksum[32],hexPrefix[9];
	char dataChecksum[32],bloomChecksum[32];
	size_t bytesRead;
	uint64_t dataSize;
	/*
		if the FLAGSAVEREADFILE is Set to 1 we need to the checksum and check if we have that information already saved
	*/
	if(FLAGSAVEREADFILE)	{	/* if the flag is set to REAd and SAVE the file firs we need to check it the file exist*/
		if(!sha256_file((const char*)fileName,checksum)){
			fprintf(stderr,"[E] sha256_file error line %i\n",__LINE__ - 1);
			return false;
		}
		tohex_dst((char*)checksum,4,(char*)hexPrefix); // we save the prefix (last fourt bytes) hexadecimal value
		snprintf(fileBloomName,30,"data_%s.dat",hexPrefix);
		fileDescriptor = fopen(fileBloomName,"rb");
		if(fileDescriptor != NULL)	{
			printf("[+] Reading file %s\n",fileBloomName);
		
			//read bloom checksum (expected value to be checked)
			//read bloom filter structure
			//read bloom filter data
			//calculate checksum of the current readed data
			//Compare checksums
			//read data checksum (expected value to be checked)
			//read data size
			//read data
			//compare the expected datachecksum againts the current data checksum
			//compare the expected bloom checksum againts the current bloom checksum
			

			//read bloom checksum (expected value to be checked)
			bytesRead = fread(bloomChecksum,1,32,fileDescriptor);
			if(bytesRead != 32)	{
				fprintf(stderr,"[E] Errore reading file, code line %i\n",__LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}
			
			//read bloom filter structure
			bytesRead = fread(&bloom.orig,1,sizeof(struct bloom),fileDescriptor);
			if(bytesRead != sizeof(struct bloom))	{
				fprintf(stderr,"[E] Error reading file, code line %i\n",__LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}
			
			printf("[+] Bloom filter for %" PRIu64 " elements.\n",bloom.orig.entries);

			const bool cache_fast = (bloom.orig.major == BLOOM_EXT_FAST_MAJOR && bloom.orig.minor == BLOOM_EXT_FAST_MINOR);
#if defined(_WIN64) && !defined(__CYGWIN__)
			if (cache_fast) {
				bloom.orig.bf = (uint8_t*)_aligned_malloc(bloom.orig.bytes, 64);
			} else {
				bloom.orig.bf = (uint8_t*)malloc(bloom.orig.bytes);
			}
#else
			if (cache_fast) {
				void *ptr = NULL;
				if (posix_memalign(&ptr, 64, bloom.orig.bytes) != 0) ptr = NULL;
				bloom.orig.bf = (uint8_t*)ptr;
			} else {
				bloom.orig.bf = (uint8_t*)malloc(bloom.orig.bytes);
			}
#endif
			if(bloom.orig.bf == NULL)	{
				fprintf(stderr,"[E] Error allocating memory, code line %i\n",__LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}

			//read bloom filter data
			bytesRead = fread(bloom.orig.bf,1,bloom.orig.bytes,fileDescriptor);
			if(bytesRead != bloom.orig.bytes)	{
				fprintf(stderr,"[E] Error reading file, code line %i\n",__LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}
			if(FLAGSKIPCHECKSUM == 0){

				//calculate checksum of the current readed data
				sha256((uint8_t*)bloom.orig.bf,bloom.orig.bytes,(uint8_t*)checksum);
				
				//Compare checksums
				/*
				if(FLAGDEBUG)	{
					hextemp = tohex((char*)checksum,32);
					printf("[D] Current Bloom checksum %s\n",hextemp);
					free(hextemp);
				}
				*/
				if(memcmp(checksum,bloomChecksum,32) != 0)	{
					fprintf(stderr,"[E] Error checksum mismatch, code line %i\n",__LINE__ - 2);
					fclose(fileDescriptor);
					return false;
				}
			}
			
			/*
			if(FLAGDEBUG) {
				hextemp = tohex((char*)bloom.orig.bf,32);
				printf("[D] first 32 bytes of the bloom : %s\n",hextemp);
				bloom_print(&bloom);
				printf("[D] bloom.orig.bf points to %p\n",bloom.orig.bf);
			}
			*/

			// If this cache was built with the fast bloom implementation, restore fast metadata so
			// bloom_ext_check* uses the correct hashing algorithm (XXH3) for this bitset.
			bloom_ext_sync_from_orig(&bloom);
#if USE_FAST_BLOOM
			if (bloom_ext_is_fast(&bloom)) {
				printf("[+] Using FAST bloom filter (cached)\n");
			}
#endif
#ifdef __linux__
			if (bloom_ext_is_fast(&bloom)) {
				(void)madvise(bloom.fast.bf, bloom.fast.bits / 8, MADV_HUGEPAGE);
			}
#endif
			
			bytesRead = fread(dataChecksum,1,32,fileDescriptor);
			if(bytesRead != 32)	{
				fprintf(stderr,"[E] Errore reading file, code line %i\n",__LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}
			
			bytesRead = fread(&dataSize,1,sizeof(uint64_t),fileDescriptor);
			if(bytesRead != sizeof(uint64_t))	{
				fprintf(stderr,"[E] Errore reading file, code line %i\n",__LINE__ - 2);
				fclose(fileDescriptor);
				return false; 
			}
			N = dataSize / sizeof(struct address_value);
	
			printf("[+] Allocating memory for %" PRIu64 " elements: %.2f MB\n",N,(double)(((double) sizeof(struct address_value)*N)/(double)1048576));
			
			addressTable = (struct address_value*) malloc(dataSize);
			if(addressTable == NULL)	{
				fprintf(stderr,"[E] Error allocating memory, code line %i\n",__LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}
			
			bytesRead = fread(addressTable,1,dataSize,fileDescriptor);
			if(bytesRead != dataSize)	{
				fprintf(stderr,"[E] Error reading file, code line %i\n",__LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}
			if(FLAGSKIPCHECKSUM == 0)	{
					
				sha256((uint8_t*)addressTable,dataSize,(uint8_t*)checksum);
				if(memcmp(checksum,dataChecksum,32) != 0)	{
					fprintf(stderr,"[E] Error checksum mismatch, code line %i\n",__LINE__ - 2);
					fclose(fileDescriptor);
					return false;
				}
			}
			//printf("[D] bloom.orig.bf points to %p\n",bloom.orig.bf);
			FLAGREADEDFILE1 = 1;	/* We mark the file as readed*/
			fclose(fileDescriptor);
			MAXLENGTHADDRESS = sizeof(struct address_value);
		}
	}
	if(FLAGVANITY)	{
		processOneVanity();
	}
	if(!FLAGREADEDFILE1)	{
		/*
			if the data_ file doesn't exist we need read it first:
		*/
		switch(FLAGMODE)	{
			case MODE_ADDRESS:
				if(FLAGCRYPTO == CRYPTO_BTC)	{
					return forceReadFileAddress(fileName);
				}
				if(FLAGCRYPTO == CRYPTO_ETH)	{
					return forceReadFileAddressEth(fileName);
				}
			break;
			case MODE_MINIKEYS:
			case MODE_RMD160:
				return forceReadFileAddress(fileName);
			break;
			case MODE_XPOINT:
				return forceReadFileXPoint(fileName);
			break;
			default:
				return false;
			break;
		}
	}
	return true;
}

bool forceReadFileAddress(char *fileName)	{
	/* Here we read the original file as usual */
	FILE *fileDescriptor;
	bool validAddress;
	uint64_t numberItems,i;
	size_t r,raw_value_length;
	uint8_t rawvalue[50];
	char aux[100],*hextemp;
	fileDescriptor = fopen(fileName,"r");	
	if(fileDescriptor == NULL)	{
		fprintf(stderr,"[E] Error opening the file %s, line %i\n",fileName,__LINE__ - 2);
		return false;
	}

	/*Count lines in the file*/
	numberItems = 0;
	while(!feof(fileDescriptor))	{
		hextemp = fgets(aux,100,fileDescriptor);
		trim(aux," \t\n\r");
		if(hextemp == aux)	{			
			r = strlen(aux);
			if(r > 20)	{ 
				numberItems++;
			}
		}
	}
	fseek(fileDescriptor,0,SEEK_SET);
	MAXLENGTHADDRESS = 20;		/*20 bytes beacuase we only need the data in binary*/
	
	printf("[+] Allocating memory for %" PRIu64 " elements: %.2f MB\n",numberItems,(double)(((double) sizeof(struct address_value)*numberItems)/(double)1048576));
	addressTable = (struct address_value*) malloc(sizeof(struct address_value)*numberItems);
	checkpointer((void *)addressTable,__FILE__,"malloc","addressTable" ,__LINE__ -1 );
		
	if(!initBloomFilterExt(&bloom,numberItems))
		return false;

	i = 0;
	while(i < numberItems)	{
		validAddress = false;
		memset(aux,0,100);
		memset(addressTable[i].value,0,sizeof(struct address_value));
		hextemp = fgets(aux,100,fileDescriptor);
		trim(aux," \t\n\r");			
		r = strlen(aux);
		if(r > 0 && r <= 40)	{
			if(r<40 && isValidBase58String(aux))	{	//Address
				raw_value_length = 25;
				b58tobin(rawvalue,&raw_value_length,aux,r);
				if(raw_value_length == 25)	{
					//hextemp = tohex((char*)rawvalue+1,20);
					bloom_ext_add(&bloom, rawvalue+1 ,sizeof(struct address_value));
					memcpy(addressTable[i].value,rawvalue+1,sizeof(struct address_value));											
					i++;
					validAddress = true;
				}
			}
			if(r == 40 && isValidHex(aux))	{	//RMD
				hexs2bin(aux,rawvalue);				
				bloom_ext_add(&bloom, rawvalue ,sizeof(struct address_value));
				memcpy(addressTable[i].value,rawvalue,sizeof(struct address_value));											
				i++;
				validAddress = true;
			}
		}
		if(!validAddress)	{
			fprintf(stderr,"[I] Ommiting invalid line %s\n",aux);
			numberItems--;
		}
	}
	N = numberItems;
	return true;
}

bool forceReadFileAddressEth(char *fileName)	{
	/* Here we read the original file as usual */
	FILE *fileDescriptor;
	bool validAddress;
	uint64_t numberItems,i;
	size_t r;
	uint8_t rawvalue[50];
	char aux[100],*hextemp;
	fileDescriptor = fopen(fileName,"r");	
	if(fileDescriptor == NULL)	{
		fprintf(stderr,"[E] Error opening the file %s, line %i\n",fileName,__LINE__ - 2);
		return false;
	}
	/*Count lines in the file*/
	numberItems = 0;
	while(!feof(fileDescriptor))	{
		hextemp = fgets(aux,100,fileDescriptor);
		trim(aux," \t\n\r");
		if(hextemp == aux)	{			
			r = strlen(aux);
			if(r >= 40)	{ 
				numberItems++;
			}
		}
	}
	fseek(fileDescriptor,0,SEEK_SET);

	MAXLENGTHADDRESS = 20;		/*20 bytes beacuase we only need the data in binary*/
	N = numberItems;
	
	printf("[+] Allocating memory for %" PRIu64 " elements: %.2f MB\n",numberItems,(double)(((double) sizeof(struct address_value)*numberItems)/(double)1048576));
	addressTable = (struct address_value*) malloc(sizeof(struct address_value)*numberItems);
	checkpointer((void *)addressTable,__FILE__,"malloc","addressTable" ,__LINE__ -1 );


	if(!initBloomFilterExt(&bloom,N))
		return false;

	i = 0;
	while(i < numberItems)	{
		validAddress = false;
		memset(aux,0,100);
		memset(addressTable[i].value,0,sizeof(struct address_value));
		hextemp = fgets(aux,100,fileDescriptor);
		trim(aux," \t\n\r");			
		r = strlen(aux);
		if(r >= 40 && r <= 42){
			switch(r)		{
				case 40:
					if(isValidHex(aux)){
						hexs2bin(aux,rawvalue);
						bloom_ext_add(&bloom, rawvalue ,sizeof(struct address_value));
						memcpy(addressTable[i].value,rawvalue,sizeof(struct address_value));											
						i++;
						validAddress = true;
					}
				break;
				case 42:
					if(isValidHex(aux+2)){
						hexs2bin(aux+2,rawvalue);
						bloom_ext_add(&bloom, rawvalue ,sizeof(struct address_value));
						memcpy(addressTable[i].value,rawvalue,sizeof(struct address_value));											
						i++;
						validAddress = true;
					}
				break;
			}
		}
		if(!validAddress)	{
			fprintf(stderr,"[I] Ommiting invalid line %s\n",aux);
			numberItems--;
		}
	}
	
	fclose(fileDescriptor);
	return true;
}



bool forceReadFileXPoint(char *fileName)	{
	/* Here we read the original file as usual */
	FILE *fileDescriptor;
	uint64_t numberItems,i;
	size_t r,lenaux;
	uint8_t rawvalue[100];
	char aux[1000],*hextemp;
	Tokenizer tokenizer_xpoint{};	//tokenizer
	fileDescriptor = fopen(fileName,"r");	
	if(fileDescriptor == NULL)	{
		fprintf(stderr,"[E] Error opening the file %s, line %i\n",fileName,__LINE__ - 2);
		return false;
	}
	/*Count lines in the file*/
	numberItems = 0;
	while(!feof(fileDescriptor))	{
		hextemp = fgets(aux,1000,fileDescriptor);
		trim(aux," \t\n\r");
		if(hextemp == aux)	{			
			r = strlen(aux);
			if(r >= 40)	{ 
				numberItems++;
			}
		}
	}
	fseek(fileDescriptor,0,SEEK_SET);

	MAXLENGTHADDRESS = 20;		/*20 bytes beacuase we only need the data in binary*/
	
	printf("[+] Allocating memory for %" PRIu64 " elements: %.2f MB\n",numberItems,(double)(((double) sizeof(struct address_value)*numberItems)/(double)1048576));
	addressTable = (struct address_value*) malloc(sizeof(struct address_value)*numberItems);
	checkpointer((void *)addressTable,__FILE__,"malloc","addressTable" ,__LINE__ - 1);
	
	N = numberItems;

	if(!initBloomFilterExt(&bloom,N))
		return false;

	i= 0;
	while(i < N)	{
		memset(aux,0,1000);
		hextemp = fgets(aux,1000,fileDescriptor);
		memset((void *)&addressTable[i],0,sizeof(struct address_value));
		if(hextemp == aux)	{
			trim(aux," \t\n\r");
			stringtokenizer(aux,&tokenizer_xpoint);
			hextemp = nextToken(&tokenizer_xpoint);
			lenaux = strlen(hextemp);
			if(isValidHex(hextemp)) {
				switch(lenaux)	{
					case 64:	/*X value*/
						r = hexs2bin(aux,(uint8_t*) rawvalue);
						if(r)	{
							memcpy(addressTable[i].value,rawvalue,20);
							bloom_ext_add(&bloom,rawvalue,MAXLENGTHADDRESS);
						}
						else	{
							fprintf(stderr,"[E] error hexs2bin\n");
						}
					break;
					case 66:	/*Compress publickey*/
						r = hexs2bin(aux+2, (uint8_t*)rawvalue);
						if(r)	{
							memcpy(addressTable[i].value,rawvalue,20);
							bloom_ext_add(&bloom,rawvalue,MAXLENGTHADDRESS);
						}
						else	{
							fprintf(stderr,"[E] error hexs2bin\n");
						}
					break;
					case 130:	/* Uncompress publickey length*/
						r = hexs2bin(aux, (uint8_t*) rawvalue);
						if(r)	{
								memcpy(addressTable[i].value,rawvalue+2,20);
								bloom_ext_add(&bloom,rawvalue,MAXLENGTHADDRESS);
						}
						else	{
							fprintf(stderr,"[E] error hexs2bin\n");
						}
					break;
					default:
						fprintf(stderr,"[E] Omiting line unknow length size %li: %s\n",lenaux,aux);
					break;
				}
			}
			else	{
				fprintf(stderr,"[E] Ignoring invalid hexvalue %s\n",aux);
			}
			freetokenizer(&tokenizer_xpoint);
		}
		else	{
			fprintf(stderr,"[E] Omiting line : %s\n",aux);
			N--;
		}
		i++;
	}
	fclose(fileDescriptor);
	return true;
}


/*
	I write this as a function because i have the same segment of code in 3 different functions
*/

bool initBloomFilter(struct bloom *bloom_arg,uint64_t items_bloom)	{
	bool r = true;
	printf("[+] Bloom filter for %" PRIu64 " elements.\n",items_bloom);
	if(items_bloom <= 10000)	{
		if(bloom_init2(bloom_arg,10000,0.000001) == 1){
			fprintf(stderr,"[E] error bloom_init for 10000 elements.\n");
			r = false;
		}
	}
	else	{
		if(bloom_init2(bloom_arg,FLAGBLOOMMULTIPLIER*items_bloom,0.000001)	== 1){
			fprintf(stderr,"[E] error bloom_init for %" PRIu64 " elements.\n",items_bloom);
			r = false;
		}
	}
	printf("[+] Loading data to the bloomfilter total: %.2f MB\n",(double)(((double) bloom_arg->bytes)/(double)1048576));

	// Memory check: verify bloom filter fits in available RAM
	if(r) {
		uint64_t bloom_mb = bloom_arg->bytes / (1024 * 1024);
		uint64_t available_ram_mb = g_sysinfo.ram_available;
		uint64_t safe_limit_mb = (available_ram_mb * 80) / 100; // 80% safety margin

		if(bloom_mb > safe_limit_mb) {
			fprintf(stderr,"\n");
			fprintf(stderr,"[W] ========================================================\n");
			fprintf(stderr,"[W] INSUFFICIENT MEMORY FOR BLOOM FILTER\n");
			fprintf(stderr,"[W] ========================================================\n");
			fprintf(stderr,"[W] Bloom filter: %" PRIu64 " MB (~%.1f GB)\n", bloom_mb, (double)bloom_mb/1024);
			fprintf(stderr,"[W] Available:    %" PRIu64 " MB (~%.1f GB)\n", available_ram_mb, (double)available_ram_mb/1024);
			fprintf(stderr,"[W] Safe limit:   %" PRIu64 " MB (80%% of available)\n", safe_limit_mb);
			fprintf(stderr,"[W]\n");
			fprintf(stderr,"[W] Current settings:\n");
			fprintf(stderr,"[W]   Items:      %" PRIu64 "\n", items_bloom);
			fprintf(stderr,"[W]   Multiplier: %d (-z parameter)\n", FLAGBLOOMMULTIPLIER);
			fprintf(stderr,"[W]   Total bloom elements: %" PRIu64 "\n", FLAGBLOOMMULTIPLIER*items_bloom);
			fprintf(stderr,"[W]\n");
			fprintf(stderr,"[W] SUGGESTIONS:\n");
			fprintf(stderr,"[W] --------------------------------------------------------\n");

			// Calculate optimal multiplier that fits
			int suggested_multiplier = (int)(safe_limit_mb * 1024 * 1024 / items_bloom / 3.59);
			if(suggested_multiplier < 1) suggested_multiplier = 1;

			fprintf(stderr,"[W] Try reducing -z parameter to: %d\n", suggested_multiplier);
			fprintf(stderr,"[W]   Command: add -z %d to your command line\n", suggested_multiplier);
			fprintf(stderr,"[W]   This will use ~%" PRIu64 " MB\n",
				(uint64_t)(items_bloom * suggested_multiplier * 3.59 / 1024 / 1024));
			fprintf(stderr,"[W]\n");
			fprintf(stderr,"[W] Or reduce the number of items in your input file\n");
			fprintf(stderr,"[W] ========================================================\n\n");

			// Free the bloom filter we just allocated
			bloom_free(bloom_arg);
			bloom_arg->bf = NULL;

			r = false;
		}
		else {
			// Show memory usage info
			double percent_used = (double)bloom_mb * 100.0 / (double)available_ram_mb;
			fprintf(stderr,"[I] Memory check: %" PRIu64 " MB bloom filter, %" PRIu64 " MB available (%.1f%% used)\n",
				bloom_mb, available_ram_mb, percent_used);
		}
	}

	return r;
}

/*
 * Initialize bloom filter using the fast extended wrapper
 * This provides ~2x speedup on bloom lookups
 */
bool initBloomFilterExt(bloom_extended_t *bloom_arg, uint64_t items_bloom) {
	bool r = true;
	uint64_t effective_items = items_bloom <= 10000 ? 10000 : FLAGBLOOMMULTIPLIER * items_bloom;

	printf("[+] Bloom filter for %" PRIu64 " elements.\n", items_bloom);

	if (bloom_ext_init(bloom_arg, effective_items, 0.000001) != 0) {
		fprintf(stderr, "[E] error bloom_init for %" PRIu64 " elements.\n", effective_items);
		return false;
	}

	uint64_t bloom_bytes = bloom_ext_bytes(bloom_arg);
	printf("[+] Loading data to the bloomfilter total: %.2f MB\n", (double)bloom_bytes / 1048576.0);

	if (bloom_ext_is_fast(bloom_arg)) {
		printf("[+] Using FAST bloom filter (XXH3 + bitmask optimization)\n");
	}

	// Memory check
	uint64_t bloom_mb = bloom_bytes / (1024 * 1024);
	uint64_t available_ram_mb = g_sysinfo.ram_available;
	uint64_t safe_limit_mb = (available_ram_mb * 80) / 100;

	if (bloom_mb > safe_limit_mb) {
		fprintf(stderr, "\n");
		fprintf(stderr, "[W] ========================================================\n");
		fprintf(stderr, "[W] INSUFFICIENT MEMORY FOR BLOOM FILTER\n");
		fprintf(stderr, "[W] ========================================================\n");
		fprintf(stderr, "[W] Bloom filter: %" PRIu64 " MB (~%.1f GB)\n", bloom_mb, (double)bloom_mb / 1024);
		fprintf(stderr, "[W] Available:    %" PRIu64 " MB (~%.1f GB)\n", available_ram_mb, (double)available_ram_mb / 1024);
		fprintf(stderr, "[W] Safe limit:   %" PRIu64 " MB (80%% of available)\n", safe_limit_mb);
		fprintf(stderr, "[W]\n");
		fprintf(stderr, "[W] Try reducing -z parameter or input file size\n");
		fprintf(stderr, "[W] ========================================================\n\n");

		bloom_ext_free(bloom_arg);
		r = false;
	} else {
		double percent_used = (double)bloom_mb * 100.0 / (double)available_ram_mb;
		fprintf(stderr, "[I] Memory check: %" PRIu64 " MB bloom filter, %" PRIu64 " MB available (%.1f%% used)\n",
			bloom_mb, available_ram_mb, percent_used);
	}

	return r;
}

void writeFileIfNeeded(const char *fileName)	{
	//printf("[D] FLAGSAVEREADFILE %i, FLAGREADEDFILE1 %i\n",FLAGSAVEREADFILE,FLAGREADEDFILE1);
	if(FLAGSAVEREADFILE && !FLAGREADEDFILE1)	{
		FILE *fileDescriptor;
		char fileBloomName[30];
		uint8_t checksum[32],hexPrefix[9];
		char dataChecksum[32],bloomChecksum[32];
		size_t bytesWrite;
		uint64_t dataSize;
		if(!sha256_file((const char*)fileName,checksum)){
			fprintf(stderr,"[E] sha256_file error line %i\n",__LINE__ - 1);
			exit(EXIT_FAILURE);
		}
		tohex_dst((char*)checksum,4,(char*)hexPrefix); // we save the prefix (last fourt bytes) hexadecimal value
		snprintf(fileBloomName,30,"data_%s.dat",hexPrefix);
		fileDescriptor = fopen(fileBloomName,"wb");
		dataSize = N * (sizeof(struct address_value));
		if (FLAGDEBUG) {
			printf("[D] size data %" PRIu64 "\n", dataSize);
		}
		if(fileDescriptor != NULL)	{
			printf("[+] Writing file %s ",fileBloomName);
			

			//calculate bloom checksum
			//write bloom checksum (expected value to be checked)
			//write bloom filter structure
			//write bloom filter data


			//calculate dataChecksum
			//write data checksum (expected value to be checked)
			//write data size
			//write data
			
			
			

			sha256((uint8_t*)bloom.orig.bf,bloom.orig.bytes,(uint8_t*)bloomChecksum);
			printf(".");
			bytesWrite = fwrite(bloomChecksum,1,32,fileDescriptor);
			if(bytesWrite != 32)	{
				fprintf(stderr,"[E] Errore writing file, code line %i\n",__LINE__ - 2);
				exit(EXIT_FAILURE);
			}
			printf(".");

			bytesWrite = fwrite(&bloom.orig,1,sizeof(struct bloom),fileDescriptor);
			if(bytesWrite != sizeof(struct bloom))	{
				fprintf(stderr,"[E] Error writing file, code line %i\n",__LINE__ - 2);
				exit(EXIT_FAILURE);
			}
			printf(".");

			bytesWrite = fwrite(bloom.orig.bf,1,bloom.orig.bytes,fileDescriptor);
			if(bytesWrite != bloom.orig.bytes)	{
				fprintf(stderr,"[E] Error writing file, code line %i\n",__LINE__ - 2);
				fclose(fileDescriptor);
				exit(EXIT_FAILURE);
			}
			printf(".");
			
			/*
			if(FLAGDEBUG)	{
				hextemp = tohex((char*)bloom.orig.bf,32);
				printf("\n[D] first 32 bytes bloom : %s\n",hextemp);
				bloom_print(&bloom);
				free(hextemp);
			}
			*/

			
			
			sha256((uint8_t*)addressTable,dataSize,(uint8_t*)dataChecksum);
			printf(".");

			bytesWrite = fwrite(dataChecksum,1,32,fileDescriptor);
			if(bytesWrite != 32)	{
				fprintf(stderr,"[E] Errore writing file, code line %i\n",__LINE__ - 2);
				exit(EXIT_FAILURE);
			}
			printf(".");	
			
			bytesWrite = fwrite(&dataSize,1,sizeof(uint64_t),fileDescriptor);
			if(bytesWrite != sizeof(uint64_t))	{
				fprintf(stderr,"[E] Errore writing file, code line %i\n",__LINE__ - 2);
				exit(EXIT_FAILURE);
			}
			printf(".");
			
			bytesWrite = fwrite(addressTable,1,dataSize,fileDescriptor);
			if(bytesWrite != dataSize)	{
				fprintf(stderr,"[E] Error writing file, code line %i\n",__LINE__ - 2);
				exit(EXIT_FAILURE);
			}
			printf(".");
			
			FLAGREADEDFILE1 = 1;	
			fclose(fileDescriptor);		
			printf("\n");
		}
	}
}

void calcualteindex(int i,Int *key)	{
	if(i == 0)	{
		key->Set(&BSGS_M3);
	}
	else	{
		key->SetInt32(i);
		key->Mult(&BSGS_M3_double);
		key->Add(&BSGS_M3);
	}
}
