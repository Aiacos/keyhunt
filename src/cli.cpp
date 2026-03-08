// src/cli.cpp
// CLI argument parsing implementation for keyhunt

#include "cli.h"
#include "config/config.h"
#include "benchmark.h"
#include "secp256k1/Int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <strings.h>  // for strcasecmp on Linux
#include <ctype.h>    // for tolower

// Mode name strings
static const char *mode_names[] = {
    "xpoint", "address", "bsgs", "rmd160", "pub2rmd", "minikeys", "vanity"
};

static const char *keytype_names[] = {
    "compress", "uncompress", "both"
};

static const char *bsgs_mode_names[] = {
    "sequential", "backward", "both", "random", "dance"
};

static const char *gpu_mode_names[] = {
    "off", "on", "auto", "hybrid"
};

const char *cli_mode_name(search_mode_t mode) {
    if (mode >= MODE_XPOINT && mode <= MODE_VANITY) {
        return mode_names[mode];
    }
    return "unknown";
}

const char *cli_keytype_name(key_type_t type) {
    if (type >= KEYTYPE_COMPRESSED && type <= KEYTYPE_BOTH) {
        return keytype_names[type];
    }
    return "unknown";
}

const char *cli_bsgs_mode_name(bsgs_mode_t mode) {
    if (mode >= BSGS_SEQUENTIAL && mode <= BSGS_DANCE) {
        return bsgs_mode_names[mode];
    }
    return "unknown";
}

const char *cli_gpu_mode_name(gpu_mode_t mode) {
    if (mode >= GPU_OFF && mode <= GPU_HYBRID) {
        return gpu_mode_names[mode];
    }
    return "unknown";
}

const char *get_mode_name(int mode) {
    switch (mode) {
        case MODE_XPOINT:   return "xpoint";
        case MODE_ADDRESS:  return "address";
        case MODE_BSGS:     return "bsgs";
        case MODE_RMD160:   return "rmd160";
        case MODE_PUB2RMD:  return "pub2rmd";
        case MODE_MINIKEYS: return "minikeys";
        case MODE_VANITY:   return "vanity";
        default:            return "unknown";
    }
}

