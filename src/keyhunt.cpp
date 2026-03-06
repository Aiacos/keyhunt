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
 *   - Support for GPU acceleration (CUDA + OpenCL, multi-vendor)
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
#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#endif
#include "base58/libbase58.h"
#include "bloom/bloom.h"
#include "bloom/bloom_wrapper.h"
#include "sha3/sha3.h"
#include "core/util.h"
#include "core/workqueue.h"
#include "core/sysinfo.h"
#include "core/parameter_validator.h"
#include "error/enhanced_error.h"
#include "gpu/gpu_backend.h"
#include "gpu/gpu_multi_worker.h"
#include "core/config.h"
#include "config/config.h"
#include "hybrid/adaptive_scheduler.h"
#include "wizard/wizard.h"
#include "benchmark.h"
#include "diagnostics/diagnostics.h"
#include "output.h"
#include "progress.h"
#include "cli.h"
#include "bsgs/bsgs_fast.h"
#include "bsgs/bsgs_sort.h"
#include "sort/sort.h"
#include "crypto/address_util.h"
#include "crypto/bloom_init.h"
#include "io/io.h"
#include "search/search_common.h"
#include "modes/modes.h"
#include "modes/bsgs_globals.h"
#include "gpu/gpu_dispatch.h"
#include "monitoring/monitoring.h"

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
#else
#include <signal.h>
#ifdef __linux__
#include <sys/mman.h>
#include <sys/random.h>
#include <linux/random.h>
#endif
#endif

#include "util/thread_util.h"
#include "util/profiling.h"
#include "util/work_queue.h"

/* ============================================================================
 * Config Migration Helpers
 * ============================================================================ */

static key_format_t flagsearch_to_key_format(int flagsearch) {
    switch (flagsearch) {
        case 0: return KEYTYPE_UNCOMPRESSED;
        case 1: return KEYTYPE_COMPRESSED;
        case 2: return KEYTYPE_BOTH;
        default: return KEYTYPE_BOTH;
    }
}

typedef kh_profile_scope_t profile_scope_t;

/* ============================================================================
 * Global variable definitions
 * ============================================================================ */

/* struct checksumsha256 now declared in modes/bsgs_globals.h */

struct address_value {
    uint8_t value[20];
};

struct tothread {
    int nt;
    char *rs;
    char *rpt;
};

struct bPload {
    uint32_t threadid;
    uint64_t from;
    uint64_t to;
    uint64_t counter;
    uint64_t workload;
    uint32_t aux;
    uint32_t finished;
};

