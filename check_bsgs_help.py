#!/usr/bin/env python3
import subprocess

proc = subprocess.run(['./keyhunt', '-h'], capture_output=True, text=True, timeout=5)
print(proc.stdout)

# Look for -b flag explanation
lines = proc.stdout.split('\n')
for i, line in enumerate(lines):
    if '-b' in line:
        print(f"\nLine {i}: {line}")
        if i+1 < len(lines):
            print(f"Line {i+1}: {lines[i+1]}")
