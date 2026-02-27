#!/bin/bash
# Test script for PGO binary verification

echo "Testing PGO-optimized binary on known test cases..."
./keyhunt_pgo -m address -f tests/1to32.txt -r 1:FF -q -s 0 2>&1 | grep 'Hit!' && echo 'OK' || echo 'FAIL'
