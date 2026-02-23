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

/* Thread calling convention helper */
#if defined(_WIN64) && !defined(__CYGWIN__)
    #define PLATFORM_THREAD_CALL WINAPI
#else
    #define PLATFORM_THREAD_CALL
#endif

/* ============================================================================
 * Search Mode Constants
 * ============================================================================ */

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
struct thread_counter {
    uint64_t value;
    uint8_t padding[56];
};

struct thread_flag {
    unsigned int value;
    uint8_t padding[60];
};

/* Core search state */
extern std::atomic<uint64_t> FINISHED_ITEMS;
extern uint64_t OLDFINISHED_ITEMS;
extern uint64_t N;
extern uint64_t u64range;

/* Thread state arrays */
extern struct thread_counter *steps;
extern struct thread_flag *ends;

/* Configuration flags */
extern int FLAGMODE;
extern int FLAGSEARCH;
extern int FLAGCRYPTO;
extern int FLAGENDOMORPHISM;
extern int FLAGQUIET;
extern int FLAGDEBUG;
extern int FLAGRANDOM;
extern int FLAGSTRIDE;
extern int NTHREADS;
extern int KFACTOR;

/* Bloom filter */
extern bloom_extended_t bloom;

/* BSGS-specific globals */
extern Int BSGS_M;
extern Int BSGS_M_double;
extern Int BSGS_CURRENT;
extern Point BSGS_P;
extern Point BSGS_MP;
extern Point BSGS_MP2;
extern struct bloom *bloom_bP;
extern struct bloom *bloom_bP2;
extern struct bloom *bloom_bP3;

/* Byte encode for address generation */
extern uint8_t byte_encode_crypto;

/* Vanity mode specific */
extern int vanity_rmd_targets;
extern int vanity_rmd_total;
extern int *vanity_rmd_limits;
extern uint8_t ***vanity_rmd_limit_values_A;
extern uint8_t ***vanity_rmd_limit_values_B;
extern int vanity_rmd_minimun_bytes_check_length;
extern char **vanity_address_targets;
extern struct bloom *vanity_bloom;

/* Thread synchronization */
extern platform_mutex_t write_keys;
extern platform_mutex_t write_random;
extern platform_mutex_t bsgs_thread;

/* Output control */
extern Int OUTPUTSECONDS;

/* ============================================================================
 * Shared Function Declarations
 * ============================================================================ */

/* Address/hash generation */
char *pubkeytopubaddress(char *pkey, int length);
void pubkeytopubaddress_dst(char *pkey, int length, char *dst);
void rmd160toaddress_dst(char *rmd, char *dst);
void generate_binaddress_eth(Point &publickey, unsigned char *dst_address);
void KECCAK_256(uint8_t *source, size_t size, uint8_t *dst);

/* Minikey functions */
void set_minikey(char *buffer, char *rawbuffer, int length);
bool increment_minikey_index(char *buffer, char *rawbuffer, int index);
void increment_minikey_N(char *rawbuffer);

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
