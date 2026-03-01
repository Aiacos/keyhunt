/*
 * search_bsgs_threads.cpp - BSGS thread functions
 *
 * MIGRATION STATUS: Config-wired (bsgs_context_t via thread_args)
 *
 * This file contains all BSGS thread entry points moved from keyhunt.cpp:
 * - thread_process_bsgs: Sequential BSGS search
 * - thread_process_bsgs_random: Random starting points
 * - thread_bPload: Baby step table loading (3 bloom levels)
 * - thread_bPload_2blooms: Baby step table loading (2 bloom levels)
 * - thread_process_bsgs_dance: Interleaved top/bottom/random pattern
 * - thread_process_bsgs_backward: Search from end towards start
 * - thread_process_bsgs_both: Bidirectional search
 *
 * All 5 BSGS search thread variants receive thread_args with config pointer.
 * BSGS algorithm state is accessed via config->runtime.bsgs_context (bsgs_context_t*).
 * Helper functions (bsgs_secondcheck, bsgs_thirdcheck) receive bsgs_context_t*.
 *
 * See search_common.h for bsgs_context_t definition and thread_args.
 */

/* Include bsgs_sort.h FIRST so BSGS_SORT_H is defined before search_common.h
 * and search_context.h try to define struct bsgs_xvalue conditionally. */
#include "../bsgs/bsgs_sort.h"
#include "search_common.h"
#include "search_context.h"
#include "search_utils.h"
#include "../secp256k1/IntGroup.h"
#include "../platform/platform.h"
#include "../output.h"
#include "../bsgs/bsgs_ops.h"
#include "../core/sysinfo.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include "../secure_file.h"
#include <cmath>

/* Infrastructure function externs (not BSGS state) */
extern void profile_set_thread(int idx);
extern system_info_t g_sysinfo;

/**
 * suggest_practical_alternatives - Calculate and suggest practical N/K values
 *
 * When the user requests a bit range that exceeds practical limits (e.g., -b 160),
 * this function calculates alternative parameters that would fit in available RAM.
 *
 * @param thread_id      Thread number for reporting
 * @param bit_range      User's requested bit range (e.g., 160)
 * @param current_k      Current K factor (from config->bsgs.k_factor)
 */
static void suggest_practical_alternatives(uint32_t thread_id, int bit_range, int current_k) {
	/* Use global sysinfo for RAM availability */
	uint64_t available_ram_mb = g_sysinfo.ram_available;
	if (available_ram_mb == 0) {
		available_ram_mb = 4096; /* Fallback: assume 4 GB */
	}

	/* Use 60% of available RAM as safe target */
	uint64_t target_ram_mb = (available_ram_mb * 60) / 100;
	uint64_t target_ram_bytes = target_ram_mb * 1024 * 1024;

	output_info("\n[Thread %u] Suggesting practical alternatives for your system:\n", thread_id);
	output_info("  Available RAM: %llu MB\n", (unsigned long long)available_ram_mb);
	output_info("  Safe target:   %llu MB (60%% of available)\n\n", (unsigned long long)target_ram_mb);

	/* BSGS memory formula: Total = M * K * 20 bytes (approximate, with bloom overhead)
	 * Where M = sqrt(N) = sqrt(2^bit_range) = 2^(bit_range/2)
	 *
	 * Example calculations for different bit ranges:
	 * - 66-bit:  M = 2^33  = 8.6 billion entries
	 * - 125-bit: M = 2^62.5 ~= 6.5e18 entries
	 * - 160-bit: M = 2^80  = 1.2e24 entries (impractical!)
	 */

	output_info("  Recommended configurations:\n\n");

	/* Strategy 1: Keep K factor, reduce bit range */
	int max_bit_for_k = 0;
	for (int b = 40; b <= 125; b += 5) {
		/* Rough approximation: M = 2^(b/2), Memory = M * K * 20 bytes */
		/* For precise calculation, we'd need to compute sqrt(2^b), but this estimates well */
		double m_bits = (double)b / 2.0;
		double m_approx = pow(2.0, m_bits);
		uint64_t mem_approx = (uint64_t)(m_approx * current_k * 20.0);

		if (mem_approx > UINT64_MAX || mem_approx > target_ram_bytes) {
			break;
		}
		max_bit_for_k = b;
	}

	if (max_bit_for_k > 0) {
		output_info("  Strategy 1: Keep K = %d, reduce bit range\n", current_k);
		output_info("    Maximum practical bit range: %d bits\n", max_bit_for_k);
		output_info("    Command: ./keyhunt -m bsgs -f targets.txt -b %d -k %d\n\n",
		            max_bit_for_k, current_k);
	}

	/* Strategy 2: Keep bit range at practical maximum (125), adjust K factor */
	if (bit_range > 125) {
		int max_k_for_125 = 1;
		for (int k = 1; k <= 4096; k *= 2) {
			double m_125 = pow(2.0, 125.0 / 2.0); /* M for 125-bit range */
			uint64_t mem_approx = (uint64_t)(m_125 * k * 20.0);

			if (mem_approx > UINT64_MAX || mem_approx > target_ram_bytes) {
				break;
			}
			max_k_for_125 = k;
		}

		if (max_k_for_125 >= 1) {
			output_info("  Strategy 2: Use 125-bit range (practical maximum), adjust K factor\n");
			output_info("    Maximum K factor: %d\n", max_k_for_125);
			output_info("    Command: ./keyhunt -m bsgs -f targets.txt -b 125 -k %d\n\n",
			            max_k_for_125);
		}
	}

	/* Strategy 3: Use smaller K factor for current bit range (if practical) */
	int min_k_for_current = 0;
	if (bit_range <= 125) {
		for (int k = 1; k <= current_k; k *= 2) {
			double m_bits = (double)bit_range / 2.0;
			double m_approx = pow(2.0, m_bits);
			uint64_t mem_approx = (uint64_t)(m_approx * k * 20.0);

			if (mem_approx <= target_ram_bytes && mem_approx < UINT64_MAX) {
				min_k_for_current = k;
			} else {
				break;
			}
		}

		if (min_k_for_current > 0 && min_k_for_current < current_k) {
			output_info("  Strategy 3: Reduce K factor for %d-bit range\n", bit_range);
			output_info("    Suggested K factor: %d\n", min_k_for_current);
			output_info("    Command: ./keyhunt -m bsgs -f targets.txt -b %d -k %d\n\n",
			            bit_range, min_k_for_current);
		}
	}

	output_info("  Note: For ranges beyond 160 bits, BSGS is generally impractical.\n");
	output_info("        Consider using address search mode instead.\n\n");
}

/**
 * validate_bsgs_nm_values - Validate BSGS N/M values for practical ranges
 *
 * This function checks if the BSGS N and M values (Int 256-bit types) fit
 * within practical computation limits. The values originate from:
 * - config->bsgs.n_value_int: N value as Int* (256-bit precision)
 * - config->bsgs.m_value_int: M value as Int* (256-bit precision)
 *
 * These are converted to working Int globals (BSGS_N, BSGS_M) during
 * initialization in keyhunt.cpp. This function validates them at thread
 * startup to ensure practical memory and computation constraints.
 *
 * Practical limits:
 * - N <= 2^80 (~1.2e24): Ensures reasonable computation time
 * - M <= 2^64 (~1.8e19): Ensures M can fit in uint64_t for array indexing
 * - Memory: M * K * ~20 bytes per entry (bloom + table)
 *
 * Graceful degradation: If N > uint64_t, suggests reducing K factor or using smaller N
 *
 * @param thread_id  Thread number for error reporting
 * @param bctx       BSGS context with N/M values
 * @param k_factor   K factor from config
 * @return           0 on success, -1 if values are impractical
 */
