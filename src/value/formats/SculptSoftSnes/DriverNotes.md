# Sculptured Software SNES driver

This format supports the standard revision used by **Bugs Bunny Rabbit Rampage**
and the extended revision used by **NHL Stanley Cup** and **Rocko's Modern Life**.
Addresses below refer to the standard revision unless stated otherwise; related
revisions may relocate code and tables.

## Detection and tables

* $0e8e: indexed table lookup. The directory of pointers is $19e0.
* $1850: lookup table +$1a, then store the selected song header in DP $6d/$6e.
  Header: track count, then one byte per track indexing table +$10.
* $0715: sample-directory address $1a00. It is also sent to DSP DIR at $0f24.
* Pointer directory: +$00 echo RAM, +$02 instruments, +$04 GAIN curves,
  +$06 pitch curves, +$08 pan curves, +$0a sample curves, +$0c echo presets,
  +$0e sound effects, +$10 tracks, +$1a songs.
* Entries are absolute little-endian addresses. $ffff denotes an unloaded entry.
* $1187: each BRR stream has a three-byte prefix: signed pitch adjustment word
  (twentieths of a semitone), then sample flags. SRCN selects a normal DSP DIR
  entry; the prefix is **not** BRR data.
* Detection also checks the phrase-handler and pitch/delta table relationships;
  the extended revision additionally requires its pitch-bias and $fb/$fc handlers.
  Direct-page and absolute table addresses may move.
* The live song-header pointer identifies the current sequence. Sound-effect
  cues without an active song header are not treated as music sequences.

## Sequence and clock

$070c sets timer 0 to $20 (4 ms); $0744 consumes five overflows per update
(20 ms). $0856 updates the music tempo accumulator and calls $141c on carry;
a zero increment means every frame. Envelopes and mixing run every frame,
independently of the music tempo. Initial accumulator $0201 is $ff.
Tracks execute in descending channel order. Initial pitch is $021c, volume zero,
divisor one, no phrase transposition. The frame clock is used as the performance
timebase so tempo changes affect pending waits on every channel without
retiming the physical envelopes.

| Opcode | Operands | Meaning / handler |
| --- | --- | --- |
| $00–$7f | duration | Add signed delta table entry indexed by low five bits; reset fine pitch. Bit 5 retriggers. $14db |
| $80–$ef | duration | Fine-pitch accumulator ±2 × (low three bits + 1); bit 3 chooses positive. Bit 5 retriggers. $14aa |
| $f0 | — | End this track. $15b0 |
| $f1 | multiplier LE16 | Rounded 8.8 volume multiplication, saturating at 127. $15bb/$15de |
| $f2 | volume | Set volume for the next attack. $15ff |
| $f3 | duration | Silence current voice and wait. $160a |
| $f4 | duration | Wait without silencing. $1626 |
| $f5 | patch | Select patch for next attack; phrase replacement list may override. $1638 |
| $f6 | descriptor | Bounded, nested phrase call. $167b |
| $f7 | pitch LE16, duration | Absolute pitch and attack. $1753 |
| $f8 | pitch LE16, duration | Absolute pitch without attack. $1753 |
| $f9 | — | Restart original track; reset pitch and volume. $176a |
| $fa | tempo, gate LE16 | Set global tempo increment and 8.8 envelope-gate multiplier. $178f |
| $fb (extended) | duration bytes | Override the next note's envelope gate; $ff continues the sum. |
| $fc (extended) | — | Update the next attack without keying on or resetting sample, pan, and echo state. |

A zero note duration updates pitch state but does not start a voice or wait.
A zero $f3/$f4 duration wraps the byte countdown and waits 256 music ticks.
$f7/$f8 attack selection comes from bit 1 of the doubled dispatch index, not
bit 5 of the opcode.

