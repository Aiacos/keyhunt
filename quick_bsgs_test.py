#!/usr/bin/env python3
"""Quick BSGS test with shorter timeout"""

import subprocess
import sys

def main():
    print("=== Quick BSGS Test ===\n")

    # Try with just 1 second search time
    cmd = [
        './keyhunt',
        '-m', 'bsgs',
        '-f', 'tests/125.txt',
        '-b', '125',
        '-q',
        '-s', '1',
        '-R'
    ]

    print(f"Running: {' '.join(cmd)}\n")

    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=10
        )

        print("STDOUT:")
        print(proc.stdout)
        print("\nSTDERR:")
        print(proc.stderr)
        print(f"\nExit code: {proc.returncode}")

        sys.exit(proc.returncode)

    except subprocess.TimeoutExpired:
        print("\n✗ Timeout after 10 seconds - BSGS may be stuck")
        sys.exit(1)
    except Exception as e:
        print(f"\n✗ Error: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
