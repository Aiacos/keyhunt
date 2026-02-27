/*
 * wizard_webhooks.h - Webhook notification module for Discord/Telegram
 *
 * Provides functions to send notifications to Discord webhooks and
 * Telegram bots for real-time progress updates and key found alerts.
 */

#ifndef WIZARD_WEBHOOKS_H
#define WIZARD_WEBHOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Send notification to Discord webhook
 *
 * Sends a message to a Discord webhook URL in the format:
 * {"content": "message"}
 *
 * @param webhook_url  Discord webhook URL (https://discord.com/api/webhooks/...)
 * @param message      Message content to send
 * @return 0 on success, -1 on error
 */
int wizard_webhook_discord(const char *webhook_url, const char *message);

/**
 * Send notification to Telegram bot
 *
 * Sends a message to a Telegram bot using the Bot API in the format:
 * {"chat_id": "...", "text": "message"}
 *
 * @param bot_token    Telegram bot token (from @BotFather)
 * @param chat_id      Telegram chat ID (user or channel)
 * @param message      Message content to send
 * @return 0 on success, -1 on error
 */
int wizard_webhook_telegram(const char *bot_token, const char *chat_id, const char *message);

/**
 * Send "key found" notification to all configured webhooks
 *
 * Formats and sends a notification when a private key is found.
 *
 * @param discord_url      Discord webhook URL (NULL to skip)
 * @param telegram_token   Telegram bot token (NULL to skip)
 * @param telegram_chat_id Telegram chat ID (NULL to skip)
 * @param private_key      Found private key (hex string)
 * @param address          Bitcoin address
 * @param puzzle_number    Puzzle number (0 if not a puzzle)
 * @return Number of successful notifications (0-2)
 */
int wizard_webhook_notify_found(const char *discord_url,
                                 const char *telegram_token,
                                 const char *telegram_chat_id,
                                 const char *private_key,
                                 const char *address,
                                 int puzzle_number);

/**
 * Send progress milestone notification to all configured webhooks
 *
 * Formats and sends a progress update notification.
 *
 * @param discord_url      Discord webhook URL (NULL to skip)
 * @param telegram_token   Telegram bot token (NULL to skip)
 * @param telegram_chat_id Telegram chat ID (NULL to skip)
 * @param percent_complete Progress percentage (0-100)
 * @param keys_checked     Total keys checked so far
 * @param puzzle_number    Puzzle number (0 if not a puzzle)
 * @return Number of successful notifications (0-2)
 */
int wizard_webhook_notify_progress(const char *discord_url,
                                    const char *telegram_token,
                                    const char *telegram_chat_id,
                                    double percent_complete,
                                    unsigned long long keys_checked,
                                    int puzzle_number);

#ifdef __cplusplus
}
#endif

#endif /* WIZARD_WEBHOOKS_H */
