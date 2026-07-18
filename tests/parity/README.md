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
