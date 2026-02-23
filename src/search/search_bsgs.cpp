/*
 * search_bsgs.cpp - Baby Step Giant Step (BSGS) mode search implementation
 *
 * MIGRATION STATUS: Config-aware (extern globals)
 *
 * This file contains the BSGS helper functions: sorting, binary search,
 * and verification checks (secondcheck, thirdcheck).
 *
 * Current implementation:
 * - Uses extern BSGS algorithm state variables (BSGS_M*, BSGS_AMP*, etc.)
 * - These are defined in keyhunt.cpp and declared in search_common.h
 * - Sorting functions are pure utilities (no globals, config-independent)
 * - Helper functions (calcualteindex, bsgs_secondcheck, bsgs_thirdcheck)
 *   use extern BSGS state and will be migrated to accept config parameter
 *
 * The main BSGS thread functions remain in keyhunt.cpp due to extensive
 * global variable dependencies, but will be migrated here incrementally.
 *
 * Extern BSGS globals used:
 * - BSGS_M_double, BSGS_M2_double, BSGS_M3, BSGS_M3_double (Int types)
 * - OriginalPointsBSGS (target public keys)
 * - BSGS_AMP2, BSGS_AMP3 (amplification point vectors)
 * - bloom_bPx2nd, bloom_bPx3rd (extended bloom filters)
 * - bPtable (baby step point table)
 * - bsgs_m3 (M3 value for table size)
 * - BSGS_BUFFERXPOINTLENGTH (X-point buffer length constant)
 * - secp (SECP256K1 instance for elliptic curve operations)
 *
 * Future migration:
 * - Move BSGS algorithm state into keyhunt_config_t->runtime.bsgs_*
 * - Update helper functions to accept config parameter
 * - Move main BSGS thread functions from keyhunt.cpp to this file
 *
 * BSGS Algorithm Overview:
 * - Time complexity: O(sqrt(N)) instead of O(N)
 * - Space complexity: O(sqrt(N))
 * - Requires known public key (X-coordinate)
 *
 * See search_common.h for shared declarations.
 */

#include "search_common.h"
#include "../output.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cinttypes>

/* ============================================================================
 * BSGS Sorting Functions
 * Optimized introsort for bsgs_xvalue structures with uint64_t comparison
 * ============================================================================ */

void bsgs_swap(struct bsgs_xvalue *a, struct bsgs_xvalue *b) {
    struct bsgs_xvalue t;
    t = *a;
    *a = *b;
    *b = t;
}

void bsgs_sort(struct bsgs_xvalue *arr, int64_t n) {
    uint32_t depthLimit = ((uint32_t)ceil(log((double)n))) * 2;
    bsgs_introsort(arr, depthLimit, n);
}

void bsgs_introsort(struct bsgs_xvalue *arr, uint32_t depthLimit, int64_t n) {
    int64_t p;
    if (n > 1) {
        if (n <= 16) {
            bsgs_insertionsort(arr, n);
        } else {
            if (depthLimit == 0) {
                bsgs_myheapsort(arr, n);
            } else {
                p = bsgs_partition(arr, n);
                if (p > 0) bsgs_introsort(arr, depthLimit - 1, p);
                if (p < n) bsgs_introsort(&arr[p + 1], depthLimit - 1, n - (p + 1));
            }
        }
    }
}

/* Optimized with uint64_t comparison */
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

