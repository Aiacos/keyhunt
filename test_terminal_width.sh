#!/bin/bash
# Terminal Width Responsive Output - Automated Test Script
#
# This script tests the keyhunt terminal width-responsive output
# on various terminal widths (narrow, standard, wide).

set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Check if keyhunt exists
if [ ! -x "./keyhunt" ]; then
    echo -e "${RED}Error: keyhunt executable not found or not executable${NC}"
    echo "Please run 'make' first to build the project."
    exit 1
fi

echo -e "${CYAN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║${NC}  Terminal Width Responsive Output - Test Suite           ${CYAN}║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Test 1: Narrow Terminal (80 columns)
echo -e "${YELLOW}[TEST 1]${NC} Testing on 80-column terminal (narrow)"
echo -e "${CYAN}Command:${NC} COLUMNS=80 ./keyhunt --help"
echo ""
COLUMNS=80 ./keyhunt --help 2>&1 | head -40
echo ""
echo -e "${GREEN}✓${NC} Test 1 complete. Verify banner fits in 80 columns with no overflow."
echo ""
read -p "Press Enter to continue to Test 2..."
echo ""

# Test 2: Standard Terminal (120 columns)
echo -e "${YELLOW}[TEST 2]${NC} Testing on 120-column terminal (standard)"
echo -e "${CYAN}Command:${NC} COLUMNS=120 ./keyhunt --help"
echo ""
COLUMNS=120 ./keyhunt --help 2>&1 | head -40
echo ""
echo -e "${GREEN}✓${NC} Test 2 complete. Verify banner is capped at 80 columns."
echo ""
read -p "Press Enter to continue to Test 3..."
echo ""

# Test 3: Very Wide Terminal (200 columns)
echo -e "${YELLOW}[TEST 3]${NC} Testing on 200-column terminal (very wide)"
echo -e "${CYAN}Command:${NC} COLUMNS=200 ./keyhunt --help"
echo ""
COLUMNS=200 ./keyhunt --help 2>&1 | head -40
echo ""
echo -e "${GREEN}✓${NC} Test 3 complete. Verify banner remains at 80 columns max."
echo ""
read -p "Press Enter to continue to Test 4..."
echo ""

# Test 4: Very Narrow Terminal (60 columns)
echo -e "${YELLOW}[TEST 4]${NC} Testing on 60-column terminal (very narrow)"
echo -e "${CYAN}Command:${NC} COLUMNS=60 ./keyhunt --help"
echo ""
COLUMNS=60 ./keyhunt --help 2>&1 | head -40
echo ""
echo -e "${GREEN}✓${NC} Test 4 complete. Verify banner uses minimum 50 columns."
echo ""
read -p "Press Enter to continue to Test 5..."
echo ""

# Test 5: Benchmark on 80 columns
echo -e "${YELLOW}[TEST 5]${NC} Testing benchmark on 80-column terminal"
echo -e "${CYAN}Command:${NC} COLUMNS=80 ./keyhunt --benchmark"
echo -e "${YELLOW}Note:${NC} This will run a full benchmark (may take 30-60 seconds)"
echo ""
read -p "Press Enter to start benchmark or Ctrl+C to skip..."
COLUMNS=80 ./keyhunt --benchmark 2>&1
echo ""
echo -e "${GREEN}✓${NC} Test 5 complete. Verify all tables fit in 80 columns."
echo ""
read -p "Press Enter to continue to Test 6..."
echo ""

# Test 6: Redirected Output (Pipe)
echo -e "${YELLOW}[TEST 6]${NC} Testing redirected output (pipe detection)"
echo -e "${CYAN}Command:${NC} ./keyhunt --help | cat | head -40"
echo ""
./keyhunt --help 2>&1 | cat | head -40
echo ""
echo -e "${GREEN}✓${NC} Test 6 complete. Verify output defaults to 80 columns."
echo ""

# Summary
echo ""
echo -e "${CYAN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║${NC}  All Tests Complete                                       ${CYAN}║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${GREEN}Test Summary:${NC}"
echo "  ✓ Test 1: 80-column terminal (narrow)"
echo "  ✓ Test 2: 120-column terminal (standard)"
echo "  ✓ Test 3: 200-column terminal (very wide)"
echo "  ✓ Test 4: 60-column terminal (very narrow)"
echo "  ✓ Test 5: Benchmark on 80 columns"
echo "  ✓ Test 6: Redirected output (pipe)"
echo ""
echo -e "${YELLOW}Verification Checklist:${NC}"
echo "  [ ] No text overflow beyond terminal width"
echo "  [ ] No awkward line wrapping"
echo "  [ ] Box borders align properly (╔╗╚╝═║)"
echo "  [ ] Content is centered/padded correctly"
echo "  [ ] Tables have proper alignment"
echo "  [ ] Banner caps at 80 columns on wide terminals"
echo "  [ ] Banner uses minimum 50 columns on narrow terminals"
echo "  [ ] Benchmark tables fit within terminal width"
echo ""
echo -e "${GREEN}If all items checked, the implementation is VERIFIED ✅${NC}"
echo ""
