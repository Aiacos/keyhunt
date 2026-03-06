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

/* Shared globals (secp, flags, counters, struct definitions, etc.)
 * Also provides MODE_* values via search_mode_t enum (cli.h -> config.h -> globals.h) */
#include "../globals.h"

/* ============================================================================
 * Search Mode Constants (legacy macro compatibility)
 * MODE_* values come from search_mode_t enum in cli.h (included via globals.h).
 * Do NOT redefine MODE_* as macros -- that conflicts with the enum.
 * ============================================================================ */

#define SEARCH_UNCOMPRESS 0
#define SEARCH_COMPRESS 1
#define SEARCH_BOTH 2

#define CRYPTO_NONE 0
#define CRYPTO_BTC 1
#define CRYPTO_ETH 2
#define CRYPTO_ALL 3

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
 * BSGS Context - Algorithm state for Baby Step Giant Step mode
 *
 * This struct encapsulates all BSGS-specific algorithm state that threads
 * need access to. It is NOT user configuration -- it is mutable algorithm
 * state initialized once before threads start.
 *
 * Stored in config->runtime.bsgs_context and cast to bsgs_context_t*.
 *
 * DESIGN NOTE: The struct stores POINTERS to Int/Point values (not copies)
 * because:
 *   1. Int/Point are large objects (256 bits each)
 *   2. The globals already exist in keyhunt.cpp -- we point to them, not copy
 *   3. Multiple threads share the same values (read-only after init)
 *   4. BSGS_CURRENT is written by threads (needs mutex, already existing)
 * ============================================================================ */

struct bsgs_context_t {
    /* Int parameters (BSGS key range and step values) */
    Int *BSGS_CURRENT;
    Int *BSGS_R;
    Int *BSGS_AUX;
    Int *BSGS_N;
    Int *BSGS_N_double;
    Int *BSGS_M;
    Int *BSGS_M_double;
    Int *BSGS_M2;
    Int *BSGS_M2_double;
    Int *BSGS_M3;
    Int *BSGS_M3_double;

    /* Point parameters (precomputed step points) */
    Point *BSGS_MP_double;
    Point *BSGS_MP2_double;
    Point *BSGS_MP3_double;

    /* Amplification point vectors */
    std::vector<Point> *BSGS_AMP2;
    std::vector<Point> *BSGS_AMP3;

    /* Target public keys */
    std::vector<Point> *OriginalPointsBSGS;
    bool *OriginalPointsBSGScompressed;

    /* Generator points for BSGS */
    std::vector<Point> *GSn;
    Point *_2GSn;

    /* Found state (per-target atomic flags) */
    std::atomic<int> *bsgs_found;

    /* Bloom filters (3-tier) */
    bloom_extended_t *bloom_bP;
    bloom_extended_t *bloom_bPx2nd;
    bloom_extended_t *bloom_bPx3rd;

    /* Baby step point table */
    struct bsgs_xvalue *bPtable;

    /* Bloom filter mutexes */
    platform_mutex_t *bloom_bP_mutex;
    platform_mutex_t *bloom_bPx2nd_mutex;
    platform_mutex_t *bloom_bPx3rd_mutex;
    platform_mutex_t *bPload_mutex;

    /* Scalar parameters */
    uint64_t bsgs_m;
    uint64_t bsgs_m2;
    uint64_t bsgs_m3;
    uint64_t bsgs_aux;
    uint32_t bsgs_point_number;
    uint64_t BSGS_BUFFERXPOINTLENGTH;

    /* Cache loading state */
    int FLAGREADEDFILE1;
    int FLAGREADEDFILE2;
    int FLAGREADEDFILE3;
    int FLAGREADEDFILE4;
};

/* BSGS helper functions (receive bsgs_context_t* parameter) */
int bsgs_secondcheck(bsgs_context_t *bctx, Int *start_range, uint64_t a, uint32_t k_index, Int *privatekey);
int bsgs_thirdcheck(bsgs_context_t *bctx, Int *start_range, uint64_t a, uint32_t k_index, Int *privatekey);
void calcualteindex(bsgs_context_t *bctx, int i, Int *key);

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

/* Byte encode for address generation (declared in globals.h) */

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

/* Thread synchronization and output control (declared in globals.h) */

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
void writekey(const keyhunt_config_t *config, bool found, Int *key);
void checksumalidate(char *address, char *checksum, char *checksum_calculated);

/* BSGS check function */
int bsgs_point_check(Point &p, Int &key);

/* Thread-safe random (defined in util/thread_util.cpp) */
#include "../util/thread_util.h"

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
