/*
 * keyhunt.cpp - High-performance cryptocurrency private key search tool
 *
 * Develop by Alberto
 * email: albertobsd@gmail.com
 *
 * Search Modes:
 *   - MODE_ADDRESS (1): Search for Bitcoin addresses using bloom filters
 *   - MODE_RMD160 (3): Search for RIPEMD160 hashes directly
 *   - MODE_XPOINT (0): Search for public key X-coordinates (fastest)
 *   - MODE_BSGS (2): Baby Step Giant Step for known public keys
 *   - MODE_VANITY (6): Generate vanity addresses with specific prefixes
 *   - MODE_MINIKEYS (5): Search minikey format private keys
 *
 * Architecture:
 *   - Multi-threaded with SIMD optimizations (SSE2/AVX2/AVX-512)
 *   - Bloom filters for fast target lookup
 *   - Support for GPU acceleration (CUDA + OpenCL, multi-vendor)
 *   - Distributed mode for multi-machine coordination
 *
 * See src/search/search_common.h for modular search declarations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <chrono>
#include <vector>
#include <algorithm>
#include <atomic>
#include <inttypes.h>
#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#endif
#include "base58/libbase58.h"
#include "bloom/bloom.h"
#include "bloom/bloom_wrapper.h"
#include "sha3/sha3.h"
#include "core/util.h"
#include "core/workqueue.h"
#include "core/sysinfo.h"
#include "core/parameter_validator.h"
#include "error/enhanced_error.h"
#include "gpu/gpu_backend.h"
#include "gpu/gpu_multi_worker.h"
#include "config/config.h"
#include "hybrid/adaptive_scheduler.h"
/* wizard, benchmark, diagnostics moved to cli.cpp (parse_cli_args) */
#include "output.h"
#include "progress.h"
#include "cli.h"
#include "bsgs/bsgs_fast.h"
#include "bsgs/bsgs_sort.h"
#include "sort/sort.h"
#include "crypto/address_util.h"
#include "crypto/bloom_init.h"
#include "io/io.h"
#include "search/search_common.h"
#include "modes/modes.h"
#include "modes/bsgs_globals.h"
#include "gpu/gpu_dispatch.h"
#include "monitoring/monitoring.h"

#include "secp256k1/SECP256k1.h"
#include "secp256k1/Point.h"
#include "secp256k1/Int.h"
#include "secp256k1/IntGroup.h"
#include "secp256k1/Random.h"

#include "hash/sha256.h"
#include "hash/ripemd160.h"
#include "platform/platform.h"

#if defined(_WIN64) && !defined(__CYGWIN__)
#include "getopt.h"
#else
#include <signal.h>
#ifdef __linux__
#include <sys/mman.h>
#include <sys/random.h>
#include <linux/random.h>
#endif
#endif

#include "util/thread_util.h"
#include "util/profiling.h"
#include "util/work_queue.h"
#include "globals.h"

typedef kh_profile_scope_t profile_scope_t;

/* ============================================================================
 * File-local (static) variables -- NOT moved to globals
 * ============================================================================ */

static progress_state_t g_progress_state;
static bool g_progress_enabled = false;

static WorkQueue<Int> g_workQueue;

/* env_truthy_kh: check environment variable for truthy value */
static inline bool env_truthy_kh(const char *name) {
    const char *v = getenv(name);
    if (!v || !*v) return false;
    if (v[0] == '0' && v[1] == '\0') return false;
    if ((v[0] == 'f' || v[0] == 'F') && (v[1] == 'a' || v[1] == 'A')) return false;
    if ((v[0] == 'n' || v[0] == 'N') && (v[1] == 'o' || v[1] == 'O')) return false;
    return true;
}

#ifndef _WIN64
static void configure_work_queue(size_t threadCount);
void shutdown_work_queue();
#endif
bool acquire_base_key(Int &key);

void sleep_ms(int milliseconds);

/* Vanity functions moved to cli.cpp (parse_cli_args) */

static gpu_multi_worker_t *g_multi_gpu_workers = NULL;
static volatile sig_atomic_t g_sigint_received = 0;

/* sigint_handler moved to gpu_dispatch.cpp (gpu_sigint_handler) */

