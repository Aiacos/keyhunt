/*
 * search_address.cpp - ADDRESS and RMD160 mode search implementation
 *
 * MIGRATION STATUS: Config-aware (in progress)
 *
 * The thread_process() function that handles ADDRESS, RMD160, and XPOINT
 * search modes has been migrated to use local config-aware variables instead
 * of direct global variable access. This is part of the incremental migration
 * to the new configuration system (src/config/config.h).
 *
 * Current implementation:
 * - thread_process() resides in keyhunt.cpp (lines 5261-5900)
 * - Uses local variables (local_mode, local_search, local_crypto, etc.)
 *   that are initialized from globals
 * - Future: Will accept thread_args struct with config pointer
 *
 * Migration completed in this phase:
 * ✅ Replaced FLAGMODE with local_mode
 * ✅ Replaced FLAGSEARCH with local_search
 * ✅ Replaced FLAGCRYPTO with local_crypto
 * ✅ Replaced FLAGENDOMORPHISM with local_endomorphism
 * ✅ Replaced FLAGRANDOM with local_random
 * ✅ Replaced FLAGQUIET with local_quiet
 * ✅ Replaced FLAGMATRIX with local_matrix
 *
 * Still using globals (to be migrated in future subtasks):
 * - Thread management (NTHREADS, tid, etc.)
 * - BSGS state (BSGS_M, bloom filters, etc.)
 * - Shared counters (FINISHED_ITEMS, steps, ends, etc.)
 * - Synchronization primitives (write_keys, write_random, etc.)
 *
 * Dependencies:
 * - secp256k1 library for elliptic curve operations
 * - bloom filter for fast target lookups
 * - SIMD hash functions (SHA256, RIPEMD160)
 *
 * Search modes supported:
 * - MODE_ADDRESS: Search for Bitcoin addresses
 * - MODE_RMD160: Search for RIPEMD160 hashes
 * - MODE_XPOINT: Search for public key X-coordinates
 *
 * See search_common.h for shared declarations.
 */

#include "search_common.h"
#include "../output.h"

/*
 * NOTE: The actual implementation of thread_process() is currently in
 * keyhunt.cpp (lines 5261-5900). The function has been migrated to use
 * config-aware local variables instead of direct global access.
 *
 * The migration strategy uses local variables that are initialized from
 * globals at function entry. This provides a clean abstraction layer
 * for future migration to the thread_args struct approach.
 *
 * Example from thread_process():
 *
 *   // Config-aware variables - use config if available, else fall back to globals
 *   int local_mode = FLAGMODE;
 *   int local_search = FLAGSEARCH;
 *   int local_crypto = FLAGCRYPTO;
 *   bool local_endomorphism = FLAGENDOMORPHISM != 0;
 *   bool local_random = FLAGRANDOM != 0;
 *   bool local_quiet = FLAGQUIET != 0;
 *   bool local_matrix = FLAGMATRIX != 0;
 *
 *   // Function body uses local_* variables instead of FLAG* globals
 *   if (local_mode == MODE_ADDRESS) {
 *       if (local_crypto == CRYPTO_BTC) {
 *           // ADDRESS mode logic...
 *       }
 *   }
 *
 * Future migration steps:
 * 1. Update thread creation to pass thread_args instead of tothread
 * 2. Initialize local_* variables from cfg->search.* instead of globals
 * 3. Move the function from keyhunt.cpp to this file
 * 4. Create helper functions for address generation and checking
 */

/* Placeholder - actual implementation in keyhunt.cpp (config-aware) */