void menu(void) {
    printf("\n");
    printf("keyhunt - High-performance cryptocurrency private key search tool\n");
    printf("\n");
    printf("USAGE:\n");
    printf("  keyhunt -m <mode> -f <file> [options]\n");
    printf("\n");
    printf("MODES (-m):\n");
    printf("  address     Search for Bitcoin addresses using bloom filters (default)\n");
    printf("  rmd160      Search for RIPEMD160 hashes directly\n");
    printf("  xpoint      Search for public key X-coordinates (fastest for known pubkeys)\n");
    printf("  bsgs        Baby Step Giant Step algorithm for known public keys\n");
    printf("  vanity      Generate vanity addresses with specific prefixes\n");
    printf("\n");
    printf("REQUIRED OPTIONS:\n");
    printf("  -f <file>   Input file with addresses, xpoints, or public keys\n");
    printf("  -m <mode>   Search mode (see MODES above)\n");
    printf("\n");
    printf("COMMON OPTIONS:\n");
    printf("  -h, --help  Show this help message\n");
    printf("  -t <num>    Number of threads (default: auto-detect CPU cores)\n");
    printf("  -b <bits>   Bit range for puzzle solving (e.g., 66 for puzzle #66)\n");
    printf("  -r <range>  Search range as START:END in hex (e.g., 1:FFFFFFFF)\n");
    printf("  -R          Random search mode (default behavior)\n");
    printf("  -q          Quiet mode - suppress thread output\n");
    printf("  -s <secs>   Stats output interval in seconds (0 to disable)\n");
    printf("  -l <type>   Address type: compress, uncompress, both\n");
    printf("  -c <crypto> Cryptocurrency: btc, eth (only with -m address)\n");
    printf("  -e          Enable endomorphism (6x speed for full curve search)\n");
    printf("  -I <stride> Stride value for sequential search\n");
    printf("  -P          Show segmented range progress indicator\n");
    printf("  -M          Matrix display mode (slower but cool looking)\n");
    printf("\n");
    printf("BSGS OPTIONS:\n");
    printf("  -n <value>  N value - larger N uses more RAM but faster search\n");
    printf("  -k <value>  K factor multiplier for M (more RAM, more speed)\n");
    printf("  -B <mode>   BSGS search pattern: sequential, backward, both, random, dance\n");
    printf("  -S          Save/load BSGS data (bloom filters and bP tables)\n");
    printf("  -6          Skip SHA256 checksum verification on data files\n");
    printf("\n");
    printf("VANITY OPTIONS:\n");
    printf("  -v <prefix> Vanity address prefix to search for\n");
    printf("\n");
    printf("MINIKEY OPTIONS:\n");
    printf("  -C <base>   Set 22-character minikey base (e.g., SRPqx8QiwnW4WNWnTVa2W5)\n");
    printf("  -8 <alpha>  Set custom Base58 alphabet for minikeys\n");
    printf("\n");
    printf("GPU OPTIONS:\n");
    printf("  -G <mode>   GPU mode: auto, on, off (default: off)\n");
    printf("\n");
    printf("ADVANCED OPTIONS:\n");
    printf("  -z <mult>   Bloom filter size multiplier (>= 1)\n");
    printf("  -W, --wizard          Interactive setup wizard\n");
    printf("  --wizard-client <hp>  Non-interactive client mode (host:port)\n");
    printf("  --config <file>       Load configuration from file\n");
    printf("  --save-config <file>  Save current configuration to file\n");
    printf("  --visual              Enhanced progress display with graphs and stats\n");
    printf("\n");
    printf("QUICK START EXAMPLES:\n");
    printf("\n");
    printf("  # Search for Bitcoin addresses in a 32-bit range:\n");
    printf("  ./keyhunt -m address -f targets.txt -r 1:FFFFFFFF\n");
    printf("\n");
    printf("  # Solve puzzle #66 with RIPEMD160 hashes:\n");
    printf("  ./keyhunt -m rmd160 -f puzzle66.rmd -b 66 -l compress -R -q -t 8\n");
    printf("\n");
    printf("  # BSGS mode for known public key:\n");
    printf("  ./keyhunt -m bsgs -f pubkey.txt -b 125 -q -S -R\n");
    printf("\n");
    printf("  # Search for vanity address starting with '1ABC':\n");
    printf("  ./keyhunt -m vanity -v 1ABC -t 4\n");
    printf("\n");
    printf("For more information, see: https://github.com/albertobsd/keyhunt\n");
    printf("\n");
    printf("Developed by AlbertoBSD\n");
    printf("Tips BTC: 1Coffee1jV4gB5gaXfHgSHDz9xx9QSECVW\n");
    printf("Thanks to Iceland for ideas and contributions.\n");
    printf("Tips to Iceland: bc1q39meky2mn5qjq704zz0nnkl0v7kj4uz6r529at\n");
    printf("\n");
    exit(EXIT_SUCCESS);
}


// ============================================================================
// Extended N value parsing (C++ only - requires Int class)
// ============================================================================

int parse_n_value_extended(const char *value_str, Int *result) {
    if (value_str == NULL || result == NULL) {
        fprintf(stderr, "[E] parse_n_value_extended: NULL parameter\n");
        return -1;
    }

    // Skip whitespace at the beginning
    while (*value_str && isspace(*value_str)) {
        value_str++;
    }

    // Check for empty string
    if (*value_str == '\0') {
        fprintf(stderr, "[E] parse_n_value_extended: Empty value string\n");
        return -1;
    }

    // Handle "0x" or "0X" prefix
    const char *hex_str = value_str;
    if (value_str[0] == '0' && (value_str[1] == 'x' || value_str[1] == 'X')) {
        hex_str = value_str + 2;  // Skip "0x" or "0X"
    }

    // Check if we have at least one hex digit after stripping prefix
    if (*hex_str == '\0') {
        fprintf(stderr, "[E] parse_n_value_extended: No hex digits after prefix\n");
        return -1;
    }

    // Validate that all characters are valid hex digits
    for (const char *p = hex_str; *p != '\0'; p++) {
        if (!isxdigit(*p)) {
            fprintf(stderr, "[E] parse_n_value_extended: Invalid hex character '%c' at position %ld\n",
                    *p, (long)(p - value_str));
            return -1;
        }
    }

    // Parse the hex string using Int::SetBase16()
    // Note: SetBase16() internally handles invalid characters and prints errors
    result->SetBase16(hex_str);

    return 0;
}


