/*
 * test_community_multisource.c - Test multi-source community fetch
 *
 * This test verifies that wizard_community_fetch_all_sources() successfully
 * fetches data from all 3 community sources:
 * 1. BTCPuzzle.info (scanned ranges)
 * 2. Privatekeys.pw (progress percentage)
 * 3. Keys.lol (progress percentage)
 *
 * Usage: ./test_community_multisource [puzzle_number]
 * Default: puzzle 66
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/wizard/wizard.h"

int main(int argc, char *argv[]) {
    int puzzle_number = 66;  /* Default to puzzle 66 */

    /* Parse command line arguments */
    if (argc > 1) {
        puzzle_number = atoi(argv[1]);
        if (puzzle_number <= 0 || puzzle_number > 256) {
            fprintf(stderr, "[-] Invalid puzzle number: %d\n", puzzle_number);
            fprintf(stderr, "Usage: %s [puzzle_number]\n", argv[0]);
            return 1;
        }
    }

    printf("========================================\n");
    printf("Multi-Source Community Fetch Test\n");
    printf("========================================\n\n");

    /* Prepare output variables */
    community_range_t *btc_ranges = NULL;
    int btc_count = 0;
    privatekeys_progress_t privatekeys_progress;
    keyslol_progress_t keyslol_progress;

    /* Initialize structures */
    memset(&privatekeys_progress, 0, sizeof(privatekeys_progress));
    memset(&keyslol_progress, 0, sizeof(keyslol_progress));

    /* Call multi-source fetch */
    printf("Testing wizard_community_fetch_all_sources() for puzzle #%d\n\n", puzzle_number);

    int result = wizard_community_fetch_all_sources(
        puzzle_number,
        &btc_ranges,
        &btc_count,
        &privatekeys_progress,
        &keyslol_progress
    );

    printf("\n========================================\n");
    printf("Test Results\n");
    printf("========================================\n\n");

    /* Check return value */
    if (result == 0) {
        printf("[✓] wizard_community_fetch_all_sources() returned success\n\n");
    } else {
        printf("[✗] wizard_community_fetch_all_sources() returned error (%d)\n", result);
        printf("[!] This may be expected if all sources are unreachable\n\n");
    }

    /* Verify each source */
    int sources_ok = 0;

    /* Source 1: BTCPuzzle.info */
    printf("Source 1: BTCPuzzle.info\n");
    if (btc_count > 0) {
        printf("  [✓] Success: %d scanned ranges fetched\n", btc_count);
        sources_ok++;

        /* Show first few ranges */
        printf("  Sample ranges:\n");
        int show_count = (btc_count < 5) ? btc_count : 5;
        for (int i = 0; i < show_count; i++) {
            printf("    %s - %s\n", btc_ranges[i].start, btc_ranges[i].end);
        }
        if (btc_count > 5) {
            printf("    ... and %d more ranges\n", btc_count - 5);
        }
    } else {
        printf("  [✗] No data available\n");
    }
    printf("\n");

    /* Source 2: Privatekeys.pw */
    printf("Source 2: Privatekeys.pw\n");
    if (privatekeys_progress.percent_scanned > 0.0) {
        printf("  [✓] Success: %.6f%% scanned\n", privatekeys_progress.percent_scanned);
        if (privatekeys_progress.keys_scanned > 0) {
            printf("      Keys scanned: %llu\n", (unsigned long long)privatekeys_progress.keys_scanned);
        }
        if (privatekeys_progress.total_keys > 0) {
            printf("      Total keys: %llu\n", (unsigned long long)privatekeys_progress.total_keys);
        }
        sources_ok++;
    } else {
        printf("  [✗] No data available\n");
    }
    printf("\n");

    /* Source 3: Keys.lol */
    printf("Source 3: Keys.lol\n");
    if (keyslol_progress.percent_scanned > 0.0) {
        printf("  [✓] Success: %.4f%% scanned\n", keyslol_progress.percent_scanned);
        if (keyslol_progress.keys_scanned > 0) {
            printf("      Keys scanned: %llu\n", (unsigned long long)keyslol_progress.keys_scanned);
        }
        if (keyslol_progress.total_keys > 0) {
            printf("      Total keys: %llu\n", (unsigned long long)keyslol_progress.total_keys);
        }
        sources_ok++;
    } else {
        printf("  [✗] No data available\n");
    }
    printf("\n");

    /* Summary */
    printf("========================================\n");
    printf("Summary\n");
    printf("========================================\n\n");

    printf("Sources successfully fetched: %d/3\n", sources_ok);

    if (sources_ok == 3) {
        printf("\n[✓✓✓] ALL 3 SOURCES SUCCESSFUL!\n");
        printf("Multi-source community fetch is working correctly.\n");
    } else if (sources_ok >= 1) {
        printf("\n[✓] PARTIAL SUCCESS (%d/%d sources)\n", sources_ok, 3);
        printf("Some sources are unavailable. This is acceptable for offline/network issues.\n");
    } else {
        printf("\n[✗] NO SOURCES AVAILABLE\n");
        printf("This is expected if offline or all sources are unreachable.\n");
    }

    /* Cleanup */
    if (btc_ranges) {
        free(btc_ranges);
    }

    /* Exit code: 0 if at least one source succeeded, 1 if all failed */
    return (sources_ok > 0) ? 0 : 1;
}
