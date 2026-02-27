/*
 * search_vanity.cpp - Vanity address generation mode
 *
 * MIGRATION STATUS: Config-aware (extern globals)
 *
 * This file contains the VANITY mode search implementation:
 * - thread_process_vanity(): Main vanity search thread
 * - vanityrmdmatch(): Check RMD160 hash against vanity targets
 * - writevanitykey(): Write found vanity key to file and console
 * - addvanity(): Parse and add a vanity address target
 * - minimum_same_bytes(): Count leading identical bytes between two buffers
 *
 * Vanity addresses are Bitcoin addresses with custom prefixes, e.g.:
 * - 1LOVE...
 * - 1Pizza...
 * - 1BTC...
 *
 * See search_common.h for shared declarations.
 */

#include "search_common.h"
#ifndef BSGS_SORT_H
#define BSGS_SORT_H
#endif
#include "search_context.h"
#include "search_utils.h"
#include "../output.h"
#include "../io/io.h"
#include "../core/util.h"
#include "../base58/libbase58.h"
#include "../secure_file.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <time.h>

/* ============================================================================
 * External Dependencies (defined in keyhunt.cpp)
 * ============================================================================ */

extern bool g_avx2_available;

#include "../core/sysinfo.h"
extern system_info_t g_sysinfo;

/* WorkPool (defined in keyhunt.cpp) */
#include "../core/workpool.h"
extern WorkPool g_work_pool;

/* Thread-local block cache for work-stealing */
extern thread_local Int cpu_cached_block_start;
extern thread_local Int cpu_cached_block_end;
extern thread_local bool cpu_cached_block_valid;

/* ============================================================================
 * Profiling Support (mirrors keyhunt.cpp)
 * ============================================================================ */

typedef struct {
	uint64_t ns_ec;
	uint64_t ns_hash;
	uint64_t ns_bloom;
	uint64_t ns_binsearch;
	uint64_t ns_write;
	uint64_t keys;
} profile_counters_t;

extern bool g_profile_enabled;
extern thread_local profile_counters_t *tls_prof;
extern void profile_set_thread(int idx);

/* sub_u64_if_fits: now provided by search_utils.h as int_sub_to_u64() */

/* ============================================================================
 * Forward declarations for functions defined in this file
 * ============================================================================ */

bool vanityrmdmatch(unsigned char *rmdhash);
void writevanitykey(bool compressed, Int *key);
int addvanity(char *target);
int minimum_same_bytes(unsigned char* A, unsigned char* B, int length);

/* ============================================================================
 * thread_process_vanity - VANITY mode search thread
 *
 * Generates vanity addresses with custom prefixes (e.g., 1LOVE..., 1Pizza...).
 * Uses batch EC operations with SIMD-optimized hashing for high throughput.
 *
 * Optimization features:
 * - Batch public key computation (CPU_GRP_SIZE points at a time)
 * - SIMD hash functions (AVX512/AVX2/SSE)
 * - Endomorphism for 6x key checking per EC operation
 * - Bloom filter for multi-prefix matching
 * ============================================================================ */

