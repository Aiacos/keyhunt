/*
 * wizard_webhooks.c - Webhook notification module implementation
 */

#include "wizard_webhooks.h"
#include "wizard_http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Discord Webhook
 * ============================================================================ */

int wizard_webhook_discord(const char *webhook_url, const char *message) {
    if (!webhook_url || !message) {
        fprintf(stderr, "[-] wizard_webhook_discord: Invalid parameters\n");
        return -1;
    }

    /* Escape message for JSON */
    char *escaped = wizard_http_json_escape(message);
    if (!escaped) {
        fprintf(stderr, "[-] wizard_webhook_discord: Failed to escape message\n");
        return -1;
    }

    /* Build JSON payload: {"content": "message"} */
    char json[8192];
    int len = snprintf(json, sizeof(json), "{\"content\": \"%s\"}", escaped);
    free(escaped);

    if (len < 0 || len >= (int)sizeof(json)) {
        fprintf(stderr, "[-] wizard_webhook_discord: Message too long\n");
        return -1;
    }

    /* Send POST request */
    char *response = NULL;
    size_t response_len = 0;
    int ret = wizard_http_post_json(webhook_url, json, &response, &response_len);

    /* Clean up response */
    if (response) {
        free(response);
    }

    if (ret != 0) {
        fprintf(stderr, "[-] wizard_webhook_discord: HTTP POST failed\n");
        return -1;
    }

    return 0;
}

/* ============================================================================
 * Telegram Webhook
 * ============================================================================ */

int wizard_webhook_telegram(const char *bot_token, const char *chat_id, const char *message) {
    if (!bot_token || !chat_id || !message) {
        fprintf(stderr, "[-] wizard_webhook_telegram: Invalid parameters\n");
        return -1;
    }

    /* Build Telegram API URL: https://api.telegram.org/bot<token>/sendMessage */
    char url[512];
    int url_len = snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", bot_token);
    if (url_len < 0 || url_len >= (int)sizeof(url)) {
        fprintf(stderr, "[-] wizard_webhook_telegram: Bot token too long\n");
        return -1;
    }

    /* Escape message and chat_id for JSON */
    char *escaped_msg = wizard_http_json_escape(message);
    if (!escaped_msg) {
        fprintf(stderr, "[-] wizard_webhook_telegram: Failed to escape message\n");
        return -1;
    }

    char *escaped_chat = wizard_http_json_escape(chat_id);
    if (!escaped_chat) {
        free(escaped_msg);
        fprintf(stderr, "[-] wizard_webhook_telegram: Failed to escape chat_id\n");
        return -1;
    }

    /* Build JSON payload: {"chat_id": "...", "text": "message"} */
    char json[8192];
    int len = snprintf(json, sizeof(json), "{\"chat_id\": \"%s\", \"text\": \"%s\"}",
                       escaped_chat, escaped_msg);
    free(escaped_msg);
    free(escaped_chat);

    if (len < 0 || len >= (int)sizeof(json)) {
        fprintf(stderr, "[-] wizard_webhook_telegram: Message too long\n");
        return -1;
    }

    /* Send POST request */
    char *response = NULL;
    size_t response_len = 0;
    int ret = wizard_http_post_json(url, json, &response, &response_len);

    /* Clean up response */
    if (response) {
        free(response);
    }

    if (ret != 0) {
        fprintf(stderr, "[-] wizard_webhook_telegram: HTTP POST failed\n");
        return -1;
    }

    return 0;
}

/* ============================================================================
 * High-Level Notification Functions
 * ============================================================================ */

int wizard_webhook_notify_found(const char *discord_url,
                                 const char *telegram_token,
                                 const char *telegram_chat_id,
                                 const char *private_key,
                                 const char *address,
                                 int puzzle_number) {
    if (!private_key || !address) {
        fprintf(stderr, "[-] wizard_webhook_notify_found: Invalid parameters\n");
        return 0;
    }

    /* Build notification message */
    char message[1024];
    if (puzzle_number > 0) {
        snprintf(message, sizeof(message),
                 "🎉 KEY FOUND! 🎉\n"
                 "Puzzle: #%d\n"
                 "Address: %s\n"
                 "Private Key: %s",
                 puzzle_number, address, private_key);
    } else {
        snprintf(message, sizeof(message),
                 "🎉 KEY FOUND! 🎉\n"
                 "Address: %s\n"
                 "Private Key: %s",
                 address, private_key);
    }

    int success_count = 0;

    /* Send to Discord if configured */
    if (discord_url && discord_url[0] != '\0') {
        if (wizard_webhook_discord(discord_url, message) == 0) {
            printf("[+] Notification sent to Discord\n");
            success_count++;
        } else {
            fprintf(stderr, "[!] Failed to send Discord notification\n");
        }
    }

    /* Send to Telegram if configured */
    if (telegram_token && telegram_token[0] != '\0' &&
        telegram_chat_id && telegram_chat_id[0] != '\0') {
        if (wizard_webhook_telegram(telegram_token, telegram_chat_id, message) == 0) {
            printf("[+] Notification sent to Telegram\n");
            success_count++;
        } else {
            fprintf(stderr, "[!] Failed to send Telegram notification\n");
        }
    }

    return success_count;
}

int wizard_webhook_notify_progress(const char *discord_url,
                                    const char *telegram_token,
                                    const char *telegram_chat_id,
                                    double percent_complete,
                                    unsigned long long keys_checked,
                                    int puzzle_number) {
    /* Build progress message */
    char message[1024];
    if (puzzle_number > 0) {
        snprintf(message, sizeof(message),
                 "📊 Progress Update\n"
                 "Puzzle: #%d\n"
                 "Progress: %.2f%%\n"
                 "Keys Checked: %llu",
                 puzzle_number, percent_complete, keys_checked);
    } else {
        snprintf(message, sizeof(message),
                 "📊 Progress Update\n"
                 "Progress: %.2f%%\n"
                 "Keys Checked: %llu",
                 percent_complete, keys_checked);
    }

    int success_count = 0;

    /* Send to Discord if configured */
    if (discord_url && discord_url[0] != '\0') {
        if (wizard_webhook_discord(discord_url, message) == 0) {
            success_count++;
        } else {
            fprintf(stderr, "[!] Failed to send Discord progress notification\n");
        }
    }

    /* Send to Telegram if configured */
    if (telegram_token && telegram_token[0] != '\0' &&
        telegram_chat_id && telegram_chat_id[0] != '\0') {
        if (wizard_webhook_telegram(telegram_token, telegram_chat_id, message) == 0) {
            success_count++;
        } else {
            fprintf(stderr, "[!] Failed to send Telegram progress notification\n");
        }
    }

    return success_count;
}
