#ifndef SEARCH_CONTEXT_H
#define SEARCH_CONTEXT_H

/*
 * search_context.h -- Shared type definitions for search modules.
 *
 * MIGRATION COMPLETE (Phase 3, CFG-08):
 * All extern variable declarations have been removed.
 * Search modules now receive state via keyhunt_config_t* (thread_args).
 * See search_common.h for thread_args struct and thread entry point declarations.
 * See config/config.h for keyhunt_config_t definition.
 *
 * This file retains only:
 * - Struct type definitions used across multiple translation units
 * - Mode/crypto/search constant macros (legacy compatibility)
 * - BSGS helper function declarations
 */

#include <stdint.h>
#include <stdbool.h>

#include "secp256k1/SECP256k1.h"
#include "secp256k1/Point.h"
#include "secp256k1/Int.h"
#include "bloom/bloom.h"
#include "bloom/bloom_wrapper.h"

#include "platform/platform.h"

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

#ifndef CHECKSUMSHA256_DEFINED
#define CHECKSUMSHA256_DEFINED
struct checksumsha256 {
	char data[32];
	char backup[32];
};
#endif

/* struct bsgs_xvalue defined in bsgs/bsgs_sort.h */
#include "bsgs/bsgs_sort.h"

struct address_value {
	uint8_t value[20];
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

/* ------------------------------------------------------------------ */
/*  BSGS helper function declarations                                 */
/*  Sort/search functions declared in bsgs/bsgs_sort.h (extern "C")   */
/* ------------------------------------------------------------------ */

/* BSGS helper functions accept bsgs_context_t* (defined in search_common.h).
 * search_context.h forward-declares the struct here for callers that only
 * include search_context.h.  Full definition is in search_common.h. */
struct bsgs_context_t;

int bsgs_secondcheck(bsgs_context_t *bctx, Int *start_range, uint64_t a, uint32_t k_index, Int *privatekey);
int bsgs_thirdcheck(bsgs_context_t *bctx, Int *start_range, uint64_t a, uint32_t k_index, Int *privatekey);
void calcualteindex(bsgs_context_t *bctx, int i, Int *key);

#endif /* SEARCH_CONTEXT_H */