The extended revision's $fb command sums bytes into a wrapping 16-bit gate
duration, reading until a byte below $ff. This changes the next nonzero pitched
event's envelope gate, not its sequence wait. A pitch-only event also consumes
the override. The gate multiplier scales the low and high bytes separately,
rounding each product before recombining them; for example, duration 300 with a
1.5 multiplier becomes 578, not 450. The standard revision scales only the low
duration byte.

$fc makes the next nonzero pitched event legato. An attack still loads patch
flags, volume, and pitch and restarts its GAIN and pitch curves, but preserves
the current DSP envelope, sample, pan-curve position, alternating-pan phase,
and echo state. A pitch-only event consumes the flag without reloading a patch.
Both pending modifiers are global; zero-duration events, rests, and waits leave
them available to the next qualifying event, including one on another channel.

Phrase descriptor (following $f6): start LE16, exclusive end LE16, repeat count,
transpose LE16, volume multiplier LE16, replacement count, replacement bytes.
The driver permits five nesting levels. There is no return opcode: $1463 checks
whether the current PC equals the innermost phrase's end. Count zero repeats
forever. The caller's volume is restored and transpose subtracted on exit;
pitch and selected instrument persist. Each $f5 cycles through the current
phrase's replacement list; $ff preserves the encoded patch. Repeating a phrase
does not reapply its initial transpose/volume scale or reset replacement index.

## Instruments and physical envelopes

$0d2b loads a six-byte patch: flags, gain, sample, pitch curve, pan, echo preset.
Flags: $01 gain curve, $02 sample curve, $04 pitch curve, $08 pan curve,
$10 echo, $20 DSP pitch modulation, $40 raw DSP pitch, $80 voice allocation
lifetime controlled by sample ENDX. Unset gain/sample/pan curve flags make the field a
literal. A pitch curve is centered on $04b0.

Sample-prefix flags: $80 selects noise (low five bits set the global noise
clock); $40 permits note commands to key on an unchanged SRCN. The latter is
checked by $08b8–$08c4; a changed SRCN also considers the pending attack bit at
$08ff. When $20 is clear, a recorded ENDX also allows a pending attack to
key on an unchanged SRCN even without $40.
Music assigns logical track N to DSP voice N; sound-effect priority and voice
stealing are outside the sequence export.

DSP ADSR1 is initialized to zero for all voices ($0f2a); this driver does not
have dynamic ADSR commands. Instead, $0a37 compares ENVX to a software target
and writes a linear GAIN mode/rate selected from tables $07a6/$07bc. On attack,
the initial GAIN value is written directly. Software curves are therefore not
ordinary ADSR envelopes. They can encode vibrato, tremolo, pan motion, arbitrary
attack/release shapes and sample changes. These lanes share $0f65 (start) and
$0fed (update).

Curve header: loop start, loop end, release lead, speed, interpolation flag,
point count, point width, alternating-pan flag. Points are bytes except pitch,
which uses little-endian words. Initial value is point zero; countdown starts
at speed+1. The main loop advances it once in the same frame as the attack,
before writing the voice registers. The remaining gate is max(0, scaled-duration
− release-lead × speed), using the revision's duration scaling above.
On a point advance, subtract speed. When it reaches
zero, jump to loop-end (not loop-end+1) and continue into the release points.
While it remains positive, passing loop-end jumps to loop-start. A final point
that equals loop-end loops indefinitely; other final points freeze the value.
$ff disables the release jump. Interpolation uses a signed integer delta divided
by speed, followed by a snap to the target when the old countdown is two.
The nominal point count does not bound a release/loop jump. For example, a
Bugs Bunny pitch curve has loop start 40 and end 39 with only 30 nominal points.
The loader retains that reachable tail, and the player
applies the same terminal-index comparisons as $1014–$1098.