void check_sigint_cleanup(void) {
    if (g_sigint_received && g_multi_gpu_workers != NULL) {
        output_info("\nReceived Ctrl+C, stopping multi-GPU workers...\n");
        gpu_worker_stop(g_multi_gpu_workers, 10000);
        g_multi_gpu_workers = NULL;
    }
}

/* Cleanup functions */
static void cleanup_general_resources(void) {
    if (addressTable != NULL) {
        free(addressTable);
        addressTable = NULL;
    }
}

static void cleanup_all_resources(void) {
    cleanup_general_resources();
    cleanup_bsgs_resources();
}

#ifndef _WIN64
static void configure_work_queue(size_t threadCount) {
    if (FLAGRANDOM) {
        g_workQueue.shutdown();
        return;
    }
    uint64_t chunk = N_SEQUENTIAL_MAX;
    size_t prefetch = std::max<size_t>(threadCount * 4, static_cast<size_t>(64));
    g_workQueue.configure(&n_range_start, &n_range_end, chunk, prefetch);
    g_workQueue.start();
}

void shutdown_work_queue() {
    g_workQueue.shutdown();
}
#endif

void maybe_adjust_cpu_sequential_max(size_t threadCount,
                                            Int &cpuStart,
                                            Int &rangeEnd,
                                            const char *env_override,
                                            const char *tag) {
    if (FLAG_N) return;
    if (threadCount == 0) return;
    uint64_t span = 0;
    if (!monitoring_span_u64_from_range(cpuStart, rangeEnd, span) || span == 0) return;

    {
        const uint64_t min_blocks = (uint64_t)threadCount * 4ULL;
        if (N_SEQUENTIAL_MAX > 0 && (span / N_SEQUENTIAL_MAX) >= min_blocks) return;
    }

    const char *env = (env_override && env_override[0]) ? getenv(env_override) : NULL;
    if (env && env[0]) {
        uint64_t forced = 0;
        if (env[0] == '0' && env[1] == 'x') {
            forced = strtoull(env + 2, NULL, 16);
        } else {
            forced = strtoull(env, NULL, 10);
        }
        if (forced >= 1024 && (forced % 1024ULL) == 0) {
            if (forced != N_SEQUENTIAL_MAX) {
                N_SEQUENTIAL_MAX = forced;
                output_info("%s: forced CPU N to 0x%llx via %s\n",
                       tag ? tag : "CPU",
                       (unsigned long long)N_SEQUENTIAL_MAX,
                       env_override ? env_override : "ENV");
            }
        }
        return;
    }

    const uint64_t blocks_per_thread = 16ULL;
    uint64_t desired = (span + (uint64_t)threadCount * blocks_per_thread - 1ULL) /
                       ((uint64_t)threadCount * blocks_per_thread);

    const uint64_t kAlign = 1024ULL;
    const uint64_t kMin = 8ULL * kAlign;
    const uint64_t kMax = 256ULL * 1024ULL * kAlign;

    if (desired < kMin) desired = kMin;
    if (desired > kMax) desired = kMax;
    desired = ((desired + kAlign - 1) / kAlign) * kAlign;

    if (desired < N_SEQUENTIAL_MAX) {
        N_SEQUENTIAL_MAX = desired;
        output_info("%s: adjusted CPU N to 0x%llx for better thread utilization\n",
               tag ? tag : "CPU",
               (unsigned long long)N_SEQUENTIAL_MAX);
    }
}

/* Thread-local block cache for work-stealing mode */
thread_local Int cpu_cached_block_start;
thread_local Int cpu_cached_block_end;
thread_local bool cpu_cached_block_valid = false;

bool acquire_base_key(Int &key) {
    if (g_work_pool.enabled.load(std::memory_order_acquire)) {
        for (;;) {
            if (!cpu_cached_block_valid || !cpu_cached_block_start.IsLower(&cpu_cached_block_end)) {
                if (!g_work_pool.get_block(cpu_cached_block_start, cpu_cached_block_end)) {
                    return false;
                }
                cpu_cached_block_valid = true;
            }
            if (!cpu_cached_block_start.IsLower(&cpu_cached_block_end)) {
                cpu_cached_block_valid = false;
                continue;
            }
            key.Set(&cpu_cached_block_start);
            cpu_cached_block_start.Add(N_SEQUENTIAL_MAX);
            return true;
        }
    }

#ifndef _WIN64
    if (!FLAGRANDOM && g_workQueue.enabled()) {
        return g_workQueue.pop(key);
    }
#endif
    if (FLAGRANDOM) {
        key.Rand(&n_range_start,&n_range_end);
        return true;
    }
    platform_mutex_lock(&write_random);
    bool hasWork = n_range_start.IsLower(&n_range_end);
    if(hasWork) {
        key.Set(&n_range_start);
        n_range_start.Add(N_SEQUENTIAL_MAX);
    }
    platform_mutex_unlock(&write_random);
    return hasWork;
}

