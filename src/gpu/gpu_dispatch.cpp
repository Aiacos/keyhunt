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

#ifndef _WIN64
#include <signal.h>
#endif

#include "../error/enhanced_error.h"
#include "../secp256k1/SECP256k1.h"
#include "../monitoring/monitoring.h"
#include "../core/sysinfo.h"

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

/* ============================================================================
 * GPU multi-worker state (moved from keyhunt.cpp)
 * ============================================================================ */

static gpu_multi_worker_t *g_gpu_multi_workers_local = NULL;
static volatile sig_atomic_t g_sigint_received_local = 0;

#ifndef _WIN64
static void gpu_sigint_handler(int sig) {
    (void)sig;
    g_sigint_received_local = 1;
    g_gpu_should_stop.store(1, std::memory_order_release);
}
#endif

static void gpu_check_sigint_cleanup(void) {
    if (g_sigint_received_local && g_gpu_multi_workers_local != NULL) {
        output_info("\nReceived Ctrl+C, stopping multi-GPU workers...\n");
        gpu_worker_stop(g_gpu_multi_workers_local, 10000);
        g_gpu_multi_workers_local = NULL;
    }
}

/* ============================================================================
 * resolve_gpu_mode - GPU mode resolution (extracted from main())
 * ============================================================================ */

