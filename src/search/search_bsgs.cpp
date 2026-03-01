/*
 * search_bsgs.cpp - Baby Step Giant Step (BSGS) mode search implementation
 *
 * MIGRATION STATUS: Config-wired (bsgs_context_t parameter)
 *
 * This file contains the BSGS helper functions: verification checks
 * (secondcheck, thirdcheck) and key calculation utilities.
 *
 * All 3 helper functions receive bsgs_context_t* parameter for algorithm
 * state instead of reading extern globals.
 *
 * Sorting and binary search functions are provided by the shared bsgs_sort
 * library to eliminate code duplication across multiple files.
 *
 * See search_common.h for bsgs_context_t definition.
 */

/* Include bsgs_sort.h FIRST to define struct bsgs_xvalue and BSGS_SORT_H,
 * so search_common.h's conditional definition is skipped. */
#include "../bsgs/bsgs_sort.h"
#include "search_common.h"
#include "../output.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cinttypes>

/* Secp256k1 instance accessed via local extern (used by all 3 functions) */
extern Secp256K1 *secp;

/* ============================================================================
 * BSGS Helper Functions
 * Sorting and binary search provided by src/bsgs/bsgs_sort library
 * ============================================================================ */

/* Calculate index for third check
 * key = BSGS_M3 + i * BSGS_M3_double
 */
void calcualteindex(bsgs_context_t *bctx, int i, Int *key) {
    Int &BSGS_M3 = *bctx->BSGS_M3;
    Int &BSGS_M3_double = *bctx->BSGS_M3_double;

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
int bsgs_secondcheck(bsgs_context_t *bctx, Int *start_range, uint64_t a, uint32_t k_index, Int *privatekey) {
    /* Extract BSGS state from context */
    Int &BSGS_M_double = *bctx->BSGS_M_double;
    std::vector<Point> &OriginalPointsBSGS = *bctx->OriginalPointsBSGS;
    std::vector<Point> &BSGS_AMP2 = *bctx->BSGS_AMP2;
    bloom_extended_t *bloom_bPx2nd = bctx->bloom_bPx2nd;
    uint64_t BSGS_BUFFERXPOINTLENGTH = bctx->BSGS_BUFFERXPOINTLENGTH;

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
            found = bsgs_thirdcheck(bctx, &base_key, i, k_index, privatekey);
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
int bsgs_thirdcheck(bsgs_context_t *bctx, Int *start_range, uint64_t a, uint32_t k_index, Int *privatekey) {
    /* Extract BSGS state from context */
    Int &BSGS_M2_double = *bctx->BSGS_M2_double;
    std::vector<Point> &OriginalPointsBSGS = *bctx->OriginalPointsBSGS;
    std::vector<Point> &BSGS_AMP3 = *bctx->BSGS_AMP3;
    bloom_extended_t *bloom_bPx3rd = bctx->bloom_bPx3rd;
    struct bsgs_xvalue *bPtable = bctx->bPtable;
    uint64_t bsgs_m3 = bctx->bsgs_m3;
    uint64_t BSGS_BUFFERXPOINTLENGTH = bctx->BSGS_BUFFERXPOINTLENGTH;

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
                calcualteindex(bctx, i, &calculatedkey);
                privatekey->Set(&calculatedkey);
                privatekey->Add((uint64_t)(j + 1));
                privatekey->Add(&base_key);
                point_aux = secp->ComputePublicKey(privatekey);
                if (point_aux.x.IsEqual(&OriginalPointsBSGS[k_index].x)) {
                    found = 1;
                } else {
                    calcualteindex(bctx, i, &calculatedkey);
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
                calcualteindex(bctx, i, &calculatedkey);
                privatekey->Set(&calculatedkey);
                privatekey->Add(&base_key);
                point_aux = secp->ComputePublicKey(privatekey);
                if (point_aux.x.IsEqual(&OriginalPointsBSGS[k_index].x)) {
                    found = 1;
                }
            }
        }
        i++;
    } while (i < 32 && !found);
    return found;
}
