/*
 * search_common.h - Common declarations for search mode modules
 *
 * This header provides shared declarations and extern references
 * for all search mode implementations.
 */

#ifndef SEARCH_COMMON_H
#define SEARCH_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <atomic>

#include "../platform/platform.h"
#include "../secp256k1/SECP256k1.h"
#include "../secp256k1/Point.h"
#include "../secp256k1/Int.h"
#include "../secp256k1/IntGroup.h"
#include "../bloom/bloom_wrapper.h"
#include "../config/config.h"

/* Thread calling convention helper */
#if defined(_WIN64) && !defined(__CYGWIN__)
    #define PLATFORM_THREAD_CALL WINAPI
#else
    #define PLATFORM_THREAD_CALL
#endif

/* ============================================================================
 * Search Mode Constants
 * ============================================================================ */

#ifndef MODE_XPOINT
#define MODE_XPOINT 0
#define MODE_ADDRESS 1
#define MODE_BSGS 2
#define MODE_RMD160 3
#define MODE_PUB2RMD 4
#define MODE_MINIKEYS 5
#define MODE_VANITY 6
#endif

#define SEARCH_UNCOMPRESS 0
#define SEARCH_COMPRESS 1
#define SEARCH_BOTH 2

#define CRYPTO_NONE 0
#define CRYPTO_BTC 1
#define CRYPTO_ETH 2
#define CRYPTO_ALL 3

/* ============================================================================
 * Shared Global Variables (extern declarations)
 * These are defined in keyhunt.cpp
 * ============================================================================ */

/* SECP256K1 curve instance */
extern Secp256K1 *secp;

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
 * Thread Arguments - New configuration-based threading
 * ============================================================================ */

/**
 * thread_args - Unified thread argument structure
 *
 * This struct provides a clean way to pass configuration to thread functions.
 * It replaces the legacy tothread struct during the migration process.
 *
 * Usage:
 *   thread_args args = { .config = &global_config, .thread_id = i };
 *   pthread_create(&tid, NULL, thread_func, &args);
 *
 * The config pointer gives threads access to all search parameters,
 * BSGS settings, GPU config, and runtime state without using globals.
 */
struct thread_args {
    keyhunt_config_t *config;   /* Pointer to configuration structure */
    int thread_id;              /* Thread number (0-based) */
};

/* ============================================================================
 * Shared Global Variables (extern declarations)
 * These are defined in keyhunt.cpp
 * NOTE: These will be gradually removed as migration to config progresses
 * ============================================================================ */

/* NOTE: Core search state, thread state arrays, configuration flags, and
 * bloom filter have been migrated to keyhunt_config_t and removed in subtask-4-1.
 * See MIGRATION_GUIDE.md and REMOVED_GLOBALS_SUMMARY.txt for details.
 *
 * Removed extern declarations (variables now in config or using local externs):
 *   - FINISHED_ITEMS, OLDFINISHED_ITEMS (runtime_state_t)
 *   - N, u64range (search_config_t)
 *   - steps, ends (runtime_state_t)
 *   - FLAGMODE, FLAGSEARCH, FLAGCRYPTO, FLAGENDOMORPHISM (search_config_t)
 *   - FLAGQUIET, FLAGDEBUG, FLAGRANDOM, FLAGSTRIDE (search_config_t)
 *   - NTHREADS, KFACTOR (runtime_state_t, bsgs_config_t)
 *   - bloom (runtime_state_t)
 *
 * Functions needing removed variables should use local 'extern' declarations.
 */

/* BSGS-specific globals */
extern Int BSGS_M;
extern Int BSGS_M_double;
extern Int BSGS_M2_double;
extern Int BSGS_M3;
extern Int BSGS_M3_double;
extern Int BSGS_CURRENT;
extern Point BSGS_P;
extern Point BSGS_MP;
extern Point BSGS_MP2;
extern bloom_extended_t *bloom_bP;
extern bloom_extended_t *bloom_bPx2nd;
extern bloom_extended_t *bloom_bPx3rd;

