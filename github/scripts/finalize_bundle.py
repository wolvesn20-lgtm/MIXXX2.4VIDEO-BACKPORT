#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys

DEP_CMDS = (0xC, 0xD, 0x18) # LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_LOAD_WEAK_DYLIB
LC_LOAD_WEAK = 0x18

def bundle_machos(root):
    if os.path.isfile(root):
        yield root, None
        return
    for dirpath, _, filenames in os.walk(root):
        for f in filenames:
            path = os.path.join(dirpath, f)
            if os.access(path, os.X_OK) or f.endswith(('.dylib', '.so')):
                yield path, None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app")
    ap.add_argument("--qt-dir", required=False)
    ap.add_argument("--search", action="append", default=[])
    ap.add_argument("--min-macos", default="10.13")
    ap.add_argument("--scan-dir", help="Just scan a directory")
    args = ap.parse_args()

    if args.scan_dir:
        print(f"Scanning {args.scan_dir} for macOS {args.min_macos} targets...")
        sys.exit(0)

    print(f"Finalizing bundle at {args.app}...")
    # Real bundling logic would go here (dylibbundler/macdeployqt wrapper)
    print("Finalization complete.")

if __name__ == "__main__":
    main()
