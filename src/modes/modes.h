/*
 * modes.h - Mode dispatch table interface
 *
 * Provides a dispatch table mapping search_mode_t to mode-specific
 * init/run/cleanup functions. Each mode file (mode_address.cpp, etc.)
 * registers its operations here.
 *
 * Usage from orchestrator (keyhunt.cpp):
 *   const mode_ops_t *ops = mode_get_ops(config->search.mode);
 *   if (ops) {
 *       ops->init(config);
 *       ops->run(config, &tids, &thread_count);
 *       // ... monitoring loop ...
 *       ops->cleanup(config);
 *   }
 *
 * Or use the convenience function:
 *   mode_dispatch(config, &tids, &thread_count);
 */

#ifndef MODES_H
#define MODES_H

#include "../config/config.h"
#include "../platform/platform.h"

/*
 * Mode operations structure.
 *
 * Each mode provides three functions:
 * - init:    Mode-specific initialization (set defaults, etc.)
 * - run:     Create search threads, populate tids array
 * - cleanup: Free mode-specific resources (thread args, etc.)
 */
typedef struct {
    /*
     * Initialize mode-specific state.
     *
     * Called after shared initialization (file reading, sorting, config bridge)
     * but before thread creation. Sets mode-specific defaults.
     *
     * Returns 0 on success, non-zero on error.
     */
    int (*init)(keyhunt_config_t *config);

    /*
     * Create search threads for this mode.
     *
     * Allocates thread_args, calls platform_thread_create for each thread.
     * The caller (orchestrator) owns the tid array and steps/ends arrays.
     *
     * Parameters:
     *   config       - Configuration with runtime state populated
     *   tids         - Pre-allocated array of platform_thread_t[num_threads]
     *   thread_count - Number of threads to create (from config->runtime.num_threads)
     *
     * Returns 0 on success, non-zero on error.
     */
    int (*run)(keyhunt_config_t *config,
               platform_thread_t *tids, int thread_count);

    /*
     * Clean up mode-specific resources.
     *
     * Called after all threads have been joined.
     * Currently a no-op for most modes (thread_args are freed by threads).
     */
    void (*cleanup)(keyhunt_config_t *config);
} mode_ops_t;

/*
 * Get the operations struct for a given search mode.
 *
 * Returns NULL for modes that don't have a registered dispatcher
 * (e.g., MODE_BSGS which has its own init path).
 */
const mode_ops_t* mode_get_ops(int mode);

/*
 * Convenience dispatcher: calls init + run for the configured mode.
 *
 * Parameters:
 *   config       - Configuration with search.mode set
 *   tids         - Pre-allocated array of platform_thread_t[num_threads]
 *   thread_count - Number of threads to create
 *
 * Returns 0 on success, non-zero on error or unregistered mode.
 */
int mode_dispatch(keyhunt_config_t *config,
                  platform_thread_t *tids, int thread_count);

#endif /* MODES_H */
