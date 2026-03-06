/*
 * mode_minikeys.cpp - MINIKEYS mode dispatcher
 *
 * Handles MINIKEYS mode thread creation. The MINIKEYS mode searches for
 * Bitcoin minikey format private keys (22-character Base58 strings).
 *
 * Init: No-op (minikey parsing and coinbuffer setup done in keyhunt.cpp CLI).
 * Run:  Creates threads calling thread_process_minikeys (search_minikeys.cpp).
 * Cleanup: No-op (thread_args freed by thread functions).
 *
 * MIGRATION STATUS: Phase 4 extraction from keyhunt.cpp
 */

#include "modes.h"
#include "../search/search_common.h"
#include "../output.h"

#include <cstdlib>

/*
 * mode_minikeys_init - MINIKEYS mode initialization
 *
 * Called after shared init (minikey base parsing, coinbuffer setup, config bridge).
 * Minikey-specific state (str_baseminikey, raw_baseminikey, minikeyN, coinbuffer)
 * is populated during CLI parsing in keyhunt.cpp, then bridged to config->runtime
 * before thread creation.
 *
 * Currently a no-op; future plans may move minikey init logic here.
 */
static int mode_minikeys_init(keyhunt_config_t * /*config*/) {
    return 0;
}

/*
 * mode_minikeys_run - Create MINIKEYS mode search threads
 *
 * Creates thread_count threads, each running thread_process_minikeys from
 * search_minikeys.cpp. Each thread receives a thread_args struct with
 * the config pointer and thread ID.
 */
static int mode_minikeys_run(keyhunt_config_t *config,
                             platform_thread_t *tids, int thread_count) {
    struct thread_counter *steps =
        (struct thread_counter *)config->runtime.thread_counters;

    for (int j = 0; j < thread_count; j++) {
        steps[j].value = 0;
        thread_args *margs = new thread_args{ config, j };
        int s = platform_thread_create(&tids[j], thread_process_minikeys, (void *)margs);
        if (s != 0) {
            output_error("pthread_create thread_process_minikeys (minikeys mode, thread %d)\n", j);
            delete margs;
            return -1;
        }
    }
    return 0;
}

/*
 * mode_minikeys_cleanup - MINIKEYS mode cleanup
 *
 * Currently a no-op. thread_args are allocated with new and freed by
 * the thread function or when it completes.
 */
static void mode_minikeys_cleanup(keyhunt_config_t * /*config*/) {
    /* No mode-specific cleanup needed */
}

/* Registered operations for MINIKEYS mode (extern linkage for dispatch table) */
extern const mode_ops_t mode_minikeys_ops;
const mode_ops_t mode_minikeys_ops = {
    mode_minikeys_init,
    mode_minikeys_run,
    mode_minikeys_cleanup
};
