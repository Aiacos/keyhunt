/*
 * search_minikeys.cpp - Minikey mode search implementation
 *
 * MIGRATION STATUS: Config-aware (extern globals)
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
 * Extern globals used:
 * - Ccoinbuffer (base58 lookup table)
 * - raw_baseminikey, minikeyN, minikey_n_limit (minikey state)
 * - FLAGRANDOM, FLAGBASEMINIKEY, FLAGMATRIX, FLAGQUIET (flags)
 * - bloom (bloom filter for address matching)
 * - addressTable, N (target addresses)
 * - secp (SECP256K1 instance)
 * - steps (per-thread counters)
 * - N_SEQUENTIAL_MAX (iteration limit)
 * - write_keys, write_random (mutexes)
 *
 * See search_context.h for shared declarations and extern globals.
 */

#include "search_context.h"
#include "search_common.h"
#include "search_utils.h"
#include "../io/io.h"
#include "../crypto/address_util.h"
#include "../output.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>

/* ============================================================================
 * External Dependencies
 * These are defined in keyhunt.cpp and linked at compile time
 * ============================================================================ */

extern int searchbinary(struct address_value *buffer, char *data, int64_t array_length);

/* Globals from search_context.h not covered by search_common.h */
extern int FLAGBASEMINIKEY;
extern int FLAGRANDOM;
extern int FLAGMATRIX;
extern int FLAGQUIET;
extern uint64_t N;
extern uint64_t N_SEQUENTIAL_MAX;
extern uint64_t *steps;
extern struct address_value *addressTable;
extern bloom_extended_t bloom;

/* ============================================================================
 * Minikey Utility Functions
 * ============================================================================ */

/*
 * set_minikey - Convert raw byte data into base58 minikey characters
 *
 * Uses the Ccoinbuffer lookup table to map raw byte values (0-57)
 * to the base58 alphabet characters.
 */
void set_minikey(char *buffer, char *rawbuffer, int length) {
	for (int i = 0; i < length; i++) {
		buffer[i] = Ccoinbuffer[(uint8_t)rawbuffer[i]];
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
bool increment_minikey_index(char *buffer, char *rawbuffer, int index) {
	if (rawbuffer[index] < 57) {
		rawbuffer[index]++;
		buffer[index] = Ccoinbuffer[(uint8_t)rawbuffer[index]];
	}
	else {
		rawbuffer[index] = 0x00;
		buffer[index] = Ccoinbuffer[0];
		if (index > 0) {
			return increment_minikey_index(buffer, rawbuffer, index - 1);
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
 * Uses the minikeyN lookup table to add a multi-digit base58 number
 * to the raw buffer, handling carry-over when values exceed 57.
 * The minikey_n_limit controls how many digits are processed.
 */
void increment_minikey_N(char *rawbuffer) {
	int i = 20, j = 0;
	while (i > 0 && j < minikey_n_limit) {
		rawbuffer[i] = rawbuffer[i] + minikeyN[i];
		if (rawbuffer[i] > 57) {	/* Handling carry-over if value exceeds 57 */
			rawbuffer[i] = rawbuffer[i] % 58;
			rawbuffer[i - 1]++;
		}
		i--;
		j++;
	}
}

/* ============================================================================
 * Minikey Thread Function
 * ============================================================================ */

#if defined(_WIN64) && !defined(__CYGWIN__)
DWORD WINAPI thread_process_minikeys(LPVOID vargp) {
#else
void *thread_process_minikeys(void *vargp) {
#endif
	FILE *keys;
	Point publickey[4];
	Int key_mpz[4];
	struct tothread *tt;
	uint64_t count;
	char publickeyhashrmd160_uncompress[4][20];
	char public_key_uncompressed_hex[131];
	char address[4][40], minikey[4][24], minikeys[8][24], buffer_b58[21], minikey2check[24], rawvalue[4][32];
	char *hextemp, *rawbuffer;
	int r, thread_number, continue_flag = 1, k, j, count_valid;
	Int counter;
	tt = (struct tothread *)vargp;
	thread_number = tt->nt;
	free(tt);
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
		if (FLAGRANDOM) {
			counter.Rand(256);
			for (k = 0; k < 21; k++) {
				buffer_b58[k] = (uint8_t)((uint8_t)rawbuffer[k] % 58);
			}
		}
		else {
			if (FLAGBASEMINIKEY) {
				platform_mutex_lock(&write_random);
				memcpy(buffer_b58, raw_baseminikey, 21);
				increment_minikey_N(raw_baseminikey);
				platform_mutex_unlock(&write_random);
			}
			else {
				platform_mutex_lock(&write_random);
				if (raw_baseminikey == NULL) {
					raw_baseminikey = (char *)malloc(22);
					checkpointer((void *)raw_baseminikey, __FILE__, "malloc", "raw_baseminikey", __LINE__ - 1);
					counter.Rand(256);
					for (k = 0; k < 21; k++) {
						raw_baseminikey[k] = (uint8_t)((uint8_t)rawbuffer[k] % 58);
					}
					memcpy(buffer_b58, raw_baseminikey, 21);
					increment_minikey_N(raw_baseminikey);
				}
				else {
					memcpy(buffer_b58, raw_baseminikey, 21);
					increment_minikey_N(raw_baseminikey);
				}
				platform_mutex_unlock(&write_random);
			}
		}
		set_minikey(minikey2check + 1, buffer_b58, 21);
		if (continue_flag) {
			count = 0;
			if (FLAGMATRIX) {
				output_success("Base minikey: %s     \n", minikey2check);
				fflush(stdout);
			}
			else {
				if (!FLAGQUIET) {
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
						increment_minikey_index(minikey2check + 1, buffer_b58, 20);
						memcpy(minikey[0] + 1, minikey2check + 1, 21);
						increment_minikey_index(minikey2check + 1, buffer_b58, 20);
						memcpy(minikey[1] + 1, minikey2check + 1, 21);
						increment_minikey_index(minikey2check + 1, buffer_b58, 20);
						memcpy(minikey[2] + 1, minikey2check + 1, 21);
						increment_minikey_index(minikey2check + 1, buffer_b58, 20);
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
						r = bloom_ext_check_rmd160(&bloom, (uint8_t *)publickeyhashrmd160_uncompress[k]);
						if (r) {
							r = searchbinary(addressTable, publickeyhashrmd160_uncompress[k], N);
							if (r) {
								/* hit */
								hextemp = key_mpz[k].GetBase16();
								secp->GetPublicKeyHex(false, publickey[k], public_key_uncompressed_hex);
								platform_mutex_lock(&write_keys);

								keys = fopen("KEYFOUNDKEYFOUND.txt", "a+");
								rmd160toaddress_dst(publickeyhashrmd160_uncompress[k], address[k]);
								minikeys[k][22] = '\0';
								if (keys != NULL) {
									fprintf(keys, "Private Key: %s\npubkey: %s\nminikey: %s\naddress: %s\n", hextemp, public_key_uncompressed_hex, minikeys[k], address[k]);
									fclose(keys);
								}
								printf("\nHIT!! Private Key: %s\npubkey: %s\nminikey: %s\naddress: %s\n", hextemp, public_key_uncompressed_hex, minikeys[k], address[k]);
								platform_mutex_unlock(&write_keys);

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
	return NULL;
}
