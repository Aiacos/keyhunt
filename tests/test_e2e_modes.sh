#!/bin/bash
# E2E Mode Tests for keyhunt
# Tests ADDRESS, BSGS, and XPOINT modes against known answers
#
# Each test runs ./keyhunt as a subprocess with known inputs and verifies
# that the correct private key is discovered and written to KEYFOUNDKEYFOUND.txt.

set -euo pipefail

KEYHUNT="./keyhunt"
PASS=0
FAIL=0
SKIP=0

# Colors (disabled if not a terminal)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    NC=''
fi

run_test() {
    local name="$1"
    local result="$2"
    if [ "$result" -eq 0 ]; then
        echo -e "  ${GREEN}PASS${NC}: $name"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}FAIL${NC}: $name"
        FAIL=$((FAIL + 1))
    fi
}

skip_test() {
    local name="$1"
    local reason="$2"
    echo -e "  ${YELLOW}SKIP${NC}: $name ($reason)"
    SKIP=$((SKIP + 1))
}

# ============================================================================
# Test 1: ADDRESS mode
# Search range 1:20 (hex) with tests/1to32.txt targets, both compressed+uncompressed
# Private key 1 should be found -> address 1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH
# ============================================================================
test_address_mode() {
    echo ">>> ADDRESS mode"
    rm -f KEYFOUNDKEYFOUND.txt

    timeout 60 $KEYHUNT -m address -f tests/1to32.txt -r 1:20 -t 1 -l both -q 2>/dev/null || true

    if [ -f KEYFOUNDKEYFOUND.txt ]; then
        # Check for the known address of private key 1 (compressed)
        if grep -q "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH" KEYFOUNDKEYFOUND.txt; then
            run_test "ADDRESS: finds key 1 -> 1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH" 0
        else
            echo "    KEYFOUNDKEYFOUND.txt contents:"
            head -20 KEYFOUNDKEYFOUND.txt
            run_test "ADDRESS: finds key 1 -> 1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH" 1
        fi
    else
        echo "    KEYFOUNDKEYFOUND.txt not created"
        run_test "ADDRESS: finds key 1 -> 1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH" 1
    fi
    rm -f KEYFOUNDKEYFOUND.txt
}

# ============================================================================
# Test 2: BSGS mode
# Uses the public key for Bitcoin puzzle #21 (known private key 0x1BA534)
# Searches in 21-bit range (2^20 to 2^21) with small N for fast execution
# ============================================================================
test_bsgs_mode() {
    echo ">>> BSGS mode"
    rm -f KEYFOUNDKEYFOUND.txt

    # Create temp file with compressed public key for puzzle #21
    # Private key: 0x1BA534 (1811764 decimal), in 21-bit range
    local BSGS_INPUT
    BSGS_INPUT=$(mktemp /tmp/keyhunt_bsgs_test.XXXXXX)
    echo "031a746c78f72754e0be046186df8a20cdce5c79b2eda76013c647af08d306e49e" > "$BSGS_INPUT"

    # Search in 21-bit range with N=0x100000 (sqrt(N)=1024, divisible by 1024)
    # K=128 (minimum auto-corrected value)
    timeout 60 $KEYHUNT -m bsgs -f "$BSGS_INPUT" -b 21 -n 0x100000 -k 128 -t 1 -q 2>/dev/null || true

    if [ -f KEYFOUNDKEYFOUND.txt ]; then
        # BSGS output format: "Key found privkey 1ba534"
        if grep -qi "1ba534" KEYFOUNDKEYFOUND.txt; then
            run_test "BSGS: finds puzzle #21 privkey 0x1BA534 from its public key" 0
        else
            echo "    KEYFOUNDKEYFOUND.txt contents:"
            head -20 KEYFOUNDKEYFOUND.txt
            run_test "BSGS: finds puzzle #21 privkey 0x1BA534 from its public key" 1
        fi
    else
        echo "    KEYFOUNDKEYFOUND.txt not created"
        run_test "BSGS: finds puzzle #21 privkey 0x1BA534 from its public key" 1
    fi
    rm -f KEYFOUNDKEYFOUND.txt "$BSGS_INPUT"
}

# ============================================================================
# Test 3: XPOINT mode
# Searches for the x-coordinate of the public key for private key 7
# X-coordinate: 5CBDF0646E5DB4EAA398F365F2EA7A0E3D419B7E0330E39CE92BDDEDCAC4F9BC
# ============================================================================
test_xpoint_mode() {
    echo ">>> XPOINT mode"
    rm -f KEYFOUNDKEYFOUND.txt

    # Create temp file with x-coordinate of public key for private key 7
    local XPOINT_INPUT
    XPOINT_INPUT=$(mktemp /tmp/keyhunt_xpoint_test.XXXXXX)
    echo "5CBDF0646E5DB4EAA398F365F2EA7A0E3D419B7E0330E39CE92BDDEDCAC4F9BC" > "$XPOINT_INPUT"

    # Search in range 1:F where key 7 lives
    timeout 60 $KEYHUNT -m xpoint -f "$XPOINT_INPUT" -r 1:F -t 1 -l compress -q 2>/dev/null || true

    if [ -f KEYFOUNDKEYFOUND.txt ]; then
        # XPOINT output format: "Private Key: 7"
        if grep -q "Private Key: 7" KEYFOUNDKEYFOUND.txt; then
            run_test "XPOINT: finds privkey 7 from its x-coordinate" 0
        else
            echo "    KEYFOUNDKEYFOUND.txt contents:"
            head -20 KEYFOUNDKEYFOUND.txt
            run_test "XPOINT: finds privkey 7 from its x-coordinate" 1
        fi
    else
        echo "    KEYFOUNDKEYFOUND.txt not created"
        run_test "XPOINT: finds privkey 7 from its x-coordinate" 1
    fi
    rm -f KEYFOUNDKEYFOUND.txt "$XPOINT_INPUT"
}

# ============================================================================
# Main
# ============================================================================

# Check keyhunt binary exists
if [ ! -x "$KEYHUNT" ]; then
    echo "ERROR: $KEYHUNT not found or not executable. Run 'make' first."
    exit 1
fi

echo ""
echo "=== E2E Mode Tests ==="
echo ""

test_address_mode
test_bsgs_mode
test_xpoint_mode

echo ""
echo "---"
echo "  Passed: $PASS  Failed: $FAIL  Skipped: $SKIP"
echo "---"

exit $FAIL
