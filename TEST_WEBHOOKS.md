# Webhook Notification Testing Guide

This guide explains how to test the Discord and Telegram webhook notification system for keyhunt.

## Overview

The webhook notification system allows you to receive real-time notifications when:
1. **A private key is found** (🎉 Key Found alert)
2. **Progress milestones are reached** (📊 Progress Update)

Webhooks are supported for:
- **Discord** (via Discord webhooks)
- **Telegram** (via Telegram Bot API)

---

## Quick Start

### Option 1: Interactive Test Script (Easiest)

```bash
./test_webhooks.sh
```

The script will guide you through:
1. Setting up Discord webhook URL (optional)
2. Setting up Telegram bot credentials (optional)
3. Choosing which notification type to test
4. Running the test and verifying results

### Option 2: Command-Line Test

```bash
# Test Discord webhook
./test_webhooks.sh --discord "https://discord.com/api/webhooks/YOUR_WEBHOOK_ID/YOUR_TOKEN"

# Test Telegram webhook
./test_webhooks.sh --telegram "YOUR_BOT_TOKEN:YOUR_CHAT_ID"

# Test both notification types
./test_webhooks.sh --discord "URL" --telegram "TOKEN:CHAT" --both
```

---

## Setting Up Discord Webhooks

### Step 1: Create a Discord Webhook

1. Open Discord and go to your server
2. Click on **Server Settings** (gear icon)
3. Navigate to **Integrations** → **Webhooks**
4. Click **New Webhook** or **Create Webhook**
5. Configure the webhook:
   - **Name**: keyhunt notifications (or any name you prefer)
   - **Channel**: Select the channel where notifications should appear
6. Click **Copy Webhook URL**
   - URL format: `https://discord.com/api/webhooks/WEBHOOK_ID/TOKEN`

### Step 2: Test the Webhook

```bash
./test_webhooks.sh --discord "https://discord.com/api/webhooks/YOUR_WEBHOOK_ID/YOUR_TOKEN"
```

### Step 3: Verify

Check your Discord channel. You should see a test message like:

```
🎉 KEY FOUND! 🎉
Puzzle: #66
Address: 1BoatSLRHtKNngkdXEeobR76b53LETtpyT
Private Key: 0x1234567890abcdef1234567890abcdef12345678
```

---

## Setting Up Telegram Webhooks

### Step 1: Create a Telegram Bot

1. Open Telegram and search for **@BotFather**
2. Send `/newbot` command
3. Follow the prompts:
   - Choose a name for your bot (e.g., "My Keyhunt Bot")
   - Choose a username (must end in "bot", e.g., "mykeyhuntbot")
4. BotFather will provide a **bot token**:
   - Format: `123456789:ABCdefGHIjklMNOpqrsTUVwxyz`
   - **Keep this token secret!**

### Step 2: Get Your Chat ID

**Method 1: Using @userinfobot**
1. Search for **@userinfobot** on Telegram
2. Send any message to the bot
3. It will reply with your chat ID (a number like `987654321`)

**Method 2: Using the Bot API**
1. Start a chat with your bot (search for the username you created)
2. Send any message to your bot (e.g., "Hello")
3. Visit: `https://api.telegram.org/bot<YOUR_BOT_TOKEN>/getUpdates`
4. Look for `"chat":{"id":123456789}` in the JSON response

### Step 3: Test the Webhook

```bash
./test_webhooks.sh --telegram "123456789:ABCdefGHIjklMNOpqrsTUVwxyz:987654321"
```

Format: `BOT_TOKEN:CHAT_ID`

### Step 4: Verify

Check your Telegram chat with the bot. You should see a test message like:

```
🎉 KEY FOUND! 🎉
Puzzle: #66
Address: 1BoatSLRHtKNngkdXEeobR76b53LETtpyT
Private Key: 0x1234567890abcdef1234567890abcdef12345678
```

---

## Manual Testing Procedure

If you prefer to compile and run the test manually:

### Compile the Test Program

```bash
gcc -o tests/test_webhooks \
    tests/test_webhooks.c \
    src/wizard/wizard_webhooks.c \
    src/wizard/wizard_http.c \
    -I./src \
    -Wall -O2
```

### Run Tests

**Test Key Found Notification:**
```bash
./tests/test_webhooks --discord "DISCORD_URL"
```

**Test Progress Notification:**
```bash
./tests/test_webhooks --discord "DISCORD_URL" --progress
```

**Test Both Types:**
```bash
./tests/test_webhooks --discord "DISCORD_URL" --both
```

**Test Multiple Webhooks:**
```bash
./tests/test_webhooks \
    --discord "DISCORD_URL" \
    --telegram "BOT_TOKEN:CHAT_ID" \
    --both
```

---

## Expected Test Output

### Successful Test

```
========================================
Webhook Notification Test
========================================

[+] Discord webhook configured
    URL: https://discord.com/api/webhooks/123/abc

------------------------------------------
Testing KEY FOUND notification...
------------------------------------------
Test data:
  Private Key: 0x1234567890abcdef1234567890abcdef12345678
  Address: 1BoatSLRHtKNngkdXEeobR76b53LETtpyT
  Puzzle: #66

[+] Notification sent to Discord

[✓] Key found notification: 1/1 webhooks succeeded

========================================
Test Summary
========================================
Tests run: 1
Tests passed: 1
Tests failed: 0

[✓] All tests PASSED

Check your Discord channel or Telegram chat to verify
that the notification(s) were received correctly.
```