void resolve_gpu_mode(void) {
    int gpu_available = gpu_backend_available();
    const bool wantCompressed = (FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH);
    const bool wantUncompressed = (FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH);
    const int mode_supports_gpu_full = (FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_ADDRESS) &&
                    FLAGCRYPTO == CRYPTO_BTC && !FLAGENDOMORPHISM && (wantCompressed || wantUncompressed);
    const int mode_supports_gpu_hash = (FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_ADDRESS) &&
                    FLAGCRYPTO == CRYPTO_BTC && !FLAGENDOMORPHISM && wantCompressed && !wantUncompressed;

    if (FLAGGPU != 0 || FLAGGPU_FULL != 0) {
        if (gpu_available) {
            int available_backends = gpu_enumerate_backends();
            gpu_backend_type_t current_backend = gpu_backend_get_type();
            output_success("GPU Backend: %s\n", gpu_backend_type_name(current_backend));
            if (current_backend == GPU_BACKEND_TYPE_UNIFIED) {
                output_info("  Total devices: %d across multiple vendors\n", g_gpu_backend_info.gpu_count);
                if (available_backends & (1 << GPU_BACKEND_TYPE_CUDA)) output_info("  - CUDA backend available (NVIDIA GPUs)\n");
                if (available_backends & (1 << GPU_BACKEND_TYPE_OPENCL)) output_info("  - OpenCL backend available (AMD/Intel GPUs)\n");
                if (g_gpu_backend_info.name[0]) { output_info("  Primary device: %s", g_gpu_backend_info.name); if (g_gpu_backend_info.vendor[0]) output_info(" (%s)", g_gpu_backend_info.vendor); output_info("\n"); }
                output_info("  Compute units: %d, VRAM: %lu MB\n", g_gpu_backend_info.multiprocessors, (unsigned long)g_gpu_backend_info.vram_mb);
            } else if (current_backend == GPU_BACKEND_TYPE_CUDA) {
                output_success("  CUDA device: %s (%d SMs, %lu MB VRAM)\n", g_gpu_backend_info.name[0] ? g_gpu_backend_info.name : "NVIDIA GPU", g_gpu_backend_info.multiprocessors, (unsigned long)g_gpu_backend_info.vram_mb);
            } else if (current_backend == GPU_BACKEND_TYPE_OPENCL) {
                output_success("  OpenCL device: %s", g_gpu_backend_info.name[0] ? g_gpu_backend_info.name : "GPU");
                if (g_gpu_backend_info.vendor[0]) output_success(" (%s)", g_gpu_backend_info.vendor);
                output_success("\n");
                output_info("  Compute units: %d, VRAM: %lu MB\n", g_gpu_backend_info.multiprocessors, (unsigned long)g_gpu_backend_info.vram_mb);
            }
        } else {
            output_warning("No GPU devices detected\n");
#if defined(HAVE_CUDA_BACKEND) && defined(HAVE_OPENCL_BACKEND)
            output_info("Build supports: CUDA (NVIDIA) and OpenCL (AMD/Intel)\n");
#elif defined(HAVE_CUDA_BACKEND)
            output_info("Build supports: CUDA only (NVIDIA GPUs)\n");
#elif defined(HAVE_OPENCL_BACKEND)
            output_info("Build supports: OpenCL only (AMD/Intel GPUs)\n");
#else
            output_info("GPU backends not compiled - rebuild with CUDA or OpenCL support\n");
#endif
        }
    }

    if (FLAGGPU == -1 || FLAGGPU_FULL == -1) {
        if (gpu_available && mode_supports_gpu_full) {
            FLAGGPU = 1; FLAGGPU_FULL = 1;
            output_success("GPU auto: using full mode (ECC + hash160 + matching on GPU)\n");
        } else {
            FLAGGPU = 0; FLAGGPU_FULL = 0;
            if (!gpu_available) output_info("GPU auto: falling back to CPU (no GPU available)\n");
            else if (!mode_supports_gpu_full) output_info("GPU auto: falling back to CPU (mode not supported)\n");
        }
    }

    if ((FLAGGPU == 1 || FLAGGPU_FULL == 1) && !gpu_available) {
        output_warning("GPU requested but not available, falling back to CPU\n");
        FLAGGPU = 0; FLAGGPU_FULL = 0;
    }
    if ((FLAGGPU == 1 || FLAGGPU_FULL == 1) && !stride.IsOne()) {
        output_warning("GPU mode requires stride=1 (-I 1). Falling back to CPU.\n");
        FLAGGPU = 0; FLAGGPU_FULL = 0;
        FLAGGPU_HYBRID.store(0, std::memory_order_relaxed);
    }
    if (FLAGGPU_FULL == 1 && !mode_supports_gpu_full) {
        output_warning("GPU FULL not supported for this mode/options, using CPU\n");
        FLAGGPU = 0; FLAGGPU_FULL = 0;
    }
    if (FLAGGPU == 1 && FLAGGPU_FULL == 0 && !mode_supports_gpu_hash) {
        if (wantUncompressed && gpu_available && mode_supports_gpu_full) {
            output_info("GPU HASH mode does not support uncompressed; upgrading to GPU FULL\n");
            FLAGGPU_FULL = 1;
        } else {
            output_warning("GPU HASH not supported for this mode/options, using CPU\n");
            FLAGGPU = 0; FLAGGPU_FULL = 0;
        }
    }
    if (FLAGGPU_FULL == 1) output_success("GPU mode: FULL (secp256k1 + SHA256 + RIPEMD160 + matching on GPU)\n");
    else if (FLAGGPU == 1) output_success("GPU mode: HASH (CPU generates points, GPU computes hash160)\n");

    if (FLAGGPU == 1 && getenv("KEYHUNT_GPU_SELFTEST")) {
        if (!gpu_selftest_hash160_fromX()) {
            output_warning("Disabling GPU due to failed self-test\n");
            FLAGGPU = 0; FLAGGPU_FULL = 0;
        } else { printf("[OK] GPU self-test passed\n"); }
    }
    if ((FLAGGPU == 1 || FLAGGPU_FULL == 1) && !FLAGTHREADS) {
        int gpu_threads = 0;
        if (FLAGGPU_HYBRID) {
            gpu_threads = g_sysinfo.cpu_logical_cores > 0 ? g_sysinfo.cpu_logical_cores : g_sysinfo.recommended_threads;
            if (gpu_threads > 1) gpu_threads -= 1;
        } else {
            gpu_threads = g_sysinfo.cpu_physical_cores > 0 ? g_sysinfo.cpu_physical_cores : g_sysinfo.recommended_threads;
        }
        if (gpu_threads > 0 && gpu_threads < NTHREADS) {
            NTHREADS = gpu_threads;
            output_info("GPU active: using %d CPU threads\n", NTHREADS);
        }
    }
}

