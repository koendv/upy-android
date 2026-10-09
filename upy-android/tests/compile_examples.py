# test: required
# runs: host
#
# Every example script in the repo compiles on the phone. Nothing runs.
# The repo copy, not the phone's /examples: that copy is only refreshed
# when the app's example version changes.
import os
import sys

import _upy_adb_core as core

EXAMPLES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "examples")

paths = []
for folder in sorted(os.listdir(EXAMPLES)):
    folder_path = os.path.join(EXAMPLES, folder)
    if os.path.isdir(folder_path):
        for name in sorted(os.listdir(folder_path)):
            if name.endswith(".py"):
                paths.append(os.path.join(folder, name))
if not paths:
    sys.exit("no example scripts found in %s" % EXAMPLES)

failed = []
for rel in paths:
    with open(os.path.join(EXAMPLES, rel), encoding="utf-8") as f:
        source = f.read()
    script = "compile(%r, %r, 'exec')\nprint('ok')\n" % (source, rel)
    output, error = core.run_script(script, reset_first=False)
    if error is not None or output != "ok\n":
        failed.append("%s: %s" % (rel, (error or output).strip()))

print("%d examples compiled, %d failed" % (len(paths), len(failed)))
for line in failed:
    print("  " + line)
sys.exit(1 if failed else 0)
