/*
 * config.h - Simple INI configuration file parser
 *
 * Supports keyhunt.conf format:
 *   mode = hybrid
 *   threads = auto
 *   gpu = auto
 *   gpu_devices = 0,1
 *   batch_size = 1024
 *   memory_limit = 75%
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum lengths */
#define CONFIG_MAX_LINE     256
#define CONFIG_MAX_VALUE    128
#define CONFIG_MAX_DEVICES  8

/* Configuration structure */
typedef struct {
    /* Mode: "auto", "cpu", "gpu", "hybrid" */
    char mode[32];

    /* Threads: -1 = auto, 0+ = specific count */
    int threads;

    /* GPU: -1 = auto, 0 = off, 1 = on */
    int gpu_enabled;

    /* GPU devices: array of device IDs, -1 terminated */
    int gpu_devices[CONFIG_MAX_DEVICES + 1];
    int gpu_device_count;

    /* Batch size: 0 = auto */
    int batch_size;

    /* Memory limit: percentage (1-100) */
    int memory_limit_percent;

    /* Flags for which values were explicitly set */
    bool mode_set;
    bool threads_set;
    bool gpu_set;
    bool gpu_devices_set;
    bool batch_size_set;
    bool memory_limit_set;

    /* Config file path (if loaded) */
    char loaded_from[256];
} keyhunt_ini_config_t;

/**
 * Initialize config with default values
 */
void config_init(keyhunt_ini_config_t *cfg);

/**
 * Load configuration from file
 * Returns 0 on success, -1 on error (file not found or parse error)
 */
int config_load(keyhunt_ini_config_t *cfg, const char *filepath);

/**
 * Save current configuration to file
 * Returns 0 on success, -1 on error
 */
int config_save(const keyhunt_ini_config_t *cfg, const char *filepath);

/**
 * Try to load default config file (./keyhunt.conf)
 * Returns 0 if loaded, -1 if not found (not an error)
 */
int config_load_default(keyhunt_ini_config_t *cfg);

/**
 * Print current configuration to stderr (for debugging)
 */
void config_print(const keyhunt_ini_config_t *cfg);

/**
 * Get mode string for config value
 */
const char* config_mode_str(const keyhunt_ini_config_t *cfg);

/**
 * Parse GPU devices string "0,1,2" into array
 * Returns count of devices parsed
 */
int config_parse_gpu_devices(const char *str, int *devices, int max_devices);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
