/*
 * test_stubs.cpp - Stub implementations for symbols defined in keyhunt.cpp
 *
 * The test build links all shared modules but NOT keyhunt.cpp (which has main()).
 * Some modules reference symbols from keyhunt.cpp; these stubs satisfy the linker.
 */

#include "../src/secp256k1/Int.h"

/* Work queue stubs */
void shutdown_work_queue() {}
void check_sigint_cleanup(void) {}

/* Base key acquisition stub - always fails (tests don't use work queue) */
bool acquire_base_key(Int &key) {
    (void)key;
    return false;
}

/* Thread-local block cache stubs */
thread_local Int cpu_cached_block_end;
thread_local bool cpu_cached_block_valid = false;

/* CPU sequential max adjustment stub */
void maybe_adjust_cpu_sequential_max(size_t, Int &, Int &, const char *, const char *) {}
