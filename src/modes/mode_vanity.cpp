/*
 * mode_vanity.cpp - VANITY mode dispatcher
 *
 * Handles VANITY mode thread creation. The VANITY mode generates vanity
 * addresses with user-specified prefixes (e.g., 1LOVE..., 1Pizza...).
 *
 * Init: No-op (vanity bloom/targets set up during CLI parsing in keyhunt.cpp).
 * Run:  Creates threads calling thread_process_vanity (search_vanity.cpp).
 * Cleanup: No-op (thread_args freed by thread functions).
 *
 * MIGRATION STATUS: Phase 4 extraction from keyhunt.cpp
 */

#include "modes.h"
#include "../search/search_common.h"
#include "../output.h"

#include <cstdlib>

/*
 * mode_vanity_init - VANITY mode initialization
 *
 * Called after shared init (vanity file reading, bloom setup, config bridge).
 * Vanity-specific state (bloom filter, RMD targets, address targets) is
 * populated during CLI parsing and file reading in keyhunt.cpp, then bridged
 * to config->runtime before thread creation.
 *
 * Currently a no-op; future plans may move vanity init logic here.
 */
static int mode_vanity_init(keyhunt_config_t * /*config*/) {
    return 0;
}

/*
 * mode_vanity_run - Create VANITY mode search threads
 *
 * Creates thread_count threads, each running thread_process_vanity from
 * search_vanity.cpp. Each thread receives a thread_args struct with
 * the config pointer and thread ID.
 */
static int mode_vanity_run(keyhunt_config_t *config,
                           platform_thread_t *tids, int thread_count) {
    struct thread_counter *steps =
        (struct thread_counter *)config->runtime.thread_counters;

    for (int j = 0; j < thread_count; j++) {
        steps[j].value = 0;
        thread_args *vargs = new thread_args{ config, j };
        int s = platform_thread_create(&tids[j], thread_process_vanity, (void *)vargs);
        if (s != 0) {
            output_error("pthread_create thread_process_vanity (vanity mode, thread %d)\n", j);
            delete vargs;
            return -1;
        }
    }
    return 0;
}

/*
 * mode_vanity_cleanup - VANITY mode cleanup
 *
 * Currently a no-op. thread_args are allocated with new and freed by
 * the thread function or when it completes.
 */
static void mode_vanity_cleanup(keyhunt_config_t * /*config*/) {
    /* No mode-specific cleanup needed */
}

/* Registered operations for VANITY mode (extern linkage for dispatch table) */
extern const mode_ops_t mode_vanity_ops;
const mode_ops_t mode_vanity_ops = {
    mode_vanity_init,
    mode_vanity_run,
    mode_vanity_cleanup
};
