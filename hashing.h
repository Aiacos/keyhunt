#ifndef HASHSING
#define HASHSING

#include <stddef.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute the SHA-256 hash of a contiguous memory buffer.
 * @param data Pointer to the input data. Must not be NULL when @p length is non-zero.
 * @param length Number of bytes to hash.
 * @param digest Output buffer receiving the 32-byte digest.
 * @return 0 on success, non-zero on error.
 */
int sha256(const unsigned char *data, size_t length, unsigned char *digest);

/**
 * @brief Compute the RIPEMD-160 hash of a contiguous memory buffer.
 * @param data Pointer to the input data. Must not be NULL when @p length is non-zero.
 * @param length Number of bytes to hash.
 * @param digest Output buffer receiving the 20-byte digest.
 * @return 0 on success, non-zero on error.
 */
int rmd160(const unsigned char *data, size_t length, unsigned char *digest);

/**
 * @brief Compute the Keccak-256 hash of a contiguous memory buffer.
 * @param data Pointer to the input data. Must not be NULL when @p length is non-zero.
 * @param length Number of bytes to hash.
 * @param digest Output buffer receiving the 32-byte digest.
 * @return 0 on success, non-zero on error.
 */
int keccak(const unsigned char *data, size_t length, unsigned char *digest);

/**
 * @brief Incrementally compute the SHA-256 hash of a file.
 * @param file_name Path to the file to hash.
 * @param checksum Output buffer receiving the 32-byte digest.
 * @return true on success, false on error.
 */
bool sha256_file(const char* file_name, unsigned char * checksum);

/**
 * @brief Compute four RIPEMD-160 hashes in parallel.
 * @param length Number of bytes to hash in each message.
 * @param data0 Pointer to first message.
 * @param data1 Pointer to second message.
 * @param data2 Pointer to third message.
 * @param data3 Pointer to fourth message.
 * @param digest0 Output buffer for the first digest.
 * @param digest1 Output buffer for the second digest.
 * @param digest2 Output buffer for the third digest.
 * @param digest3 Output buffer for the fourth digest.
 * @return 0 on success, non-zero on error.
 */
int rmd160_4(size_t length, const unsigned char *data0, const unsigned char *data1,
                const unsigned char *data2, const unsigned char *data3,
                unsigned char *digest0, unsigned char *digest1,
                unsigned char *digest2, unsigned char *digest3);

/**
 * @brief Compute four SHA-256 hashes in parallel.
 * @param length Number of bytes to hash in each message.
 * @param data0 Pointer to first message.
 * @param data1 Pointer to second message.
 * @param data2 Pointer to third message.
 * @param data3 Pointer to fourth message.
 * @param digest0 Output buffer for the first digest.
 * @param digest1 Output buffer for the second digest.
 * @param digest2 Output buffer for the third digest.
 * @param digest3 Output buffer for the fourth digest.
 * @return 0 on success, non-zero on error.
 */
int sha256_4(size_t length, const unsigned char *data0, const unsigned char *data1,
             const unsigned char *data2, const unsigned char *data3,
             unsigned char *digest0, unsigned char *digest1,
             unsigned char *digest2, unsigned char *digest3);

#ifdef __cplusplus
}
#endif

#endif // HASHSING
