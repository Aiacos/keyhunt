// src/cli.cpp
// CLI argument parsing implementation for keyhunt

#include "cli.h"
#include "config/config.h"
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

static void cli_set_defaults(cli_args_t *args) {
    if (args == NULL) return;

    memset(args, 0, sizeof(*args));

    args->mode = MODE_ADDRESS;
    args->threads = 0;  // 0 means auto-detect
    args->threads_specified = false;
    args->key_type = KEYTYPE_COMPRESSED;
    args->gpu_mode = GPU_OFF;
    args->bsgs_mode = BSGS_RANDOM;
    args->k_factor = 1;
    args->random_mode = true;
    args->status_interval = 10;
    args->crypto_type = 1;  // BTC
    args->bloom_multiplier = 1;

    strncpy(args->range_start, "1", sizeof(args->range_start) - 1);
    strncpy(args->range_end, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364140",
            sizeof(args->range_end) - 1);
}

static int parse_mode(const char *str) {
    if (str == NULL) return -1;
    for (int i = 0; i <= MODE_VANITY; i++) {
        if (strcasecmp(str, mode_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int parse_keytype(const char *str) {
    if (str == NULL) return -1;
    for (int i = 0; i <= KEYTYPE_BOTH; i++) {
        if (strcasecmp(str, keytype_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int parse_bsgs_mode(const char *str) {
    if (str == NULL) return -1;
    for (int i = 0; i <= BSGS_DANCE; i++) {
        if (strcasecmp(str, bsgs_mode_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int parse_gpu_mode(const char *str) {
    if (str == NULL) return -1;
    if (strcasecmp(str, "off") == 0) return GPU_OFF;
    if (strcasecmp(str, "on") == 0) return GPU_ON;
    if (strcasecmp(str, "auto") == 0) return GPU_AUTO;
    if (strcasecmp(str, "hybrid") == 0) return GPU_HYBRID;
    return -1;
}

int cli_parse(int argc, char **argv, cli_args_t *args) {
    if (args == NULL) return -1;

    cli_set_defaults(args);

    // Check for long options first
    for (int i = 1; i < argc; i++) {
        if (argv[i] == NULL) continue;

        if (strcmp(argv[i], "--help") == 0) {
            return 1;  // Signal to show help
        }
        if (strcmp(argv[i], "--wizard") == 0 || strcmp(argv[i], "-W") == 0) {
            args->run_wizard = true;
            return 0;
        }
        if (strcmp(argv[i], "--benchmark") == 0) {
            args->run_benchmark = true;
            return 0;
        }
        if (strcmp(argv[i], "--wizard-client") == 0 && i + 1 < argc) {
            strncpy(args->wizard_client, argv[i + 1], sizeof(args->wizard_client) - 1);
            args->wizard_client[sizeof(args->wizard_client) - 1] = '\0';
            return 0;
        }
    }

    // Reset getopt state
    optind = 1;

    int c;
    while ((c = getopt(argc, argv, "deh6MqRSB:b:c:C:E:f:I:k:l:m:N:n:p:r:s:t:v:G:8:z:PW")) != -1) {
        switch (c) {
            case 'h':
                return 1;  // Show help

            case 'f':
                if (optarg != NULL) {
                    strncpy(args->target_file, optarg, sizeof(args->target_file) - 1);
                    args->target_file[sizeof(args->target_file) - 1] = '\0';
                }
                break;

            case 'm': {
                int mode = parse_mode(optarg);
                if (mode < 0) {
                    fprintf(stderr, "[E] Unknown mode: %s\n", optarg ? optarg : "(null)");
                    return -1;
                }
                args->mode = (search_mode_t)mode;
                break;
            }

            case 'b':
                if (optarg != NULL) {
                    char *endptr;
                    long val = strtol(optarg, &endptr, 10);
                    if (endptr == optarg || *endptr != '\0' || val <= 0 || val > 256) {
                        fprintf(stderr, "[E] Invalid bits value: %s\n", optarg);
                        return -1;
                    }
                    args->bits = (int)val;
                }
                break;

            case 't':
                if (optarg != NULL) {
                    char *endptr;
                    long val = strtol(optarg, &endptr, 10);
                    if (endptr != optarg && (*endptr == '\0' || *endptr == ' ')) {
                        args->threads = (int)val;
                        args->threads_specified = true;
                    }
                }
                break;

            case 'l': {
                int kt = parse_keytype(optarg);
                if (kt < 0) {
                    fprintf(stderr, "[E] Unknown key type: %s\n", optarg ? optarg : "(null)");
                    return -1;
                }
                args->key_type = (key_type_t)kt;
                break;
            }

            case 'G': {
                int gm = parse_gpu_mode(optarg);
                if (gm < 0) {
                    fprintf(stderr, "[E] Unknown GPU mode: %s\n", optarg ? optarg : "(null)");
                    return -1;
                }
                args->gpu_mode = (gpu_mode_t)gm;
                break;
            }

            case 'B': {
                int bm = parse_bsgs_mode(optarg);
                if (bm < 0) {
                    fprintf(stderr, "[E] Unknown BSGS mode: %s\n", optarg ? optarg : "(null)");
                    return -1;
                }
                args->bsgs_mode = (bsgs_mode_t)bm;
                break;
            }

            case 'r': {
                if (optarg != NULL) {
                    // Make a copy since we'll modify it
                    char range_copy[136];
                    strncpy(range_copy, optarg, sizeof(range_copy) - 1);
                    range_copy[sizeof(range_copy) - 1] = '\0';

                    char *colon = strchr(range_copy, ':');
                    if (colon) {
                        *colon = '\0';
                        strncpy(args->range_start, range_copy, sizeof(args->range_start) - 1);
                        args->range_start[sizeof(args->range_start) - 1] = '\0';
                        strncpy(args->range_end, colon + 1, sizeof(args->range_end) - 1);
                        args->range_end[sizeof(args->range_end) - 1] = '\0';
                    } else {
                        strncpy(args->range_start, range_copy, sizeof(args->range_start) - 1);
                        args->range_start[sizeof(args->range_start) - 1] = '\0';
                    }
                }
                break;
            }

            case 'n':
            case 'N':
                if (optarg != NULL) {
                    strncpy(args->n_value, optarg, sizeof(args->n_value) - 1);
                    args->n_value[sizeof(args->n_value) - 1] = '\0';
                }
                break;

            case 'k':
                if (optarg != NULL) {
                    char *endptr;
                    long val = strtol(optarg, &endptr, 10);
                    if (endptr != optarg && (*endptr == '\0' || *endptr == ' ') && val > 0) {
                        args->k_factor = (int)val;
                    }
                }
                break;

            case 'R':
                args->random_mode = true;
                break;

            case 'q':
                args->quiet_mode = true;
                break;

            case 's':
                if (optarg != NULL) {
                    char *endptr;
                    long val = strtol(optarg, &endptr, 10);
                    if (endptr != optarg && (*endptr == '\0' || *endptr == ' ') && val >= 0) {
                        args->status_interval = (int)val;
                    }
                }
                break;

            case 'S':
                args->save_bloom = true;
                break;

            case 'e':
                args->endomorphism = true;
                break;

            case 'M':
                args->matrix_mode = true;
                break;

            case 'P':
                args->show_progress_bar = true;
                break;

            case '6':
                args->skip_checksum = true;
                break;

            case 'd':
                // Debug mode - could add debug flag if needed
                break;

            case 'c':
                if (optarg != NULL) {
                    if (strcasecmp(optarg, "btc") == 0) {
                        args->crypto_type = 1;
                    } else if (strcasecmp(optarg, "eth") == 0) {
                        args->crypto_type = 2;
                    }
                }
                break;

            case 'v':
                if (optarg != NULL) {
                    strncpy(args->vanity_pattern, optarg, sizeof(args->vanity_pattern) - 1);
                    args->vanity_pattern[sizeof(args->vanity_pattern) - 1] = '\0';
                }
                break;

            case 'I':
                if (optarg != NULL) {
                    strncpy(args->stride, optarg, sizeof(args->stride) - 1);
                    args->stride[sizeof(args->stride) - 1] = '\0';
                }
                break;

            case 'z':
                if (optarg != NULL) {
                    char *endptr;
                    long val = strtol(optarg, &endptr, 10);
                    if (endptr != optarg && (*endptr == '\0' || *endptr == ' ') && val >= 1) {
                        args->bloom_multiplier = (int)val;
                    }
                }
                break;

            case 'C':
                if (optarg != NULL) {
                    strncpy(args->minikey_base, optarg, sizeof(args->minikey_base) - 1);
                    args->minikey_base[sizeof(args->minikey_base) - 1] = '\0';
                }
                break;

            case '8':
                if (optarg != NULL) {
                    strncpy(args->base58_alphabet, optarg, sizeof(args->base58_alphabet) - 1);
                    args->base58_alphabet[sizeof(args->base58_alphabet) - 1] = '\0';
                }
                break;

            case 'p':
                if (optarg != NULL) {
                    strncpy(args->output_file, optarg, sizeof(args->output_file) - 1);
                    args->output_file[sizeof(args->output_file) - 1] = '\0';
                }
                break;

            case 'E':
                if (optarg != NULL) {
                    strncpy(args->config_file, optarg, sizeof(args->config_file) - 1);
                    args->config_file[sizeof(args->config_file) - 1] = '\0';
                }
                break;

            case 'W':
                args->run_wizard = true;
                break;

            case '?':
            default:
                // getopt prints its own error message
                return -1;
        }
    }

    return 0;
}

int cli_validate(cli_args_t *args) {
    if (args == NULL) return -1;

    // Target file required for most modes
    if (!args->run_wizard && !args->run_benchmark &&
        args->wizard_client[0] == '\0' && args->target_file[0] == '\0') {
        fprintf(stderr, "[E] Target file required (-f)\n");
        return -1;
    }

    // Validate bits if specified
    if (args->bits < 0 || args->bits > 256) {
        fprintf(stderr, "[E] Bits must be between 1 and 256\n");
        return -1;
    }

    // Validate threads
    if (args->threads < 0) {
        fprintf(stderr, "[E] Threads must be positive\n");
        return -1;
    }

    // Validate k_factor
    if (args->k_factor < 1) {
        fprintf(stderr, "[E] K factor must be at least 1\n");
        return -1;
    }

    // Validate status interval
    if (args->status_interval < 0) {
        fprintf(stderr, "[E] Status interval must be non-negative\n");
        return -1;
    }

    return 0;
}

void cli_print(const cli_args_t *args) {
    if (args == NULL) {
        printf("cli_args_t: NULL\n");
        return;
    }

    printf("Parsed Arguments:\n");
    printf("  Mode: %s\n", cli_mode_name(args->mode));
    printf("  Target file: %s\n", args->target_file[0] ? args->target_file : "(none)");
    printf("  Output file: %s\n", args->output_file[0] ? args->output_file : "(none)");
    printf("  Config file: %s\n", args->config_file[0] ? args->config_file : "(none)");
    printf("  Range: %s : %s\n", args->range_start, args->range_end);
    printf("  Bits: %d\n", args->bits);
    printf("  Threads: %d%s\n", args->threads,
           args->threads_specified ? "" : " (auto)");
    printf("  Key type: %s\n", cli_keytype_name(args->key_type));
    printf("  GPU mode: %s\n", cli_gpu_mode_name(args->gpu_mode));
    printf("  BSGS mode: %s\n", cli_bsgs_mode_name(args->bsgs_mode));
    printf("  N value: %s\n", args->n_value[0] ? args->n_value : "(default)");
    printf("  K factor: %d\n", args->k_factor);
    printf("  Random mode: %s\n", args->random_mode ? "yes" : "no");
    printf("  Quiet mode: %s\n", args->quiet_mode ? "yes" : "no");
    printf("  Status interval: %d\n", args->status_interval);
    printf("  Endomorphism: %s\n", args->endomorphism ? "yes" : "no");
    printf("  Stride: %s\n", args->stride[0] ? args->stride : "(none)");
    printf("  Bloom multiplier: %d\n", args->bloom_multiplier);
    printf("  Crypto type: %d (%s)\n", args->crypto_type,
           args->crypto_type == 1 ? "BTC" : args->crypto_type == 2 ? "ETH" : "other");
    printf("  Save bloom: %s\n", args->save_bloom ? "yes" : "no");
    printf("  Skip checksum: %s\n", args->skip_checksum ? "yes" : "no");
    printf("  Matrix mode: %s\n", args->matrix_mode ? "yes" : "no");
    printf("  Progress bar: %s\n", args->show_progress_bar ? "yes" : "no");
    printf("  Run wizard: %s\n", args->run_wizard ? "yes" : "no");
    printf("  Run benchmark: %s\n", args->run_benchmark ? "yes" : "no");
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
// Config population
// ============================================================================

int cli_populate_config(const cli_args_t *args, void *cfg_ptr) {
    if (args == NULL || cfg_ptr == NULL) {
        return -1;
    }

    // Cast void* to keyhunt_config_t* (full type available via config.h include)
    keyhunt_config_t *cfg = (keyhunt_config_t *)cfg_ptr;

    // Initialize config with defaults first
    kh_config_init(cfg);

    // ========================================================================
    // Populate search_config_t
    // ========================================================================

    // Mode and type settings
    cfg->search.mode = args->mode;
    cfg->search.key_format = args->key_type;

    // Map crypto_type (1=BTC, 2=ETH) to crypto_type_t enum
    if (args->crypto_type == 1) {
        cfg->search.crypto_type = CRYPTO_TYPE_BTC;
    } else if (args->crypto_type == 2) {
        cfg->search.crypto_type = CRYPTO_TYPE_ETH;
    } else {
        cfg->search.crypto_type = CRYPTO_TYPE_ALL;
    }

    // Range specification
    strncpy(cfg->search.range_start, args->range_start,
            sizeof(cfg->search.range_start) - 1);
    cfg->search.range_start[sizeof(cfg->search.range_start) - 1] = '\0';

    strncpy(cfg->search.range_end, args->range_end,
            sizeof(cfg->search.range_end) - 1);
    cfg->search.range_end[sizeof(cfg->search.range_end) - 1] = '\0';

    cfg->search.bit_range = args->bits;

    // Stride
    if (args->stride[0] != '\0') {
        strncpy(cfg->search.stride, args->stride,
                sizeof(cfg->search.stride) - 1);
        cfg->search.stride[sizeof(cfg->search.stride) - 1] = '\0';
        cfg->search.stride_enabled = true;
        cfg->explicitly_set.stride = true;
    } else {
        cfg->search.stride_enabled = false;
    }

    // Target file
    if (args->target_file[0] != '\0') {
        strncpy(cfg->search.target_file, args->target_file,
                sizeof(cfg->search.target_file) - 1);
        cfg->search.target_file[sizeof(cfg->search.target_file) - 1] = '\0';
        cfg->explicitly_set.target_file = true;
    }

    // Search flags
    cfg->search.random_mode = args->random_mode;
    cfg->search.endomorphism = args->endomorphism;
    cfg->search.quiet_mode = args->quiet_mode;
    cfg->search.debug_mode = false;  // Set from -d if needed
    cfg->search.matrix_mode = args->matrix_mode;
    cfg->search.progress_bar = args->show_progress_bar;

    // ========================================================================
    // Populate bsgs_config_t
    // ========================================================================

    cfg->bsgs.k_factor = args->k_factor;
    cfg->bsgs.bsgs_mode = args->bsgs_mode;
    cfg->bsgs.bloom_multiplier = args->bloom_multiplier;
    cfg->bsgs.save_progress = args->save_bloom;
    cfg->bsgs.load_precalc = false;  // Set when loading precalc files

    // N value (if provided)
    if (args->n_value[0] != '\0') {
        // Parse n_value string to uint64_t
        // For now, store as string and let config validation handle it
        cfg->explicitly_set.n_value = true;
        // Note: Actual n_value parsing happens in config validation
    }

    // ========================================================================
    // Populate gpu_config_t
    // ========================================================================

    // Map gpu_mode_t enum to gpu.enabled integer
    switch (args->gpu_mode) {
        case GPU_OFF:
            cfg->gpu.enabled = 0;
            break;
        case GPU_ON:
            cfg->gpu.enabled = 1;
            cfg->explicitly_set.gpu = true;
            break;
        case GPU_AUTO:
            cfg->gpu.enabled = -1;
            break;
        case GPU_HYBRID:
            cfg->gpu.enabled = 1;
            cfg->gpu.hybrid_mode = true;
            cfg->explicitly_set.gpu = true;
            break;
        default:
            cfg->gpu.enabled = 0;
            break;
    }

    // GPU defaults are already set by kh_config_init()
    cfg->gpu.full_mode = false;  // Can be set by additional flags
    cfg->gpu.device_count = 0;   // Will be auto-detected if GPU enabled

    // ========================================================================
    // Populate runtime_state_t
    // ========================================================================

    // Thread count
    if (args->threads_specified) {
        cfg->runtime.num_threads = args->threads;
        cfg->explicitly_set.threads = true;
    } else {
        cfg->runtime.num_threads = 0;  // 0 means auto-detect
    }

    // Output settings
    cfg->runtime.output_interval_sec = args->status_interval;

    if (args->output_file[0] != '\0') {
        strncpy(cfg->runtime.output_file, args->output_file,
                sizeof(cfg->runtime.output_file) - 1);
        cfg->runtime.output_file[sizeof(cfg->runtime.output_file) - 1] = '\0';
    }

    // ========================================================================
    // Set explicitly_set flags
    // ========================================================================

    cfg->explicitly_set.mode = true;  // Mode always explicitly set

    if (args->bits > 0) {
        cfg->explicitly_set.bit_range = true;
    }

    if (args->range_start[0] != '1' || args->range_end[0] != 'F') {
        cfg->explicitly_set.range = true;
    }

    if (args->k_factor != 1) {
        cfg->explicitly_set.k_factor = true;
    }

    // ========================================================================
    // Legacy INI config path (if provided)
    // ========================================================================

    if (args->config_file[0] != '\0') {
        strncpy(cfg->ini_config_path, args->config_file,
                sizeof(cfg->ini_config_path) - 1);
        cfg->ini_config_path[sizeof(cfg->ini_config_path) - 1] = '\0';
        // Note: INI loading happens separately
    }

    return 0;
}
