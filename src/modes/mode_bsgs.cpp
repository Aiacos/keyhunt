/*
 * mode_bsgs.cpp - BSGS (Baby Step Giant Step) mode dispatcher
 *
 * Handles BSGS mode initialization (bloom filter allocation, bP table
 * computation, bPload thread spawning, bsgs_context_t population) and
 * thread dispatch for 5 BSGS search modes (sequential, backward, both,
 * random, dance).
 *
 * Extracted from keyhunt.cpp during Phase 4 monolith decomposition.
 *
 * MIGRATION STATUS: Phase 4 extraction from keyhunt.cpp
 */

/* Include bsgs_sort.h FIRST to define struct bsgs_xvalue and BSGS_SORT_H,
 * so search_common.h's conditional definition is skipped. */
#include "../bsgs/bsgs_sort.h"
#include "modes.h"
#include "../search/search_common.h"
#include "../output.h"
#include "../io/io.h"
#include "../core/util.h"
#include "../core/sysinfo.h"
#include "../bloom/bloom.h"
#include "../bloom/bloom_wrapper.h"
#include "../hash/sha256.h"
#include "../util/thread_util.h"
#include "../util/profiling.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cinttypes>
#include <atomic>
#include <vector>

#if defined(_WIN64) && !defined(__CYGWIN__)
#include <malloc.h>
#endif

/* ============================================================================
 * Extern declarations for globals defined in keyhunt.cpp
 *
 * These remain in keyhunt.cpp until Phase 4 completes full global absorption.
 * mode_bsgs.cpp references them via extern to avoid duplication.
 * ============================================================================ */

/* BSGS Int parameters */
extern Int BSGS_GROUP_SIZE;
extern Int BSGS_CURRENT;
extern Int BSGS_R;
extern Int BSGS_AUX;
extern Int BSGS_N;
extern Int BSGS_N_double;
extern Int BSGS_M;
extern Int BSGS_M_double;
extern Int BSGS_M2;
extern Int BSGS_M2_double;
extern Int BSGS_M3;
extern Int BSGS_M3_double;

/* BSGS Point parameters */
extern Point BSGS_P;
extern Point BSGS_MP;
extern Point BSGS_MP2;
extern Point BSGS_MP3;
extern Point BSGS_MP_double;
extern Point BSGS_MP2_double;
extern Point BSGS_MP3_double;

/* BSGS vectors */
extern std::vector<Point> BSGS_AMP2;
extern std::vector<Point> BSGS_AMP3;
extern std::vector<Point> GSn;
extern Point _2GSn;

/* Target points */
extern std::vector<Point> OriginalPointsBSGS;
extern bool *OriginalPointsBSGScompressed;
extern std::atomic<int> *bsgs_found;

/* BSGS bloom filters and tables */
extern bloom_extended_t *bloom_bP;
extern bloom_extended_t *bloom_bPx2nd;
extern bloom_extended_t *bloom_bPx3rd;
extern struct bsgs_xvalue *bPtable;

/* Bloom checksums */
struct checksumsha256 {
    char data[32];
    char backup[32];
};
extern struct checksumsha256 *bloom_bP_checksums;
extern struct checksumsha256 *bloom_bPx2nd_checksums;
extern struct checksumsha256 *bloom_bPx3rd_checksums;

/* Bloom mutexes */
extern platform_mutex_t *bloom_bP_mutex;
extern platform_mutex_t *bloom_bPx2nd_mutex;
extern platform_mutex_t *bloom_bPx3rd_mutex;
extern platform_mutex_t *bPload_mutex;
extern platform_mutex_t bsgs_thread;

/* BSGS scalar parameters */
extern uint64_t BSGS_XVALUE_RAM;
extern uint64_t BSGS_BUFFERXPOINTLENGTH;
extern uint64_t BSGS_BUFFERREGISTERLENGTH;
extern uint64_t bloom_bP_totalbytes;
extern uint64_t bloom_bP2_totalbytes;
extern uint64_t bloom_bP3_totalbytes;
extern uint64_t bsgs_m;
extern uint64_t bsgs_m2;
extern uint64_t bsgs_m3;
extern uint64_t bsgs_aux;
extern uint32_t bsgs_point_number;
extern uint64_t bytes;
extern char checksum[32], checksum_backup[32];
extern char buffer_bloom_file[1024];

/* Range variables */
extern Int n_range_start;
extern Int n_range_end;
extern Int n_range_diff;
extern Int n_range_aux;

/* Search config flags */
extern int FLAGMODE;
extern int FLAGBSGSMODE;
extern int FLAGSAVEREADFILE;
extern int FLAGREADEDFILE1;
extern int FLAGREADEDFILE2;
extern int FLAGREADEDFILE3;
extern int FLAGREADEDFILE4;
extern int FLAGUPDATEFILE1;
extern int FLAGSKIPCHECKSUM;
extern int FLAGRANGE;
extern int FLAGBITRANGE;
extern int FLAG_N;
extern int FLAGTHREADS;
extern int KFACTOR;
extern int NTHREADS;
extern int OPTIMAL_THREADS;
extern int OPTIMAL_KFACTOR;
extern uint64_t OPTIMAL_N;
extern char *str_N;
extern char *range_start;
extern char *range_end;
extern int bitrange;
extern char *bit_range_str_min;
extern char *bit_range_str_max;

/* Thread runtime state */
extern uint64_t FINISHED_THREADS_COUNTER;
extern uint64_t FINISHED_THREADS_BP;
extern uint64_t THREADCYCLES;
extern uint64_t THREADCOUNTER;
extern std::atomic<uint64_t> FINISHED_ITEMS;
extern uint64_t OLDFINISHED_ITEMS;
extern uint32_t THREADBPWORKLOAD;
extern uint32_t CPU_GRP_SIZE;

/* Misc globals */
extern uint64_t N;
extern struct thread_counter *steps;
extern struct thread_flag *ends;
extern platform_thread_t *tid;
extern Secp256K1 *secp;
extern system_info_t g_sysinfo;
extern Point point_temp;
extern struct address_value *addressTable;
extern platform_mutex_t write_keys;
extern platform_mutex_t write_random;
extern std::atomic<int> THREADOUTPUT;
extern uint64_t N_SEQUENTIAL_MAX;
extern bloom_extended_t bloom;
extern Int OUTPUTSECONDS;
extern Int ONE;
extern Int ZERO;
extern bool g_avx2_available;
extern const char *bsgs_modes[5];

/* bPload struct (defined in keyhunt.cpp) */
struct bPload {
    uint32_t threadid;
    uint64_t from;
    uint64_t to;
    uint64_t counter;
    uint64_t workload;
    uint32_t aux;
    uint32_t finished;
};

/* bPload thread declarations */
platform_thread_return_t PLATFORM_THREAD_CALL thread_bPload(void *vargp);
platform_thread_return_t PLATFORM_THREAD_CALL thread_bPload_2blooms(void *vargp);

#ifndef _WIN64
extern void shutdown_work_queue();
#endif

/* ============================================================================
 * mode_bsgs_init - BSGS mode initialization
 *
 * Performs all BSGS-specific setup:
 * 1. Read target public keys from file
 * 2. Calculate BSGS parameters (N, M, K, bloom sizes)
 * 3. Memory validation and auto-adjustment
 * 4. Allocate bloom filters (3 tiers) and bP table
 * 5. Compute generator points (GSn, _2GSn, AMP2, AMP3)
 * 6. Spawn bPload threads to populate bloom filters + bP table
 * 7. Compute/verify checksums
 * 8. Write bloom filter cache files
 * 9. Populate bsgs_context_t and config bridge
 * ============================================================================ */
