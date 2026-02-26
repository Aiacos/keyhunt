/*
 * test_search_mocks.cpp - Shared mock functions for search mode tests
 *
 * Provides mock implementations of external dependencies needed by
 * search_xpoint and search_rmd160 modules during unit testing.
 */

#include <string.h>
#include <stdint.h>

/* Forward declaration */
struct address_value {
    char address[64];
};

/*
 * Mock binary search function for testing.
 *
 * Simple implementation that compares data against all targets in array.
 * Real implementation in keyhunt.cpp performs binary search on sorted array.
 *
 * Parameters:
 *   buffer       - Array of target addresses
 *   data         - Data to search for
 *   array_length - Number of targets in buffer
 *
 * Returns:
 *   1 if match found in any target, 0 otherwise
 */
int searchbinary(struct address_value *buffer, char *data, int64_t array_length) {
    /* Iterate through all targets to find a match */
    for (int64_t i = 0; i < array_length; i++) {
        /* Try 32-byte comparison first (XPOINT) */
        if (memcmp(buffer[i].address, data, 32) == 0) {
            return 1;
        }
        /* Try 20-byte comparison (RMD160) */
        if (memcmp(buffer[i].address, data, 20) == 0) {
            return 1;
        }
    }
    return 0;
}
