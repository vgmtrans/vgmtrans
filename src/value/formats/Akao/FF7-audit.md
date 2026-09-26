# FF7 AKAO playback audit

Audited against `SCUS_941.63` in Ghidra and the PSF rip of **114 Underneath the
Rotting Pizza**, 2026-09-26. Sequence changes are restricted to `Version1_0`.
Song offsets below refer to the extracted PSF buffer, not executable addresses.
Track numbers are zero-based, as displayed by the source tree.

## Confirmed discrepancies and fixes

### Envelope commands

The sequence decoder consumed FF7's envelope commands without applying them.
These commands modify the current articulation's envelope; they do not specify
a complete independent envelope. Bank preparation now supplies the selected
sample pools' native ADSR defaults to sequence playback. Playback retains the
register state and applies these operations:

| Opcode | Operation | FF7 handler |
| --- | --- | --- |
| AD | Attack rate | `80032e08` |
| AE | Decay rate | `80032e6c` |
| AF | Sustain level | `80032ed0` |
| B0 | Decay rate and sustain level | `800335d8` |
| B1 | Sustain rate | `80032f34` |
| B2 | Release rate | `80032f98` |
| B3 | Restore articulation ADSR | `800323cc` |
| B7 | Attack mode | `80032ffc` |
| BB | Sustain mode | `80033060` |
| BF | Release mode | `800330c4` |

`A1` also reloads articulation defaults (`800320c4`, loader `80031820`).
SDK mode operands differ from packed SPU bits: attack mode 5 is exponential;
sustain modes 1/5/7 mean linear increase/exponential increase/exponential decrease,
with other values selecting linear decrease; release mode 7 is exponential.
The native SDK setters at `80039850`, `800398ec`, and `800399d0` establish this
mapping. Voice-cache commits occur at `8002e23c`.

In the song, articulation 27 on tracks 7–10 requests `B1 40`, and articulation 28
on tracks 11–12 requests `B1 45`. Their default sustain rate is `3b`:

| State | ADSR1 | ADSR2 | Full-scale sustain fall time in the existing SPU model |
| --- | --- | --- | --- |
| Articulation default | `00ff` | `4ec5` | 1.189 s |
| `B1 40` | `00ff` | `5005` | 2.972 s |
| `B1 45` | `00ff` | `5145` | 6.794 s |

These are sustain-phase calculations before conversion to the SoundFont envelope
shape, not measured complete note lengths. The first commands occur at extracted
offsets `1c0321` and `1c04ab`. Ignoring them substantially accelerates the decay.
The corrected paired export contains two additional envelope presets.

### Slurs and portamento

`CC` writes 1 to the entire connection-flags word at track offset `6e`
(`80032d44`). After a note or tie, the decoder copies bit 0 into bit 1. On the
next melodic note, bit 1 suppresses SPU key-on while the pitch still changes
(`80030e7c`). This preserves both sample position and envelope phase. Previously,
the converter merely lengthened the gate and emitted another attack.

The corrected performance links these notes as one voice. Different pitches
become immediate pitch-bend transitions; repeated pitches extend the same note.
The first note after `CC` still attacks. `D0` (`80032d58`) sets bit 2 instead:
it lengthens gates but does not suppress attacks.

The song's tracks 11 and 12 contain these slurs. For example,
`1c04bb..1c04c5` encodes a B–D–B phrase with one attack. With zero additional
loops, each track now exports **120 attacks instead of 154**, preserving its
pitch changes. The corresponding MIDI note lasts 34 ticks rather than being
split into three attacks.

FF7 also looks ahead through commands and repeat/jump control flow
(`800318bc`). `CD`, `D1`, `DB`, rests, and end clear connection modes before
the preceding timed event, restoring its two-tick gate gap. The converter
revises that preceding event when the VM reaches the boundary. `CC` or `D0`
encountered after that event can establish a new mode; the native off handlers
themselves are no-ops.

`DA` (`8003257c`) additionally enables slur and clears the preceding pitch.
Its first note attacks; subsequent notes glide without attacking. Lookahead
disables the final glide before an off boundary, leaving an immediate pitch
change on the same voice. This behavior is covered separately from the song's
ordinary slurs.

### Early drum envelopes

FF7's five-byte drum entries contain articulation, pitch, expression, and pan,
with no ADSR override fields. The driver loads ADSR from the selected articulation
in `80030e7c`. The converter had applied zero-initialized envelope overrides.
Early drum regions now inherit the articulation envelope; tables which actually
contain ADSR overrides retain that behavior. In this song the correction restores
release rate 5 instead of 0 on the drum regions.

## Volume check

The native mixer at `8002ed34` multiplies expression and channel volume linearly.
`A8` (`80031cb0`) stores its signed operand shifted left by 23 and cancels the
expression fade. Track 13 explicitly requests expression 39 followed by 9 in
its repeating phrase (`1c097e`, `1c0981`). The converter's linear gain is
consistent with that path. MIDI quantization slightly reduces this contrast
(about 0.19 dB). No volume-curve change was made.

## Remaining limits

- SoundFont ADSR shapes approximate SPU envelopes. Sequence ADSR changes request
  active-voice updates, but generated instrument variants only reproduce future
  attacks. The song's relevant B1 commands precede their attacks.
- Dynamic ADSR requires a bound sample pool and currently covers FF7's direct
  melodic articulation path, not every drum or overlay voice case.
- Smooth pitch slides retain the existing semitone interpolation approximation;
  native FF7 interpolates a fixed-point SPU pitch register. The song's immediate
  slurs do not depend on that approximation.
- Vibrato commands remain unimplemented. Expression fade cancellation and
  fractional accumulator behavior need a separate audit (`80031ce0`, `8002e478`).
- `CB` resets several effects in the driver (`80033708`) and remains unimplemented.
  It is also a connection boundary in native lookahead.
- Reverb commands remain source annotations; the exported bank's baseline reverb
  send does not reproduce native SPU reverb. Overlay voices also remain incomplete.
- The other available Ghidra program was SaGa Frontier 1 (`SCUS_942.30`), not
  SaGa Frontier 2. The FF7 conclusions above use the FF7 executable directly.

## Validation

Regression fixtures cover ADSR composition and reset, prepared sample defaults,
legacy drum envelopes, the song's slurred phrase in export and preview rendering,
same-pitch slurs, rests, ties, legato, repeated CC commands, repeats, and DA/DB.
Fixtures are synthetic and contain no executable or sample data from the game.

The Akao, value-core, shell, and shell-CLI suites pass. The actual song exports
successfully without diagnostics. MIDI attacks, pitch bends, preset selections,
and SoundFont envelope generators were inspected before and after. This was not
an audio A/B against an emulated original. The headless targets were rebuilt;
the GUI application was not rebuilt as part of this audit.

Relevant FF7 functions were created/renamed and annotated in Ghidra, with the
program saved after each group of changes.
