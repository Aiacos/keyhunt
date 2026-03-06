/*
 * modes.cpp - Mode dispatch table implementation
 *
 * Maps search_mode_t values to their corresponding mode_ops_t structs.
 * Each mode file declares its ops as an extern const, registered here.
 *
 * MIGRATION STATUS: Phase 4 extraction from keyhunt.cpp
 */

#include "modes.h"
#include "../search/search_common.h"

/* Mode ops declarations from individual mode files */
extern const mode_ops_t mode_address_ops;   /* mode_address.cpp */

/*
 * mode_get_ops - Get operations for a given search mode
 *
 * Returns the mode_ops_t for registered modes.
 * Returns NULL for unregistered modes (BSGS, VANITY, MINIKEYS use
 * their own dispatch paths in keyhunt.cpp until extracted).
 */
const mode_ops_t* mode_get_ops(int mode) {
    switch (mode) {
        case MODE_ADDRESS:
            return &mode_address_ops;
        default:
            return nullptr;
    }
}

/*
 * mode_dispatch - Convenience dispatcher
 *
 * Looks up the mode ops and calls init + run.
 * Returns 0 on success, -1 if mode is not registered.
 */
int mode_dispatch(keyhunt_config_t *config,
                  platform_thread_t *tids, int thread_count) {
    int mode = (int)config->search.mode;
    const mode_ops_t *ops = mode_get_ops(mode);

    if (!ops) {
        return -1;  /* Mode not registered in dispatch table */
    }

    int rc = ops->init(config);
    if (rc != 0) {
        return rc;
    }

    return ops->run(config, tids, thread_count);
}
