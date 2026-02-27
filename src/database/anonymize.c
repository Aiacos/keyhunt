// src/database/anonymize.c
#include "anonymize.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

// Simple hash function for string anonymization
static unsigned int hash_string(const char *str) {
    if (!str) return 0;
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

// Extract CPU generation/family from model string
static void extract_cpu_family_info(const char *model, char *vendor,
                                    char *family, char *generation) {
    if (!model || !vendor || !family || !generation) return;

    vendor[0] = family[0] = generation[0] = '\0';

    // Convert to lowercase for parsing
    char lower_model[256];
    snprintf(lower_model, sizeof(lower_model), "%s", model);
    for (int i = 0; lower_model[i]; i++) {
        lower_model[i] = tolower(lower_model[i]);
    }

    // Extract vendor
    if (strstr(lower_model, "intel")) {
        strcpy(vendor, "intel");
    } else if (strstr(lower_model, "amd")) {
        strcpy(vendor, "amd");
    } else if (strstr(lower_model, "arm")) {
        strcpy(vendor, "arm");
    } else {
        strcpy(vendor, "unknown");
    }

    // Extract CPU family
    if (strstr(lower_model, "core i9") || strstr(lower_model, "i9-")) {
        strcpy(family, "core_i9");
    } else if (strstr(lower_model, "core i7") || strstr(lower_model, "i7-")) {
        strcpy(family, "core_i7");
    } else if (strstr(lower_model, "core i5") || strstr(lower_model, "i5-")) {
        strcpy(family, "core_i5");
    } else if (strstr(lower_model, "core i3") || strstr(lower_model, "i3-")) {
        strcpy(family, "core_i3");
    } else if (strstr(lower_model, "xeon")) {
        strcpy(family, "xeon");
    } else if (strstr(lower_model, "ryzen 9")) {
        strcpy(family, "ryzen_9");
    } else if (strstr(lower_model, "ryzen 7")) {
        strcpy(family, "ryzen_7");
    } else if (strstr(lower_model, "ryzen 5")) {
        strcpy(family, "ryzen_5");
    } else if (strstr(lower_model, "threadripper")) {
        strcpy(family, "threadripper");
    } else if (strstr(lower_model, "epyc")) {
        strcpy(family, "epyc");
    } else {
        strcpy(family, "generic");
    }

    // Extract generation (Intel: 10xxx, 11xxx, 12xxx, etc.)
    // AMD: 3xxx, 5xxx, 7xxx series
    if (strcmp(vendor, "intel") == 0) {
        // Look for generation pattern like "12700K" or "i7-12700"
        const char *p = lower_model;
        while (*p) {
            if (isdigit(*p)) {
                int gen = (*p - '0');
                if (gen >= 1 && gen <= 9) {
                    if (gen >= 1 && gen <= 3) {
                        sprintf(generation, "gen%d", 10 + gen); // 11th, 12th, 13th gen
                    } else if (gen == 4 || gen == 5) {
                        sprintf(generation, "gen%d", gen); // 4th, 5th gen (older)
                    } else {
                        sprintf(generation, "gen%d", gen); // Generic
                    }
                    break;
                }
            }
            p++;
        }
        if (generation[0] == '\0') {
            strcpy(generation, "gen_unknown");
        }
    } else if (strcmp(vendor, "amd") == 0) {
        // AMD Ryzen: 3000 series (Zen 2), 5000 series (Zen 3), 7000 series (Zen 4)
        if (strstr(lower_model, "7000") || strstr(lower_model, "79")) {
            strcpy(generation, "zen4");
        } else if (strstr(lower_model, "5000") || strstr(lower_model, "59")) {
            strcpy(generation, "zen3");
        } else if (strstr(lower_model, "3000") || strstr(lower_model, "39")) {
            strcpy(generation, "zen2");
        } else {
            strcpy(generation, "zen_unknown");
        }
    } else {
        strcpy(generation, "gen_unknown");
    }
}

// Anonymize CPU model to family string
void anonymize_cpu_model(const char *cpu_model, char *cpu_family,
                        size_t family_size, anonymize_level_t level) {
    if (!cpu_model || !cpu_family || family_size == 0) return;

    char vendor[32], family[32], generation[32];
    extract_cpu_family_info(cpu_model, vendor, family, generation);

    if (level == ANONYMIZE_MINIMAL) {
        // Include generation for more specific grouping
        snprintf(cpu_family, family_size, "%s_%s_%s", vendor, family, generation);
    } else if (level == ANONYMIZE_FULL) {
        // Only vendor and family, no generation
        snprintf(cpu_family, family_size, "%s_%s", vendor, family);
    } else {
        // ANONYMIZE_NONE: use original (should not happen in anonymization)
        snprintf(cpu_family, family_size, "%s", cpu_model);
    }
}

// Extract GPU architecture from compute capability
static void get_gpu_architecture(int compute_capability, char *arch) {
    if (!arch) return;

    switch (compute_capability / 10) {
        case 5:  strcpy(arch, "maxwell"); break;
        case 6:  strcpy(arch, "pascal"); break;
        case 7:  strcpy(arch, "volta_turing"); break;
        case 8:  strcpy(arch, "ampere"); break;
        case 9:  strcpy(arch, "ada_hopper"); break;
        default: strcpy(arch, "unknown"); break;
    }
}

// Anonymize GPU model to family string
void anonymize_gpu_model(const char *gpu_name, int compute_capability,
                        char *gpu_family, size_t family_size,
                        anonymize_level_t level) {
    if (!gpu_family || family_size == 0) return;

    if (!gpu_name || gpu_name[0] == '\0') {
        strcpy(gpu_family, "no_gpu");
        return;
    }

    // Convert to lowercase for parsing
    char lower_name[256];
    snprintf(lower_name, sizeof(lower_name), "%s", gpu_name);
    for (int i = 0; lower_name[i]; i++) {
        lower_name[i] = tolower(lower_name[i]);
    }

    char vendor[32] = "unknown";
    char arch[32] = "unknown";
    char series[32] = "unknown";

    // Detect vendor
    if (strstr(lower_name, "nvidia") || strstr(lower_name, "geforce") ||
        strstr(lower_name, "rtx") || strstr(lower_name, "gtx")) {
        strcpy(vendor, "nvidia");
    } else if (strstr(lower_name, "amd") || strstr(lower_name, "radeon")) {
        strcpy(vendor, "amd");
    } else if (strstr(lower_name, "intel")) {
        strcpy(vendor, "intel");
    }

    // Get architecture from compute capability (NVIDIA specific)
    if (strcmp(vendor, "nvidia") == 0) {
        get_gpu_architecture(compute_capability, arch);

        // Detect series
        if (strstr(lower_name, "rtx 40") || strstr(lower_name, "4090") ||
            strstr(lower_name, "4080") || strstr(lower_name, "4070")) {
            strcpy(series, "rtx_40_series");
        } else if (strstr(lower_name, "rtx 30") || strstr(lower_name, "3090") ||
                   strstr(lower_name, "3080") || strstr(lower_name, "3070")) {
            strcpy(series, "rtx_30_series");
        } else if (strstr(lower_name, "rtx 20") || strstr(lower_name, "2080") ||
                   strstr(lower_name, "2070")) {
            strcpy(series, "rtx_20_series");
        } else if (strstr(lower_name, "gtx 16") || strstr(lower_name, "1660")) {
            strcpy(series, "gtx_16_series");
        } else if (strstr(lower_name, "gtx 10") || strstr(lower_name, "1080") ||
                   strstr(lower_name, "1070")) {
            strcpy(series, "gtx_10_series");
        } else {
            strcpy(series, "unknown_series");
        }
    }

    if (level == ANONYMIZE_MINIMAL) {
        // Include series for more specific grouping
        snprintf(gpu_family, family_size, "%s_%s_%s", vendor, arch, series);
    } else if (level == ANONYMIZE_FULL) {
        // Only vendor and architecture
        snprintf(gpu_family, family_size, "%s_%s", vendor, arch);
    } else {
        // ANONYMIZE_NONE
        snprintf(gpu_family, family_size, "%s", gpu_name);
    }
}

// Quantize RAM to power-of-2 buckets
int anonymize_quantize_ram(uint64_t ram_mb) {
    // Convert MB to GB and round to nearest power of 2
    int gb = (int)((ram_mb + 512) / 1024); // Round to nearest GB

    // Find next power of 2 bucket: 4, 8, 16, 32, 64, 128, 256, etc.
    int bucket = 4;
    while (bucket < gb && bucket < 1024) {
        bucket *= 2;
    }

    return bucket;
}

// Quantize L3 cache to standard buckets
int anonymize_quantize_cache_l3(uint64_t cache_kb) {
    int mb = (int)((cache_kb + 512) / 1024); // Convert to MB

    // Standard L3 cache sizes: 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 MB
    if (mb <= 4) return 4;
    if (mb <= 8) return 8;
    if (mb <= 12) return 12;
    if (mb <= 16) return 16;
    if (mb <= 24) return 24;
    if (mb <= 32) return 32;
    if (mb <= 48) return 48;
    if (mb <= 64) return 64;
    if (mb <= 96) return 96;
    return 128;
}

// Compute anonymized hardware hash
void anonymize_compute_hardware_hash(const anonymized_benchmark_t *anon,
                                    char *hash_out, size_t hash_size) {
    if (!anon || !hash_out || hash_size < 65) return;

    // Create fingerprint from anonymized data
    char fingerprint[512];
    snprintf(fingerprint, sizeof(fingerprint),
             "%s_%d_%d_%d_%d_%s_%d_%d",
             anon->cpu_family,
             anon->cpu_physical_cores,
             anon->has_avx2 ? 1 : 0,
             anon->has_avx512 ? 1 : 0,
             anon->ram_gb_bucket,
             anon->gpu_family,
             anon->gpu_sm_count,
             anon->gpu_compute_capability);

    // Generate hash (using simple hash function since we don't have crypto libs)
    unsigned int h1 = hash_string(fingerprint);
    unsigned int h2 = hash_string(anon->cpu_family);
    unsigned int h3 = hash_string(anon->gpu_family);
    unsigned int h4 = (unsigned int)(anon->cpu_physical_cores * 1000 + anon->ram_gb_bucket);
    unsigned int h5 = (unsigned int)(anon->has_avx2 << 16 | anon->has_avx512 << 8 | anon->has_sha_ni);
    unsigned int h6 = (unsigned int)(anon->gpu_sm_count * 100 + anon->gpu_compute_capability);
    unsigned int h7 = (unsigned int)(anon->cache_l3_mb_bucket);
    unsigned int h8 = hash_string(anon->mode);

    snprintf(hash_out, hash_size, "%08x%08x%08x%08x%08x%08x%08x%08x",
             h1, h2, h3, h4, h5, h6, h7, h8);
}

// Anonymize benchmark result
int anonymize_benchmark_result(const perfdb_benchmark_t *original,
                               anonymized_benchmark_t *anonymized,
                               anonymize_level_t level) {
    if (!original || !anonymized) return -1;

    // Copy non-identifying data directly
    strncpy(anonymized->mode, original->mode, sizeof(anonymized->mode) - 1);
    anonymized->mode[sizeof(anonymized->mode) - 1] = '\0';

    anonymized->bits = original->bits;

    strncpy(anonymized->key_type, original->key_type, sizeof(anonymized->key_type) - 1);
    anonymized->key_type[sizeof(anonymized->key_type) - 1] = '\0';

    // Performance metrics (not identifying)
    anonymized->cpu_speed_mkeys = original->cpu_speed_mkeys;
    anonymized->gpu_speed_mkeys = original->gpu_speed_mkeys;
    anonymized->hybrid_speed_mkeys = original->hybrid_speed_mkeys;
    anonymized->efficiency_ratio = original->efficiency_ratio;

    // Benchmark metadata
    anonymized->benchmark_duration_seconds = original->benchmark_duration_seconds;
    strncpy(anonymized->keyhunt_version, original->keyhunt_version,
            sizeof(anonymized->keyhunt_version) - 1);
    anonymized->keyhunt_version[sizeof(anonymized->keyhunt_version) - 1] = '\0';

    // Anonymize CPU model
    anonymize_cpu_model(original->hardware.cpu_model, anonymized->cpu_family,
                       sizeof(anonymized->cpu_family), level);

    // Copy non-identifying hardware specs
    anonymized->cpu_physical_cores = original->hardware.cpu_physical_cores;
    anonymized->cpu_logical_cores = original->hardware.cpu_logical_cores;
    anonymized->has_avx2 = original->hardware.has_avx2;
    anonymized->has_avx512 = original->hardware.has_avx512;
    anonymized->has_avx512f = original->hardware.has_avx512f;
    anonymized->has_sha_ni = original->hardware.has_sha_ni;

    // Quantize memory values for privacy
    anonymized->ram_gb_bucket = anonymize_quantize_ram(original->hardware.ram_total);
    anonymized->cache_l3_mb_bucket = anonymize_quantize_cache_l3(original->hardware.cache_l3_size);

    // Anonymize GPU model
    anonymize_gpu_model(original->hardware.gpu_name,
                       original->hardware.gpu_compute_capability,
                       anonymized->gpu_family,
                       sizeof(anonymized->gpu_family),
                       level);

    anonymized->gpu_sm_count = original->hardware.gpu_sm_count;
    anonymized->gpu_compute_capability = original->hardware.gpu_compute_capability;

    // Generate anonymized hardware hash
    anonymize_compute_hardware_hash(anonymized, anonymized->anonymized_hash,
                                   sizeof(anonymized->anonymized_hash));

    // Timestamp handling: only date, no time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info) {
        strftime(anonymized->submission_date, sizeof(anonymized->submission_date),
                "%Y-%m-%d", tm_info);
    } else {
        strcpy(anonymized->submission_date, "unknown");
    }

    // Relative timeline (would need persistent storage of first_run_date)
    // For now, set to 0 (TODO: implement persistent tracking)
    anonymized->days_since_first_run = 0;

    return 0;
}