/* BSGS algorithm state (defined in keyhunt.cpp) */
#include <vector>
extern std::vector<Point> BSGS_AMP2;          /* Amplification points for 2nd check */
extern std::vector<Point> BSGS_AMP3;          /* Amplification points for 3rd check */
extern std::vector<Point> OriginalPointsBSGS; /* Target public keys */

/* BSGS data structures (canonical definition in bsgs/bsgs_sort.h) */
#ifndef BSGS_SORT_H
struct bsgs_xvalue {
    uint64_t value;    /* 8 bytes (last 8 bytes of X coordinate) */
    uint64_t index;    /* Index in bPtable */
};
#endif

extern struct bsgs_xvalue *bPtable;           /* Baby step point table */
extern uint64_t bsgs_m3;                      /* M3 value for table size */
extern uint64_t BSGS_BUFFERXPOINTLENGTH;      /* X-point buffer length (16) */

/* BSGS extended bloom filters (declared above with bloom_bP) */

/* Byte encode for address generation */
extern uint8_t byte_encode_crypto;

/* NOTE: Vanity mode variables have been migrated to keyhunt_config_t and
 * removed in subtask-4-1 (see REMOVED_GLOBALS_SUMMARY.txt section 3).
 * Functions needing these variables should use local 'extern' declarations.
 *
 * Removed extern declarations (now in runtime_state_t):
 *   - vanity_rmd_targets, vanity_rmd_total, vanity_rmd_limits
 *   - vanity_rmd_limit_values_A, vanity_rmd_limit_values_B
 *   - vanity_rmd_minimun_bytes_check_length
 *   - vanity_address_targets, vanity_bloom
 */

/* Thread synchronization */
extern platform_mutex_t write_keys;
extern platform_mutex_t write_random;
extern platform_mutex_t bsgs_thread;

/* Output control */
extern Int OUTPUTSECONDS;

/* ============================================================================
 * Shared Function Declarations
 * ============================================================================ */

/* Address/hash generation (C-linkage, defined in crypto/address_util.cpp) */
extern "C" {
char *pubkeytopubaddress(char *pkey, int length);
void pubkeytopubaddress_dst(char *pkey, int length, char *dst);
void rmd160toaddress_dst(char *rmd, char *dst);
void KECCAK_256(uint8_t *source, size_t size, uint8_t *dst);
}
/* C++ linkage */
void generate_binaddress_eth(Point &publickey, unsigned char *dst_address);

/* Minikey functions (coinbuffer/minikeyN params replace extern globals) */
void set_minikey(char *buffer, char *rawbuffer, int length, char *coinbuffer);
bool increment_minikey_index(char *buffer, char *rawbuffer, int index, char *coinbuffer);
void increment_minikey_N(char *rawbuffer, char *minikeyN_buf, int n_limit);

/* Generator initialization */
void init_generator(void);

/* Output utilities */
void writekey(bool found, Int *key);
void checksumalidate(char *address, char *checksum, char *checksum_calculated);

/* BSGS check function */
int bsgs_point_check(Point &p, Int &key);

/* Thread-safe random */
int thread_rand(void);
int thread_rand_n(int n);

/* ============================================================================
 * Thread Entry Points (search mode implementations)
 * ============================================================================ */

platform_thread_return_t PLATFORM_THREAD_CALL thread_process(void *vargp);
platform_thread_return_t PLATFORM_THREAD_CALL thread_process_bsgs(void *vargp);
platform_thread_return_t PLATFORM_THREAD_CALL thread_process_bsgs_backward(void *vargp);
platform_thread_return_t PLATFORM_THREAD_CALL thread_process_bsgs_both(void *vargp);
platform_thread_return_t PLATFORM_THREAD_CALL thread_process_bsgs_random(void *vargp);
platform_thread_return_t PLATFORM_THREAD_CALL thread_process_bsgs_dance(void *vargp);
platform_thread_return_t PLATFORM_THREAD_CALL thread_process_vanity(void *vargp);
platform_thread_return_t PLATFORM_THREAD_CALL thread_process_minikeys(void *vargp);

#endif /* SEARCH_COMMON_H */