// ============================================================================
// parse_cli_args - Unified CLI parsing extracted from main()
// ============================================================================

#include "globals.h"
#include "modes/bsgs_globals.h"
#include "core/config.h"
#include "core/util.h"
#include "core/sysinfo.h"
#include "core/parameter_validator.h"
#include "error/enhanced_error.h"
#include "output.h"
#include "diagnostics/diagnostics.h"
#include "wizard/wizard.h"
#include "crypto/address_util.h"
#include "search/search_common.h"
#include "io/io.h"

/* Vanity functions (defined in search/search_vanity.cpp) */
extern int addvanity(char *target);

void parse_cli_args(int argc, char **argv) {
    char *hextemp = NULL;
    int c, index_value;
    uint64_t i;

    /* ========== Early checks (help, wizard, benchmark, diagnose) ========== */
    for (int ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--help") == 0) { menu(); }
        if (strcmp(argv[ai], "--wizard") == 0 || strcmp(argv[ai], "-W") == 0) {
            int result = wizard_run();
            exit(result < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
        }
        if (strcmp(argv[ai], "--wizard-client") == 0 && ai + 1 < argc) {
            int result = wizard_client_run_auto(argv[ai + 1]);
            exit(result < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
        }
        if (strcmp(argv[ai], "--benchmark") == 0) {
            bool submit_to_community = false;
            for (int check_i = 1; check_i < argc; check_i++) {
                if (strcmp(argv[check_i], "--submit-benchmark") == 0) {
                    submit_to_community = true;
                    break;
                }
            }
            benchmark_result_t bench_result;
            benchmark_run(&bench_result, 15, submit_to_community);
            benchmark_print_results(&bench_result, 66);
            exit(EXIT_SUCCESS);
        }
        if (strcmp(argv[ai], "--perf-compare") == 0) {
            benchmark_show_community_stats();
            exit(EXIT_SUCCESS);
        }
        if (strcmp(argv[ai], "--diagnose") == 0) {
            diagnostic_report_t report;
            diagnostics_run(&report);
            diagnostics_print_report(&report);
            exit(EXIT_SUCCESS);
        }
    }

    /* ========== Configuration file handling ========== */
    config_init(&g_ini_config);
    const char *config_file_arg = NULL;
    int new_argc = 1;
    for (int ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--config") == 0 && ai + 1 < argc) {
            config_file_arg = argv[ai + 1];
            ai++;
            continue;
        } else if (strncmp(argv[ai], "--config=", 9) == 0) {
            config_file_arg = argv[ai] + 9;
            continue;
        } else if (strcmp(argv[ai], "--save-config") == 0) {
            g_save_config_path = "keyhunt.conf";
            continue;
        } else if (strncmp(argv[ai], "--save-config=", 14) == 0) {
            g_save_config_path = argv[ai] + 14;
            continue;
        } else if (strcmp(argv[ai], "--visual") == 0) {
            FLAGVISUAL = 1;
            continue;
        }
        argv[new_argc++] = argv[ai];
    }
    argc = new_argc;
    argv[argc] = NULL;

    if (config_file_arg) {
        if (config_load(&g_ini_config, config_file_arg) == 0) {
            output_success("Loaded configuration from '%s'\n", config_file_arg);
            g_ini_config_loaded = true;
        } else {
            error_report_t report;
            error_file_io(config_file_arg, "load configuration",
                          "File not found, invalid format, or permission denied", &report);
            error_fatal(&report);
        }
    } else {
        if (config_load_default(&g_ini_config) == 0) {
            output_success("Loaded configuration from 'keyhunt.conf'\n");
            g_ini_config_loaded = true;
        }
    }

    if (g_ini_config_loaded) {
        if (g_ini_config.threads_set && g_ini_config.threads > 0) {
            NTHREADS = g_ini_config.threads;
            FLAGTHREADS = 1;
        }
        if (g_ini_config.gpu_set) {
            if (g_ini_config.gpu_enabled == 0) { FLAGGPU = 0; FLAGGPU_FULL = 0; }
            else if (g_ini_config.gpu_enabled > 0) { FLAGGPU = 1; FLAGGPU_FULL = 1; }
        }
        if (g_ini_config.mode_set) {
            if (strcasecmp(g_ini_config.mode, "hybrid") == 0) {
                FLAGGPU = 1; FLAGGPU_FULL = 1;
                FLAGGPU_HYBRID.store(1, std::memory_order_relaxed);
            } else if (strcasecmp(g_ini_config.mode, "gpu") == 0) {
                FLAGGPU = 1; FLAGGPU_FULL = 1;
            } else if (strcasecmp(g_ini_config.mode, "cpu") == 0) {
                FLAGGPU = 0; FLAGGPU_FULL = 0;
            }
        }
    }

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "-h") == 0)) {
        sysinfo_print(&g_sysinfo);
    }

    /* ========== CLI getopt loop ========== */
    optind = 1;  /* Reset getopt state */
    while ((c = getopt(argc, argv, "deh6MqRSB:b:c:C:E:f:I:k:l:m:N:n:p:r:s:t:v:G:8:z:P")) != -1) {
        switch(c) {
            case 'h': menu(); break;
            case '6': FLAGSKIPCHECKSUM = 1; output_warning("Skipping checksums on files\n"); break;
            case 'B':
                index_value = indexOf(optarg,bsgs_modes,5);
                if(index_value >= 0 && index_value <= 4) { FLAGBSGSMODE = index_value; }
                else { output_warning("Ignoring unknown BSGS mode: %s\n",optarg); }
                break;
            case 'b':
                bitrange = strtol(optarg,NULL,10);
                if(bitrange > 0 && bitrange <=256) {
                    MPZAUX.Set(&ONE); MPZAUX.ShiftL(bitrange-1);
                    bit_range_str_min = MPZAUX.GetBase16();
                    checkpointer((void *)bit_range_str_min,__FILE__,"malloc","bit_range_str_min",__LINE__-1);
                    MPZAUX.Set(&ONE); MPZAUX.ShiftL(bitrange);
                    if(MPZAUX.IsGreater(&secp->order)) { MPZAUX.Set(&secp->order); }
                    bit_range_str_max = MPZAUX.GetBase16();
                    checkpointer((void *)bit_range_str_max,__FILE__,"malloc","bit_range_str_min",__LINE__-1);
                    FLAGBITRANGE = 1;
                } else { output_error("invalid bits param: %s.\n",optarg); }
                break;
            case 'c':
                index_value = indexOf(optarg,cryptos,3);
                switch(index_value) {
                    case 0: FLAGCRYPTO = CRYPTO_BTC; break;
                    case 1: FLAGCRYPTO = CRYPTO_ETH; output_success("Setting search for ETH address\n"); break;
                    default: FLAGCRYPTO = CRYPTO_NONE; output_error("Unknown crypto value %s\n",optarg); exit(EXIT_FAILURE); break;
                }
                break;
            case 'C':
                if(strlen(optarg) == 22) {
                    FLAGBASEMINIKEY = 1;
                    str_baseminikey = (char*) malloc(23);
                    checkpointer((void *)str_baseminikey,__FILE__,"malloc","str_baseminikey",__LINE__-1);
                    raw_baseminikey = (char*) malloc(23);
                    checkpointer((void *)raw_baseminikey,__FILE__,"malloc","raw_baseminikey",__LINE__-1);
                    strncpy(str_baseminikey,optarg,22); str_baseminikey[22] = '\0';
                    for(i = 0; i< 21; i++) {
                        if(strchr(Ccoinbuffer,str_baseminikey[i+1]) != NULL) {
                            raw_baseminikey[i] = (int)(strchr(Ccoinbuffer,str_baseminikey[i+1]) - Ccoinbuffer) % 58;
                        } else { output_error("invalid character in minikey\n"); exit(EXIT_FAILURE); }
                    }
                    raw_baseminikey[21] = '\0';
                } else { output_error("Invalid Minikey length %zu : %s\n",strlen(optarg),optarg); exit(EXIT_FAILURE); }
                break;
            case 'd': FLAGDEBUG = 1; output_success("Flag DEBUG enabled\n"); break;
            case 'e':
                FLAGENDOMORPHISM = 1; output_success("Endomorphism enabled\n");
                lambda.SetBase16("5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
                lambda2.SetBase16("ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");
                beta.SetBase16("7ae96a2b657c07106e64479eac3434e99cf0497512f58995c1396c28719501ee");
                beta2.SetBase16("851695d49a83f8ef919bb86153cbcb16630fb68aed0a766a3ec693d68e6afa40");
                break;
            case 'f': FLAGFILE = 1; g_fileName = optarg; break;
            case 'G':
                if (optarg) {
                    if (strcasecmp(optarg, "off") == 0 || strcasecmp(optarg, "no") == 0 || strcmp(optarg, "0") == 0) {
                        FLAGGPU = 0; FLAGGPU_FULL = 0;
                    } else if (strcasecmp(optarg, "auto") == 0) {
                        FLAGGPU = -1; FLAGGPU_FULL = -1;
                    } else if (strcasecmp(optarg, "hash") == 0) {
                        FLAGGPU = 1; FLAGGPU_FULL = 0;
                        output_success("GPU hash-only mode (CPU generates points, GPU hashes)\n");
                    } else if (strcasecmp(optarg, "full") == 0 || strcasecmp(optarg, "on") == 0 || strcasecmp(optarg, "yes") == 0 || strcmp(optarg, "1") == 0) {
                        FLAGGPU = 1; FLAGGPU_FULL = 1;
                        output_success("GPU full mode (ECC + hash160 + matching on GPU)\n");
                    } else if (strcasecmp(optarg, "hybrid") == 0) {
                        FLAGGPU = 1; FLAGGPU_FULL = 1;
                        FLAGGPU_HYBRID.store(1, std::memory_order_relaxed);
                        output_success("GPU hybrid mode (GPU + CPU in parallel for maximum throughput)\n");
                    } else {
                        output_warning("Invalid -G value '%s', use: off|auto|hash|full|hybrid\n", optarg);
                    }
                }
                break;
            case 'I': FLAGSTRIDE = 1; str_stride = optarg; break;
            case 'k':
                KFACTOR = (int)strtol(optarg,NULL,10);
                if(KFACTOR <= 0) { KFACTOR = 1; }
                output_success("K factor %i\n",KFACTOR);
                break;
            case 'l':
                switch(indexOf(optarg,publicsearch,3)) {
                    case SEARCH_UNCOMPRESS: FLAGSEARCH = SEARCH_UNCOMPRESS; output_success("Search uncompressed keys only\n"); break;
                    case SEARCH_COMPRESS: FLAGSEARCH = SEARCH_COMPRESS; output_success("Search compressed keys only\n"); break;
                    case SEARCH_BOTH: FLAGSEARCH = SEARCH_BOTH; output_success("Search both compressed and uncompressed keys\n"); break;
                }
                break;
            case 'M': FLAGMATRIX = 1; output_success("Matrix screen\n"); break;
            case 'P': FLAGPROGRESSBAR = 1; output_success("Segmented progress indicator enabled\n"); break;
            case 'm':
                switch(indexOf(optarg,modes,7)) {
                    case MODE_XPOINT: FLAGMODE = MODE_XPOINT; output_success("Mode xpoint\n"); break;
                    case MODE_ADDRESS: FLAGMODE = MODE_ADDRESS; output_success("Mode address\n"); break;
                    case MODE_BSGS: FLAGMODE = MODE_BSGS; break;
                    case MODE_RMD160: FLAGMODE = MODE_RMD160; FLAGCRYPTO = CRYPTO_BTC; output_success("Mode rmd160\n"); break;
                    case MODE_PUB2RMD: FLAGMODE = MODE_PUB2RMD; output_success("Mode pub2rmd was removed\n"); exit(0); break;
                    case MODE_MINIKEYS: FLAGMODE = MODE_MINIKEYS; output_success("Mode minikeys\n"); break;
                    case MODE_VANITY:
                        FLAGMODE = MODE_VANITY; output_success("Mode vanity\n");
                        if(vanity_bloom == NULL){
                            vanity_bloom = (struct bloom*) calloc(1,sizeof(struct bloom));
                            checkpointer((void *)vanity_bloom,__FILE__,"calloc","vanity_bloom",__LINE__-1);
                        }
                        break;
                    default: output_error("Unknown mode value %s\n",optarg); exit(EXIT_FAILURE); break;
                }
                break;
            case 'n': FLAG_N = 1; str_N = optarg; break;
            case 'q': FLAGQUIET = 1; break;
            case 'R': output_success("Random mode\n"); FLAGRANDOM = 1; FLAGBSGSMODE = 3; break;
            case 'r':
                if(optarg != NULL) {
                    Tokenizer t{};
                    stringtokenizer(optarg,&t);
                    switch(t.n) {
                        case 1:
                            range_start = nextToken(&t);
                            if(isValidHex(range_start)) { FLAGRANGE = 1; range_end = secp->order.GetBase16(); }
                            else { output_error("Invalid hex string:%s.\n",range_start); }
                            break;
                        case 2:
                            range_start = nextToken(&t);
                            range_end = nextToken(&t);
                            if(isValidHex(range_start) && isValidHex(range_end)) { FLAGRANGE = 1; }
                            else { if(isValidHex(range_start)) { output_error("Invalid hex string:%s\n",range_start); } else { output_error("Invalid hex string:%s\n",range_end); } }
                            break;
                        default: output_error("Unknown number of Range Params: %i\n",t.n); break;
                    }
                }
                break;
            case 's':
                OUTPUTSECONDS.SetBase10(optarg);
                if(OUTPUTSECONDS.IsLower(&ZERO)) { OUTPUTSECONDS.SetInt32(30); }
                if(OUTPUTSECONDS.IsZero()) { output_success("Turn off stats output\n"); }
                else { hextemp = OUTPUTSECONDS.GetBase10(); output_success("Stats output every %s seconds\n",hextemp); free(hextemp); }
                break;
            case 'S': FLAGSAVEREADFILE = 1; break;
            case 't':
                NTHREADS = strtol(optarg,NULL,10);
                if(NTHREADS <= 0) { NTHREADS = 1; }
                FLAGTHREADS = 1;
                output_success((NTHREADS > 1) ? "Threads : %u (user-specified)\n": "Thread : %u (user-specified)\n",NTHREADS);
                break;
            case 'v':
                FLAGVANITY = 1;
                if(vanity_bloom == NULL){
                    vanity_bloom = (struct bloom*) calloc(1,sizeof(struct bloom));
                    checkpointer((void *)vanity_bloom,__FILE__,"calloc","vanity_bloom",__LINE__-1);
                }
                if(isValidBase58String(optarg)) {
                    if(addvanity(optarg) > 0) { output_success("Added Vanity search : %s\n",optarg); }
                    else { output_success("Vanity search \"%s\" was NOT Added\n",optarg); }
                } else { output_success("The string \"%s\" is not Valid Base58\n",optarg); }
                break;
            case '8':
                if(strlen(optarg) == 58) { Ccoinbuffer = optarg; output_success("Base58 for Minikeys %s\n",Ccoinbuffer); }
                else { output_error("The base58 alphabet must be 58 characters long.\n"); exit(EXIT_FAILURE); }
                break;
            case 'z':
                FLAGBLOOMMULTIPLIER = strtol(optarg,NULL,10);
                if(FLAGBLOOMMULTIPLIER <= 0) { FLAGBLOOMMULTIPLIER = 1; }
                output_success("Bloom Size Multiplier %i\n",FLAGBLOOMMULTIPLIER);
                break;
            default: output_error("Unknown option -%c\n",c); exit(EXIT_FAILURE); break;
        }
    }

    output_init(FLAGQUIET ? OUTPUT_MINIMAL : OUTPUT_NORMAL);

    /* ========== Parameter Validation ========== */
    {
        uint64_t user_n_value = 0;
        if (FLAG_N && str_N) {
            if (str_N[0] == '0' && str_N[1] == 'x') { user_n_value = strtoull(str_N + 2, NULL, 16); }
            else { user_n_value = strtoull(str_N, NULL, 10); }
        }
        int threads_to_validate = FLAGTHREADS ? NTHREADS : 0;
        uint32_t batch_size = CPU_GRP_SIZE;
        bool validation_ok = validate_all_parameters(
            &threads_to_validate, &user_n_value, &KFACTOR, &batch_size, &g_sysinfo, true);
        NTHREADS = threads_to_validate;
        CPU_GRP_SIZE = batch_size;
        if (FLAG_N && user_n_value != 0) {
            char corrected_n[32];
            snprintf(corrected_n, sizeof(corrected_n), "0x%llx", (unsigned long long)user_n_value);
            str_N = strdup(corrected_n);
            if (str_N == NULL) { output_error("Memory allocation failed for N parameter\n"); exit(EXIT_FAILURE); }
        }
        if (!FLAG_N && FLAGMODE == MODE_BSGS && OPTIMAL_N > 0) {
            char auto_n[32];
            snprintf(auto_n, sizeof(auto_n), "0x%llx", (unsigned long long)OPTIMAL_N);
            str_N = strdup(auto_n);
            if (str_N == NULL) { output_error("Memory allocation failed for N parameter\n"); exit(EXIT_FAILURE); }
            FLAG_N = 1;
        }
        if (!validation_ok) { output_warning("Some parameters were auto-corrected for safety\n"); }
    }

    /* Display configuration summary */
    {
        const char *mode_name = (FLAGMODE >= 0 && FLAGMODE < 7) ? modes[FLAGMODE] : "unknown";
        const char *gpu_name = NULL;
        if (FLAGGPU || FLAGGPU_HYBRID) {
            gpu_name = g_gpu_backend_info.name[0] ? g_gpu_backend_info.name : "GPU";
        }
        output_banner(version, mode_name, NTHREADS, gpu_name, bitrange);
    }

    /* Save configuration if requested */
    if (g_save_config_path) {
        if (NTHREADS > 0) { g_ini_config.threads = NTHREADS; g_ini_config.threads_set = true; }
        if (FLAGGPU_HYBRID) { strncpy(g_ini_config.mode, "hybrid", sizeof(g_ini_config.mode) - 1); }
        else if (FLAGGPU && FLAGGPU_FULL) { strncpy(g_ini_config.mode, "gpu", sizeof(g_ini_config.mode) - 1); }
        else { strncpy(g_ini_config.mode, "cpu", sizeof(g_ini_config.mode) - 1); }
        g_ini_config.mode_set = true;
        g_ini_config.gpu_enabled = FLAGGPU ? 1 : 0; g_ini_config.gpu_set = true;
        g_ini_config.batch_size = CPU_GRP_SIZE; g_ini_config.batch_size_set = true;
        if (config_save(&g_ini_config, g_save_config_path) == 0) {
            output_success("Configuration saved. You can now use it with: --config %s\n", g_save_config_path);
        } else { output_error("Failed to save configuration to %s. Check disk space and permissions.\n", g_save_config_path); }
    }

    if (FLAGBSGSMODE == MODE_BSGS && FLAGENDOMORPHISM) {
        output_error("Endomorphism doesn't work with BSGS\n"); exit(EXIT_FAILURE);
    }
    if (FLAGBSGSMODE == MODE_BSGS && FLAGSTRIDE) {
        output_error("Stride doesn't work with BSGS\n"); exit(EXIT_FAILURE);
    }
    if(FLAGSTRIDE) {
        if(str_stride[0] == '0' && str_stride[1] == 'x') { stride.SetBase16(str_stride+2); }
        else { stride.SetBase10(str_stride); }
        output_success("Stride : %s\n",stride.GetBase10());
    } else {
        FLAGSTRIDE = 1;
        stride.Set(&ONE);
    }
    init_generator();
    if(FLAGMODE == MODE_BSGS) { output_success("Mode BSGS %s\n",bsgs_modes[FLAGBSGSMODE]); }
    if(FLAGFILE == 0) { g_fileName = (char*) default_fileName; }

    if(FLAGMODE == MODE_ADDRESS && FLAGCRYPTO == CRYPTO_NONE) {
        FLAGCRYPTO = CRYPTO_BTC; output_success("Setting search for BTC address\n");
    }
    if(FLAGMODE == MODE_RMD160 && FLAGCRYPTO == CRYPTO_NONE) {
        FLAGCRYPTO = CRYPTO_BTC; output_success("Setting search for btc rmd160\n");
    }
}

