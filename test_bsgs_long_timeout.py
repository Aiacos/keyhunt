#!/usr/bin/env python3
"""Test BSGS with longer timeout to see initialization"""

import subprocess
import sys
import time

def main():
    print("=== BSGS Test (60 second timeout) ===\n")

    cmd = [
        './keyhunt',
        '-m', 'bsgs',
        '-f', 'tests/125.txt',
        '-b', '125',
        '-q',
        '-s', '1'
    ]

    print(f"Running: {' '.join(cmd)}\n")
    print("Waiting for initialization (this may take a while for 125-bit range)...\n")

    start_time = time.time()

    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=60
        )

        elapsed = time.time() - start_time
        print(f"\nCompleted in {elapsed:.1f} seconds")
        print("\nSTDOUT:")
        print(proc.stdout[:1000])  # First 1000 chars
        print("\nSTDERR:")
        print(proc.stderr[:1000])
        print(f"\nExit code: {proc.returncode}")

        if proc.returncode == 0 or 'Key' in proc.stdout:
            print("\n✓ BSGS completed")
            sys.exit(0)
        else:
            sys.exit(proc.returncode)

    except subprocess.TimeoutExpired as e:
        print(f"\n✗ Timeout after 60 seconds")
        print("\nPartial STDOUT:")
        print(e.stdout[:1000] if e.stdout else "None")
        print("\nPartial STDERR:")
        print(e.stderr[:1000] if e.stderr else "None")
        sys.exit(1)
    except Exception as e:
        print(f"\n✗ Error: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
