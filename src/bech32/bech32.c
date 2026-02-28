/*
 * Bech32 encoding/decoding implementation
 * Based on BIP 173 (Bech32) and BIP 350 (Bech32m)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the standard MIT license.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bech32.h"

/* Bech32 character set */
static const char *charset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

/* Character to value mapping (-1 for invalid) */
static const int8_t charset_rev[128] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    15, -1, 10, 17, 21, 20, 26, 30,  7,  5, -1, -1, -1, -1, -1, -1,
    -1, 29, -1, 24, 13, 25,  9,  8, 23, -1, 18, 22, 31, 27, 19, -1,
     1,  0,  3, 16, 11, 28, 12, 14,  6,  4,  2, -1, -1, -1, -1, -1,
    -1, 29, -1, 24, 13, 25,  9,  8, 23, -1, 18, 22, 31, 27, 19, -1,
     1,  0,  3, 16, 11, 28, 12, 14,  6,  4,  2, -1, -1, -1, -1, -1
};

/* Generator values for BCH checksum */
static const uint32_t generator[5] = {
    0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3
};

/**
 * Compute the Bech32 checksum polymod
 */
static uint32_t bech32_polymod(const uint8_t *values, size_t len) {
    uint32_t chk = 1;
    size_t i, j;

    for (i = 0; i < len; ++i) {
        uint8_t top = chk >> 25;
        chk = (chk & 0x1ffffff) << 5 ^ values[i];
        for (j = 0; j < 5; ++j) {
            if ((top >> j) & 1) {
                chk ^= generator[j];
            }
        }
    }

    return chk;
}

/**
 * Expand the HRP for checksum computation
 */
static void bech32_hrp_expand(const char *hrp, uint8_t *output) {
    size_t i, len = strlen(hrp);

    for (i = 0; i < len; ++i) {
        output[i] = hrp[i] >> 5;
    }
    output[len] = 0;
    for (i = 0; i < len; ++i) {
        output[len + 1 + i] = hrp[i] & 0x1f;
    }
}

/**
 * Create checksum for Bech32 encoding
 * spec = 1 for Bech32, 0x2bc830a3 for Bech32m
 */
static void bech32_create_checksum(const char *hrp, const uint8_t *data,
                                   size_t data_len, uint8_t *checksum,
                                   uint32_t spec) {
    size_t hrp_len = strlen(hrp);
    if (hrp_len > 83 || data_len > 84) return;  /* BIP173 limits */
    size_t values_len = hrp_len * 2 + 1 + data_len + 6;
    uint8_t values[256];  /* Fixed buffer: max 83*2+1+84+6 = 257, but BIP173 caps at ~200 */
    uint32_t polymod;
    size_t i;

    if (values_len > sizeof(values)) return;

    bech32_hrp_expand(hrp, values);
    memcpy(values + hrp_len * 2 + 1, data, data_len);
    memset(values + hrp_len * 2 + 1 + data_len, 0, 6);

    polymod = bech32_polymod(values, values_len) ^ spec;

    for (i = 0; i < 6; ++i) {
        checksum[i] = (polymod >> (5 * (5 - i))) & 0x1f;
    }
}

/**
 * Verify Bech32 checksum
 * Returns the constant (1 for Bech32, 0x2bc830a3 for Bech32m, 0 for invalid)
 */
static uint32_t bech32_verify_checksum(const char *hrp, const uint8_t *data,
                                       size_t data_len) {
    size_t hrp_len = strlen(hrp);
    if (hrp_len > 83 || data_len > 90) return 0;  /* BIP173 limits */
    size_t values_len = hrp_len * 2 + 1 + data_len;
    uint8_t values[256];

    if (values_len > sizeof(values)) return 0;

    bech32_hrp_expand(hrp, values);
    memcpy(values + hrp_len * 2 + 1, data, data_len);

    return bech32_polymod(values, values_len);
}

/**
 * Convert from 8-bit to 5-bit encoding
 */
static int convert_bits(uint8_t *out, size_t *outlen, int outbits,
                       const uint8_t *in, size_t inlen, int inbits,
                       int pad) {
    uint32_t val = 0;
    int bits = 0;
    uint32_t maxv = (((uint32_t)1) << outbits) - 1;
    size_t out_idx = 0;
    size_t i;

    for (i = 0; i < inlen; ++i) {
        val = (val << inbits) | in[i];
        bits += inbits;
        while (bits >= outbits) {
            bits -= outbits;
            out[out_idx++] = (val >> bits) & maxv;
        }
    }

    if (pad) {
        if (bits) {
            out[out_idx++] = (val << (outbits - bits)) & maxv;
        }
    } else if (bits >= inbits || ((val << (outbits - bits)) & maxv)) {
        return 0;
    }

    *outlen = out_idx;
    return 1;
}

/**
 * Encode data to Bech32 string
 */
int bech32_encode(char *output, const char *hrp, const uint8_t *data,
                  size_t data_len) {
    uint8_t checksum[6] = {0};
    size_t i, hrp_len;

    if (!output || !hrp || !data) {
        return 0;
    }

    hrp_len = strlen(hrp);

    /* Validate HRP */
    for (i = 0; i < hrp_len; ++i) {
        if (hrp[i] < 33 || hrp[i] > 126) {
            return 0;
        }
    }

    /* Create checksum (using Bech32 constant = 1) */
    bech32_create_checksum(hrp, data, data_len, checksum, 1);

    /* Build output string */
    strcpy(output, hrp);
    output[hrp_len] = '1';

    for (i = 0; i < data_len; ++i) {
        if (data[i] >= 32) {
            return 0;
        }
        output[hrp_len + 1 + i] = charset[data[i]];
    }

    for (i = 0; i < 6; ++i) {
        output[hrp_len + 1 + data_len + i] = charset[checksum[i]];
    }

    output[hrp_len + 1 + data_len + 6] = '\0';

    return 1;
}