/* ============================================================================
 * main()
 * ============================================================================ */

int main(int argc, char **argv) {
    char *hextemp = NULL;
    uint64_t i;
    int salir;
    Int int_aux,int_r,int_q,int58;

    platform_thread_t gpu_thread_id = 0;
    gpu_hybrid_args_t gpu_hybrid_args = {};
    int gpu_hybrid_started = 0;

    platform_mutex_init(&write_keys);
    platform_mutex_init(&write_random);
    platform_mutex_init(&bsgs_thread);

    srand(time(NULL));
    atexit(cleanup_all_resources);

    secp = new Secp256K1();
    secp->Init();
    OUTPUTSECONDS.SetInt32(30);
    ZERO.SetInt32(0);
    ONE.SetInt32(1);
    BSGS_GROUP_SIZE.SetInt32(CPU_GRP_SIZE);

#if defined(_WIN64) && !defined(__CYGWIN__)
    rseed(clock() + time(NULL) + thread_rand());
#else
    unsigned long rseedvalue;
    int bytes_read = getrandom(&rseedvalue, sizeof(unsigned long), GRND_NONBLOCK);
    if(bytes_read > 0) {
        rseed(rseedvalue);
    } else {
        output_warning("getrandom() failed (bytes_read=%d), using fallback RNG\n", bytes_read);
        rseed(clock() + time(NULL) + thread_rand() * thread_rand());
    }
#endif
    bool early_quiet = false;
    for (int qi = 1; qi < argc; qi++) {
        if (strcmp(argv[qi], "-q") == 0) {
            early_quiet = true;
            break;
        }
    }
    output_init(early_quiet ? OUTPUT_MINIMAL : OUTPUT_NORMAL);
    output_success("Version %s, developed by AlbertoBSD\n",version);

    g_profile_enabled = env_truthy_kh("KEYHUNT_PROFILE");
    if (g_profile_enabled) {
        output_info("Profiling enabled (KEYHUNT_PROFILE=1)\n");
    }

    keyhunt_config_t config;
    kh_config_init(&config);
    g_kh_config_ptr = &config;

    bsgs_context_t bsgs_ctx;
    memset(&bsgs_ctx, 0, sizeof(bsgs_ctx));

    if (getenv("KEYHUNT_SKIP_SYSINFO")) {
        output_warning("Skipping system detection (KEYHUNT_SKIP_SYSINFO set)\n");
        output_info("Using safe default parameters\n");
        memset(&g_sysinfo, 0, sizeof(g_sysinfo));
        g_sysinfo.cpu_physical_cores = 4;
        g_sysinfo.cpu_logical_cores = 8;
        g_sysinfo.cache_l1_size = 32;
        g_sysinfo.cache_l2_size = 256;
        g_sysinfo.cache_l3_size = 8192;
        g_sysinfo.ram_total = 8192;
        g_sysinfo.ram_available = 4096;
        g_sysinfo.has_avx2 = false;
        g_sysinfo.has_avx512 = false;
        g_sysinfo.has_sha_ni = false;
        g_sysinfo.recommended_threads = 8;
        g_sysinfo.recommended_batch_size = 1024;
        g_sysinfo.recommended_workload = 8192;
        g_sysinfo.recommended_n = 0x10000000000ULL;
        g_sysinfo.recommended_kfactor = 1024;
    } else {
        sysinfo_init(&g_sysinfo);
    }
    gpu_backend_init(&g_gpu_backend_info);

    g_avx2_available = ripemd160_avx2_available();
    if (g_avx2_available) {
        output_success("AVX2 detected: Using optimized 8-way parallel RIPEMD160\n");
    } else {
        output_info("AVX2 not available: Using SSE2 4-way parallel RIPEMD160\n");
    }
    bsgs_fast_set_cpu_features(g_avx2_available, false);

    OPTIMAL_THREADS = g_sysinfo.recommended_threads;
    OPTIMAL_N = g_sysinfo.recommended_n;
    OPTIMAL_KFACTOR = g_sysinfo.recommended_kfactor;
    CPU_GRP_SIZE = 1024;
    output_info("Using CPU_GRP_SIZE: %u (proven optimal)\n", CPU_GRP_SIZE);

    /* ========== CLI Parsing (early checks + config file + getopt + validation) ========== */
    parse_cli_args(argc, argv);

    /* ========== GPU Mode Resolution ========== */
    resolve_gpu_mode();

    /* ========== Range setup (extracted to cli.cpp) ========== */
    setup_search_range();

    /* ========== Config bridge (extracted to config/config.cpp) ========== */
    kh_config_bridge_from_globals(&config, g_fileName);

    N = 0;

    if(FLAGMODE != MODE_BSGS) {
        if(!FLAG_N && OPTIMAL_N > 0) {
            N_SEQUENTIAL_MAX = OPTIMAL_N;
            output_info("Using auto-tuned N value: 0x%llx\n", (unsigned long long)OPTIMAL_N);
        } else if(FLAG_N) {
            int base = 10;
            const char *num = str_N;
            if (num[0] == '0' && (num[1] == 'x' || num[1] == 'X')) base = 16;
            errno = 0;
            char *endp = NULL;
            unsigned long long parsed = strtoull(num, &endp, base);
            if (errno != 0 || endp == num || (endp && *endp != '\0')) {
                output_error("Invalid -n value: %s\n", str_N); FLAG_N = 0; N_SEQUENTIAL_MAX = 0x100000000;
            } else { N_SEQUENTIAL_MAX = (uint64_t)parsed; }
            if(N_SEQUENTIAL_MAX < 1024) { output_info("n value need to be equal or great than 1024, back to defaults\n"); FLAG_N = 0; N_SEQUENTIAL_MAX = 0x100000000; }
            if(N_SEQUENTIAL_MAX % 1024 != 0) { output_info("n value need to be multiplier of  1024\n"); FLAG_N = 0; N_SEQUENTIAL_MAX = 0x100000000; }
        } else { N_SEQUENTIAL_MAX = 0x100000000; }
        output_success("N = 0x%llx\n",(unsigned long long)N_SEQUENTIAL_MAX);

        if(FLAGMODE == MODE_MINIKEYS) {
            BSGS_N.SetInt32(DEBUGCOUNT);
            if(FLAGBASEMINIKEY) output_success("Base Minikey : %s\n",str_baseminikey);
            minikeyN = (char*) malloc(22);
            checkpointer((void *)minikeyN,__FILE__,"malloc","minikeyN",__LINE__-1);
            i = 0; int58.SetInt32(58); int_aux.SetInt64(N_SEQUENTIAL_MAX); int_aux.Mult(253);
            i = 20; salir = 0;
            do {
                if(!int_aux.IsZero()) {
                    int_r.Set(&int_aux); int_r.Mod(&int58); int_q.Set(&int_aux);
                    minikeyN[i] = (uint8_t)int_r.GetInt64();
                    int_q.Sub(&int_r); int_q.Div(&int58); int_aux.Set(&int_q); i--;
                } else { salir = 1; }
            } while(!salir && i > 0);
            minikey_n_limit = 21 - i;
        } else {
            if(FLAGBITRANGE) output_success("Bit Range %i\n",bitrange);
            else output_success("Range \n");
        }
        if(FLAGMODE != MODE_MINIKEYS) {
            hextemp = n_range_start.GetBase16(); output_success("-- from : 0x%s\n",hextemp); free(hextemp);
            if (FLAGRANGE) { Int end_inclusive; end_inclusive.Set(&n_range_end); end_inclusive.SubOne(); hextemp = end_inclusive.GetBase16(); }
            else { hextemp = n_range_end.GetBase16(); }
            output_success("-- to   : 0x%s\n",hextemp); free(hextemp);
        }

        monitoring_initialize_range_progress(n_range_start, n_range_end, FLAGPROGRESSBAR, FLAGMODE);
        config.runtime.range_progress_start = (void *)monitoring_range_progress_start();
        config.runtime.range_progress_end = (void *)monitoring_range_progress_end();

        if (!FLAGGPU_HYBRID && !g_work_pool.enabled && !FLAGRANDOM &&
            (FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_XPOINT || FLAGMODE == MODE_VANITY)) {
            maybe_adjust_cpu_sequential_max((size_t)NTHREADS, n_range_start, n_range_end, "KEYHUNT_CPU_N", "CPU");
        }

        /* Wire I/O state into config BEFORE readFile calls */
        config.runtime.bloom_filter = (void *)&bloom;
        config.runtime.address_table = (void *)addressTable;
        config.runtime.address_count = (int64_t)N;
        config.runtime.io_read_cached = FLAGREADEDFILE1;
        config.runtime.max_address_length = MAXLENGTHADDRESS;
        config.runtime.vanity_targets = vanity_rmd_targets;
        config.runtime.vanity_total = vanity_rmd_total;
        config.runtime.vanity_bloom = (void *)vanity_bloom;
        config.runtime.vanity_limits = (void *)vanity_rmd_limits;
        config.runtime.vanity_values_a = (void *)vanity_rmd_limit_values_A;
        config.runtime.vanity_values_b = (void *)vanity_rmd_limit_values_B;
        config.runtime.vanity_min_check_len = vanity_rmd_minimun_bytes_check_length;
        config.runtime.vanity_addresses = (void *)vanity_address_targets;

        switch(FLAGMODE) {
            case MODE_MINIKEYS: case MODE_RMD160: case MODE_ADDRESS: case MODE_XPOINT:
                if(!readFileAddress(g_fileName, &config)) { output_error("Unexpected error\n"); exit(EXIT_FAILURE); }
                break;
            case MODE_VANITY:
                if(!readFileVanity(g_fileName, &config)) { output_error("Unexpected error\n"); exit(EXIT_FAILURE); }
                break;
        }

        N = (uint64_t)config.runtime.address_count;
        addressTable = (struct address_value *)config.runtime.address_table;
        FLAGREADEDFILE1 = config.runtime.io_read_cached;
        MAXLENGTHADDRESS = config.runtime.max_address_length;

        if(FLAGMODE != MODE_VANITY && !FLAGREADEDFILE1) {
            output_success("Sorting data ..."); kh_sort(addressTable,N);
            printf(" done! %" PRIu64 " values were loaded and sorted\n",N);
            config.runtime.address_table = (void *)addressTable;
            config.runtime.address_count = (int64_t)N;
            writeFileIfNeeded(g_fileName, &config);
            FLAGREADEDFILE1 = config.runtime.io_read_cached;
        }

        /* GPU Full Search initialization */
        if (FLAGGPU_FULL == 1) {
            output_success("Initializing GPU full search...\n");
            if (gpu_dispatch_upload_gtable() == 0) output_success("G table uploaded to GPU (8192 points)\n");
            else { error_report_t report; error_gpu_init_failed("GPU","Failed to upload precomputed G table",&report); error_print(&report); FLAGGPU_FULL = 0; FLAGGPU = 0; }
            if (FLAGGPU_FULL && gpu_dispatch_upload_targets((void *)addressTable, N) == 0) {
                output_success("Targets uploaded to GPU (%" PRIu64 " hashes)\n", N);
                if (N > 32) {
                    if (gpu_dispatch_upload_bloom((void *)addressTable, N) == 0) output_success("GPU bloom uploaded\n");
                    else output_warning("GPU bloom upload failed; continuing without GPU bloom\n");
                }
            } else if (FLAGGPU_FULL) {
                error_report_t report; char details[256];
                snprintf(details, sizeof(details), "Failed to upload %" PRIu64 " target hashes to GPU", N);
                error_gpu_init_failed("GPU", details, &report); error_print(&report);
                FLAGGPU_FULL = 0; FLAGGPU = 0;
            }
        }
    }

    /* ========== Progress tracking init ========== */
    if (progress_init() != 0) {
        output_warning("Failed to initialize progress system. Progress will NOT be saved.\n");
    } else {
        char *range_start_hex = n_range_start.GetBase16();
        char *range_end_hex = n_range_end.GetBase16();
        int create_result = progress_create(&g_progress_state, get_mode_name(FLAGMODE),
                            g_fileName, bitrange,
                            range_start_hex ? range_start_hex : "0",
                            range_end_hex ? range_end_hex : "0");
        if (create_result != 0) output_warning("Failed to create progress file.\n");
        else { g_progress_enabled = true; g_progress_state.is_random_mode = (FLAGRANDOM != 0); g_progress_state.thread_count = NTHREADS; output_info("Progress tracking enabled (auto-saves every 60s)\n"); }
        if (range_start_hex) free(range_start_hex);
        if (range_end_hex) free(range_end_hex);
    }

    /* ========== Mode dispatch ========== */
    if(FLAGMODE == MODE_BSGS) {
        config.runtime.bsgs_context = (void *)&bsgs_ctx;
        int bsgs_rc = mode_dispatch(&config, NULL, 0);
        if (bsgs_rc != 0) { output_error("BSGS mode initialization failed\n"); exit(EXIT_FAILURE); }
    }

    if(FLAGMODE != MODE_BSGS) {
        monitoring_initialize_rate_limits();
        if (!FLAGTHREADS && NTHREADS == 1 && OPTIMAL_THREADS > 0) {
            NTHREADS = OPTIMAL_THREADS;
            output_info("Using auto-tuned thread count: %d\n", NTHREADS);
        }
        steps = (struct thread_counter *) aligned_calloc(64, NTHREADS, sizeof(struct thread_counter));
        checkpointer((void *)steps,__FILE__,"aligned_calloc","steps",__LINE__-1);
        ends = (struct thread_flag *) aligned_calloc(64, NTHREADS, sizeof(struct thread_flag));
        checkpointer((void *)ends,__FILE__,"aligned_calloc","ends",__LINE__-1);
        tid = (platform_thread_t *) calloc(NTHREADS, sizeof(platform_thread_t));
        checkpointer((void *)tid,__FILE__,"calloc","tid",__LINE__-1);
#ifndef _WIN64
        shutdown_work_queue();
#endif

        /* GPU Full Search Mode */
        if (FLAGGPU_FULL && !FLAGGPU_HYBRID && (FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160)) {
            int rc = run_gpu_full_search_mode(&config, &g_multi_gpu_workers);
            if (rc == 0) return 0;  /* GPU search completed successfully */
        }

        /* GPU Hybrid Mode */
        if (FLAGGPU_HYBRID.load(std::memory_order_relaxed) && (FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_RMD160)) {
            run_gpu_hybrid_setup(&config, &gpu_thread_id, &gpu_hybrid_args, &gpu_hybrid_started);
        }

        /* CPU Thread Mode */
#ifndef _WIN64
        if (g_work_pool.enabled) { shutdown_work_queue(); }
        else if(FLAGMODE == MODE_ADDRESS || FLAGMODE == MODE_XPOINT || FLAGMODE == MODE_RMD160 || FLAGMODE == MODE_VANITY) { configure_work_queue((size_t)NTHREADS); }
        else { shutdown_work_queue(); }
#endif
        config.runtime.num_threads = NTHREADS;
        config.runtime.bloom_filter = (void *)&bloom;
        config.runtime.address_table = (void *)addressTable;
        config.runtime.address_count = (int64_t)N;
        config.runtime.write_mutex = (void *)&write_keys;
        config.runtime.random_mutex = (void *)&write_random;
        config.runtime.thread_counters = (void *)steps;
        config.runtime.thread_flags = (void *)ends;
        config.runtime.thread_output = (void *)&THREADOUTPUT;
        config.runtime.sequential_max = N_SEQUENTIAL_MAX;
        config.gpu.enabled = FLAGGPU;
        config.gpu.full_mode = (FLAGGPU_FULL != 0);
        config.gpu.hybrid_mode = (FLAGGPU_HYBRID.load(std::memory_order_relaxed) != 0);
        config.autotune.has_avx2 = g_avx2_available;
        config.runtime.minikey_coinbuffer = (void *)Ccoinbuffer;
        config.runtime.minikey_raw_base = (void *)raw_baseminikey;
        config.runtime.minikey_n = (void *)minikeyN;
        config.runtime.minikey_n_limit = minikey_n_limit;
        profile_init_threads((int)NTHREADS);

        { int rc = mode_dispatch(&config, tid, (int)NTHREADS); if (rc != 0) { output_error("mode_dispatch failed for mode %d\n", FLAGMODE); exit(EXIT_FAILURE); } }
    }

    /* ========== Monitoring loop (init extracted to monitoring.cpp) ========== */
    monitoring_params_t mon_params;
    init_monitoring_params(&mon_params, &config, gpu_hybrid_started, gpu_thread_id,
                           (void *)&gpu_hybrid_args, (void *)&g_progress_state,
                           g_progress_enabled, (void *)g_multi_gpu_workers);
    run_monitoring_loop(&mon_params);

    /* ========== Post-monitoring cleanup ========== */
    if (FLAGGPU_HYBRID && gpu_hybrid_started) {
        g_gpu_should_stop.store(1, std::memory_order_release);
        printf("\n[+] Waiting for GPU thread to complete...\n");
        platform_thread_join(gpu_thread_id, NULL);
        output_success("GPU thread finished. Result: %d keys found\n", gpu_hybrid_args.result.load(std::memory_order_acquire));
        output_success("GPU keys checked: %" PRIu64 "\n", monitoring_gpu_keys_checked_total_u64());
        if (g_adaptive_scheduler.initialized) {
            double cpu_mkeys = 0.0, gpu_mkeys = 0.0; int cpu_pct = 0, gpu_pct = 0;
            adaptive_get_stats(&cpu_mkeys, &gpu_mkeys, &cpu_pct, &gpu_pct);
            output_success("Final adaptive stats: CPU=%.1f Mkeys/s (%d%%), GPU=%.1f Mkeys/s (%d%%)\n", cpu_mkeys, cpu_pct, gpu_mkeys, gpu_pct);
            output_success("Optimal ratio for next run: CPU=%d%%, GPU=%d%%\n", cpu_pct, gpu_pct);
        }
        adaptive_cleanup();
        gpu_backend_shutdown();
        if (g_work_pool.enabled) g_work_pool.disable();
    }

    if (g_progress_enabled) { progress_complete(&g_progress_state); output_info("Progress tracking completed\n"); }
    printf("\nEnd\n");
#ifndef _WIN64
    shutdown_work_queue();
#endif
    platform_mutex_destroy(&write_keys);
    platform_mutex_destroy(&write_random);
    platform_mutex_destroy(&bsgs_thread);
}

