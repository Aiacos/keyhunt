#!/usr/bin/env python3
"""Test basic keyhunt functionality"""

import subprocess
import sys

def test_help():
    """Test help output"""
    print("Test 1: Help output")
    proc = subprocess.run(['./keyhunt', '-h'], capture_output=True, text=True, timeout=5)
    if 'Usage' in proc.stdout or 'keyhunt' in proc.stdout.lower():
        print("✓ Help works\n")
        return True
    print("✗ Help failed\n")
    return False

def test_version():
    """Test version or basic execution"""
    print("Test 2: Basic execution test")
    # Try running with minimal parameters
    proc = subprocess.run(
        ['./keyhunt'],
        capture_output=True,
        text=True,
        timeout=5
    )
    print(f"Exit code: {proc.returncode}")
    if proc.stdout:
        print(f"STDOUT: {proc.stdout[:200]}")
    if proc.stderr:
        print(f"STDERR: {proc.stderr[:200]}")
    print()
    return True

def main():
    try:
        test_help()
        test_version()
        print("Basic tests completed")
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
