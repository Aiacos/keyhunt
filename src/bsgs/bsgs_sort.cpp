/*
 * BSGS Sorting Module - Function Implementations
 * Optimized sorting and binary search for Baby Step Giant Step
 * Extracted from keyhunt.cpp to eliminate code duplication
 */

#include "bsgs_sort.h"
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Swap two bsgs_xvalue structs */
void bsgs_swap(struct bsgs_xvalue *a, struct bsgs_xvalue *b) {
	struct bsgs_xvalue t;
	t  = *a;
	*a = *b;
	*b = t;
}

/* Main entry point - Introsort with optimal depth limit */
void bsgs_sort(struct bsgs_xvalue *arr, int64_t n) {
	uint32_t depthLimit = (n <= 1) ? 0 : ((uint32_t) ceil(log2((double)n))) * 2;
	bsgs_introsort(arr, depthLimit, n);
}

/* Introspective Sort - Hybrid algorithm dispatcher */
void bsgs_introsort(struct bsgs_xvalue *arr, uint32_t depthLimit, int64_t n) {
	int64_t p;
	if (n > 1) {
		if (n <= 16) {
			bsgs_insertionsort(arr, n);
		}
		else {
			if (depthLimit == 0) {
				bsgs_myheapsort(arr, n);
			}
			else {
				p = bsgs_partition(arr, n);
				if (p > 0) bsgs_introsort(arr, depthLimit - 1, p);
				if (p < n) bsgs_introsort(&arr[p + 1], depthLimit - 1, n - (p + 1));
			}
		}
	}
}

/* Insertion Sort - Efficient for small arrays, optimized with uint64_t comparison */
void bsgs_insertionsort(struct bsgs_xvalue *arr, int64_t n) {
	int64_t j;
	int64_t i;
	struct bsgs_xvalue key;
	for (i = 1; i < n; i++) {
		key = arr[i];
		j = i - 1;
		while (j >= 0 && arr[j].value > key.value) {
			arr[j + 1] = arr[j];
			j--;
		}
		arr[j + 1] = key;
	}
}

/* Partition for Quicksort - Middle pivot, optimized with uint64_t comparison */
int64_t bsgs_partition(struct bsgs_xvalue *arr, int64_t n) {
	struct bsgs_xvalue pivot;
	int64_t r, left, right;
	r = n / 2;
	pivot = arr[r];
	left = 0;
	right = n - 1;
	do {
		while (left < right && arr[left].value <= pivot.value) {
			left++;
		}
		while (right >= left && arr[right].value > pivot.value) {
			right--;
		}
		if (left < right) {
			if (left == r || right == r) {
				if (left == r) {
					r = right;
				} else if (right == r) {
					r = left;
				}
			}
			bsgs_swap(&arr[right], &arr[left]);
		}
	} while (left < right);
	if (right != r) {
		bsgs_swap(&arr[right], &arr[r]);
	}
	return right;
}

/* Heapify - Maintain max-heap property, optimized with uint64_t comparison */
void bsgs_heapify(struct bsgs_xvalue *arr, int64_t n, int64_t i) {
	int64_t largest = i;
	int64_t l = 2 * i + 1;
	int64_t r = 2 * i + 2;
	if (l < n && arr[l].value > arr[largest].value)
		largest = l;
	if (r < n && arr[r].value > arr[largest].value)
		largest = r;
	if (largest != i) {
		bsgs_swap(&arr[i], &arr[largest]);
		bsgs_heapify(arr, n, largest);
	}
}

/* Heap Sort - Guaranteed O(n log n) worst case */
void bsgs_myheapsort(struct bsgs_xvalue *arr, int64_t n) {
	int64_t i;
	for (i = (n / 2) - 1; i >= 0; i--) {
		bsgs_heapify(arr, n, i);
	}
	for (i = n - 1; i > 0; i--) {
		bsgs_swap(&arr[0], &arr[i]);
		bsgs_heapify(arr, i, 0);
	}
}

/*
 * Binary Search for X-point in sorted bsgs_xvalue table
 * Optimized: Compare 8-byte uint64_t (bytes 16-23 of X-coord) instead of full 32 bytes
 */
int bsgs_searchbinary(struct bsgs_xvalue *buffer, char *data, int64_t array_length, uint64_t *r_value) {
	if (array_length <= 0) return 0;
	int64_t lo = 0;
	int64_t hi = array_length;
	int r = 0;
	uint64_t search_key;
	memcpy(&search_key, data + 16, 8);
	while (lo < hi) {
		const int64_t mid = lo + ((hi - lo) >> 1);
		const uint64_t table_value = buffer[mid].value;
		if (search_key == table_value) {
			*r_value = buffer[mid].index;
			r = 1;
			break;
		}
		if (search_key < table_value) hi = mid;
		else lo = mid + 1;
	}
	return r;
}

#ifdef __cplusplus
}
#endif
