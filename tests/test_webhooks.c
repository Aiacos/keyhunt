/*
 * test_webhooks.c - Test webhook notification system
 *
 * This test verifies that webhook notifications work correctly for:
 * 1. Discord webhooks (key found + progress)
 * 2. Telegram webhooks (key found + progress)
 *
 * Usage: ./test_webhooks [options]
 * Options:
 *   --discord URL         Test Discord webhook
 *   --telegram TOKEN:CHAT Test Telegram webhook (format: botTOKEN:chatID)
 *   --both                Test both notification types
 *   --progress            Test progress notification (default: key found)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/wizard/wizard_webhooks.h"

/* Test data */
#define TEST_PRIVATE_KEY "0x1234567890abcdef1234567890abcdef12345678"
#define TEST_ADDRESS "1BoatSLRHtKNngkdXEeobR76b53LETtpyT"
#define TEST_PUZZLE_NUMBER 66
#define TEST_PERCENT_COMPLETE 25.5
#define TEST_KEYS_CHECKED 1000000000ULL

void print_usage(const char *progname) {
    printf("Usage: %s [options]\n\n", progname);
    printf("Options:\n");
    printf("  --discord URL         Test Discord webhook\n");
    printf("  --telegram TOKEN:CHAT Test Telegram webhook (format: botTOKEN:chatID)\n");
    printf("  --both                Test both notification types\n");
    printf("  --progress            Test progress notification (default: key found)\n");
    printf("\nExample:\n");
    printf("  %s --discord https://discord.com/api/webhooks/123/abc\n", progname);
    printf("  %s --telegram 123456:ABCDEF:987654321\n", progname);
    printf("  %s --discord URL --telegram TOKEN:CHAT --both\n", progname);
}

int main(int argc, char *argv[]) {
    const char *discord_url = NULL;
    const char *telegram_full = NULL;  /* Format: token:chat_id */
    char *telegram_token = NULL;
    char *telegram_chat_id = NULL;
    int test_progress = 0;  /* 0 = key found, 1 = progress */
    int test_both = 0;      /* Test both types */

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--discord") == 0 && i + 1 < argc) {
            discord_url = argv[++i];
        } else if (strcmp(argv[i], "--telegram") == 0 && i + 1 < argc) {
            telegram_full = argv[++i];
        } else if (strcmp(argv[i], "--progress") == 0) {
            test_progress = 1;
        } else if (strcmp(argv[i], "--both") == 0) {
            test_both = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "[-] Unknown option: %s\n\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Parse Telegram token:chat_id format */
    if (telegram_full) {
        char *colon = strchr(telegram_full, ':');
        if (!colon) {
            fprintf(stderr, "[-] Invalid Telegram format. Expected: botTOKEN:chatID\n");
            return 1;
        }

        /* Allocate and copy token and chat_id */
        size_t token_len = colon - telegram_full;
        telegram_token = malloc(token_len + 1);
        if (!telegram_token) {
            fprintf(stderr, "[-] Memory allocation failed\n");
            return 1;
        }
        strncpy(telegram_token, telegram_full, token_len);
        telegram_token[token_len] = '\0';

        telegram_chat_id = strdup(colon + 1);
        if (!telegram_chat_id) {
            fprintf(stderr, "[-] Memory allocation failed\n");
            free(telegram_token);
            return 1;
        }
    }

    /* Validate at least one webhook is configured */
    if (!discord_url && !telegram_full) {
        fprintf(stderr, "[-] No webhook configured. Use --discord or --telegram\n\n");
        print_usage(argv[0]);
        return 1;
    }

    printf("========================================\n");
    printf("Webhook Notification Test\n");
    printf("========================================\n\n");

    if (discord_url) {
        printf("[+] Discord webhook configured\n");
        printf("    URL: %s\n", discord_url);
    }
    if (telegram_full) {
        printf("[+] Telegram webhook configured\n");
        printf("    Token: %s\n", telegram_token);
        printf("    Chat ID: %s\n", telegram_chat_id);
    }
    printf("\n");

    int success_count = 0;
    int total_tests = 0;

    /* Test key found notification */
    if (!test_progress || test_both) {
        printf("------------------------------------------\n");
        printf("Testing KEY FOUND notification...\n");
        printf("------------------------------------------\n");
        printf("Test data:\n");
        printf("  Private Key: %s\n", TEST_PRIVATE_KEY);
        printf("  Address: %s\n", TEST_ADDRESS);
        printf("  Puzzle: #%d\n\n", TEST_PUZZLE_NUMBER);

        int result = wizard_webhook_notify_found(
            discord_url,
            telegram_token,
            telegram_chat_id,
            TEST_PRIVATE_KEY,
            TEST_ADDRESS,
            TEST_PUZZLE_NUMBER
        );

        total_tests++;
        if (result > 0) {
            printf("\n[✓] Key found notification: %d/%d webhooks succeeded\n\n",
                   result, (discord_url ? 1 : 0) + (telegram_full ? 1 : 0));
            success_count++;
        } else {
            printf("\n[✗] Key found notification: FAILED\n\n");
        }
    }

    /* Test progress notification */
    if (test_progress || test_both) {
        printf("------------------------------------------\n");
        printf("Testing PROGRESS notification...\n");
        printf("------------------------------------------\n");
        printf("Test data:\n");
        printf("  Percent Complete: %.2f%%\n", TEST_PERCENT_COMPLETE);
        printf("  Keys Checked: %llu\n", TEST_KEYS_CHECKED);
        printf("  Puzzle: #%d\n\n", TEST_PUZZLE_NUMBER);

        int result = wizard_webhook_notify_progress(
            discord_url,
            telegram_token,
            telegram_chat_id,
            TEST_PERCENT_COMPLETE,
            TEST_KEYS_CHECKED,
            TEST_PUZZLE_NUMBER
        );

        total_tests++;
        if (result > 0) {
            printf("\n[✓] Progress notification: %d/%d webhooks succeeded\n\n",
                   result, (discord_url ? 1 : 0) + (telegram_full ? 1 : 0));
            success_count++;
        } else {
            printf("\n[✗] Progress notification: FAILED\n\n");
        }
    }

    /* Cleanup */
    if (telegram_token) free(telegram_token);
    if (telegram_chat_id) free(telegram_chat_id);

    /* Print summary */
    printf("========================================\n");
    printf("Test Summary\n");
    printf("========================================\n");
    printf("Tests run: %d\n", total_tests);
    printf("Tests passed: %d\n", success_count);
    printf("Tests failed: %d\n", total_tests - success_count);
    printf("\n");

    if (success_count == total_tests) {
        printf("[✓] All tests PASSED\n");
        printf("\nCheck your Discord channel or Telegram chat to verify\n");
        printf("that the notification(s) were received correctly.\n");
        return 0;
    } else {
        printf("[✗] Some tests FAILED\n");
        printf("\nCheck the error messages above and verify:\n");
        printf("  1. Webhook URLs are correct\n");
        printf("  2. Network connectivity is working\n");
        printf("  3. Discord/Telegram bot permissions are set\n");
        return 1;
    }
}
