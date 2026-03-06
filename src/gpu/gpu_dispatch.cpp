/*
 * gpu_dispatch.cpp - GPU dispatch function implementations
 *
 * Extracted from keyhunt.cpp (Phase 4, Plan 05).
 *
 * Functions handle GPU search orchestration: uploading tables/targets/bloom,
 * executing full GPU search, and the hybrid GPU thread (work-stealing or
 * static-range mode).
 */

#include "gpu_dispatch.h"
#include "gpu_backend.h"
#include "gpu_multi_worker.h"
#include "../io/io.h"
#include "../output.h"
#include "../util/work_queue.h"
#include "../search/search_common.h"
#include "../hybrid/adaptive_scheduler.h"
#include "../secp256k1/SECP256k1.h"
#include "../secp256k1/Int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "../error/enhanced_error.h"
#include "../secp256k1/SECP256k1.h"

/* ============================================================================
 * File-local state
 * ============================================================================ */

static int g_gpu_bloom_uploaded = 0;

/* Shared globals (secp, flags, counters, config pointer, etc.) */
#include "../globals.h"

/* ============================================================================
 * GPU Dispatch Implementations
 * ============================================================================ */

int gpu_dispatch_upload_gtable(void) {
    if (!gpu_backend_available()) return 1;

    /* GTable has 256*32 = 8192 points, each 64 bytes (X + Y, big-endian) */
    const size_t GTABLE_POINTS = 256 * 32;
    uint8_t *gtable_data = (uint8_t *)malloc(GTABLE_POINTS * 64);
    if (!gtable_data) return 1;

    secp->ExportGTable(gtable_data);

    int result = gpu_upload_gtable(gtable_data, GTABLE_POINTS);
    free(gtable_data);
    return result;
}

int gpu_dispatch_upload_targets(void *table, int64_t count) {
    if (!gpu_backend_available() || count <= 0) return 1;

    struct address_value *addressTable = (struct address_value *)table;
    return gpu_upload_targets((const uint8_t *)addressTable, (size_t)count);
}

int gpu_dispatch_upload_bloom(void *table, int64_t count) {
    if (!gpu_backend_available() || count <= 32) return 1;

    struct address_value *addressTable = (struct address_value *)table;

    const int num_hashes = 4;
    const uint32_t GOLDEN = 0x9E3779B9u;
    const uint64_t bits_per_element = 12;
    uint64_t desired_bits = (uint64_t)count * bits_per_element;

    if (desired_bits < (1ULL << 16)) desired_bits = (1ULL << 16);

    /* Round up to power-of-two bits for fast GPU masking */
    auto next_pow2_u64 = [](uint64_t v) -> uint64_t {
        if (v <= 1) return 1;
        v--;
        v |= v >> 1;  v |= v >> 2;  v |= v >> 4;
        v |= v >> 8;  v |= v >> 16; v |= v >> 32;
        return v + 1;
    };

    uint64_t bloom_bits = next_pow2_u64(desired_bits);
    if (bloom_bits > (1ULL << 32)) bloom_bits = (1ULL << 32);
    if (bloom_bits < 64) bloom_bits = 64;

    size_t bloom_bytes = (size_t)(bloom_bits / 8);
    if ((bloom_bytes & 7) != 0) {
        bloom_bytes = (bloom_bytes + 7) & ~(size_t)7;
        bloom_bits = (uint64_t)bloom_bytes * 8;
    }

    uint8_t *bloom = (uint8_t *)calloc(bloom_bytes, 1);
    if (!bloom) return 1;

    uint64_t *bloom64 = (uint64_t *)bloom;
    uint32_t mask = (uint32_t)(bloom_bits - 1);

    for (int64_t i = 0; i < count; i++) {
        const uint8_t *h = addressTable[i].value;

        uint32_t h0 = ((uint32_t)h[0] << 8) | (uint32_t)h[1];
        uint32_t h1 = ((uint32_t)h[2] << 8) | (uint32_t)h[3];
        uint32_t h2 = ((uint32_t)h[4] << 8) | (uint32_t)h[5];
        uint32_t h3 = ((uint32_t)h[6] << 8) | (uint32_t)h[7];

        uint32_t idx0 = (h0 * GOLDEN) & mask;
        uint32_t idx1 = (h1 * GOLDEN) & mask;
        uint32_t idx2 = (h2 * GOLDEN) & mask;
        uint32_t idx3 = (h3 * GOLDEN) & mask;

        bloom64[idx0 >> 6] |= (1ULL << (idx0 & 63));
        bloom64[idx1 >> 6] |= (1ULL << (idx1 & 63));
        bloom64[idx2 >> 6] |= (1ULL << (idx2 & 63));
        bloom64[idx3 >> 6] |= (1ULL << (idx3 & 63));
    }

    int rc = gpu_upload_bloom(bloom, bloom_bytes, num_hashes);
    free(bloom);

    if (rc == 0) {
        g_gpu_bloom_uploaded = 1;
    }

    return rc;
}

