#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include "hashing.h"
#include "../sha3/sha3.h"
#include "../hash/ripemd160.h"

int sha256(const unsigned char *data, size_t length, unsigned char *digest) {
    if (digest == NULL) {
        printf("Invalid SHA256 output buffer\n");
        return 1;
    }
    if (data == NULL && length != 0) {
        printf("Invalid SHA256 input buffer\n");
        return 1;
    }
    SHA256_CTX ctx;
    if (SHA256_Init(&ctx) != 1) {
        printf("Failed to initialize SHA256 context\n");
        return 1;
    }
    if (SHA256_Update(&ctx, data, length) != 1) {
        printf("Failed to update digest\n");
        return 1;
    }
    if (SHA256_Final(digest, &ctx) != 1) {
        printf("Failed to finalize digest\n");
        return 1;
    }
    return 0; // Success
}

int sha256_4(size_t length, const unsigned char *data0, const unsigned char *data1,
             const unsigned char *data2, const unsigned char *data3,
             unsigned char *digest0, unsigned char *digest1,
             unsigned char *digest2, unsigned char *digest3) {
    const unsigned char *inputs[4] = {data0, data1, data2, data3};
    unsigned char *outputs[4] = {digest0, digest1, digest2, digest3};
    SHA256_CTX ctx[4];

    for (size_t i = 0; i < 4; ++i) {
        if (outputs[i] == NULL) {
            printf("Invalid SHA256 output buffer at index %zu\n", i);
            return 1;
        }
        if (inputs[i] == NULL && length != 0) {
            printf("Invalid SHA256 input buffer at index %zu\n", i);
            return 1;
        }
        if (SHA256_Init(&ctx[i]) != 1) {
            printf("Failed to initialize SHA256 context %zu\n", i);
            return 1;
        }
    }

    for (size_t i = 0; i < 4; ++i) {
        if (length == 0) {
            continue;
        }
        if (SHA256_Update(&ctx[i], inputs[i], length) != 1) {
            printf("Failed to update SHA256 digest %zu\n", i);
            return 1;
        }
    }

    for (size_t i = 0; i < 4; ++i) {
        if (SHA256_Final(outputs[i], &ctx[i]) != 1) {
            printf("Failed to finalize SHA256 digest %zu\n", i);
            return 1;
        }
    }

    return 0; // Success
}

// Function for hashing
int keccak(const unsigned char *data, size_t length, unsigned char *digest) {
	if (digest == NULL)	{
		printf("Invalid KECCAK output buffer\n");
		return 1;
	}
	if (data == NULL && length != 0) {
		printf("Invalid KECCAK input buffer\n");
		return 1;
	}
	SHA3_256_CTX ctx;
	SHA3_256_Init(&ctx);
	SHA3_256_Update(&ctx,data,length);
	KECCAK_256_Final(digest,&ctx);
	return 0; // Success
}



int rmd160(const unsigned char *data, size_t length, unsigned char *digest) {
    if (digest == NULL) {
        printf("Invalid RIPEMD160 output buffer\n");
        return 1;
    }
    if (data == NULL && length != 0) {
        printf("Invalid RIPEMD160 input buffer\n");
        return 1;
    }

    if (length == 32) {
        ripemd160_32(data, digest);
        return 0;
    }

    RIPEMD160_CTX ctx;
    if (RIPEMD160_Init(&ctx) != 1) {
        printf("Failed to initialize RIPEMD-160 context\n");
        return 1;
    }
    if (RIPEMD160_Update(&ctx, data, length) != 1) {
        printf("Failed to update digest\n");
        return 1;
    }
    if (RIPEMD160_Final(digest, &ctx) != 1) {
        printf("Failed to finalize digest\n");
        return 1;
    }
    return 0; // Success
}

int rmd160_4(size_t length, const unsigned char *data0, const unsigned char *data1,
                const unsigned char *data2, const unsigned char *data3,
                unsigned char *digest0, unsigned char *digest1,
                unsigned char *digest2, unsigned char *digest3) {
    const unsigned char *inputs[4] = {data0, data1, data2, data3};
    unsigned char *outputs[4] = {digest0, digest1, digest2, digest3};
    RIPEMD160_CTX ctx[4];

    if (length == 32) {
        ripemd160sse_32(data0, data1, data2, data3,
                        digest0, digest1, digest2, digest3);
        return 0;
    }

    for (size_t i = 0; i < 4; ++i) {
        if (outputs[i] == NULL) {
            printf("Invalid RIPEMD160 output buffer at index %zu\n", i);
            return 1;
        }
        if (inputs[i] == NULL && length != 0) {
            printf("Invalid RIPEMD160 input buffer at index %zu\n", i);
            return 1;
        }
        if (RIPEMD160_Init(&ctx[i]) != 1) {
            printf("Failed to initialize RIPEMD-160 context %zu\n", i);
            return 1;
        }
    }

    for (size_t i = 0; i < 4; ++i) {
        if (length == 0) {
            continue;
        }
        if (RIPEMD160_Update(&ctx[i], inputs[i], length) != 1) {
            printf("Failed to update RIPEMD-160 digest %zu\n", i);
            return 1;
        }
    }

    for (size_t i = 0; i < 4; ++i) {
        if (RIPEMD160_Final(outputs[i], &ctx[i]) != 1) {
            printf("Failed to finalize RIPEMD-160 digest %zu\n", i);
            return 1;
        }
    }

    return 0; // Success
}

bool sha256_file(const char* file_name, unsigned char* digest) {
    if (file_name == NULL || digest == NULL) {
        printf("Invalid parameters for sha256_file\n");
        return false;
    }
    FILE* file = fopen(file_name, "rb");
    if (file == NULL) {
        printf("Failed to open file: %s (errno=%d)\n", file_name, errno);
        return false;
    }
    
    uint8_t buffer[8192]; // Buffer to read file contents
    size_t bytes_read;
    
    SHA256_CTX ctx;
    if (SHA256_Init(&ctx) != 1) {
        printf("Failed to initialize SHA256 context\n");
        fclose(file);
        return false;
    }
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (SHA256_Update(&ctx, buffer, bytes_read) != 1) {
            printf("Failed to update digest\n");
            fclose(file);
            return false;
        }
    }
    
    if (SHA256_Final(digest, &ctx) != 1) {
        printf("Failed to finalize digest\n");
        fclose(file);
        return false;
    }
    
    fclose(file);
    return true;
}
