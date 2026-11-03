# Sculptured Software SNES driver

The driver family shares six-byte patches, software envelopes, sample prefixes,
and echo presets across four sequence revisions:

* **Early**, used by **Super Star Wars**: separate track-control streams, pattern
  lists, and note patterns, with a local tick divisor for each track.
* **Standard**, used by **Bugs Bunny Rabbit Rampage**: a single command stream per
  track with nested phrases and a global tempo accumulator.
* **Extended**, used by **NHL Stanley Cup** and **Rocko's Modern Life**: the
  standard sequence encoding with gate overrides, legato attacks, and a wider
  pitch range.
* **Later**, used by **Mortal Kombat II**, **Return of the Jedi**, and
  **Secret of Evermore**: packed semitone notes, separate music and envelope
  timers, key-off-controlled envelope release, and priority-based voice allocation.

Addresses below refer to Bugs Bunny Rabbit Rampage unless stated otherwise;
related revisions may relocate code and tables.

## Memory layout and tables

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
* Early song entries index table +$18 rather than +$10.
* The live song-header pointer identifies the initialized music sequence.
  Sound effects use separate control streams.

## Sequence and clock

$070c sets timer 0 to $20 (4 ms); $0744 consumes five overflows per update
(20 ms). $0856 updates the music tempo accumulator and calls $141c on carry;
a zero increment means every frame. Envelopes and mixing run every frame,
independently of the music tempo. Initial accumulator $0201 is $ff.
Tracks execute in descending channel order. Initial pitch is $021c, volume zero,
divisor one, no phrase transposition. Tempo changes affect pending waits on
every channel without changing the physical envelope clock.

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

Tracks can select a fixed DSP voice or allocate from a voice mask. Moving a
track between voices or allocating from multiple voices can leave overlapping
voices with independent envelopes. The random-pattern generator is advanced by
the host polling loop, so the selected path is not determined by the sequence
bytes alone.

## Later sequence revision

Addresses in this section refer to Mortal Kombat II. The pointer directory is
$1ee0 and the BRR directory is $1f00. The six-byte patches, sample prefixes, and
four envelope tables retain the earlier organization.

Song headers contain a count of up to twenty logical tracks. One variant stores
little-endian track pointers immediately after the count; another stores byte
indexes into table +$10. The track initializer distinguishes these layouts.
The command table at $1c6b contains thirty-two entries for $e0–$ff.

Host command $06 starts a song indexed through table +$1a. Commands wait in a
ring before the music header is initialized. In Mortal Kombat II, DP $e9 holds
the pending count, DP $eb the read index, and $1065/$1075/$1085 hold parallel
arrays for commands and their two arguments. The first argument is the song
index; bit zero of the command is ignored. Related revisions have 16, 20, 24,
or 32 slots, and some mirror dequeued commands into the communication ports.
Until a pending song request is processed, the live song-header pointer may
still be zero or refer to the previous song.

Timer 0 advances envelopes every 20 ms; timer 1 supplies music pulses at the
same base interval. Tempo is an eight-bit accumulator increment: zero admits
every pulse, and other values admit carries. Envelopes continue during skipped
music pulses. Timer 2 services DSP register fades.

| Opcode | Operands | Meaning |
| --- | --- | --- |
| $00–$7f | — | Add `(opcode >> 3) - 8` semitones; duration `(opcode & 7) + 1` |
| $80–$bf | duration | Absolute note `opcode - $60` |
| $c0–$df | — | Key off, then rest `opcode - $bf` pulses |
| $e0 | word | Add a raw DSP pitch offset after pitch conversion |
| $e1/$e2/$e3/$e4 | byte | GAIN/pan/sample/pitch envelope speed multiplier |
| $e5 | mode, voice, mask | Set voice allocation parameters |
| $e6 | — | Restore the logical track's default fixed voice |
| $e7 | — | Reset track parameters; a no-op in Mortal Kombat II |
| $e8/$ee | — | Unimplemented dispatch entries |
| $e9 | mask | Restrict sound-effect voice allocation; no music event |
| $ea | — | Key off without waiting |
| $eb/$ec | fine stream | Enter fine-pitch mode; $ec first keys off |
| $ed | byte | Attack volume multiplier, divided by 256 |
| $ef | note, duration | Absolute note |
| $f0 | — | End track |
| $f1 | word | Scale stored volume by an 8.8 multiplier |
| $f2 | byte | Set stored volume |
| $f3 | byte | Set allocation priority |
| $f4 | — | Suppress the next note's patch attack |
| $f5 | byte | Select patch, including phrase substitutions |
| $f6 | descriptor | Bounded phrase call, using the earlier descriptor layout |
| $f7 | word | Set track pitch offset; see revision distinction below |
| $f8/$fd/$fe/$ff | — | Wait 32/64/128/256 pulses without key-off |
| $f9 | — | Restart track and reset its parameters |
| $fa | byte | Set global tempo accumulator increment |
| $fb | signed byte | Pan offset, latched on the next attack |
| $fc | — | Legato patch attack on the previously allocated voice |

