#!/bin/bash
# Performance comparison script: Regular vs PGO build
# This script builds and benchmarks both versions, then compares results

set -e

RESULTS_FILE="pgo_performance_results.txt"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

echo "==================================================================="
echo "PGO Performance Comparison Test"
echo "Date: $TIMESTAMP"
echo "==================================================================="

# Function to extract keys/sec from benchmark output
extract_performance() {
    local output="$1"
    # Look for patterns like "X.XX Mkeys/sec" or "X,XXX keys/sec"
    echo "$output" | grep -oP '\d+[\.,]?\d*\s*(M?keys/sec|keys/s)' | head -1 || echo "N/A"
}

# Clean everything first
echo ""
echo "[1/6] Cleaning previous builds..."
make clean > /dev/null 2>&1
make pgo-clean > /dev/null 2>&1

# Build regular version
echo "[2/6] Building regular version (with -O2 optimization)..."
time make -j$(nproc) > /dev/null 2>&1
REGULAR_SIZE=$(stat -c%s keyhunt)
echo "  Regular binary size: $(numfmt --to=iec-i --suffix=B $REGULAR_SIZE)"

# Run regular benchmark
echo "[3/6] Running regular build benchmark..."
echo "  This will take approximately 30-60 seconds..."
REGULAR_OUTPUT=$(./keyhunt --benchmark 2>&1 || true)
echo "$REGULAR_OUTPUT" | tail -20

# Build PGO version
echo ""
echo "[4/6] Building PGO version (instrumented + training + optimized)..."
echo "  Step 1: Building instrumented binary..."
make pgo-generate > /dev/null 2>&1

echo "  Step 2: Running training workloads (this takes ~60 seconds)..."
make pgo-train > /dev/null 2>&1

echo "  Step 3: Building PGO-optimized binary..."
time make pgo-use > /dev/null 2>&1
PGO_SIZE=$(stat -c%s keyhunt_pgo)
echo "  PGO binary size: $(numfmt --to=iec-i --suffix=B $PGO_SIZE)"

# Run PGO benchmark
echo "[5/6] Running PGO build benchmark..."
echo "  This will take approximately 30-60 seconds..."
PGO_OUTPUT=$(./keyhunt_pgo --benchmark 2>&1 || true)
echo "$PGO_OUTPUT" | tail -20

# Compare results
echo ""
echo "==================================================================="
echo "[6/6] PERFORMANCE COMPARISON RESULTS"
echo "==================================================================="

# Save detailed results to file
{
    echo "PGO Performance Comparison Test"
    echo "Date: $TIMESTAMP"
    echo "System: $(uname -a)"
    echo "CPU: $(grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)"
    echo "Cores: $(nproc)"
    echo ""
    echo "==================================================================="
    echo "REGULAR BUILD (-O2)"
    echo "==================================================================="
    echo "$REGULAR_OUTPUT"
    echo ""
    echo "==================================================================="
    echo "PGO BUILD (-O2 + Profile-Guided Optimization)"
    echo "==================================================================="
    echo "$PGO_OUTPUT"
    echo ""
    echo "==================================================================="
    echo "SUMMARY"
    echo "==================================================================="
} > "$RESULTS_FILE"

# Display summary
echo ""
echo "Binary Sizes:"
echo "  Regular:  $(numfmt --to=iec-i --suffix=B $REGULAR_SIZE)"
echo "  PGO:      $(numfmt --to=iec-i --suffix=B $PGO_SIZE)"
SIZE_DIFF=$(( (PGO_SIZE - REGULAR_SIZE) * 100 / REGULAR_SIZE ))
echo "  Size diff: ${SIZE_DIFF}%"
echo ""

# Try to extract and compare performance metrics
echo "Performance Metrics:"
echo "  Regular: $(extract_performance "$REGULAR_OUTPUT")"
echo "  PGO:     $(extract_performance "$PGO_OUTPUT")"
echo ""
echo "Detailed results saved to: $RESULTS_FILE"
echo ""
echo "==================================================================="
echo "INTERPRETATION"
echo "==================================================================="
echo "PGO (Profile-Guided Optimization) uses runtime profiling data to:"
echo "  • Optimize hot code paths (frequently executed functions)"
echo "  • Improve branch prediction"
echo "  • Enhance instruction cache locality"
echo "  • Optimize function inlining decisions"
echo ""
echo "Expected improvement: 5-20% for crypto-heavy workloads"
echo "Actual results are shown above in the benchmark output."
echo "==================================================================="

# Append summary to results file
{
    echo ""
    echo "Binary Sizes:"
    echo "  Regular:  $(numfmt --to=iec-i --suffix=B $REGULAR_SIZE)"
    echo "  PGO:      $(numfmt --to=iec-i --suffix=B $PGO_SIZE)"
    echo "  Size diff: ${SIZE_DIFF}%"
    echo ""
    echo "Performance extracted:"
    echo "  Regular: $(extract_performance "$REGULAR_OUTPUT")"
    echo "  PGO:     $(extract_performance "$PGO_OUTPUT")"
} >> "$RESULTS_FILE"

echo ""
echo "✓ Benchmark comparison complete!"
echo "  Review $RESULTS_FILE for full details"
