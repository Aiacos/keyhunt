/*
 * io.h - File I/O operations for keyhunt
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

/* Forward declarations */
class Int;

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * File Reading Functions
 * ============================================================================ */

/*
 * Read a file of Bitcoin addresses into the target array.
 *
 * Parameters:
 *   fileName - Path to file containing one address per line
 *
 * Returns:
 *   true on success, false on failure (file not found, parse error)
 */
bool readFileAddress(char *fileName);

/*
 * Read a file of vanity prefixes into the target array.
 *
 * Parameters:
 *   fileName - Path to file containing one vanity prefix per line
 *
 * Returns:
 *   true on success, false on failure
 */
bool readFileVanity(char *fileName);

/*
 * Force-read an address file, replacing any previously loaded targets.
 *
 * Parameters:
 *   fileName - Path to file containing one address per line
 *
 * Returns:
 *   true on success, false on failure
 */
bool forceReadFileAddress(char *fileName);

/*
 * Force-read an Ethereum address file, replacing any previously loaded targets.
 *
 * Parameters:
 *   fileName - Path to file containing one Ethereum address per line
 *
 * Returns:
 *   true on success, false on failure
 */
bool forceReadFileAddressEth(char *fileName);

/*
 * Force-read an X-point file, replacing any previously loaded targets.
 *
 * Parameters:
 *   fileName - Path to file containing one x-coordinate (hex) per line
 *
 * Returns:
 *   true on success, false on failure
 */
bool forceReadFileXPoint(char *fileName);

/* ============================================================================
 * File Writing Functions
 * ============================================================================ */

/*
 * Write file contents if the file does not already exist.
 *
 * Parameters:
 *   fileName - Path to the file to conditionally write
 */
void writeFileIfNeeded(const char *fileName);

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
 *   compressed - true for compressed public key, false for uncompressed
 *   key        - Pointer to the found private key
 */
void writekey(bool compressed, Int *key);

/*
 * Write a found Ethereum private key and its corresponding address to output.
 *
 * Parameters:
 *   key - Pointer to the found private key
 */
void writekeyeth(Int *key);

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
 * Returns:
 *   true if a vanity match was found and processed, false otherwise
 */
bool processOneVanity(void);

#endif /* IO_H */
