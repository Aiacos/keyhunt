#ifndef SEARCH_CONTEXT_H
#define SEARCH_CONTEXT_H

/*
 * search_context.h — Shared declarations for search modules.
 *
 * This header provides extern declarations for globals defined in
 * keyhunt.cpp that are actually used by search modules and io.cpp.
 *
 * Included by: search_address.cpp, search_vanity.cpp, search_minikeys.cpp,
 *              search_bsgs.cpp, search_bsgs_threads.cpp, io/io.cpp
 *
 * Globals that are only used within keyhunt.cpp itself are NOT declared
 * here.  See MIGRATION_GUIDE.md for the mapping to keyhunt_config_t.
 */

#include <stdint.h>
#include <stdbool.h>
#include <vector>

#include "secp256k1/SECP256k1.h"
#include "secp256k1/Point.h"
#include "secp256k1/Int.h"
#include "bloom/bloom.h"
#include "bloom/bloom_wrapper.h"

#if defined(_WIN64) && !defined(__CYGWIN__)
#include <windows.h>
#else
#include <pthread.h>
#endif

/* ------------------------------------------------------------------ */
/*  Mode and crypto constants                                         */
/* ------------------------------------------------------------------ */

#ifndef CRYPTO_NONE
#define CRYPTO_NONE 0
#define CRYPTO_BTC  1
#define CRYPTO_ETH  2
#define CRYPTO_ALL  3
#endif

#ifndef MODE_XPOINT
#define MODE_XPOINT   0
#define MODE_ADDRESS  1
#define MODE_BSGS     2
#define MODE_RMD160   3
#define MODE_PUB2RMD  4
#define MODE_MINIKEYS 5
#define MODE_VANITY   6
#endif

#ifndef SEARCH_UNCOMPRESS
#define SEARCH_UNCOMPRESS 0
#define SEARCH_COMPRESS   1
#define SEARCH_BOTH       2
#endif

/* Canonical definition. Also defined in keyhunt_legacy.cpp and bsgsd.cpp
 * (standalone monoliths that don't include this header). */
#ifndef CPU_GRP_SIZE
#define CPU_GRP_SIZE 1024
#endif

/* ------------------------------------------------------------------ */
/*  Struct definitions                                                */
/* ------------------------------------------------------------------ */

struct checksumsha256 {
	char data[32];
	char backup[32];
};

/* struct bsgs_xvalue defined in bsgs/bsgs_sort.h */
#include "bsgs/bsgs_sort.h"

struct address_value {
	uint8_t value[20];
};

