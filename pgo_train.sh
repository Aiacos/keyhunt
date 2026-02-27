#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

# PGO Training Script
# Runs representative workloads to generate high-quality profile data
# Total runtime: ~60 seconds

echo "[PGO Training] Starting profile data generation..."

# Use the instrumented binary
KEYHUNT="./keyhunt_pgo_gen"

if [ ! -f "$KEYHUNT" ]; then
  echo "[ERROR] keyhunt_pgo_gen not found. Run 'make pgo-generate' first."
  exit 1
fi

# Create profile data directory
mkdir -p pgo_data

echo "[1/5] Address mode - compressed keys (15s)"
# Exercise: EC point operations → SHA256 → RIPEMD160 → bloom filter → binary search
timeout 16 "$KEYHUNT" -m address -f tests/1to32.txt -r 1:FFFFFFFF -l compress -q -s 2 2>&1 | tail -n 3 || true

echo "[2/5] Address mode - uncompressed keys (10s)"
# Different code path for uncompressed point serialization
timeout 11 "$KEYHUNT" -m address -f tests/1to32.txt -r 1:FFFFFFF -l uncompress -q -s 2 2>&1 | tail -n 3 || true

echo "[3/5] RMD160 mode - direct hash search (10s)"
# Exercise: EC ops → hash computation → bloom filter (no base58 decode overhead)
timeout 11 "$KEYHUNT" -m rmd160 -f tests/66.rmd -r 1:FFFFFFFF -l compress -q -s 2 2>&1 | tail -n 3 || true

echo "[4/5] BSGS mode - small table (10s)"
# Exercise: BSGS algorithm → bloom filters → point arithmetic
# Use small -n value to keep memory manageable during training
timeout 11 "$KEYHUNT" -m bsgs -f tests/66.txt -b 66 -n 67108864 -q -s 2 2>&1 | tail -n 3 || true

echo "[5/5] XPoint mode - public key search (10s)"
# Exercise: EC ops → X-coordinate extraction → bloom filter
timeout 11 "$KEYHUNT" -m xpoint -f tests/120.txt -r 1:FFFFFFFF -q -s 2 2>&1 | tail -n 3 || true

echo ""
echo "[PGO Training] Profile data generation complete"
echo "[PGO Training] Checking generated profile files..."

# Verify profile data was generated
if [ -d pgo_data ] && [ -n "$(ls pgo_data/*.gcda 2>/dev/null)" ]; then
  GCDA_COUNT=$(ls pgo_data/*.gcda 2>/dev/null | wc -l)
  echo "[✓] Generated $GCDA_COUNT profile data files in pgo_data/"
  echo ""
  echo "Next step: Run 'make pgo-use' to build optimized binary with profile data"
else
  echo "[!] Warning: No .gcda files found in pgo_data/"
  echo "    Profile data may not have been generated correctly"
  exit 1
fi

echo "[✓] pgo_train.sh completed"
