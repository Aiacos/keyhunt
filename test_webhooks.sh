#!/bin/bash
#
# test_webhooks.sh - Easy webhook testing script
#
# This script helps you test Discord and Telegram webhook notifications
# without needing to run the full keyhunt wizard.
#
# Usage:
#   ./test_webhooks.sh               # Interactive mode
#   ./test_webhooks.sh --discord URL # Test Discord only
#   ./test_webhooks.sh --telegram TOKEN:CHAT # Test Telegram only
#

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored message
print_info() {
    echo -e "${BLUE}[i]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

# Check if test program exists
TEST_PROGRAM="./tests/test_webhooks"

if [ ! -f "$TEST_PROGRAM" ]; then
    print_info "Test program not found. Building..."

    # Compile test program
    gcc -o "$TEST_PROGRAM" \
        tests/test_webhooks.c \
        src/wizard/wizard_webhooks.c \
        src/wizard/wizard_http.c \
        -I./src \
        -Wall -O2

    if [ $? -ne 0 ]; then
        print_error "Failed to compile test program"
        exit 1
    fi

    print_success "Test program built successfully"
fi

# Interactive mode if no arguments
if [ $# -eq 0 ]; then
    echo "========================================"
    echo "Webhook Test - Interactive Mode"
    echo "========================================"
    echo ""

    # Ask for Discord webhook
    echo -e "${BLUE}Discord Webhook (optional)${NC}"
    echo "Get webhook URL from: https://discord.com/developers/applications"
    echo "Server Settings → Integrations → Webhooks → New Webhook"
    echo ""
    read -p "Enter Discord webhook URL (or press Enter to skip): " DISCORD_URL

    # Ask for Telegram webhook
    echo ""
    echo -e "${BLUE}Telegram Bot (optional)${NC}"
    echo "Get bot token from: @BotFather on Telegram"
    echo "Get chat ID by messaging @userinfobot"
    echo "Format: botTOKEN:chatID"
    echo ""
    read -p "Enter Telegram bot:chat (or press Enter to skip): " TELEGRAM_FULL

    # Validate at least one is configured
    if [ -z "$DISCORD_URL" ] && [ -z "$TELEGRAM_FULL" ]; then
        print_error "No webhooks configured. Please provide at least one webhook."
        exit 1
    fi

    # Ask what to test
    echo ""
    echo -e "${BLUE}What would you like to test?${NC}"
    echo "1) Key found notification (default)"
    echo "2) Progress notification"
    echo "3) Both"
    echo ""
    read -p "Enter choice [1-3]: " TEST_CHOICE

    # Build command
    CMD="$TEST_PROGRAM"
    [ -n "$DISCORD_URL" ] && CMD="$CMD --discord \"$DISCORD_URL\""
    [ -n "$TELEGRAM_FULL" ] && CMD="$CMD --telegram \"$TELEGRAM_FULL\""

    case "$TEST_CHOICE" in
        2)
            CMD="$CMD --progress"
            ;;
        3)
            CMD="$CMD --both"
            ;;
        *)
            # Default: key found (no flag needed)
            ;;
    esac

    echo ""
    print_info "Running webhook test..."
    echo ""

    # Run the test
    eval $CMD
    EXIT_CODE=$?

    echo ""
    if [ $EXIT_CODE -eq 0 ]; then
        print_success "Webhook test completed successfully!"
        echo ""
        print_warning "Please check your Discord channel or Telegram chat"
        print_warning "to verify that you received the test notification(s)."
    else
        print_error "Webhook test failed. Check the error messages above."
    fi

    exit $EXIT_CODE
else
    # Command-line mode: pass arguments directly to test program
    exec "$TEST_PROGRAM" "$@"
fi