struct tothread {
	int nt;       /* Number thread */
	char *rs;     /* range start */
	char *rpt;    /* rng per thread */
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

/* Cache-line padded counters to avoid false sharing */
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

/* ================================================================== */
/*  Extern globals — defined in keyhunt.cpp                           */
/*                                                                    */
/*  Only variables actually used by the 5 includer files are declared */
/*  here.  Dead externs were removed (Feb 2026 audit).                */
/*  See MIGRATION_GUIDE.md for keyhunt_config_t equivalents.          */
/* ================================================================== */

/* --- Core: secp256k1 instance and precomputed generator points --- */
extern Secp256K1 *secp;
extern std::vector<Point> Gn;
extern Point _2Gn;
extern std::vector<Point> GSn;   /* BSGS generator points */
extern Point _2GSn;

/* --- Synchronisation: thread handles and mutexes --- */
#if defined(_WIN64) && !defined(__CYGWIN__)
extern HANDLE *tid;
extern HANDLE write_keys;
extern HANDLE write_random;
extern HANDLE bsgs_thread;
extern HANDLE *bPload_mutex;
#else
extern pthread_t *tid;
extern pthread_mutex_t write_keys;
extern pthread_mutex_t write_random;
extern pthread_mutex_t bsgs_thread;
extern pthread_mutex_t *bPload_mutex;
#endif

/* --- Thread progress counters --- */
extern struct thread_counter *steps;
extern struct thread_flag *ends;
extern volatile int THREADOUTPUT;

/* --- Search flags (used by search modules and/or io.cpp) --- */
extern int FLAGMODE;
extern int FLAGCRYPTO;
extern int FLAGSEARCH;
extern int FLAGENDOMORPHISM;
extern int FLAGRANDOM;
extern int FLAGQUIET;
extern int FLAGMATRIX;
extern int FLAGDEBUG;
extern int FLAGBSGSMODE;
extern int FLAGBASEMINIKEY;
extern int FLAGSKIPCHECKSUM;        /* io.cpp */
extern int FLAGVANITY;              /* io.cpp */
extern int FLAGSAVEREADFILE;        /* io.cpp */
extern int FLAGREADEDFILE1;         /* io.cpp, search_bsgs_threads */
extern int FLAGREADEDFILE2;         /* search_bsgs_threads */
extern int FLAGREADEDFILE3;         /* search_bsgs_threads */
extern int FLAGREADEDFILE4;         /* search_bsgs_threads */
extern int KFACTOR;
extern int MAXLENGTHADDRESS;        /* io.cpp */

/* --- Range and stride --- */
extern Int stride;
extern Int n_range_start;
extern Int n_range_end;

/* --- Data sizes --- */
extern uint64_t N;                  /* Target count */
extern uint64_t N_SEQUENTIAL_MAX;   /* Max keys per thread iteration */

/* --- Endomorphism constants --- */
extern Int lambda, lambda2, beta, beta2;

/* --- Bloom filter (address/rmd160/xpoint modes) --- */
extern bloom_extended_t bloom;

/* --- Target data --- */
extern struct address_value *addressTable;

/* --- String tables --- */
extern const char *modes[7];
extern const char *version;

/* --- Vanity mode state --- */
extern int vanity_rmd_targets;
extern int vanity_rmd_total;
extern int *vanity_rmd_limits;
extern uint8_t ***vanity_rmd_limit_values_A;
extern uint8_t ***vanity_rmd_limit_values_B;
extern int vanity_rmd_minimun_bytes_check_length;
extern char **vanity_address_targets;
extern struct bloom *vanity_bloom;

/* --- Minikey mode state --- */
extern char *Ccoinbuffer;
extern char *raw_baseminikey;
extern char *minikeyN;
extern int minikey_n_limit;

/* ------------------------------------------------------------------ */
/*  BSGS variables (used by search_bsgs*.cpp)                        */
/* ------------------------------------------------------------------ */

extern uint64_t BSGS_BUFFERXPOINTLENGTH;

extern int *bsgs_found;
extern std::vector<Point> OriginalPointsBSGS;
extern bool *OriginalPointsBSGScompressed;

extern struct bsgs_xvalue *bPtable;

/* BSGS bloom filters and mutexes */
extern bloom_extended_t *bloom_bP;
extern bloom_extended_t *bloom_bPx2nd;
extern bloom_extended_t *bloom_bPx3rd;

#if defined(_WIN64) && !defined(__CYGWIN__)
extern std::vector<HANDLE> bloom_bP_mutex;
extern std::vector<HANDLE> bloom_bPx2nd_mutex;
extern std::vector<HANDLE> bloom_bPx3rd_mutex;
#else
extern pthread_mutex_t *bloom_bP_mutex;
extern pthread_mutex_t *bloom_bPx2nd_mutex;
extern pthread_mutex_t *bloom_bPx3rd_mutex;
#endif

extern uint64_t bsgs_m;
extern uint64_t bsgs_m2;
extern uint64_t bsgs_m3;
extern uint64_t bsgs_aux;
extern uint32_t bsgs_point_number;

/* BSGS Int parameters */
extern Int BSGS_CURRENT;
extern Int BSGS_R;
extern Int BSGS_AUX;
extern Int BSGS_N;
extern Int BSGS_N_double;
extern Int BSGS_M;
extern Int BSGS_M_double;
extern Int BSGS_M2;
extern Int BSGS_M2_double;
extern Int BSGS_M3;
extern Int BSGS_M3_double;

/* BSGS Point parameters */
extern Point BSGS_MP_double;
extern Point BSGS_MP2_double;
extern Point BSGS_MP3_double;

extern std::vector<Point> BSGS_AMP2;
extern std::vector<Point> BSGS_AMP3;

/* ------------------------------------------------------------------ */
/*  Utility function declarations                                     */
/* ------------------------------------------------------------------ */

/* Thread-safe acquisition of the next base key to search from.
 * Defined in keyhunt.cpp. Supports work pool, work queue, random,
 * and sequential modes. Returns false when no more work is available. */
extern bool acquire_base_key(Int &key);

/* ------------------------------------------------------------------ */
/*  Thread function declarations                                      */
/* ------------------------------------------------------------------ */

#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process(LPVOID vargp);
DWORD WINAPI thread_process_vanity(LPVOID vargp);
DWORD WINAPI thread_process_minikeys(LPVOID vargp);
DWORD WINAPI thread_process_bsgs(LPVOID vargp);
DWORD WINAPI thread_process_bsgs_backward(LPVOID vargp);
DWORD WINAPI thread_process_bsgs_both(LPVOID vargp);
DWORD WINAPI thread_process_bsgs_random(LPVOID vargp);
DWORD WINAPI thread_process_bsgs_dance(LPVOID vargp);
DWORD WINAPI thread_bPload(LPVOID vargp);
DWORD WINAPI thread_bPload_2blooms(LPVOID vargp);
#else
void *thread_process(void *vargp);
void *thread_process_vanity(void *vargp);
void *thread_process_minikeys(void *vargp);
void *thread_process_bsgs(void *vargp);
void *thread_process_bsgs_backward(void *vargp);
void *thread_process_bsgs_both(void *vargp);
void *thread_process_bsgs_random(void *vargp);
void *thread_process_bsgs_dance(void *vargp);
void *thread_bPload(void *vargp);
void *thread_bPload_2blooms(void *vargp);
#endif

/* ------------------------------------------------------------------ */
/*  BSGS helper function declarations                                 */
/*  Sort/search functions declared in bsgs/bsgs_sort.h (extern "C")   */
/* ------------------------------------------------------------------ */

int bsgs_secondcheck(Int *start_range, uint32_t a, uint32_t k_index, Int *privatekey);
int bsgs_thirdcheck(Int *start_range, uint32_t a, uint32_t k_index, Int *privatekey);
void calcualteindex(int i, Int *key);

#endif /* SEARCH_CONTEXT_H */
