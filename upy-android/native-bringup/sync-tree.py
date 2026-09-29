#!/usr/bin/env python3
# Makes DEST an exact copy of SRC, but only writes files whose content
# differs. Unchanged files keep their timestamps, so an incremental
# native build only recompiles what really changed.
# Usage: sync-tree.py SRC DEST
import filecmp
import os
import shutil
import sys

src, dest = sys.argv[1], sys.argv[2]

wanted = set()
for root, _, files in os.walk(src):
    rel = os.path.relpath(root, src)
    os.makedirs(os.path.join(dest, rel), exist_ok=True)
    for name in files:
        s = os.path.join(root, name)
        d = os.path.normpath(os.path.join(dest, rel, name))
        wanted.add(d)
        if not os.path.isfile(d) or not filecmp.cmp(s, d, shallow=False):
            shutil.copyfile(s, d)

for root, dirs, files in os.walk(dest, topdown=False):
    for name in files:
        d = os.path.normpath(os.path.join(root, name))
        if d not in wanted:
            os.remove(d)
    if not os.listdir(root) and root != dest:
        os.rmdir(root)
