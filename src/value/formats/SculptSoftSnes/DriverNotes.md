# Sculptured Software SNES driver

This format supports the early revision used by **Super Star Wars**, the standard
revision used by **Bugs Bunny Rabbit Rampage**, and the extended revision used by
**NHL Stanley Cup** and **Rocko's Modern Life**.
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
* Early detection also verifies the track, pattern-list, and note dispatch
  tables against their linked handlers. Its song entries index table +$18.
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

## Early sequence revision

Addresses in this section refer to Super Star Wars. Its pointer directory is
$17e0; Super Strike Eagle relocates it to $18e0. Patch, curve, echo and sample
formats are shared with the standard revision. The additional tables are:

| Directory offset | Contents |
| --- | --- |
| +$10 | Note patterns: initial pitch LE16, then note commands |
| +$12 | Random pitch groups: eight pitch words |
| +$14 | Random rhythm patterns: pitch-group index, duration bytes, zero terminator |
| +$16 | Pattern lists |
| +$18 | Track control streams |
| +$1a | Song headers: track count followed by track indices |

Each track has three persistent instruction pointers. Track control calls a
pattern list; that list calls note patterns. Pattern end returns to its list,
and list end returns to track control. Calls do not save or restore musical
state. Track and pattern transpose are independent absolute settings and are
added to the note pitch. Restart returns to the original track pointer without
resetting pitch, transposition, patch selection or volume.

Track control dispatch ($1687):

| Opcode | Operands | Meaning |
| --- | --- | --- |
| $00 | — | End track |
| $02 | — | Restart track |
| $04 | divisor | Physical frames per sequence tick, local to this track |
| $06 | pitch LE16 | Track transpose, in twentieths of a semitone |
| $08 | index | Call pattern list from table +$16 |
| $0a | voice; mask if voice=$ff | Fixed DSP voice, or automatic allocation from a mask |

Pattern-list dispatch ($1597):

| Opcode | Operands | Meaning |
| --- | --- | --- |
| $00, $16 | — | End list |
| $02 | index | Play note pattern from table +$10 |
| $04 | index | Play random rhythm pattern from table +$14 |
| $06 | pitch LE16 | Pattern transpose |
| $08, $0a | patch | Primary / secondary patch |
| $0c | — | Enable pitched notes |
| $0e | pitch LE16 | Set fixed pitch and disable note deltas, fine pitch and transposition |
| $10, $12 | volume | Signed addition / replacement of base volume |
| $14 | index, repeated $14/index pairs, $16 | Randomly choose a normal pattern from up to 16 candidates |

Note dispatch ($1363) shares the standard delta and fine-pitch encoding below
$f0. Bit 5 requests an attack; bit 6 selects the secondary patch. Loading a
pattern always reads its initial pitch word, even in fixed-pitch mode. A delta
note clears fine pitch in pitched mode; the initial pitch word alone does not.
The five control opcodes are $f0 (return to list), $f1 (signed addition to note
volume), $f2 (replace note volume), $f3 (silence and wait), and $f4 (wait).
All except $f0 consume one operand byte. Signed volume addition saturates at
0–127. Attack volume is the byte sum of base and note volumes, clamped to 127
when its sign bit is set. Changing either volume does not alter a sounding note
until its next attack.

The frame clock normally remains 20 ms. There is no global music-tempo
accumulator: duration and divisor are separate byte counters. Their product
determines the wait; each zero counter wraps to 256. A zero-duration note only
updates pitch and continues immediately. Envelope gates use the ordinary
16-bit product of the encoded duration and divisor, so a zero divisor produces
a zero envelope gate despite a long sequence wait. Curves and DSP GAIN advance
every physical frame independently of either counter.

Independent fixed assignments are supported, including an allocation mask
containing a single voice. Sharing a DSP voice between tracks is diagnosed.
Moving a track between voices or allocating from multiple voices
can leave overlapping voices with independent envelopes; those commands stop
the affected track with a diagnostic. Random pattern commands also stop their
track with a diagnostic. Their generator is advanced by the host polling loop,
so the selected path is not determined by the sequence bytes alone. Other
tracks continue. These limitations affect several logo/special cues and some ordinary music;
the compatibility table below distinguishes them.

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
Standard/extended music assigns logical track N to DSP voice N. Early music
selects its voice explicitly; export retains independent tracks for fixed
assignments. Sound-effect priority and voice stealing are outside the sequence
export.

