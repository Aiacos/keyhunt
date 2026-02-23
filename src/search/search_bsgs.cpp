/*
 * search_bsgs.cpp - Baby Step Giant Step (BSGS) mode search implementation
 *
 * This file contains the BSGS helper functions: verification checks
 * (secondcheck, thirdcheck) and key calculation utilities.
 *
 * Sorting and binary search functions are provided by the shared bsgs_sort
 * library to eliminate code duplication across multiple files.
 *
 * The main BSGS thread functions remain in keyhunt.cpp due to extensive
 * global variable dependencies, but will be migrated here incrementally.
 *
 * BSGS Algorithm Overview:
 * - Time complexity: O(sqrt(N)) instead of O(N)
 * - Space complexity: O(sqrt(N))
 * - Requires known public key (X-coordinate)
 *
 * See search_common.h for shared declarations.
 */

#include "search_common.h"
#include "../bsgs/bsgs_sort.h"
#include "../output.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cinttypes>

/* ============================================================================
 * BSGS Helper Functions
 * Sorting and binary search provided by src/bsgs/bsgs_sort library
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
