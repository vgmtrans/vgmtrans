# Intelligent Systems N-SPC audit

Audited on 2026-09-07. Scope: the value implementation in `src/value/formats/NinSnes`, with Metal Combat as the initial failure case. Driver addresses below are SPC ARAM addresses, not offsets in the SPC container.

## Evidence and driver differences

References:

- [Metal Combat disassembly](/Users/mike/spcdas/build/metalcombat.s), checked against the 57 SPC snapshots in `/Volumes/emu/vgm/SPC/Metal Combat.rsn`.
- [Fire Emblem: Monshou no Nazo (FE3)](https://raw.githubusercontent.com/loveemu/vgm-disasm/refs/heads/master/snes/NSPC/Intelligent%20Systems/Fire%20Emblem%20-%20Monshou%20no%20Nazo.s).
- [Fire Emblem: Seisen no Keifu (FE4)](https://raw.githubusercontent.com/loveemu/vgm-disasm/refs/heads/master/snes/NSPC/Intelligent%20Systems/Fire%20Emblem%20-%20Seisen%20no%20Keifu.s).
- [Panel de Pon](https://raw.githubusercontent.com/loveemu/vgm-disasm/refs/heads/master/snes/NSPC/Intelligent%20Systems/Panel%20de%20Pon.s).
- [Tetris Attack](https://raw.githubusercontent.com/loveemu/vgm-disasm/refs/heads/master/snes/NSPC/Intelligent%20Systems/Tetris%20Attack.s).

Instruction bytes, branch destinations, table references, and DSP writes take precedence over annotations. In particular, the FE3 percussion comment is misleading: `BBC0` at `$04D4` enters the custom-table path when bit 0 is **clear**. Apparent instructions inside command-address, length, pitch, and instrument tables were treated as data.

| Driver | Instruments / DIR | Commands start | Percussion | FA negative argument |
| --- | --- | --- | --- | --- |
| Metal Combat | `$1980` / `$1B00` | `$D6` | 12 slots; F9 copies three arrays; F5 controls mode | No overwrite branch |
| FE3 | `$1980` / `$1B00` | `$D6` | Same layout and flag polarity as Metal Combat | Overwrites a six-byte instrument |
| Panel de Pon | `$1D80` / `$1F00` | `$DA` | 16 slots; FC deinterleaves triples; bit 6 selects custom mode | Overwrites a six-byte instrument |
| Tetris Attack | `$1A80` / `$1C00` | `$DA` | Same command family as Panel de Pon | Overwrites a six-byte instrument |
| FE4 | `$FE00` / `$FD00` | `$DA` | Always uses its 16-slot table | Dispatches a separate host/SFX operation |

These are addresses in the supplied references, not hardcoded game-identification rules. Metal Combat remains in the FE3 family, with its missing overwrite extension detected separately.

## Corrected findings

1. **Metal Combat rejected as an unknown revision.** The scanner required the initial negative-argument branch of FA. Metal Combat's FA begins at `$0863` with the voice-table pointer save, while FE3 adds an overwrite branch at `$0884`. Recognition now validates the complete FE3 command-length table and note-decoder signature independently of this optional extension. Its actual instrument loader at `$092E` and DIR setup at `$0432` then become available to bank discovery.
2. **Revision-specific tables were replaced with constants.** The scanner now captures the driver's extended duration/velocity tables and FE3-family FB transpose table. Metal Combat uses -6/+6 where FE3 uses -1/+1. Separate duration and velocity pointers are retained even when they reference the same data.
3. **FE4's aligned instrument loader lacked a probe.** Its `$0B2D` loader adds only a high byte after checking the channel mask. An explicit probe supports this form without depending on another six-byte loader elsewhere in RAM.
4. **Sparse banks were truncated.** Mixed zero/FF padding in Tetris Attack's no-intro main-theme snapshot hid instruments 34 and above. Intelligent Systems scanning skips invalid holes and, when DIR follows the table, stops at that boundary.
5. **Noise rows were rejected as bad SRCNs.** FE3/Metal Combat and TA/Panel de Pon interpret negative SRCNs as DSP noise. They now produce noise samples using the existing DSP noise codec, without hiding subsequent instruments. Per-key regions prevent melodic key tracking from changing noise frequency. FE4 retains literal SRCNs, including the upper half of DIR.
6. **Percussion semantics differed across revisions.** F9 now decodes the FE3 family's three 12-byte arrays, flag polarity matches the branch instruction, and negative program numbers use the percussion base. FE4 always reads its custom table and masks the patch to six bits. FC preserves untouched entries and derives its first destination slot from the high byte of the driver's multiplication. Captured RAM supplies initial percussion tables when a song does not redefine them. Drum recipes preserve note/transpose changes; percussion pan updates persistent channel state.
7. **FB discarded valid flagged indices and tuning.** The eight-bit multiply means only the low six index bits address a record. TA/FE4 high-bit vibrato cancellation is preserved. FE3's zero tuning nibble keeps the previous tuning, and transpose uses the captured revision-specific table.
8. **FE3 instrument overwrites were ignored; TA applied them too early.** Both now update the shared instrument map when FA executes. The current DSP instrument remains selected until an actual instrument-load command. Repeated identical definitions reuse a program identity instead of creating a new exported instrument on every repeat.
9. **Envelope commands were ineffective or changed note timing.** ADSR changes now emit envelopes. Direct GAIN is applied when ADSR is disabled. TA's F8/F9 duration field belongs to an independent ADSR-to-GAIN counter; it no longer overwrites the note key-off rate. FE4 F9 is identified as a zero-operand release-GAIN clear, consistent with its dispatcher and handler.
10. **Channel echo commands replaced global echo settings with a fixed send.** They now update the relevant channel bit while preserving echo volumes, feedback, delay, and other channels. Percussion echo uses the same path.
11. **Section entry discarded persistent instrument and legato state.** Only pattern state is reset now. Held legato voices carry across notes, and a two-tick note no longer receives a zero duration from the key-off clamp.

Cleanup accompanying these changes removes the redundant TA-only program-resolver enum, shares instrument sample collection across revisions, extracts Intelligent Systems table probing into one helper, and removes duplicated custom-note flag state and unused logical-instrument state.

## Validation

A dedicated `vgmtrans-nin-snes-tests` target runs the existing NinSnes unit tests plus regressions for scanner recognition, FE4's loader, live tables, percussion modes, voice indices/tuning, overwrite timing/deduplication, ADSR/echo/GAIN separation, noise, and sparse banks. Its optional archive argument exercises normal Session extraction, scanning, rendering, MIDI export, and SoundFont export. Real game bytes remain outside the repository.

The shared corpus checker now verifies that **each sequence** has a collection with a bank; previously it missed standalone sequences without a collection. Export diagnostics identify their source files.

Commands:

```sh
cmake --build build/codex-release -j 8
QT_QPA_PLATFORM=offscreen ctest --test-dir build/codex-release --output-on-failure -j 4
build/codex-release/tests/vgmtrans-nin-snes-tests '/Volumes/emu/vgm/SPC/Metal Combat.rsn'
build/codex-release/tests/vgmtrans-nin-snes-tests '/Volumes/emu/vgm/SPC/Fire Emblem 3.rsn'
build/codex-release/tests/vgmtrans-nin-snes-tests '/Volumes/emu/vgm/SPC/Fire Emblem 4.rsn'
build/codex-release/tests/vgmtrans-nin-snes-tests '/Volumes/emu/vgm/SPC/Tetris Attack.rsn'
```

The Release build completed without warnings or errors, and all **18/18 CTest targets passed**. The archive checks use one sequence pass for rendering and export:

| Archive | Sequences loaded with banks | Render failures | MIDI/SoundFont export failures |
| --- | --- | --- | --- |
| Metal Combat | 57/57 | 0 | 0 |
| Fire Emblem 3 | 87/87 | 0 | 0 |
| Fire Emblem 4 | 127/127 | 0 | 0 |
| Tetris Attack, including Panel de Pon | 53/53 | 0 | 0 |
| **Total** | **324/324** | **0** | **0** |

The Tetris Attack archive contains 48 Tetris Attack and five Panel de Pon snapshots. Metal Combat's output was also written to disk and verified to contain 57 MIDI files and 57 SoundFonts.

Release application: [VGMTrans.app](/Users/mike/vgmtrans2/build/codex-release/src/ui/qt/VGMTrans.app). Its executable is `/Users/mike/vgmtrans2/build/codex-release/src/ui/qt/VGMTrans.app/Contents/MacOS/VGMTrans` (Mach-O x86_64).

## Remaining accuracy limits

- Rate-based GAIN and the independent ADSR-to-GAIN switch counter require live envelope state for accurate realization. Their operands are parsed, but the timed envelope trajectory is not emulated. This audit removes the incorrect key-off timing substitution without claiming a complete DSP envelope simulation.
- Host port waits, host-triggered SFX/voice operations, and FE4's negative-FA operation are not modeled as interactions with a running SNES CPU.
- DSP noise exports retain each selected rate; the shared global noise clock and cross-voice phase relationships are approximated by independent sample playback.
- Pan polarity flags, hardware stereo/mono switches, and the driver's pitch interpolation/clamping at extreme notes are not fully reproduced. Standard vibrato/tremolo/pitch-envelope behavior uses the common NinSnes performance model.
- Panel de Pon `pdp-03.spc` (Game Over, inside the Tetris Attack archive) contains long finite pattern repeats. Requesting an additional sequence loop can exceed the existing VM limit of 100,000 commands per track. This also occurred before these changes. The corpus runner explicitly exports one pass (`sequenceLoops = 0`); Metal Combat additionally passed with one extra loop during development.

Successful loading and export establish structural correctness and instrument resolution for the tested corpus; they are not a claim of sample-identical audio against SPC hardware.
