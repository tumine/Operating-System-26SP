#!/usr/bin/env python3
"""
Merge per-object compile database fragments into a single compile_commands.json.
"""

import glob
import json
import os
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: merge_compdb.py <compdb_dir> <out_json>", file=sys.stderr)
        return 2

    compdb_dir = sys.argv[1]
    out = sys.argv[2]

    files = sorted(glob.glob(os.path.join(compdb_dir, "*.json")))
    cmds = []
    for path in files:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read().strip()
        if not text:
            continue
        cmds.append(json.loads(text))

    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(cmds, f, indent=2)
        f.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

