/*
 * io.cpp - File I/O operations for keyhunt
 *
 * MIGRATION STATUS: Fully config-wired (Phase 4, Plan 07)
 *
 * All functions receive keyhunt_config_t* parameter. No extern globals.
 * Functions read configuration flags from config->search/bsgs fields and
 * write output state (N, addressTable, bloom) through config->runtime fields.
 */

#include "io.h"
#include "../search/search_context.h"
#include "../crypto/address_util.h"
#include "../crypto/bloom_init.h"
#include "../sort/sort.h"
#include "../output.h"
#include "../platform/platform.h"
#include "../core/util.h"
#include "../core/sysinfo.h"
#include "../error/enhanced_error.h"
#include "../hash/sha256.h"
#include "../base58/libbase58.h"
#include "../bech32/bech32.h"
#include "../secure_file.h"
#include "../globals.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef __linux__
#include <sys/mman.h>
#endif

void checkpointer(void *ptr, const char *file, const char *function, const char *name, int line) {
	if(ptr == NULL) {
		// Get system info for available memory diagnostic
		system_info_t sysinfo;
		sysinfo_init(&sysinfo);

		// Create enhanced error report
		error_report_t report;
		error_context_t ctx = ERROR_CONTEXT_VALUES(
			ERROR_CAT_MEMORY,
			ERROR_SEV_FATAL,
			function,
			name,
			0,  // We don't know required memory here
			sysinfo.ram_available
		);
		ctx.file = file;
		ctx.line = line;

		error_report(&ctx, &report);
		error_fatal(&report);
	}
}

void writekey(const keyhunt_config_t *config, bool compressed, Int *key) {
	/* Extract config-mapped state into local shadow variables */
	Secp256K1 *secp = (Secp256K1 *)config->runtime.secp;
	platform_mutex_t *write_mutex = (platform_mutex_t *)config->runtime.write_mutex;
	Int *rangeStart = (Int *)config->runtime.range_progress_start;
	Int *rangeEnd = (Int *)config->runtime.range_progress_end;

	if (!secp) {
		fprintf(stderr, "[E] writekey: secp not initialized\n");
		return;
	}

	// Range validation: skip keys outside the original search range
	// This prevents false positives from key negation producing out-of-range keys
	if (rangeStart && rangeEnd) {
		if (key->IsLower(rangeStart) || key->IsGreaterOrEqual(rangeEnd)) {
			return;
		}
	}

	Point publickey;
	FILE *keys;
	char *hextemp, *hexrmd, public_key_hex[132], address[50], bech32_address[100], rmdhash[20];
	memset(address, 0, 50);
	memset(bech32_address, 0, 100);
	memset(public_key_hex, 0, 132);
	hextemp = key->GetBase16();
	publickey = secp->ComputePublicKey(key);
	secp->GetPublicKeyHex(compressed, publickey, public_key_hex);
	secp->GetHash160(P2PKH, compressed, publickey, (uint8_t*)rmdhash);
	hexrmd = tohex(rmdhash, 20);
	rmd160toaddress_dst(rmdhash, address);

	// Generate Bech32 address (P2WPKH) - only for compressed keys
	if (compressed) {
		if (!segwit_addr_encode(bech32_address, "bc", 0, (const uint8_t*)rmdhash, 20)) {
			// If encoding fails, use empty string
			strcpy(bech32_address, "[bech32 encoding failed]");
		}
	} else {
		// Bech32 addresses require compressed keys
		strcpy(bech32_address, "[bech32 requires compressed]");
	}

	platform_mutex_lock(write_mutex);

	printf("\nHit! Private Key: %s\npubkey: %s\nAddress %s\nBech32 %s\nrmd160 %s\n",hextemp,public_key_hex,address,bech32_address,hexrmd);

	// Colored box display
	printf("\n");
	output_key_found(hextemp, address, public_key_hex);
	printf("\n");

	keys = fopen_secure_append("KEYFOUNDKEYFOUND.txt");
	if(keys != NULL) {
		fprintf(keys,"Private Key: %s\npubkey: %s\nAddress: %s\nBech32: %s\nrmd160 %s\n",hextemp,public_key_hex,address,bech32_address,hexrmd);
		fclose(keys);
	}
	platform_mutex_unlock(write_mutex);
	free(hextemp);
	free(hexrmd);
}

