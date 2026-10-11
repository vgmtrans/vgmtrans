# OceanSnes

This module imports the eight music tracks of the Ocean driver used by
Waterworld, Addams Family Values, The Flintstones, and Jurassic Park II.
The first three share a build; JP2 needs a small number of different arithmetic
and scheduling rules. Detection requires independent executable signatures,
the command table, and a bounded song header. It does not identify arbitrary
Ocean-published games or assume that all Ocean drivers share this format.

## Source and playback model

The host's music pointer at direct page `$E8` identifies the 26-byte header.
Import starts at order zero, rather than the independently advancing stream
cursors in the snapshot. Captured host music gain, output mode, and PRNG seed
are retained. SFX allocation, host commands, and their interruption of music
are outside the authored score.

The header points to an order table, instrument pointer table, and sample
pointer table. Orders contain eight stream pointers. A pointer with high byte
`$FF` terminates the order list. Instrument records have a three-byte header
and ordered 23-byte key regions. Sample objects contain a tuning word and a
BRR-relative loop offset, followed by BRR data. Samples are discovered through
regions; the live DSP directory is populated lazily and is not authoritative.

Timer 0 is `$A2`, giving a 20.25 ms base tick. The VM uses two ticks per base
tick so all music streams execute before voice updates. The latter emit at
the original frame boundary. This preserves same-frame global commands,
one-frame delayed key-ons, and the order in which tracks affect shared state.
Only changed pitch and volume values are emitted.

Important executable behavior (addresses below are for the canonical build):

| Routine | Behavior preserved |
| --- | --- |
| `$08CB`, `$09AB` | Duration uses a radix-127 high counter; zero tokens continue parsing. |
| `$09CF`, `$1076` | BF forces fetches until a common continuation clears it; instrument commands force fetch. |
| `$1097` | C7 primes the next stream immediately, even before all tracks reach the barrier. Order identity, rather than shared pattern addresses, defines loops. |
| `$0C75` | First inclusive key-region match; null pointers retain the old header and region; a region miss replaces only the header. |
| `$0FB3` | C1's voice lookup replaces the requested duration with the logical slot (`2 * track`); track zero's ramp remains idle. |
| `$100D`, `$0FF3` | C4 consumes a byte without setting pan. C3 updates the volume cache without marking it dirty. |
| `$1204` | C5 ignores its operand and reads the shared 16-byte bend pattern; its track-side store uses the pattern index. |
| `$1240`, `$128D` | CD retriggers with note minus one; CF and E9 glide without retriggering. Fractional pitch survives interrupted slides. Completion snaps to the target. |
| `$0DB1`, `$1697` | Literal pitch table, byte-wrapped transposition, delayed vibrato, signed bend, and final 14-bit DSP pitch. |
| `$1798` | Global LFO updates every other base tick, with byte-wrapped accumulators and destination-specific dirty behavior. |
| `$152C` | Integer truncation at every volume stage, signed stereo output, random pan, and pan/surround retention. |

JP2 halves C2's signed operand instead of doubling it, multiplies instrument
volume instead of treating it as a cap, reverses the D9 swing ordering, omits
the global per-row slide clear, and omits portamento's negative-index clamp.
These distinctions are selected by code signatures, not filenames.

## Export boundaries

Pitch slides and modulation use the driver's integer per-frame values and
export as pitch bends. Zero DSP pitch freezes a sample; that interval is
muted in exported playback because MIDI cannot express zero frequency. GAIN envelopes use the shared SNES envelope conversion;
asynchronous ENVX polling and DSP counter phase are approximated. Sustain
countdowns are reflected in note gates. Threshold zero never enters sustain.

Both builds leave DSP FLG at `$20`, disabling echo writes. Echo and FIR commands
remain annotated source data; the importer does not substitute generic reverb
for reads of the captured echo buffer. Hardware pitch modulation is annotated
and diagnosed because standard MIDI/sample banks cannot reproduce it.
Signed stereo is retained in the performance. The common exporter diagnoses
phase/pan changes it cannot apply to an already sounding stereo bank variant.

Two supplied songs contain unusual data:

- AFV `afv-16`: program 4 is past the four-entry instrument table. The driver
  reads `$FE01` and unrelated RAM. Its note is suppressed with a diagnostic.
- Flintstones `flint-b01`: sample 1's END+LOOP block jumps beyond its first BRR
  segment into another segment overlapping the next sample. The first segment
  is exported as a one-shot with a diagnostic; arbitrary BRR jump chains are
  not represented by the shared sample codec.

Reserved C9 clears driver RAM and is stopped with a diagnostic. SFX-only D2/D3
also stop a music stream. Invalid structures, truncated operands, and runaway
command streams are bounded by the normal decode and VM limits.

## Verification

The implementation was checked against the supplied raw/annotated SPC700
listings and execution of the original routines, not just the prior prose
analysis. The local corpus contains 69 matching SPC files (65 songs plus four
reference duplicates) and 119 negative examples. All matching files render
and export MIDI, SF2, DLS, and WAV through the shell. Full-song parser traces
contain 105,824 note triggers: all match the imported note timings and keys,
except the single deliberately suppressed AFV instrument described above.
Another 333,958 voice/frame comparisons checked pitch and stereo-volume
registers over the first 1,000 base ticks of each file (or its shorter length),
with no unexplained differences. These omit note reinitialization frames
before delayed KON and account for the zero-pitch muting convention above.
The opcode runner checks parsing and register arithmetic, not cycle-accurate
DSP audio or ENVX polling.

The checked-in test executable needs no game data. It covers both revisions,
barriers and reused orders, continuation/duration rules, source associations,
retained and overlapping regions, portamento arithmetic, volume behavior,
sample discovery, malformed operands, and deterministic mutated streams.
An optional corpus argument also runs structural validation and verifies
positive/negative detection. A second optional directory writes note and
controller CSV traces for external comparisons:

```sh
cmake --build build/core-migration-headless --target vgmtrans-ocean-snes-tests
build/core-migration-headless/tests/vgmtrans-ocean-snes-tests
build/core-migration-headless/tests/vgmtrans-ocean-snes-tests /path/to/ocean_snes_driver_analysis /tmp/ocean-traces
```

No game data or extracted driver code is included in the repository.