static int validate_bsgs_nm_values(uint32_t thread_id, bsgs_context_t *bctx, int k_factor) {
	Int &BSGS_N = *bctx->BSGS_N;
	Int &BSGS_M = *bctx->BSGS_M;
	int KFACTOR = k_factor;
	bool n_overflow = false;
	bool m_overflow = false;

	/* Check if higher bits are set (indicating overflow beyond 64 bits) */
	for (int i = 1; i < NB64BLOCK; i++) {
		if (BSGS_N.bits64[i] != 0) {
			n_overflow = true;
		}
		if (BSGS_M.bits64[i] != 0) {
			m_overflow = true;
		}
	}

	/* Check if N > 2^80 (practical limit for BSGS computation)
	 * 2^80 = 0x100000000000000000000 (21 hex digits)
	 * If bit 80 or higher is set, it's beyond practical range */
	bool n_beyond_practical = false;
	if (n_overflow) {
		/* If any bit beyond bit 63 is set, check if beyond 2^80 */
		/* 2^80 requires bits64[1] to have bit 16 or higher set (80-64=16) */
		if (BSGS_N.bits64[1] >= (1ULL << 16)) {
			n_beyond_practical = true;
		}
		/* Or any higher limb is set */
		for (int i = 2; i < NB64BLOCK; i++) {
			if (BSGS_N.bits64[i] != 0) {
				n_beyond_practical = true;
				break;
			}
		}
	}

	/* Report warnings for impractical ranges */
	if (n_beyond_practical) {
		output_error("\n[Thread %u] ERROR: BSGS N value is beyond practical computation range (>2^80)\n", thread_id);
		output_error("  Current N would require astronomical computation time and memory.\n");
		output_error("  For reference:\n");
		output_error("    - Puzzle #66:  N = 2^66  (practical with BSGS)\n");
		output_error("    - Puzzle #125: N = 2^125 (practical with BSGS)\n");
		output_error("    - N > 2^160:   Impractical even with perfect hardware\n\n");

		/* Estimate bit range from N value for better suggestions */
		int estimated_bits = 0;
		for (int i = NB64BLOCK - 1; i >= 0; i--) {
			if (BSGS_N.bits64[i] != 0) {
				/* Find highest set bit in this limb */
				uint64_t limb = BSGS_N.bits64[i];
				int bit_pos = 63;
				while (bit_pos >= 0 && !(limb & (1ULL << bit_pos))) {
					bit_pos--;
				}
				estimated_bits = i * 64 + bit_pos + 1;
				break;
			}
		}

		if (estimated_bits > 0) {
			suggest_practical_alternatives(thread_id, estimated_bits, KFACTOR);
		}

		return -1;
	}

	if (m_overflow) {
		output_warning("\n[Thread %u] WARNING: BSGS M value exceeds 64-bit addressable range\n", thread_id);
		output_warning("  M = sqrt(N) = %s (hex)\n", BSGS_M.GetBase16());
		output_warning("  This may cause memory allocation or indexing issues.\n\n");

		/* Estimate bit range for suggestions */
		int estimated_bits = 0;
		for (int i = NB64BLOCK - 1; i >= 0; i--) {
			if (BSGS_N.bits64[i] != 0) {
				uint64_t limb = BSGS_N.bits64[i];
				int bit_pos = 63;
				while (bit_pos >= 0 && !(limb & (1ULL << bit_pos))) {
					bit_pos--;
				}
				estimated_bits = i * 64 + bit_pos + 1;
				break;
			}
		}

		if (estimated_bits > 0) {
			suggest_practical_alternatives(thread_id, estimated_bits, KFACTOR);
		}

		/* Continue execution but warn - may fail later during memory allocation */
	}

	if (n_overflow && !n_beyond_practical) {
		output_warning("\n[Thread %u] WARNING: BSGS N value exceeds 64-bit range (but within 2^80 practical limit)\n", thread_id);
		output_warning("  N = %s (hex)\n", BSGS_N.GetBase16());
		output_warning("  Using extended precision (Int 256-bit) for computation.\n\n");

		/* Provide helpful suggestions for N > 2^64 case */
		int estimated_bits = 0;
		for (int i = NB64BLOCK - 1; i >= 0; i--) {
			if (BSGS_N.bits64[i] != 0) {
				uint64_t limb = BSGS_N.bits64[i];
				int bit_pos = 63;
				while (bit_pos >= 0 && !(limb & (1ULL << bit_pos))) {
					bit_pos--;
				}
				estimated_bits = i * 64 + bit_pos + 1;
				break;
			}
		}

		if (estimated_bits > 64) {
			output_info("[Thread %u] Graceful degradation recommendations:\n", thread_id);
			output_info("  Your N value is large (estimated %d bits) but theoretically feasible.\n", estimated_bits);
			output_info("  However, you may want to consider more practical alternatives:\n\n");
			suggest_practical_alternatives(thread_id, estimated_bits, KFACTOR);
		}
	}

	/* Success: Values are within practical limits */
	return 0;
}

/* ============================================================================
 * thread_process_bsgs - Sequential BSGS search
 * ============================================================================ */

platform_thread_return_t PLATFORM_THREAD_CALL thread_process_bsgs(void *vargp) {
	/* Extract config and thread ID from thread_args */
	thread_args *args = (thread_args *)vargp;
	keyhunt_config_t *config = args->config;
	uint32_t thread_number = (uint32_t)args->thread_id;
	delete args;

	/* BSGS context from config */
	bsgs_context_t *bctx = (bsgs_context_t *)config->runtime.bsgs_context;

	/* Config-derived local variables (shadows matching old global names) */
	Secp256K1 *secp = (Secp256K1 *)config->runtime.secp;
	bool FLAGMATRIX = config->search.matrix_mode;
	bool FLAGQUIET = config->search.quiet_mode;
	struct thread_counter *steps = (struct thread_counter *)config->runtime.thread_counters;
	struct thread_flag *ends = (struct thread_flag *)config->runtime.thread_flags;
	std::atomic<int> &THREADOUTPUT = *(std::atomic<int> *)config->runtime.thread_output;
	Int &n_range_end = *(Int *)config->runtime.range_end;
	platform_mutex_t &bsgs_thread = *(platform_mutex_t *)config->runtime.bsgs_mutex;
	platform_mutex_t &write_keys = *(platform_mutex_t *)config->runtime.write_mutex;

	/* BSGS context reads */
	Int &BSGS_CURRENT = *bctx->BSGS_CURRENT;
	Int &BSGS_N_double = *bctx->BSGS_N_double;
	Int &BSGS_M = *bctx->BSGS_M;
	Int &BSGS_M_double = *bctx->BSGS_M_double;
	std::vector<Point> &GSn = *bctx->GSn;
	Point &_2GSn = *bctx->_2GSn;
	std::vector<Point> &OriginalPointsBSGS = *bctx->OriginalPointsBSGS;
	bool *OriginalPointsBSGScompressed = bctx->OriginalPointsBSGScompressed;
	std::atomic<int> *bsgs_found = bctx->bsgs_found;
	bloom_extended_t *bloom_bP = bctx->bloom_bP;
	uint64_t bsgs_aux = bctx->bsgs_aux;
	uint32_t bsgs_point_number = bctx->bsgs_point_number;
	uint64_t BSGS_BUFFERXPOINTLENGTH = bctx->BSGS_BUFFERXPOINTLENGTH;

	// File-related variables
	FILE* filekey;

	// Character variables
	uint8_t xpoint_raw[16];
	char *aux_c, *hextemp;

	// Integer variables
	Int base_key, keyfound;
	IntGroup* grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Int dx[CPU_GRP_SIZE / 2 + 1];
	Int dy, dyn, _s, _p, intaux;

	// Point variables
	Point base_point, point_aux, point_found, offset_point;
	Point startP;
	Point pp, pn;

	// Unsigned integer variables
	uint32_t k, l, r, salir;
	uint64_t cycles;

	// Other variables
	int hLength = (CPU_GRP_SIZE / 2 - 1);
	grp->Set(dx);

	// Batch context for optimized operations
	bsgs_batch_ctx_t batch_ctx;

	profile_set_thread((int)thread_number);

	/* Validate BSGS N/M values */
	if (validate_bsgs_nm_values(thread_number, bctx, config->bsgs.k_factor) != 0) {
		output_error("[Thread %u] Aborting due to impractical BSGS parameters\n", thread_number);
		delete grp;
		return (platform_thread_return_t)0;
	}

	// Initialize batch context
	if (bsgs_batch_init(&batch_ctx, BSGS_BATCH_SIZE) != 0) {
		output_error("Failed to initialize BSGS batch context in thread %u\n", thread_number);
		delete grp;
		return (platform_thread_return_t)0;
	}

	cycles = bsgs_aux / 1024;
	if(bsgs_aux % 1024 != 0)	{
		cycles++;
	}

	intaux.Set(&BSGS_M_double);
	intaux.Mult(CPU_GRP_SIZE/2);
	intaux.Add(&BSGS_M);
	offset_point = secp->ComputePublicKey(&intaux);

	do	{
	/*
		We do this in an atomic pthread_mutex operation to not affect others threads
		so BSGS_CURRENT is never the same between threads
	*/
platform_mutex_lock(&bsgs_thread);

		base_key.Set(&BSGS_CURRENT);	/* we need to set our base_key to the current BSGS_CURRENT value*/
		BSGS_CURRENT.Add(&BSGS_N_double);		/*Then add 2*BSGS_N to BSGS_CURRENT*/

platform_mutex_unlock(&bsgs_thread);

		if(base_key.IsGreaterOrEqual(&n_range_end))
			break;

		if(FLAGMATRIX)	{
			aux_c = base_key.GetBase16();
			output_success("Thread 0x%s \n",aux_c);
			fflush(stdout);
			free(aux_c);
		}
		else	{
			if(!FLAGQUIET){
				aux_c = base_key.GetBase16();
				printf("\r[+] Thread 0x%s   \r",aux_c);
				fflush(stdout);
				free(aux_c);
				THREADOUTPUT.store(1, std::memory_order_release);
			}
		}
		base_point = secp->ComputePublicKey(&base_key);
		point_aux = secp->AddDirect(base_point, offset_point);
		point_aux = secp->Negation(point_aux);
		for(k = 0; k < bsgs_point_number ; k++)	{
			if(bsgs_found[k].load(std::memory_order_relaxed) == 0)	{
				startP  = secp->AddDirect(OriginalPointsBSGS[k],point_aux);
				uint64_t j = 0;
				while( j < cycles && bsgs_found[k].load(std::memory_order_relaxed) == 0 )	{
					// Use optimized batch point computation
					bsgs_batch_compute_points(&batch_ctx, &startP, GSn.data(), &_2GSn, hLength);

					// Check all computed points against bloom filter
					for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k].load(std::memory_order_relaxed) == 0; i++) {
						batch_ctx.pts[i].x.GetHi16Bytes(xpoint_raw);
						r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
						if(r) {
							r = bsgs_secondcheck(bctx,&base_key,((j*1024) + i),k,&keyfound);
							if(r)	{
								hextemp = keyfound.GetBase16();
								output_success("Thread Key found privkey %s   \n",hextemp);
								point_found = secp->ComputePublicKey(&keyfound);
								aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
								output_success("Publickey %s\n",aux_c);
platform_mutex_lock(&write_keys);

								filekey = fopen_secure_append("KEYFOUNDKEYFOUND.txt");
								if(filekey != NULL)	{
									fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
									fclose(filekey);
								} else {
									output_error("Could not write to KEYFOUNDKEYFOUND.txt\n");
								}
								free(hextemp);
								free(aux_c);
platform_mutex_unlock(&write_keys);
								bsgs_found[k].store(1, std::memory_order_release);
								salir = 1;
								for(l = 0; l < bsgs_point_number && salir; l++)	{
									salir &= bsgs_found[l].load(std::memory_order_acquire);
								}
								if(salir)	{
									printf("All points were found\n");
									exit(EXIT_SUCCESS);
								}
							} //End if second check
						}//End if first check
					}// For for pts variable
					// Next start point (startP += (bsSize*GRP_SIZE).G)
					pp = startP;
					dy.ModSub(&_2GSn.y,&pp.y);

					_s.ModMulK1(&dy,&batch_ctx.dx[hLength + 1]);
					_p.ModSquareK1(&_s);

					pp.x.ModNeg();
					pp.x.ModAdd(&_p);
					pp.x.ModSub(&_2GSn.x);

					pp.y.ModSub(&_2GSn.x,&pp.x);
					pp.y.ModMulK1(&_s);
					pp.y.ModSub(&_2GSn.y);
					startP = pp;

					j++;
				} // end while
			}// End if
		}
		steps[thread_number].value+=2;
	}while(1);
	ends[thread_number].value = 1;
	bsgs_batch_free(&batch_ctx);
	delete grp;
	return (platform_thread_return_t)0;
}

