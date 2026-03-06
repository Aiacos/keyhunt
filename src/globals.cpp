/*
 * globals.cpp - Shared global variable definitions
 *
 * All shared global variables that were previously defined in keyhunt.cpp
 * are now defined here. Other translation units access them via
 * #include "globals.h".
 *
 * NOTE: File-local (static) variables remain in keyhunt.cpp:
 *   - g_config, g_config_loaded, g_save_config_path
 *   - g_progress_state, g_progress_enabled
 *   - g_workQueue
 *   - g_multi_gpu_workers, g_sigint_received
 *
 * NOTE: BSGS-specific globals remain in mode_bsgs.cpp / bsgs_globals.h.
 *
 * Created during Phase 4 Plan 09: globals extraction.
 */

#include "globals.h"

/* ============================================================================
 * Variable definitions (moved from keyhunt.cpp)
 * ============================================================================ */

/* String constants */
const char *Ccoinbuffer_default = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
char *Ccoinbuffer = (char*) Ccoinbuffer_default;
char *str_baseminikey = NULL;
char *raw_baseminikey = NULL;
char *minikeyN = NULL;
int minikey_n_limit;

const char *version = "0.2.230519 Satoshi Quest";

/* CPU group size and optimal parameters */
uint32_t CPU_GRP_SIZE = 1024;
int OPTIMAL_THREADS = 0;
uint64_t OPTIMAL_N = 0;
int OPTIMAL_KFACTOR = 0;

/* Generator points */
std::vector<Point> Gn;
Point _2Gn;

/* Thread workload */
uint32_t THREADBPWORKLOAD = 1048576;
bool g_avx2_available = false;

/* Mode and crypto string tables */
const char *modes[7] = {"xpoint","address","bsgs","rmd160","pub2rmd","minikeys","vanity"};
const char *cryptos[3] = {"btc","eth","all"};
const char *publicsearch[3] = {"uncompress","compress","both"};
const char *default_fileName = "addresses.txt";

/* Atomic thread output flag */
std::atomic<int> THREADOUTPUT{0};
char *bit_range_str_min;
char *bit_range_str_max;

/* Mutex and thread handles */
platform_thread_t *tid = NULL;
platform_mutex_t write_keys;
platform_mutex_t write_random;
platform_mutex_t bsgs_thread;
platform_mutex_t *bPload_mutex = NULL;

/* Thread counters */
uint64_t FINISHED_THREADS_COUNTER = 0;
uint64_t FINISHED_THREADS_BP = 0;
uint64_t THREADCYCLES = 0;
uint64_t THREADCOUNTER = 0;
std::atomic<uint64_t> FINISHED_ITEMS{0};
uint64_t OLDFINISHED_ITEMS = 0;

/* Byte encode for address generation */
uint8_t byte_encode_crypto = 0x00;

/* Vanity mode state */
int vanity_rmd_targets = 0;
int vanity_rmd_total = 0;
int *vanity_rmd_limits = NULL;
uint8_t ***vanity_rmd_limit_values_A = NULL;
uint8_t ***vanity_rmd_limit_values_B = NULL;
int vanity_rmd_minimun_bytes_check_length = 999999;
char **vanity_address_targets = NULL;
struct bloom *vanity_bloom = NULL;
bloom_extended_t bloom;

/* Thread state arrays */
struct thread_counter *steps = NULL;
struct thread_flag *ends = NULL;
uint64_t N = 0;

/* Search flags */
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
int FLAGSEARCH = 2; /* SEARCH_BOTH */
int FLAGBITRANGE = 0;
int FLAGRANGE = 0;
int FLAGFILE = 0;
int FLAGMODE = 1; /* MODE_ADDRESS */
int FLAGCRYPTO = 0;
int FLAGRAWDATA = 0;
int FLAGRANDOM = 0;
int FLAG_N = 0;
int FLAGPRECALCUTED_P_FILE = 0;
int FLAGGPU = 0;
int FLAGGPU_FULL = 0;
std::atomic<int> FLAGGPU_HYBRID{0};
int DEBUGCOUNT = 0;

/* GPU state */
std::atomic<uint64_t> g_gpu_keys_checked{0};
std::atomic<uint64_t> g_gpu_keys_checked_cur{0};
std::atomic<int> g_gpu_should_stop{0};
int g_gpu_range_percent = 0;

/* Range and stride */
int bitrange = 0;
char *str_N = NULL;
char *range_start = NULL;
char *range_end = NULL;
char *str_stride = NULL;
Int stride;
Int OUTPUTSECONDS;

uint64_t N_SEQUENTIAL_MAX = 4096;

/* System and GPU info */
system_info_t g_sysinfo;
gpu_backend_info_t g_gpu_backend_info;

/* Address table */
struct address_value *addressTable;

/* Int constants and range variables */
Int ONE;
Int ZERO;
Int MPZAUX;
Int n_range_start;
Int n_range_end;
Int n_range_diff;
Int n_range_aux;

/* Endomorphism constants */
Int lambda;
Int lambda2;
Int beta;
Int beta2;

/* Secp256k1 instance */
Secp256K1 *secp;

/* Config pointer (set by main() in keyhunt.cpp) */
keyhunt_config_t *g_kh_config_ptr = nullptr;
