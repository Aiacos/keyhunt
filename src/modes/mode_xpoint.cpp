/*
 * mode_xpoint.cpp - XPOINT mode dispatcher
 *
 * Handles XPOINT mode thread creation. The XPOINT mode searches for
 * public key X-coordinates, which is the fastest mode when the target
 * public key is known.
 *
 * Init: No-op (XPOINT uses same thread_process as ADDRESS/RMD160).
 * Run:  Creates threads calling thread_process (defined in search_address.cpp).
 * Cleanup: No-op (thread_args freed by thread functions).
 *
 * MIGRATION STATUS: Phase 4 extraction from keyhunt.cpp
 */

#include "modes.h"
#include "../search/search_common.h"
#include "../output.h"

/*
 * mode_xpoint_init - XPOINT mode initialization
 *
 * Called after shared init (file reading, sorting, config bridge).
 * XPOINT mode does not need crypto defaults -- it works directly
 * with X-coordinates, not addresses.
 */
static int mode_xpoint_init(keyhunt_config_t * /*config*/) {
    return 0;
}

/*
 * mode_xpoint_run - Create XPOINT mode search threads
 *
 * Creates thread_count threads, each running thread_process from
 * search_address.cpp. The thread_process function handles XPOINT
 * mode internally (checking config->search.mode).
 */
static int mode_xpoint_run(keyhunt_config_t *config,
                           platform_thread_t *tids, int thread_count) {
    struct thread_counter *steps =
        (struct thread_counter *)config->runtime.thread_counters;

    for (int j = 0; j < thread_count; j++) {
        steps[j].value = 0;
        thread_args *aargs = new thread_args{ config, j };
        int s = platform_thread_create(&tids[j], thread_process, (void *)aargs);
        if (s != 0) {
            output_error("pthread_create thread_process (xpoint mode, thread %d)\n", j);
            delete aargs;
            return -1;
        }
    }
    return 0;
}

/*
 * mode_xpoint_cleanup - XPOINT mode cleanup (no-op)
 */
static void mode_xpoint_cleanup(keyhunt_config_t * /*config*/) {
    /* No mode-specific cleanup needed */
}

/* Registered operations for XPOINT mode (extern linkage for dispatch table) */
extern const mode_ops_t mode_xpoint_ops;
const mode_ops_t mode_xpoint_ops = {
    mode_xpoint_init,
    mode_xpoint_run,
    mode_xpoint_cleanup
};