static int mode_bsgs_init(keyhunt_config_t *config) {
    /* Local variables for BSGS initialization */
    char *aux = NULL;
    char *aux2 = NULL;
    char *pointx_str = NULL;
    char *pointy_str = NULL;
    char *hextemp = NULL;
    char rawvalue[32];
    char *bPload_threads_available;
    FILE *fd, *fd_aux1, *fd_aux2, *fd_aux3;
    uint64_t i, BASE, PERTHREAD_R, itemsbloom, itemsbloom2, itemsbloom3;
    uint32_t finished;
    int readed, salir, j;
    struct bPload *bPload_temp_ptr;
    size_t rsize;
    int s;
    Tokenizer tokenizerbsgs{};
    const char *fileName = config->search.target_file;

    /* ---- Step 1: Read target public keys ---- */
    output_success("Opening file %s\n", fileName);
    fd = fopen(fileName, "rb");
    if (fd == NULL) {
        output_error("Can't open file %s\n", fileName);
        return -1;
    }
    aux = (char *)malloc(1024);
    checkpointer((void *)aux, __FILE__, "malloc", "aux", __LINE__ - 1);
    while (fgets(aux, 1022, fd) != NULL) {
        trim(aux, " \t\n\r");
        if (strlen(aux) >= 128) {
            N++;
        } else {
            if (strlen(aux) >= 66) {
                N++;
            }
        }
    }
    if (N == 0) {
        output_error("There is no valid data in the file\n");
        free(aux);
        fclose(fd);
        return -1;
    }
    bsgs_found = new std::atomic<int>[N]{};
    OriginalPointsBSGS.resize(N);
    OriginalPointsBSGScompressed = (bool *)malloc(N * sizeof(bool));
    checkpointer((void *)OriginalPointsBSGScompressed, __FILE__, "malloc", "OriginalPointsBSGScompressed", __LINE__ - 1);
    pointx_str = (char *)malloc(65);
    checkpointer((void *)pointx_str, __FILE__, "malloc", "pointx_str", __LINE__ - 1);
    pointy_str = (char *)malloc(65);
    checkpointer((void *)pointy_str, __FILE__, "malloc", "pointy_str", __LINE__ - 1);
    fseek(fd, 0, SEEK_SET);
    i = 0;
    while (fgets(aux, 1022, fd) != NULL) {
        trim(aux, " \t\n\r");
        if (strlen(aux) >= 66) {
            stringtokenizer(aux, &tokenizerbsgs);
            aux2 = nextToken(&tokenizerbsgs);
            memset(pointx_str, 0, 65);
            memset(pointy_str, 0, 65);
            switch (strlen(aux2)) {
                case 66:
                    if (secp->ParsePublicKeyHex(aux2, OriginalPointsBSGS[i], OriginalPointsBSGScompressed[i])) {
                        i++;
                    } else {
                        N--;
                    }
                    break;
                case 130:
                    if (secp->ParsePublicKeyHex(aux2, OriginalPointsBSGS[i], OriginalPointsBSGScompressed[i])) {
                        i++;
                    } else {
                        N--;
                    }
                    break;
                default:
                    printf("Invalid length: %s\n", aux2);
                    N--;
                    break;
            }
            freetokenizer(&tokenizerbsgs);
        }
    }
    fclose(fd);
    bsgs_point_number = N;
    if (bsgs_point_number > 0) {
        output_success("Added %u points from file\n", bsgs_point_number);
    } else {
        output_error("The file don't have any valid publickeys\n");
        free(aux);
        free(pointx_str);
        free(pointy_str);
        return -1;
    }

    /* ---- Step 2: Calculate BSGS parameters ---- */
    BSGS_N.SetInt32(0);
    BSGS_M.SetInt32(0);
    BSGS_M.SetInt64(bsgs_m);

    if (!FLAG_N && OPTIMAL_N > 0) {
        BSGS_N.SetInt64(OPTIMAL_N);
        output_info("Using auto-tuned N value: 0x%llx\n", (unsigned long long)OPTIMAL_N);
    } else if (FLAG_N) {
        if (str_N[0] == '0' && str_N[1] == 'x') {
            BSGS_N.SetBase16((char *)(str_N + 2));
        } else {
            BSGS_N.SetBase10(str_N);
        }
    } else {
        BSGS_N.SetInt64((uint64_t)0x100000000000);
    }

    if (KFACTOR == 1 && OPTIMAL_KFACTOR > 0) {
        KFACTOR = OPTIMAL_KFACTOR;
        output_info("Using auto-tuned K factor: %d\n", OPTIMAL_KFACTOR);
    }

bsgs_recalculate_with_new_params:
    if (BSGS_N.HasSqrt()) {
        BSGS_M.Set(&BSGS_N);
        BSGS_M.ModSqrt();
    } else {
        output_error("-n param doesn't have exact square root\n");
        free(aux);
        free(pointx_str);
        free(pointy_str);
        return -1;
    }

    BSGS_AUX.Set(&BSGS_M);
    BSGS_AUX.Mod(&BSGS_GROUP_SIZE);

    if (!BSGS_AUX.IsZero()) {
        hextemp = BSGS_GROUP_SIZE.GetBase10();
        output_error("M value is not divisible by %s\n", hextemp);
        free(hextemp);
        free(aux);
        free(pointx_str);
        free(pointy_str);
        return -1;
    }

    bsgs_m = BSGS_M.GetInt64();

    if (FLAGRANGE || FLAGBITRANGE) {
        if (FLAGBITRANGE) {
            n_range_start.SetBase16(bit_range_str_min);
            n_range_end.SetBase16(bit_range_str_max);
            n_range_diff.Set(&n_range_end);
            n_range_diff.Sub(&n_range_start);
            output_success("Bit Range %i\n", bitrange);
            output_success("-- from : 0x%s\n", bit_range_str_min);
            output_success("-- to   : 0x%s\n", bit_range_str_max);
        } else {
            output_success("Range \n");
            output_success("-- from : 0x%s\n", range_start);
            output_success("-- to   : 0x%s\n", range_end);
        }
    } else {
        n_range_start.SetInt32(1);
        n_range_end.Set(&secp->order);
        n_range_diff.Rand(&n_range_start, &n_range_end);
        n_range_start.Set(&n_range_diff);
    }
    BSGS_CURRENT.Set(&n_range_start);

    if (n_range_diff.IsLower(&BSGS_N)) {
        output_error("the given range is small\n");
        free(aux);
        free(pointx_str);
        free(pointy_str);
        return -1;
    }

    BSGS_M.Mult((uint64_t)KFACTOR);
    BSGS_AUX.SetInt32(32);
    BSGS_R.Set(&BSGS_M);
    BSGS_R.Mod(&BSGS_AUX);
    BSGS_M2.Set(&BSGS_M);
    BSGS_M2.Div(&BSGS_AUX);

    if (!BSGS_R.IsZero()) {
        BSGS_M2.AddOne();
    }

    BSGS_M_double.SetInt32(2);
    BSGS_M_double.Mult(&BSGS_M);

    BSGS_M2_double.SetInt32(2);
    BSGS_M2_double.Mult(&BSGS_M2);

    BSGS_R.Set(&BSGS_M2);
    BSGS_R.Mod(&BSGS_AUX);

    BSGS_M3.Set(&BSGS_M2);
    BSGS_M3.Div(&BSGS_AUX);

    if (!BSGS_R.IsZero()) {
        BSGS_M3.AddOne();
    }

    BSGS_M3_double.SetInt32(2);
    BSGS_M3_double.Mult(&BSGS_M3);

    bsgs_m2 = BSGS_M2.GetInt64();
    bsgs_m3 = BSGS_M3.GetInt64();

    BSGS_AUX.Set(&BSGS_N);
    BSGS_AUX.Div(&BSGS_M);

    BSGS_R.Set(&BSGS_N);
    BSGS_R.Mod(&BSGS_M);

    if (!BSGS_R.IsZero()) {
        BSGS_N.Set(&BSGS_M);
        BSGS_N.Mult(&BSGS_AUX);
    }

    bsgs_m = BSGS_M.GetInt64();
    bsgs_aux = BSGS_AUX.GetInt64();

    BSGS_N_double.SetInt32(2);
    BSGS_N_double.Mult(&BSGS_N);

    hextemp = BSGS_N.GetBase16();
    output_success("N = 0x%s\n", hextemp);
    free(hextemp);

    /* Bloom item counts */
    if (((uint64_t)(bsgs_m / 256)) > 10000) {
        itemsbloom = (uint64_t)(bsgs_m / 256);
        if (bsgs_m % 256 != 0) {
            itemsbloom++;
        }
    } else {
        itemsbloom = 1000;
    }

    if (((uint64_t)(bsgs_m2 / 256)) > 1000) {
        itemsbloom2 = (uint64_t)(bsgs_m2 / 256);
        if (bsgs_m2 % 256 != 0) {
            itemsbloom2++;
        }
    } else {
        itemsbloom2 = 1000;
    }

    if (((uint64_t)(bsgs_m3 / 256)) > 1000) {
        itemsbloom3 = (uint64_t)(bsgs_m3 / 256);
        if (bsgs_m3 % 256 != 0) {
            itemsbloom3++;
        }
    } else {
        itemsbloom3 = 1000;
    }

    /* ---- Step 3: Memory validation ---- */
    {
        uint64_t bloom1_bytes = (uint64_t)((double)bsgs_m * 3.5);
        uint64_t bloom2_bytes = (uint64_t)((double)bsgs_m2 * 3.5);
        uint64_t bloom3_bytes = (uint64_t)((double)bsgs_m3 * 3.5);
        uint64_t bp_table_bytes = bsgs_m2 * 16;

        uint64_t total_required_mb = (bloom1_bytes + bloom2_bytes + bloom3_bytes + bp_table_bytes) / (1024 * 1024);
        uint64_t available_ram_mb = g_sysinfo.ram_available;
        uint64_t safe_limit_mb = (available_ram_mb * 80) / 100;

        if (total_required_mb > safe_limit_mb) {
            fprintf(stderr, "\n");
            output_warning("========================================================\n");
            output_warning("INSUFFICIENT MEMORY FOR BSGS PARAMETERS\n");
            output_warning("========================================================\n");
            output_warning("Required RAM:  %" PRIu64 " MB (~%.1f GB)\n", total_required_mb, (double)total_required_mb / 1024);
            output_warning("Available RAM: %" PRIu64 " MB (~%.1f GB)\n", available_ram_mb, (double)available_ram_mb / 1024);
            output_warning("Safe limit:    %" PRIu64 " MB (80%% of available)\n", safe_limit_mb);
            output_warning("\n");
            output_warning("Current parameters:\n");
            output_warning("  N = 0x%" PRIx64 "\n", BSGS_N.GetInt64());
            output_warning("  K = %i\n", KFACTOR);
            output_warning("  M = %" PRIu64 " (sqrt(N))\n", bsgs_m / KFACTOR);
            output_warning("  M * K = %" PRIu64 " elements\n", bsgs_m);
            output_warning("\n");
            output_warning("AUTO-ADJUSTING PARAMETERS...\n");
            output_warning("--------------------------------------------------------\n");

            struct {
                uint64_t n;
                int k;
            } suggestions[] = {
                {0x40000000000ULL, 2048},
                {0x10000000000ULL, 2048},
                {0x10000000000ULL, 1024},
                {0x4000000000ULL, 1024},
            };

            uint64_t new_n = 0;
            int new_k = 0;

            for (int si = 0; si < 4; si++) {
                uint64_t test_m = (uint64_t)sqrt((double)suggestions[si].n);
                uint64_t test_mk = test_m * suggestions[si].k;
                uint64_t test_bloom1 = (uint64_t)((double)test_mk * 3.5);
                uint64_t test_bloom2 = test_bloom1 / 32;
                uint64_t test_bloom3 = test_bloom1 / 1024;
                uint64_t test_bp = (test_mk / 32) * 16;
                uint64_t test_total_mb = (test_bloom1 + test_bloom2 + test_bloom3 + test_bp) / (1024 * 1024);

                if (test_total_mb <= safe_limit_mb) {
                    new_n = suggestions[si].n;
                    new_k = suggestions[si].k;
                    output_info("Auto-adjusted to: N = 0x%" PRIx64 ", K = %d\n", new_n, new_k);
                    output_info("New RAM requirement: %" PRIu64 " MB (~%.1f GB)\n",
                                test_total_mb, (double)test_total_mb / 1024);
                    break;
                }
            }

            if (new_n == 0) {
                output_error("ERROR: Insufficient RAM even for minimum configuration\n");
                output_error("Minimum requires: ~1.8 GB, Available: %" PRIu64 " MB\n", available_ram_mb);
                output_error("Cannot continue.\n");
                output_error("========================================================\n");
                free(aux);
                free(pointx_str);
                free(pointy_str);
                return -1;
            }

            KFACTOR = new_k;
            BSGS_N.SetInt64(new_n);
            output_info("Recalculating with optimized parameters...\n");
            output_warning("========================================================\n\n");
            goto bsgs_recalculate_with_new_params;
        } else {
            output_info("Memory check: %" PRIu64 " MB required, %" PRIu64 " MB available (%.1f%% used)\n",
                        total_required_mb, available_ram_mb,
                        (double)total_required_mb * 100.0 / (double)available_ram_mb);
        }
    }

    /* ---- Step 4: Allocate bloom filters ---- */
    output_success("Bloom filter for %" PRIu64 " elements ", bsgs_m);
    bloom_bP = (bloom_extended_t *)calloc(256, sizeof(bloom_extended_t));
    checkpointer((void *)bloom_bP, __FILE__, "calloc", "bloom_bP", __LINE__ - 1);
    bloom_bP_checksums = (struct checksumsha256 *)calloc(256, sizeof(struct checksumsha256));
    checkpointer((void *)bloom_bP_checksums, __FILE__, "calloc", "bloom_bP_checksums", __LINE__ - 1);
    bloom_bP_mutex = (platform_mutex_t *)calloc(256, sizeof(platform_mutex_t));
    checkpointer((void *)bloom_bP_mutex, __FILE__, "calloc", "bloom_bP_mutex", __LINE__ - 1);

    fflush(stdout);
    bloom_bP_totalbytes = 0;
    for (i = 0; i < 256; i++) {
        platform_mutex_init(&bloom_bP_mutex[i]);
        if (bloom_ext_init(&bloom_bP[i], itemsbloom, 0.000001) != 0) {
            output_error("error bloom_init _ [%" PRIu64 "]\n", i);
            free(aux); free(pointx_str); free(pointy_str);
            return -1;
        }
        bloom_bP_totalbytes += bloom_ext_bytes(&bloom_bP[i]);
    }
    printf(": %.2f MB\n", (float)((float)(uint64_t)bloom_bP_totalbytes / (float)(uint64_t)1048576));

    output_success("Bloom filter for %" PRIu64 " elements ", bsgs_m2);
    bloom_bPx2nd_mutex = (platform_mutex_t *)calloc(256, sizeof(platform_mutex_t));
    checkpointer((void *)bloom_bPx2nd_mutex, __FILE__, "calloc", "bloom_bPx2nd_mutex", __LINE__ - 1);
    bloom_bPx2nd = (bloom_extended_t *)calloc(256, sizeof(bloom_extended_t));
    checkpointer((void *)bloom_bPx2nd, __FILE__, "calloc", "bloom_bPx2nd", __LINE__ - 1);
    bloom_bPx2nd_checksums = (struct checksumsha256 *)calloc(256, sizeof(struct checksumsha256));
    checkpointer((void *)bloom_bPx2nd_checksums, __FILE__, "calloc", "bloom_bPx2nd_checksums", __LINE__ - 1);
    bloom_bP2_totalbytes = 0;
    for (i = 0; i < 256; i++) {
        platform_mutex_init(&bloom_bPx2nd_mutex[i]);
        if (bloom_ext_init(&bloom_bPx2nd[i], itemsbloom2, 0.000001) != 0) {
            output_error("error bloom_init _ [%" PRIu64 "]\n", i);
            free(aux); free(pointx_str); free(pointy_str);
            return -1;
        }
        bloom_bP2_totalbytes += bloom_ext_bytes(&bloom_bPx2nd[i]);
    }
    printf(": %.2f MB\n", (float)((float)(uint64_t)bloom_bP2_totalbytes / (float)(uint64_t)1048576));

    bloom_bPx3rd_mutex = (platform_mutex_t *)calloc(256, sizeof(platform_mutex_t));
    checkpointer((void *)bloom_bPx3rd_mutex, __FILE__, "calloc", "bloom_bPx3rd_mutex", __LINE__ - 1);
    bloom_bPx3rd = (bloom_extended_t *)calloc(256, sizeof(bloom_extended_t));
    checkpointer((void *)bloom_bPx3rd, __FILE__, "calloc", "bloom_bPx3rd", __LINE__ - 1);
    bloom_bPx3rd_checksums = (struct checksumsha256 *)calloc(256, sizeof(struct checksumsha256));
    checkpointer((void *)bloom_bPx3rd_checksums, __FILE__, "calloc", "bloom_bPx3rd_checksums", __LINE__ - 1);

    output_success("Bloom filter for %" PRIu64 " elements ", bsgs_m3);
    bloom_bP3_totalbytes = 0;
    for (i = 0; i < 256; i++) {
        platform_mutex_init(&bloom_bPx3rd_mutex[i]);
        if (bloom_ext_init(&bloom_bPx3rd[i], itemsbloom3, 0.000001) != 0) {
            output_error("error bloom_init [%" PRIu64 "]\n", i);
            free(aux); free(pointx_str); free(pointy_str);
            return -1;
        }
        bloom_bP3_totalbytes += bloom_ext_bytes(&bloom_bPx3rd[i]);
    }
    printf(": %.2f MB\n", (float)((float)(uint64_t)bloom_bP3_totalbytes / (float)(uint64_t)1048576));

    /* ---- Step 5: Compute generator points ---- */
    BSGS_MP = secp->ComputePublicKey(&BSGS_M);
    BSGS_MP_double = secp->ComputePublicKey(&BSGS_M_double);
    BSGS_MP2 = secp->ComputePublicKey(&BSGS_M2);
    BSGS_MP2_double = secp->ComputePublicKey(&BSGS_M2_double);
    BSGS_MP3 = secp->ComputePublicKey(&BSGS_M3);
    BSGS_MP3_double = secp->ComputePublicKey(&BSGS_M3_double);

    BSGS_AMP2.resize(32);
    BSGS_AMP3.resize(32);
    GSn.resize(CPU_GRP_SIZE / 2);

    i = 0;

    Point bsP = secp->Negation(BSGS_MP_double);
    Point g = bsP;
    GSn[0] = g;

    g = secp->DoubleDirect(g);
    GSn[1] = g;

    for (size_t gi = 2; gi < CPU_GRP_SIZE / 2; gi++) {
        g = secp->AddDirect(g, bsP);
        GSn[gi] = g;
    }

    _2GSn = secp->DoubleDirect(GSn[CPU_GRP_SIZE / 2 - 1]);

    i = 0;
    point_temp.Set(BSGS_MP2);
    BSGS_AMP2[0] = secp->Negation(point_temp);
    BSGS_AMP2[0].Reduce();
    point_temp.Set(BSGS_MP2_double);
    point_temp = secp->Negation(point_temp);
    point_temp.Reduce();

    for (i = 1; i < 32; i++) {
        BSGS_AMP2[i] = secp->AddDirect(BSGS_AMP2[i - 1], point_temp);
        BSGS_AMP2[i].Reduce();
    }

    i = 0;
    point_temp.Set(BSGS_MP3);
    BSGS_AMP3[0] = secp->Negation(point_temp);
    BSGS_AMP3[0].Reduce();
    point_temp.Set(BSGS_MP3_double);
    point_temp = secp->Negation(point_temp);
    point_temp.Reduce();

    for (i = 1; i < 32; i++) {
        BSGS_AMP3[i] = secp->AddDirect(BSGS_AMP3[i - 1], point_temp);
        BSGS_AMP3[i].Reduce();
    }

    /* ---- Step 6: Allocate bP table ---- */
    bytes = (uint64_t)bsgs_m3 * (uint64_t)sizeof(bsgs_xvalue);
    output_success("Allocating %.2f MB for %" PRIu64 " bP Points\n", (double)bytes / 1048576.0, bsgs_m3);

    bPtable = (bsgs_xvalue *)malloc(bytes);
    checkpointer((void *)bPtable, __FILE__, "malloc", "bPtable", __LINE__ - 1);
    memset(bPtable, 0, bytes);

    /* ---- Step 7: Read cached bloom filters and bP table (if -S was used) ---- */
    if (FLAGSAVEREADFILE) {
        /* Reading file for 1st bloom filter */
        snprintf(buffer_bloom_file, 1024, "keyhunt_bsgs_11_%" PRIu64 ".blm", bsgs_m);
        fd_aux1 = fopen(buffer_bloom_file, "rb");
        if (fd_aux1 != NULL) {
            output_success("Reading bloom filter from file %s\n", buffer_bloom_file);
            for (i = 0; i < 256; i++) {
                struct bloom tmp_bloom;
                readed = fread(&tmp_bloom, sizeof(struct bloom), 1, fd_aux1);
                if (readed != 1) {
                    output_error("Error reading the file %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }

                bloom_ext_free(&bloom_bP[i]);
                bloom_bP[i].orig = tmp_bloom;
                bloom_bP[i].orig.bf = NULL;
                const bool cache_fast = (bloom_bP[i].orig.major == BLOOM_EXT_FAST_MAJOR && bloom_bP[i].orig.minor == BLOOM_EXT_FAST_MINOR);
#if defined(_WIN64) && !defined(__CYGWIN__)
                if (cache_fast) {
                    bloom_bP[i].orig.bf = (uint8_t *)_aligned_malloc(bloom_bP[i].orig.bytes, 64);
                } else {
                    bloom_bP[i].orig.bf = (uint8_t *)malloc(bloom_bP[i].orig.bytes);
                }
#else
                if (cache_fast) {
                    void *ptr = NULL;
                    if (posix_memalign(&ptr, 64, bloom_bP[i].orig.bytes) != 0) ptr = NULL;
                    bloom_bP[i].orig.bf = (uint8_t *)ptr;
                } else {
                    bloom_bP[i].orig.bf = (uint8_t *)malloc(bloom_bP[i].orig.bytes);
                }
#endif
                if (!bloom_bP[i].orig.bf) {
                    output_error("Error allocating memory for bloom cache %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }

                readed = fread(bloom_bP[i].orig.bf, bloom_bP[i].orig.bytes, 1, fd_aux1);
                if (readed != 1) {
                    output_error("Error reading the file %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }
                bloom_ext_sync_from_orig(&bloom_bP[i]);
                readed = fread(&bloom_bP_checksums[i], sizeof(struct checksumsha256), 1, fd_aux1);
                if (readed != 1) {
                    output_error("Error reading the file %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }
                if (FLAGSKIPCHECKSUM == 0) {
                    sha256((uint8_t *)bloom_bP[i].orig.bf, bloom_bP[i].orig.bytes, (uint8_t *)rawvalue);
                    if (memcmp(bloom_bP_checksums[i].data, rawvalue, 32) != 0 || memcmp(bloom_bP_checksums[i].backup, rawvalue, 32) != 0) {
                        output_error("Error checksum file mismatch! %s\n", buffer_bloom_file);
                        free(aux); free(pointx_str); free(pointy_str);
                        return -1;
                    }
                }
                double percent = ((double)(i + 1) / 256.0) * 100.0;
                printf("\r[");
                output_progress_bar(percent, 40);
                printf("] %.1f%%", percent);
                fflush(stdout);
            }
            printf("\n");
            fclose(fd_aux1);
            memset(buffer_bloom_file, 0, 1024);
            snprintf(buffer_bloom_file, 1024, "keyhunt_bsgs_3_%" PRIu64 ".blm", bsgs_m);
            fd_aux1 = fopen(buffer_bloom_file, "rb");
            if (fd_aux1 != NULL) {
                output_warning("Unused file detected %s you can delete it without worry\n", buffer_bloom_file);
                fclose(fd_aux1);
            }
            FLAGREADEDFILE1 = 1;
        } else {
            FLAGREADEDFILE1 = 0;
        }

        /* Reading file for 2nd bloom filter */
        snprintf(buffer_bloom_file, 1024, "keyhunt_bsgs_12_%" PRIu64 ".blm", bsgs_m2);
        fd_aux2 = fopen(buffer_bloom_file, "rb");
        if (fd_aux2 != NULL) {
            output_success("Reading bloom filter from file %s\n", buffer_bloom_file);
            for (i = 0; i < 256; i++) {
                struct bloom tmp_bloom;
                readed = fread(&tmp_bloom, sizeof(struct bloom), 1, fd_aux2);
                if (readed != 1) {
                    output_error("Error reading the file %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }

                bloom_ext_free(&bloom_bPx2nd[i]);
                bloom_bPx2nd[i].orig = tmp_bloom;
                bloom_bPx2nd[i].orig.bf = NULL;
                const bool cache_fast = (bloom_bPx2nd[i].orig.major == BLOOM_EXT_FAST_MAJOR && bloom_bPx2nd[i].orig.minor == BLOOM_EXT_FAST_MINOR);
#if defined(_WIN64) && !defined(__CYGWIN__)
                if (cache_fast) {
                    bloom_bPx2nd[i].orig.bf = (uint8_t *)_aligned_malloc(bloom_bPx2nd[i].orig.bytes, 64);
                } else {
                    bloom_bPx2nd[i].orig.bf = (uint8_t *)malloc(bloom_bPx2nd[i].orig.bytes);
                }
#else
                if (cache_fast) {
                    void *ptr = NULL;
                    if (posix_memalign(&ptr, 64, bloom_bPx2nd[i].orig.bytes) != 0) ptr = NULL;
                    bloom_bPx2nd[i].orig.bf = (uint8_t *)ptr;
                } else {
                    bloom_bPx2nd[i].orig.bf = (uint8_t *)malloc(bloom_bPx2nd[i].orig.bytes);
                }
#endif
                if (!bloom_bPx2nd[i].orig.bf) {
                    output_error("Error allocating memory for bloom cache %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }

                readed = fread(bloom_bPx2nd[i].orig.bf, bloom_bPx2nd[i].orig.bytes, 1, fd_aux2);
                if (readed != 1) {
                    output_error("Error reading the file %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }
                bloom_ext_sync_from_orig(&bloom_bPx2nd[i]);
                readed = fread(&bloom_bPx2nd_checksums[i], sizeof(struct checksumsha256), 1, fd_aux2);
                if (readed != 1) {
                    output_error("Error reading the file %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }
                memset(rawvalue, 0, 32);
                if (FLAGSKIPCHECKSUM == 0) {
                    sha256((uint8_t *)bloom_bPx2nd[i].orig.bf, bloom_bPx2nd[i].orig.bytes, (uint8_t *)rawvalue);
                    if (memcmp(bloom_bPx2nd_checksums[i].data, rawvalue, 32) != 0 || memcmp(bloom_bPx2nd_checksums[i].backup, rawvalue, 32) != 0) {
                        output_error("Error checksum file mismatch! %s\n", buffer_bloom_file);
                        free(aux); free(pointx_str); free(pointy_str);
                        return -1;
                    }
                }
                double percent = ((double)(i + 1) / 256.0) * 100.0;
                printf("\r[");
                output_progress_bar(percent, 40);
                printf("] %.1f%%", percent);
                fflush(stdout);
            }
            fclose(fd_aux2);
            printf("\n");
            memset(buffer_bloom_file, 0, 1024);
            snprintf(buffer_bloom_file, 1024, "keyhunt_bsgs_5_%" PRIu64 ".blm", bsgs_m2);
            fd_aux2 = fopen(buffer_bloom_file, "rb");
            if (fd_aux2 != NULL) {
                output_warning("Unused file detected %s you can delete it without worry\n", buffer_bloom_file);
                fclose(fd_aux2);
            }
            memset(buffer_bloom_file, 0, 1024);
            snprintf(buffer_bloom_file, 1024, "keyhunt_bsgs_1_%" PRIu64 ".blm", bsgs_m2);
            fd_aux2 = fopen(buffer_bloom_file, "rb");
            if (fd_aux2 != NULL) {
                output_warning("Unused file detected %s you can delete it without worry\n", buffer_bloom_file);
                fclose(fd_aux2);
            }
            FLAGREADEDFILE2 = 1;
        } else {
            FLAGREADEDFILE2 = 0;
        }

        /* Reading file for bPtable */
        snprintf(buffer_bloom_file, 1024, "keyhunt_bsgs_2_%" PRIu64 ".tbl", bsgs_m3);
        fd_aux3 = fopen(buffer_bloom_file, "rb");
        if (fd_aux3 != NULL) {
            output_success("Reading bP Table from file %s\n", buffer_bloom_file);
            fflush(stdout);

            const size_t chunk_size = 10 * 1024 * 1024;
            uint64_t bytes_read = 0;
            char *bPtable_ptr = (char *)bPtable;

            while (bytes_read < bytes) {
                size_t to_read = (bytes - bytes_read > chunk_size) ? chunk_size : (bytes - bytes_read);
                rsize = fread(bPtable_ptr + bytes_read, 1, to_read, fd_aux3);
                if (rsize != to_read) {
                    output_error("Error reading the file %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }
                bytes_read += rsize;

                double percent = (double)bytes_read / (double)bytes * 100.0;
                printf("\r  ");
                output_progress_bar(percent, 40);
                printf(" %.1f%%", percent);
                fflush(stdout);
            }
            printf("\n");

            rsize = fread(checksum, 32, 1, fd_aux3);
            if (rsize != 1) {
                output_error("Error reading the file %s\n", buffer_bloom_file);
                free(aux); free(pointx_str); free(pointy_str);
                return -1;
            }
            if (FLAGSKIPCHECKSUM == 0) {
                sha256((uint8_t *)bPtable, bytes, (uint8_t *)checksum_backup);
                if (memcmp(checksum, checksum_backup, 32) != 0) {
                    output_error("Error checksum file mismatch! %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }
            }
            output_success("bP Table loaded successfully\n");
            fclose(fd_aux3);
            FLAGREADEDFILE3 = 1;
        } else {
            FLAGREADEDFILE3 = 0;
        }

        /* Reading file for 3rd bloom filter */
        snprintf(buffer_bloom_file, 1024, "keyhunt_bsgs_13_%" PRIu64 ".blm", bsgs_m3);
        fd_aux2 = fopen(buffer_bloom_file, "rb");
        if (fd_aux2 != NULL) {
            output_success("Reading bloom filter from file %s\n", buffer_bloom_file);
            for (i = 0; i < 256; i++) {
                struct bloom tmp_bloom;
                readed = fread(&tmp_bloom, sizeof(struct bloom), 1, fd_aux2);
                if (readed != 1) {
                    output_error("Error reading the file %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }

                bloom_ext_free(&bloom_bPx3rd[i]);
                bloom_bPx3rd[i].orig = tmp_bloom;
                bloom_bPx3rd[i].orig.bf = NULL;
                const bool cache_fast = (bloom_bPx3rd[i].orig.major == BLOOM_EXT_FAST_MAJOR && bloom_bPx3rd[i].orig.minor == BLOOM_EXT_FAST_MINOR);
#if defined(_WIN64) && !defined(__CYGWIN__)
                if (cache_fast) {
                    bloom_bPx3rd[i].orig.bf = (uint8_t *)_aligned_malloc(bloom_bPx3rd[i].orig.bytes, 64);
                } else {
                    bloom_bPx3rd[i].orig.bf = (uint8_t *)malloc(bloom_bPx3rd[i].orig.bytes);
                }
#else
                if (cache_fast) {
                    void *ptr = NULL;
                    if (posix_memalign(&ptr, 64, bloom_bPx3rd[i].orig.bytes) != 0) ptr = NULL;
                    bloom_bPx3rd[i].orig.bf = (uint8_t *)ptr;
                } else {
                    bloom_bPx3rd[i].orig.bf = (uint8_t *)malloc(bloom_bPx3rd[i].orig.bytes);
                }
#endif
                if (!bloom_bPx3rd[i].orig.bf) {
                    output_error("Error allocating memory for bloom cache %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }

                readed = fread(bloom_bPx3rd[i].orig.bf, bloom_bPx3rd[i].orig.bytes, 1, fd_aux2);
                if (readed != 1) {
                    output_error("Error reading the file %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }
                bloom_ext_sync_from_orig(&bloom_bPx3rd[i]);
                readed = fread(&bloom_bPx3rd_checksums[i], sizeof(struct checksumsha256), 1, fd_aux2);
                if (readed != 1) {
                    output_error("Error reading the file %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }
                memset(rawvalue, 0, 32);
                if (FLAGSKIPCHECKSUM == 0) {
                    sha256((uint8_t *)bloom_bPx3rd[i].orig.bf, bloom_bPx3rd[i].orig.bytes, (uint8_t *)rawvalue);
                    if (memcmp(bloom_bPx3rd_checksums[i].data, rawvalue, 32) != 0 || memcmp(bloom_bPx3rd_checksums[i].backup, rawvalue, 32) != 0) {
                        output_error("Error checksum file mismatch! %s\n", buffer_bloom_file);
                        free(aux); free(pointx_str); free(pointy_str);
                        return -1;
                    }
                }
                double percent = ((double)(i + 1) / 256.0) * 100.0;
                printf("\r[");
                output_progress_bar(percent, 40);
                printf("] %.1f%%", percent);
                fflush(stdout);
            }
            fclose(fd_aux2);
            printf("\n");
            FLAGREADEDFILE4 = 1;
        } else {
            FLAGREADEDFILE4 = 0;
        }
    }

    /* ---- Step 8: Spawn bPload threads to populate bloom filters + table ---- */
    if (!FLAGREADEDFILE1 || !FLAGREADEDFILE2 || !FLAGREADEDFILE3 || !FLAGREADEDFILE4) {
        if (FLAGREADEDFILE1 == 1) {
            /* Need to recalculate files 2-4 only (3% of work) */
            output_info("We need to recalculate some files, don't worry this is only 3%% of the previous work\n");
            FINISHED_THREADS_COUNTER = 0;
            FINISHED_THREADS_BP = 0;
            FINISHED_ITEMS.store(0, std::memory_order_relaxed);
            salir = 0;
            BASE = 0;
            THREADCOUNTER = 0;
            if (THREADBPWORKLOAD >= bsgs_m2) {
                THREADBPWORKLOAD = bsgs_m2;
            }
            THREADCYCLES = bsgs_m2 / THREADBPWORKLOAD;
            PERTHREAD_R = bsgs_m2 % THREADBPWORKLOAD;
            if (PERTHREAD_R != 0) {
                THREADCYCLES++;
            }

            double initial_percent = (bsgs_m > 0) ? ((double)FINISHED_ITEMS.load(std::memory_order_relaxed) / (double)bsgs_m) * 100.0 : 0.0;
            printf("\r[BSGS] ");
            output_progress_bar(initial_percent, 25);
            printf(" processing bP points: %" PRIu64 "/%" PRIu64 " (%.1f%%)   ", (uint64_t)FINISHED_ITEMS.load(std::memory_order_relaxed), bsgs_m, initial_percent);
            fflush(stdout);

            tid = (platform_thread_t *)calloc(NTHREADS, sizeof(platform_thread_t));
            checkpointer((void *)tid, __FILE__, "calloc", "tid", __LINE__ - 1);
            bPload_mutex = (platform_mutex_t *)calloc(NTHREADS, sizeof(platform_mutex_t));
            checkpointer((void *)bPload_mutex, __FILE__, "calloc", "bPload_mutex", __LINE__ - 1);
            bPload_temp_ptr = (struct bPload *)calloc(NTHREADS, sizeof(struct bPload));
            checkpointer((void *)bPload_temp_ptr, __FILE__, "calloc", "bPload_temp_ptr", __LINE__ - 1);
            bPload_threads_available = (char *)calloc(NTHREADS, sizeof(char));
            checkpointer((void *)bPload_threads_available, __FILE__, "calloc", "bPload_threads_available", __LINE__ - 1);

            memset(bPload_threads_available, 1, NTHREADS);

            for (j = 0; j < NTHREADS; j++) {
                platform_mutex_init(&bPload_mutex[j]);
            }

            do {
                for (j = 0; j < NTHREADS && !salir; j++) {
                    if (bPload_threads_available[j] && !salir) {
                        bPload_threads_available[j] = 0;
                        bPload_temp_ptr[j].from = BASE;
                        bPload_temp_ptr[j].threadid = j;
                        bPload_temp_ptr[j].finished = 0;
                        if (THREADCOUNTER < THREADCYCLES - 1) {
                            bPload_temp_ptr[j].to = BASE + THREADBPWORKLOAD;
                            bPload_temp_ptr[j].workload = THREADBPWORKLOAD;
                        } else {
                            bPload_temp_ptr[j].to = BASE + THREADBPWORKLOAD + PERTHREAD_R;
                            bPload_temp_ptr[j].workload = THREADBPWORKLOAD + PERTHREAD_R;
                            salir = 1;
                        }
                        s = platform_thread_create(&tid[j], thread_bPload_2blooms, (void *)&bPload_temp_ptr[j]);
                        if (s == 0) {
                            platform_thread_detach(tid[j]);
                        }
                        BASE += THREADBPWORKLOAD;
                        THREADCOUNTER++;
                    }
                }

                {
                    uint64_t current_items = FINISHED_ITEMS.load(std::memory_order_relaxed);
                    if (OLDFINISHED_ITEMS != current_items) {
                        double percent = (bsgs_m2 > 0) ? ((double)current_items / (double)bsgs_m2) * 100.0 : 0.0;
                        printf("\r[BSGS] ");
                        output_progress_bar(percent, 25);
                        printf(" processing bP points: %" PRIu64 "/%" PRIu64 " (%.1f%%)   ", current_items, bsgs_m2, percent);
                        fflush(stdout);
                        OLDFINISHED_ITEMS = current_items;
                    }
                }

                for (j = 0; j < NTHREADS; j++) {
                    platform_mutex_lock(&bPload_mutex[j]);
                    finished = bPload_temp_ptr[j].finished;
                    platform_mutex_unlock(&bPload_mutex[j]);
                    if (finished) {
                        bPload_temp_ptr[j].finished = 0;
                        bPload_threads_available[j] = 1;
                        FINISHED_ITEMS.fetch_add(bPload_temp_ptr[j].workload, std::memory_order_relaxed);
                        FINISHED_THREADS_COUNTER++;
                    }
                }
            } while (FINISHED_THREADS_COUNTER < THREADCYCLES);
            printf("\r[BSGS] ");
            output_progress_bar(100.0, 25);
            printf(" processing bP points: %" PRIu64 "/%" PRIu64 " (100.0%%) done\n", bsgs_m2, bsgs_m2);

            free(tid);
            free(bPload_mutex);
            free(bPload_temp_ptr);
            free(bPload_threads_available);
        } else {
            /* Need to do all files (100%) */
            FINISHED_THREADS_COUNTER = 0;
            FINISHED_THREADS_BP = 0;
            FINISHED_ITEMS.store(0, std::memory_order_relaxed);
            salir = 0;
            BASE = 0;
            THREADCOUNTER = 0;
            if (THREADBPWORKLOAD >= bsgs_m) {
                THREADBPWORKLOAD = bsgs_m;
            }
            THREADCYCLES = bsgs_m / THREADBPWORKLOAD;
            PERTHREAD_R = bsgs_m % THREADBPWORKLOAD;
            if (PERTHREAD_R != 0) {
                THREADCYCLES++;
            }

            double initial_percent = (bsgs_m > 0) ? ((double)FINISHED_ITEMS.load(std::memory_order_relaxed) / (double)bsgs_m) * 100.0 : 0.0;
            printf("\r[BSGS] ");
            output_progress_bar(initial_percent, 25);
            printf(" processing bP points: %" PRIu64 "/%" PRIu64 " (%.1f%%)   ", (uint64_t)FINISHED_ITEMS.load(std::memory_order_relaxed), bsgs_m, initial_percent);
            fflush(stdout);

            tid = (platform_thread_t *)calloc(NTHREADS, sizeof(platform_thread_t));
            checkpointer((void *)tid, __FILE__, "calloc", "tid", __LINE__ - 1);
            bPload_mutex = (platform_mutex_t *)calloc(NTHREADS, sizeof(platform_mutex_t));
            checkpointer((void *)bPload_mutex, __FILE__, "calloc", "bPload_mutex", __LINE__ - 1);

            bPload_temp_ptr = (struct bPload *)calloc(NTHREADS, sizeof(struct bPload));
            checkpointer((void *)bPload_temp_ptr, __FILE__, "calloc", "bPload_temp_ptr", __LINE__ - 1);
            bPload_threads_available = (char *)calloc(NTHREADS, sizeof(char));
            checkpointer((void *)bPload_threads_available, __FILE__, "calloc", "bPload_threads_available", __LINE__ - 1);

            memset(bPload_threads_available, 1, NTHREADS);

            for (j = 0; j < NTHREADS; j++) {
                platform_mutex_init(&bPload_mutex[j]);
            }

            do {
                for (j = 0; j < NTHREADS && !salir; j++) {
                    if (bPload_threads_available[j] && !salir) {
                        bPload_threads_available[j] = 0;
                        bPload_temp_ptr[j].from = BASE;
                        bPload_temp_ptr[j].threadid = j;
                        bPload_temp_ptr[j].finished = 0;
                        if (THREADCOUNTER < THREADCYCLES - 1) {
                            bPload_temp_ptr[j].to = BASE + THREADBPWORKLOAD;
                            bPload_temp_ptr[j].workload = THREADBPWORKLOAD;
                        } else {
                            bPload_temp_ptr[j].to = BASE + THREADBPWORKLOAD + PERTHREAD_R;
                            bPload_temp_ptr[j].workload = THREADBPWORKLOAD + PERTHREAD_R;
                            salir = 1;
                        }
                        s = platform_thread_create(&tid[j], thread_bPload, (void *)&bPload_temp_ptr[j]);
                        if (s == 0) {
                            platform_thread_detach(tid[j]);
                        }
                        BASE += THREADBPWORKLOAD;
                        THREADCOUNTER++;
                    }
                }
                {
                    uint64_t current_items = FINISHED_ITEMS.load(std::memory_order_relaxed);
                    if (OLDFINISHED_ITEMS != current_items) {
                        double percent = (bsgs_m > 0) ? ((double)current_items / (double)bsgs_m) * 100.0 : 0.0;
                        printf("\r[BSGS] ");
                        output_progress_bar(percent, 25);
                        printf(" processing bP points: %" PRIu64 "/%" PRIu64 " (%.1f%%)   ", current_items, bsgs_m, percent);
                        fflush(stdout);
                        OLDFINISHED_ITEMS = current_items;
                    }
                }

                for (j = 0; j < NTHREADS; j++) {
                    platform_mutex_lock(&bPload_mutex[j]);
                    finished = bPload_temp_ptr[j].finished;
                    platform_mutex_unlock(&bPload_mutex[j]);
                    if (finished) {
                        bPload_temp_ptr[j].finished = 0;
                        bPload_threads_available[j] = 1;
                        FINISHED_ITEMS.fetch_add(bPload_temp_ptr[j].workload, std::memory_order_relaxed);
                        FINISHED_THREADS_COUNTER++;
                    }
                }

            } while (FINISHED_THREADS_COUNTER < THREADCYCLES);
            printf("\r[BSGS] ");
            output_progress_bar(100.0, 25);
            printf(" processing bP points: %" PRIu64 "/%" PRIu64 " (100.0%%) done\n", bsgs_m, bsgs_m);

            free(tid);
            free(bPload_mutex);
            free(bPload_temp_ptr);
            free(bPload_threads_available);
        }
    }

    /* ---- Step 9: Compute checksums ---- */
    if (!FLAGREADEDFILE1) {
        printf("\r[BSGS] ");
        output_progress_bar(0.0, 25);
        printf(" computing bloom_bP checksums: 0/256 (0.0%%)   ");
        fflush(stdout);
        for (i = 0; i < 256; i++) {
            sha256((uint8_t *)bloom_bP[i].orig.bf, bloom_bP[i].orig.bytes, (uint8_t *)bloom_bP_checksums[i].data);
            memcpy(bloom_bP_checksums[i].backup, bloom_bP_checksums[i].data, 32);
            if ((i + 1) % 16 == 0 || i == 255) {
                double percent = ((double)(i + 1) / 256.0) * 100.0;
                printf("\r[BSGS] ");
                output_progress_bar(percent, 25);
                printf(" computing bloom_bP checksums: %" PRIu64 "/256 (%.1f%%)   ", i + 1, percent);
                fflush(stdout);
            }
        }
        printf("\r[BSGS] ");
        output_progress_bar(100.0, 25);
        printf(" computing bloom_bP checksums: 256/256 (100.0%%) done\n");
        fflush(stdout);
    }
    if (!FLAGREADEDFILE2) {
        printf("\r[BSGS] ");
        output_progress_bar(0.0, 25);
        printf(" computing bloom_bPx2nd checksums: 0/256 (0.0%%)   ");
        fflush(stdout);
        for (i = 0; i < 256; i++) {
            sha256((uint8_t *)bloom_bPx2nd[i].orig.bf, bloom_bPx2nd[i].orig.bytes, (uint8_t *)bloom_bPx2nd_checksums[i].data);
            memcpy(bloom_bPx2nd_checksums[i].backup, bloom_bPx2nd_checksums[i].data, 32);
            if ((i + 1) % 16 == 0 || i == 255) {
                double percent = ((double)(i + 1) / 256.0) * 100.0;
                printf("\r[BSGS] ");
                output_progress_bar(percent, 25);
                printf(" computing bloom_bPx2nd checksums: %" PRIu64 "/256 (%.1f%%)   ", i + 1, percent);
                fflush(stdout);
            }
        }
        printf("\r[BSGS] ");
        output_progress_bar(100.0, 25);
        printf(" computing bloom_bPx2nd checksums: 256/256 (100.0%%) done\n");
        fflush(stdout);
    }
    if (!FLAGREADEDFILE4) {
        printf("\r[BSGS] ");
        output_progress_bar(0.0, 25);
        printf(" computing bloom_bPx3rd checksums: 0/256 (0.0%%)   ");
        fflush(stdout);
        for (i = 0; i < 256; i++) {
            sha256((uint8_t *)bloom_bPx3rd[i].orig.bf, bloom_bPx3rd[i].orig.bytes, (uint8_t *)bloom_bPx3rd_checksums[i].data);
            memcpy(bloom_bPx3rd_checksums[i].backup, bloom_bPx3rd_checksums[i].data, 32);
            if ((i + 1) % 16 == 0 || i == 255) {
                double percent = ((double)(i + 1) / 256.0) * 100.0;
                printf("\r[BSGS] ");
                output_progress_bar(percent, 25);
                printf(" computing bloom_bPx3rd checksums: %" PRIu64 "/256 (%.1f%%)   ", i + 1, percent);
                fflush(stdout);
            }
        }
        printf("\r[BSGS] ");
        output_progress_bar(100.0, 25);
        printf(" computing bloom_bPx3rd checksums: 256/256 (100.0%%) done\n");
        fflush(stdout);
    }
    if (!FLAGREADEDFILE3) {
        printf("\r[BSGS] ");
        output_progress_bar(0.0, 25);
        printf(" sorting bP table: %" PRIu64 " elements (0.0%%)   ", bsgs_m3);
        fflush(stdout);
        bsgs_sort(bPtable, bsgs_m3);
        sha256((uint8_t *)bPtable, bytes, (uint8_t *)checksum);
        memcpy(checksum_backup, checksum, 32);
        printf("\r[BSGS] ");
        output_progress_bar(100.0, 25);
        printf(" sorting bP table: %" PRIu64 " elements (100.0%%) done\n", bsgs_m3);
        fflush(stdout);
    }

    /* ---- Step 10: Write bloom filter cache files ---- */
    if (FLAGSAVEREADFILE || FLAGUPDATEFILE1) {
        if (!FLAGREADEDFILE1 || FLAGUPDATEFILE1) {
            snprintf(buffer_bloom_file, 1024, "keyhunt_bsgs_11_%" PRIu64 ".blm", bsgs_m);
            if (FLAGUPDATEFILE1) {
                output_warning("Updating old file into a new one\n");
            }
            fd_aux1 = fopen(buffer_bloom_file, "wb");
            if (fd_aux1 != NULL) {
                output_success("Writing bloom filter to file %s\n", buffer_bloom_file);
                for (i = 0; i < 256; i++) {
                    readed = fwrite(&bloom_bP[i].orig, sizeof(struct bloom), 1, fd_aux1);
                    if (readed != 1) {
                        output_error("Error writing the file %s please delete it\n", buffer_bloom_file);
                        free(aux); free(pointx_str); free(pointy_str);
                        return -1;
                    }
                    readed = fwrite(bloom_bP[i].orig.bf, bloom_bP[i].orig.bytes, 1, fd_aux1);
                    if (readed != 1) {
                        output_error("Error writing the file %s please delete it\n", buffer_bloom_file);
                        free(aux); free(pointx_str); free(pointy_str);
                        return -1;
                    }
                    readed = fwrite(&bloom_bP_checksums[i], sizeof(struct checksumsha256), 1, fd_aux1);
                    if (readed != 1) {
                        output_error("Error writing the file %s please delete it\n", buffer_bloom_file);
                        free(aux); free(pointx_str); free(pointy_str);
                        return -1;
                    }
                    double percent = ((double)(i + 1) / 256.0) * 100.0;
                    printf("\r[");
                    output_progress_bar(percent, 40);
                    printf("] %.1f%%", percent);
                    fflush(stdout);
                }
                printf("\n");
                fclose(fd_aux1);
            } else {
                output_error("Error can't create the file %s\n", buffer_bloom_file);
                free(aux); free(pointx_str); free(pointy_str);
                return -1;
            }
        }
        if (!FLAGREADEDFILE2) {
            snprintf(buffer_bloom_file, 1024, "keyhunt_bsgs_12_%" PRIu64 ".blm", bsgs_m2);
            fd_aux2 = fopen(buffer_bloom_file, "wb");
            if (fd_aux2 != NULL) {
                output_success("Writing bloom filter to file %s\n", buffer_bloom_file);
                for (i = 0; i < 256; i++) {
                    readed = fwrite(&bloom_bPx2nd[i].orig, sizeof(struct bloom), 1, fd_aux2);
                    if (readed != 1) {
                        output_error("Error writing the file %s\n", buffer_bloom_file);
                        free(aux); free(pointx_str); free(pointy_str);
                        return -1;
                    }
                    readed = fwrite(bloom_bPx2nd[i].orig.bf, bloom_bPx2nd[i].orig.bytes, 1, fd_aux2);
                    if (readed != 1) {
                        output_error("Error writing the file %s\n", buffer_bloom_file);
                        free(aux); free(pointx_str); free(pointy_str);
                        return -1;
                    }
                    readed = fwrite(&bloom_bPx2nd_checksums[i], sizeof(struct checksumsha256), 1, fd_aux2);
                    if (readed != 1) {
                        output_error("Error writing the file %s please delete it\n", buffer_bloom_file);
                        free(aux); free(pointx_str); free(pointy_str);
                        return -1;
                    }
                    double percent = ((double)(i + 1) / 256.0) * 100.0;
                    printf("\r[");
                    output_progress_bar(percent, 40);
                    printf("] %.1f%%", percent);
                    fflush(stdout);
                }
                printf("\n");
                fclose(fd_aux2);
            } else {
                output_error("Error can't create the file %s\n", buffer_bloom_file);
                free(aux); free(pointx_str); free(pointy_str);
                return -1;
            }
        }

        if (!FLAGREADEDFILE3) {
            snprintf(buffer_bloom_file, 1024, "keyhunt_bsgs_2_%" PRIu64 ".tbl", bsgs_m3);
            fd_aux3 = fopen(buffer_bloom_file, "wb");
            if (fd_aux3 != NULL) {
                output_success("Writing bP Table to file %s\n", buffer_bloom_file);
                printf("[");
                output_progress_bar(0, 40);
                printf("] 0.0%%");
                fflush(stdout);
                readed = fwrite(bPtable, bytes, 1, fd_aux3);
                if (readed != 1) {
                    output_error("Error writing the file %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }
                readed = fwrite(checksum, 32, 1, fd_aux3);
                if (readed != 1) {
                    output_error("Error writing the file %s\n", buffer_bloom_file);
                    free(aux); free(pointx_str); free(pointy_str);
                    return -1;
                }
                printf("\r[");
                output_progress_bar(100, 40);
                printf("] 100.0%%\n");
                fclose(fd_aux3);
            } else {
                output_error("Error can't create the file %s\n", buffer_bloom_file);
                free(aux); free(pointx_str); free(pointy_str);
                return -1;
            }
        }
        if (!FLAGREADEDFILE4) {
            snprintf(buffer_bloom_file, 1024, "keyhunt_bsgs_13_%" PRIu64 ".blm", bsgs_m3);
            fd_aux2 = fopen(buffer_bloom_file, "wb");
            if (fd_aux2 != NULL) {
                output_success("Writing bloom filter to file %s\n", buffer_bloom_file);
                for (i = 0; i < 256; i++) {
                    readed = fwrite(&bloom_bPx3rd[i].orig, sizeof(struct bloom), 1, fd_aux2);
                    if (readed != 1) {
                        output_error("Error writing the file %s\n", buffer_bloom_file);
                        free(aux); free(pointx_str); free(pointy_str);
                        return -1;
                    }
                    readed = fwrite(bloom_bPx3rd[i].orig.bf, bloom_bPx3rd[i].orig.bytes, 1, fd_aux2);
                    if (readed != 1) {
                        output_error("Error writing the file %s\n", buffer_bloom_file);
                        free(aux); free(pointx_str); free(pointy_str);
                        return -1;
                    }
                    readed = fwrite(&bloom_bPx3rd_checksums[i], sizeof(struct checksumsha256), 1, fd_aux2);
                    if (readed != 1) {
                        output_error("Error writing the file %s please delete it\n", buffer_bloom_file);
                        free(aux); free(pointx_str); free(pointy_str);
                        return -1;
                    }
                    double percent = ((double)(i + 1) / 256.0) * 100.0;
                    printf("\r[");
                    output_progress_bar(percent, 40);
                    printf("] %.1f%%", percent);
                    fflush(stdout);
                }
                printf("\n");
                fclose(fd_aux2);
            } else {
                output_error("Error can't create the file %s\n", buffer_bloom_file);
                free(aux); free(pointx_str); free(pointy_str);
                return -1;
            }
        }
    }

    i = 0;

    /* ---- Step 11: Apply auto-tuned thread count ---- */
    if (!FLAGTHREADS && NTHREADS == 1 && OPTIMAL_THREADS > 0) {
        NTHREADS = OPTIMAL_THREADS;
        output_info("Using auto-tuned thread count: %d (optimal for %d physical cores)\n",
                    NTHREADS, g_sysinfo.cpu_physical_cores);
    }

    /* ---- Step 12: Allocate thread arrays ---- */
    steps = (struct thread_counter *)aligned_calloc(64, NTHREADS, sizeof(struct thread_counter));
    checkpointer((void *)steps, __FILE__, "aligned_calloc", "steps", __LINE__ - 1);
    ends = (struct thread_flag *)aligned_calloc(64, NTHREADS, sizeof(struct thread_flag));
    checkpointer((void *)ends, __FILE__, "aligned_calloc", "ends", __LINE__ - 1);
    tid = (platform_thread_t *)calloc(NTHREADS, sizeof(platform_thread_t));
    checkpointer((void *)tid, __FILE__, "calloc", "tid", __LINE__ - 1);

#ifndef _WIN64
    shutdown_work_queue();
#endif

    /* ---- Step 13: Config bridge - populate runtime state ---- */
    config->runtime.num_threads = NTHREADS;
    config->runtime.bloom_filter = (void *)bloom_bP;
    config->runtime.address_table = (void *)addressTable;
    config->runtime.address_count = (int64_t)N;
    config->runtime.write_mutex = (void *)&write_keys;
    config->runtime.random_mutex = (void *)&write_random;
    config->runtime.bsgs_mutex = (void *)&bsgs_thread;
    config->runtime.thread_counters = (void *)steps;
    config->runtime.thread_flags = (void *)ends;
    config->runtime.thread_output = (void *)&THREADOUTPUT;
    config->runtime.bsgs_generator_points = (void *)&GSn;
    config->runtime.bsgs_generator_point_2 = (void *)&_2GSn;
    config->runtime.sequential_max = N_SEQUENTIAL_MAX;

    /* ---- Step 14: Populate bsgs_context_t ---- */
    bsgs_context_t *bsgs_ctx = (bsgs_context_t *)config->runtime.bsgs_context;
    bsgs_ctx->BSGS_CURRENT = &BSGS_CURRENT;
    bsgs_ctx->BSGS_R = &BSGS_R;
    bsgs_ctx->BSGS_AUX = &BSGS_AUX;
    bsgs_ctx->BSGS_N = &BSGS_N;
    bsgs_ctx->BSGS_N_double = &BSGS_N_double;
    bsgs_ctx->BSGS_M = &BSGS_M;
    bsgs_ctx->BSGS_M_double = &BSGS_M_double;
    bsgs_ctx->BSGS_M2 = &BSGS_M2;
    bsgs_ctx->BSGS_M2_double = &BSGS_M2_double;
    bsgs_ctx->BSGS_M3 = &BSGS_M3;
    bsgs_ctx->BSGS_M3_double = &BSGS_M3_double;
    bsgs_ctx->BSGS_MP_double = &BSGS_MP_double;
    bsgs_ctx->BSGS_MP2_double = &BSGS_MP2_double;
    bsgs_ctx->BSGS_MP3_double = &BSGS_MP3_double;
    bsgs_ctx->BSGS_AMP2 = &BSGS_AMP2;
    bsgs_ctx->BSGS_AMP3 = &BSGS_AMP3;
    bsgs_ctx->OriginalPointsBSGS = &OriginalPointsBSGS;
    bsgs_ctx->OriginalPointsBSGScompressed = OriginalPointsBSGScompressed;
    bsgs_ctx->GSn = &GSn;
    bsgs_ctx->_2GSn = &_2GSn;
    bsgs_ctx->bsgs_found = bsgs_found;
    bsgs_ctx->bloom_bP = bloom_bP;
    bsgs_ctx->bloom_bPx2nd = bloom_bPx2nd;
    bsgs_ctx->bloom_bPx3rd = bloom_bPx3rd;
    bsgs_ctx->bPtable = bPtable;
    bsgs_ctx->bloom_bP_mutex = bloom_bP_mutex;
    bsgs_ctx->bloom_bPx2nd_mutex = bloom_bPx2nd_mutex;
    bsgs_ctx->bloom_bPx3rd_mutex = bloom_bPx3rd_mutex;
    bsgs_ctx->bPload_mutex = bPload_mutex;
    bsgs_ctx->bsgs_m = bsgs_m;
    bsgs_ctx->bsgs_m2 = bsgs_m2;
    bsgs_ctx->bsgs_m3 = bsgs_m3;
    bsgs_ctx->bsgs_aux = bsgs_aux;
    bsgs_ctx->bsgs_point_number = bsgs_point_number;
    bsgs_ctx->BSGS_BUFFERXPOINTLENGTH = BSGS_BUFFERXPOINTLENGTH;
    bsgs_ctx->FLAGREADEDFILE1 = FLAGREADEDFILE1;
    bsgs_ctx->FLAGREADEDFILE2 = FLAGREADEDFILE2;
    bsgs_ctx->FLAGREADEDFILE3 = FLAGREADEDFILE3;
    bsgs_ctx->FLAGREADEDFILE4 = FLAGREADEDFILE4;

    free(aux);
    free(pointx_str);
    free(pointy_str);

    return 0;
}

/* ============================================================================
 * mode_bsgs_run - Create BSGS mode search threads
 *
 * Selects the appropriate BSGS thread function based on FLAGBSGSMODE
 * and creates thread_count threads.
 * ============================================================================ */
static int mode_bsgs_run(keyhunt_config_t *config,
                          platform_thread_t * /*tids_unused*/, int /*thread_count_unused*/) {
    /*
     * BSGS mode uses the global tid/steps arrays allocated in mode_bsgs_init.
     * The tids/thread_count parameters from mode_dispatch are ignored since
     * init already allocated the correct arrays and stored them in globals.
     */
    struct thread_counter *steps_ptr =
        (struct thread_counter *)config->runtime.thread_counters;

    profile_init_threads((int)NTHREADS);

    for (int j = 0; j < NTHREADS; j++) {
        thread_args *bargs = new thread_args{config, j};
        steps_ptr[j].value = 0;
        int s = 0;
        switch (FLAGBSGSMODE) {
            case 0:
                s = platform_thread_create(&tid[j], thread_process_bsgs, (void *)bargs);
                break;
            case 1:
                s = platform_thread_create(&tid[j], thread_process_bsgs_backward, (void *)bargs);
                break;
            case 2:
                s = platform_thread_create(&tid[j], thread_process_bsgs_both, (void *)bargs);
                break;
            case 3:
                s = platform_thread_create(&tid[j], thread_process_bsgs_random, (void *)bargs);
                break;
            case 4:
                s = platform_thread_create(&tid[j], thread_process_bsgs_dance, (void *)bargs);
                break;
        }
        if (s != 0) {
            output_error("thread thread_process_bsgs (thread %d)\n", j);
            delete bargs;
            return -1;
        }
    }
    return 0;
}

/* ============================================================================
 * mode_bsgs_cleanup - BSGS mode cleanup
 *
 * No-op: cleanup_bsgs_resources is already registered via atexit() in main().
 * ============================================================================ */
static void mode_bsgs_cleanup(keyhunt_config_t * /*config*/) {
    /* No mode-specific cleanup needed - atexit handles it */
}

/* Registered operations for BSGS mode (extern linkage for dispatch table) */
extern const mode_ops_t mode_bsgs_ops;
const mode_ops_t mode_bsgs_ops = {
    mode_bsgs_init,
    mode_bsgs_run,
    mode_bsgs_cleanup
};