/* ============================================================================
 * run_gpu_full_search_mode - GPU full search orchestration (extracted from main())
 * ============================================================================ */

int run_gpu_full_search_mode(keyhunt_config_t *config, gpu_multi_worker_t **multi_gpu_workers) {
    output_success("Running GPU full search mode...\n");
    g_gpu_keys_checked.store(0, std::memory_order_release);
    g_gpu_keys_checked_cur.store(0, std::memory_order_release);
    g_gpu_should_stop.store(0, std::memory_order_release);

#ifndef _WIN64
    platform_thread_t gpu_stats_tid;
    int gpu_stats_started = 0;
    std::atomic<int> gpu_stats_stop{0};
    gpu_full_stats_args_t gpu_stats_args;
    memset(&gpu_stats_args, 0, sizeof(gpu_stats_args));
    if (OUTPUTSECONDS.IsGreater(&ZERO)) {
        gpu_stats_args.period_seconds = OUTPUTSECONDS.GetInt32();
        gpu_stats_args.stop_flag = &gpu_stats_stop;
        if (gpu_stats_args.period_seconds > 0) {
            if (platform_thread_create(&gpu_stats_tid, gpu_full_stats_thread, &gpu_stats_args) == 0) gpu_stats_started = 1;
        }
    }
#endif
    int gpu_result = -1;
    if (config->gpu.multi_gpu_enabled && config->gpu.device_count > 1) {
        output_success("Running multi-GPU search with %d devices...\n", config->gpu.device_count);
        multi_gpu_config_t sched_config;
        sched_config.device_count = config->gpu.device_count;
        for (int gi = 0; gi < config->gpu.device_count; gi++) sched_config.device_ids[gi] = config->gpu.device_ids[gi];
        sched_config.adaptive_balancing = true;
        sched_config.rebalance_interval_keys = 100000000;
        multi_gpu_scheduler_t *scheduler = multi_gpu_init(&sched_config);
        if (!scheduler) { output_error("Failed to initialize multi-GPU scheduler\n"); gpu_result = -1; }
        else {
            uint64_t rs = n_range_start.GetInt64(); uint64_t re = n_range_end.GetInt64();
            multi_gpu_set_range(scheduler, rs, re);
            worker_config_t worker_cfg = gpu_worker_default_config(scheduler, config->gpu.device_count);
            for (int gi = 0; gi < config->gpu.device_count; gi++) worker_cfg.device_ids[gi] = config->gpu.device_ids[gi];
            worker_cfg.batch_size = THREADBPWORKLOAD;
            gpu_multi_worker_t *workers = gpu_worker_init(&worker_cfg);
            if (!workers) { output_error("Failed to initialize multi-GPU workers\n"); multi_gpu_shutdown(scheduler); gpu_result = -1; }
            else {
                g_gpu_multi_workers_local = workers;
                if (multi_gpu_workers) *multi_gpu_workers = workers;
#ifndef _WIN64
                struct sigaction sa; memset(&sa, 0, sizeof(sa)); sa.sa_handler = gpu_sigint_handler; sigemptyset(&sa.sa_mask); sa.sa_flags = 0; sigaction(SIGINT, &sa, NULL);
#endif
                if (!gpu_worker_start(workers)) { output_error("Failed to start multi-GPU workers\n"); g_gpu_multi_workers_local = NULL; if (multi_gpu_workers) *multi_gpu_workers = NULL; gpu_worker_shutdown(workers); multi_gpu_shutdown(scheduler); gpu_result = -1; }
                else {
                    while (!gpu_worker_has_result(workers)) { sleep_ms(1000); if (g_gpu_should_stop.load(std::memory_order_acquire)) { gpu_check_sigint_cleanup(); break; } }
                    gpu_worker_stop(workers, 10000);
                    gpu_result = gpu_worker_has_result(workers) ? 0 : -1;
                    gpu_worker_shutdown(workers); g_gpu_multi_workers_local = NULL; if (multi_gpu_workers) *multi_gpu_workers = NULL; multi_gpu_shutdown(scheduler);
                }
            }
        }
    } else {
        if (config->gpu.multi_gpu_enabled && config->gpu.device_count == 1) output_info("Multi-GPU enabled but only 1 device specified, using single GPU mode\n");
        gpu_result = gpu_dispatch_run_full_search(config, &n_range_start, &n_range_end, &stride, N);
    }
#ifndef _WIN64
    gpu_stats_stop.store(1, std::memory_order_release);
    if (gpu_stats_started) platform_thread_join(gpu_stats_tid, NULL);
#endif
    if (gpu_result >= 0) {
        output_success("GPU search finished. Keys found: %d\n", gpu_result);
        output_success("Total keys checked: %" PRIu64 "\n", g_gpu_keys_checked.load(std::memory_order_acquire));
#ifndef _WIN64
        extern void shutdown_work_queue();
        shutdown_work_queue();
#endif
        gpu_backend_shutdown();
        output_success("Done!\n");
        return 0;
    }
    output_warning("GPU search failed, falling back to CPU threads\n");
    FLAGGPU_FULL = 0;
    return -1;
}

