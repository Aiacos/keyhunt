// src/cli.cpp
// CLI argument parsing implementation for keyhunt

#include "cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <strings.h>  // for strcasecmp on Linux

// Mode name strings
static const char *mode_names[] = {
    "address", "bsgs", "xpoint", "rmd160", "vanity", "pub2rmd", "minikeys"
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
    if (mode >= MODE_ADDRESS && mode <= MODE_MINIKEYS) {
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
    for (int i = 0; i <= MODE_MINIKEYS; i++) {
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
