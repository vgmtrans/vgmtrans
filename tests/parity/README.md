# Original/value parity tests

`vgmtrans-parity` links both architectures into one executable. The original
side uses `VGMRoot`, `VGMColl`, and `VGMSeq`; the rewritten side uses `Session`
and the value format registry. A test therefore compares the two code paths
from the same source revision. It does not compare two Git branches.

The synthetic self-test is always available:

```sh
cmake --build cmake-build-debug --target vgmtrans-parity
ctest --test-dir cmake-build-debug --output-on-failure -L self-test
```

## Real corpus

Game data is opt-in because it cannot be checked into the repository. Configure
one or more local files:

```sh
cmake -S . -B cmake-build-debug \
  -DVGMTRANS_PARITY_CAPCOM_SNES_FILE="/path/to/game.rsn" \
  -DVGMTRANS_PARITY_AKAO_SNES_FILE="/path/to/game.rsn" \
  -DVGMTRANS_PARITY_KONAMI_SNES_FILE="/path/to/song.spc" \
  -DVGMTRANS_PARITY_AKAO_FILE="/path/to/song.psf" \
  -DVGMTRANS_PARITY_NDS_FILE="/path/to/game.nds"
```

Then build and run every configured comparison:

```sh
cmake --build cmake-build-debug --target vgmtrans-parity
ctest --test-dir cmake-build-debug --output-on-failure -L corpus
```

Each format is also a CTest label, so a single family can be run independently:

```sh
ctest --test-dir cmake-build-debug --output-on-failure -L capcom-snes
```

Summary, MIDI, and SF2/DLS tests are deliberately separate. One mismatch will
not prevent CTest from reporting the other layers. Capcom SNES additionally has
an export smoke test that checks whole-archive discovery and artifact creation.

### Known NDS baseline findings

The compiler-cursor NDS migration was checked against a Castlevania: Dawn of
Sorrow ROM (40 collections). Summary parity passed for all collections, as did
every SF2 and DLS comparison. MIDI reaches a pre-existing original/value
difference in `SDL_BGM_ARR1_`: at tick 817 on track 6, the original path emits
pitch-bend-range data entry (`CC 6 = 2`) before expression, while the value path
does not. The identical normalized event counts, first mismatch, and context
reproduce at `9341da765`, before the compiler-cursor migration.

The earlier semantic NDS migration was also checked against a Mega Man ZX ROM (35
collections) and a Last Window mini2SF set (56 collections). Summary and
SF2/DLS parity passed for every collection. MIDI comparison reached these two
pre-existing differences between the original and value architectures:

- Mega Man ZX `CRISIS`: the original path emits an extra zero pitch-bend at
  tick 2343 before the next bend at tick 2345.
- Last Window `SEQ_BGM_23`: the original path emits one extra pitch-bend-range
  data-entry controller at tick zero on track 15.

Both differences reproduce with the same normalized event counts, first
mismatch, and event context at commit `0b1f6970c`, before the NDS semantic
migration. They are renderer/output-normalization differences, not migration
regressions. Tetris DS mini2SF parity currently stops earlier because the
original-side corpus map contains duplicate empty collection names; this
harness limitation also reproduces before the migration.

### Konami SNES compiler-cursor baseline

Immediately before the Konami SNES compiler-cursor migration, the complete
`vgmtrans-value-core-tests` suite and the parity harness self-test passed. No
Konami SPC or RSN corpus was configured or present in the development
environment, so original/value real-file summary, MIDI, requested-loop MIDI,
simulation, and synth comparisons could not be recorded there.

The checked-in synthetic Konami fixture supplies the executable fallback
baseline: module summary/source-map structure, default MIDI modulation,
sequence-event vibrato simulation, synth/sample construction, program tuning,
early-engine global vibrato range behavior, percussion banking, calls,
context-sensitive return/end, both loop-state families, compressed notes,
ties, rests, pitch slides, versioned operand lengths, truncation, and
play-once global loop coordination.