Pan is a percentage of the voice volume assigned to the left output; right is
volume minus that integer quotient. Alternating pan switches sides on successive
attacks. The sample's tuning is added before pitch conversion. $11b1 uses split
240-byte pitch tables, octave shifts, and a special negative-word path.
The Bugs Bunny table starts at 8192 and approaches 16384, rather than starting
at the DSP's unity rate of 4096. Entry `i` is `round(8192 × 2^(i/240))`, a rounded
equal-tempered octave. Export uses that first entry to establish the
pitch reference, then converts the sequence's twentieths of a semitone directly
to musical pitch. It preserves sample tuning, pitch curves, and the driver's
octave limits and wrapping, while omitting the table's integer rounding and
discarded shift bits. Semitone changes therefore retain a constant tuning bend
instead of introducing tiny per-note corrections. Bank regions use unity key 72
at 32000 Hz; sample-prefix tuning is already included in the sequence's pitch
and must not be applied twice.

The extended revision adds $05a0 (six octaves) before the table lookup and
shifts over ten octaves instead of four. This preserves the ordinary pitch
range while accommodating lower signed pitches. Raw DSP pitch patches bypass
both the bias and the table conversion; export retains their exact 14-bit
register-to-playback-rate calculation.

MIDI export rounds note numbers, so each attack uses an integral anchor key
and retains the fractional part in its pitch bend. This preserves sample tuning
and subsequent fine-pitch commands without losing up to half a semitone to
note-number rounding. No minimum bend size or change threshold is applied:
authored five-cent steps and smaller raw DSP-pitch changes remain intact.

Echo presets contain EDL, EVOL L/R, feedback, and eight FIR coefficients, in the
DSP register order $080a. The global master starts at 75/128. Each patch attack
updates its EON bit and may replace the global echo preset.

## Scope and export limits

The sequence model preserves physical envelope motion through explicit gain,
pitch and stereo events. MIDI/SF2 cannot emulate the SNES echo/FIR processor or
cross-voice DSP pitch modulation exactly. Echo gain, delay, feedback and channel
mask can be retained in the performance model; custom FIR bytes remain source
data. DSP pitch modulation emits an explicit diagnostic once per affected track.
GAIN is sampled at the software frame rate with continuous DSP rate counters;
this is not a sample-accurate DSP emulator. ENDX-dependent retriggers without
sample flag $40, simultaneous voices sharing different noise clocks, and sound-effect voice
stealing are not reproduced. Ordinary BRR loop points and noise waveforms are
exported through the shared SNES sample support.

## Compatibility

| Game | Recognized music snapshots / SPCs | Result |
| --- | ---: | --- |
| Bugs Bunny Rabbit Rampage | 23 / 24 | All music passes; one snapshot contains speech only |
| Rex Ronan | 5 / 5 | Pass |
| Super Battleship | 5 / 6 | Recognized sequences pass |
| Bronkie | 17 / 17 | Pass |
| Packy and Marlon | 32 / 32 | 14 clean; 18 report DSP pitch modulation |
| Boxing Legends of the Ring | 7 / 7 | Pass |
| Barbie Super Model | 10 / 10 | Pass |
| Cliffhanger | 7 / 7 | Pass |
| WildSnake | 9 / 10 | Recognized sequences pass |
| Spectre | 9 / 9 | Pass |
| WWF Royal Rumble | 14 / 14 | Pass |
| RoboCop vs. the Terminator | 10 / 10 | Pass |
| Mortal Kombat | 34 / 34 | Pass |
| Tony Meola's Sidekicks Soccer | 4 / 8 | Recognized sequences pass |
| Sports Illustrated Championship Football and Baseball | 6 / 6 | Pass |
| Super Star Wars: The Empire Strikes Back | 19 / 20 | Sequences pass; opening logo cue uses the sound-effect system |
| NHL Stanley Cup | 5 / 11 | Sequences pass; six organ cues use the sound-effect system |
| Rocko's Modern Life | 6 / 6 | Pass |

All 222 recognized sequences have sound banks.
Partial archive counts do not establish support for the unrecognized snapshots
or different Sculptured driver revisions. The counts describe parsing and export
coverage, not waveform equivalence with the original DSP output.
