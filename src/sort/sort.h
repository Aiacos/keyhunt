/*
 * sort.h - ADDRESS-mode sorting and binary search functions
 *
 * Provides custom sorting implementations (introsort with heapsort fallback)
 * and binary search for address_value arrays used in ADDRESS mode target lookup.
 *
 * Sorting pipeline:
 * 1. _sort() selects introsort with computed depth limit
 * 2. _introsort() partitions recursively, falls back to heapsort at depth limit
 * 3. _insertionsort() handles small partitions (≤16 elements)
 *
 * Binary search:
 * - searchbinary() performs binary search on sorted address_value array
 * - Used after bloom filter positive to confirm exact match
 */

#ifndef SORT_H
#define SORT_H

#include <stdint.h>

/* Forward declarations for types defined in keyhunt.cpp */
struct address_value;

/* ============================================================================
 * Sorting Functions
 * ============================================================================ */

/*
 * Swap two address_value elements.
 *
 * Parameters:
 *   a - Pointer to first element
 *   b - Pointer to second element
 */
void _swap(struct address_value *a, struct address_value *b);

/*
 * Sort an array of address_value elements using introsort.
 * Entry point that computes depth limit and delegates to _introsort.
 *
 * Parameters:
 *   arr - Array to sort
 *   N   - Number of elements
 */
void _sort(struct address_value *arr, int64_t N);

/*
 * Introsort implementation with heapsort fallback.
 * Switches to heapsort when recursion depth exceeds depthLimit,
 * and to insertion sort for small partitions.
 *
 * Parameters:
 *   arr        - Array to sort
 *   depthLimit - Maximum recursion depth before heapsort fallback
 *   n          - Number of elements
 */
void _introsort(struct address_value *arr, uint32_t depthLimit, int64_t n);

/*
 * Insertion sort for small arrays or nearly-sorted partitions.
 *
 * Parameters:
 *   arr - Array to sort
 *   n   - Number of elements
 */
void _insertionsort(struct address_value *arr, int64_t n);

/*
 * Partition array around pivot for introsort.
 *
 * Parameters:
 *   arr - Array to partition
 *   n   - Number of elements
 *
 * Returns:
 *   Partition index
 */
int64_t _partition(struct address_value *arr, int64_t n);

/*
 * Heapify subtree rooted at index i.
 *
 * Parameters:
 *   arr - Array representing the heap
 *   n   - Size of heap
 *   i   - Root index of subtree to heapify
 */
void _heapify(struct address_value *arr, int64_t n, int64_t i);

/*
 * Heapsort implementation used as fallback when introsort depth limit is reached.
 *
 * Parameters:
 *   arr - Array to sort
 *   n   - Number of elements
 */
void _myheapsort(struct address_value *arr, int64_t n);

/* ============================================================================
 * Binary Search
 * ============================================================================ */

/*
 * Binary search for a data value in a sorted address_value array.
 *
 * Parameters:
 *   buffer       - Sorted array of address_value targets
 *   data         - Pointer to data to search for
 *   array_length - Number of elements in buffer
 *
 * Returns:
 *   1 if found, 0 if not found
 */
int searchbinary(struct address_value *buffer, char *data, int64_t array_length);

#endif /* SORT_H */
