/*
 * sort.cpp - ADDRESS-mode sorting and binary search implementations
 *
 * MIGRATION STATUS: Config-independent (pure utility functions)
 *
 * This file contains custom sorting implementations (introsort with heapsort
 * fallback) and binary search for address_value arrays used in ADDRESS mode
 * target lookup.
 *
 * Sorting pipeline:
 * 1. kh_sort() selects introsort with computed depth limit
 * 2. kh_introsort() partitions recursively, falls back to heapsort at depth limit
 * 3. kh_insertionsort() handles small partitions (<=16 elements)
 *
 * Binary search:
 * - searchbinary() performs binary search on sorted address_value array
 * - Used after bloom filter positive to confirm exact match
 *
 * These are pure utility functions with no global variable dependencies.
 */

#include "sort.h"
#include "../search/search_context.h"
#include <cstring>
#include <cmath>
#include <cstdint>

/* ============================================================================
 * Endian-aware comparison helpers for 20-byte hash values
 * ============================================================================ */

static inline uint64_t load_u64_be(const void *p) {
	uint64_t v;
	memcpy(&v, p, sizeof(v));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	v = __builtin_bswap64(v);
#endif
	return v;
}

static inline uint32_t load_u32_be(const void *p) {
	uint32_t v;
	memcpy(&v, p, sizeof(v));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	v = __builtin_bswap32(v);
#endif
	return v;
}

static inline int cmp_hash20(const uint8_t *a, const uint8_t *b) {
	const uint64_t a0 = load_u64_be(a);
	const uint64_t b0 = load_u64_be(b);
	if (a0 < b0) return -1;
	if (a0 > b0) return 1;
	const uint64_t a1 = load_u64_be(a + 8);
	const uint64_t b1 = load_u64_be(b + 8);
	if (a1 < b1) return -1;
	if (a1 > b1) return 1;
	const uint32_t a2 = load_u32_be(a + 16);
	const uint32_t b2 = load_u32_be(b + 16);
	if (a2 < b2) return -1;
	if (a2 > b2) return 1;
	return 0;
}

/* ============================================================================
 * Sorting Functions
 * ============================================================================ */

void kh_swap(struct address_value *a, struct address_value *b) {
	struct address_value t;
	t  = *a;
	*a = *b;
	*b =  t;
}

void kh_sort(struct address_value *arr, int64_t n) {
	if (n <= 1) return;
	uint32_t depthLimit = ((uint32_t) ceil(log2((double)n))) * 2;
	kh_introsort(arr, depthLimit, n);
}

void kh_introsort(struct address_value *arr, uint32_t depthLimit, int64_t n) {
	int64_t p;
	if(n > 1)	{
		if(n <= 16) {
			kh_insertionsort(arr, n);
		}
		else	{
			if(depthLimit == 0) {
				kh_myheapsort(arr, n);
			}
			else	{
				p = kh_partition(arr, n);
				if(p > 0) kh_introsort(arr, depthLimit-1, p);
				if(p < n) kh_introsort(&arr[p+1], depthLimit-1, n-(p+1));
			}
		}
	}
}

void kh_insertionsort(struct address_value *arr, int64_t n) {
	int64_t j;
	int64_t i;
	struct address_value key;
	for(i = 1; i < n; i++) {
		key = arr[i];
		j = i - 1;
		while(j >= 0 && memcmp(arr[j].value, key.value, 20) > 0) {
			arr[j+1] = arr[j];
			j--;
		}
		arr[j+1] = key;
	}
}

int64_t kh_partition(struct address_value *arr, int64_t n) {
	struct address_value pivot;
	int64_t r, left, right;
	r = n / 2;
	pivot = arr[r];
	left = 0;
	right = n - 1;
	do {
		while(left < right && memcmp(arr[left].value, pivot.value, 20) <= 0) {
			left++;
		}
		while(right >= left && memcmp(arr[right].value, pivot.value, 20) > 0) {
			right--;
		}
		if(left < right) {
			if(left == r || right == r) {
				if(left == r) {
					r = right;
				}
				if(right == r) {
					r = left;
				}
			}
			kh_swap(&arr[right], &arr[left]);
		}
	} while(left < right);
	if(right != r) {
		kh_swap(&arr[right], &arr[r]);
	}
	return right;
}

void kh_heapify(struct address_value *arr, int64_t n, int64_t i) {
	int64_t largest = i;
	int64_t l = 2 * i + 1;
	int64_t r = 2 * i + 2;
	if (l < n && memcmp(arr[l].value, arr[largest].value, 20) > 0)
		largest = l;
	if (r < n && memcmp(arr[r].value, arr[largest].value, 20) > 0)
		largest = r;
	if (largest != i) {
		kh_swap(&arr[i], &arr[largest]);
		kh_heapify(arr, n, largest);
	}
}

void kh_myheapsort(struct address_value *arr, int64_t n) {
	int64_t i;
	for (i = (n / 2) - 1; i >= 0; i--) {
		kh_heapify(arr, n, i);
	}
	for (i = n - 1; i > 0; i--) {
		kh_swap(&arr[0], &arr[i]);
		kh_heapify(arr, i, 0);
	}
}

/* ============================================================================
 * Binary Search
 * ============================================================================ */

int searchbinary(struct address_value *buffer, char *data, int64_t array_length) {
	if (array_length <= 0) return 0;
	int64_t lo = 0;
	int64_t hi = array_length; // exclusive
	while (lo < hi) {
		const int64_t mid = lo + ((hi - lo) >> 1);
		const int rcmp = cmp_hash20((const uint8_t*)data, (const uint8_t*)buffer[mid].value);
		if (rcmp == 0) return 1;
		if (rcmp < 0) hi = mid;
		else lo = mid + 1;
	}
	return 0;
}