#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process_vanity(LPVOID vargp) {
#else
void *thread_process_vanity(void *vargp)	{
#endif
	struct tothread *tt;
	Point pts[CPU_GRP_SIZE];
	Point endomorphism_beta[CPU_GRP_SIZE];
	Point endomorphism_beta2[CPU_GRP_SIZE];
	Point endomorphism_negeted_point[4];

	Int dx[CPU_GRP_SIZE / 2 + 1];

	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Point startP;
	Int dy;
	Int dyn;
	Int _s;
	Int _p;
	Point pp;	//point positive
	Point pn;	//point negative
	int l,pp_offset,pn_offset,i,hLength = (CPU_GRP_SIZE / 2 - 1);
	uint64_t j,count;
	Point R,temporal,publickey;
	int thread_number,continue_flag = 1,k;
	char *hextemp = NULL;
	char publickeyhashrmd160[20];
	char publickeyhashrmd160_uncompress[4][20];

	char publickeyhashrmd160_endomorphism[12][4][20];

	Int key_mpz,keyfound;
	Int key_center;
	Int stride_half;
	Int stride_four;
	tt = (struct tothread *)vargp;
	thread_number = tt->nt;
	free(tt);
	profile_set_thread(thread_number);
	grp->Set(dx);
	stride_half.SetInt32(CPU_GRP_SIZE / 2);
	stride_half.Mult(&stride);
	stride_four.SetInt32(4);
	stride_four.Mult(&stride);


	//if FLAGENDOMORPHISM  == 1 and only compress search is enabled then there is no need to calculate the Y value value

	bool calculate_y = FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH;


	do {
		if(!acquire_base_key(key_mpz))	{
			continue_flag = 0;
		}
			if(continue_flag)	{
					count = 0;
					uint64_t block_limit = N_SEQUENTIAL_MAX;
					if (!FLAGRANDOM && stride.IsOne()) {
						Int range_end_local;
						if (g_work_pool.enabled && cpu_cached_block_valid) {
							range_end_local.Set(&cpu_cached_block_end);
						} else {
							range_end_local.Set(&n_range_end);
						}
						uint64_t rem_u64 = 0;
						if (int_sub_to_u64(range_end_local, key_mpz, &rem_u64) && rem_u64 < block_limit) {
							block_limit = rem_u64;
						}
				}
				if(FLAGMATRIX)	{
						hextemp = key_mpz.GetBase16();
						printf("Base key: %s thread %i\n",hextemp,thread_number);
						fflush(stdout);
					free(hextemp);
			}
			else	{
				if(FLAGQUIET == 0)	{
					hextemp = key_mpz.GetBase16();
					printf("\rBase key: %s     \r",hextemp);
					fflush(stdout);
					free(hextemp);
					THREADOUTPUT = 1;
				}
			}
				do {
					if (count == 0) {
						key_center.Set(&key_mpz);
						key_center.Add(&stride_half);
						startP = secp->ComputePublicKey(&key_center);
					}

				for(i = 0; i < hLength; i++) {
					dx[i].ModSub(&Gn[i].x,&startP.x);
				}

				dx[i].ModSub(&Gn[i].x,&startP.x);  // For the first point
				dx[i + 1].ModSub(&_2Gn.x,&startP.x); // For the next center point
				grp->ModInvOptimized();  // Use 8x unrolled version

				pts[CPU_GRP_SIZE / 2] = startP;

				for(i = 0; i<hLength; i++) {
					// Prefetch next Gn element for better cache utilization
					if (i + 4 < hLength) {
						__builtin_prefetch(&Gn[i + 4], 0, 3);
						__builtin_prefetch(&dx[i + 4], 0, 3);
					}

					pp = startP;
					pn = startP;

					// P = startP + i*G
					dy.ModSub(&Gn[i].y,&pp.y);

					_s.ModMulK1(&dy,&dx[i]);        // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
					_p.ModSquareK1(&_s);            // _p = pow2(s)

					pp.x.ModNeg();
					pp.x.ModAdd(&_p);
					pp.x.ModSub(&Gn[i].x);           // rx = pow2(s) - p1.x - p2.x;

					if(calculate_y)	{
						pp.y.ModSub(&Gn[i].x,&pp.x);
						pp.y.ModMulK1(&_s);
						pp.y.ModSub(&Gn[i].y);           // ry = - p2.y - s*(ret.x-p2.x);
					}

					// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
					dyn.Set(&Gn[i].y);
					dyn.ModNeg();
					dyn.ModSub(&pn.y);

					_s.ModMulK1(&dyn,&dx[i]);      // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
					_p.ModSquareK1(&_s);            // _p = pow2(s)
					pn.x.ModNeg();
					pn.x.ModAdd(&_p);
					pn.x.ModSub(&Gn[i].x);          // rx = pow2(s) - p1.x - p2.x;

					if( calculate_y  )	{
						pn.y.ModSub(&Gn[i].x,&pn.x);
						pn.y.ModMulK1(&_s);
						pn.y.ModAdd(&Gn[i].y);          // ry = - p2.y - s*(ret.x-p2.x);
					}
					pp_offset = CPU_GRP_SIZE / 2 + (i + 1);
					pn_offset = CPU_GRP_SIZE / 2 - (i + 1);

					pts[pp_offset] = pp;
					pts[pn_offset] = pn;

					if(FLAGENDOMORPHISM)	{
						/*
							Q = (x,y)
							For any point Q
							Q*lambda = (x*beta mod p ,y)
							Q*lambda is a Scalar Multiplication
							x*beta is just a Multiplication (Very fast)
						*/

						if( calculate_y  )	{
							endomorphism_beta[pp_offset].y.Set(&pp.y);
							endomorphism_beta[pn_offset].y.Set(&pn.y);
							endomorphism_beta2[pp_offset].y.Set(&pp.y);
							endomorphism_beta2[pn_offset].y.Set(&pn.y);
						}
						endomorphism_beta[pp_offset].x.ModMulK1(&pp.x, &beta);
						endomorphism_beta[pn_offset].x.ModMulK1(&pn.x, &beta);
						endomorphism_beta2[pp_offset].x.ModMulK1(&pp.x, &beta2);
						endomorphism_beta2[pn_offset].x.ModMulK1(&pn.x, &beta2);
					}
				}
				/*
					Half point for endomorphism because pts[CPU_GRP_SIZE / 2] was not calcualte in the previous cycle
				*/
				if(FLAGENDOMORPHISM)	{
					if( calculate_y  )	{

						endomorphism_beta[CPU_GRP_SIZE / 2].y.Set(&pts[CPU_GRP_SIZE / 2].y);
						endomorphism_beta2[CPU_GRP_SIZE / 2].y.Set(&pts[CPU_GRP_SIZE / 2].y);
					}
					endomorphism_beta[CPU_GRP_SIZE / 2].x.ModMulK1(&pts[CPU_GRP_SIZE / 2].x, &beta);
					endomorphism_beta2[CPU_GRP_SIZE / 2].x.ModMulK1(&pts[CPU_GRP_SIZE / 2].x, &beta2);
				}

				// First point (startP - (GRP_SZIE/2)*G)
				pn = startP;
				dyn.Set(&Gn[i].y);
				dyn.ModNeg();
				dyn.ModSub(&pn.y);

				_s.ModMulK1(&dyn,&dx[i]);
				_p.ModSquareK1(&_s);

				pn.x.ModNeg();
				pn.x.ModAdd(&_p);
				pn.x.ModSub(&Gn[i].x);

				if(calculate_y )	{
					pn.y.ModSub(&Gn[i].x,&pn.x);
					pn.y.ModMulK1(&_s);
					pn.y.ModAdd(&Gn[i].y);
				}
				pts[0] = pn;

				/*
					First point for endomorphism because pts[0] was not calcualte previously
				*/
				if(FLAGENDOMORPHISM)	{
					if( calculate_y  )	{
						endomorphism_beta[0].y.Set(&pn.y);
						endomorphism_beta2[0].y.Set(&pn.y);
					}
					endomorphism_beta[0].x.ModMulK1(&pn.x, &beta);
					endomorphism_beta2[0].x.ModMulK1(&pn.x, &beta2);
				}

					if(!FLAGENDOMORPHISM && FLAGCRYPTO == CRYPTO_BTC)	{
						const bool wantCompressed = (FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH);
						const bool wantUncompressed = (FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH);

						alignas(32) char hashCompressed02[CPU_GRP_SIZE][20];
						alignas(32) char hashCompressed03[CPU_GRP_SIZE][20];
						alignas(32) char hashUncompressed[CPU_GRP_SIZE][20];

						if (wantCompressed) {
							if (g_sysinfo.has_avx512) {
								for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 16) {
									secp->GetHash160_fromX_AVX512(P2PKH, 0x02,
										&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
										&pts[idx + 4].x, &pts[idx + 5].x, &pts[idx + 6].x, &pts[idx + 7].x,
										&pts[idx + 8].x, &pts[idx + 9].x, &pts[idx + 10].x, &pts[idx + 11].x,
										&pts[idx + 12].x, &pts[idx + 13].x, &pts[idx + 14].x, &pts[idx + 15].x,
										(uint8_t*)hashCompressed02[idx], (uint8_t*)hashCompressed02[idx + 1],
										(uint8_t*)hashCompressed02[idx + 2], (uint8_t*)hashCompressed02[idx + 3],
										(uint8_t*)hashCompressed02[idx + 4], (uint8_t*)hashCompressed02[idx + 5],
										(uint8_t*)hashCompressed02[idx + 6], (uint8_t*)hashCompressed02[idx + 7],
										(uint8_t*)hashCompressed02[idx + 8], (uint8_t*)hashCompressed02[idx + 9],
										(uint8_t*)hashCompressed02[idx + 10], (uint8_t*)hashCompressed02[idx + 11],
										(uint8_t*)hashCompressed02[idx + 12], (uint8_t*)hashCompressed02[idx + 13],
										(uint8_t*)hashCompressed02[idx + 14], (uint8_t*)hashCompressed02[idx + 15]);
									secp->GetHash160_fromX_AVX512(P2PKH, 0x03,
										&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
										&pts[idx + 4].x, &pts[idx + 5].x, &pts[idx + 6].x, &pts[idx + 7].x,
										&pts[idx + 8].x, &pts[idx + 9].x, &pts[idx + 10].x, &pts[idx + 11].x,
										&pts[idx + 12].x, &pts[idx + 13].x, &pts[idx + 14].x, &pts[idx + 15].x,
										(uint8_t*)hashCompressed03[idx], (uint8_t*)hashCompressed03[idx + 1],
										(uint8_t*)hashCompressed03[idx + 2], (uint8_t*)hashCompressed03[idx + 3],
										(uint8_t*)hashCompressed03[idx + 4], (uint8_t*)hashCompressed03[idx + 5],
										(uint8_t*)hashCompressed03[idx + 6], (uint8_t*)hashCompressed03[idx + 7],
										(uint8_t*)hashCompressed03[idx + 8], (uint8_t*)hashCompressed03[idx + 9],
										(uint8_t*)hashCompressed03[idx + 10], (uint8_t*)hashCompressed03[idx + 11],
										(uint8_t*)hashCompressed03[idx + 12], (uint8_t*)hashCompressed03[idx + 13],
										(uint8_t*)hashCompressed03[idx + 14], (uint8_t*)hashCompressed03[idx + 15]);
								}
							} else if (g_avx2_available) {
								for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 8) {
									secp->GetHash160_fromX_AVX2(P2PKH, 0x02,
										&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
										&pts[idx + 4].x, &pts[idx + 5].x, &pts[idx + 6].x, &pts[idx + 7].x,
										(uint8_t*)hashCompressed02[idx], (uint8_t*)hashCompressed02[idx + 1],
										(uint8_t*)hashCompressed02[idx + 2], (uint8_t*)hashCompressed02[idx + 3],
										(uint8_t*)hashCompressed02[idx + 4], (uint8_t*)hashCompressed02[idx + 5],
										(uint8_t*)hashCompressed02[idx + 6], (uint8_t*)hashCompressed02[idx + 7]);
									secp->GetHash160_fromX_AVX2(P2PKH, 0x03,
										&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
										&pts[idx + 4].x, &pts[idx + 5].x, &pts[idx + 6].x, &pts[idx + 7].x,
										(uint8_t*)hashCompressed03[idx], (uint8_t*)hashCompressed03[idx + 1],
										(uint8_t*)hashCompressed03[idx + 2], (uint8_t*)hashCompressed03[idx + 3],
										(uint8_t*)hashCompressed03[idx + 4], (uint8_t*)hashCompressed03[idx + 5],
										(uint8_t*)hashCompressed03[idx + 6], (uint8_t*)hashCompressed03[idx + 7]);
								}
							} else {
								for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 4) {
									secp->GetHash160_fromX(P2PKH, 0x02,
										&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
										(uint8_t*)hashCompressed02[idx], (uint8_t*)hashCompressed02[idx + 1],
										(uint8_t*)hashCompressed02[idx + 2], (uint8_t*)hashCompressed02[idx + 3]);
									secp->GetHash160_fromX(P2PKH, 0x03,
										&pts[idx].x, &pts[idx + 1].x, &pts[idx + 2].x, &pts[idx + 3].x,
										(uint8_t*)hashCompressed03[idx], (uint8_t*)hashCompressed03[idx + 1],
										(uint8_t*)hashCompressed03[idx + 2], (uint8_t*)hashCompressed03[idx + 3]);
								}
							}
						}

						if (wantUncompressed) {
							if (g_sysinfo.has_avx512) {
								for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 16) {
									secp->GetHash160_AVX512(P2PKH, false,
										pts[idx], pts[idx + 1], pts[idx + 2], pts[idx + 3],
										pts[idx + 4], pts[idx + 5], pts[idx + 6], pts[idx + 7],
										pts[idx + 8], pts[idx + 9], pts[idx + 10], pts[idx + 11],
										pts[idx + 12], pts[idx + 13], pts[idx + 14], pts[idx + 15],
										(uint8_t*)hashUncompressed[idx], (uint8_t*)hashUncompressed[idx + 1],
										(uint8_t*)hashUncompressed[idx + 2], (uint8_t*)hashUncompressed[idx + 3],
										(uint8_t*)hashUncompressed[idx + 4], (uint8_t*)hashUncompressed[idx + 5],
										(uint8_t*)hashUncompressed[idx + 6], (uint8_t*)hashUncompressed[idx + 7],
										(uint8_t*)hashUncompressed[idx + 8], (uint8_t*)hashUncompressed[idx + 9],
										(uint8_t*)hashUncompressed[idx + 10], (uint8_t*)hashUncompressed[idx + 11],
										(uint8_t*)hashUncompressed[idx + 12], (uint8_t*)hashUncompressed[idx + 13],
										(uint8_t*)hashUncompressed[idx + 14], (uint8_t*)hashUncompressed[idx + 15]);
								}
							} else if (g_avx2_available) {
								for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 8) {
									secp->GetHash160_AVX2(P2PKH,false,
										pts[idx], pts[idx + 1], pts[idx + 2], pts[idx + 3],
										pts[idx + 4], pts[idx + 5], pts[idx + 6], pts[idx + 7],
										(uint8_t*)hashUncompressed[idx], (uint8_t*)hashUncompressed[idx + 1],
										(uint8_t*)hashUncompressed[idx + 2], (uint8_t*)hashUncompressed[idx + 3],
										(uint8_t*)hashUncompressed[idx + 4], (uint8_t*)hashUncompressed[idx + 5],
										(uint8_t*)hashUncompressed[idx + 6], (uint8_t*)hashUncompressed[idx + 7]);
								}
							} else {
								for (size_t idx = 0; idx < CPU_GRP_SIZE; idx += 4) {
									secp->GetHash160(P2PKH,false,
										pts[idx], pts[idx + 1], pts[idx + 2], pts[idx + 3],
										(uint8_t*)hashUncompressed[idx], (uint8_t*)hashUncompressed[idx + 1],
										(uint8_t*)hashUncompressed[idx + 2], (uint8_t*)hashUncompressed[idx + 3]);
								}
							}
						}

						Int keyCurrent;
						keyCurrent.Set(&key_mpz);
						for (size_t idx = 0; idx < CPU_GRP_SIZE; ++idx) {
							if (wantCompressed) {
								if (vanityrmdmatch((unsigned char*)hashCompressed02[idx])) {
									Int candidate(keyCurrent);
									publickey = secp->ComputePublicKey(&candidate);
									if(publickey.y.IsOdd()) {
										candidate.Neg();
										candidate.Add(&secp->order);
									}
									writevanitykey(true,&candidate);
								}
								if (vanityrmdmatch((unsigned char*)hashCompressed03[idx])) {
									Int candidate(keyCurrent);
									publickey = secp->ComputePublicKey(&candidate);
									if(publickey.y.IsEven()) {
										candidate.Neg();
										candidate.Add(&secp->order);
									}
									writevanitykey(true,&candidate);
								}
							}

							if (wantUncompressed) {
								if (vanityrmdmatch((unsigned char*)hashUncompressed[idx])) {
									Int candidate(keyCurrent);
									writevanitykey(false,&candidate);
								}
							}

							keyCurrent.Add(&stride);
						}
						key_mpz.Set(&keyCurrent);
						count += CPU_GRP_SIZE;
					}
					else	{
						for(j = 0; j < CPU_GRP_SIZE/4;j++)	{
					if(FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH ){
						if(FLAGENDOMORPHISM)	{
							secp->GetHash160_fromX(P2PKH,0x02,&pts[(j*4)].x,&pts[(j*4)+1].x,&pts[(j*4)+2].x,&pts[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[0][0],(uint8_t*)publickeyhashrmd160_endomorphism[0][1],(uint8_t*)publickeyhashrmd160_endomorphism[0][2],(uint8_t*)publickeyhashrmd160_endomorphism[0][3]);
							secp->GetHash160_fromX(P2PKH,0x03,&pts[(j*4)].x,&pts[(j*4)+1].x,&pts[(j*4)+2].x,&pts[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[1][0],(uint8_t*)publickeyhashrmd160_endomorphism[1][1],(uint8_t*)publickeyhashrmd160_endomorphism[1][2],(uint8_t*)publickeyhashrmd160_endomorphism[1][3]);

							secp->GetHash160_fromX(P2PKH,0x02,&endomorphism_beta[(j*4)].x,&endomorphism_beta[(j*4)+1].x,&endomorphism_beta[(j*4)+2].x,&endomorphism_beta[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[2][0],(uint8_t*)publickeyhashrmd160_endomorphism[2][1],(uint8_t*)publickeyhashrmd160_endomorphism[2][2],(uint8_t*)publickeyhashrmd160_endomorphism[2][3]);
							secp->GetHash160_fromX(P2PKH,0x03,&endomorphism_beta[(j*4)].x,&endomorphism_beta[(j*4)+1].x,&endomorphism_beta[(j*4)+2].x,&endomorphism_beta[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[3][0],(uint8_t*)publickeyhashrmd160_endomorphism[3][1],(uint8_t*)publickeyhashrmd160_endomorphism[3][2],(uint8_t*)publickeyhashrmd160_endomorphism[3][3]);

							secp->GetHash160_fromX(P2PKH,0x02,&endomorphism_beta2[(j*4)].x,&endomorphism_beta2[(j*4)+1].x,&endomorphism_beta2[(j*4)+2].x,&endomorphism_beta2[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[4][0],(uint8_t*)publickeyhashrmd160_endomorphism[4][1],(uint8_t*)publickeyhashrmd160_endomorphism[4][2],(uint8_t*)publickeyhashrmd160_endomorphism[4][3]);
							secp->GetHash160_fromX(P2PKH,0x03,&endomorphism_beta2[(j*4)].x,&endomorphism_beta2[(j*4)+1].x,&endomorphism_beta2[(j*4)+2].x,&endomorphism_beta2[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[5][0],(uint8_t*)publickeyhashrmd160_endomorphism[5][1],(uint8_t*)publickeyhashrmd160_endomorphism[5][2],(uint8_t*)publickeyhashrmd160_endomorphism[5][3]);

						}
						else	{
							secp->GetHash160_fromX(P2PKH,0x02,&pts[(j*4)].x,&pts[(j*4)+1].x,&pts[(j*4)+2].x,&pts[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[0][0],(uint8_t*)publickeyhashrmd160_endomorphism[0][1],(uint8_t*)publickeyhashrmd160_endomorphism[0][2],(uint8_t*)publickeyhashrmd160_endomorphism[0][3]);
							secp->GetHash160_fromX(P2PKH,0x03,&pts[(j*4)].x,&pts[(j*4)+1].x,&pts[(j*4)+2].x,&pts[(j*4)+3].x,(uint8_t*)publickeyhashrmd160_endomorphism[1][0],(uint8_t*)publickeyhashrmd160_endomorphism[1][1],(uint8_t*)publickeyhashrmd160_endomorphism[1][2],(uint8_t*)publickeyhashrmd160_endomorphism[1][3]);
						}
					}
					if(FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH)	{
						if(FLAGENDOMORPHISM)	{
							for(l = 0; l < 4; l++)	{
								endomorphism_negeted_point[l] = secp->Negation(pts[(j*4)+l]);
							}
							secp->GetHash160(P2PKH,false, pts[(j*4)], pts[(j*4)+1], pts[(j*4)+2], pts[(j*4)+3],(uint8_t*)publickeyhashrmd160_endomorphism[6][0],(uint8_t*)publickeyhashrmd160_endomorphism[6][1],(uint8_t*)publickeyhashrmd160_endomorphism[6][2],(uint8_t*)publickeyhashrmd160_endomorphism[6][3]);
							secp->GetHash160(P2PKH,false,endomorphism_negeted_point[0] ,endomorphism_negeted_point[1],endomorphism_negeted_point[2],endomorphism_negeted_point[3],(uint8_t*)publickeyhashrmd160_endomorphism[7][0],(uint8_t*)publickeyhashrmd160_endomorphism[7][1],(uint8_t*)publickeyhashrmd160_endomorphism[7][2],(uint8_t*)publickeyhashrmd160_endomorphism[7][3]);
							for(l = 0; l < 4; l++)	{
								endomorphism_negeted_point[l] = secp->Negation(endomorphism_beta[(j*4)+l]);
							}
							secp->GetHash160(P2PKH,false,endomorphism_beta[(j*4)],  endomorphism_beta[(j*4)+1], endomorphism_beta[(j*4)+2], endomorphism_beta[(j*4)+3] ,(uint8_t*)publickeyhashrmd160_endomorphism[8][0],(uint8_t*)publickeyhashrmd160_endomorphism[8][1],(uint8_t*)publickeyhashrmd160_endomorphism[8][2],(uint8_t*)publickeyhashrmd160_endomorphism[8][3]);
							secp->GetHash160(P2PKH,false,endomorphism_negeted_point[0],endomorphism_negeted_point[1],endomorphism_negeted_point[2],endomorphism_negeted_point[3],(uint8_t*)publickeyhashrmd160_endomorphism[9][0],(uint8_t*)publickeyhashrmd160_endomorphism[9][1],(uint8_t*)publickeyhashrmd160_endomorphism[9][2],(uint8_t*)publickeyhashrmd160_endomorphism[9][3]);

							for(l = 0; l < 4; l++)	{
								endomorphism_negeted_point[l] = secp->Negation(endomorphism_beta2[(j*4)+l]);
							}
							secp->GetHash160(P2PKH,false, endomorphism_beta2[(j*4)],  endomorphism_beta2[(j*4)+1] ,  endomorphism_beta2[(j*4)+2] ,  endomorphism_beta2[(j*4)+3] ,(uint8_t*)publickeyhashrmd160_endomorphism[10][0],(uint8_t*)publickeyhashrmd160_endomorphism[10][1],(uint8_t*)publickeyhashrmd160_endomorphism[10][2],(uint8_t*)publickeyhashrmd160_endomorphism[10][3]);
							secp->GetHash160(P2PKH,false, endomorphism_negeted_point[0], endomorphism_negeted_point[1],   endomorphism_negeted_point[2],endomorphism_negeted_point[3],(uint8_t*)publickeyhashrmd160_endomorphism[11][0],(uint8_t*)publickeyhashrmd160_endomorphism[11][1],(uint8_t*)publickeyhashrmd160_endomorphism[11][2],(uint8_t*)publickeyhashrmd160_endomorphism[11][3]);
						}
						else	{
							secp->GetHash160(P2PKH,false,pts[(j*4)],pts[(j*4)+1],pts[(j*4)+2],pts[(j*4)+3],(uint8_t*)publickeyhashrmd160_uncompress[0],(uint8_t*)publickeyhashrmd160_uncompress[1],(uint8_t*)publickeyhashrmd160_uncompress[2],(uint8_t*)publickeyhashrmd160_uncompress[3]);

						}
					}
					for(k = 0; k < 4;k++)	{
						if(FLAGSEARCH == SEARCH_COMPRESS || FLAGSEARCH == SEARCH_BOTH ){
							if(FLAGENDOMORPHISM)	{
								for(l = 0;l < 6; l++)	{
									if(vanityrmdmatch((uint8_t*)publickeyhashrmd160_endomorphism[l][k]))	{
										// Here the given publickeyhashrmd160 match againts one of the vanity targets
										// We need to check which of the cases is it.

										keyfound.SetInt32(k);
										keyfound.Mult(&stride);
										keyfound.Add(&key_mpz);
										publickey = secp->ComputePublicKey(&keyfound);

										switch(l)	{
											case 0:	//Original point, prefix 02
												if(publickey.y.IsOdd())	{	//if the current publickey is odd that means, we need to negate the keyfound to get the correct key
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
												// else we dont need to chage the current keyfound because it already have prefix 02
											break;
											case 1:	//Original point, prefix 03
												if(publickey.y.IsEven())	{	//if the current publickey is even that means, we need to negate the keyfound to get the correct key
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
												// else we dont need to chage the current keyfound because it already have prefix 03
											break;
											case 2:	//Beta point, prefix 02
												keyfound.ModMulK1order(&lambda);
												if(publickey.y.IsOdd())	{	//if the current publickey is odd that means, we need to negate the keyfound to get the correct key
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
												// else we dont need to chage the current keyfound because it already have prefix 02
											break;
											case 3:	//Beta point, prefix 03
												keyfound.ModMulK1order(&lambda);
												if(publickey.y.IsEven())	{	//if the current publickey is even that means, we need to negate the keyfound to get the correct key
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
												// else we dont need to chage the current keyfound because it already have prefix 02
											break;
											case 4:	//Beta^2 point, prefix 02
												keyfound.ModMulK1order(&lambda2);
												if(publickey.y.IsOdd())	{	//if the current publickey is odd that means, we need to negate the keyfound to get the correct key
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
												// else we dont need to chage the current keyfound because it already have prefix 02
											break;
											case 5:	//Beta^2 point, prefix 03
												keyfound.ModMulK1order(&lambda2);
												if(publickey.y.IsEven())	{	//if the current publickey is even that means, we need to negate the keyfound to get the correct key
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
												// else we dont need to chage the current keyfound because it already have prefix 02
											break;
										}
										writevanitykey(true,&keyfound);
									}
								}
							}
							else	{
								for(l = 0;l < 2; l++)	{
									if(vanityrmdmatch((uint8_t*)publickeyhashrmd160_endomorphism[l][k]))	{
										keyfound.SetInt32(k);
										keyfound.Mult(&stride);
										keyfound.Add(&key_mpz);

										publickey = secp->ComputePublicKey(&keyfound);
										secp->GetHash160(P2PKH,true,publickey,(uint8_t*)publickeyhashrmd160);
										if(memcmp(publickeyhashrmd160_endomorphism[l][k],publickeyhashrmd160,20) != 0){
											keyfound.Neg();
											keyfound.Add(&secp->order);
										}
										writevanitykey(true,&keyfound);
									}
								}
							}
						}
						if(FLAGSEARCH == SEARCH_UNCOMPRESS || FLAGSEARCH == SEARCH_BOTH)	{
							if(FLAGENDOMORPHISM)	{
								for(l = 6;l < 12; l++)	{
									if(vanityrmdmatch((uint8_t*)publickeyhashrmd160_endomorphism[l][k]))	{
										// Here the given publickeyhashrmd160 match againts one of the vanity targets
										// We need to check which of the cases is it.

										keyfound.SetInt32(k);
										keyfound.Mult(&stride);
										keyfound.Add(&key_mpz);


										switch(l)	{
											case 6:
											case 7:
												publickey = secp->ComputePublicKey(&keyfound);
												secp->GetHash160(P2PKH,false,publickey,(uint8_t*)publickeyhashrmd160_uncompress[0]);
												if(memcmp(publickeyhashrmd160_endomorphism[l][k],publickeyhashrmd160_uncompress[0],20) != 0){
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
											break;
											case 8:
											case 9:
												keyfound.ModMulK1order(&lambda);
												publickey = secp->ComputePublicKey(&keyfound);
												secp->GetHash160(P2PKH,false,publickey,(uint8_t*)publickeyhashrmd160_uncompress[0]);
												if(memcmp(publickeyhashrmd160_endomorphism[l][k],publickeyhashrmd160_uncompress[0],20) != 0){
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
											break;
											case 10:
											case 11:
												keyfound.ModMulK1order(&lambda2);
												publickey = secp->ComputePublicKey(&keyfound);
												secp->GetHash160(P2PKH,false,publickey,(uint8_t*)publickeyhashrmd160_uncompress[0]);
												if(memcmp(publickeyhashrmd160_endomorphism[l][k],publickeyhashrmd160_uncompress[0],20) != 0){
													keyfound.Neg();
													keyfound.Add(&secp->order);
												}
											break;
										}
										writevanitykey(false,&keyfound);
									}
								}

							}
							else	{
								if(vanityrmdmatch((uint8_t*)publickeyhashrmd160_uncompress[k]))	{
									keyfound.SetInt32(k);
									keyfound.Mult(&stride);
									keyfound.Add(&key_mpz);
									writevanitykey(false,&keyfound);
								}
							}
						}

					}

							count+=4;
							key_mpz.Add(&stride_four);
					}
					}
				steps[thread_number].value++;

				// Next start point (startP + GRP_SIZE*G)
				pp = startP;
				dy.ModSub(&_2Gn.y,&pp.y);

				_s.ModMulK1(&dy,&dx[i + 1]);
				_p.ModSquareK1(&_s);

				pp.x.ModNeg();
				pp.x.ModAdd(&_p);
				pp.x.ModSub(&_2Gn.x);

				//The Y value for the next start point always need to be calculated
				pp.y.ModSub(&_2Gn.x,&pp.x);
				pp.y.ModMulK1(&_s);
				pp.y.ModSub(&_2Gn.y);
				startP = pp;
				}while(count < block_limit && continue_flag);
			}
		} while(continue_flag);
		ends[thread_number].value = 1;
		return NULL;
	}

/* ============================================================================
 * vanityrmdmatch - Check RMD160 hash against vanity targets
 *
 * Uses bloom filter for fast negative lookups, then performs exact
 * range comparison against all registered vanity targets.
 * ============================================================================ */

bool vanityrmdmatch(unsigned char *rmdhash)	{
	bool r = false;
	int i,j,cmpA,cmpB,result;
	result = bloom_check(vanity_bloom,rmdhash,vanity_rmd_minimun_bytes_check_length);
	switch(result)	{
		case -1:
			output_error("Bloom is not initialized\n");
			exit(EXIT_FAILURE);
		break;
		case 1:
			for(i = 0; i < vanity_rmd_targets && !r;i++)	{
				for(j = 0; j < vanity_rmd_limits[i] && !r; j++)	{
					cmpA = memcmp(vanity_rmd_limit_values_A[i][j],rmdhash,20);
					cmpB = memcmp(vanity_rmd_limit_values_B[i][j],rmdhash,20);
					if(cmpA <= 0 && cmpB >= 0)	{
						r = true;
					}
				}
			}
		break;
		default:
			r = false;
		break;
	}
	return r;
}

/* ============================================================================
 * writevanitykey - Write found vanity key to file and console
 *
 * Computes the public key, address, and RMD160 hash from the private key,
 * then writes the result to VANITYKEYFOUND.txt and prints to console.
 * Thread-safe via write_keys mutex.
 * ============================================================================ */

void writevanitykey(bool compressed,Int *key)	{
	Point publickey;
	FILE *keys;
	char *hextemp,*hexrmd,public_key_hex[131],address[50],rmdhash[20];
	hextemp = key->GetBase16();
	publickey = secp->ComputePublicKey(key);
	secp->GetPublicKeyHex(compressed,publickey,public_key_hex);

	secp->GetHash160(P2PKH,compressed,publickey,(uint8_t*)rmdhash);
	hexrmd = tohex(rmdhash,20);
	rmd160toaddress_dst(rmdhash,address);

platform_mutex_lock(&write_keys);
	keys = fopen_secure_append("VANITYKEYFOUND.txt");
	if(keys != NULL)	{
		fprintf(keys,"Vanity Private Key: %s\npubkey: %s\nAddress %s\nrmd160 %s\n",hextemp,public_key_hex,address,hexrmd);
		fclose(keys);
	}
	printf("\nVanity Private Key: %s\npubkey: %s\nAddress %s\nrmd160 %s\n",hextemp,public_key_hex,address,hexrmd);

platform_mutex_unlock(&write_keys);
	free(hextemp);
	free(hexrmd);
}

/* ============================================================================
 * addvanity - Parse and add a vanity address target
 *
 * Converts a Base58 vanity prefix into RMD160 range bounds (A and B).
 * The prefix is extended with '1' characters for the lower bound and
 * 'z' characters for the upper bound to find the exact byte ranges
 * that match the prefix.
 *
 * Returns: number of range pairs added (0 on failure)
 * ============================================================================ */

int addvanity(char *target)	{
	unsigned char raw_value_A[50],raw_value_B[50];
	char target_copy[50];
	int stringsize,targetsize,j,r = 0;
	size_t raw_value_length;
	int values_A_size = 0,values_B_size = 0,minimun_bytes;
	raw_value_length = 50;
	targetsize = strlen(target);
	stringsize = targetsize;
	memset(raw_value_A,0,50);
	memset(target_copy,0,50);
	if(targetsize >= 30 )	{
		return 0;
	}
	memcpy(target_copy,target,targetsize);
	j = 0;
	vanity_address_targets = (char**)  realloc(vanity_address_targets,(vanity_rmd_targets+1) * sizeof(char*));
	vanity_address_targets[vanity_rmd_targets] = NULL;
	checkpointer((void *)vanity_address_targets,__FILE__,"realloc","vanity_address_targets" ,__LINE__ -1 );
	vanity_rmd_limits = (int*) realloc(vanity_rmd_limits,(vanity_rmd_targets+1) * sizeof(int));
	vanity_rmd_limits[vanity_rmd_targets] = 0;
	checkpointer((void *)vanity_rmd_limits,__FILE__,"realloc","vanity_rmd_limits" ,__LINE__ -1 );
	vanity_rmd_limit_values_A = (uint8_t***)realloc(vanity_rmd_limit_values_A,(vanity_rmd_targets+1) * sizeof(unsigned char *));
	checkpointer((void *)vanity_rmd_limit_values_A,__FILE__,"realloc","vanity_rmd_limit_values_A" ,__LINE__ -1 );
	vanity_rmd_limit_values_A[vanity_rmd_targets] = NULL;
	vanity_rmd_limit_values_B = (uint8_t***)realloc(vanity_rmd_limit_values_B,(vanity_rmd_targets+1) * sizeof(unsigned char *));
	checkpointer((void *)vanity_rmd_limit_values_B,__FILE__,"realloc","vanity_rmd_limit_values_B" ,__LINE__ -1 );
	vanity_rmd_limit_values_B[vanity_rmd_targets] = NULL;
	do	{
		raw_value_length = 50;
		b58tobin(raw_value_A,&raw_value_length,target_copy,stringsize);
		if(raw_value_length < 25)	{
			if(stringsize >= 49) break;
			target_copy[stringsize] = '1';
			stringsize++;
		}
		if(raw_value_length == 25)	{
			b58tobin(raw_value_A,&raw_value_length,target_copy,stringsize);

			vanity_rmd_limit_values_A[vanity_rmd_targets] = (uint8_t**)realloc(vanity_rmd_limit_values_A[vanity_rmd_targets],(j+1) * sizeof(unsigned char *));
			checkpointer((void *)vanity_rmd_limit_values_A[vanity_rmd_targets],__FILE__,"realloc","vanity_rmd_limit_values_A" ,__LINE__ -1 );
			vanity_rmd_limit_values_A[vanity_rmd_targets][j] = (uint8_t*)calloc(20,1);
			checkpointer((void *)vanity_rmd_limit_values_A[vanity_rmd_targets][j],__FILE__,"realloc","vanity_rmd_limit_values_A" ,__LINE__ -1 );

			memcpy(vanity_rmd_limit_values_A[vanity_rmd_targets][j] ,raw_value_A +1,20);

			j++;
			values_A_size = j;
			if(stringsize >= 49) break;
			target_copy[stringsize] = '1';
			stringsize++;
		}
	}while(raw_value_length <= 25);

	stringsize = targetsize;
	memset(raw_value_B,0,50);
	memset(target_copy,0,50);
	memcpy(target_copy,target,targetsize);

	j = 0;
	do	{
		raw_value_length = 50;
		b58tobin(raw_value_B,&raw_value_length,target_copy,stringsize);
		if(raw_value_length < 25)	{
			if(stringsize >= 49) break;
			target_copy[stringsize] = 'z';
			stringsize++;
		}
		if(raw_value_length == 25)	{

			b58tobin(raw_value_B,&raw_value_length,target_copy,stringsize);
			vanity_rmd_limit_values_B[vanity_rmd_targets] = (uint8_t**)realloc(vanity_rmd_limit_values_B[vanity_rmd_targets],(j+1) * sizeof(unsigned char *));
			checkpointer((void *)vanity_rmd_limit_values_B[vanity_rmd_targets],__FILE__,"realloc","vanity_rmd_limit_values_B" ,__LINE__ -1 );
			vanity_rmd_limit_values_B[vanity_rmd_targets][j] = (uint8_t*)calloc(20,1);
			checkpointer((void *)vanity_rmd_limit_values_B[vanity_rmd_targets][j],__FILE__,"calloc","vanity_rmd_limit_values_B" ,__LINE__ -1 );
			memcpy(vanity_rmd_limit_values_B[vanity_rmd_targets][j],raw_value_B+1,20);

			j++;
			values_B_size = j;

			if(stringsize >= 49) break;
			target_copy[stringsize] = 'z';
			stringsize++;
		}
	}while(raw_value_length <= 25);

	if(values_A_size >= 1 && values_B_size >= 1)	{
		if(values_A_size != values_B_size)	{
			if(values_A_size > values_B_size)
				r = values_B_size;
			else
				r = values_A_size;
		}
		else	{
			r = values_A_size;
		}
		for(j = 0; j < r; j++)	{
			minimun_bytes =  minimum_same_bytes(vanity_rmd_limit_values_A[vanity_rmd_targets][j],vanity_rmd_limit_values_B[vanity_rmd_targets][j],20);
			if(minimun_bytes < vanity_rmd_minimun_bytes_check_length)	{
				vanity_rmd_minimun_bytes_check_length = minimun_bytes;
			}
		}
		vanity_address_targets[vanity_rmd_targets] = (char*) calloc(targetsize+1,sizeof(char));
		checkpointer((void *)vanity_address_targets[vanity_rmd_targets],__FILE__,"calloc","vanity_address_targets" ,__LINE__ -1 );
		memcpy(vanity_address_targets[vanity_rmd_targets],target,targetsize+1);	// +1 to copy the null character
		vanity_rmd_limits[vanity_rmd_targets] = r;
		vanity_rmd_total+=r;
		vanity_rmd_targets++;
	}
	else	{
		for(j = 0; j < values_A_size;j++)	{
			free(vanity_rmd_limit_values_A[vanity_rmd_targets][j]);
		}
		free(vanity_rmd_limit_values_A[vanity_rmd_targets]);
		vanity_rmd_limit_values_A[vanity_rmd_targets] = NULL;

		for(j = 0; j < values_B_size;j++)	{
			free(vanity_rmd_limit_values_B[vanity_rmd_targets][j]);
		}
		free(vanity_rmd_limit_values_B[vanity_rmd_targets]);
		vanity_rmd_limit_values_B[vanity_rmd_targets] = NULL;
		r = 0;
	}
	return r;
}

/* ============================================================================
 * minimum_same_bytes - Count leading identical bytes between two buffers
 *
 * A and B are binary data pointers.
 * length is the max length to check.
 *
 * Caller must be sure that the pointers are valid and have at least
 * length bytes readable without causing overflow.
 * ============================================================================ */

int minimum_same_bytes(unsigned char* A,unsigned char* B, int length) {
    int minBytes = 0; // Assume initially that all bytes are the same
	if(A == NULL || B  == NULL)	{	// In case of some NULL pointer
		return 0;
	}
    for (int i = 0; i < length; i++) {
        if (A[i] != B[i]) {
            break; // Exit the loop since we found a mismatch
        }
        minBytes++; // Update the minimum number of bytes where data is the same
    }

    return minBytes;
}