/* ============================================================================
 * thread_process_bsgs_random - Random starting point BSGS search
 * ============================================================================ */

platform_thread_return_t PLATFORM_THREAD_CALL thread_process_bsgs_random(void *vargp) {
	/* Extract config and thread ID from thread_args */
	thread_args *args = (thread_args *)vargp;
	keyhunt_config_t *config = args->config;
	uint32_t thread_number = (uint32_t)args->thread_id;
	delete args;

	/* BSGS context from config */
	bsgs_context_t *bctx = (bsgs_context_t *)config->runtime.bsgs_context;

	/* Config-derived local variables */
	Secp256K1 *secp = (Secp256K1 *)config->runtime.secp;
	bool FLAGMATRIX = config->search.matrix_mode;
	bool FLAGQUIET = config->search.quiet_mode;
	struct thread_counter *steps = (struct thread_counter *)config->runtime.thread_counters;
	struct thread_flag *ends = (struct thread_flag *)config->runtime.thread_flags;
	std::atomic<int> &THREADOUTPUT = *(std::atomic<int> *)config->runtime.thread_output;
	Int &n_range_start = *(Int *)config->runtime.range_start;
	Int &n_range_end = *(Int *)config->runtime.range_end;
	platform_mutex_t &bsgs_thread = *(platform_mutex_t *)config->runtime.bsgs_mutex;
	platform_mutex_t &write_keys = *(platform_mutex_t *)config->runtime.write_mutex;

	/* BSGS context reads */
	Int &BSGS_M = *bctx->BSGS_M;
	Int &BSGS_M_double = *bctx->BSGS_M_double;
	std::vector<Point> &GSn = *bctx->GSn;
	Point &_2GSn = *bctx->_2GSn;
	std::vector<Point> &OriginalPointsBSGS = *bctx->OriginalPointsBSGS;
	bool *OriginalPointsBSGScompressed = bctx->OriginalPointsBSGScompressed;
	std::atomic<int> *bsgs_found = bctx->bsgs_found;
	bloom_extended_t *bloom_bP = bctx->bloom_bP;
	uint64_t bsgs_aux = bctx->bsgs_aux;
	uint32_t bsgs_point_number = bctx->bsgs_point_number;
	uint64_t BSGS_BUFFERXPOINTLENGTH = bctx->BSGS_BUFFERXPOINTLENGTH;

	FILE *filekey;
	uint8_t xpoint_raw[16];
	char *aux_c,*hextemp;
	Int base_key,keyfound,n_range_random;
	Point base_point,point_aux,point_found,offset_point;
	uint32_t l,k,r,salir;
	uint64_t cycles;

	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Point startP;

	int hLength = (CPU_GRP_SIZE / 2 - 1);

	Int dx[CPU_GRP_SIZE / 2 + 1];

	Int dy;
	Int dyn;
	Int _s;
	Int _p;
	Int intaux;
	Point pp;
	Point pn;
	grp->Set(dx);

	// Batch context for optimized operations
	bsgs_batch_ctx_t batch_ctx;

	profile_set_thread((int)thread_number);

	/* Validate BSGS N/M values */
	if (validate_bsgs_nm_values(thread_number, bctx, config->bsgs.k_factor) != 0) {
		output_error("[Thread %u] Aborting due to impractical BSGS parameters\n", thread_number);
		delete grp;
		return (platform_thread_return_t)0;
	}

	// Initialize batch context
	if (bsgs_batch_init(&batch_ctx, BSGS_BATCH_SIZE) != 0) {
		output_error("Failed to initialize BSGS batch context in thread %u\n", thread_number);
		delete grp;
		return (platform_thread_return_t)0;
	}

	cycles = bsgs_aux / 1024;
	if(bsgs_aux % 1024 != 0)	{
		cycles++;
	}

	intaux.Set(&BSGS_M_double);
	intaux.Mult(CPU_GRP_SIZE/2);
	intaux.Add(&BSGS_M);
	offset_point = secp->ComputePublicKey(&intaux);

	do	{

platform_mutex_lock(&bsgs_thread);

		base_key.Rand(&n_range_start,&n_range_end);
platform_mutex_unlock(&bsgs_thread);

		if(FLAGMATRIX)	{
				aux_c = base_key.GetBase16();
				output_success("Thread 0x%s  \n",aux_c);
				fflush(stdout);
				free(aux_c);
		}
		else{
			if(!FLAGQUIET){
				aux_c = base_key.GetBase16();
				printf("\r[+] Thread 0x%s  \r",aux_c);
				fflush(stdout);
				free(aux_c);
				THREADOUTPUT.store(1, std::memory_order_release);
			}
		}
		base_point = secp->ComputePublicKey(&base_key);
		point_aux = secp->AddDirect(base_point, offset_point);
		point_aux = secp->Negation(point_aux);


		/* We need to test individually every point in BSGS_Q */
		for(k = 0; k < bsgs_point_number ; k++)	{
			if(bsgs_found[k].load(std::memory_order_relaxed) == 0)	{
				startP  = secp->AddDirect(OriginalPointsBSGS[k],point_aux);
				uint64_t j = 0;
				while( j < cycles && bsgs_found[k].load(std::memory_order_relaxed) == 0 )	{
					// Use optimized batch point computation
					bsgs_batch_compute_points(&batch_ctx, &startP, GSn.data(), &_2GSn, hLength);

					// Check all computed points against bloom filter
					for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k].load(std::memory_order_relaxed) == 0; i++) {
						batch_ctx.pts[i].x.GetHi16Bytes(xpoint_raw);
						r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
						if(r) {
							r = bsgs_secondcheck(bctx,&base_key,((j*1024) + i),k,&keyfound);
							if(r)	{
								hextemp = keyfound.GetBase16();
								output_success("Thread Key found privkey %s    \n",hextemp);
								point_found = secp->ComputePublicKey(&keyfound);
								aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
								output_success("Publickey %s\n",aux_c);
platform_mutex_lock(&write_keys);

								filekey = fopen_secure_append("KEYFOUNDKEYFOUND.txt");
								if(filekey != NULL)	{
									fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
									fclose(filekey);
								} else {
									output_error("Could not write to KEYFOUNDKEYFOUND.txt\n");
								}
								free(hextemp);
								free(aux_c);
platform_mutex_unlock(&write_keys);

								bsgs_found[k].store(1, std::memory_order_release);
								salir = 1;
								for(l = 0; l < bsgs_point_number && salir; l++)	{
									salir &= bsgs_found[l].load(std::memory_order_acquire);
								}
								if(salir)	{
									printf("All points were found\n");
									exit(EXIT_SUCCESS);
								}
							} //End if second check
						}//End if first check

					}// For for pts variable

					// Next start point (startP += (bsSize*GRP_SIZE).G)

					pp = startP;
					dy.ModSub(&_2GSn.y,&pp.y);

					_s.ModMulK1(&dy,&batch_ctx.dx[hLength + 1]);
					_p.ModSquareK1(&_s);

					pp.x.ModNeg();
					pp.x.ModAdd(&_p);
					pp.x.ModSub(&_2GSn.x);

					pp.y.ModSub(&_2GSn.x,&pp.x);
					pp.y.ModMulK1(&_s);
					pp.y.ModSub(&_2GSn.y);
					startP = pp;

					j++;

				}	//End While
			}	//End if
		} // End for with k bsgs_point_number

		steps[thread_number].value+=2;
	}while(1);
	ends[thread_number].value = 1;
	bsgs_batch_free(&batch_ctx);
	delete grp;
	return (platform_thread_return_t)0;
}