### Failed Test

If the test fails, you'll see error messages like:

```
[-] wizard_webhook_discord: HTTP POST failed
[!] Failed to send Discord notification

[✗] Key found notification: FAILED
```

**Common failure reasons:**
1. **Invalid webhook URL** - Double-check the URL format
2. **Network connectivity issues** - Check your internet connection
3. **Rate limiting** - Discord/Telegram may rate-limit rapid requests
4. **Deleted webhook** - Recreate the webhook if it was deleted
5. **Bot permissions** - Ensure Telegram bot can send messages to the chat

---

## Integration with Keyhunt Wizard

Once you've verified webhooks work with the test script, you can configure them in the wizard:

```bash
./keyhunt --wizard
```

During the wizard configuration:
1. **Step 4: Community Integration**
   - Enable community synchronization
   - Configure webhook URLs when prompted

2. **Discord Webhook:**
   - Paste your Discord webhook URL
   - Format: `https://discord.com/api/webhooks/ID/TOKEN`

3. **Telegram Webhook:**
   - Enter bot token and chat ID
   - Format: `TOKEN:CHAT_ID`

The wizard will save these settings to `keyhunt_wizard.json` and use them during searches.

---

## Notification Examples

### Key Found Notification

**Discord/Telegram Message:**
```
🎉 KEY FOUND! 🎉
Puzzle: #66
Address: 1BoatSLRHtKNngkdXEeobR76b53LETtpyT
Private Key: 0x20d45a6a762535700ce9e0b216e31994335db8a5
```

### Progress Notification

**Discord/Telegram Message:**
```
📊 Progress Update
Puzzle: #66
Progress: 25.50%
Keys Checked: 1000000000
```

---

## Troubleshooting

### Discord Issues

**Problem:** "HTTP POST failed"
- **Solution:** Verify webhook URL is correct and not deleted
- **Check:** Server Settings → Integrations → Webhooks

**Problem:** Rate limiting
- **Solution:** Discord allows 30 messages per minute per webhook
- **Recommendation:** Send progress updates every 5-10%

### Telegram Issues

**Problem:** "Unauthorized" error
- **Solution:** Bot token is incorrect or expired
- **Fix:** Create new bot with @BotFather

**Problem:** "Chat not found"
- **Solution:** Chat ID is incorrect or bot was blocked
- **Fix:** Ensure bot can access the chat:
  1. Start a conversation with your bot
  2. Send a message first
  3. Use @userinfobot to verify chat ID

**Problem:** Bot doesn't respond
- **Solution:** Bot needs to be started by the user
- **Fix:** Search for your bot and send `/start`

### Network Issues

**Problem:** Connection timeout
- **Solution:** Check internet connectivity
- **Test:** `curl https://discord.com` or `curl https://api.telegram.org`

**Problem:** Proxy/firewall blocking
- **Solution:** Configure proxy or whitelist these domains:
  - `discord.com`
  - `api.telegram.org`

---

## Security Notes

### Discord Webhooks
- ⚠️ Webhook URLs contain sensitive tokens
- Don't share webhook URLs publicly
- Anyone with the URL can send messages to your channel
- Regenerate webhook if accidentally exposed

### Telegram Bots
- ⚠️ Bot tokens are like passwords
- Never commit tokens to git repositories
- Keep tokens in environment variables or config files
- Revoke and regenerate tokens if exposed

### Privacy
- Webhook notifications contain private keys (when found)
- Use private Discord channels or Telegram chats
- Don't send notifications to public channels

---

## Advanced Usage

### Testing from Code

If you want to integrate webhook testing into your own code:

```c
#include "src/wizard/wizard_webhooks.h"

/* Test Discord */
int ret = wizard_webhook_discord(
    "https://discord.com/api/webhooks/ID/TOKEN",
    "Test message from keyhunt"
);

/* Test Telegram */
int ret = wizard_webhook_telegram(
    "123456789:ABCdefGHIjklMNOpqrsTUVwxyz",  /* bot_token */
    "987654321",                                /* chat_id */
    "Test message from keyhunt"
);

/* High-level notification */
int count = wizard_webhook_notify_found(
    discord_url,
    telegram_token,
    telegram_chat_id,
    "0x20d45a6a762535700ce9e0b216e31994335db8a5",
    "1BoatSLRHtKNngkdXEeobR76b53LETtpyT",
    66  /* puzzle number */
);
```

### Automated Testing

Add webhook testing to CI/CD:

```bash
# Export test webhooks (in secure CI variables)
export TEST_DISCORD_URL="https://discord.com/api/webhooks/TEST_ID/TEST_TOKEN"
export TEST_TELEGRAM="TEST_TOKEN:TEST_CHAT_ID"

# Run automated test
./test_webhooks.sh --discord "$TEST_DISCORD_URL" --telegram "$TEST_TELEGRAM" --both
```

---

## Summary

✅ **Created:** Test program (`tests/test_webhooks.c`)
✅ **Created:** Interactive script (`test_webhooks.sh`)
✅ **Created:** Documentation (`TEST_WEBHOOKS.md`)

**To test webhooks:**
1. Get Discord webhook URL from Discord settings
2. Get Telegram bot token from @BotFather and chat ID
3. Run `./test_webhooks.sh` and follow prompts
4. Verify notifications appear in Discord/Telegram
5. Configure in keyhunt wizard: `./keyhunt --wizard`

**Questions?** Check the Troubleshooting section or inspect `src/wizard/wizard_webhooks.c` for implementation details.