void init_generator() {
    Point G = secp->ComputePublicKey(&stride);
    Point g;
    g.Set(G);
    Gn.resize(CPU_GRP_SIZE / 2);
    Gn[0] = g;
    g = secp->DoubleDirect(g);
    Gn[1] = g;
    for(size_t i = 2; i < CPU_GRP_SIZE / 2; i++) {
        g = secp->AddDirect(g,G);
        Gn[i] = g;
    }
    _2Gn = secp->DoubleDirect(Gn[CPU_GRP_SIZE / 2 - 1]);
}

/* Functions extracted to other modules:
 * - menu() -> src/cli.cpp (Phase 4, Plan 08)
 * - Monitoring loop -> src/monitoring/monitoring.cpp (Phase 4, Plan 08)
 * - gpu_selftest_hash160_fromX -> src/gpu/gpu_dispatch.cpp (Phase 4, Plan 08)
 * - hybrid_get_gpu_range_percent_default -> src/gpu/gpu_dispatch.cpp (Phase 4, Plan 08)
 * - format_keys_per_second, progress tracking -> src/monitoring/monitoring.cpp (Phase 4, Plan 08)
 * - GPU dispatch functions -> src/gpu/gpu_dispatch.cpp (Phase 4, Plan 05)
 * - writekey, writekeyeth, checkpointer -> io/io.cpp
 * - readFileAddress, readFileVanity -> io/io.cpp
 * - vanityrmdmatch, addvanity -> search/search_vanity.cpp
 * - sort functions -> sort/sort.cpp
 * - BSGS sort/search -> bsgs/bsgs_sort.cpp
 * - BSGS threads -> search/search_bsgs_threads.cpp
 * - address_util functions -> crypto/address_util.cpp
 * - bloom init -> crypto/bloom_init.cpp
 * - calcualteindex -> search/search_bsgs.cpp
 * - sleep_ms -> util/thread_util.cpp
 */
