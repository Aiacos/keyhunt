/*
 * BSGS Sorting Module
 * Optimized sorting and binary search algorithms for Baby Step Giant Step
 *
 * Key features:
 * - Introsort hybrid algorithm (quicksort + heapsort + insertion sort)
 * - Optimized uint64_t comparison for bsgs_xvalue structs
 * - Binary search for X-point lookup in sorted tables
 * - Cache-efficient partitioning and sorting
 */

#ifndef BSGS_SORT_H
#define BSGS_SORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BSGS X-Value Structure
 * Stores pre-computed baby step X-coordinates and their indices
 * Optimized for 64-bit comparisons and cache alignment
 */
struct bsgs_xvalue {
	uint64_t value;    // 8 bytes instead of 6 - same memory due to padding, faster comparison
	uint64_t index;    // Index in baby step table
};

/*
 * Main sorting function - Entry point for BSGS table sorting
 * Uses introsort algorithm (quicksort + heapsort + insertion sort)
 *
 * @param arr  Array of bsgs_xvalue structs to sort
 * @param n    Number of elements in array
 */
void bsgs_sort(struct bsgs_xvalue *arr, int64_t n);

/*
 * Introspective sort - Hybrid sorting algorithm
 * Switches between quicksort, heapsort, and insertion sort based on conditions
 *
 * @param arr        Array to sort
 * @param depthLimit Recursion depth limit (prevents quadratic quicksort behavior)
 * @param n          Number of elements
 */
void bsgs_introsort(struct bsgs_xvalue *arr, uint32_t depthLimit, int64_t n);

/*
 * Swap two bsgs_xvalue structs
 *
 * @param a  Pointer to first element
 * @param b  Pointer to second element
 */
void bsgs_swap(struct bsgs_xvalue *a, struct bsgs_xvalue *b);

/*
 * Heapify operation for heap sort
 * Maintains max-heap property for subtree rooted at index i
 *
 * @param arr  Array to heapify
 * @param n    Heap size
 * @param i    Root index of subtree to heapify
 */
void bsgs_heapify(struct bsgs_xvalue *arr, int64_t n, int64_t i);

/*
 * Partition operation for quicksort
 * Uses last element as pivot, optimized with uint64_t comparison
 *
 * @param arr  Array to partition
 * @param n    Number of elements
 * @return     Partition index (pivot final position)
 */
int64_t bsgs_partition(struct bsgs_xvalue *arr, int64_t n);

/*
 * Heap sort implementation
 * Guaranteed O(n log n) worst case, used when quicksort recurses too deep
 *
 * @param arr  Array to sort
 * @param n    Number of elements
 */
void bsgs_myheapsort(struct bsgs_xvalue *arr, int64_t n);

/*
 * Insertion sort implementation
 * Efficient for small arrays (< 16 elements), used as final cleanup
 *
 * @param arr  Array to sort
 * @param n    Number of elements
 */
void bsgs_insertionsort(struct bsgs_xvalue *arr, int64_t n);

/*
 * Binary search for X-point in sorted bsgs_xvalue table
 * Optimized with uint64_t comparison for fast lookups
 *
 * @param arr          Sorted array of bsgs_xvalue structs
 * @param data         32-byte X-coordinate to search for
 * @param array_length Number of elements in array
 * @param r_value      Output: index value if found
 * @return             1 if found, 0 if not found
 */
int bsgs_searchbinary(struct bsgs_xvalue *arr, char *data, int64_t array_length, uint64_t *r_value);

#ifdef __cplusplus
}
#endif

#endif // BSGS_SORT_H