/* ============================================================================
 * setup_search_range - Resolve range start/end from CLI flags
 *
 * Extracted from keyhunt.cpp main() (Phase 4, Plan 10) to reduce monolith.
 * Sets n_range_start, n_range_end, n_range_diff from FLAGRANGE/FLAGBITRANGE.
 * ============================================================================ */

void setup_search_range(void) {
    extern Secp256K1 *secp;

    if(FLAGRANGE) {
        n_range_start.SetBase16(range_start);
        n_range_end.SetBase16(range_end);
        if(n_range_start.IsZero()) n_range_start.AddOne();
        if(n_range_end.IsZero()) { output_error("End range can't be zero\nFallback to random mode!\n"); FLAGRANGE = 0; }
        if(FLAGRANGE) {
            if(n_range_start.IsGreater(&n_range_end)) {
                output_warning("Opps, start range can't be great than end range. Swapping them\n");
                n_range_aux.Set(&n_range_start); n_range_start.Set(&n_range_end); n_range_end.Set(&n_range_aux);
                if(n_range_start.IsZero()) n_range_start.AddOne();
            }
            if(n_range_start.IsLower(&secp->order) && n_range_end.IsLowerOrEqual(&secp->order)) {
                if (n_range_end.IsLower(&secp->order)) n_range_end.AddOne();
                else n_range_end.Set(&secp->order);
                n_range_diff.Set(&n_range_end); n_range_diff.Sub(&n_range_start);
            } else { output_error("Start and End range can't be great than N\nFallback to random mode!\n"); FLAGRANGE = 0; }
        }
    }
    if(FLAGMODE != MODE_BSGS && FLAGMODE != MODE_MINIKEYS) {
        if(DEBUGCOUNT == 0) DEBUGCOUNT = 1024;
        BSGS_N.SetInt32(DEBUGCOUNT);
        if(FLAGRANGE == 0 && FLAGBITRANGE == 0) {
            n_range_start.SetInt32(1); n_range_end.Set(&secp->order);
            n_range_diff.Set(&n_range_end); n_range_diff.Sub(&n_range_start);
        } else {
            if(FLAGBITRANGE) {
                n_range_start.SetBase16(bit_range_str_min); n_range_end.SetBase16(bit_range_str_max);
                n_range_diff.Set(&n_range_end); n_range_diff.Sub(&n_range_start);
            } else { if(FLAGRANGE == 0) output_warning("WTF!\n"); }
        }
    }
}
