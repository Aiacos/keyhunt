/*
 * mode_address.cpp - ADDRESS mode dispatcher
 *
 * Handles ADDRESS mode thread creation. The ADDRESS mode searches for
 * Bitcoin/Ethereum addresses using bloom filters and RIPEMD160 hashing.
 *
 * Init: Sets crypto default to BTC if not specified.
 * Run:  Creates threads calling thread_process (defined in search_address.cpp).
 * Cleanup: No-op (thread_args freed by thread functions).
 *
 * MIGRATION STATUS: Phase 4 extraction from keyhunt.cpp
 */

#include "modes.h"
#include "../search/search_common.h"
#include "../output.h"

#include <cstdlib>

/*
 * mode_address_init - ADDRESS mode initialization
 *
 * Called after shared init (file reading, sorting, bloom setup, config bridge)
 * but before thread creation. Crypto defaults (BTC) are set in keyhunt.cpp
 * before file reading since readFileAddress depends on FLAGCRYPTO.
 *
 * Currently a no-op; future plans may move more init logic here.
 */
static int mode_address_init(keyhunt_config_t * /*config*/) {
    return 0;
}

/*
 * mode_address_run - Create ADDRESS mode search threads
 *
 * Creates thread_count threads, each running thread_process from
 * search_address.cpp. Each thread receives a thread_args struct
 * with the config pointer and thread ID.
 */
static int mode_address_run(keyhunt_config_t *config,
                            platform_thread_t *tids, int thread_count) {
    struct thread_counter *steps =
        (struct thread_counter *)config->runtime.thread_counters;

    for (int j = 0; j < thread_count; j++) {
        steps[j].value = 0;
        thread_args *aargs = new thread_args{ config, j };
        int s = platform_thread_create(&tids[j], thread_process, (void *)aargs);
        if (s != 0) {
            output_error("pthread_create thread_process (address mode, thread %d)\n", j);
            delete aargs;
            return -1;
        }
    }
    return 0;
}

/*
 * mode_address_cleanup - ADDRESS mode cleanup
 *
 * Currently a no-op. thread_args are allocated with new and freed by
 * the thread function or when it completes.
 */
static void mode_address_cleanup(keyhunt_config_t * /*config*/) {
    /* No mode-specific cleanup needed */
}

/* Registered operations for ADDRESS mode (extern linkage for dispatch table) */
extern const mode_ops_t mode_address_ops;
const mode_ops_t mode_address_ops = {
    mode_address_init,
    mode_address_run,
    mode_address_cleanup
};
