/*
 * io.h - File I/O operations for keyhunt
 *
 * MIGRATION STATUS: Fully config-wired (Phase 4, Plan 07)
 * All functions accept keyhunt_config_t* parameter. No extern globals.
 *
 * Handles reading target files (addresses, vanity patterns, x-points),
 * writing found keys to output files, and checkpoint validation.
 *
 * Supported file formats:
 * - Address files: One Bitcoin/Ethereum address per line
 * - Vanity files: One vanity prefix per line
 * - XPoint files: One x-coordinate (hex) per line
 *
 * Key output:
 * - writekey(): Writes found Bitcoin private key + address
 * - writekeyeth(): Writes found Ethereum private key + address
 * - writeFileIfNeeded(): Conditional file write with existence check
 */

#ifndef IO_H
#define IO_H

#include <stdint.h>
#include <stdbool.h>
#include "../config/config.h"

/* Forward declarations */
class Int;

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * File Reading Functions
 *
 * All readFile functions accept a config parameter to access flags
 * and write output state (N, addressTable, bloom) through config fields.
 * ============================================================================ */

/*
 * Read a file of Bitcoin addresses into the target array.
 *
 * Reads flags from config (mode, crypto_type, skip_checksum, save_progress).
 * Writes output to config (address_count, address_table, bloom_filter,
 * max_address_length).
 *
 * Parameters:
 *   fileName - Path to file containing one address per line
 *   config   - Configuration (read flags, write output state)
 *
 * Returns:
 *   true on success, false on failure (file not found, parse error)
 */
bool readFileAddress(char *fileName, keyhunt_config_t *config);

/*
 * Read a file of vanity prefixes into the target array.
 *
 * Parameters:
 *   fileName - Path to file containing one vanity prefix per line
 *   config   - Configuration (read vanity state, write address_count)
 *
 * Returns:
 *   true on success, false on failure
 */
bool readFileVanity(char *fileName, keyhunt_config_t *config);

/*
 * Force-read an address file, replacing any previously loaded targets.
 *
 * Parameters:
 *   fileName - Path to file containing one address per line
 *   config   - Configuration (write output state)
 *
 * Returns:
 *   true on success, false on failure
 */
bool forceReadFileAddress(char *fileName, keyhunt_config_t *config);

/*
 * Force-read an Ethereum address file, replacing any previously loaded targets.
 *
 * Parameters:
 *   fileName - Path to file containing one Ethereum address per line
 *   config   - Configuration (write output state)
 *
 * Returns:
 *   true on success, false on failure
 */
bool forceReadFileAddressEth(char *fileName, keyhunt_config_t *config);

/*
 * Force-read an X-point file, replacing any previously loaded targets.
 *
 * Parameters:
 *   fileName - Path to file containing one x-coordinate (hex) per line
 *   config   - Configuration (write output state)
 *
 * Returns:
 *   true on success, false on failure
 */
bool forceReadFileXPoint(char *fileName, keyhunt_config_t *config);

/* ============================================================================
 * File Writing Functions
 * ============================================================================ */

/*
 * Write file contents if the file does not already exist.
 *
 * Parameters:
 *   fileName - Path to the file to conditionally write
 *   config   - Configuration (read flags, access bloom/addressTable)
 */
void writeFileIfNeeded(const char *fileName, keyhunt_config_t *config);

#ifdef __cplusplus
}
#endif

/* ============================================================================
 * Key Output Functions (C++ linkage - use Int class)
 * ============================================================================ */

/*
 * Write a found Bitcoin private key and its corresponding address to output.
 *
 * Parameters:
 *   config     - Pointer to the keyhunt configuration (provides secp, mutex, range bounds)
 *   compressed - true for compressed public key, false for uncompressed
 *   key        - Pointer to the found private key
 */
void writekey(const keyhunt_config_t *config, bool compressed, Int *key);

/*
 * Write a found Ethereum private key and its corresponding address to output.
 *
 * Parameters:
 *   config - Pointer to the keyhunt configuration (provides secp, mutex, range bounds)
 *   key    - Pointer to the found private key
 */
void writekeyeth(const keyhunt_config_t *config, Int *key);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/*
 * Validate a pointer and abort with diagnostic info if NULL.
 *
 * Parameters:
 *   ptr      - Pointer to validate
 *   file     - Source file name (__FILE__)
 *   function - Function name (__func__)
 *   name     - Description of the pointer being checked
 *   line     - Source line number (__LINE__)
 */
void checkpointer(void *ptr, const char *file, const char *function,
                   const char *name, int line);

/*
 * Process a single vanity address match.
 *
 * Parameters:
 *   config - Configuration (read vanity state)
 *
 * Returns:
 *   true if a vanity match was found and processed, false otherwise
 */
bool processOneVanity(keyhunt_config_t *config);

#endif /* IO_H */
