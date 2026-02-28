#ifndef BECH32_H
#define BECH32_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bech32 encoding/decoding library
 * Implements BIP 173 (Bech32) and BIP 350 (Bech32m) specifications
 * for Bitcoin SegWit address encoding
 */

/**
 * Encode data to Bech32 string
 *
 * @param output    Output buffer for Bech32 string (null-terminated)
 * @param hrp       Human-readable part (e.g., "bc" for Bitcoin mainnet)
 * @param data      Input data bytes
 * @param data_len  Length of input data
 * @return          1 on success, 0 on failure
 */
extern int bech32_encode(
    char *output,
    const char *hrp,
    const uint8_t *data,
    size_t data_len
);

/**
 * Decode Bech32 string to data
 *
 * @param hrp       Output buffer for human-readable part
 * @param data      Output buffer for decoded data
 * @param data_len  Output: length of decoded data
 * @param input     Input Bech32 string
 * @return          1 on success, 0 on failure
 */
extern int bech32_decode(
    char *hrp,
    uint8_t *data,
    size_t *data_len,
    const char *input
);

/**
 * Encode SegWit address (high-level API)
 *
 * @param output        Output buffer for SegWit address (null-terminated)
 * @param hrp           Human-readable part ("bc" for mainnet, "tb" for testnet)
 * @param witver        Witness version (0 for P2WPKH/P2WSH, 1 for Taproot)
 * @param witprog       Witness program (hash of public key or script)
 * @param witprog_len   Length of witness program (20 for P2WPKH, 32 for P2WSH/Taproot)
 * @return              1 on success, 0 on failure
 */
extern int segwit_addr_encode(
    char *output,
    const char *hrp,
    int witver,
    const uint8_t *witprog,
    size_t witprog_len
);

/**
 * Decode SegWit address (high-level API)
 *
 * @param witver        Output: witness version
 * @param witprog       Output buffer for witness program
 * @param witprog_len   Output: length of witness program
 * @param hrp           Expected human-readable part ("bc" or "tb")
 * @param addr          Input SegWit address string
 * @return              1 on success, 0 on failure
 */
extern int segwit_addr_decode(
    int *witver,
    uint8_t *witprog,
    size_t *witprog_len,
    const char *hrp,
    const char *addr
);

#ifdef __cplusplus
}
#endif

#endif /* BECH32_H */