void writekeyeth(const keyhunt_config_t *config, Int *key) {
	/* Extract config-mapped state into local shadow variables */
	Secp256K1 *secp = (Secp256K1 *)config->runtime.secp;
	platform_mutex_t *write_mutex = (platform_mutex_t *)config->runtime.write_mutex;
	Int *rangeStart = (Int *)config->runtime.range_progress_start;
	Int *rangeEnd = (Int *)config->runtime.range_progress_end;

	if (!secp) {
		fprintf(stderr, "[E] writekeyeth: secp not initialized\n");
		return;
	}

	// Range validation: skip keys outside the original search range
	if (rangeStart && rangeEnd) {
		if (key->IsLower(rangeStart) || key->IsGreaterOrEqual(rangeEnd)) {
			return;
		}
	}

	Point publickey;
	FILE *keys;
	char *hextemp, address[43], hash[20];
	hextemp = key->GetBase16();
	publickey = secp->ComputePublicKey(key);
	generate_binaddress_eth(publickey, (unsigned char*)hash);
	address[0] = '0';
	address[1] = 'x';
	tohex_dst(hash, 20, address+2);

	platform_mutex_lock(write_mutex);

	printf("\nHit! Private Key: %s\naddress: %s\n",hextemp,address);

	output_key_found(hextemp, address, NULL);

	keys = fopen_secure_append("KEYFOUNDKEYFOUND.txt");
	if(keys != NULL) {
		fprintf(keys,"Private Key: %s\naddress: %s\n",hextemp,address);
		fclose(keys);
	}

	platform_mutex_unlock(write_mutex);
	free(hextemp);
}

bool processOneVanity(keyhunt_config_t *config) {
	int vanity_rmd_targets = config->runtime.vanity_targets;
	int vanity_rmd_total = config->runtime.vanity_total;
	int *vanity_rmd_limits = (int *)config->runtime.vanity_limits;
	uint8_t ***vanity_rmd_limit_values_A = (uint8_t ***)config->runtime.vanity_values_a;
	int vanity_rmd_minimun_bytes_check_length = config->runtime.vanity_min_check_len;
	struct bloom *vanity_bloom = (struct bloom *)config->runtime.vanity_bloom;

	int i, k;
	if(vanity_rmd_targets == 0) {
		output_error("There aren't any vanity targets\n");
		return false;
	}

	if(!initBloomFilter(vanity_bloom, vanity_rmd_total))
		return false;

	for(i = 0; i < vanity_rmd_targets; i++) {
		for(k = 0; k < vanity_rmd_limits[i]; k++) {
			bloom_add(vanity_bloom, vanity_rmd_limit_values_A[i][k], vanity_rmd_minimun_bytes_check_length);
		}
	}
	return true;
}

bool readFileVanity(char *fileName, keyhunt_config_t *config) {
	int vanity_rmd_targets = config->runtime.vanity_targets;
	int *vanity_rmd_limits = (int *)config->runtime.vanity_limits;
	uint8_t ***vanity_rmd_limit_values_A = (uint8_t ***)config->runtime.vanity_values_a;
	int vanity_rmd_minimun_bytes_check_length = config->runtime.vanity_min_check_len;
	struct bloom *vanity_bloom = (struct bloom *)config->runtime.vanity_bloom;
	uint64_t N = (uint64_t)config->runtime.address_count;

	/* Forward declaration for function defined in keyhunt.cpp */
	int addvanity(char *target);

	FILE *fileDescriptor;
	int i, k, len;
	char aux[100];

	fileDescriptor = fopen(fileName, "r");
	if(fileDescriptor == NULL) {
		if(vanity_rmd_targets == 0) {
			output_error("There aren't any vanity targets\n");
			return false;
		}
	}
	else {
		while(fgets(aux, 100, fileDescriptor) != NULL) {
			trim(aux, " \t\n\r");
			len = strlen(aux);
			if(len > 0 && len < 36) {
				if(isValidBase58String(aux)) {
					addvanity(aux);
				}
				else {
					output_error("the string \"%s\" is not valid Base58, omiting it\n", aux);
				}
			}
		}
		fclose(fileDescriptor);
	}

	/* Re-read vanity state from globals since addvanity() modifies them directly */
	vanity_rmd_targets = ::vanity_rmd_targets;
	vanity_rmd_limits = ::vanity_rmd_limits;
	vanity_rmd_limit_values_A = ::vanity_rmd_limit_values_A;
	vanity_rmd_minimun_bytes_check_length = ::vanity_rmd_minimun_bytes_check_length;
	vanity_bloom = ::vanity_bloom;
	N = ::vanity_rmd_total;

	if(!initBloomFilter(vanity_bloom, N))
		return false;

	for(i = 0; i < vanity_rmd_targets; i++) {
		for(k = 0; k < vanity_rmd_limits[i]; k++) {
			bloom_add(vanity_bloom, vanity_rmd_limit_values_A[i][k], vanity_rmd_minimun_bytes_check_length);
		}
	}

	/* Write updated vanity state back to config */
	config->runtime.vanity_targets = vanity_rmd_targets;
	config->runtime.vanity_total = (int)N;
	config->runtime.vanity_bloom = (void *)vanity_bloom;
	config->runtime.vanity_limits = (void *)vanity_rmd_limits;
	config->runtime.vanity_values_a = (void *)vanity_rmd_limit_values_A;
	config->runtime.vanity_values_b = (void *)::vanity_rmd_limit_values_B;
	config->runtime.vanity_min_check_len = vanity_rmd_minimun_bytes_check_length;
	config->runtime.vanity_addresses = (void *)::vanity_address_targets;
	config->runtime.address_count = (int64_t)N;

	return true;
}