// Validate anonymized result for privacy
bool anonymize_validate_privacy(const anonymized_benchmark_t *anon) {
    if (!anon) return false;

    // Check that no exact model strings are present
    if (strstr(anon->cpu_family, "Intel(R)") ||
        strstr(anon->cpu_family, "AMD Ryzen") ||
        strstr(anon->cpu_family, "@") ||
        strstr(anon->cpu_family, "GHz")) {
        return false; // Exact model string leaked
    }

    if (strstr(anon->gpu_family, "GeForce") ||
        strstr(anon->gpu_family, "RTX ") ||
        strstr(anon->gpu_family, "GTX ")) {
        return false; // Exact GPU model leaked
    }

    // Check that RAM is quantized (not exact)
    if (anon->ram_gb_bucket == 0 || anon->ram_gb_bucket > 1024) {
        return false; // Invalid bucket
    }

    // Check that hash is generated
    if (anon->anonymized_hash[0] == '\0') {
        return false; // Missing hash
    }

    // Check that timestamp is date-only (no time component)
    if (strchr(anon->submission_date, ':') != NULL) {
        return false; // Time component leaked
    }

    return true; // Privacy looks good
}

// Export anonymized result to JSON
int anonymize_export_json(const anonymized_benchmark_t *anon,
                          char *json_out, size_t json_size) {
    if (!anon || !json_out || json_size == 0) return -1;

    int ret = snprintf(json_out, json_size,
        "{\n"
        "  \"mode\": \"%s\",\n"
        "  \"bits\": %d,\n"
        "  \"key_type\": \"%s\",\n"
        "  \"cpu_speed_mkeys\": %.2f,\n"
        "  \"gpu_speed_mkeys\": %.2f,\n"
        "  \"hybrid_speed_mkeys\": %.2f,\n"
        "  \"efficiency_ratio\": %.3f,\n"
        "  \"benchmark_duration_seconds\": %d,\n"
        "  \"keyhunt_version\": \"%s\",\n"
        "  \"cpu_family\": \"%s\",\n"
        "  \"cpu_physical_cores\": %d,\n"
        "  \"cpu_logical_cores\": %d,\n"
        "  \"has_avx2\": %s,\n"
        "  \"has_avx512\": %s,\n"
        "  \"has_avx512f\": %s,\n"
        "  \"has_sha_ni\": %s,\n"
        "  \"ram_gb_bucket\": %d,\n"
        "  \"cache_l3_mb_bucket\": %d,\n"
        "  \"gpu_family\": \"%s\",\n"
        "  \"gpu_sm_count\": %d,\n"
        "  \"gpu_compute_capability\": %d,\n"
        "  \"anonymized_hash\": \"%s\",\n"
        "  \"submission_date\": \"%s\",\n"
        "  \"days_since_first_run\": %d\n"
        "}",
        anon->mode,
        anon->bits,
        anon->key_type,
        anon->cpu_speed_mkeys,
        anon->gpu_speed_mkeys,
        anon->hybrid_speed_mkeys,
        anon->efficiency_ratio,
        anon->benchmark_duration_seconds,
        anon->keyhunt_version,
        anon->cpu_family,
        anon->cpu_physical_cores,
        anon->cpu_logical_cores,
        anon->has_avx2 ? "true" : "false",
        anon->has_avx512 ? "true" : "false",
        anon->has_avx512f ? "true" : "false",
        anon->has_sha_ni ? "true" : "false",
        anon->ram_gb_bucket,
        anon->cache_l3_mb_bucket,
        anon->gpu_family,
        anon->gpu_sm_count,
        anon->gpu_compute_capability,
        anon->anonymized_hash,
        anon->submission_date,
        anon->days_since_first_run);

    if (ret < 0 || (size_t)ret >= json_size) {
        return -1; // Buffer too small
    }

    return 0;
}
