# Winkysoft SNES driver

This module reads the resident music sequence from a 64 KiB ARAM image. The SPC
and RSN extractors provide those images. Code signatures recover relocated
tables; the scanner does not depend on game titles or fixed program addresses.

## Sources and compared builds

The starting reference is loveemu's annotated
[Dai-4-ji Super Robot Taisen disassembly](https://github.com/loveemu/vgm-disasm/blob/master/snes/Winkysoft/Dai-4-ji%20Super%20Robot%20Taisen.s).
The [build sorting notes](https://github.com/loveemu/vgm-disasm/blob/master/snes/Winkysoft/Winkysoft%20Build%20Sorting%20Notes.txt)
identify additional revisions. Representative SPCs from each of the following
sets were stripped to ARAM and disassembled with `spcdas raw`, following the
[spcdas guide](https://zenn.dev/loveemu/scraps/3326335e91642a).

| Game | Program entry | Profile | SPCs checked |
| --- | --- | --- | ---: |
| Kouryuu Densetsu Villgust | $f600 | Early | 15 |
| Hero Senki: Project Olympus | $f600 | Early | 16 |
| Mazinger Z | $f600 | Early | 22 |
| Jojo no Kimyou na Bouken | $f600 | Jojo | 19 |
| Dai-3-ji Super Robot Taisen | $f500 | Standard, six-byte song descriptors | 33 |
| Macross: Scrambled Valkyrie | $f300 | Standard | 29 |
| Super Robot Taisen EX | $f400 | Standard | 32 |
| Ghost Chaser Densei | $f300 | Standard | 15 |
| Dai-4-ji Super Robot Taisen | $f180 | Extended | 34 |
| Masoukishin: The Lord of Elemental | $f180 | Extended, different loop reset | 43 |

The Mazinger putative beta and Battle Robot Retsuden are listed in the source
notes but were not present in the tested archive. Their compatibility is not
established by this corpus.

All 258 SPCs were scanned, rendered, and exported as MIDI, SF2, and DLS.
Separate DSP-write traces using Game_Music_Emu exercised the actual Villgust
and SRW4 driver code. The Villgust title sequence supplied 470 matching attack
pitches and attack intervals at the 10 ms frame resolution, including its
tempo ramps. SRW4 supplied another 1,837 matching attack pitches across all
eight music voices, with attack times agreeing within one frame after removing
the startup offset. These checks included low-key tuning and tied passages. Small injected
sequences established the regression-test pitch words and tempo-ramp timings.
The emulator is a research tool, not a build or test dependency. No game data
is included in the tests.

Unless qualified below, addresses refer to the SRW4 disassembly.

## Detection and resident song

The dispatcher at $f370 maps opcodes through a 128-byte slot table, then jumps
through a little-endian handler table. Notes map to slot $02. The common tail
of the map and the instrument/song-pointer initializer identify the family.
The pitch lookup and DSP initialization loops provide independent table checks.
The eight-byte patch table is initialized through DP $0a, and the song table
through DP $0c. DSP DIR comes from the initialization register/value arrays.

Early builds and SRW3 use six-byte descriptors: priority, tempo, volume, logical
channel, absolute stream address. Priority zero denotes music. Later loaders
start channel zero at a fixed uploaded sequence address and select tempo/volume
from a two-byte table. SRW4's loader is $fa74, with music at $5200 and headers
at $0800; Masoukishin uses $3c00. Most preceding builds use music at $0600.

The scanner first checks the selected song index against the live base tempo
and volume, then searches the resident descriptor table if that index is stale.
An uninitialized live tempo permits the loader's selected default; this occurs
in the Macross startup snapshot. The module publishes the resident music,
not every descriptor pointing into the same uploaded sequence buffer.

## Clock, tracks, and control flow

Timer 0 normally uses target $50: one update per 10 ms. Late drivers can change
the timer target through a RAM variable. One value-core tick represents a
hardware update; a fixed tempo event expresses its real duration. This keeps
vibrato, slides, and pan sequences on their original clock during tempo ramps.

Encoded lengths and gate lengths are multiplied by 125. Every frame subtracts
the current tempo byte from a 16-bit duration accumulator. Commands run while
the remainder is less than one tempo step. Fractional remainders survive waits
and propagate to newly started tracks. A zero packed/controller length changes
state without waiting. Initial note length is $8000, and initial gate is zero.

Tempo scale $80 is unity. The effective step is the high byte of twice the
base-tempo/scale product. A nonzero ramp operand changes scale by **operand + 1**:
the preceding comparison supplies carry/borrow to ADC/SBC. Late drivers update
the ramp each hardware frame. Early drivers update it only when channel zero
is ready to decode, after copying that channel's old tempo for the frame.

There are ten logical channels, visited in ascending order. `$6e` starts or
replaces another channel at an absolute address. A higher channel runs in the
same frame; an already-visited channel runs in the following frame. Dormant VM
streams and `VmApi::startStream` implement this without retaining source bytes
or expanding the song into precomputed performance events.

Repeats use an eight-entry stack. Count one means one total pass; zero is an
infinite repeat subject to the VM export loop policy. A count smaller than the
current iteration takes the infinite-loop arm as well. Older revisions clear
the fractional duration remainder and restore base tempo there; Masoukishin
omits that reset. Calls have **one return slot**, independent of the repeat
stack. A nested call overwrites it rather than creating a second call frame.

## Sequence encoding

Operands below exclude the opcode. Words are little-endian unless noted.

| Opcode | Operands | Meaning |
| --- | --- | --- |
| $00–$66 | note lookahead | Melodic key or percussion patch index |
| $67/$68, before Extended | note lookahead | Additional note keys |
| $67, Extended | address LE16 | Pan sequence |
| $68, Extended | index | Pan sequence preset / special control |
| $69, Standard/Extended | register, value | DSP write |
| $6a, Jojo and later | step | Vibrato phase increment |
| $6b | noise clock | Clock $00–$1f starts a noise note; larger values clear NON |
| $6c, Early/Jojo | value | Write a CPU communication port |
| $6c, Standard/Extended | signed step | Pitch-word drift per frame |
| $6d, Early/Jojo | condition, address LE16 | Branch when captured CPU condition differs |
| $6d, Standard/Extended | voice mask, feedback, left, right | Echo |
| $6e | logical channel, address LE16 | Start/restart track |
| $6f | — | Toggle percussion |
| $70 | value, length; possible continuation bytes | Pitch multiplier, unity $40 |
| $71 | value, length; possible continuation bytes | Vibrato depth |
| $72 | value, length; possible continuation bytes | Scale patch volume by `2 * (value & 127) / 256` |
| $73 | position | Pan, unless a pan sequence owns it |
| $74 | — | Begin repeat |
| $75 | count | End repeat |
| $76 | address LE16 | Call; unimplemented in the Early handler table |
| $77 | — | Return through the saved slot |
| $78 | — | End track |
| $79 | target scale, ramp step | Tempo scale/ramp |
| $7a | transpose | Replace the patch transpose |
| $7b | patch | Select instrument |
| $7c | length | Rest; clear gate high bytes and wait |
| $7d | velocity | Velocity only |
| $7e | gate | Gate only |
| $7f | length | Set length and wait |
| $80–$ff | gate, length | Velocity from opcode low seven bits, gate, and optional wait |

Unused early `$69` and `$6a` slots consume one operand. They must not be
decoded with the later command lengths.

A note waits using the previous note length only if the next byte is below
`$7f`. It absorbs an immediately following `$7d` or `$7e` and its operand.
Otherwise the following packed event or `$7f` supplies its delay. The key-on
decision precedes these inline gate changes: previous gate < previous length
restarts the envelope; equal/longer gates tie or slur the voice.

For `$70–$72`, the first value always has a length. A value with bit 7 set
enters an implicit controller stream: subsequent high-bit bytes are controller
values, not packed notes, and reuse the previous length. The final low-bit
value reads another length and returns to opcode decoding.

Pan sequences contain `$f0–$ff` positions (low nibble times eight) interspersed
with frame counts below `$f0`. Zero disables the sequence. High preset indices
select special actions through `index & 6`: preset zero, an index from the
current pan pointer, the channel-8/channel-9 pointer linkage, or disable.

## Pitch, instruments, and mixing

Each patch contains SRCN, ADSR1, ADSR2, a **big-endian** pitch word, volume, pan,
and transpose. Melodic selection copies the pitch word as unsigned 8.8 tuning;
percussion selection uses the same word as the absolute DSP pitch and reloads
patch volume on every note. Instrument selection resets vibrato depth, pitch
drift, and the pitch multiplier. BRR data, loop points, and ADSR use the common
SNES sample and envelope helpers.

The semitone lookup at $fc39 has thirteen words, $085f through $10be. The driver
adds a fractional correction above key 51 and below key 19, interpolates using
the **low byte** of the table-word difference, shifts by octave, and applies
patch tuning. The low-key correction is signed 8.8: its borrow decrements the
semitone before lookup. Eight-bit key doubling and sixteen-bit truncation are
preserved. Notes export fractional keys relative to the sample's unity key.

The early pitch writer doubles the truncated low-byte product, losing its
carry. Its vibrato overwrites the pitch multiplier with a positive triangle
offset above $80. Later builds keep that carry, center vibrato around zero,
and add vibrato only to the **low-byte** pitch product. Their ascending phase
also inherits carry from centering the sample. These quirks are intentional;
replacing them with a generic MIDI vibrato changes the driver's sound.

Mixing performs two truncating products: velocity × channel volume, then song
volume. Early stereo uses `(gain >> 1) + (gain * pan >> 8)` on the left and
`gain - (gain * pan >> 8)` on the right. Later stereo uses two resident 16-entry
tables indexed by `pan >> 3`. Mono uses the center position. Signed DSP output
gains are retained in stereo-balance events. Noise adapters use the common
SNES LFSR codec at a fixed key and retain the selected patch's ADSR.

## Export boundaries

The music interpreter preserves source annotations for every command, including
CPU communication and DSP writes. CPU replies are not generated; conditional
branches use the condition captured in the SPC. Host fades, pauses, subsequent
song uploads, and host-controlled timer changes cannot be reconstructed from
the resident music alone.

Channels 8 and 9 are sound-effect overlays on hardware voices 6 and 7. They
have independent logical output tracks here; priority arbitration and restoring
a displaced music voice are not modeled. Noise samples approximate the DSP's
shared free-running LFSR with looping PCM, and changing NON during a held note
does not replace that note's already-selected sample. Arbitrary DSP writes to
voice registers, pitch modulation, and custom echo FIR taps are annotated but
do not all have portable SF2/DLS/MIDI equivalents. Echo mask, delay, feedback,
and stereo return levels are represented in the performance model.

Malformed operands stop with diagnostics. Discovery is bounded, validates
pointer reads, and rejects conflicting implicit-controller decoding at one
address. The parser visits loop bodies in their resulting percussion state
to collect instruments needed by later iterations. Synthetic tests cover
relocation, truncation, timing, loops/calls, launches/restarts, pitch arithmetic,
pan, noise, source-independent rendering, and all three export types.
