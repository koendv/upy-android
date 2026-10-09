# Tests

The tests run on a phone or an emulator through adb exec. `tools/run-tests.py` sends each test script to the app and compares the output with the `.exp` file next to the script.

Test scripts are `upy-android/tests/*.py` and `upy-android/examples/*_selftest/*.py`.

## Markers

Markers are comment lines at the top of a test script. The first line that is not a comment, or a blank line, ends the markers.

```python
# test: required
# fixture: ../add_simple/add_simple.tflite /examples/add_simple.tflite
```

- `# test: required`: must pass. A failure fails the run.
- `# test: known-failure <reason>`: a known bug. The test runs and a failure is reported, but does not fail the run. If the test passes, the report says "unexpectedly passed": the bug may be fixed, and the test can become required.
- `# test: manual <reason>`: not run by default. Run with `--manual`.
- `# fixture: <local> <device>`: copies a file to the phone before the test. `<local>` is relative to the folder of the test script, `<device>` is an absolute path on the phone.
- `# runs: host`: the script runs on the PC instead of the phone, and talks to the phone through adb exec. The test passes when the script exits with code 0. A host check has no `.exp` file.

Each test script needs one `# test:` line.

A test is named after what it tests.

## Expected output

The `.exp` file holds the expected output. For a known failure, the `.exp` file holds the correct output, not what the script prints today.

A missing `.exp` file is an error. `--record --only <name>` writes the current output as the `.exp` file; read the file before trusting it.

## Running

```
tools/run-tests.py                 # required and known-failure tests
tools/run-tests.py --manual        # also manual tests
tools/run-tests.py --only litert   # one test
tools/run-tests.py --list          # tests and their tier
```

Exit code: 0 when all required tests pass, 1 when a required test fails, 2 for a marker error.
