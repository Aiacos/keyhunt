#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

echo "[*] Build (CPU/CUDA autodetect)"
./build_cuda.sh

echo "[*] Bloom benchmark (integrated)"
g++ -O3 -march=native -std=gnu++17 -o /tmp/bench_bloom_integrated \
  benchmark_bloom_integrated.cpp bloom/bloom.cpp xxhash/xxhash.c -lpthread
/tmp/bench_bloom_integrated | head -n 60

echo "[*] Profiler smoke (no hits, 3s)"
NOHIT="/tmp/keyhunt_nohit.rmd"
printf '0123456789abcdef0123456789abcdef01234567\n' > "$NOHIT"
KEYHUNT_PROFILE=1 timeout 4 ./keyhunt -m rmd160 -f "$NOHIT" -r 1:FFFFFFFF -l compress -q -s 1 2>&1 | tail -n 5 || true

echo "[*] GPU FULL smoke (compressed, no hits, 3s)"
KEYHUNT_PROFILE=1 timeout 4 ./keyhunt -m rmd160 -f "$NOHIT" -r 1:FFFFFFFFFFFFFFFF -l compress -G full -q -s 1 2>&1 | tail -n 5 || true

echo "[*] Address quick correctness (small range)"
timeout 5 ./keyhunt -m address -f tests/1to32.txt -r 1:FF -q -s 0 2>&1 | rg -n "Hit! Private Key" | head -n 5 || true

echo "[*] GPU FULL correctness (compressed targets, small range)"
timeout 5 ./keyhunt -m address -f tests/1to32.txt -r 1:FF -G full -q -s 0 2>&1 | rg -n "Hit! Private Key" | head -n 5 || true

echo "[*] GPU FULL correctness (uncompressed target, key=1)"
timeout 5 ./keyhunt -m rmd160 -f tests/key1_uncompressed.rmd -r 1:1 -l uncompress -G full -q -s 0 2>&1 | rg -n "Hit! Private Key: 1" || true

echo "[✓] perf_tests.sh completed"
