#ifndef SEARCH_CONTEXT_H
#define SEARCH_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>
#include <vector>

#include "../../secp256k1/SECP256k1.h"
#include "../../secp256k1/Point.h"
#include "../../secp256k1/Int.h"
#include "../../bloom/bloom.h"
#include "../../oldbloom/oldbloom.h"

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

struct bsgs_xvalue {
	uint8_t value[6];
	uint64_t index;
};

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
	union {
		uint8_t data8[32];
		uint32_t data32[8];
		uint64_t data64[4];
	} X;
};
#endif

/* ------------------------------------------------------------------ */
/*  Extern global variables — defined in keyhunt.cpp                  */
/* ------------------------------------------------------------------ */

/* Secp256K1 instance */
extern Secp256K1 *secp;

/* Generator points */
extern std::vector<Point> Gn;
extern Point _2Gn;
extern std::vector<Point> GSn;
extern Point _2GSn;

/* Thread handles and mutexes */
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

/* Thread counters */
extern uint64_t FINISHED_THREADS_COUNTER;
extern uint64_t FINISHED_THREADS_BP;
extern uint64_t THREADCYCLES;
extern uint64_t THREADCOUNTER;
extern uint64_t FINISHED_ITEMS;
extern uint64_t OLDFINISHED_ITEMS;

/* Byte encoding */
extern uint8_t byte_encode_crypto;

/* Vanity state */
extern int vanity_rmd_targets;
extern int vanity_rmd_total;
extern int *vanity_rmd_limits;
extern uint8_t ***vanity_rmd_limit_values_A;
extern uint8_t ***vanity_rmd_limit_values_B;
extern int vanity_rmd_minimun_bytes_check_length;
extern char **vanity_address_targets;
extern struct bloom *vanity_bloom;

/* General bloom */
extern struct bloom bloom;

/* Data arrays */
extern uint64_t *steps;
extern unsigned int *ends;
extern uint64_t N;

extern uint64_t N_SEQUENTIAL_MAX;
extern uint64_t DEBUGCOUNT;
extern uint64_t u64range;

extern Int OUTPUTSECONDS;

/* Flags */
extern int FLAGSKIPCHECKSUM;
extern int FLAGENDOMORPHISM;
extern int FLAGBLOOMMULTIPLIER;
extern int FLAGVANITY;
extern int FLAGBASEMINIKEY;
extern int FLAGBSGSMODE;
extern int FLAGDEBUG;
extern int FLAGQUIET;
extern int FLAGMATRIX;
extern int KFACTOR;
extern int MAXLENGTHADDRESS;
extern int NTHREADS;
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
extern int FLAGRAWDATA;
extern int FLAGRANDOM;
extern int FLAG_N;
extern int FLAGPRECALCUTED_P_FILE;

/* Range variables */
extern int bitrange;
extern char *str_N;
extern char *range_start;
extern char *range_end;
extern char *str_stride;
extern Int stride;

extern Int n_range_start;
extern Int n_range_end;
extern Int n_range_diff;
extern Int n_range_aux;

/* Endomorphism */
extern Int lambda, lambda2, beta, beta2;

/* Thread output */
extern int THREADOUTPUT;
extern char *bit_range_str_min;
extern char *bit_range_str_max;

/* String tables */
extern const char *bsgs_modes[5];
extern const char *modes[7];
extern const char *cryptos[3];
extern const char *publicsearch[3];
extern const char *default_fileName;

/* Minikey variables */
extern const char *Ccoinbuffer_default;
extern char *Ccoinbuffer;
extern char *str_baseminikey;
extern char *raw_baseminikey;
extern char *minikeyN;
extern int minikey_n_limit;

/* Version */
extern const char *version;

/* Thread BP workload */
extern uint32_t THREADBPWORKLOAD;

/* ------------------------------------------------------------------ */
/*  BSGS variables                                                    */
/* ------------------------------------------------------------------ */

extern uint64_t BSGS_XVALUE_RAM;
extern uint64_t BSGS_BUFFERXPOINTLENGTH;
extern uint64_t BSGS_BUFFERREGISTERLENGTH;

extern int *bsgs_found;
extern std::vector<Point> OriginalPointsBSGS;
extern bool *OriginalPointsBSGScompressed;

extern uint64_t bytes;
extern char checksum[32];
extern char checksum_backup[32];
extern char buffer_bloom_file[1024];
extern struct bsgs_xvalue *bPtable;
extern struct address_value *addressTable;

extern struct oldbloom oldbloom_bP;

extern struct bloom *bloom_bP;
extern struct bloom *bloom_bPx2nd;
extern struct bloom *bloom_bPx3rd;

extern struct checksumsha256 *bloom_bP_checksums;
extern struct checksumsha256 *bloom_bPx2nd_checksums;
extern struct checksumsha256 *bloom_bPx3rd_checksums;

/* Bloom filter mutexes */
#if defined(_WIN64) && !defined(__CYGWIN__)
extern std::vector<HANDLE> bloom_bP_mutex;
extern std::vector<HANDLE> bloom_bPx2nd_mutex;
extern std::vector<HANDLE> bloom_bPx3rd_mutex;
#else
extern pthread_mutex_t *bloom_bP_mutex;
extern pthread_mutex_t *bloom_bPx2nd_mutex;
extern pthread_mutex_t *bloom_bPx3rd_mutex;
#endif

extern uint64_t bloom_bP_totalbytes;
extern uint64_t bloom_bP2_totalbytes;
extern uint64_t bloom_bP3_totalbytes;
extern uint64_t bsgs_m;
extern uint64_t bsgs_m2;
extern uint64_t bsgs_m3;
extern uint64_t bsgs_aux;
extern uint32_t bsgs_point_number;

/* Limits */
extern const char *str_limits_prefixs[7];
extern const char *str_limits[7];
extern Int int_limits[7];

/* BSGS Int variables */
extern Int BSGS_GROUP_SIZE;
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

extern Int ONE;
extern Int ZERO;
extern Int MPZAUX;

/* BSGS Point variables */
extern Point BSGS_P;
extern Point BSGS_MP;
extern Point BSGS_MP2;
extern Point BSGS_MP3;
extern Point BSGS_MP_double;
extern Point BSGS_MP2_double;
extern Point BSGS_MP3_double;

extern std::vector<Point> BSGS_AMP2;
extern std::vector<Point> BSGS_AMP3;

/* Temp points */
extern Point point_temp;
extern Point point_temp2;

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
/* ------------------------------------------------------------------ */

void bsgs_sort(struct bsgs_xvalue *arr, int64_t n);
void bsgs_myheapsort(struct bsgs_xvalue *arr, int64_t n);
void bsgs_insertionsort(struct bsgs_xvalue *arr, int64_t n);
void bsgs_introsort(struct bsgs_xvalue *arr, uint32_t depthLimit, int64_t n);
void bsgs_swap(struct bsgs_xvalue *a, struct bsgs_xvalue *b);
void bsgs_heapify(struct bsgs_xvalue *arr, int64_t n, int64_t i);
int64_t bsgs_partition(struct bsgs_xvalue *arr, int64_t n);
int bsgs_searchbinary(struct bsgs_xvalue *arr, char *data, int64_t array_length, uint64_t *r_value);
int bsgs_secondcheck(Int *start_range, uint32_t a, uint32_t k_index, Int *privatekey);
int bsgs_thirdcheck(Int *start_range, uint32_t a, uint32_t k_index, Int *privatekey);
void calcualteindex(int i, Int *key);

#endif /* SEARCH_CONTEXT_H */
