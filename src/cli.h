// src/cli.h
// CLI argument parsing module for keyhunt
// Provides enums, structures, and functions for command line argument handling

#ifndef CLI_H
#define CLI_H

#include <stdint.h>
#include <stdbool.h>

/* Shared buffer size constants */
#define KH_PATH_BUF_SIZE    512
#define KH_RANGE_BUF_SIZE   128
#define KH_NAME_BUF_SIZE    68

#ifdef __cplusplus
extern "C" {
#endif

// Search modes (aligned with search_common.h legacy defines)
typedef enum {
    MODE_XPOINT = 0,
    MODE_ADDRESS,
    MODE_BSGS,
    MODE_RMD160,
    MODE_PUB2RMD,
    MODE_MINIKEYS,
    MODE_VANITY
} search_mode_t;

// Key types
typedef enum {
    KEYTYPE_COMPRESSED = 0,
    KEYTYPE_UNCOMPRESSED,
    KEYTYPE_BOTH
} key_type_t;

// GPU modes
typedef enum {
    GPU_OFF = 0,
    GPU_ON,
    GPU_AUTO,
    GPU_HYBRID
} gpu_mode_t;

// BSGS modes
typedef enum {
    BSGS_SEQUENTIAL = 0,
    BSGS_BACKWARD,
    BSGS_BOTH,
    BSGS_RANDOM,
    BSGS_DANCE
} bsgs_mode_t;

// Command line arguments structure
typedef struct {
    // Mode
    search_mode_t mode;

    // Files
    char target_file[256];
    char output_file[256];
    char config_file[256];

    // Range
    char range_start[68];
    char range_end[68];
    int bits;

    // Threading
    int threads;
    bool threads_specified;

    // Key type
    key_type_t key_type;

    // GPU
    gpu_mode_t gpu_mode;

    // BSGS specific
    bsgs_mode_t bsgs_mode;
    char n_value[68];
    int k_factor;
    bool save_bloom;

    // Search options
    bool random_mode;
    bool quiet_mode;
    int status_interval;
    bool endomorphism;
    char stride[68];
    int bloom_multiplier;

    // Crypto
    int crypto_type;  // 1=BTC, 2=ETH

    // Vanity
    char vanity_pattern[64];

    // Minikeys
    char minikey_base[64];
    char base58_alphabet[64];

    // Flags
    bool skip_checksum;
    bool matrix_mode;
    bool show_progress_bar;

    // Special modes
    bool run_wizard;
    bool run_benchmark;
    char wizard_client[128];

} cli_args_t;

// Parse command line arguments
// Returns: 0 on success, 1 to show help, -1 on error
int cli_parse(int argc, char **argv, cli_args_t *args);

// Validate parsed arguments
// Returns: 0 on success, -1 on error
int cli_validate(cli_args_t *args);

// Print parsed arguments (for debugging)
void cli_print(const cli_args_t *args);

// Get mode name string
const char *cli_mode_name(search_mode_t mode);

// Get key type name string
const char *cli_keytype_name(key_type_t type);

// Get BSGS mode name string
const char *cli_bsgs_mode_name(bsgs_mode_t mode);

// Get GPU mode name string
const char *cli_gpu_mode_name(gpu_mode_t mode);

// Populate keyhunt config from parsed CLI arguments
// Note: cfg parameter type is defined in config/config.h (keyhunt_config_t)
// To avoid circular dependency, we use void* here. Cast to keyhunt_config_t* when calling.
// Returns: 0 on success, -1 on error
int cli_populate_config(const cli_args_t *args, void *cfg);

#ifdef __cplusplus
}

// C++ only functions (requires Int class from secp256k1)
#include "secp256k1/Int.h"

// Parse extended N value from hex string (with optional 0x prefix)
// Supports 256-bit values using Int::SetBase16()
// value_str: Input hex string (e.g., "0x1000...000" or "1000...000")
// result: Output Int object to store the parsed value
// Returns: 0 on success, -1 on error
int parse_n_value_extended(const char *value_str, Int *result);

#endif

#endif // CLI_H
