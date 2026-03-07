/*
 * globals.h - Shared global variable declarations
 *
 * This header declares all shared global variables that were previously
 * defined in keyhunt.cpp. Variable definitions live in globals.cpp.
 *
 * Any translation unit needing access to shared globals should
 * #include "globals.h" instead of using inline extern declarations.
 *
 * Created during Phase 4 Plan 09: globals extraction.
 */

#ifndef KEYHUNT_GLOBALS_H
#define KEYHUNT_GLOBALS_H

#include <stdint.h>
#include <stdbool.h>
#include <atomic>
#include <vector>

#include "secp256k1/SECP256k1.h"
#include "secp256k1/Point.h"
#include "secp256k1/Int.h"
#include "bloom/bloom.h"
#include "bloom/bloom_wrapper.h"
#include "platform/platform.h"
#include "config/config.h"
#include "core/sysinfo.h"
#include "gpu/gpu_backend.h"

/* ============================================================================
 * Struct definitions (previously in keyhunt.cpp)
 * ============================================================================ */

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

/* Thread-padded counters to avoid false sharing */
#ifndef THREAD_COUNTER_DEFINED
#define THREAD_COUNTER_DEFINED
struct thread_counter {
    uint64_t value;
    uint8_t padding[56];
};

struct thread_flag {
    unsigned int value;
    uint8_t padding[60];
};
#endif

/* ============================================================================
 * Shared global variable declarations (defined in globals.cpp)
 * ============================================================================ */

/* String constants */
extern const char *Ccoinbuffer_default;
extern char *Ccoinbuffer;
extern char *str_baseminikey;
extern char *raw_baseminikey;
extern char *minikeyN;
extern int minikey_n_limit;

extern const char *version;

/* CPU group size and optimal parameters */
extern uint32_t CPU_GRP_SIZE;
extern int OPTIMAL_THREADS;
extern uint64_t OPTIMAL_N;
extern int OPTIMAL_KFACTOR;

/* Generator points */
extern std::vector<Point> Gn;
extern Point _2Gn;

/* Thread workload */
extern uint32_t THREADBPWORKLOAD;
extern bool g_avx2_available;

/* Mode and crypto string tables */
extern const char *modes[7];
extern const char *cryptos[3];
extern const char *publicsearch[3];
extern const char *default_fileName;

/* Atomic thread output flag */
extern std::atomic<int> THREADOUTPUT;
extern char *bit_range_str_min;
extern char *bit_range_str_max;

/* Mutex and thread handles */
extern platform_thread_t *tid;
extern platform_mutex_t write_keys;
extern platform_mutex_t write_random;
extern platform_mutex_t bsgs_thread;
extern platform_mutex_t *bPload_mutex;

/* Thread counters */
extern uint64_t FINISHED_THREADS_COUNTER;
extern uint64_t FINISHED_THREADS_BP;
extern uint64_t THREADCYCLES;
extern uint64_t THREADCOUNTER;
extern std::atomic<uint64_t> FINISHED_ITEMS;
extern uint64_t OLDFINISHED_ITEMS;

/* Byte encode for address generation */
extern uint8_t byte_encode_crypto;

/* Vanity mode state */
extern int vanity_rmd_targets;
extern int vanity_rmd_total;
extern int *vanity_rmd_limits;
extern uint8_t ***vanity_rmd_limit_values_A;
extern uint8_t ***vanity_rmd_limit_values_B;
extern int vanity_rmd_minimun_bytes_check_length;
extern char **vanity_address_targets;
extern struct bloom *vanity_bloom;
extern bloom_extended_t bloom;

/* Thread state arrays */
extern struct thread_counter *steps;
extern struct thread_flag *ends;
extern uint64_t N;

/* Search flags */
extern int FLAGSKIPCHECKSUM;
extern int FLAGENDOMORPHISM;
extern int FLAGBLOOMMULTIPLIER;
extern int FLAGVANITY;
extern int FLAGBASEMINIKEY;
extern int FLAGBSGSMODE;
extern int FLAGDEBUG;
extern int FLAGQUIET;
extern int FLAGMATRIX;
extern int FLAGPROGRESSBAR;
extern int FLAGVISUAL;
extern int KFACTOR;
extern int MAXLENGTHADDRESS;
extern int NTHREADS;
extern int FLAGTHREADS;
extern int FLAGSAVEREADFILE;
extern int FLAGREADEDFILE1;
extern int FLAGREADEDFILE2;
extern int FLAGREADEDFILE3;
extern int FLAGREADEDFILE4;
extern int FLAGUPDATEFILE1;
extern int FLAGSTRIDE;
extern int FLAGSEARCH;
extern int FLAGBITRANGE;
extern int FLAGRANGE;
extern int FLAGFILE;
extern int FLAGMODE;
extern int FLAGCRYPTO;
extern int FLAGRANDOM;
extern int FLAG_N;
extern int FLAGGPU;
extern int FLAGGPU_FULL;
extern std::atomic<int> FLAGGPU_HYBRID;
extern int DEBUGCOUNT;

/* GPU state */
extern std::atomic<uint64_t> g_gpu_keys_checked;
extern std::atomic<uint64_t> g_gpu_keys_checked_cur;
extern std::atomic<int> g_gpu_should_stop;
extern int g_gpu_range_percent;

/* Range and stride */
extern int bitrange;
extern char *str_N;
extern char *range_start;
extern char *range_end;
extern char *str_stride;
extern Int stride;
extern Int OUTPUTSECONDS;

extern uint64_t N_SEQUENTIAL_MAX;

/* System and GPU info */
extern system_info_t g_sysinfo;
extern gpu_backend_info_t g_gpu_backend_info;

/* Address table */
extern struct address_value *addressTable;

/* Int constants and range variables */
extern Int ONE;
extern Int ZERO;
extern Int MPZAUX;
extern Int n_range_start;
extern Int n_range_end;
extern Int n_range_diff;
extern Int n_range_aux;

/* Endomorphism constants */
extern Int lambda;
extern Int lambda2;
extern Int beta;
extern Int beta2;

/* Secp256k1 instance */
extern Secp256K1 *secp;

/* Config pointer (set by main() in keyhunt.cpp) */
extern keyhunt_config_t *g_kh_config_ptr;

/* INI config state (used by CLI parsing and main()) */
#include "core/config.h"
extern keyhunt_ini_config_t g_ini_config;
extern bool g_ini_config_loaded;
extern const char *g_save_config_path;

/* Parsed fileName pointer (set by CLI parsing, used throughout) */
extern char *g_fileName;

/* ============================================================================
 * Forward declarations for cross-TU functions
 * ============================================================================ */

/* Signal cleanup (defined in keyhunt.cpp) */
extern void check_sigint_cleanup(void);

/* Work queue shutdown (defined in keyhunt.cpp) */
#ifndef _WIN64
extern void shutdown_work_queue();
#endif

/* Base key acquisition (defined in keyhunt.cpp) */
extern bool acquire_base_key(Int &key);

/* Sleep utility (defined in util/thread_util.cpp) */
extern void sleep_ms(int milliseconds);

/* Generator initialization (defined in globals.cpp) */
extern void init_generator();

#endif /* KEYHUNT_GLOBALS_H */
