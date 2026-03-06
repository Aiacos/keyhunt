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

/* globals.h provides: struct definitions (address_value, bPload, publickey,
 * thread_counter, thread_flag), all shared extern declarations, and
 * transitively includes config/config.h -> cli.h which provides
 * search_mode_t enum (MODE_ADDRESS, MODE_BSGS, etc.). */
#include "globals.h"

/* ------------------------------------------------------------------ */
/*  Mode and crypto constants (legacy macro compatibility)             */
/*  The enum values in cli.h (via globals.h) are the canonical source. */
/*  These macros are retained for translation units that compare       */
/*  integer flag values directly (e.g., FLAGMODE == MODE_ADDRESS).     */
/* ------------------------------------------------------------------ */

#ifndef CRYPTO_NONE
#define CRYPTO_NONE 0
#define CRYPTO_BTC  1
#define CRYPTO_ETH  2
#define CRYPTO_ALL  3
#endif

/* MODE_* values come from search_mode_t enum in cli.h (included via globals.h).
 * Do NOT redefine them as macros -- that would conflict with the enum. */

#ifndef SEARCH_UNCOMPRESS
#define SEARCH_UNCOMPRESS 0
#define SEARCH_COMPRESS   1
#define SEARCH_BOTH       2
#endif

/* CPU_GRP_SIZE is now a variable in globals.h (uint32_t CPU_GRP_SIZE = 1024).
 * keyhunt_legacy.cpp and bsgsd.cpp define their own local versions. */

/* ------------------------------------------------------------------ */
/*  Struct definitions (now in globals.h)                              */
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
