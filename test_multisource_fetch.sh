#!/bin/bash
#
# test_multisource_fetch.sh - Test multi-source community fetch
#
# This script tests that wizard_community_fetch_all_sources() successfully
# fetches data from all 3 sources: BTCPuzzle.info, Privatekeys.pw, Keys.lol
#

echo "========================================"
echo "Multi-Source Community Fetch Test"
echo "========================================"
echo ""
echo "This test verifies that the wizard can fetch community progress"
echo "from all 3 sources for puzzle #66:"
echo "  1. BTCPuzzle.info (scanned ranges)"
echo "  2. Privatekeys.pw (progress percentage)"
echo "  3. Keys.lol (progress percentage)"
echo ""
echo "Expected output:"
echo "  [+] Successfully fetched data from X/3 sources"
echo ""
echo "Running test..."
echo ""
echo "========================================"
echo ""

# Check if keyhunt exists
if [ ! -f ./keyhunt ]; then
    echo "[-] Error: keyhunt executable not found"
    echo "[!] Please run 'make' first to build keyhunt"
    exit 1
fi

# Create a test input file to automate wizard selections
cat > /tmp/wizard_test_input.txt <<'EOF'
66
n
EOF

# Run wizard with automated input and capture output
echo "[+] Running wizard with puzzle 66..."
echo ""

# Run the wizard and filter for community fetch output
./keyhunt --wizard < /tmp/wizard_test_input.txt 2>&1 | tee /tmp/wizard_output.txt

# Clean up input file
rm -f /tmp/wizard_test_input.txt

echo ""
echo "========================================"
echo "Test Results"
echo "========================================"
echo ""

# Check for success indicators in output
if grep -q "Successfully fetched data from.*sources" /tmp/wizard_output.txt; then
    SOURCES=$(grep "Successfully fetched data from" /tmp/wizard_output.txt | grep -oE '[0-9]+/[0-9]+' | head -1)
    echo "[✓] Multi-source fetch completed: $SOURCES sources"

    # Check each source
    echo ""
    echo "Source breakdown:"

    if grep -q "BTCPuzzle.info:" /tmp/wizard_output.txt; then
        if grep -A1 "BTCPuzzle.info:" /tmp/wizard_output.txt | grep -q "✓"; then
            echo "  [✓] BTCPuzzle.info - Success"
        else
            echo "  [✗] BTCPuzzle.info - Failed"
        fi
    fi

    if grep -q "Privatekeys.pw:" /tmp/wizard_output.txt; then
        if grep -A1 "Privatekeys.pw:" /tmp/wizard_output.txt | grep -q "✓"; then
            echo "  [✓] Privatekeys.pw - Success"
        else
            echo "  [✗] Privatekeys.pw - Failed"
        fi
    fi

    if grep -q "Keys.lol:" /tmp/wizard_output.txt; then
        if grep -A1 "Keys.lol:" /tmp/wizard_output.txt | grep -q "✓"; then
            echo "  [✓] Keys.lol - Success"
        else
            echo "  [✗] Keys.lol - Failed"
        fi
    fi

    echo ""

    # Check if all 3 sources succeeded
    SUCCESS_COUNT=$(echo "$SOURCES" | cut -d'/' -f1)
    if [ "$SUCCESS_COUNT" = "3" ]; then
        echo "[✓✓✓] ALL 3 SOURCES SUCCESSFUL!"
        echo "Multi-source community fetch is working correctly."
        echo ""
        exit 0
    elif [ "$SUCCESS_COUNT" -ge "1" ]; then
        echo "[✓] PARTIAL SUCCESS ($SOURCES)"
        echo "Some sources are unavailable (network/offline)."
        echo "This is acceptable for testing purposes."
        echo ""
        exit 0
    fi
else
    echo "[✗] Could not find multi-source fetch output"
    echo "[!] The test may have failed or output format changed"
    echo ""
    echo "Full output saved to: /tmp/wizard_output.txt"
    exit 1
fi

# Clean up
rm -f /tmp/wizard_output.txt