/* ============================================================================
 * thread_bPload - Baby step table loading (3 bloom levels)
 * ============================================================================ */

platform_thread_return_t PLATFORM_THREAD_CALL thread_bPload(void *vargp) {
	/* bPload threads run during initialization before config bridge is fully
	 * populated. They use struct bPload (not thread_args) and need local externs
	 * for the BSGS globals they access. */
	extern Secp256K1 *secp;
	extern std::vector<Point> Gn;
	extern Point _2Gn;
	extern bloom_extended_t *bloom_bP;
	extern bloom_extended_t *bloom_bPx2nd;
	extern bloom_extended_t *bloom_bPx3rd;
	extern platform_mutex_t *bloom_bP_mutex;
	extern platform_mutex_t *bloom_bPx2nd_mutex;
	extern platform_mutex_t *bloom_bPx3rd_mutex;
	extern platform_mutex_t *bPload_mutex;
	extern struct bsgs_xvalue *bPtable;
	extern uint64_t bsgs_m2;
	extern uint64_t bsgs_m3;
	extern uint64_t BSGS_BUFFERXPOINTLENGTH;
	extern int FLAGREADEDFILE1;
	extern int FLAGREADEDFILE2;
	extern int FLAGREADEDFILE3;
	extern int FLAGREADEDFILE4;

	char rawvalue[32];
	struct bPload *tt;
	uint64_t i_counter,j,nbStep,to;

	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Point startP;
	Int dx[CPU_GRP_SIZE / 2 + 1];
	Point pts[CPU_GRP_SIZE];
	Int dy,dyn,_s,_p;
	Point pp,pn;

	int i,bloom_bP_index,hLength = (CPU_GRP_SIZE / 2 - 1) ,threadid;
	tt = (struct bPload *)vargp;
	Int km((uint64_t)(tt->from + 1));
	threadid = tt->threadid;
	//if(FLAGDEBUG) printf("[D] thread %i from %" PRIu64 " to %" PRIu64 "\n",threadid,tt->from,tt->to);

	i_counter = tt->from;

	nbStep = (tt->to - tt->from) / CPU_GRP_SIZE;

	if( ((tt->to - tt->from) % CPU_GRP_SIZE )  != 0)	{
		nbStep++;
	}
	//if(FLAGDEBUG) printf("[D] thread %i nbStep %" PRIu64 "\n",threadid,nbStep);
	to = tt->to;

	km.Add((uint64_t)(CPU_GRP_SIZE / 2));
	startP = secp->ComputePublicKey(&km);
	grp->Set(dx);
	for(uint64_t s=0;s<nbStep;s++) {
		for(i = 0; i < hLength; i++) {
			dx[i].ModSub(&Gn[i].x,&startP.x);
		}
		dx[i].ModSub(&Gn[i].x,&startP.x); // For the first point
		dx[i + 1].ModSub(&_2Gn.x,&startP.x);// For the next center point
		// Grouped ModInv
		grp->ModInvOptimized();  // Use 8x unrolled version

		// We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
		// We compute key in the positive and negative way from the center of the group
		// center point

		pts[CPU_GRP_SIZE / 2] = startP;	//Center point

		for(i = 0; i<hLength; i++) {
			pp = startP;
			pn = startP;

			// P = startP + i*G
			dy.ModSub(&Gn[i].y,&pp.y);

			_s.ModMulK1(&dy,&dx[i]);        // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
			_p.ModSquareK1(&_s);            // _p = pow2(s)

			pp.x.ModNeg();
			pp.x.ModAdd(&_p);
			pp.x.ModSub(&Gn[i].x);           // rx = pow2(s) - p1.x - p2.x;

			// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
			dyn.Set(&Gn[i].y);
			dyn.ModNeg();
			dyn.ModSub(&pn.y);

			_s.ModMulK1(&dyn,&dx[i]);      // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
			_p.ModSquareK1(&_s);            // _p = pow2(s)

			pn.x.ModNeg();
			pn.x.ModAdd(&_p);
			pn.x.ModSub(&Gn[i].x);          // rx = pow2(s) - p1.x - p2.x;

			pts[CPU_GRP_SIZE / 2 + (i + 1)] = pp;
			pts[CPU_GRP_SIZE / 2 - (i + 1)] = pn;
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

		pts[0] = pn;
		for(j=0;j<CPU_GRP_SIZE;j++)	{
			pts[j].x.Get32Bytes((unsigned char*)rawvalue);
			bloom_bP_index = (uint8_t)rawvalue[0];
			/*
			if(FLAGDEBUG){
				tohex_dst(rawvalue,32,hexraw);
				printf("%i : %s : %i\n",i_counter,hexraw,bloom_bP_index);
			}
			*/
			if(i_counter < bsgs_m3)	{
				if(!FLAGREADEDFILE3)	{
					memcpy(&bPtable[i_counter].value, rawvalue+16, 8);  // Copy 8 bytes to uint64_t
					bPtable[i_counter].index = i_counter;
				}
				if(!FLAGREADEDFILE4)	{
					platform_mutex_lock(&bloom_bPx3rd_mutex[bloom_bP_index]);
					bloom_ext_add(&bloom_bPx3rd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
					platform_mutex_unlock(&bloom_bPx3rd_mutex[bloom_bP_index]);
				}
			}
			if(i_counter < bsgs_m2 && !FLAGREADEDFILE2)	{
				platform_mutex_lock(&bloom_bPx2nd_mutex[bloom_bP_index]);
bloom_ext_add(&bloom_bPx2nd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
				platform_mutex_unlock(&bloom_bPx2nd_mutex[bloom_bP_index]);
			}
			if(i_counter < to && !FLAGREADEDFILE1 )	{
			platform_mutex_lock(&bloom_bP_mutex[bloom_bP_index]);
bloom_ext_add(&bloom_bP[bloom_bP_index], rawvalue ,BSGS_BUFFERXPOINTLENGTH);
				platform_mutex_unlock(&bloom_bP_mutex[bloom_bP_index]);
			}
			i_counter++;
		}
		// Next start point (startP + GRP_SIZE*G)
		pp = startP;
		dy.ModSub(&_2Gn.y,&pp.y);

		_s.ModMulK1(&dy,&dx[i + 1]);
		_p.ModSquareK1(&_s);

		pp.x.ModNeg();
		pp.x.ModAdd(&_p);
		pp.x.ModSub(&_2Gn.x);

		pp.y.ModSub(&_2Gn.x,&pp.x);
		pp.y.ModMulK1(&_s);
		pp.y.ModSub(&_2Gn.y);
		startP = pp;
	}
	delete grp;
	platform_mutex_lock(&bPload_mutex[threadid]);
	tt->finished = 1;
	platform_mutex_unlock(&bPload_mutex[threadid]);
	return (platform_thread_return_t)0;
}

/* ============================================================================
 * thread_bPload_2blooms - Baby step table loading (2 bloom levels)
 * ============================================================================ */

platform_thread_return_t PLATFORM_THREAD_CALL thread_bPload_2blooms(void *vargp) {
	/* bPload threads run during initialization before config bridge is fully
	 * populated. They use struct bPload (not thread_args) and need local externs. */
	extern Secp256K1 *secp;
	extern std::vector<Point> Gn;
	extern Point _2Gn;
	extern bloom_extended_t *bloom_bPx2nd;
	extern bloom_extended_t *bloom_bPx3rd;
	extern platform_mutex_t *bloom_bPx2nd_mutex;
	extern platform_mutex_t *bloom_bPx3rd_mutex;
	extern platform_mutex_t *bPload_mutex;
	extern struct bsgs_xvalue *bPtable;
	extern uint64_t bsgs_m2;
	extern uint64_t bsgs_m3;
	extern uint64_t BSGS_BUFFERXPOINTLENGTH;
	extern int FLAGREADEDFILE2;
	extern int FLAGREADEDFILE3;
	extern int FLAGREADEDFILE4;

	char rawvalue[32];
	struct bPload *tt;
	uint64_t i_counter,j,nbStep; //,to;
	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Point startP;
	Int dx[CPU_GRP_SIZE / 2 + 1];
	Point pts[CPU_GRP_SIZE];
	Int dy,dyn,_s,_p;
	Point pp,pn;
	int i,bloom_bP_index,hLength = (CPU_GRP_SIZE / 2 - 1) ,threadid;
	tt = (struct bPload *)vargp;
	Int km((uint64_t)(tt->from +1 ));
	threadid = tt->threadid;

	i_counter = tt->from;

	nbStep = (tt->to - (tt->from)) / CPU_GRP_SIZE;

	if( ((tt->to - (tt->from)) % CPU_GRP_SIZE )  != 0)	{
		nbStep++;
	}
	//if(FLAGDEBUG) printf("[D] thread %i nbStep %" PRIu64 "\n",threadid,nbStep);
	//to = tt->to;

	km.Add((uint64_t)(CPU_GRP_SIZE / 2));
	startP = secp->ComputePublicKey(&km);
	grp->Set(dx);
	for(uint64_t s=0;s<nbStep;s++) {
		for(i = 0; i < hLength; i++) {
			dx[i].ModSub(&Gn[i].x,&startP.x);
		}
		dx[i].ModSub(&Gn[i].x,&startP.x); // For the first point
		dx[i + 1].ModSub(&_2Gn.x,&startP.x);// For the next center point
		// Grouped ModInv
		grp->ModInvOptimized();  // Use 8x unrolled version

		// We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
		// We compute key in the positive and negative way from the center of the group
		// center point

		pts[CPU_GRP_SIZE / 2] = startP;	//Center point

		for(i = 0; i<hLength; i++) {
			pp = startP;
			pn = startP;

			// P = startP + i*G
			dy.ModSub(&Gn[i].y,&pp.y);

			_s.ModMulK1(&dy,&dx[i]);        // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
			_p.ModSquareK1(&_s);            // _p = pow2(s)

			pp.x.ModNeg();
			pp.x.ModAdd(&_p);
			pp.x.ModSub(&Gn[i].x);           // rx = pow2(s) - p1.x - p2.x;

			// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
			dyn.Set(&Gn[i].y);
			dyn.ModNeg();
			dyn.ModSub(&pn.y);

			_s.ModMulK1(&dyn,&dx[i]);      // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
			_p.ModSquareK1(&_s);            // _p = pow2(s)

			pn.x.ModNeg();
			pn.x.ModAdd(&_p);
			pn.x.ModSub(&Gn[i].x);          // rx = pow2(s) - p1.x - p2.x;

			pts[CPU_GRP_SIZE / 2 + (i + 1)] = pp;
			pts[CPU_GRP_SIZE / 2 - (i + 1)] = pn;
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

		pts[0] = pn;
		for(j=0;j<CPU_GRP_SIZE;j++)	{
			pts[j].x.Get32Bytes((unsigned char*)rawvalue);
			bloom_bP_index = (uint8_t)rawvalue[0];
			if(i_counter < bsgs_m3)	{
				if(!FLAGREADEDFILE3)	{
					memcpy(&bPtable[i_counter].value, rawvalue+16, 8);  // Copy 8 bytes to uint64_t
					bPtable[i_counter].index = i_counter;
				}
				if(!FLAGREADEDFILE4)	{
					platform_mutex_lock(&bloom_bPx3rd_mutex[bloom_bP_index]);
					bloom_ext_add(&bloom_bPx3rd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
					platform_mutex_unlock(&bloom_bPx3rd_mutex[bloom_bP_index]);
				}
			}
			if(i_counter < bsgs_m2 && !FLAGREADEDFILE2)	{
					platform_mutex_lock(&bloom_bPx2nd_mutex[bloom_bP_index]);
bloom_ext_add(&bloom_bPx2nd[bloom_bP_index], rawvalue, BSGS_BUFFERXPOINTLENGTH);
					platform_mutex_unlock(&bloom_bPx2nd_mutex[bloom_bP_index]);
			}
			i_counter++;
		}
		// Next start point (startP + GRP_SIZE*G)
		pp = startP;
		dy.ModSub(&_2Gn.y,&pp.y);

		_s.ModMulK1(&dy,&dx[i + 1]);
		_p.ModSquareK1(&_s);

		pp.x.ModNeg();
		pp.x.ModAdd(&_p);
		pp.x.ModSub(&_2Gn.x);

		pp.y.ModSub(&_2Gn.x,&pp.x);
		pp.y.ModMulK1(&_s);
		pp.y.ModSub(&_2Gn.y);
		startP = pp;
	}
	delete grp;
	platform_mutex_lock(&bPload_mutex[threadid]);
	tt->finished = 1;
	platform_mutex_unlock(&bPload_mutex[threadid]);
	return (platform_thread_return_t)0;
}

/* ============================================================================
 * thread_process_bsgs_dance - Interleaved top/bottom/random BSGS search
 * ============================================================================ */

platform_thread_return_t PLATFORM_THREAD_CALL thread_process_bsgs_dance(void *vargp) {
	/* Extract config and thread ID from thread_args */
	thread_args *args = (thread_args *)vargp;
	keyhunt_config_t *config = args->config;
	uint32_t thread_number = (uint32_t)args->thread_id;
	delete args;

	/* BSGS context from config */
	bsgs_context_t *bctx = (bsgs_context_t *)config->runtime.bsgs_context;

	/* Config-derived local variables */
	Secp256K1 *secp = (Secp256K1 *)config->runtime.secp;
	bool FLAGMATRIX = config->search.matrix_mode;
	bool FLAGQUIET = config->search.quiet_mode;
	struct thread_counter *steps = (struct thread_counter *)config->runtime.thread_counters;
	struct thread_flag *ends = (struct thread_flag *)config->runtime.thread_flags;
	std::atomic<int> &THREADOUTPUT = *(std::atomic<int> *)config->runtime.thread_output;
	Int &n_range_end = *(Int *)config->runtime.range_end;
	platform_mutex_t &bsgs_thread = *(platform_mutex_t *)config->runtime.bsgs_mutex;
	platform_mutex_t &write_keys = *(platform_mutex_t *)config->runtime.write_mutex;

	/* BSGS context reads */
	Int &BSGS_CURRENT = *bctx->BSGS_CURRENT;
	Int &BSGS_N_double = *bctx->BSGS_N_double;
	Int &BSGS_M = *bctx->BSGS_M;
	Int &BSGS_M_double = *bctx->BSGS_M_double;
	std::vector<Point> &GSn = *bctx->GSn;
	Point &_2GSn = *bctx->_2GSn;
	std::vector<Point> &OriginalPointsBSGS = *bctx->OriginalPointsBSGS;
	bool *OriginalPointsBSGScompressed = bctx->OriginalPointsBSGScompressed;
	std::atomic<int> *bsgs_found = bctx->bsgs_found;
	bloom_extended_t *bloom_bP = bctx->bloom_bP;
	uint64_t bsgs_aux = bctx->bsgs_aux;
	uint32_t bsgs_point_number = bctx->bsgs_point_number;
	uint64_t BSGS_BUFFERXPOINTLENGTH = bctx->BSGS_BUFFERXPOINTLENGTH;

	Point pts[CPU_GRP_SIZE];
	Int dx[CPU_GRP_SIZE / 2 + 1];
	Point pp,pn,startP,base_point,point_aux,point_found,offset_point;
	FILE *filekey;
	uint8_t xpoint_raw[16];
	char *aux_c,*hextemp;
	Int base_key,keyfound,dy,dyn,_s,_p,intaux;
	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	struct thread_rand_state rand_state;
	uint32_t k,l,r,salir,entrar;
	uint64_t cycles;
	int hLength = (CPU_GRP_SIZE / 2 - 1);

	grp->Set(dx);

	// Batch context for optimized operations
	bsgs_batch_ctx_t batch_ctx;

	profile_set_thread((int)thread_number);

	/* Validate BSGS N/M values */
	if (validate_bsgs_nm_values(thread_number, bctx, config->bsgs.k_factor) != 0) {
		output_error("[Thread %u] Aborting due to impractical BSGS parameters\n", thread_number);
		delete grp;
		return (platform_thread_return_t)0;
	}

	thread_rand_init(&rand_state, (uint64_t)thread_number ^ (uint64_t)time(NULL));

	// Initialize batch context
	if (bsgs_batch_init(&batch_ctx, BSGS_BATCH_SIZE) != 0) {
		output_error("Failed to initialize BSGS batch context in thread %u\n", thread_number);
		delete grp;
		return (platform_thread_return_t)0;
	}

	cycles = bsgs_aux / 1024;
	if(bsgs_aux % 1024 != 0)	{
		cycles++;
	}

	intaux.Set(&BSGS_M_double);
	intaux.Mult(CPU_GRP_SIZE/2);
	intaux.Add(&BSGS_M);
	offset_point = secp->ComputePublicKey(&intaux);

	entrar = 1;


	/*
		while base_key is less than n_range_end then:
	*/
	do	{
		r = (uint32_t)thread_rand_n(&rand_state, 3);
platform_mutex_lock(&bsgs_thread);
	switch(r)	{
		case 0:	//TOP
			if(n_range_end.IsGreater(&BSGS_CURRENT) && n_range_end.IsGreaterOrEqual(&BSGS_N_double))	{
				/*
					n_range_end.Sub(&BSGS_N);
					n_range_end.Sub(&BSGS_N);
				*/
					n_range_end.Sub(&BSGS_N_double);
					if(n_range_end.IsLower(&BSGS_CURRENT))	{
						base_key.Set(&BSGS_CURRENT);
					}
					else	{
						base_key.Set(&n_range_end);
					}
			}
			else	{
				entrar = 0;
			}
		break;
		case 1: //BOTTOM
			if(BSGS_CURRENT.IsLower(&n_range_end))	{
				base_key.Set(&BSGS_CURRENT);
				//BSGS_N_double
				BSGS_CURRENT.Add(&BSGS_N_double);
				/*
				BSGS_CURRENT.Add(&BSGS_N);
				BSGS_CURRENT.Add(&BSGS_N);
				*/
			}
			else	{
				entrar = 0;
			}
		break;
		case 2: //random - middle
			base_key.Rand(&BSGS_CURRENT,&n_range_end);
		break;
	}
platform_mutex_unlock(&bsgs_thread);

		if(entrar == 0)
			break;

		if(FLAGMATRIX)	{
			aux_c = base_key.GetBase16();
			output_success("Thread 0x%s \n",aux_c);
			fflush(stdout);
			free(aux_c);
		}
		else	{
			if(!FLAGQUIET){
				aux_c = base_key.GetBase16();
				printf("\r[+] Thread 0x%s   \r",aux_c);
				fflush(stdout);
				free(aux_c);
				THREADOUTPUT.store(1, std::memory_order_release);
			}
		}

		base_point = secp->ComputePublicKey(&base_key);
		point_aux = secp->AddDirect(base_point, offset_point);
		point_aux = secp->Negation(point_aux);

		for(k = 0; k < bsgs_point_number ; k++)	{
			if(bsgs_found[k].load(std::memory_order_relaxed) == 0)	{
				startP  = secp->AddDirect(OriginalPointsBSGS[k],point_aux);
				uint64_t j = 0;
				while( j < cycles && bsgs_found[k].load(std::memory_order_relaxed) == 0 )	{
					// Use optimized batch point computation
					bsgs_batch_compute_points(&batch_ctx, &startP, GSn.data(), &_2GSn, hLength);

					// Check all computed points against bloom filter
					for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k].load(std::memory_order_relaxed) == 0; i++) {
						batch_ctx.pts[i].x.GetHi16Bytes(xpoint_raw);
						r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
						if(r) {
							r = bsgs_secondcheck(bctx,&base_key,((j*1024) + i),k,&keyfound);
							if(r)	{
								hextemp = keyfound.GetBase16();
								output_success("Thread Key found privkey %s   \n",hextemp);
								point_found = secp->ComputePublicKey(&keyfound);
								aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
								output_success("Publickey %s\n",aux_c);
platform_mutex_lock(&write_keys);

								filekey = fopen_secure_append("KEYFOUNDKEYFOUND.txt");
								if(filekey != NULL)	{
									fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
									fclose(filekey);
								} else {
									output_error("Could not write to KEYFOUNDKEYFOUND.txt\n");
								}
								free(hextemp);
								free(aux_c);
platform_mutex_unlock(&write_keys);

								bsgs_found[k].store(1, std::memory_order_release);
								salir = 1;
								for(l = 0; l < bsgs_point_number && salir; l++)	{
									salir &= bsgs_found[l].load(std::memory_order_acquire);
								}
								if(salir)	{
									printf("All points were found\n");
									exit(EXIT_SUCCESS);
								}
							} //End if second check
						}//End if first check

					}// For for pts variable

					// Next start point (startP += (bsSize*GRP_SIZE).G)

					pp = startP;
					dy.ModSub(&_2GSn.y,&pp.y);

					_s.ModMulK1(&dy,&batch_ctx.dx[hLength + 1]);
					_p.ModSquareK1(&_s);

					pp.x.ModNeg();
					pp.x.ModAdd(&_p);
					pp.x.ModSub(&_2GSn.x);

					pp.y.ModSub(&_2GSn.x,&pp.x);
					pp.y.ModMulK1(&_s);
					pp.y.ModSub(&_2GSn.y);
					startP = pp;

					j++;
				}//while all the aMP points
			}// End if
		}
		steps[thread_number].value+=2;
	}while(1);
	ends[thread_number].value = 1;
	bsgs_batch_free(&batch_ctx);
	delete grp;
	return (platform_thread_return_t)0;
}

/* ============================================================================
 * thread_process_bsgs_backward - Search from end towards start
 * ============================================================================ */

platform_thread_return_t PLATFORM_THREAD_CALL thread_process_bsgs_backward(void *vargp) {
	/* Extract config and thread ID from thread_args */
	thread_args *args = (thread_args *)vargp;
	keyhunt_config_t *config = args->config;
	uint32_t thread_number = (uint32_t)args->thread_id;
	delete args;

	/* BSGS context from config */
	bsgs_context_t *bctx = (bsgs_context_t *)config->runtime.bsgs_context;

	/* Config-derived local variables */
	Secp256K1 *secp = (Secp256K1 *)config->runtime.secp;
	bool FLAGMATRIX = config->search.matrix_mode;
	bool FLAGQUIET = config->search.quiet_mode;
	struct thread_counter *steps = (struct thread_counter *)config->runtime.thread_counters;
	struct thread_flag *ends = (struct thread_flag *)config->runtime.thread_flags;
	std::atomic<int> &THREADOUTPUT = *(std::atomic<int> *)config->runtime.thread_output;
	Int &n_range_start = *(Int *)config->runtime.range_start;
	Int &n_range_end = *(Int *)config->runtime.range_end;
	platform_mutex_t &bsgs_thread = *(platform_mutex_t *)config->runtime.bsgs_mutex;
	platform_mutex_t &write_keys = *(platform_mutex_t *)config->runtime.write_mutex;

	/* BSGS context reads */
	Int &BSGS_N_double = *bctx->BSGS_N_double;
	Int &BSGS_M = *bctx->BSGS_M;
	Int &BSGS_M_double = *bctx->BSGS_M_double;
	std::vector<Point> &GSn = *bctx->GSn;
	Point &_2GSn = *bctx->_2GSn;
	std::vector<Point> &OriginalPointsBSGS = *bctx->OriginalPointsBSGS;
	bool *OriginalPointsBSGScompressed = bctx->OriginalPointsBSGScompressed;
	std::atomic<int> *bsgs_found = bctx->bsgs_found;
	bloom_extended_t *bloom_bP = bctx->bloom_bP;
	uint64_t bsgs_aux = bctx->bsgs_aux;
	uint32_t bsgs_point_number = bctx->bsgs_point_number;
	uint64_t BSGS_BUFFERXPOINTLENGTH = bctx->BSGS_BUFFERXPOINTLENGTH;

	FILE *filekey;
	uint8_t xpoint_raw[16];
	char *aux_c,*hextemp;
	Int base_key,keyfound;
	Point base_point,point_aux,point_found,offset_point;
	uint32_t k,l,r,salir,entrar;
	uint64_t cycles;

	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Point startP;

	int hLength = (CPU_GRP_SIZE / 2 - 1);

	Int dx[CPU_GRP_SIZE / 2 + 1];

	Int dy;
	Int dyn;
	Int _s;
	Int _p;
	Int intaux;
	Point pp;
	Point pn;
	grp->Set(dx);

	// Batch context for optimized operations
	bsgs_batch_ctx_t batch_ctx;

	profile_set_thread((int)thread_number);

	/* Validate BSGS N/M values */
	if (validate_bsgs_nm_values(thread_number, bctx, config->bsgs.k_factor) != 0) {
		output_error("[Thread %u] Aborting due to impractical BSGS parameters\n", thread_number);
		delete grp;
		return (platform_thread_return_t)0;
	}

	// Initialize batch context
	if (bsgs_batch_init(&batch_ctx, BSGS_BATCH_SIZE) != 0) {
		output_error("Failed to initialize BSGS batch context in thread %u\n", thread_number);
		delete grp;
		return (platform_thread_return_t)0;
	}

	cycles = bsgs_aux / 1024;
	if(bsgs_aux % 1024 != 0)	{
		cycles++;
	}

	intaux.Set(&BSGS_M_double);
	intaux.Mult(CPU_GRP_SIZE/2);
	intaux.Add(&BSGS_M);
	offset_point = secp->ComputePublicKey(&intaux);

	entrar = 1;
	/*
		while base_key is less than n_range_end then:
	*/
	do	{

platform_mutex_lock(&bsgs_thread);
		if(n_range_end.IsGreater(&n_range_start) && n_range_end.IsGreaterOrEqual(&BSGS_N_double))	{
			n_range_end.Sub(&BSGS_N_double);
			if(n_range_end.IsLower(&n_range_start))	{
				base_key.Set(&n_range_start);
			}
			else	{
				base_key.Set(&n_range_end);
			}
		}
		else	{
			entrar = 0;
		}
platform_mutex_unlock(&bsgs_thread);
		if(entrar == 0)
			break;

		if(FLAGMATRIX)	{
			aux_c = base_key.GetBase16();
			output_success("Thread 0x%s \n",aux_c);
			fflush(stdout);
			free(aux_c);
		}
		else	{
			if(!FLAGQUIET){
				aux_c = base_key.GetBase16();
				printf("\r[+] Thread 0x%s   \r",aux_c);
				fflush(stdout);
				free(aux_c);
				THREADOUTPUT.store(1, std::memory_order_release);
			}
		}

		base_point = secp->ComputePublicKey(&base_key);
		point_aux = secp->AddDirect(base_point, offset_point);
		point_aux = secp->Negation(point_aux);

		for(k = 0; k < bsgs_point_number ; k++)	{
			if(bsgs_found[k].load(std::memory_order_relaxed) == 0)	{
				startP  = secp->AddDirect(OriginalPointsBSGS[k],point_aux);
				uint64_t j = 0;
				while( j < cycles && bsgs_found[k].load(std::memory_order_relaxed) == 0 )	{
					// Use optimized batch point computation
					bsgs_batch_compute_points(&batch_ctx, &startP, GSn.data(), &_2GSn, hLength);

					// Check all computed points against bloom filter
					for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k].load(std::memory_order_relaxed) == 0; i++) {
						batch_ctx.pts[i].x.GetHi16Bytes(xpoint_raw);
						r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
						if(r) {
							r = bsgs_secondcheck(bctx,&base_key,((j*1024) + i),k,&keyfound);
							if(r)	{
								hextemp = keyfound.GetBase16();
								output_success("Thread Key found privkey %s   \n",hextemp);
								point_found = secp->ComputePublicKey(&keyfound);
								aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
								output_success("Publickey %s\n",aux_c);
platform_mutex_lock(&write_keys);

								filekey = fopen_secure_append("KEYFOUNDKEYFOUND.txt");
								if(filekey != NULL)	{
									fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
									fclose(filekey);
								} else {
									output_error("Could not write to KEYFOUNDKEYFOUND.txt\n");
								}
								free(hextemp);
								free(aux_c);
platform_mutex_unlock(&write_keys);

								bsgs_found[k].store(1, std::memory_order_release);
								salir = 1;
								for(l = 0; l < bsgs_point_number && salir; l++)	{
									salir &= bsgs_found[l].load(std::memory_order_acquire);
								}
								if(salir)	{
									printf("All points were found\n");
									exit(EXIT_SUCCESS);
								}
							} //End if second check
						}//End if first check

					}// For for pts variable

					// Next start point (startP += (bsSize*GRP_SIZE).G)

					pp = startP;
					dy.ModSub(&_2GSn.y,&pp.y);

					_s.ModMulK1(&dy,&batch_ctx.dx[hLength + 1]);
					_p.ModSquareK1(&_s);

					pp.x.ModNeg();
					pp.x.ModAdd(&_p);
					pp.x.ModSub(&_2GSn.x);

					pp.y.ModSub(&_2GSn.x,&pp.x);
					pp.y.ModMulK1(&_s);
					pp.y.ModSub(&_2GSn.y);
					startP = pp;
					j++;
				}//while all the aMP points
			}// End if
		}
		steps[thread_number].value+=2;
	}while(1);
	ends[thread_number].value = 1;
	bsgs_batch_free(&batch_ctx);
	delete grp;
	return (platform_thread_return_t)0;
}

/* ============================================================================
 * thread_process_bsgs_both - Bidirectional BSGS search
 * ============================================================================ */

platform_thread_return_t PLATFORM_THREAD_CALL thread_process_bsgs_both(void *vargp) {
	/* Extract config and thread ID from thread_args */
	thread_args *args = (thread_args *)vargp;
	keyhunt_config_t *config = args->config;
	uint32_t thread_number = (uint32_t)args->thread_id;
	delete args;

	/* BSGS context from config */
	bsgs_context_t *bctx = (bsgs_context_t *)config->runtime.bsgs_context;

	/* Config-derived local variables */
	Secp256K1 *secp = (Secp256K1 *)config->runtime.secp;
	bool FLAGMATRIX = config->search.matrix_mode;
	bool FLAGQUIET = config->search.quiet_mode;
	struct thread_counter *steps = (struct thread_counter *)config->runtime.thread_counters;
	struct thread_flag *ends = (struct thread_flag *)config->runtime.thread_flags;
	std::atomic<int> &THREADOUTPUT = *(std::atomic<int> *)config->runtime.thread_output;
	Int &n_range_end = *(Int *)config->runtime.range_end;
	platform_mutex_t &bsgs_thread = *(platform_mutex_t *)config->runtime.bsgs_mutex;
	platform_mutex_t &write_keys = *(platform_mutex_t *)config->runtime.write_mutex;

	/* BSGS context reads */
	Int &BSGS_CURRENT = *bctx->BSGS_CURRENT;
	Int &BSGS_N_double = *bctx->BSGS_N_double;
	Int &BSGS_M = *bctx->BSGS_M;
	Int &BSGS_M_double = *bctx->BSGS_M_double;
	std::vector<Point> &GSn = *bctx->GSn;
	Point &_2GSn = *bctx->_2GSn;
	std::vector<Point> &OriginalPointsBSGS = *bctx->OriginalPointsBSGS;
	bool *OriginalPointsBSGScompressed = bctx->OriginalPointsBSGScompressed;
	std::atomic<int> *bsgs_found = bctx->bsgs_found;
	bloom_extended_t *bloom_bP = bctx->bloom_bP;
	uint64_t bsgs_aux = bctx->bsgs_aux;
	uint32_t bsgs_point_number = bctx->bsgs_point_number;
	uint64_t BSGS_BUFFERXPOINTLENGTH = bctx->BSGS_BUFFERXPOINTLENGTH;

	FILE *filekey;
	uint8_t xpoint_raw[16];
	char *aux_c,*hextemp;
	Int base_key,keyfound;
	Point base_point,point_aux,point_found,offset_point;
	uint32_t k,l,r,salir,entrar;
	uint64_t cycles;

	IntGroup *grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);
	Point startP;

	int hLength = (CPU_GRP_SIZE / 2 - 1);

	Int dx[CPU_GRP_SIZE / 2 + 1];

	Int dy;
	Int dyn;
	Int _s;
	Int _p;
	Int intaux;
	Point pp;
	Point pn;
	struct thread_rand_state rand_state;
	grp->Set(dx);

	// Batch context for optimized operations
	bsgs_batch_ctx_t batch_ctx;

	profile_set_thread((int)thread_number);

	/* Validate BSGS N/M values */
	if (validate_bsgs_nm_values(thread_number, bctx, config->bsgs.k_factor) != 0) {
		output_error("[Thread %u] Aborting due to impractical BSGS parameters\n", thread_number);
		delete grp;
		return (platform_thread_return_t)0;
	}

	thread_rand_init(&rand_state, (uint64_t)thread_number ^ (uint64_t)time(NULL));

	// Initialize batch context
	if (bsgs_batch_init(&batch_ctx, BSGS_BATCH_SIZE) != 0) {
		output_error("Failed to initialize BSGS batch context in thread %u\n", thread_number);
		delete grp;
		return (platform_thread_return_t)0;
	}

	cycles = bsgs_aux / 1024;
	if(bsgs_aux % 1024 != 0)	{
		cycles++;
	}
	intaux.Set(&BSGS_M_double);
	intaux.Mult(CPU_GRP_SIZE/2);
	intaux.Add(&BSGS_M);
	offset_point = secp->ComputePublicKey(&intaux);

	entrar = 1;


	/*
		while BSGS_CURRENT is less than n_range_end
	*/
	do	{

		r = (uint32_t)thread_rand_n(&rand_state, 2);
platform_mutex_lock(&bsgs_thread);
		switch(r)	{
			case 0:	//TOP
				if(n_range_end.IsGreater(&BSGS_CURRENT) && n_range_end.IsGreaterOrEqual(&BSGS_N_double))	{
						n_range_end.Sub(&BSGS_N_double);
						/*
						n_range_end.Sub(&BSGS_N);
						n_range_end.Sub(&BSGS_N);
						*/
						if(n_range_end.IsLower(&BSGS_CURRENT))	{
							base_key.Set(&BSGS_CURRENT);
						}
						else	{
							base_key.Set(&n_range_end);
						}
				}
				else	{
					entrar = 0;
				}
			break;
			case 1: //BOTTOM
				if(BSGS_CURRENT.IsLower(&n_range_end))	{
					base_key.Set(&BSGS_CURRENT);
					//BSGS_N_double
					BSGS_CURRENT.Add(&BSGS_N_double);
					/*
					BSGS_CURRENT.Add(&BSGS_N);
					BSGS_CURRENT.Add(&BSGS_N);
					*/
				}
				else	{
					entrar = 0;
				}
			break;
		}
platform_mutex_unlock(&bsgs_thread);

		if(entrar == 0)
			break;


		if(FLAGMATRIX)	{
			aux_c = base_key.GetBase16();
			output_success("Thread 0x%s \n",aux_c);
			fflush(stdout);
			free(aux_c);
		}
		else	{
			if(!FLAGQUIET){
				aux_c = base_key.GetBase16();
				printf("\r[+] Thread 0x%s   \r",aux_c);
				fflush(stdout);
				free(aux_c);
				THREADOUTPUT.store(1, std::memory_order_release);
			}
		}

		base_point = secp->ComputePublicKey(&base_key);
		point_aux = secp->AddDirect(base_point, offset_point);
		point_aux = secp->Negation(point_aux);

		for(k = 0; k < bsgs_point_number ; k++)	{
			if(bsgs_found[k].load(std::memory_order_relaxed) == 0)	{
					startP  = secp->AddDirect(OriginalPointsBSGS[k],point_aux);
					uint64_t j = 0;
					while( j < cycles && bsgs_found[k].load(std::memory_order_relaxed) == 0 )	{
						// Use optimized batch point computation
						bsgs_batch_compute_points(&batch_ctx, &startP, GSn.data(), &_2GSn, hLength);

						// Check all computed points against bloom filter
						for(size_t i = 0; i<CPU_GRP_SIZE && bsgs_found[k].load(std::memory_order_relaxed) == 0; i++) {
							batch_ctx.pts[i].x.GetHi16Bytes(xpoint_raw);
							r = bloom_ext_check(&bloom_bP[((unsigned char)xpoint_raw[0])], xpoint_raw, (int)BSGS_BUFFERXPOINTLENGTH);
							if(r) {
								r = bsgs_secondcheck(bctx,&base_key,((j*1024) + i),k,&keyfound);
								if(r)	{
									hextemp = keyfound.GetBase16();
									output_success("Thread Key found privkey %s   \n",hextemp);
									point_found = secp->ComputePublicKey(&keyfound);
									aux_c = secp->GetPublicKeyHex(OriginalPointsBSGScompressed[k],point_found);
									output_success("Publickey %s\n",aux_c);
platform_mutex_lock(&write_keys);

									filekey = fopen_secure_append("KEYFOUNDKEYFOUND.txt");
									if(filekey != NULL)	{
										fprintf(filekey,"Key found privkey %s\nPublickey %s\n",hextemp,aux_c);
										fclose(filekey);
									} else {
										output_error("Could not write to KEYFOUNDKEYFOUND.txt\n");
									}
									free(hextemp);
									free(aux_c);
platform_mutex_unlock(&write_keys);

									bsgs_found[k].store(1, std::memory_order_release);
									salir = 1;
									for(l = 0; l < bsgs_point_number && salir; l++)	{
										salir &= bsgs_found[l].load(std::memory_order_acquire);
									}
									if(salir)	{
										printf("All points were found\n");
										exit(EXIT_SUCCESS);
									}
								} //End if second check
							}//End if first check

						}// For for pts variable

						// Next start point (startP += (bsSize*GRP_SIZE).G)

						pp = startP;
						dy.ModSub(&_2GSn.y,&pp.y);

						_s.ModMulK1(&dy,&batch_ctx.dx[hLength + 1]);
						_p.ModSquareK1(&_s);

						pp.x.ModNeg();
						pp.x.ModAdd(&_p);
						pp.x.ModSub(&_2GSn.x);

						pp.y.ModSub(&_2GSn.x,&pp.x);
						pp.y.ModMulK1(&_s);
						pp.y.ModSub(&_2GSn.y);
						startP = pp;

						j++;
					}//while all the aMP points
			}// End if
		}
			steps[thread_number].value+=2;
	}while(1);
	ends[thread_number].value = 1;
	bsgs_batch_free(&batch_ctx);
	delete grp;
	return (platform_thread_return_t)0;
}
