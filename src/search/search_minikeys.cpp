/*
 * search_minikeys.cpp - Minikey mode search implementation
 *
 * MIGRATION STATUS: Config-wired (reads from keyhunt_config_t, no extern globals)
 *
 * This file implements the MINIKEYS search mode which generates and validates
 * Bitcoin minikeys (23-character base58 strings starting with 'S').
 *
 * Minikey format: S<21 base58 chars>
 * Validation: SHA256(minikey + "?")[0] == 0x00
 * Private key: SHA256(minikey) (22 bytes, no '?')
 *
 * Current implementation:
 * - thread_process_minikeys: Main worker thread for minikey search
 * - set_minikey: Convert raw buffer to base58 minikey characters
 * - increment_minikey_index: Increment a specific position with carry
 * - increment_minikey_N: Increment by N using lookup table with carry
 *
 * All state is received through thread_args->config (keyhunt_config_t*).
 * No extern globals are accessed except profile_set_thread (keyhunt.cpp).
 */

#include "search_common.h"
#include "secp256k1/Random.h"
#include "bloom/bloom.h"
#include "hash/sha256.h"
#include "../io/io.h"
#include "../output.h"
#include "../core/util.h"
#include "../sort/sort.h"
#include "../secure_file.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>

/* From crypto/address_util.cpp (C linkage via address_util.h) */
extern "C" {
void sha256sse_22(uint8_t *src0, uint8_t *src1, uint8_t *src2, uint8_t *src3,
                  uint8_t *dst0, uint8_t *dst1, uint8_t *dst2, uint8_t *dst3);
void sha256sse_23(uint8_t *src0, uint8_t *src1, uint8_t *src2, uint8_t *src3,
                  uint8_t *dst0, uint8_t *dst1, uint8_t *dst2, uint8_t *dst3);
}

/* From crypto/address_util.cpp */
extern "C" void rmd160toaddress_dst(char *rmd, char *dst);

/* profile_set_thread is defined in keyhunt.cpp */
extern void profile_set_thread(int idx);

/* ============================================================================
 * Minikey Utility Functions
 * ============================================================================ */

/*
 * set_minikey - Convert raw byte data into base58 minikey characters
 *
 * Uses the coinbuffer lookup table to map raw byte values (0-57)
 * to the base58 alphabet characters.
 */
void set_minikey(char *buffer, char *rawbuffer, int length, char *coinbuffer) {
	for (int i = 0; i < length; i++) {
		buffer[i] = coinbuffer[(uint8_t)rawbuffer[i]];
	}
}

/*
 * increment_minikey_index - Increment a specific position in the minikey
 *
 * Recursively increments the value at the specified index in the raw data
 * array. If the value exceeds 57 (base58 limit), it resets to 0 and carries
 * to the previous position.
 *
 * Returns false if carry propagates past index 0 (overflow).
 */
bool increment_minikey_index(char *buffer, char *rawbuffer, int index, char *coinbuffer) {
	if (rawbuffer[index] < 57) {
		rawbuffer[index]++;
		buffer[index] = coinbuffer[(uint8_t)rawbuffer[index]];
	}
	else {
		rawbuffer[index] = 0x00;
		buffer[index] = coinbuffer[0];
		if (index > 0) {
			return increment_minikey_index(buffer, rawbuffer, index - 1, coinbuffer);
		}
		else {
			return false;
		}
	}
	return true;
}

/*
 * increment_minikey_N - Increment minikey raw buffer by N positions
 *
 * Uses the minikeyN_buf lookup table to add a multi-digit base58 number
 * to the raw buffer, handling carry-over when values exceed 57.
 * The n_limit controls how many digits are processed.
 */
void increment_minikey_N(char *rawbuffer, char *minikeyN_buf, int n_limit) {
	int i = 20, j = 0;
	while (i > 0 && j < n_limit) {
		rawbuffer[i] = rawbuffer[i] + minikeyN_buf[i];
		if (rawbuffer[i] > 57) {
			rawbuffer[i] = rawbuffer[i] % 58;
			rawbuffer[i - 1]++;
		}
		i--;
		j++;
	}
	/* Propagate any remaining carries from the addition above */
	i = 20 - n_limit;
	while (i >= 0 && rawbuffer[i] > 57) {
		rawbuffer[i] = rawbuffer[i] % 58;
		if (i > 0) rawbuffer[i - 1]++;
		i--;
	}
}

