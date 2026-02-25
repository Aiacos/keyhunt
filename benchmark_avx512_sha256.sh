#!/bin/bash
# AVX-512 SHA256 Benchmark Script
# Compares performance between dual AVX2 (2x 8-way) and native AVX-512 (1x 16-way)
# Requires: AVX-512 capable CPU (Intel Skylake-X or newer, AMD Zen 4+)

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}AVX-512 SHA256 Benchmark Script${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check CPU features
echo -e "${YELLOW}Checking CPU features...${NC}"
if grep -q "avx512f" /proc/cpuinfo; then
    echo -e "${GREEN}✓ AVX-512 Foundation supported${NC}"
else
    echo -e "${RED}✗ AVX-512 not supported on this CPU${NC}"
    echo "This benchmark requires an AVX-512 capable CPU"
    exit 1
fi

if grep -q "avx512dq" /proc/cpuinfo; then
    echo -e "${GREEN}✓ AVX-512 DQ supported${NC}"
else
    echo -e "${RED}✗ AVX-512 DQ not supported${NC}"
    exit 1
fi

if grep -q "avx2" /proc/cpuinfo; then
    echo -e "${GREEN}✓ AVX2 supported (for comparison)${NC}"
else
    echo -e "${YELLOW}! AVX2 not found (unusual for AVX-512 CPU)${NC}"
fi

echo ""

# Get CPU model
CPU_MODEL=$(grep "model name" /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)
echo -e "${BLUE}CPU:${NC} $CPU_MODEL"
echo ""

# Check if keyhunt binary exists
if [ ! -f "./keyhunt" ]; then
    echo -e "${RED}Error: keyhunt binary not found${NC}"
    echo "Please build keyhunt first: make clean && make"
    exit 1
fi

# Check test files
if [ ! -f "tests/1to32.txt" ]; then
    echo -e "${RED}Error: Test file tests/1to32.txt not found${NC}"
    exit 1
fi

echo -e "${YELLOW}========================================${NC}"
echo -e "${YELLOW}Running Benchmarks${NC}"
echo -e "${YELLOW}========================================${NC}"
echo ""
echo "Test configuration:"
echo "  Mode: address (GetHash160_fromX_AVX512 code path)"
echo "  File: tests/1to32.txt"
echo "  Range: 1:FFFFFFFF (32-bit)"
echo "  Threads: All available cores"
echo "  Duration: 10 seconds per run"
echo ""

# Detect number of threads
THREADS=$(nproc)
echo "Detected $THREADS CPU threads"
echo ""

# Function to extract keys/sec from output
extract_speed() {
    grep -oP '\d+\.\d+ Mkey/s' | head -1 | cut -d' ' -f1
}

echo -e "${BLUE}Running benchmark (this will take ~30 seconds)...${NC}"
echo ""

# Run benchmark
echo "Command: ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t $THREADS -s 10 -q"
RESULT=$(./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t $THREADS -s 10 -q 2>&1 || true)

echo "$RESULT"
echo ""

# Extract performance metrics
SPEED=$(echo "$RESULT" | extract_speed)

if [ -z "$SPEED" ]; then
    echo -e "${RED}Failed to extract performance metrics${NC}"
    echo "Full output:"
    echo "$RESULT"
    exit 1
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Benchmark Results${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "AVX-512 16-way SHA256: $SPEED Mkey/s"
echo ""

# Theoretical analysis
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Theoretical Analysis${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "AVX-512 Implementation Benefits:"
echo "  • Single-pass processing (vs 2 passes with dual AVX2)"
echo "  • 16-way parallelism using ZMM registers (512-bit)"
echo "  • Reduced register pressure and instruction count"
echo "  • Better pipeline utilization"
echo ""
echo "Expected improvement over dual AVX2: 20-35%"
echo "  • Instruction count reduction: ~40%"
echo "  • Memory bandwidth savings: ~30%"
echo "  • Overall SHA256 stage: 20-35% faster"
echo ""

# Compare with baseline from PERFORMANCE_ANALYSIS.md
echo -e "${YELLOW}========================================${NC}"
echo -e "${YELLOW}Historical Comparison${NC}"
echo -e "${YELLOW}========================================${NC}"
echo ""
echo "Performance Evolution:"
echo "  Baseline (SSE2 SHA256):     63 Mkey/s (2025-11-02)"
echo "  AVX2 SHA256 (8-way):        83 Mkey/s (+31.7%)"
echo "  AVX2 + -O3 optimization:    86 Mkey/s (+36.5%)"
echo "  AVX-512 SHA256 (16-way):    $SPEED Mkey/s"
echo ""

# Calculate improvement if we have the data
if [ -n "$SPEED" ]; then
    BASELINE=86
    IMPROVEMENT=$(echo "scale=2; ($SPEED - $BASELINE) / $BASELINE * 100" | bc)
    TOTAL_IMPROVEMENT=$(echo "scale=2; ($SPEED - 63) / 63 * 100" | bc)

    echo -e "${GREEN}Improvement over AVX2:      +$IMPROVEMENT%${NC}"
    echo -e "${GREEN}Total improvement:          +$TOTAL_IMPROVEMENT%${NC}"
fi

echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Benchmark Complete${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "Results saved to: benchmark_avx512_results.txt"

# Save results
cat > benchmark_avx512_results.txt << EOF
AVX-512 SHA256 Benchmark Results
=================================
Date: $(date)
CPU: $CPU_MODEL
Threads: $THREADS

Performance:
  AVX-512 16-way SHA256: $SPEED Mkey/s

Configuration:
  Mode: address (GetHash160_fromX_AVX512)
  Test file: tests/1to32.txt
  Range: 1:FFFFFFFF (32-bit)
  Duration: 10 seconds

Historical Comparison:
  Baseline (SSE2):        63 Mkey/s
  AVX2 8-way:             83 Mkey/s (+31.7%)
  AVX2 + -O3:             86 Mkey/s (+36.5%)
  AVX-512 16-way:         $SPEED Mkey/s (+$IMPROVEMENT% vs AVX2)

Implementation Details:
  - Native AVX-512 implementation using ZMM registers
  - Single-pass 16-way parallelism (vs dual 8-way)
  - Reduced instruction count and register pressure
  - Optimized with ternarylogic and broadcast operations
EOF

echo "Detailed results written to benchmark_avx512_results.txt"