/**
 * Decode Bech32 string to data
 */
int bech32_decode(char *hrp, uint8_t *data, size_t *data_len,
                  const char *input) {
    size_t input_len, i, hrp_len = 0;
    int have_lower = 0, have_upper = 0;
    int separator_pos = -1;
    uint32_t checksum_const;

    if (!hrp || !data || !data_len || !input) {
        return 0;
    }

    input_len = strlen(input);

    /* Check length */
    if (input_len < 8 || input_len > 90) {
        return 0;
    }

    /* Find separator and validate characters */
    for (i = 0; i < input_len; ++i) {
        unsigned char c = input[i];

        if (c < 33 || c > 126) {
            return 0;
        }

        if (c >= 'a' && c <= 'z') {
            have_lower = 1;
        }
        if (c >= 'A' && c <= 'Z') {
            have_upper = 1;
        }

        if (c == '1') {
            separator_pos = i;
        }
    }

    /* Check for mixed case */
    if (have_lower && have_upper) {
        return 0;
    }

    /* Check separator position */
    if (separator_pos < 1 || separator_pos + 7 > (int)input_len) {
        return 0;
    }

    hrp_len = separator_pos;
    *data_len = input_len - separator_pos - 1;

    /* Extract HRP (convert to lowercase) */
    for (i = 0; i < hrp_len; ++i) {
        unsigned char c = input[i];
        if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }
        hrp[i] = c;
    }
    hrp[hrp_len] = '\0';

    /* Decode data part */
    for (i = 0; i < *data_len; ++i) {
        unsigned char c = input[separator_pos + 1 + i];

        /* Convert to lowercase */
        if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }

        if (c >= 128 || charset_rev[c] == -1) {
            return 0;
        }

        data[i] = charset_rev[c];
    }

    /* Verify checksum */
    checksum_const = bech32_verify_checksum(hrp, data, *data_len);

    /* Accept both Bech32 (1) and Bech32m (0x2bc830a3) */
    if (checksum_const != 1 && checksum_const != 0x2bc830a3) {
        return 0;
    }

    /* Remove checksum from data */
    *data_len -= 6;

    return 1;
}

/**
 * Encode SegWit address (high-level API)
 */
int segwit_addr_encode(char *output, const char *hrp, int witver,
                       const uint8_t *witprog, size_t witprog_len) {
    uint8_t data[65];
    size_t datalen;
    uint32_t checksum_const;

    if (!output || !hrp || !witprog) {
        return 0;
    }

    /* Validate witness version */
    if (witver < 0 || witver > 16) {
        return 0;
    }

    /* Validate witness program length */
    if (witprog_len < 2 || witprog_len > 40) {
        return 0;
    }

    /* P2WPKH must be 20 bytes, P2WSH must be 32 bytes */
    if (witver == 0 && witprog_len != 20 && witprog_len != 32) {
        return 0;
    }

    /* Convert witness program to 5-bit encoding */
    data[0] = witver;
    if (!convert_bits(data + 1, &datalen, 5, witprog, witprog_len, 8, 1)) {
        return 0;
    }

    datalen += 1;

    /* Use Bech32 for witness v0, Bech32m for v1+ */
    checksum_const = (witver == 0) ? 1 : 0x2bc830a3;

    /* Create checksum */
    {
        uint8_t checksum[6] = {0};
        size_t i, hrp_len = strlen(hrp);

        bech32_create_checksum(hrp, data, datalen, checksum, checksum_const);

        /* Build output string */
        strcpy(output, hrp);
        output[hrp_len] = '1';

        for (i = 0; i < datalen; ++i) {
            output[hrp_len + 1 + i] = charset[data[i]];
        }

        for (i = 0; i < 6; ++i) {
            output[hrp_len + 1 + datalen + i] = charset[checksum[i]];
        }

        output[hrp_len + 1 + datalen + 6] = '\0';
    }

    return 1;
}

/**
 * Decode SegWit address (high-level API)
 */
int segwit_addr_decode(int *witver, uint8_t *witprog, size_t *witprog_len,
                       const char *hrp, const char *addr) {
    uint8_t data[84];
    char hrp_actual[84];
    size_t data_len;
    uint32_t checksum_const;

    if (!witver || !witprog || !witprog_len || !hrp || !addr) {
        return 0;
    }

    /* Decode Bech32 */
    if (!bech32_decode(hrp_actual, data, &data_len, addr)) {
        return 0;
    }

    /* Verify HRP matches */
    if (strcmp(hrp, hrp_actual) != 0) {
        return 0;
    }

    /* Extract witness version */
    if (data_len < 1) {
        return 0;
    }

    *witver = data[0];

    /* Validate witness version */
    if (*witver > 16) {
        return 0;
    }

    /* Convert from 5-bit to 8-bit encoding */
    if (!convert_bits(witprog, witprog_len, 8, data + 1, data_len - 1, 5, 0)) {
        return 0;
    }

    /* Validate witness program length */
    if (*witprog_len < 2 || *witprog_len > 40) {
        return 0;
    }

    /* P2WPKH must be 20 bytes, P2WSH must be 32 bytes for v0 */
    if (*witver == 0 && *witprog_len != 20 && *witprog_len != 32) {
        return 0;
    }

    /* Verify checksum type matches witness version */
    checksum_const = bech32_verify_checksum(hrp_actual, data, data_len + 6);

    if (*witver == 0 && checksum_const != 1) {
        return 0;  /* v0 must use Bech32 */
    }

    if (*witver != 0 && checksum_const != 0x2bc830a3) {
        return 0;  /* v1+ must use Bech32m */
    }

    return 1;
}
