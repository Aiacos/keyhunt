#!/bin/bash
#
# test_stale_cache.sh - Test offline mode with stale cache (>24h)
#
# This script tests that the wizard properly detects and uses stale cache
# when network is unavailable or cache is older than 24 hours.
#

set -e

CACHE_DIR="$HOME/.keyhunt"
PRIVATEKEYS_CACHE="$CACHE_DIR/privatekeys_progress.json"
KEYSLOL_CACHE="$CACHE_DIR/keyslol_progress.json"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Stale Cache Test for Wizard${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Function to create stale cache file
create_stale_cache() {
    local cache_file=$1
    local puzzle_num=$2
    local hours_old=$3

    # Calculate timestamp from hours ago
    local current_time=$(date +%s)
    local old_time=$((current_time - hours_old * 3600))

    mkdir -p "$CACHE_DIR"

    cat > "$cache_file" << EOF
{
  "puzzle_number": $puzzle_num,
  "percent_scanned": 42.123456,
  "keys_scanned": 123456789,
  "fetch_time": $old_time
}
EOF

    echo -e "${GREEN}[+]${NC} Created stale cache: $cache_file"
    echo -e "    Timestamp: $old_time ($(date -d @$old_time '+%Y-%m-%d %H:%M:%S'))"
    echo -e "    Age: $hours_old hours ($((hours_old / 24)) days)"
}

# Function to backup existing cache
backup_cache() {
    local cache_file=$1
    if [ -f "$cache_file" ]; then
        cp "$cache_file" "${cache_file}.backup"
        echo -e "${YELLOW}[i]${NC} Backed up existing cache to ${cache_file}.backup"
    fi
}

# Function to restore cache
restore_cache() {
    local cache_file=$1
    if [ -f "${cache_file}.backup" ]; then
        mv "${cache_file}.backup" "$cache_file"
        echo -e "${GREEN}[+]${NC} Restored original cache from backup"
    fi
}

# Main test procedure
echo -e "${YELLOW}Test Scenario:${NC}"
echo "  1. Create stale cache files (48 hours old)"
echo "  2. Block network access (optional)"
echo "  3. Run wizard and verify stale cache warning"
echo ""

# Ask user for test mode
echo -e "${BLUE}Select test mode:${NC}"
echo "  1. Create stale cache only (manual verification)"
echo "  2. Create stale cache + simulate network failure"
echo "  3. Clean up test artifacts"
read -p "Choice [1-3]: " choice

case $choice in
    1)
        echo ""
        echo -e "${YELLOW}[*] Creating stale cache files...${NC}"
        backup_cache "$PRIVATEKEYS_CACHE"
        backup_cache "$KEYSLOL_CACHE"

        # Create 48-hour old cache for puzzle 66
        create_stale_cache "$PRIVATEKEYS_CACHE" 66 48
        create_stale_cache "$KEYSLOL_CACHE" 66 48

        echo ""
        echo -e "${GREEN}[+] Stale cache created successfully!${NC}"
        echo ""
        echo -e "${YELLOW}Manual Verification Steps:${NC}"
        echo "  1. Run: ./keyhunt --wizard"
        echo "  2. Select puzzle #66"
        echo "  3. Enable community integration"
        echo "  4. Look for stale cache warnings:"
        echo "     - '[!] Network error - using stale cache as fallback (2.0 days old)'"
        echo "     - Message should appear for both privatekeys.pw and Keys.lol"
        echo ""
        echo -e "${BLUE}Expected Behavior:${NC}"
        echo "  ✓ Wizard tries to fetch fresh data from network"
        echo "  ✓ On network failure, falls back to stale cache"
        echo "  ✓ Displays warning about cache age (days/hours)"
        echo "  ✓ Continues with cached data despite being stale"
        echo ""
        echo -e "${YELLOW}To restore original cache:${NC}"
        echo "  Run: $0 and select option 3 (Clean up)"
        ;;

    2)
        echo ""
        echo -e "${YELLOW}[*] Creating stale cache files...${NC}"
        backup_cache "$PRIVATEKEYS_CACHE"
        backup_cache "$KEYSLOL_CACHE"

        create_stale_cache "$PRIVATEKEYS_CACHE" 66 48
        create_stale_cache "$KEYSLOL_CACHE" 66 48

        echo ""
        echo -e "${YELLOW}[*] Simulating network failure...${NC}"
        echo ""
        echo -e "${RED}WARNING:${NC} This requires modifying /etc/hosts to block external APIs"
        echo "You will need sudo privileges."
        echo ""
        read -p "Continue? [y/N]: " confirm

        if [[ $confirm == [yY] ]]; then
            # Block network access to community APIs
            echo -e "${YELLOW}[*] Adding hosts entries to block API access...${NC}"
            sudo bash -c "cat >> /etc/hosts << 'EOF'
# Temporary block for keyhunt cache test
127.0.0.1 btcpuzzle.info
127.0.0.1 privatekeys.pw
127.0.0.1 keys.lol
EOF"

            echo -e "${GREEN}[+]${NC} Network access blocked"
            echo ""
            echo -e "${YELLOW}Now run:${NC} ./keyhunt --wizard"
            echo "Select puzzle #66 and enable community integration"
            echo ""
            echo -e "${RED}IMPORTANT:${NC} After testing, run option 3 to clean up!"
        else
            echo -e "${YELLOW}[i]${NC} Network simulation skipped"
        fi
        ;;

    3)
        echo ""
        echo -e "${YELLOW}[*] Cleaning up test artifacts...${NC}"

        # Restore original cache files
        restore_cache "$PRIVATEKEYS_CACHE"
        restore_cache "$KEYSLOL_CACHE"

        # Remove hosts entries if they exist
        if grep -q "# Temporary block for keyhunt cache test" /etc/hosts 2>/dev/null; then
            echo -e "${YELLOW}[*] Removing hosts entries...${NC}"
            sudo sed -i '/# Temporary block for keyhunt cache test/,+3d' /etc/hosts
            echo -e "${GREEN}[+]${NC} Hosts entries removed"
        fi

        echo -e "${GREEN}[+] Cleanup complete!${NC}"
        ;;

    *)
        echo -e "${RED}[-] Invalid choice${NC}"
        exit 1
        ;;
esac

echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Test Complete${NC}"
echo -e "${BLUE}========================================${NC}"