#if defined(_MSC_VER)
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
    union {
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

uint32_t CPU_GRP_SIZE = 1024;
int OPTIMAL_THREADS = 0;
uint64_t OPTIMAL_N = 0;
int OPTIMAL_KFACTOR = 0;

std::vector<Point> Gn;
Point _2Gn;

uint32_t THREADBPWORKLOAD = 1048576;
bool g_avx2_available = false;
static keyhunt_ini_config_t g_config;
static bool g_config_loaded = false;
static const char *g_save_config_path = NULL;
static progress_state_t g_progress_state;
static bool g_progress_enabled = false;

static WorkQueue<Int> g_workQueue;

/* env_truthy_kh: check environment variable for truthy value */
static inline bool env_truthy_kh(const char *name) {
    const char *v = getenv(name);
    if (!v || !*v) return false;
    if (v[0] == '0' && v[1] == '\0') return false;
    if ((v[0] == 'f' || v[0] == 'F') && (v[1] == 'a' || v[1] == 'A')) return false;
    if ((v[0] == 'n' || v[0] == 'N') && (v[1] == 'o' || v[1] == 'O')) return false;
    return true;
}

#ifndef _WIN64
static void configure_work_queue(size_t threadCount);
void shutdown_work_queue();
#endif
bool acquire_base_key(Int &key);

void sleep_ms(int milliseconds);

/* Vanity functions (defined in search/search_vanity.cpp) */
bool vanityrmdmatch(unsigned char *rmdhash);
void writevanitykey(bool compress,Int *key, keyhunt_config_t *config);
int addvanity(char *target);
int minimum_same_bytes(unsigned char* A,unsigned char* B, int length);

keyhunt_config_t *g_kh_config_ptr = nullptr;

const char *modes[7] = {"xpoint","address","bsgs","rmd160","pub2rmd","minikeys","vanity"};
const char *cryptos[3] = {"btc","eth","all"};
const char *publicsearch[3] = {"uncompress","compress","both"};
const char *default_fileName = "addresses.txt";

std::atomic<int> THREADOUTPUT{0};
char *bit_range_str_min;
char *bit_range_str_max;

platform_thread_t *tid = NULL;
platform_mutex_t write_keys;
platform_mutex_t write_random;
platform_mutex_t bsgs_thread;
platform_mutex_t *bPload_mutex = NULL;

uint64_t FINISHED_THREADS_COUNTER = 0;
uint64_t FINISHED_THREADS_BP = 0;
uint64_t THREADCYCLES = 0;
uint64_t THREADCOUNTER = 0;
std::atomic<uint64_t> FINISHED_ITEMS{0};
uint64_t OLDFINISHED_ITEMS = 0;

uint8_t byte_encode_crypto = 0x00;

int vanity_rmd_targets = 0;
int vanity_rmd_total = 0;
int *vanity_rmd_limits = NULL;
uint8_t ***vanity_rmd_limit_values_A = NULL;
uint8_t ***vanity_rmd_limit_values_B = NULL;
int vanity_rmd_minimun_bytes_check_length = 999999;
char **vanity_address_targets = NULL;
struct bloom *vanity_bloom = NULL;
bloom_extended_t bloom;

struct thread_counter *steps = NULL;
struct thread_flag *ends = NULL;
uint64_t N = 0;

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
int FLAGVISUAL = 0;
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

std::atomic<uint64_t> g_gpu_keys_checked{0};
std::atomic<uint64_t> g_gpu_keys_checked_cur{0};
std::atomic<int> g_gpu_should_stop{0};
int g_gpu_range_percent = 0;

static gpu_multi_worker_t *g_multi_gpu_workers = NULL;
static volatile sig_atomic_t g_sigint_received = 0;

#ifndef _WIN64
static void sigint_handler(int sig) {
    (void)sig;
    g_sigint_received = 1;
    g_gpu_should_stop.store(1, std::memory_order_release);
}
#endif

void check_sigint_cleanup(void) {
    if (g_sigint_received && g_multi_gpu_workers != NULL) {
        output_info("\nReceived Ctrl+C, stopping multi-GPU workers...\n");
        gpu_worker_stop(g_multi_gpu_workers, 10000);
        g_multi_gpu_workers = NULL;
    }
}

int bitrange = 0;
char *str_N = NULL;
char *range_start = NULL;
char *range_end = NULL;
char *str_stride = NULL;
Int stride;
Int OUTPUTSECONDS;

uint64_t N_SEQUENTIAL_MAX = 4096;

system_info_t g_sysinfo;
gpu_backend_info_t g_gpu_backend_info;

struct address_value *addressTable;

Int ONE;
Int ZERO;
Int MPZAUX;
Int n_range_start;
Int n_range_end;
Int n_range_diff;
Int n_range_aux;

/* Cleanup functions */
static void cleanup_general_resources(void) {
    if (addressTable != NULL) {
        free(addressTable);
        addressTable = NULL;
    }
}

static void cleanup_all_resources(void) {
    cleanup_general_resources();
    cleanup_bsgs_resources();
}

Int lambda,lambda2,beta,beta2;
Secp256K1 *secp;

#ifndef _WIN64
static void configure_work_queue(size_t threadCount) {
    if (FLAGRANDOM) {
        g_workQueue.shutdown();
        return;
    }
    uint64_t chunk = N_SEQUENTIAL_MAX;
    size_t prefetch = std::max<size_t>(threadCount * 4, static_cast<size_t>(64));
    g_workQueue.configure(&n_range_start, &n_range_end, chunk, prefetch);
    g_workQueue.start();
}

void shutdown_work_queue() {
    g_workQueue.shutdown();
}
#endif

static void maybe_adjust_cpu_sequential_max(size_t threadCount,
                                            Int &cpuStart,
                                            Int &rangeEnd,
                                            const char *env_override,
                                            const char *tag) {
    if (FLAG_N) return;
    if (threadCount == 0) return;
    uint64_t span = 0;
    if (!monitoring_span_u64_from_range(cpuStart, rangeEnd, span) || span == 0) return;

    {
        const uint64_t min_blocks = (uint64_t)threadCount * 4ULL;
        if (N_SEQUENTIAL_MAX > 0 && (span / N_SEQUENTIAL_MAX) >= min_blocks) return;
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
    const uint64_t kMin = 8ULL * kAlign;
    const uint64_t kMax = 256ULL * 1024ULL * kAlign;

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

/* Thread-local block cache for work-stealing mode */
thread_local Int cpu_cached_block_start;
thread_local Int cpu_cached_block_end;
thread_local bool cpu_cached_block_valid = false;

bool acquire_base_key(Int &key) {
    if (g_work_pool.enabled.load(std::memory_order_acquire)) {
        for (;;) {
            if (!cpu_cached_block_valid || !cpu_cached_block_start.IsLower(&cpu_cached_block_end)) {
                if (!g_work_pool.get_block(cpu_cached_block_start, cpu_cached_block_end)) {
                    return false;
                }
                cpu_cached_block_valid = true;
            }
            if (!cpu_cached_block_start.IsLower(&cpu_cached_block_end)) {
                cpu_cached_block_valid = false;
                continue;
            }
            key.Set(&cpu_cached_block_start);
            cpu_cached_block_start.Add(N_SEQUENTIAL_MAX);
            return true;
        }
    }

#ifndef _WIN64
    if (!FLAGRANDOM && g_workQueue.enabled()) {
        return g_workQueue.pop(key);
    }
#endif
    if (FLAGRANDOM) {
        key.Rand(&n_range_start,&n_range_end);
        return true;
    }
    platform_mutex_lock(&write_random);
    bool hasWork = n_range_start.IsLower(&n_range_end);
    if(hasWork) {
        key.Set(&n_range_start);
        n_range_start.Add(N_SEQUENTIAL_MAX);
    }
    platform_mutex_unlock(&write_random);
    return hasWork;
}

/* ============================================================================
 * main()
 * ============================================================================ */

int main(int argc, char **argv) {
    char buffer[2048];
    char rawvalue[32];
    struct tothread *tt;
    Tokenizer t{};
    char *fileName = NULL;
    char *hextemp = NULL;
    char *aux = NULL;
    char *aux2 = NULL;
    char *str_seconds = NULL;
    char *str_total = NULL;
    char *str_pretotal = NULL;
    char *str_divpretotal = NULL;
    FILE *fd;
    uint64_t i;
    int continue_flag,check_flag,c,salir,index_value,j;
    Int total,pretotal,debugcount_mpz,seconds,div_pretotal,int_aux,int_r,int_q,int58;

    platform_thread_t gpu_thread_id = 0;
    gpu_hybrid_args_t gpu_hybrid_args = {};
    int gpu_hybrid_started = 0;

    platform_mutex_init(&write_keys);
    platform_mutex_init(&write_random);
    platform_mutex_init(&bsgs_thread);

    srand(time(NULL));
    atexit(cleanup_all_resources);

    secp = new Secp256K1();
    secp->Init();
    OUTPUTSECONDS.SetInt32(30);
    ZERO.SetInt32(0);
    ONE.SetInt32(1);
    BSGS_GROUP_SIZE.SetInt32(CPU_GRP_SIZE);

#if defined(_WIN64) && !defined(__CYGWIN__)
    rseed(clock() + time(NULL) + thread_rand());
#else
    unsigned long rseedvalue;
    int bytes_read = getrandom(&rseedvalue, sizeof(unsigned long), GRND_NONBLOCK);
    if(bytes_read > 0) {
        rseed(rseedvalue);
    } else {
        output_warning("getrandom() failed (bytes_read=%d), using fallback RNG\n", bytes_read);
        rseed(clock() + time(NULL) + thread_rand() * thread_rand());
    }
#endif
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

    keyhunt_config_t config;
    kh_config_init(&config);
    g_kh_config_ptr = &config;

    bsgs_context_t bsgs_ctx;
    memset(&bsgs_ctx, 0, sizeof(bsgs_ctx));

    if (getenv("KEYHUNT_SKIP_SYSINFO")) {
        output_warning("Skipping system detection (KEYHUNT_SKIP_SYSINFO set)\n");
        output_info("Using safe default parameters\n");
        memset(&g_sysinfo, 0, sizeof(g_sysinfo));
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
    gpu_backend_init(&g_gpu_backend_info);

    g_avx2_available = ripemd160_avx2_available();
    if (g_avx2_available) {
        output_success("AVX2 detected: Using optimized 8-way parallel RIPEMD160\n");
    } else {
        output_info("AVX2 not available: Using SSE2 4-way parallel RIPEMD160\n");
    }
    bsgs_fast_set_cpu_features(g_avx2_available, false);

    OPTIMAL_THREADS = g_sysinfo.recommended_threads;
    OPTIMAL_N = g_sysinfo.recommended_n;
    OPTIMAL_KFACTOR = g_sysinfo.recommended_kfactor;
    CPU_GRP_SIZE = 1024;
    output_info("Using CPU_GRP_SIZE: %u (proven optimal)\n", CPU_GRP_SIZE);

    /* Help, wizard, benchmark, diagnostic checks */
    for (int ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--help") == 0) { menu(); }
        if (strcmp(argv[ai], "--wizard") == 0 || strcmp(argv[ai], "-W") == 0) {
            int result = wizard_run();
            exit(result < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
        }
        if (strcmp(argv[ai], "--wizard-client") == 0 && ai + 1 < argc) {
            int result = wizard_client_run_auto(argv[ai + 1]);
            exit(result < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
        }
        if (strcmp(argv[ai], "--benchmark") == 0) {
            bool submit_to_community = false;
            for (int check_i = 1; check_i < argc; check_i++) {
                if (strcmp(argv[check_i], "--submit-benchmark") == 0) {
                    submit_to_community = true;
                    break;
                }
            }
            benchmark_result_t bench_result;
            benchmark_run(&bench_result, 15, submit_to_community);
            benchmark_print_results(&bench_result, 66);
            exit(EXIT_SUCCESS);
        }
        if (strcmp(argv[ai], "--perf-compare") == 0) {
            benchmark_show_community_stats();
            exit(EXIT_SUCCESS);
        }
        if (strcmp(argv[ai], "--diagnose") == 0) {
            diagnostic_report_t report;
            diagnostics_run(&report);
            diagnostics_print_report(&report);
            exit(EXIT_SUCCESS);
        }
    }

    /* Configuration file handling */
    config_init(&g_config);
    const char *config_file_arg = NULL;
    int new_argc = 1;
    for (int ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--config") == 0 && ai + 1 < argc) {
            config_file_arg = argv[ai + 1];
            ai++;
            continue;
        } else if (strncmp(argv[ai], "--config=", 9) == 0) {
            config_file_arg = argv[ai] + 9;
            continue;
        } else if (strcmp(argv[ai], "--save-config") == 0) {
            g_save_config_path = "keyhunt.conf";
            continue;
        } else if (strncmp(argv[ai], "--save-config=", 14) == 0) {
            g_save_config_path = argv[ai] + 14;
            continue;
        } else if (strcmp(argv[ai], "--visual") == 0) {
            FLAGVISUAL = 1;
            continue;
        }
        argv[new_argc++] = argv[ai];
    }
    argc = new_argc;
    argv[argc] = NULL;

    if (config_file_arg) {
        if (config_load(&g_config, config_file_arg) == 0) {
            output_success("Loaded configuration from '%s'\n", config_file_arg);
            g_config_loaded = true;
        } else {
            error_report_t report;
            error_file_io(config_file_arg, "load configuration",
                          "File not found, invalid format, or permission denied", &report);
            error_fatal(&report);
        }
    } else {
        if (config_load_default(&g_config) == 0) {
            output_success("Loaded configuration from 'keyhunt.conf'\n");
            g_config_loaded = true;
        }
    }

    if (g_config_loaded) {
        if (g_config.threads_set && g_config.threads > 0) {
            NTHREADS = g_config.threads;
            FLAGTHREADS = 1;
        }
        if (g_config.gpu_set) {
            if (g_config.gpu_enabled == 0) { FLAGGPU = 0; FLAGGPU_FULL = 0; }
            else if (g_config.gpu_enabled > 0) { FLAGGPU = 1; FLAGGPU_FULL = 1; }
        }
        if (g_config.mode_set) {
            if (strcasecmp(g_config.mode, "hybrid") == 0) {
                FLAGGPU = 1; FLAGGPU_FULL = 1;
                FLAGGPU_HYBRID.store(1, std::memory_order_relaxed);
            } else if (strcasecmp(g_config.mode, "gpu") == 0) {
                FLAGGPU = 1; FLAGGPU_FULL = 1;
            } else if (strcasecmp(g_config.mode, "cpu") == 0) {
                FLAGGPU = 0; FLAGGPU_FULL = 0;
            }
        }
    }

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "-h") == 0)) {
        sysinfo_print(&g_sysinfo);
    }

    /* ========== CLI getopt loop ========== */
    while ((c = getopt(argc, argv, "deh6MqRSB:b:c:C:E:f:I:k:l:m:N:n:p:r:s:t:v:G:8:z:P")) != -1) {
        switch(c) {
            case 'h': menu(); break;
            case '6': FLAGSKIPCHECKSUM = 1; output_warning("Skipping checksums on files\n"); break;
            case 'B':
                index_value = indexOf(optarg,bsgs_modes,5);
                if(index_value >= 0 && index_value <= 4) { FLAGBSGSMODE = index_value; }
                else { output_warning("Ignoring unknow bsgs mode %s\n",optarg); }
                break;
            case 'b':
                bitrange = strtol(optarg,NULL,10);
                if(bitrange > 0 && bitrange <=256) {
                    MPZAUX.Set(&ONE); MPZAUX.ShiftL(bitrange-1);
                    bit_range_str_min = MPZAUX.GetBase16();
                    checkpointer((void *)bit_range_str_min,__FILE__,"malloc","bit_range_str_min",__LINE__-1);
                    MPZAUX.Set(&ONE); MPZAUX.ShiftL(bitrange);
                    if(MPZAUX.IsGreater(&secp->order)) { MPZAUX.Set(&secp->order); }
                    bit_range_str_max = MPZAUX.GetBase16();
                    checkpointer((void *)bit_range_str_max,__FILE__,"malloc","bit_range_str_min",__LINE__-1);
                    FLAGBITRANGE = 1;
                } else { output_error("invalid bits param: %s.\n",optarg); }
                break;
            case 'c':
                index_value = indexOf(optarg,cryptos,3);
                switch(index_value) {
                    case 0: FLAGCRYPTO = CRYPTO_BTC; break;
                    case 1: FLAGCRYPTO = CRYPTO_ETH; output_success("Setting search for ETH adddress.\n"); break;
                    default: FLAGCRYPTO = CRYPTO_NONE; output_error("Unknown crypto value %s\n",optarg); exit(EXIT_FAILURE); break;
                }
                break;
            case 'C':
                if(strlen(optarg) == 22) {
                    FLAGBASEMINIKEY = 1;
                    str_baseminikey = (char*) malloc(23);
                    checkpointer((void *)str_baseminikey,__FILE__,"malloc","str_baseminikey",__LINE__-1);
                    raw_baseminikey = (char*) malloc(23);
                    checkpointer((void *)raw_baseminikey,__FILE__,"malloc","raw_baseminikey",__LINE__-1);
                    strncpy(str_baseminikey,optarg,22); str_baseminikey[22] = '\0';
                    for(i = 0; i< 21; i++) {
                        if(strchr(Ccoinbuffer,str_baseminikey[i+1]) != NULL) {
                            raw_baseminikey[i] = (int)(strchr(Ccoinbuffer,str_baseminikey[i+1]) - Ccoinbuffer) % 58;
                        } else { output_error("invalid character in minikey\n"); exit(EXIT_FAILURE); }
                    }
                    raw_baseminikey[21] = '\0';
                } else { output_error("Invalid Minikey length %zu : %s\n",strlen(optarg),optarg); exit(EXIT_FAILURE); }
                break;
            case 'd': FLAGDEBUG = 1; output_success("Flag DEBUG enabled\n"); break;
            case 'e':
                FLAGENDOMORPHISM = 1; output_success("Endomorphism enabled\n");
                lambda.SetBase16("5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
                lambda2.SetBase16("ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");
                beta.SetBase16("7ae96a2b657c07106e64479eac3434e99cf0497512f58995c1396c28719501ee");
                beta2.SetBase16("851695d49a83f8ef919bb86153cbcb16630fb68aed0a766a3ec693d68e6afa40");
                break;
            case 'f': FLAGFILE = 1; fileName = optarg; break;
            case 'G':
                if (optarg) {
                    if (strcasecmp(optarg, "off") == 0 || strcasecmp(optarg, "no") == 0 || strcmp(optarg, "0") == 0) {
                        FLAGGPU = 0; FLAGGPU_FULL = 0;
                    } else if (strcasecmp(optarg, "auto") == 0) {
                        FLAGGPU = -1; FLAGGPU_FULL = -1;
                    } else if (strcasecmp(optarg, "hash") == 0) {
                        FLAGGPU = 1; FLAGGPU_FULL = 0;
                        output_success("GPU hash-only mode (CPU generates points, GPU hashes)\n");
                    } else if (strcasecmp(optarg, "full") == 0 || strcasecmp(optarg, "on") == 0 || strcasecmp(optarg, "yes") == 0 || strcmp(optarg, "1") == 0) {
                        FLAGGPU = 1; FLAGGPU_FULL = 1;
                        output_success("GPU full mode (ECC + hash160 + matching on GPU)\n");
                    } else if (strcasecmp(optarg, "hybrid") == 0) {
                        FLAGGPU = 1; FLAGGPU_FULL = 1;
                        FLAGGPU_HYBRID.store(1, std::memory_order_relaxed);
                        output_success("GPU hybrid mode (GPU + CPU in parallel for maximum throughput)\n");
                    } else {
                        output_warning("Invalid -G value '%s', use: off|auto|hash|full|hybrid\n", optarg);
                    }
                }
                break;
            case 'I': FLAGSTRIDE = 1; str_stride = optarg; break;
            case 'k':
                KFACTOR = (int)strtol(optarg,NULL,10);
                if(KFACTOR <= 0) { KFACTOR = 1; }
                output_success("K factor %i\n",KFACTOR);
                break;
            case 'l':
                switch(indexOf(optarg,publicsearch,3)) {
                    case SEARCH_UNCOMPRESS: FLAGSEARCH = SEARCH_UNCOMPRESS; output_success("Search uncompress only\n"); break;
                    case SEARCH_COMPRESS: FLAGSEARCH = SEARCH_COMPRESS; output_success("Search compress only\n"); break;
                    case SEARCH_BOTH: FLAGSEARCH = SEARCH_BOTH; output_success("Search both compress and uncompress\n"); break;
                }
                break;
            case 'M': FLAGMATRIX = 1; output_success("Matrix screen\n"); break;
            case 'P': FLAGPROGRESSBAR = 1; output_success("Segmented progress indicator enabled\n"); break;
            case 'm':
                switch(indexOf(optarg,modes,7)) {
                    case MODE_XPOINT: FLAGMODE = MODE_XPOINT; output_success("Mode xpoint\n"); break;
                    case MODE_ADDRESS: FLAGMODE = MODE_ADDRESS; output_success("Mode address\n"); break;
                    case MODE_BSGS: FLAGMODE = MODE_BSGS; break;
                    case MODE_RMD160: FLAGMODE = MODE_RMD160; FLAGCRYPTO = CRYPTO_BTC; output_success("Mode rmd160\n"); break;
                    case MODE_PUB2RMD: FLAGMODE = MODE_PUB2RMD; output_success("Mode pub2rmd was removed\n"); exit(0); break;
                    case MODE_MINIKEYS: FLAGMODE = MODE_MINIKEYS; output_success("Mode minikeys\n"); break;
                    case MODE_VANITY:
                        FLAGMODE = MODE_VANITY; output_success("Mode vanity\n");
                        if(vanity_bloom == NULL){
                            vanity_bloom = (struct bloom*) calloc(1,sizeof(struct bloom));
                            checkpointer((void *)vanity_bloom,__FILE__,"calloc","vanity_bloom",__LINE__-1);
                        }
                        break;
                    default: output_error("Unknown mode value %s\n",optarg); exit(EXIT_FAILURE); break;
                }
                break;
            case 'n': FLAG_N = 1; str_N = optarg; break;
            case 'q': FLAGQUIET = 1; break;
            case 'R': output_success("Random mode\n"); FLAGRANDOM = 1; FLAGBSGSMODE = 3; break;
            case 'r':
                if(optarg != NULL) {
                    stringtokenizer(optarg,&t);
                    switch(t.n) {
                        case 1:
                            range_start = nextToken(&t);
                            if(isValidHex(range_start)) { FLAGRANGE = 1; range_end = secp->order.GetBase16(); }
                            else { output_error("Invalid hexstring : %s.\n",range_start); }
                            break;
                        case 2:
                            range_start = nextToken(&t);
                            range_end = nextToken(&t);
                            if(isValidHex(range_start) && isValidHex(range_end)) { FLAGRANGE = 1; }
                            else { if(isValidHex(range_start)) { output_error("Invalid hexstring : %s\n",range_start); } else { output_error("Invalid hexstring : %s\n",range_end); } }
                            break;
                        default: output_error("Unknown number of Range Params: %i\n",t.n); break;
                    }
                }
                break;
            case 's':
                OUTPUTSECONDS.SetBase10(optarg);
                if(OUTPUTSECONDS.IsLower(&ZERO)) { OUTPUTSECONDS.SetInt32(30); }
                if(OUTPUTSECONDS.IsZero()) { output_success("Turn off stats output\n"); }
                else { hextemp = OUTPUTSECONDS.GetBase10(); output_success("Stats output every %s seconds\n",hextemp); free(hextemp); }
                break;
            case 'S': FLAGSAVEREADFILE = 1; break;
            case 't':
                NTHREADS = strtol(optarg,NULL,10);
                if(NTHREADS <= 0) { NTHREADS = 1; }
                FLAGTHREADS = 1;
                output_success((NTHREADS > 1) ? "Threads : %u (user-specified)\n": "Thread : %u (user-specified)\n",NTHREADS);
                break;
            case 'v':
                FLAGVANITY = 1;
                if(vanity_bloom == NULL){
                    vanity_bloom = (struct bloom*) calloc(1,sizeof(struct bloom));
                    checkpointer((void *)vanity_bloom,__FILE__,"calloc","vanity_bloom",__LINE__-1);
                }
                if(isValidBase58String(optarg)) {
                    if(addvanity(optarg) > 0) { output_success("Added Vanity search : %s\n",optarg); }
                    else { output_success("Vanity search \"%s\" was NOT Added\n",optarg); }
                } else { output_success("The string \"%s\" is not Valid Base58\n",optarg); }
                break;
            case '8':
                if(strlen(optarg) == 58) { Ccoinbuffer = optarg; output_success("Base58 for Minikeys %s\n",Ccoinbuffer); }
                else { output_error("The base58 alphabet must be 58 characters long.\n"); exit(EXIT_FAILURE); }
                break;
            case 'z':
                FLAGBLOOMMULTIPLIER = strtol(optarg,NULL,10);
                if(FLAGBLOOMMULTIPLIER <= 0) { FLAGBLOOMMULTIPLIER = 1; }
                output_success("Bloom Size Multiplier %i\n",FLAGBLOOMMULTIPLIER);
                break;
            default: output_error("Unknown option -%c\n",c); exit(EXIT_FAILURE); break;
        }
    }

    output_init(FLAGQUIET ? OUTPUT_MINIMAL : OUTPUT_NORMAL);

    /* ========== Parameter Validation ========== */
    {
        uint64_t user_n_value = 0;
        if (FLAG_N && str_N) {
            if (str_N[0] == '0' && str_N[1] == 'x') { user_n_value = strtoull(str_N + 2, NULL, 16); }
            else { user_n_value = strtoull(str_N, NULL, 10); }
        }
        int threads_to_validate = FLAGTHREADS ? NTHREADS : 0;
        uint32_t batch_size = CPU_GRP_SIZE;
        bool validation_ok = validate_all_parameters(
            &threads_to_validate, &user_n_value, &KFACTOR, &batch_size, &g_sysinfo, true);
        NTHREADS = threads_to_validate;
        CPU_GRP_SIZE = batch_size;
        if (FLAG_N && user_n_value != 0) {
            char corrected_n[32];
            snprintf(corrected_n, sizeof(corrected_n), "0x%llx", (unsigned long long)user_n_value);
            str_N = strdup(corrected_n);
            if (str_N == NULL) { output_error("Memory allocation failed for N parameter\n"); exit(EXIT_FAILURE); }
        }
        if (!FLAG_N && FLAGMODE == MODE_BSGS && OPTIMAL_N > 0) {
            char auto_n[32];
            snprintf(auto_n, sizeof(auto_n), "0x%llx", (unsigned long long)OPTIMAL_N);
            str_N = strdup(auto_n);
            if (str_N == NULL) { output_error("Memory allocation failed for N parameter\n"); exit(EXIT_FAILURE); }
            FLAG_N = 1;
        }
        if (!validation_ok) { output_warning("Some parameters were auto-corrected for safety\n"); }
    }

    /* Display configuration summary */
    {
        const char *mode_name = (FLAGMODE >= 0 && FLAGMODE < 7) ? modes[FLAGMODE] : "unknown";
        const char *gpu_name = NULL;
        if (FLAGGPU || FLAGGPU_HYBRID) {
            gpu_name = g_gpu_backend_info.name[0] ? g_gpu_backend_info.name : "GPU";
        }
        output_banner(version, mode_name, NTHREADS, gpu_name, bitrange);
    }

    /* Save configuration if requested */
    if (g_save_config_path) {
        if (NTHREADS > 0) { g_config.threads = NTHREADS; g_config.threads_set = true; }
        if (FLAGGPU_HYBRID) { strncpy(g_config.mode, "hybrid", sizeof(g_config.mode) - 1); }
        else if (FLAGGPU && FLAGGPU_FULL) { strncpy(g_config.mode, "gpu", sizeof(g_config.mode) - 1); }
        else { strncpy(g_config.mode, "cpu", sizeof(g_config.mode) - 1); }
        g_config.mode_set = true;
        g_config.gpu_enabled = FLAGGPU ? 1 : 0; g_config.gpu_set = true;
        g_config.batch_size = CPU_GRP_SIZE; g_config.batch_size_set = true;
        if (config_save(&g_config, g_save_config_path) == 0) {
            output_success("Configuration saved. You can now use it with: --config %s\n", g_save_config_path);
        } else { output_error("Failed to save configuration to %s. Check disk space and permissions.\n", g_save_config_path); }
    }

    if (FLAGBSGSMODE == MODE_BSGS && FLAGENDOMORPHISM) {
        output_error("Endomorphism doesn't work with BSGS\n"); exit(EXIT_FAILURE);
    }
    if (FLAGBSGSMODE == MODE_BSGS && FLAGSTRIDE) {
        output_error("Stride doesn't work with BSGS\n"); exit(EXIT_FAILURE);
    }
    if(FLAGSTRIDE) {
        if(str_stride[0] == '0' && str_stride[1] == 'x') { stride.SetBase16(str_stride+2); }
        else { stride.SetBase10(str_stride); }
        output_success("Stride : %s\n",stride.GetBase10());
    } else {
        FLAGSTRIDE = 1;
        stride.Set(&ONE);
    }
    init_generator();
    if(FLAGMODE == MODE_BSGS) { output_success("Mode BSGS %s\n",bsgs_modes[FLAGBSGSMODE]); }
    if(FLAGFILE == 0) { fileName = (char*) default_fileName; }

    if(FLAGMODE == MODE_ADDRESS && FLAGCRYPTO == CRYPTO_NONE) {
        FLAGCRYPTO = CRYPTO_BTC; output_success("Setting search for btc adddress\n");
    }
    if(FLAGMODE == MODE_RMD160 && FLAGCRYPTO == CRYPTO_NONE) {
        FLAGCRYPTO = CRYPTO_BTC; output_success("Setting search for btc rmd160\n");
    }

    /* ========== GPU Mode Resolution ========== */
    {
        int gpu_available = gpu_backend_available();
        const bool wantCompressed = (FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH);
        const bool wantUncompressed = (FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH);
        const int mode_supports_gpu_full = (FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_ADDRESS) &&
                        FLAGCRYPTO == CRYPTO_BTC && !FLAGENDOMORPHISM && (wantCompressed || wantUncompressed);
        const int mode_supports_gpu_hash = (FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_ADDRESS) &&
                        FLAGCRYPTO == CRYPTO_BTC && !FLAGENDOMORPHISM && wantCompressed && !wantUncompressed;

        if (FLAGGPU != 0 || FLAGGPU_FULL != 0) {
            if (gpu_available) {
                int available_backends = gpu_enumerate_backends();
                gpu_backend_type_t current_backend = gpu_backend_get_type();
                output_success("GPU Backend: %s\n", gpu_backend_type_name(current_backend));
                if (current_backend == GPU_BACKEND_TYPE_UNIFIED) {
                    output_info("  Total devices: %d across multiple vendors\n", g_gpu_backend_info.gpu_count);
                    if (available_backends & (1 << GPU_BACKEND_TYPE_CUDA)) output_info("  - CUDA backend available (NVIDIA GPUs)\n");
                    if (available_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) output_info("  - OpenCL backend available (AMD/Intel GPUs)\n");
                    if (g_gpu_backend_info.name[0]) { output_info("  Primary device: %s", g_gpu_backend_info.name); if (g_gpu_backend_info.vendor[0]) output_info(" (%s)", g_gpu_backend_info.vendor); output_info("\n"); }
                    output_info("  Compute units: %d, VRAM: %lu MB\n", g_gpu_backend_info.multiprocessors, (unsigned long)g_gpu_backend_info.vram_mb);
                } else if (current_backend == GPU_BACKEND_TYPE_CUDA) {
                    output_success("  CUDA device: %s (%d SMs, %lu MB VRAM)\n", g_gpu_backend_info.name[0] ? g_gpu_backend_info.name : "NVIDIA GPU", g_gpu_backend_info.multiprocessors, (unsigned long)g_gpu_backend_info.vram_mb);
                } else if (current_backend == GPU_BACKEND_TYPE_OPENCL) {
                    output_success("  OpenCL device: %s", g_gpu_backend_info.name[0] ? g_gpu_backend_info.name : "GPU");
                    if (g_gpu_backend_info.vendor[0]) output_success(" (%s)", g_gpu_backend_info.vendor);
                    output_success("\n");
                    output_info("  Compute units: %d, VRAM: %lu MB\n", g_gpu_backend_info.multiprocessors, (unsigned long)g_gpu_backend_info.vram_mb);
                }
            } else {
                output_warning("No GPU devices detected\n");
#if defined(HAVE_CUDA_BACKEND) && defined(HAVE_OPENCL_BACKEND)
                output_info("Build supports: CUDA (NVIDIA) and OpenCL (AMD/Intel)\n");
#elif defined(HAVE_CUDA_BACKEND)
                output_info("Build supports: CUDA only (NVIDIA GPUs)\n");
#elif defined(HAVE_OPENCL_BACKEND)
                output_info("Build supports: OpenCL only (AMD/Intel GPUs)\n");
#else
                output_info("GPU backends not compiled - rebuild with CUDA or OpenCL support\n");
#endif
            }
        }

        if (FLAGGPU == -1 || FLAGGPU_FULL == -1) {
            if (gpu_available && mode_supports_gpu_full) {
                FLAGGPU = 1; FLAGGPU_FULL = 1;
                output_success("GPU auto: using full mode (ECC + hash160 + matching on GPU)\n");
            } else {
                FLAGGPU = 0; FLAGGPU_FULL = 0;
                if (!gpu_available) output_info("GPU auto: falling back to CPU (no GPU available)\n");
                else if (!mode_supports_gpu_full) output_info("GPU auto: falling back to CPU (mode not supported)\n");
            }
        }

        if ((FLAGGPU == 1 || FLAGGPU_FULL == 1) && !gpu_available) {
            output_warning("GPU requested but not available, falling back to CPU\n");
            FLAGGPU = 0; FLAGGPU_FULL = 0;
        }
        if ((FLAGGPU == 1 || FLAGGPU_FULL == 1) && !stride.IsOne()) {
            output_warning("GPU mode requires stride=1 (-I 1). Falling back to CPU.\n");
            FLAGGPU = 0; FLAGGPU_FULL = 0;
            FLAGGPU_HYBRID.store(0, std::memory_order_relaxed);
        }
        if (FLAGGPU_FULL == 1 && !mode_supports_gpu_full) {
            output_warning("GPU FULL not supported for this mode/options, using CPU\n");
            FLAGGPU = 0; FLAGGPU_FULL = 0;
        }
        if (FLAGGPU == 1 && FLAGGPU_FULL == 0 && !mode_supports_gpu_hash) {
            if (wantUncompressed && gpu_available && mode_supports_gpu_full) {
                output_info("GPU HASH mode does not support uncompressed; upgrading to GPU FULL\n");
                FLAGGPU_FULL = 1;
            } else {
                output_warning("GPU HASH not supported for this mode/options, using CPU\n");
                FLAGGPU = 0; FLAGGPU_FULL = 0;
            }
        }
        if (FLAGGPU_FULL == 1) output_success("GPU mode: FULL (secp256k1 + SHA256 + RIPEMD160 + matching on GPU)\n");
        else if (FLAGGPU == 1) output_success("GPU mode: HASH (CPU generates points, GPU computes hash160)\n");

        if (FLAGGPU == 1 && getenv("KEYHUNT_GPU_SELFTEST")) {
            if (!gpu_selftest_hash160_fromX()) {
                output_warning("Disabling GPU due to failed self-test\n");
                FLAGGPU = 0; FLAGGPU_FULL = 0;
            } else { printf("[OK] GPU self-test passed\n"); }
        }
        if ((FLAGGPU == 1 || FLAGGPU_FULL == 1) && !FLAGTHREADS) {
            int gpu_threads = 0;
            if (FLAGGPU_HYBRID) {
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

    /* ========== Range setup ========== */
    if(FLAGRANGE) {
        n_range_start.SetBase16(range_start);
        n_range_end.SetBase16(range_end);
        if(n_range_start.IsZero()) n_range_start.AddOne();
        if(n_range_end.IsZero()) { output_error("End range can't be zero\nFallback to random mode!\n"); FLAGRANGE = 0; }
        if(FLAGRANGE) {
            if(n_range_start.IsGreater(&n_range_end)) {
                output_warning("Opps, start range can't be great than end range. Swapping them\n");
                n_range_aux.Set(&n_range_start); n_range_start.Set(&n_range_end); n_range_end.Set(&n_range_aux);
                if(n_range_start.IsZero()) n_range_start.AddOne();
            }
            if(n_range_start.IsLower(&secp->order) && n_range_end.IsLowerOrEqual(&secp->order)) {
                if (n_range_end.IsLower(&secp->order)) n_range_end.AddOne();
                else n_range_end.Set(&secp->order);
                n_range_diff.Set(&n_range_end); n_range_diff.Sub(&n_range_start);
            } else { output_error("Start and End range can't be great than N\nFallback to random mode!\n"); FLAGRANGE = 0; }
        }
    }
    if(FLAGMODE != MODE_BSGS && FLAGMODE != MODE_MINIKEYS) {
        if(DEBUGCOUNT == 0) DEBUGCOUNT = 1024;
        BSGS_N.SetInt32(DEBUGCOUNT);
        if(FLAGRANGE == 0 && FLAGBITRANGE == 0) {
            n_range_start.SetInt32(1); n_range_end.Set(&secp->order);
            n_range_diff.Set(&n_range_end); n_range_diff.Sub(&n_range_start);
        } else {
            if(FLAGBITRANGE) {
                n_range_start.SetBase16(bit_range_str_min); n_range_end.SetBase16(bit_range_str_max);
                n_range_diff.Set(&n_range_end); n_range_diff.Sub(&n_range_start);
            } else { if(FLAGRANGE == 0) output_warning("WTF!\n"); }
        }
    }

    /* ========== Config bridge ========== */
    config.search.mode = (search_mode_t)FLAGMODE;
    config.search.key_format = flagsearch_to_key_format(FLAGSEARCH);
    config.search.crypto_type = (crypto_type_t)FLAGCRYPTO;
    config.search.endomorphism = (FLAGENDOMORPHISM != 0);
    config.search.random_mode = (FLAGRANDOM != 0);
    config.search.quiet_mode = (FLAGQUIET != 0);
    config.search.debug_mode = (FLAGDEBUG != 0);
    config.search.matrix_mode = (FLAGMATRIX != 0);
    config.search.skip_checksum = (FLAGSKIPCHECKSUM != 0);
    if (fileName != NULL) snprintf(config.search.target_file, sizeof(config.search.target_file), "%s", fileName);
    config.bsgs.k_factor = KFACTOR;
    config.bsgs.bsgs_mode = (bsgs_mode_t)FLAGBSGSMODE;
    config.bsgs.save_progress = (FLAGSAVEREADFILE != 0);
    config.runtime.num_threads = NTHREADS;
    config.runtime.secp = (void *)secp;
    config.runtime.max_address_length = MAXLENGTHADDRESS;
    config.runtime.sequential_max = N_SEQUENTIAL_MAX;
    config.runtime.endo_lambda = (void *)&lambda;
    config.runtime.endo_lambda2 = (void *)&lambda2;
    config.runtime.endo_beta = (void *)&beta;
    config.runtime.endo_beta2 = (void *)&beta2;
    config.runtime.range_start = (void *)&n_range_start;
    config.runtime.range_end = (void *)&n_range_end;
    config.runtime.stride = (void *)&stride;
    config.runtime.generator_points = (void *)&Gn;
    config.runtime.generator_point_2 = (void *)&_2Gn;
    config.runtime.minikey_coinbuffer = (void *)Ccoinbuffer;
    config.runtime.minikey_raw_base = (void *)raw_baseminikey;
    config.runtime.minikey_n = (void *)minikeyN;
    config.runtime.minikey_n_limit = minikey_n_limit;
    kh_config_validate(&config);

    N = 0;

    if(FLAGMODE != MODE_BSGS) {
        if(!FLAG_N && OPTIMAL_N > 0) {
            N_SEQUENTIAL_MAX = OPTIMAL_N;
            output_info("Using auto-tuned N value: 0x%llx\n", (unsigned long long)OPTIMAL_N);
        } else if(FLAG_N) {
            int base = 10;
            const char *num = str_N;
            if (num[0] == '0' && (num[1] == 'x' || num[1] == 'X')) base = 16;
            errno = 0;
            char *endp = NULL;
            unsigned long long parsed = strtoull(num, &endp, base);
            if (errno != 0 || endp == num || (endp && *endp != '\0')) {
                output_error("Invalid -n value: %s\n", str_N); FLAG_N = 0; N_SEQUENTIAL_MAX = 0x100000000;
            } else { N_SEQUENTIAL_MAX = (uint64_t)parsed; }
            if(N_SEQUENTIAL_MAX < 1024) { output_info("n value need to be equal or great than 1024, back to defaults\n"); FLAG_N = 0; N_SEQUENTIAL_MAX = 0x100000000; }
            if(N_SEQUENTIAL_MAX % 1024 != 0) { output_info("n value need to be multiplier of  1024\n"); FLAG_N = 0; N_SEQUENTIAL_MAX = 0x100000000; }
        } else { N_SEQUENTIAL_MAX = 0x100000000; }
        output_success("N = 0x%llx\n",(unsigned long long)N_SEQUENTIAL_MAX);

        if(FLAGMODE == MODE_MINIKEYS) {
            BSGS_N.SetInt32(DEBUGCOUNT);
            if(FLAGBASEMINIKEY) output_success("Base Minikey : %s\n",str_baseminikey);
            minikeyN = (char*) malloc(22);
            checkpointer((void *)minikeyN,__FILE__,"malloc","minikeyN",__LINE__-1);
            i = 0; int58.SetInt32(58); int_aux.SetInt64(N_SEQUENTIAL_MAX); int_aux.Mult(253);
            i = 20; salir = 0;
            do {
                if(!int_aux.IsZero()) {
                    int_r.Set(&int_aux); int_r.Mod(&int58); int_q.Set(&int_aux);
                    minikeyN[i] = (uint8_t)int_r.GetInt64();
                    int_q.Sub(&int_r); int_q.Div(&int58); int_aux.Set(&int_q); i--;
                } else { salir = 1; }
            } while(!salir && i > 0);
            minikey_n_limit = 21 - i;
        } else {
            if(FLAGBITRANGE) output_success("Bit Range %i\n",bitrange);
            else output_success("Range \n");
        }
        if(FLAGMODE != MODE_MINIKEYS) {
            hextemp = n_range_start.GetBase16(); output_success("-- from : 0x%s\n",hextemp); free(hextemp);
            if (FLAGRANGE) { Int end_inclusive; end_inclusive.Set(&n_range_end); end_inclusive.SubOne(); hextemp = end_inclusive.GetBase16(); }
            else { hextemp = n_range_end.GetBase16(); }
            output_success("-- to   : 0x%s\n",hextemp); free(hextemp);
        }

        monitoring_initialize_range_progress(n_range_start, n_range_end, FLAGPROGRESSBAR, FLAGMODE);
        config.runtime.range_progress_start = (void *)monitoring_range_progress_start();
        config.runtime.range_progress_end = (void *)monitoring_range_progress_end();

        if (!FLAGGPU_HYBRID && !g_work_pool.enabled && !FLAGRANDOM &&
            (FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_XPOINT || FLAGMODE == MODE_VANITY)) {
            maybe_adjust_cpu_sequential_max((size_t)NTHREADS, n_range_start, n_range_end, "KEYHUNT_CPU_N", "CPU");
        }

        /* Wire I/O state into config BEFORE readFile calls */
        config.runtime.bloom_filter = (void *)&bloom;
        config.runtime.address_table = (void *)addressTable;
        config.runtime.address_count = (int64_t)N;
        config.runtime.io_read_cached = FLAGREADEDFILE1;
        config.runtime.max_address_length = MAXLENGTHADDRESS;
        config.runtime.vanity_targets = vanity_rmd_targets;
        config.runtime.vanity_total = vanity_rmd_total;
        config.runtime.vanity_bloom = (void *)vanity_bloom;
        config.runtime.vanity_limits = (void *)vanity_rmd_limits;
        config.runtime.vanity_values_a = (void *)vanity_rmd_limit_values_A;
        config.runtime.vanity_values_b = (void *)vanity_rmd_limit_values_B;
        config.runtime.vanity_min_check_len = vanity_rmd_minimun_bytes_check_length;
        config.runtime.vanity_addresses = (void *)vanity_address_targets;

        switch(FLAGMODE) {
            case MODE_MINIKEYS: case MODE_RMD160: case MODE_ADDRESS: case MODE_XPOINT:
                if(!readFileAddress(fileName, &config)) { output_error("Unexpected error\n"); exit(EXIT_FAILURE); }
                break;
            case MODE_VANITY:
                if(!readFileVanity(fileName, &config)) { output_error("Unexpected error\n"); exit(EXIT_FAILURE); }
                break;
        }

        N = (uint64_t)config.runtime.address_count;
        addressTable = (struct address_value *)config.runtime.address_table;
        FLAGREADEDFILE1 = config.runtime.io_read_cached;
        MAXLENGTHADDRESS = config.runtime.max_address_length;

        if(FLAGMODE != MODE_VANITY && !FLAGREADEDFILE1) {
            output_success("Sorting data ..."); kh_sort(addressTable,N);
            printf(" done! %" PRIu64 " values were loaded and sorted\n",N);
            config.runtime.address_table = (void *)addressTable;
            config.runtime.address_count = (int64_t)N;
            writeFileIfNeeded(fileName, &config);
            FLAGREADEDFILE1 = config.runtime.io_read_cached;
        }

        /* GPU Full Search initialization */
        if (FLAGGPU_FULL == 1) {
            output_success("Initializing GPU full search...\n");
            if (gpu_dispatch_upload_gtable() == 0) output_success("G table uploaded to GPU (8192 points)\n");
            else { error_report_t report; error_gpu_init_failed("GPU","Failed to upload precomputed G table",&report); error_print(&report); FLAGGPU_FULL = 0; FLAGGPU = 0; }
            if (FLAGGPU_FULL && gpu_dispatch_upload_targets((void *)addressTable, N) == 0) {
                output_success("Targets uploaded to GPU (%" PRIu64 " hashes)\n", N);
                if (N > 32) {
                    if (gpu_dispatch_upload_bloom((void *)addressTable, N) == 0) output_success("GPU bloom uploaded\n");
                    else output_warning("GPU bloom upload failed; continuing without GPU bloom\n");
                }
            } else if (FLAGGPU_FULL) {
                error_report_t report; char details[256];
                snprintf(details, sizeof(details), "Failed to upload %" PRIu64 " target hashes to GPU", N);
                error_gpu_init_failed("GPU", details, &report); error_print(&report);
                FLAGGPU_FULL = 0; FLAGGPU = 0;
            }
        }
    }

    /* ========== Progress tracking init ========== */
    if (progress_init() != 0) {
        output_warning("Failed to initialize progress system. Progress will NOT be saved.\n");
    } else {
        char *range_start_hex = n_range_start.GetBase16();
        char *range_end_hex = n_range_end.GetBase16();
        int create_result = progress_create(&g_progress_state, get_mode_name(FLAGMODE),
                            fileName, bitrange,
                            range_start_hex ? range_start_hex : "0",
                            range_end_hex ? range_end_hex : "0");
        if (create_result != 0) output_warning("Failed to create progress file.\n");
        else { g_progress_enabled = true; g_progress_state.is_random_mode = (FLAGRANDOM != 0); g_progress_state.thread_count = NTHREADS; output_info("Progress tracking enabled (auto-saves every 60s)\n"); }
        if (range_start_hex) free(range_start_hex);
        if (range_end_hex) free(range_end_hex);
    }

    /* ========== Mode dispatch ========== */
    if(FLAGMODE == MODE_BSGS) {
        config.runtime.bsgs_context = (void *)&bsgs_ctx;
        int bsgs_rc = mode_dispatch(&config, NULL, 0);
        if (bsgs_rc != 0) { output_error("BSGS mode initialization failed\n"); exit(EXIT_FAILURE); }
    }

    if(FLAGMODE != MODE_BSGS) {
        monitoring_initialize_rate_limits();
        if (!FLAGTHREADS && NTHREADS == 1 && OPTIMAL_THREADS > 0) {
            NTHREADS = OPTIMAL_THREADS;
            output_info("Using auto-tuned thread count: %d\n", NTHREADS);
        }
        steps = (struct thread_counter *) aligned_calloc(64, NTHREADS, sizeof(struct thread_counter));
        checkpointer((void *)steps,__FILE__,"aligned_calloc","steps",__LINE__-1);
        ends = (struct thread_flag *) aligned_calloc(64, NTHREADS, sizeof(struct thread_flag));
        checkpointer((void *)ends,__FILE__,"aligned_calloc","ends",__LINE__-1);
        tid = (platform_thread_t *) calloc(NTHREADS, sizeof(platform_thread_t));
        checkpointer((void *)tid,__FILE__,"calloc","tid",__LINE__-1);
#ifndef _WIN64
        shutdown_work_queue();
#endif

        /* GPU Full Search Mode */
        if (FLAGGPU_FULL && !FLAGGPU_HYBRID && (FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160)) {
            output_success("Running GPU full search mode...\n");
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
                    if (platform_thread_create(&gpu_stats_tid, gpu_full_stats_thread, &gpu_stats_args) == 0) gpu_stats_started = 1;
                }
            }
#endif
            int gpu_result = -1;
            if (config.gpu.multi_gpu_enabled && config.gpu.device_count > 1) {
                output_success("Running multi-GPU search with %d devices...\n", config.gpu.device_count);
                multi_gpu_config_t sched_config;
                sched_config.device_count = config.gpu.device_count;
                for (int gi = 0; gi < config.gpu.device_count; gi++) sched_config.device_ids[gi] = config.gpu.device_ids[gi];
                sched_config.adaptive_balancing = true;
                sched_config.rebalance_interval_keys = 100000000;
                multi_gpu_scheduler_t *scheduler = multi_gpu_init(&sched_config);
                if (!scheduler) { output_error("Failed to initialize multi-GPU scheduler\n"); gpu_result = -1; }
                else {
                    uint64_t rs = n_range_start.GetInt64(); uint64_t re = n_range_end.GetInt64();
                    multi_gpu_set_range(scheduler, rs, re);
                    worker_config_t worker_cfg = gpu_worker_default_config(scheduler, config.gpu.device_count);
                    for (int gi = 0; gi < config.gpu.device_count; gi++) worker_cfg.device_ids[gi] = config.gpu.device_ids[gi];
                    worker_cfg.batch_size = THREADBPWORKLOAD;
                    gpu_multi_worker_t *workers = gpu_worker_init(&worker_cfg);
                    if (!workers) { output_error("Failed to initialize multi-GPU workers\n"); multi_gpu_shutdown(scheduler); gpu_result = -1; }
                    else {
                        g_multi_gpu_workers = workers;
#ifndef _WIN64
                        struct sigaction sa; memset(&sa, 0, sizeof(sa)); sa.sa_handler = sigint_handler; sigemptyset(&sa.sa_mask); sa.sa_flags = 0; sigaction(SIGINT, &sa, NULL);
#endif
                        if (!gpu_worker_start(workers)) { output_error("Failed to start multi-GPU workers\n"); g_multi_gpu_workers = NULL; gpu_worker_shutdown(workers); multi_gpu_shutdown(scheduler); gpu_result = -1; }
                        else {
                            while (!gpu_worker_has_result(workers)) { sleep_ms(1000); if (g_gpu_should_stop.load(std::memory_order_acquire)) { check_sigint_cleanup(); break; } }
                            gpu_worker_stop(workers, 10000);
                            gpu_result = gpu_worker_has_result(workers) ? 0 : -1;
                            gpu_worker_shutdown(workers); g_multi_gpu_workers = NULL; multi_gpu_shutdown(scheduler);
                        }
                    }
                }
            } else {
                if (config.gpu.multi_gpu_enabled && config.gpu.device_count == 1) output_info("Multi-GPU enabled but only 1 device specified, using single GPU mode\n");
                gpu_result = gpu_dispatch_run_full_search(&config, &n_range_start, &n_range_end, &stride, N);
            }
#ifndef _WIN64
            gpu_stats_stop.store(1, std::memory_order_release);
            if (gpu_stats_started) platform_thread_join(gpu_stats_tid, NULL);
#endif
            if (gpu_result >= 0) {
                output_success("GPU search finished. Keys found: %d\n", gpu_result);
                output_success("Total keys checked: %" PRIu64 "\n", g_gpu_keys_checked.load(std::memory_order_acquire));
#ifndef _WIN64
                shutdown_work_queue();
#endif
                gpu_backend_shutdown();
                output_success("Done!\n");
                return 0;
            } else { output_warning("GPU search failed, falling back to CPU threads\n"); FLAGGPU_FULL = 0; }
        }

        /* GPU Hybrid Mode */
        if (FLAGGPU_HYBRID.load(std::memory_order_relaxed) && (FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160)) {
            if (!gpu_backend_available()) {
                output_warning("GPU not available for hybrid mode, falling back to CPU-only\n");
                FLAGGPU_HYBRID.store(0, std::memory_order_release);
            } else {
                {
                    const char *env = getenv("KEYHUNT_HYBRID_GPU_PERCENT");
                    if (env && *env) { int v = atoi(env); if (v >= 1 && v <= 99) g_gpu_range_percent = v; }
                    if (g_gpu_range_percent <= 0) g_gpu_range_percent = hybrid_get_gpu_range_percent_default(NTHREADS);
                    if (g_gpu_range_percent <= 0) g_gpu_range_percent = 80;
                    output_info("HYBRID: split GPU %d%% / CPU %d%%\n", g_gpu_range_percent, 100 - g_gpu_range_percent);
                }

                float initial_cpu_ratio = 1.0f - (g_gpu_range_percent / 100.0f);
                adaptive_init(initial_cpu_ratio, n_range_start.GetInt64(), n_range_end.GetInt64());

                const char *ws = getenv("KEYHUNT_HYBRID_WORK_STEAL");
                const bool want_work_steal = (ws && *ws && atoi(ws) != 0);
                const bool can_work_steal = want_work_steal && !FLAGRANDOM && stride.IsOne();
                if (want_work_steal && !can_work_steal) output_warning("HYBRID: work-stealing requires non-random mode and stride=1; using static split\n");

                if (can_work_steal) {
                    uint64_t block_size = 0x100000000ULL;
                    const char *bs = getenv("KEYHUNT_HYBRID_BLOCK_SIZE");
                    if (bs && *bs) { if (bs[0] == '0' && (bs[1] == 'x' || bs[1] == 'X')) block_size = strtoull(bs + 2, NULL, 16); else block_size = strtoull(bs, NULL, 10); }
                    if (block_size < 1024ULL) block_size = 1024ULL;
                    block_size = (block_size / 1024ULL) * 1024ULL;
                    output_success("Running GPU+CPU hybrid mode (work-stealing)...\n");
                    g_work_pool.init(&n_range_start, &n_range_end, block_size);
                    gpu_hybrid_args.start_key.Set(&n_range_start); gpu_hybrid_args.end_key.Set(&n_range_end);
                    gpu_hybrid_args.stride.Set(&stride); gpu_hybrid_args.target_count = N;
                    gpu_hybrid_args.result.store(0, std::memory_order_release);
                    gpu_hybrid_args.completed.store(0, std::memory_order_release);
                    g_gpu_keys_checked.store(0, std::memory_order_release);
                    g_gpu_keys_checked_cur.store(0, std::memory_order_release);
                    g_gpu_should_stop.store(0, std::memory_order_release);
                    int err = platform_thread_create(&gpu_thread_id, gpu_dispatch_hybrid_thread, &gpu_hybrid_args);
                    if (err != 0) { output_warning("Failed to start GPU thread\n"); g_work_pool.disable(); FLAGGPU_HYBRID.store(0, std::memory_order_release); }
                    else { gpu_hybrid_started = 1; output_success("GPU thread started, CPU uses normal fast algorithm\n"); }
                } else {
                    output_success("Running GPU+CPU hybrid mode (static split)...\n");
                    Int range_diff, gpu_portion, gpu_range_end, cpu_range_start;
                    range_diff.Set(&n_range_end); range_diff.Sub(&n_range_start);
                    gpu_portion.Set(&range_diff); gpu_portion.Mult(g_gpu_range_percent);
                    Int divisor; divisor.SetInt32(100); gpu_portion.Div(&divisor);
                    gpu_range_end.Set(&n_range_start); gpu_range_end.Add(&gpu_portion);
                    cpu_range_start.Set(&gpu_range_end);
                    output_success("GPU handles %d%% of range, CPU handles %d%%\n", g_gpu_range_percent, 100 - g_gpu_range_percent);

                    hextemp = n_range_start.GetBase16(); output_success("GPU range: 0x%s", hextemp); free(hextemp);
                    { Int gpu_end_inclusive; gpu_end_inclusive.Set(&gpu_range_end); if (gpu_end_inclusive.IsGreater(&n_range_start)) gpu_end_inclusive.SubOne(); hextemp = gpu_end_inclusive.GetBase16(); printf(" - 0x%s\n", hextemp); free(hextemp); }
                    hextemp = cpu_range_start.GetBase16(); output_success("CPU range: 0x%s", hextemp); free(hextemp);
                    { Int cpu_end_inclusive; cpu_end_inclusive.Set(&n_range_end); if (cpu_end_inclusive.IsGreater(&cpu_range_start)) cpu_end_inclusive.SubOne(); hextemp = cpu_end_inclusive.GetBase16(); printf(" - 0x%s\n", hextemp); free(hextemp); }

                    gpu_hybrid_args.start_key.Set(&n_range_start); gpu_hybrid_args.end_key.Set(&gpu_range_end);
                    gpu_hybrid_args.stride.Set(&stride); gpu_hybrid_args.target_count = N;
                    gpu_hybrid_args.result.store(0, std::memory_order_release);
                    gpu_hybrid_args.completed.store(0, std::memory_order_release);
                    g_gpu_keys_checked.store(0, std::memory_order_release);
                    g_gpu_keys_checked_cur.store(0, std::memory_order_release);
                    g_gpu_should_stop.store(0, std::memory_order_release);
                    int err = platform_thread_create(&gpu_thread_id, gpu_dispatch_hybrid_thread, &gpu_hybrid_args);
                    if (err != 0) { output_warning("Failed to start GPU thread\n"); FLAGGPU_HYBRID.store(0, std::memory_order_release); }
                    else {
                        gpu_hybrid_started = 1;
                        n_range_start.Set(&cpu_range_start);
                        maybe_adjust_cpu_sequential_max((size_t)NTHREADS, cpu_range_start, n_range_end, "KEYHUNT_HYBRID_CPU_N", "HYBRID");
                        output_success("GPU thread started, CPU uses normal fast algorithm\n");
                    }
                }
            }
        }

        /* CPU Thread Mode */
#ifndef _WIN64
        if (g_work_pool.enabled) { shutdown_work_queue(); }
        else if(FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_XPOINT || FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_VANITY) { configure_work_queue((size_t)NTHREADS); }
        else { shutdown_work_queue(); }
#endif
        config.runtime.num_threads = NTHREADS;
        config.runtime.bloom_filter = (void *)&bloom;
        config.runtime.address_table = (void *)addressTable;
        config.runtime.address_count = (int64_t)N;
        config.runtime.write_mutex = (void *)&write_keys;
        config.runtime.random_mutex = (void *)&write_random;
        config.runtime.thread_counters = (void *)steps;
        config.runtime.thread_flags = (void *)ends;
        config.runtime.thread_output = (void *)&THREADOUTPUT;
        config.runtime.sequential_max = N_SEQUENTIAL_MAX;
        config.gpu.enabled = FLAGGPU;
        config.gpu.full_mode = (FLAGGPU_FULL != 0);
        config.gpu.hybrid_mode = (FLAGGPU_HYBRID.load(std::memory_order_relaxed) != 0);
        config.autotune.has_avx2 = g_avx2_available;
        config.runtime.minikey_coinbuffer = (void *)Ccoinbuffer;
        config.runtime.minikey_raw_base = (void *)raw_baseminikey;
        config.runtime.minikey_n = (void *)minikeyN;
        config.runtime.minikey_n_limit = minikey_n_limit;
        profile_init_threads((int)NTHREADS);

        { int rc = mode_dispatch(&config, tid, (int)NTHREADS); if (rc != 0) { output_error("mode_dispatch failed for mode %d\n", FLAGMODE); exit(EXIT_FAILURE); } }
    }

    /* ========== Monitoring loop ========== */
    monitoring_params_t mon_params;
    memset(&mon_params, 0, sizeof(mon_params));
    mon_params.steps = steps;
    mon_params.ends = ends;
    mon_params.num_threads = NTHREADS;
    mon_params.flagmode = FLAGMODE;
    mon_params.flagmatrix = FLAGMATRIX;
    mon_params.flagquiet = FLAGQUIET;
    mon_params.flagvisual = FLAGVISUAL;
    mon_params.flaggpu_hybrid = FLAGGPU_HYBRID.load(std::memory_order_relaxed);
    mon_params.flaggpu_full = FLAGGPU_FULL;
    mon_params.flagrandom = FLAGRANDOM;
    mon_params.flagendomorphism = FLAGENDOMORPHISM;
    mon_params.gpu_hybrid_started = gpu_hybrid_started;
    mon_params.gpu_thread_id = gpu_thread_id;
    mon_params.gpu_hybrid_args = (void *)&gpu_hybrid_args;
    mon_params.output_seconds = &OUTPUTSECONDS;
    mon_params.thread_output = &THREADOUTPUT;
    mon_params.bsgs_mutex = &bsgs_thread;
    mon_params.config = &config;
    mon_params.progress_state = (void *)&g_progress_state;
    mon_params.progress_enabled = g_progress_enabled;
    mon_params.sysinfo = (void *)&g_sysinfo;
    mon_params.multi_gpu_workers = (void *)g_multi_gpu_workers;

    run_monitoring_loop(&mon_params);

    /* ========== Post-monitoring cleanup ========== */
    if (FLAGGPU_HYBRID && gpu_hybrid_started) {
        g_gpu_should_stop.store(1, std::memory_order_release);
        printf("\n[+] Waiting for GPU thread to complete...\n");
        platform_thread_join(gpu_thread_id, NULL);
        output_success("GPU thread finished. Result: %d keys found\n", gpu_hybrid_args.result.load(std::memory_order_acquire));
        output_success("GPU keys checked: %" PRIu64 "\n", monitoring_gpu_keys_checked_total_u64());
        if (g_adaptive_scheduler.initialized) {
            double cpu_mkeys = 0.0, gpu_mkeys = 0.0; int cpu_pct = 0, gpu_pct = 0;
            adaptive_get_stats(&cpu_mkeys, &gpu_mkeys, &cpu_pct, &gpu_pct);
            output_success("Final adaptive stats: CPU=%.1f Mkeys/s (%d%%), GPU=%.1f Mkeys/s (%d%%)\n", cpu_mkeys, cpu_pct, gpu_mkeys, gpu_pct);
            output_success("Optimal ratio for next run: CPU=%d%%, GPU=%d%%\n", cpu_pct, gpu_pct);
        }
        adaptive_cleanup();
        gpu_backend_shutdown();
        if (g_work_pool.enabled) g_work_pool.disable();
    }

    if (g_progress_enabled) { progress_complete(&g_progress_state); output_info("Progress tracking completed\n"); }
    printf("\nEnd\n");
#ifndef _WIN64
    shutdown_work_queue();
#endif
    platform_mutex_destroy(&write_keys);
    platform_mutex_destroy(&write_random);
    platform_mutex_destroy(&bsgs_thread);
}

void init_generator() {
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

/* Functions extracted to other modules:
 * - menu() -> src/cli.cpp (Phase 4, Plan 08)
 * - Monitoring loop -> src/monitoring/monitoring.cpp (Phase 4, Plan 08)
 * - gpu_selftest_hash160_fromX -> src/gpu/gpu_dispatch.cpp (Phase 4, Plan 08)
 * - hybrid_get_gpu_range_percent_default -> src/gpu/gpu_dispatch.cpp (Phase 4, Plan 08)
 * - format_keys_per_second, progress tracking -> src/monitoring/monitoring.cpp (Phase 4, Plan 08)
 * - GPU dispatch functions -> src/gpu/gpu_dispatch.cpp (Phase 4, Plan 05)
 * - writekey, writekeyeth, checkpointer -> io/io.cpp
 * - readFileAddress, readFileVanity -> io/io.cpp
 * - vanityrmdmatch, addvanity -> search/search_vanity.cpp
 * - sort functions -> sort/sort.cpp
 * - BSGS sort/search -> bsgs/bsgs_sort.cpp
 * - BSGS threads -> search/search_bsgs_threads.cpp
 * - address_util functions -> crypto/address_util.cpp
 * - bloom init -> crypto/bloom_init.cpp
 * - calcualteindex -> search/search_bsgs.cpp
 * - sleep_ms -> util/thread_util.cpp
 */