void gpu_dispatch_found_callback(const uint8_t *privkey_be, int compressed,
                                 void *userdata) {
    (void)userdata;

    Int key;
    key.Set32Bytes((unsigned char *)privkey_be);

    writekey(g_kh_config_ptr, compressed ? true : false, &key);
}

platform_thread_return_t PLATFORM_THREAD_CALL
gpu_dispatch_hybrid_thread(void *arg) {
    gpu_hybrid_args_t *args = (gpu_hybrid_args_t *)arg;
    int total_found = 0;
    uint64_t blocks_processed = 0;
    uint64_t last_report_time = adaptive_time_ms();
    uint64_t keys_since_last_report = 0;

    /*
     * Two modes:
     * 1) Work-stealing: GPU pulls blocks from shared pool
     * 2) Static split: GPU scans the fixed [start_key, end_key] range once
     */
    if (!g_work_pool.enabled) {
        printf("[GPU] Static-range thread started\n");
        uint64_t start_time = adaptive_time_ms();
        int found = gpu_dispatch_run_full_search(
            g_kh_config_ptr, &args->start_key, &args->end_key,
            &args->stride, args->target_count);
        if (found > 0) total_found = found;
        (void)(adaptive_time_ms() - start_time);
        args->result.store(total_found, std::memory_order_release);
        args->completed.store(1, std::memory_order_release);
        printf("[GPU] Static-range thread completed: %d keys found\n",
               total_found);
        return (platform_thread_return_t)0;
    }

    printf("[GPU] Work-stealing thread started\n");

    while (!g_gpu_should_stop.load(std::memory_order_acquire) &&
           g_work_pool.enabled) {
        check_sigint_cleanup();
        Int block_start, block_end;

        if (!g_work_pool.get_block(block_start, block_end)) {
            break; /* No more work available */
        }

        (void)adaptive_time_ms();

        int found = gpu_dispatch_run_full_search(
            g_kh_config_ptr, &block_start, &block_end,
            &args->stride, args->target_count);
        if (found > 0) {
            total_found += found;
        }
        blocks_processed++;

        uint64_t block_keys = g_work_pool.block_size;
        keys_since_last_report += block_keys;

        uint64_t now = adaptive_time_ms();
        if ((now - last_report_time >= 500) || (blocks_processed % 5 == 0)) {
            uint64_t elapsed = now - last_report_time;
            if (elapsed > 0 && keys_since_last_report > 0) {
                adaptive_report_work(WORKER_GPU, keys_since_last_report,
                                     elapsed);
                keys_since_last_report = 0;
                last_report_time = now;
            }
        }

        if (blocks_processed % 10 == 0) {
            printf("[GPU] Processed %lu blocks, total found: %d\n",
                   (unsigned long)blocks_processed, total_found);
        }
    }

    printf("[GPU] Work-stealing thread completed: %lu blocks, %d keys found\n",
           (unsigned long)blocks_processed, total_found);

    args->result.store(total_found, std::memory_order_release);
    args->completed.store(1, std::memory_order_release);

    return (platform_thread_return_t)0;
}