/* Optimized with uint64_t comparison */
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
                }
                if (right == r) {
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

/* Optimized with uint64_t comparison */
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

/* Optimized binary search with uint64_t comparison */
int bsgs_searchbinary(struct bsgs_xvalue *buffer, char *data, int64_t array_length, uint64_t *r_value) {
    if (array_length <= 0) return 0;
    int64_t lo = 0;
    int64_t hi = array_length; // exclusive
    int r = 0;

    // Load search key as uint64_t (bytes 16-23 of X coordinate)
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

/* ============================================================================
 * BSGS Helper Functions
 * ============================================================================ */

/* Calculate index for third check
 * key = BSGS_M3 + i * BSGS_M3_double
 */
void calcualteindex(int i, Int *key) {
    if (i == 0) {
        key->Set(&BSGS_M3);
    } else {
        key->SetInt32(i);
        key->Mult(&BSGS_M3_double);
        key->Add(&BSGS_M3);
    }
}

/*
 * bsgs_secondcheck - Second level BSGS verification
 *
 * Performs a second BSGS search in a smaller range using the secondary
 * bloom filter (bloom_bPx2nd) with 1/32 the size of the primary filter.
 */
int bsgs_secondcheck(Int *start_range, uint32_t a, uint32_t k_index, Int *privatekey) {
    int i = 0, found = 0, r = 0;
    Int base_key;
    Point base_point, point_aux;
    Point BSGS_Q, BSGS_S, BSGS_Q_AMP;
    uint8_t xpoint_raw[16];

    base_key.Set(&BSGS_M_double);
    base_key.Mult((uint64_t)a);
    base_key.Add(start_range);

    base_point = secp->ComputePublicKey(&base_key);
    point_aux = secp->Negation(base_point);

    /*
     * BSGS_S = Q - base_key
     * Q is the target Key
     * base_key is the Start range + a*BSGS_M
     */
    BSGS_S = secp->AddDirect(OriginalPointsBSGS[k_index], point_aux);
    BSGS_Q.Set(BSGS_S);
    do {
        BSGS_Q_AMP = secp->AddDirect(BSGS_Q, BSGS_AMP2[i]);
        BSGS_S.Set(BSGS_Q_AMP);
        BSGS_S.x.GetHi16Bytes(xpoint_raw);
        r = bloom_ext_check(&bloom_bPx2nd[(uint8_t)xpoint_raw[0]], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
        if (r) {
            found = bsgs_thirdcheck(&base_key, i, k_index, privatekey);
        }
        i++;
    } while (i < 32 && !found);
    return found;
}

/*
 * bsgs_thirdcheck - Third level BSGS verification
 *
 * Final verification using the bPtable and bloom_bPx3rd filter.
 * If a match is found here, the private key is computed and verified.
 */
int bsgs_thirdcheck(Int *start_range, uint32_t a, uint32_t k_index, Int *privatekey) {
    uint64_t j = 0;
    int i = 0, found = 0, r = 0;
    Int base_key, calculatedkey;
    Point base_point, point_aux;
    Point BSGS_Q, BSGS_S, BSGS_Q_AMP;
    uint8_t xpoint_raw[32];

    base_key.SetInt32(a);
    base_key.Mult(&BSGS_M2_double);
    base_key.Add(start_range);

    base_point = secp->ComputePublicKey(&base_key);
    point_aux = secp->Negation(base_point);

    BSGS_S = secp->AddDirect(OriginalPointsBSGS[k_index], point_aux);
    BSGS_Q.Set(BSGS_S);

    do {
        BSGS_Q_AMP = secp->AddDirect(BSGS_Q, BSGS_AMP3[i]);
        BSGS_S.Set(BSGS_Q_AMP);
        BSGS_S.x.GetHi16Bytes(xpoint_raw);
        r = bloom_ext_check(&bloom_bPx3rd[(uint8_t)xpoint_raw[0]], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
        if (r) {
            BSGS_S.x.GetLo16Bytes(xpoint_raw + 16);
            r = bsgs_searchbinary(bPtable, (char *)xpoint_raw, bsgs_m3, &j);
            if (r) {
                calcualteindex(i, &calculatedkey);
                privatekey->Set(&calculatedkey);
                privatekey->Add((uint64_t)(j + 1));
                privatekey->Add(&base_key);
                point_aux = secp->ComputePublicKey(privatekey);
                if (point_aux.x.IsEqual(&OriginalPointsBSGS[k_index].x)) {
                    found = 1;
                } else {
                    calcualteindex(i, &calculatedkey);
                    privatekey->Set(&calculatedkey);
                    privatekey->Sub((uint64_t)(j + 1));
                    privatekey->Add(&base_key);
                    point_aux = secp->ComputePublicKey(privatekey);
                    if (point_aux.x.IsEqual(&OriginalPointsBSGS[k_index].x)) {
                        found = 1;
                    }
                }
            }
        } else {
            /*
             * Special case: AddDirect doesn't return 0 when the public keys
             * are negations of each other.
             */
            if (BSGS_Q.x.IsEqual(&BSGS_AMP3[i].x)) {
                calcualteindex(i, &calculatedkey);
                privatekey->Set(&calculatedkey);
                privatekey->Add(&base_key);
                found = 1;
            }
        }
        i++;
    } while (i < 32 && !found);
    return found;
}

/*
 * NOTE: The main BSGS thread functions (thread_process_bsgs, etc.) remain in
 * keyhunt.cpp due to extensive global variable dependencies. They will be
 * migrated here incrementally as the codebase is refactored.
 *
 * Functions to migrate:
 * - thread_process_bsgs: Sequential BSGS search
 * - thread_process_bsgs_backward: Search from end towards start
 * - thread_process_bsgs_both: Bidirectional search
 * - thread_process_bsgs_random: Random starting points
 * - thread_process_bsgs_dance: Interleaved top/bottom/random pattern
 */
