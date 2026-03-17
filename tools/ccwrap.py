#!/usr/bin/env python3
"""
Minimal compiler wrapper that emits compile database fragments.

Usage (Makefile):
  COMPDB_DIR=build/compdb COMPDB_FILE=path/to/src.c \
    python3 tools/ccwrap.py riscv64-unknown-elf-gcc <args...>
"""

import json
import os
import shlex
import subprocess
import sys


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: ccwrap.py <real-cc> [args...]", file=sys.stderr)
        return 2

    realcc = sys.argv[1]
    args = sys.argv[2:]

    # Run the real compiler first; only record successful compilations.
    p = subprocess.run([realcc] + args)
    if p.returncode != 0:
        return p.returncode

    compdb_dir = os.environ.get("COMPDB_DIR")
    if not compdb_dir:
        return 0

    src = os.environ.get("COMPDB_FILE")
    out = None
    for i, a in enumerate(args):
        if a == "-o" and i + 1 < len(args):
            out = args[i + 1]
            break

    if not src:
        for a in reversed(args):
            if a.endswith((".c", ".S", ".s", ".cc", ".cpp", ".cxx")):
                src = a
                break

    if not src or not out:
        return 0

    entry = {
        "directory": os.getcwd(),
        "command": " ".join(shlex.quote(x) for x in ([realcc] + args)),
        "file": os.path.abspath(src),
    }

    os.makedirs(compdb_dir, exist_ok=True)
    # Use output path to ensure uniqueness.
    name = out.replace("/", "_") + ".json"
    tmp = os.path.join(compdb_dir, name + ".tmp")
    final = os.path.join(compdb_dir, name)
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(entry, f)
        f.write("\n")
    os.replace(tmp, final)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

