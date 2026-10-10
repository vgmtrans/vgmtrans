# Tests

Build and run the self-contained suites from the repository root:

```sh
cmake -S . -B build/tests -DBUILD_TESTING=ON -DENABLE_UI_QT=OFF
cmake --build build/tests --parallel
ctest --test-dir build/tests --output-on-failure --parallel
```

With a multi-configuration generator, select the same configuration for the build
and CTest (for example, `--config Release` and `-C Release`). Enable `ENABLE_UI_QT`
to include the Qt model tests.

`vgmtrans-value-core-tests` covers shared architecture. Each format has a separate
`vgmtrans-*-tests` executable, so format tests compile and run once. Run a focused
suite with CTest's `-R` option, for example `-R '^vgmtrans-nin-snes-tests$'`.
Building only the core target does not build the format suites.

Core test files are grouped by behavior: MIDI encoding, MIDI rendering, pitch,
modulation, sample processing, synth serialization, and collection/sequence
exports. Format-specific byte fixtures stay alongside their regression tests.
Shared headers contain assertions, event queries, or narrowly scoped fixtures.

When adding a test, give it a behavior-based name and call it from the file's
`run...Tests()` function. Keep inputs and expected results close together. Use
`expect` rather than `assert` so checks remain active in Release builds;
`expectThrows` checks the expected exception type. Prefer small tables for related
cases and explain opaque bytecode, hardware constants, and regression triggers.
Keep distinct boundary, lifetime, malformed-input, and driver-version cases;
avoid checks that merely read back fields assigned by the fixture.

Real game data is optional and is not stored here. The parity executable retains
its self-test and the `VGMTRANS_PARITY_*` CMake cache options for local corpora.
Existing format executables with corpus command-line options retain those modes;
CTest invokes them without arguments to run their synthetic regressions.

To check a change against another value-core revision, build the shell at both
revisions and put private input paths in a text file, one per line. Then run
(Python 3.11 or later):

```sh
python3 tests/compare_value_exports.py /path/to/baseline/vgmtrans-shell \
  /path/to/candidate/vgmtrans-shell /path/to/inputs.txt /path/to/new-results \
  --jobs 4 --timeout 900
```

The output directory must be new. It retains binary/input hashes, raw logs, exit
statuses, exported MIDI/SF2/DLS files, and per-case SHA-256 comparisons. Each input
runs with synth modulation and with sampled modulation plus used instruments,
pitch-bend transitions, and RPN tuning; both use zero extra loops. Large ROMs can
produce many gigabytes of output because every collection is exported.

Any filename, byte, normalized-log, or exit-status difference produces a nonzero
exit code. Timeouts also fail, even when both revisions time out. Review intended
changes separately; the script has no allowed-difference list. Matching export
failures are reported in `baseline_nonzero_statuses` and are not evidence of
successful conversion. This checks two value-core revisions, not legacy parity
or audible accuracy.