DSP ADSR1 is initialized to zero for all voices ($0f2a); this driver does not
have dynamic ADSR commands. Instead, $0a37 compares ENVX to a software target
and writes a linear GAIN mode/rate selected from tables $07a6/$07bc. On attack,
the initial GAIN value is written directly. Software curves are therefore not
ordinary ADSR envelopes. They can encode vibrato, tremolo, pan motion, arbitrary
attack/release shapes and sample changes. These lanes share $0f65 (start) and
$0fed (update).

Curve header: loop start, loop end, release lead, speed, interpolation flag,
point count, point stride, alternating-pan flag. Stride 1 reads a byte; other
strides read a little-endian word. Gain, pan and sample output use its low byte.
Pitch curves normally use stride 2, and the other lanes normally use stride 1.
Some Alfred Chicken and Outlander pan curves use larger strides; the player
follows those strides and retains the full word during interpolation.
Initial value is point zero; countdown starts
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

The existing revisions retain their 222 recognized sequences and sound banks,
including the 18 expected pitch-modulation warnings.
Partial archive counts do not establish support for the unrecognized snapshots
or different Sculptured driver revisions. The counts describe parsing and export
coverage, not waveform equivalence with the original DSP output.

Early revision archive coverage:

| Game | Recognized music snapshots / SPCs | Result |
| --- | ---: | --- |
| Andre Agassi Tennis | 2 / 2 | Pass |
| Alfred Chicken | 10 / 10 | Sequence passes; 5 cues warn about MIDI/SF2 phase changes |
| Captain Novolin | 13 / 13 | Pass |
| Clue (2) | 17 / 17 | Pass |
| Road Runner's Death Valley Rally | 9 / 10 | 8 clean; Blueprint Boogie changes voice assignment |
| Faceball 2000 | 11 / 11 | Pass |
| Jack Nicklaus Golf | 9 / 9 | Pass |
| Out to Lunch | 22 / 22 | 20 clean; 2 cues share voices |
| M.A.C.S. Basic Rifle Simulator | 1 / 1 | Pass |
| Daffy Duck The Marvin Missions | 10 / 11 | Recognized sequences pass |
| Mario is Missing | 14 / 15 | Recognized sequences pass |
| Monopoly | 24 / 28 | Recognized sequences pass |
| Mario's Time Machine | 16 / 17 | Recognized sequences pass |
| NCAA Basketball | 31 / 33 | 24 clean; 1 allocation and 6 shared-voice warnings |
| Outlander | 4 / 4 | Sequence passes; Title warns about MIDI/SF2 phase changes |
| Pink Goes to Hollywood | 17 / 18 | Recognized sequences pass |
| Pro Quarterback | 4 / 4 | Pass |
| Roger Clemens' MVP Baseball | 10 / 11 | Recognized sequences pass |
| The Simpsons Bart's Nightmare | 15 / 33 | 13 clean; 2 bloodstream jingles share voices |
| Super Conflict The Mideast | 7 / 10 | Recognized sequences pass |
| Spellcraft | 10 / 10 | Pass |
| Super Strike Eagle | 5 / 5 | 3 clean; 2 randomized-pattern diagnostics |
| Super Star Wars | 21 / 22 | 20 clean; one LucasArts jingle allocates voices |
| Total Carnage | 7 / 26 | Recognized sequences pass |
| Tecmo Super NBA Basketball | 11 / 11 | Pass |
| Wing Commander | 42 / 42 | Pass |
| Wizard of Oz | 14 / 14 | Pass |
| WWF Super Wrestlemania | 11 / 11 | 3 clean; 3 allocation and 5 pitch-modulation warnings |

The early revision adds 367 recognized sequences from 28 games, all with sound
banks. Across all three revisions this is 589 recognized sequences from 46
games. Recognition counts include partial sequences with the explicit
limitations above; they do not imply complete support for every cue.

The early corpus reports six sequences using unsupported voice allocation,
ten sharing a DSP voice between tracks, five using DSP pitch modulation, and
two using randomized patterns. Six other
sequences preserve signed pan changes in the performance model but warn that
MIDI/SF2 stereo instrument variants only apply those changes at future attacks.
The later Mortal Kombat II / Return of the Jedi family still needs its own
interpreter audit and is deliberately excluded by detection.

All 28 early-driver pitch tables match the same rounded equal-tempered octave,
and their DSP GAIN distance/rate tables match the shared voice implementation.