bool readFileAddress(char *fileName, keyhunt_config_t *config) {
	/* Read flags from config */
	int FLAGMODE = (int)config->search.mode;
	int FLAGCRYPTO = (int)config->search.crypto_type;
	bool save_progress = config->bsgs.save_progress;
	bool skip_checksum = config->search.skip_checksum;
	bool is_vanity = (config->search.mode == MODE_VANITY);
	int FLAGREADEDFILE1 = config->runtime.io_read_cached;
	int MAXLENGTHADDRESS = config->runtime.max_address_length;
	uint64_t N = (uint64_t)config->runtime.address_count;
	bloom_extended_t *bloom_ptr = (bloom_extended_t *)config->runtime.bloom_filter;
	/* bloom is stack-local when not from cache; for cache path we need the actual struct */
	bloom_extended_t bloom_local;
	if (bloom_ptr) {
		bloom_local = *bloom_ptr;
	} else {
		memset(&bloom_local, 0, sizeof(bloom_local));
	}
	struct address_value *addressTable = (struct address_value *)config->runtime.address_table;

	FILE *fileDescriptor;
	char fileBloomName[30];
	uint8_t checksum[32], hexPrefix[9];
	char dataChecksum[32], bloomChecksum[32];
	size_t bytesRead;
	uint64_t dataSize;
	/*
		if the save_progress is Set to 1 we need to the checksum and check if we have that information already saved
	*/
	if(save_progress) {
		if(!sha256_file((const char*)fileName, checksum)) {
			output_error("sha256_file error line %i\n", __LINE__ - 1);
			return false;
		}
		tohex_dst((char*)checksum, 4, (char*)hexPrefix);
		snprintf(fileBloomName, 30, "data_%s.dat", hexPrefix);
		fileDescriptor = fopen(fileBloomName, "rb");
		if(fileDescriptor != NULL) {
			output_success("Reading file %s\n", fileBloomName);

			//read bloom checksum (expected value to be checked)
			bytesRead = fread(bloomChecksum, 1, 32, fileDescriptor);
			if(bytesRead != 32) {
				output_error("Errore reading file, code line %i\n", __LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}

			//read bloom filter structure
			bytesRead = fread(&bloom_local.orig, 1, sizeof(struct bloom), fileDescriptor);
			if(bytesRead != sizeof(struct bloom)) {
				output_error("Error reading file, code line %i\n", __LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}

			output_success("Bloom filter for %" PRIu64 " elements.\n", bloom_local.orig.entries);

			/* Validate deserialized bloom size to prevent heap overflow from corrupted cache */
			const uint64_t MAX_BLOOM_CACHE_BYTES = (uint64_t)16 * 1024 * 1024 * 1024; /* 16 GB */
			if(bloom_local.orig.bytes == 0 || bloom_local.orig.bytes > MAX_BLOOM_CACHE_BYTES) {
				output_error("Bloom filter cache has invalid size %" PRIu64 " bytes. Delete the .blm file and retry.\n", bloom_local.orig.bytes);
				fclose(fileDescriptor);
				return false;
			}

			const bool cache_fast = (bloom_local.orig.major == BLOOM_EXT_FAST_MAJOR && bloom_local.orig.minor == BLOOM_EXT_FAST_MINOR);
#if defined(_WIN64) && !defined(__CYGWIN__)
			if (cache_fast) {
				bloom_local.orig.bf = (uint8_t*)_aligned_malloc(bloom_local.orig.bytes, 64);
			} else {
				bloom_local.orig.bf = (uint8_t*)malloc(bloom_local.orig.bytes);
			}
#else
			if (cache_fast) {
				void *ptr = NULL;
				if (posix_memalign(&ptr, 64, bloom_local.orig.bytes) != 0) ptr = NULL;
				bloom_local.orig.bf = (uint8_t*)ptr;
			} else {
				bloom_local.orig.bf = (uint8_t*)malloc(bloom_local.orig.bytes);
			}
#endif
			if(bloom_local.orig.bf == NULL) {
				output_error("Error allocating memory, code line %i\n", __LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}

			//read bloom filter data
			bytesRead = fread(bloom_local.orig.bf, 1, bloom_local.orig.bytes, fileDescriptor);
			if(bytesRead != bloom_local.orig.bytes) {
				output_error("Error reading file, code line %i\n", __LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}
			if(!skip_checksum) {
				//calculate checksum of the current readed data
				sha256((uint8_t*)bloom_local.orig.bf, bloom_local.orig.bytes, (uint8_t*)checksum);

				//Compare checksums
				if(memcmp(checksum, bloomChecksum, 32) != 0) {
					output_error("Error checksum mismatch, code line %i\n", __LINE__ - 2);
					fclose(fileDescriptor);
					return false;
				}
			}

			// If this cache was built with the fast bloom implementation, restore fast metadata
			bloom_ext_sync_from_orig(&bloom_local);
#if USE_FAST_BLOOM
			if (bloom_ext_is_fast(&bloom_local)) {
				output_success("Using FAST bloom filter (cached)\n");
			}
#endif
#ifdef __linux__
			if (bloom_ext_is_fast(&bloom_local)) {
				(void)madvise(bloom_local.fast.bf, bloom_local.fast.bits / 8, MADV_HUGEPAGE);
			}
#endif

			bytesRead = fread(dataChecksum, 1, 32, fileDescriptor);
			if(bytesRead != 32) {
				output_error("Errore reading file, code line %i\n", __LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}

			bytesRead = fread(&dataSize, 1, sizeof(uint64_t), fileDescriptor);
			if(bytesRead != sizeof(uint64_t)) {
				output_error("Errore reading file, code line %i\n", __LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}
			N = dataSize / sizeof(struct address_value);

			output_success("Allocating memory for %" PRIu64 " elements: %.2f MB\n", N, (double)(((double) sizeof(struct address_value)*N)/(double)1048576));

			addressTable = (struct address_value*) malloc(dataSize);
			if(addressTable == NULL) {
				output_error("Error allocating memory, code line %i\n", __LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}

			bytesRead = fread(addressTable, 1, dataSize, fileDescriptor);
			if(bytesRead != dataSize) {
				output_error("Error reading file, code line %i\n", __LINE__ - 2);
				fclose(fileDescriptor);
				return false;
			}
			if(!skip_checksum) {
				sha256((uint8_t*)addressTable, dataSize, (uint8_t*)checksum);
				if(memcmp(checksum, dataChecksum, 32) != 0) {
					output_error("Error checksum mismatch, code line %i\n", __LINE__ - 2);
					fclose(fileDescriptor);
					return false;
				}
			}
			FLAGREADEDFILE1 = 1;
			fclose(fileDescriptor);
			MAXLENGTHADDRESS = sizeof(struct address_value);

			/* Write back to config */
			config->runtime.address_count = (int64_t)N;
			config->runtime.address_table = (void *)addressTable;
			config->runtime.max_address_length = MAXLENGTHADDRESS;
			config->runtime.io_read_cached = FLAGREADEDFILE1;
			/* Write bloom back: copy local bloom to the global bloom via config */
			if (bloom_ptr) {
				*bloom_ptr = bloom_local;
			}
		}
	}
	if(is_vanity) {
		processOneVanity(config);
	}
	if(!FLAGREADEDFILE1) {
		switch(FLAGMODE) {
			case MODE_ADDRESS:
				if(FLAGCRYPTO == CRYPTO_BTC) {
					return forceReadFileAddress(fileName, config);
				}
				if(FLAGCRYPTO == CRYPTO_ETH) {
					return forceReadFileAddressEth(fileName, config);
				}
			break;
			case MODE_MINIKEYS:
			case MODE_RMD160:
				return forceReadFileAddress(fileName, config);
			break;
			case MODE_XPOINT:
				return forceReadFileXPoint(fileName, config);
			break;
			default:
				return false;
			break;
		}
	}
	return true;
}

bool forceReadFileAddress(char *fileName, keyhunt_config_t *config) {
	int MAXLENGTHADDRESS = config->runtime.max_address_length;
	uint64_t N = (uint64_t)config->runtime.address_count;
	bloom_extended_t *bloom_ptr = (bloom_extended_t *)config->runtime.bloom_filter;
	struct address_value *addressTable = (struct address_value *)config->runtime.address_table;

	FILE *fileDescriptor;
	bool validAddress;
	uint64_t numberItems, i;
	size_t r, raw_value_length;
	uint8_t rawvalue[50];
	char aux[100];
	fileDescriptor = fopen(fileName, "r");
	if(fileDescriptor == NULL) {
		output_error("Error opening the file %s, line %i\n", fileName, __LINE__ - 2);
		return false;
	}

	/*Count lines in the file*/
	numberItems = 0;
	while(fgets(aux, 100, fileDescriptor) != NULL) {
		trim(aux, " \t\n\r");
		r = strlen(aux);
		if(r > 20) {
			numberItems++;
		}
	}
	fseek(fileDescriptor, 0, SEEK_SET);
	MAXLENGTHADDRESS = 20;

	if(numberItems > SIZE_MAX / sizeof(struct address_value)) {
		output_error("Too many items (%" PRIu64 "), allocation would overflow\n", numberItems);
		fclose(fileDescriptor);
		return false;
	}
	output_success("Allocating memory for %" PRIu64 " elements: %.2f MB\n", numberItems, (double)(((double) sizeof(struct address_value)*numberItems)/(double)1048576));
	addressTable = (struct address_value*) malloc(sizeof(struct address_value)*numberItems);
	checkpointer((void *)addressTable, __FILE__, "malloc", "addressTable", __LINE__ -1);

	if(!initBloomFilterExt(bloom_ptr, numberItems)) {
		free(addressTable);
		addressTable = NULL;
		fclose(fileDescriptor);
		return false;
	}

	i = 0;
	while(i < numberItems) {
		validAddress = false;
		memset(aux, 0, 100);
		memset(addressTable[i].value, 0, sizeof(struct address_value));
		if(fgets(aux, 100, fileDescriptor) == NULL) break;
		trim(aux, " \t\n\r");
		r = strlen(aux);
		if(r > 0 && r <= 90) {	// Extended to support Bech32 addresses (up to ~62 chars)
			if(r < 40 && isValidBase58String(aux)) {	//Base58 Address
				raw_value_length = 25;
				b58tobin(rawvalue, &raw_value_length, aux, r);
				if(raw_value_length == 25) {
					bloom_ext_add(bloom_ptr, rawvalue+1, sizeof(struct address_value));
					memcpy(addressTable[i].value, rawvalue+1, sizeof(struct address_value));
					i++;
					validAddress = true;
				}
			}
			else if(r == 40 && isValidHex(aux)) {	//Raw RMD160 hex
				hexs2bin(aux, rawvalue);
				bloom_ext_add(bloom_ptr, rawvalue, sizeof(struct address_value));
				memcpy(addressTable[i].value, rawvalue, sizeof(struct address_value));
				i++;
				validAddress = true;
			}
			else if(r >= 42 && r <= 90 && isValidBech32String(aux)) {	//Bech32 Address (bc1q...)
				int witver;
				uint8_t witprog[40];
				size_t witprog_len;
				// Decode Bech32 address to get witness program
				if(segwit_addr_decode(&witver, witprog, &witprog_len, "bc", aux)) {
					// For P2WPKH (witness v0, 20-byte program), use witness program as hash
					if(witver == 0 && witprog_len == 20) {
						bloom_ext_add(bloom_ptr, witprog, sizeof(struct address_value));
						memcpy(addressTable[i].value, witprog, sizeof(struct address_value));
						i++;
						validAddress = true;
					}
					// For P2WSH (witness v0, 32-byte program), skip for now (not RMD160)
					else if(witver == 0 && witprog_len == 32) {
						output_info("Skipping P2WSH address (32-byte witness program): %s\n", aux);
						numberItems--;
					}
					else {
						output_info("Unsupported witness version %d or program length %zu: %s\n", witver, witprog_len, aux);
						numberItems--;
					}
				}
				else {
					output_info("Failed to decode Bech32 address: %s\n", aux);
					numberItems--;
				}
			}
		}
		if(!validAddress) {
			output_info("Ommiting invalid line %s\n", aux);
			numberItems--;
		}
	}
	N = numberItems;
	fclose(fileDescriptor);

	/* Write back to config */
	config->runtime.address_count = (int64_t)N;
	config->runtime.address_table = (void *)addressTable;
	config->runtime.max_address_length = MAXLENGTHADDRESS;

	return true;
}

bool forceReadFileAddressEth(char *fileName, keyhunt_config_t *config) {
	int MAXLENGTHADDRESS = config->runtime.max_address_length;
	uint64_t N = (uint64_t)config->runtime.address_count;
	bloom_extended_t *bloom_ptr = (bloom_extended_t *)config->runtime.bloom_filter;
	struct address_value *addressTable = (struct address_value *)config->runtime.address_table;

	FILE *fileDescriptor;
	bool validAddress;
	uint64_t numberItems, i;
	size_t r;
	uint8_t rawvalue[50];
	char aux[100];
	fileDescriptor = fopen(fileName, "r");
	if(fileDescriptor == NULL) {
		output_error("Error opening the file %s, line %i\n", fileName, __LINE__ - 2);
		return false;
	}
	/*Count lines in the file*/
	numberItems = 0;
	while(fgets(aux, 100, fileDescriptor) != NULL) {
		trim(aux, " \t\n\r");
		r = strlen(aux);
		if(r >= 40) {
			numberItems++;
		}
	}
	fseek(fileDescriptor, 0, SEEK_SET);

	MAXLENGTHADDRESS = 20;
	N = numberItems;

	if(numberItems > SIZE_MAX / sizeof(struct address_value)) {
		output_error("Too many items (%" PRIu64 "), allocation would overflow\n", numberItems);
		fclose(fileDescriptor);
		return false;
	}
	output_success("Allocating memory for %" PRIu64 " elements: %.2f MB\n", numberItems, (double)(((double) sizeof(struct address_value)*numberItems)/(double)1048576));
	addressTable = (struct address_value*) malloc(sizeof(struct address_value)*numberItems);
	checkpointer((void *)addressTable, __FILE__, "malloc", "addressTable", __LINE__ -1);

	if(!initBloomFilterExt(bloom_ptr, N)) {
		free(addressTable);
		addressTable = NULL;
		fclose(fileDescriptor);
		return false;
	}

	i = 0;
	while(i < numberItems) {
		validAddress = false;
		memset(aux, 0, 100);
		memset(addressTable[i].value, 0, sizeof(struct address_value));
		if(fgets(aux, 100, fileDescriptor) == NULL) break;
		trim(aux, " \t\n\r");
		r = strlen(aux);
		if(r >= 40 && r <= 42) {
			switch(r) {
				case 40:
					if(isValidHex(aux)) {
						hexs2bin(aux, rawvalue);
						bloom_ext_add(bloom_ptr, rawvalue, sizeof(struct address_value));
						memcpy(addressTable[i].value, rawvalue, sizeof(struct address_value));
						i++;
						validAddress = true;
					}
				break;
				case 42:
					if(aux[0] != '0' || (aux[1] != 'x' && aux[1] != 'X')) {
						numberItems--;
						continue;
					}
					if(isValidHex(aux+2)) {
						hexs2bin(aux+2, rawvalue);
						bloom_ext_add(bloom_ptr, rawvalue, sizeof(struct address_value));
						memcpy(addressTable[i].value, rawvalue, sizeof(struct address_value));
						i++;
						validAddress = true;
					}
				break;
				default:
				break;
			}
		}
		if(!validAddress) {
			output_info("Ommiting invalid line %s\n", aux);
			numberItems--;
		}
	}

	fclose(fileDescriptor);

	N = numberItems;

	/* Write back to config */
	config->runtime.address_count = (int64_t)N;
	config->runtime.address_table = (void *)addressTable;
	config->runtime.max_address_length = MAXLENGTHADDRESS;

	return true;
}

bool forceReadFileXPoint(char *fileName, keyhunt_config_t *config) {
	int MAXLENGTHADDRESS = config->runtime.max_address_length;
	uint64_t N = (uint64_t)config->runtime.address_count;
	bloom_extended_t *bloom_ptr = (bloom_extended_t *)config->runtime.bloom_filter;
	struct address_value *addressTable = (struct address_value *)config->runtime.address_table;

	FILE *fileDescriptor;
	uint64_t numberItems, i;
	size_t r, lenaux;
	uint8_t rawvalue[100];
	char aux[1000], *hextemp;
	Tokenizer tokenizer_xpoint{};
	fileDescriptor = fopen(fileName, "r");
	if(fileDescriptor == NULL) {
		output_error("Error opening the file %s, line %i\n", fileName, __LINE__ - 2);
		return false;
	}
	/*Count lines in the file*/
	numberItems = 0;
	while(fgets(aux, 1000, fileDescriptor) != NULL) {
		trim(aux, " \t\n\r");
		r = strlen(aux);
		if(r >= 40) {
			numberItems++;
		}
	}
	fseek(fileDescriptor, 0, SEEK_SET);

	MAXLENGTHADDRESS = 20;

	if(numberItems > SIZE_MAX / sizeof(struct address_value)) {
		output_error("Too many items (%" PRIu64 "), allocation would overflow\n", numberItems);
		fclose(fileDescriptor);
		return false;
	}
	output_success("Allocating memory for %" PRIu64 " elements: %.2f MB\n", numberItems, (double)(((double) sizeof(struct address_value)*numberItems)/(double)1048576));
	addressTable = (struct address_value*) malloc(sizeof(struct address_value)*numberItems);
	checkpointer((void *)addressTable, __FILE__, "malloc", "addressTable", __LINE__ - 1);

	N = numberItems;

	if(!initBloomFilterExt(bloom_ptr, N)) {
		free(addressTable);
		addressTable = NULL;
		fclose(fileDescriptor);
		return false;
	}

	i = 0;
	while(i < N) {
		memset(aux, 0, 1000);
		if(fgets(aux, 1000, fileDescriptor) == NULL) break;
		memset((void *)&addressTable[i], 0, sizeof(struct address_value));
		trim(aux, " \t\n\r");
		stringtokenizer(aux, &tokenizer_xpoint);
		hextemp = nextToken(&tokenizer_xpoint);
		bool entry_valid = false;
		if(hextemp != NULL) {
			lenaux = strlen(hextemp);
			if(isValidHex(hextemp)) {
				switch(lenaux) {
					case 64:	/*X value*/
						r = hexs2bin(aux, (uint8_t*) rawvalue);
						if(r) {
							memcpy(addressTable[i].value, rawvalue, 20);
							bloom_ext_add(bloom_ptr, rawvalue, MAXLENGTHADDRESS);
							entry_valid = true;
						}
						else {
							output_error("error hexs2bin\n");
						}
					break;
					case 66:	/*Compress publickey*/
						r = hexs2bin(aux+2, (uint8_t*)rawvalue);
						if(r) {
							memcpy(addressTable[i].value, rawvalue, 20);
							bloom_ext_add(bloom_ptr, rawvalue, MAXLENGTHADDRESS);
							entry_valid = true;
						}
						else {
							output_error("error hexs2bin\n");
						}
					break;
					case 130:	/* Uncompress publickey length*/
						r = hexs2bin(aux, (uint8_t*) rawvalue);
						if(r) {
							memcpy(addressTable[i].value, rawvalue+2, 20);
							bloom_ext_add(bloom_ptr, rawvalue+2, MAXLENGTHADDRESS);
							entry_valid = true;
						}
						else {
							output_error("error hexs2bin\n");
						}
					break;
					default:
						output_error("Omiting line unknow length size %li: %s\n", lenaux, aux);
					break;
				}
			}
			else {
				output_error("Ignoring invalid hexvalue %s\n", aux);
			}
			freetokenizer(&tokenizer_xpoint);
		}
		else {
			output_error("Omiting line : %s\n", aux);
		}
		if(entry_valid) {
			i++;
		} else {
			N--;
		}
	}
	fclose(fileDescriptor);

	/* Write back to config */
	config->runtime.address_count = (int64_t)N;
	config->runtime.address_table = (void *)addressTable;
	config->runtime.max_address_length = MAXLENGTHADDRESS;

	return true;
}

void writeFileIfNeeded(const char *fileName, keyhunt_config_t *config) {
	bool save_progress = config->bsgs.save_progress;
	int FLAGREADEDFILE1 = config->runtime.io_read_cached;
	bool debug_mode = config->search.debug_mode;
	uint64_t N = (uint64_t)config->runtime.address_count;
	bloom_extended_t *bloom_ptr = (bloom_extended_t *)config->runtime.bloom_filter;
	struct address_value *addressTable = (struct address_value *)config->runtime.address_table;

	if(save_progress && !FLAGREADEDFILE1) {
		FILE *fileDescriptor;
		char fileBloomName[30];
		uint8_t checksum[32], hexPrefix[9];
		char dataChecksum[32], bloomChecksum[32];
		size_t bytesWrite;
		uint64_t dataSize;
		if(!sha256_file((const char*)fileName, checksum)) {
			output_error("sha256_file error line %i\n", __LINE__ - 1);
			exit(EXIT_FAILURE);
		}
		tohex_dst((char*)checksum, 4, (char*)hexPrefix);
		snprintf(fileBloomName, 30, "data_%s.dat", hexPrefix);
		fileDescriptor = fopen(fileBloomName, "wb");
		dataSize = N * (sizeof(struct address_value));
		if (debug_mode) {
			printf("[D] size data %" PRIu64 "\n", dataSize);
		}
		if(fileDescriptor != NULL) {
			output_success("Writing file %s ", fileBloomName);

			sha256((uint8_t*)bloom_ptr->orig.bf, bloom_ptr->orig.bytes, (uint8_t*)bloomChecksum);
			printf(".");
			bytesWrite = fwrite(bloomChecksum, 1, 32, fileDescriptor);
			if(bytesWrite != 32) {
				output_error("Errore writing file, code line %i\n", __LINE__ - 2);
				exit(EXIT_FAILURE);
			}
			printf(".");

			bytesWrite = fwrite(&bloom_ptr->orig, 1, sizeof(struct bloom), fileDescriptor);
			if(bytesWrite != sizeof(struct bloom)) {
				output_error("Error writing file, code line %i\n", __LINE__ - 2);
				exit(EXIT_FAILURE);
			}
			printf(".");

			bytesWrite = fwrite(bloom_ptr->orig.bf, 1, bloom_ptr->orig.bytes, fileDescriptor);
			if(bytesWrite != bloom_ptr->orig.bytes) {
				output_error("Error writing file, code line %i\n", __LINE__ - 2);
				fclose(fileDescriptor);
				exit(EXIT_FAILURE);
			}
			printf(".");

			sha256((uint8_t*)addressTable, dataSize, (uint8_t*)dataChecksum);
			printf(".");

			bytesWrite = fwrite(dataChecksum, 1, 32, fileDescriptor);
			if(bytesWrite != 32) {
				output_error("Errore writing file, code line %i\n", __LINE__ - 2);
				exit(EXIT_FAILURE);
			}
			printf(".");

			bytesWrite = fwrite(&dataSize, 1, sizeof(uint64_t), fileDescriptor);
			if(bytesWrite != sizeof(uint64_t)) {
				output_error("Errore writing file, code line %i\n", __LINE__ - 2);
				exit(EXIT_FAILURE);
			}
			printf(".");

			bytesWrite = fwrite(addressTable, 1, dataSize, fileDescriptor);
			if(bytesWrite != dataSize) {
				output_error("Error writing file, code line %i\n", __LINE__ - 2);
				exit(EXIT_FAILURE);
			}
			printf(".");

			config->runtime.io_read_cached = 1;
			fclose(fileDescriptor);
			printf("\n");
		}
		else {
			output_error("Error can't create the file %s\n", fileBloomName);
			exit(EXIT_FAILURE);
		}
	}
}
