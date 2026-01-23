/*
 * search_xpoint.cpp - X-Point mode search implementation
 *
 * This file is a placeholder for future refactoring. The XPOINT mode
 * is currently handled within thread_process() in keyhunt.cpp.
 *
 * X-Point search is the fastest mode when the target public key
 * X-coordinate is known (e.g., for Bitcoin puzzle solving).
 *
 * Advantages:
 * - Skips RIPEMD160 and SHA256 hashing
 * - Direct comparison of 256-bit values
 * - Best cache utilization
 *
 * See search_common.h for shared declarations.
 */

/*
 * XPOINT Mode Details:
 *
 * When the target public key is known (e.g., from blockchain transaction
 * data), we can search for the X-coordinate directly instead of computing
 * the full address.
 *
 * Pipeline:
 * 1. Generate candidate private key
 * 2. Compute public key point (x, y)
 * 3. Compare x-coordinate with target
 *
 * This is significantly faster than:
 * 1. Generate candidate private key
 * 2. Compute public key point
 * 3. SHA256 hash
 * 4. RIPEMD160 hash
 * 5. Base58Check encode
 * 6. Compare with target address
 *
 * NOTE: Implementation currently in keyhunt.cpp (MODE_XPOINT branch)
 */

/* Placeholder - actual implementation in keyhunt.cpp thread_process() */
