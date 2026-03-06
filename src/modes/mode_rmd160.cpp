/*
 * mode_rmd160.cpp - RMD160 mode dispatcher
 *
 * Handles RMD160 mode thread creation. The RMD160 mode searches for
 * RIPEMD160 hashes directly, bypassing address encoding.
 *
 * Init: No-op (crypto default to BTC is set in keyhunt.cpp before file reading).
 * Run:  Creates threads calling thread_process (defined in search_address.cpp).
 * Cleanup: No-op (thread_args freed by thread functions).
 *
 * MIGRATION STATUS: Phase 4 extraction from keyhunt.cpp
 */

#include "modes.h"
#include "../search/search_common.h"
#include "../output.h"

/*
 * mode_rmd160_init - RMD160 mode initialization
 *
 * Called after shared init (file reading, sorting, config bridge).
 * Crypto default (BTC) is set in keyhunt.cpp before readFileAddress
 * since the file reader depends on FLAGCRYPTO.
 */
static int mode_rmd160_init(keyhunt_config_t * /*config*/) {
    return 0;
}

/*
 * mode_rmd160_run - Create RMD160 mode search threads
 *
 * Creates thread_count threads, each running thread_process from
 * search_address.cpp. The thread_process function handles RMD160
 * mode internally (same code path as ADDRESS mode).
 */
static int mode_rmd160_run(keyhunt_config_t *config,
                           platform_thread_t *tids, int thread_count) {
    struct thread_counter *steps =
        (struct thread_counter *)config->runtime.thread_counters;

    for (int j = 0; j < thread_count; j++) {
        steps[j].value = 0;
        thread_args *aargs = new thread_args{ config, j };
        int s = platform_thread_create(&tids[j], thread_process, (void *)aargs);
        if (s != 0) {
            output_error("pthread_create thread_process (rmd160 mode, thread %d)\n", j);
            delete aargs;
            return -1;
        }
    }
    return 0;
}

/*
 * mode_rmd160_cleanup - RMD160 mode cleanup (no-op)
 */
static void mode_rmd160_cleanup(keyhunt_config_t * /*config*/) {
    /* No mode-specific cleanup needed */
}

/* Registered operations for RMD160 mode (extern linkage for dispatch table) */
extern const mode_ops_t mode_rmd160_ops;
const mode_ops_t mode_rmd160_ops = {
    mode_rmd160_init,
    mode_rmd160_run,
    mode_rmd160_cleanup
};