/* ============================================================================
 * run_gpu_hybrid_setup - GPU hybrid mode setup (extracted from main())
 * ============================================================================ */

/* Forward declaration for maybe_adjust_cpu_sequential_max (defined in keyhunt.cpp) */
extern void maybe_adjust_cpu_sequential_max(size_t threadCount, Int &cpuStart, Int &rangeEnd,
                                            const char *env_override, const char *tag);

int run_gpu_hybrid_setup(keyhunt_config_t *config,
                         platform_thread_t *gpu_thread_id,
                         gpu_hybrid_args_t *gpu_hybrid_args,
                         int *gpu_hybrid_started) {
    (void)config;
    if (!gpu_backend_available()) {
        output_warning("GPU not available for hybrid mode, falling back to CPU-only\n");
        FLAGGPU_HYBRID.store(0, std::memory_order_release);
        return -1;
    }

    {
        const char *env = getenv("KEYHUNT_HYBRID_GPU_PERCENT");
        if (env && *env) { int v = atoi(env); if (v >= 1 && v <= 99) g_gpu_range_percent = v; }
        if (g_gpu_range_percent <= 0) g_gpu_range_percent = hybrid_get_gpu_range_percent_default(NTHREADS);
        if (g_gpu_range_percent <= 0) g_gpu_range_percent = 80;
        output_info("HYBRID: split GPU %d%% / CPU %d%%\n", g_gpu_range_percent, 100 - g_gpu_range_percent);
    }

    float initial_cpu_ratio = 1.0f - (g_gpu_range_percent / 100.0f);
    adaptive_init(initial_cpu_ratio, n_range_start.GetInt64(), n_range_end.GetInt64());

    const char *ws = getenv("KEYHUNT_HYBRID_WORK_STEAL");
    const bool want_work_steal = (ws && *ws && atoi(ws) != 0);
    const bool can_work_steal = want_work_steal && !FLAGRANDOM && stride.IsOne();
    if (want_work_steal && !can_work_steal) output_warning("HYBRID: work-stealing requires non-random mode and stride=1; using static split\n");

    if (can_work_steal) {
        uint64_t block_size = 0x100000000ULL;
        const char *bs = getenv("KEYHUNT_HYBRID_BLOCK_SIZE");
        if (bs && *bs) { if (bs[0] == '0' && (bs[1] == 'x' || bs[1] == 'X')) block_size = strtoull(bs + 2, NULL, 16); else block_size = strtoull(bs, NULL, 10); }
        if (block_size < 1024ULL) block_size = 1024ULL;
        block_size = (block_size / 1024ULL) * 1024ULL;
        output_success("Running GPU+CPU hybrid mode (work-stealing)...\n");
        g_work_pool.init(&n_range_start, &n_range_end, block_size);
        gpu_hybrid_args->start_key.Set(&n_range_start); gpu_hybrid_args->end_key.Set(&n_range_end);
        gpu_hybrid_args->stride.Set(&stride); gpu_hybrid_args->target_count = N;
        gpu_hybrid_args->result.store(0, std::memory_order_release);
        gpu_hybrid_args->completed.store(0, std::memory_order_release);
        g_gpu_keys_checked.store(0, std::memory_order_release);
        g_gpu_keys_checked_cur.store(0, std::memory_order_release);
        g_gpu_should_stop.store(0, std::memory_order_release);
        int err = platform_thread_create(gpu_thread_id, gpu_dispatch_hybrid_thread, gpu_hybrid_args);
        if (err != 0) { output_warning("Failed to start GPU thread\n"); g_work_pool.disable(); FLAGGPU_HYBRID.store(0, std::memory_order_release); return -1; }
        *gpu_hybrid_started = 1;
        output_success("GPU thread started, CPU uses normal fast algorithm\n");
    } else {
        output_success("Running GPU+CPU hybrid mode (static split)...\n");
        Int range_diff, gpu_portion, gpu_range_end, cpu_range_start;
        range_diff.Set(&n_range_end); range_diff.Sub(&n_range_start);
        gpu_portion.Set(&range_diff); gpu_portion.Mult(g_gpu_range_percent);
        Int divisor; divisor.SetInt32(100); gpu_portion.Div(&divisor);
        gpu_range_end.Set(&n_range_start); gpu_range_end.Add(&gpu_portion);
        cpu_range_start.Set(&gpu_range_end);
        output_success("GPU handles %d%% of range, CPU handles %d%%\n", g_gpu_range_percent, 100 - g_gpu_range_percent);

        char *hextemp;
        hextemp = n_range_start.GetBase16(); output_success("GPU range: 0x%s", hextemp); free(hextemp);
        { Int gpu_end_inclusive; gpu_end_inclusive.Set(&gpu_range_end); if (gpu_end_inclusive.IsGreater(&n_range_start)) gpu_end_inclusive.SubOne(); hextemp = gpu_end_inclusive.GetBase16(); printf(" - 0x%s\n", hextemp); free(hextemp); }
        hextemp = cpu_range_start.GetBase16(); output_success("CPU range: 0x%s", hextemp); free(hextemp);
        { Int cpu_end_inclusive; cpu_end_inclusive.Set(&n_range_end); if (cpu_end_inclusive.IsGreater(&cpu_range_start)) cpu_end_inclusive.SubOne(); hextemp = cpu_end_inclusive.GetBase16(); printf(" - 0x%s\n", hextemp); free(hextemp); }

        gpu_hybrid_args->start_key.Set(&n_range_start); gpu_hybrid_args->end_key.Set(&gpu_range_end);
        gpu_hybrid_args->stride.Set(&stride); gpu_hybrid_args->target_count = N;
        gpu_hybrid_args->result.store(0, std::memory_order_release);
        gpu_hybrid_args->completed.store(0, std::memory_order_release);
        g_gpu_keys_checked.store(0, std::memory_order_release);
        g_gpu_keys_checked_cur.store(0, std::memory_order_release);
        g_gpu_should_stop.store(0, std::memory_order_release);
        int err = platform_thread_create(gpu_thread_id, gpu_dispatch_hybrid_thread, gpu_hybrid_args);
        if (err != 0) { output_warning("Failed to start GPU thread\n"); FLAGGPU_HYBRID.store(0, std::memory_order_release); return -1; }
        *gpu_hybrid_started = 1;
        n_range_start.Set(&cpu_range_start);
        maybe_adjust_cpu_sequential_max((size_t)NTHREADS, cpu_range_start, n_range_end, "KEYHUNT_HYBRID_CPU_N", "HYBRID");
        output_success("GPU thread started, CPU uses normal fast algorithm\n");
    }
    return 0;
}

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