int gpu_dispatch_run_full_search(keyhunt_config_t *config_ptr,
                                 Int *start_key, Int *end_key,
                                 Int *stride_val, int64_t target_count) {
    if (!gpu_backend_available()) {
        output_warning("GPU not available, cannot run full GPU search\n");
        return -1;
    }

    gpu_search_config_t sconfig;
    memset(&sconfig, 0, sizeof(sconfig));

    start_key->Get32Bytes(sconfig.start_key);
    end_key->Get32Bytes(sconfig.end_key);
    stride_val->Get32Bytes(sconfig.stride);

    sconfig.target_count = target_count;
    sconfig.search_compressed =
        (FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH) ? 1 : 0;
    sconfig.search_uncompressed =
        (FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH) ? 1 : 0;
    sconfig.use_bloom =
        (g_gpu_bloom_uploaded && target_count > 32) ? 1 : 0;

    sconfig.callback = gpu_dispatch_found_callback;
    sconfig.callback_userdata = NULL;

    if (g_work_pool.enabled) {
        g_gpu_keys_checked_cur.store(0, std::memory_order_release);
        sconfig.keys_checked = &g_gpu_keys_checked_cur;
    } else {
        sconfig.keys_checked = &g_gpu_keys_checked;
    }
    sconfig.should_stop = &g_gpu_should_stop;
    sconfig.quiet = (FLAGQUIET != 0) ||
                    (FLAGGPU_HYBRID != 0) ||
                    OUTPUTSECONDS.IsGreater(&ZERO);

    output_success("Starting GPU full search (ECC + hash160 + matching on GPU)\n");
    output_success("Target count: %" PRId64 ", using %s\n",
                   target_count,
                   target_count == 1
                       ? "direct comparison"
                       : (sconfig.use_bloom ? "GPU bloom + binary search"
                                            : "binary search"));
    output_success("Search mode: %s\n",
                   (sconfig.search_compressed && sconfig.search_uncompressed)
                       ? "compressed + uncompressed"
                       : (sconfig.search_compressed
                              ? "compressed only"
                              : (sconfig.search_uncompressed
                                     ? "uncompressed only"
                                     : "none")));

    int found = gpu_full_search(&sconfig);

    if (g_work_pool.enabled) {
        uint64_t done =
            g_gpu_keys_checked_cur.load(std::memory_order_acquire);
        g_gpu_keys_checked.fetch_add(done, std::memory_order_release);
        g_gpu_keys_checked_cur.store(0, std::memory_order_release);
    }

    return found;
}

int gpu_dispatch_bloom_uploaded(void) {
    return g_gpu_bloom_uploaded;
}

/* ============================================================================
 * GPU self-test (hash160 from X-coordinates)
 * ============================================================================ */

bool gpu_selftest_hash160_fromX() {
    const size_t kCount = 16;
    alignas(32) uint8_t x32_be[kCount * 32];
    Int xs[kCount];

    for (size_t i = 0; i < kCount; ++i) {
        uint8_t bytes[32];
        for (size_t j = 0; j < 32; ++j) {
            bytes[j] = (uint8_t)((i * 17 + j) & 0xFF);
        }
        xs[i].Set32Bytes(bytes);
        memcpy(x32_be + i * 32, bytes, 32);
    }

    alignas(32) uint8_t cpu02[kCount][20];
    alignas(32) uint8_t cpu03[kCount][20];
    for (size_t i = 0; i < kCount; i += 4) {
        secp->GetHash160_fromX(P2PKH, 0x02, &xs[i], &xs[i + 1], &xs[i + 2], &xs[i + 3],
            cpu02[i], cpu02[i + 1], cpu02[i + 2], cpu02[i + 3]);
        secp->GetHash160_fromX(P2PKH, 0x03, &xs[i], &xs[i + 1], &xs[i + 2], &xs[i + 3],
            cpu03[i], cpu03[i + 1], cpu03[i + 2], cpu03[i + 3]);
    }

    alignas(32) uint8_t gpu02[kCount][20];
    alignas(32) uint8_t gpu03[kCount][20];
    if (gpu_hash160_fromX_batch(x32_be, kCount, gpu02[0], gpu03[0]) != 0) {
        return false;
    }

    for (size_t i = 0; i < kCount; ++i) {
        if (memcmp(cpu02[i], gpu02[i], 20) != 0) return false;
        if (memcmp(cpu03[i], gpu03[i], 20) != 0) return false;
    }

    return true;
}

/* ============================================================================
 * Hybrid GPU range percent default
 * ============================================================================ */

/* g_gpu_backend_info declared in globals.h */

int hybrid_get_gpu_range_percent_default(int cpu_threads) {
    if (cpu_threads <= 0) return 80;
    const int sms = g_gpu_backend_info.multiprocessors;
    if (sms > 0) {
        const double ratio = ((double)sms * 5.0) / (double)cpu_threads;
        const double pct = (ratio / (ratio + 1.0)) * 100.0;
        int v = (int)lround(pct);
        if (v < 50) v = 50;
        if (v > 99) v = 99;
        return v;
    }
    return 80;
}
