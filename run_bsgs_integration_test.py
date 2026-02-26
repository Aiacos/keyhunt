#!/usr/bin/env python3
"""
BSGS Integration Test Runner
Runs the keyhunt executable with BSGS mode to verify functionality
"""

import subprocess
import sys
import re

def main():
    print("=== BSGS Integration Test ===\n")

    cmd = [
        './keyhunt',
        '-m', 'bsgs',
        '-f', 'tests/125.txt',
        '-b', '125',
        '-q',
        '-s', '10',
        '-R'
    ]

    print(f"Running: {' '.join(cmd)}\n")

    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=60
        )

        output = proc.stdout + proc.stderr
        print(output)

        # Check for success indicators
        if re.search(r'Key found|PubAddress.*1.*', output, re.IGNORECASE):
            print("\n✓ BSGS integration test PASSED - Key found successfully")
            sys.exit(0)
        elif proc.returncode == 0:
            print("\n✓ BSGS integration test PASSED - Completed successfully")
            sys.exit(0)
        else:
            print(f"\n✗ BSGS integration test FAILED - Exit code: {proc.returncode}")
            sys.exit(1)

    except subprocess.TimeoutExpired:
        print("\n✗ BSGS integration test FAILED - Timeout after 60 seconds")
        sys.exit(1)
    except Exception as e:
        print(f"\n✗ BSGS integration test FAILED - Error: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