/* ============================================================================
 * Minikey Thread Function
 * ============================================================================ */

platform_thread_return_t PLATFORM_THREAD_CALL thread_process_minikeys(void *vargp) {
	thread_args *args = (thread_args *)vargp;
	keyhunt_config_t *config = args->config;
	int thread_number = args->thread_id;
	delete args;

	/* Config reads (immutable after init) */
	Secp256K1 *secp = (Secp256K1 *)config->runtime.secp;
	bool is_random = config->search.random_mode;
	bool is_quiet = config->search.quiet_mode;
	bool is_matrix = config->search.matrix_mode;
	bool is_base_minikey = (config->search.mode == MODE_MINIKEYS);

	/* Runtime state */
	bloom_extended_t *bloom = (bloom_extended_t *)config->runtime.bloom_filter;
	struct address_value *addressTable = (struct address_value *)config->runtime.address_table;
	int64_t N = config->runtime.address_count;
	uint64_t N_SEQUENTIAL_MAX = config->runtime.sequential_max;
	struct thread_counter *steps = (struct thread_counter *)config->runtime.thread_counters;
	platform_mutex_t *write_keys = (platform_mutex_t *)config->runtime.write_mutex;
	platform_mutex_t *write_random = (platform_mutex_t *)config->runtime.random_mutex;

	/* Minikey-specific state */
	char *Ccoinbuffer = (char *)config->runtime.minikey_coinbuffer;
	char *raw_baseminikey = (char *)config->runtime.minikey_raw_base;
	char *minikeyN = (char *)config->runtime.minikey_n;
	int minikey_n_limit = config->runtime.minikey_n_limit;

	FILE *keys;
	Point publickey[4];
	Int key_mpz[4];
	uint64_t count;
	char publickeyhashrmd160_uncompress[4][20];
	char public_key_uncompressed_hex[131];
	char address[4][40], minikey[4][24], minikeys[8][24], buffer_b58[21], minikey2check[24], rawvalue[4][32];
	char *hextemp, *rawbuffer;
	int r, continue_flag = 1, k, j, count_valid;
	Int counter;

	profile_set_thread(thread_number);
	rawbuffer = (char *)&counter.bits64;
	count_valid = 0;
	for (k = 0; k < 4; k++) {
		minikey[k][0] = 'S';
		minikey[k][22] = '?';
		minikey[k][23] = 0x00;
	}
	minikey2check[0] = 'S';
	minikey2check[22] = '?';
	minikey2check[23] = 0x00;

	do {
		if (is_random) {
			counter.Rand(256);
			for (k = 0; k < 21; k++) {
				buffer_b58[k] = (uint8_t)((uint8_t)rawbuffer[k] % 58);
			}
		}
		else {
			if (is_base_minikey) {
				platform_mutex_lock(write_random);
				memcpy(buffer_b58, raw_baseminikey, 21);
				increment_minikey_N(raw_baseminikey, minikeyN, minikey_n_limit);
				platform_mutex_unlock(write_random);
			}
			else {
				platform_mutex_lock(write_random);
				if (raw_baseminikey == NULL) {
					raw_baseminikey = (char *)malloc(22);
					checkpointer((void *)raw_baseminikey, __FILE__, "malloc", "raw_baseminikey", __LINE__ - 1);
					counter.Rand(256);
					for (k = 0; k < 21; k++) {
						raw_baseminikey[k] = (uint8_t)((uint8_t)rawbuffer[k] % 58);
					}
					memcpy(buffer_b58, raw_baseminikey, 21);
					increment_minikey_N(raw_baseminikey, minikeyN, minikey_n_limit);
				}
				else {
					memcpy(buffer_b58, raw_baseminikey, 21);
					increment_minikey_N(raw_baseminikey, minikeyN, minikey_n_limit);
				}
				platform_mutex_unlock(write_random);
			}
		}
		set_minikey(minikey2check + 1, buffer_b58, 21, Ccoinbuffer);
		if (continue_flag) {
			count = 0;
			if (is_matrix) {
				output_success("Base minikey: %s     \n", minikey2check);
				fflush(stdout);
			}
			else {
				if (!is_quiet) {
					printf("\r[+] Base minikey: %s     \r", minikey2check);
					fflush(stdout);
				}
			}
			do {
				for (j = 0; j < 256; j++) {

					if (count_valid > 0) {
						for (k = 0; k < count_valid; k++) {
							memcpy(minikeys[k], minikeys[4 + k], 22);
						}
					}
					do {
						increment_minikey_index(minikey2check + 1, buffer_b58, 20, Ccoinbuffer);
						memcpy(minikey[0] + 1, minikey2check + 1, 21);
						increment_minikey_index(minikey2check + 1, buffer_b58, 20, Ccoinbuffer);
						memcpy(minikey[1] + 1, minikey2check + 1, 21);
						increment_minikey_index(minikey2check + 1, buffer_b58, 20, Ccoinbuffer);
						memcpy(minikey[2] + 1, minikey2check + 1, 21);
						increment_minikey_index(minikey2check + 1, buffer_b58, 20, Ccoinbuffer);
						memcpy(minikey[3] + 1, minikey2check + 1, 21);

						sha256sse_23((uint8_t *)minikey[0], (uint8_t *)minikey[1], (uint8_t *)minikey[2], (uint8_t *)minikey[3], (uint8_t *)rawvalue[0], (uint8_t *)rawvalue[1], (uint8_t *)rawvalue[2], (uint8_t *)rawvalue[3]);
						for (k = 0; k < 4; k++) {
							if (rawvalue[k][0] == 0x00) {
								memcpy(minikeys[count_valid], minikey[k], 22);
								count_valid++;
							}
						}
					} while (count_valid < 4);
					count_valid -= 4;
					sha256sse_22((uint8_t *)minikeys[0], (uint8_t *)minikeys[1], (uint8_t *)minikeys[2], (uint8_t *)minikeys[3], (uint8_t *)rawvalue[0], (uint8_t *)rawvalue[1], (uint8_t *)rawvalue[2], (uint8_t *)rawvalue[3]);

					for (k = 0; k < 4; k++) {
						key_mpz[k].Set32Bytes((uint8_t *)rawvalue[k]);
						publickey[k] = secp->ComputePublicKey(&key_mpz[k]);
					}

					secp->GetHash160(P2PKH, false, publickey[0], publickey[1], publickey[2], publickey[3], (uint8_t *)publickeyhashrmd160_uncompress[0], (uint8_t *)publickeyhashrmd160_uncompress[1], (uint8_t *)publickeyhashrmd160_uncompress[2], (uint8_t *)publickeyhashrmd160_uncompress[3]);

					for (k = 0; k < 4; k++) {
						r = bloom_ext_check_rmd160(bloom, (uint8_t *)publickeyhashrmd160_uncompress[k]);
						if (r) {
							r = searchbinary(addressTable, publickeyhashrmd160_uncompress[k], N);
							if (r) {
								/* hit */
								hextemp = key_mpz[k].GetBase16();
								secp->GetPublicKeyHex(false, publickey[k], public_key_uncompressed_hex);
								platform_mutex_lock(write_keys);

								keys = fopen_secure_append("KEYFOUNDKEYFOUND.txt");
								rmd160toaddress_dst(publickeyhashrmd160_uncompress[k], address[k]);
								minikeys[k][22] = '\0';
								if (keys != NULL) {
									fprintf(keys, "Private Key: %s\npubkey: %s\nminikey: %s\naddress: %s\n", hextemp, public_key_uncompressed_hex, minikeys[k], address[k]);
									fclose(keys);
								} else {
									output_error("Could not write to KEYFOUNDKEYFOUND.txt\n");
								}
								printf("\nHIT!! Private Key: %s\npubkey: %s\nminikey: %s\naddress: %s\n", hextemp, public_key_uncompressed_hex, minikeys[k], address[k]);
								platform_mutex_unlock(write_keys);

								free(hextemp);
							}
						}
					}
				}
				steps[thread_number].value++;
				count += 1024;
			} while (count < N_SEQUENTIAL_MAX && continue_flag);
		}
	} while (continue_flag);
	return (platform_thread_return_t)0;
}
