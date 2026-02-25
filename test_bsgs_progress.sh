#!/bin/bash
# Integration test for BSGS progress bars
# Tests complete workflow: build, save, load from cache

set -e

echo "=== BSGS Progress Bar Integration Test ==="
echo ""

# Step 1: Clean up any existing cache files
echo "Step 1: Cleaning up existing cache files..."
rm -f keyhunt_bsgs_*.blm keyhunt_bsgs_*.tbl
echo "✓ Cache files deleted"
echo ""

# Step 2: Build and save BSGS tables
echo "Step 2: Building and saving BSGS tables (testing build progress bars)..."
echo "Command: ./keyhunt -m bsgs -f tests/125.txt -b 125 -S -q -s 10"
echo ""
./keyhunt -m bsgs -f tests/125.txt -b 125 -S -q -s 10
echo ""
echo "Expected progress bars displayed:"
echo "  - bP table computation"
echo "  - bP table sorting"
echo "  - Checksum computation (3x bloom filters)"
echo "  - File writing (3x bloom filters + bP table)"
echo ""

# Step 3: Load from cache
echo "Step 3: Loading BSGS tables from cache (testing load progress bars)..."
echo "Command: ./keyhunt -m bsgs -f tests/125.txt -b 125 -S -q -s 10"
echo ""
./keyhunt -m bsgs -f tests/125.txt -b 125 -S -q -s 10
echo ""
echo "Expected progress bars displayed:"
echo "  - Bloom filter loading (3x)"
echo "  - bP table loading"
echo ""

echo "=== Integration Test Complete ==="
echo ""
echo "Manual verification checklist:"
echo "  [ ] Build phase showed bP computation progress bar"
echo "  [ ] Build phase showed sorting progress bar"
echo "  [ ] Build phase showed checksum progress bars (3x)"
echo "  [ ] Build phase showed file writing progress bars (4x)"
echo "  [ ] Load phase showed bloom filter loading progress bars (3x)"
echo "  [ ] Load phase showed bP table loading progress bar"
echo "  [ ] All progress bars reached 100%"
echo "  [ ] No visual artifacts or line overflow"