Explicit zero note durations mean 256 pulses. Packed notes clear the fine-pitch
accumulator. Fine mode interprets a nonzero high nibble as a signed delta of
`high nibble - 8`, in twentieths of a semitone; its low nibble supplies the wait.
A zero wait applies the delta immediately. Within that stream, $00 returns to
normal notes, $01 reads a wait byte, $02 keys off, and $03–$0f read a signed delta
byte while reusing the current wait counter. Phrase boundaries are checked by
the normal interpreter, not while consuming the fine stream.

Pitch starts at `20 * note - 900`, then adds fine pitch and phrase transposition.
The extended ten-octave converter, patch pitch curve, and sample tuning retain
five-cent musical units. The $e0 offset is applied afterward in DSP register
units, so it can produce a pitch-dependent detune. In most later variants, $f7
writes a separate offset whose low result is discarded: attacks ignore it and
ties retain only its effect on the high byte. The Jungle Book and Super Copa
instead write the phrase-transposition field and apply its full value.

The later envelope player ignores header byte 6: pitch points are words, and
GAIN, pan, and sample points are bytes. Byte values interpolate internally with
six fractional bits before truncation. A lane's speed override multiplies the
header interval and divides by 32; the integer interval is clamped to 1–255 and
the remainder adjusts an eight-bit phase accumulator. Zero selects the header
interval. Even the default phase step is $ff rather than $100. Music curves
sustain their loop until key-off, then enter the release tail at the next point
boundary; they do not derive their hold time from the next note's duration.
A track end keys off and plays any finite GAIN release tail. These curves cover
vibrato, tremolo, pan motion, and sample switching. The DSP continues to use GAIN
rather than hardware ADSR.

Tracks start with note zero, volume 127, multiplier 255, priority 64, and fixed
voice equal to their logical index. Attacks latch pan and volume settings. Pan
is clamped to 0–100 after applying its signed offset. The initial master level
is 127/128; the two default GAIN multipliers each use 255/256. Echo presets
update shared global DSP state.

The driver orders tracks by priority and allocates eight DSP voices. Allocation
mode bit 3 selects a fixed voice, bit 4 disables allocation, bit 1 requires an
idle voice, and bit 0 requires a strictly lower priority when stealing. Masked
searches can rotate and leave several voices sounding for one logical track.

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
selects its voice explicitly.

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
The terminal-index comparisons at $1014–$1098 allow playback beyond the nominal
point count.

Pan is a percentage of the voice volume assigned to the left output; right is
volume minus that integer quotient. Alternating pan switches sides on successive
attacks. The sample's tuning is added before pitch conversion. $11b1 uses split
240-byte pitch tables, octave shifts, and a special negative-word path.
The Bugs Bunny table starts at 8192 and approaches 16384, rather than starting
at the DSP's unity rate of 4096. Entry `i` is `round(8192 × 2^(i/240))`, a rounded
equal-tempered octave with 240 steps of one twentieth of a semitone. Integer
table entries and octave shifts introduce small rounding errors in the DSP
pitch value.

The extended revision adds $05a0 (six octaves) before the table lookup and
shifts over ten octaves instead of four. This preserves the ordinary pitch
range while accommodating lower signed pitches. Raw DSP pitch patches bypass
both the bias and the table conversion. The DSP pitch register is 14 bits;
$1000 is the unity playback rate.

Echo presets contain EDL, EVOL L/R, feedback, and eight FIR coefficients, in the
DSP register order $080a. The standard and extended global master starts at
75/128. Each patch attack updates its EON bit and may replace the global echo
preset.
