# Sunsoft NinSnes driver audit

The supplied `Hashire Hebereke.s` and `Albert Odyssey 2.s` contain standard
N-SPC music players with four additional voice commands. Addresses below refer
to the supplied disassemblies, not fixed addresses used by the scanner.

| Property | Hashire Hebereke (`SunsoftEarlier`) | Albert Odyssey 2 (`Sunsoft`) |
| --- | --- | --- |
| Driver identification | Embedded `Ver S1.20` at `$1388` | Identified by command handlers and lengths |
| BGM playlist word reader | `$09EF`, cursor `$40` | `$0C3C`, cursor `$40` |
| BGM song table | `$E07E`, loader `$0AD8` | `$9FFE`, loader `$0C5E` |
| Section track pointers | Six, copy loop `$0BB7` | Eight, copy loop `$0CE2` |
| Voice-command dispatch | `$0D5B` | `$0DE5` |
| Command address table | `$0FDA` | `$1069` |
| Command operand lengths | `$1018` | `$10A7` |
| Instrument table | `$1500`, loader `$0D77` | `$1F10`, loader `$0E01` |
| DSP sample directory | `$1600`, setup `$0793` | `$2000`, setup `$0A0D` |
| Duration-rate table | `$1392` | `$1EA8` |
| Velocity table | `$139A` | `$1EB0` |

The address tables contain 31 commands, `$E0` through `$FE`. The first 27 have
the standard operand lengths and meanings. The scanner checks the common
lengths, the `$FB` and `$FD` handler targets, and the four trailing lengths.
Checking the targets avoids treating unrelated code or an SFX handler copy as
proof of a music-driver revision. Existing probes locate the playlist, note
tables, instrument table, and DSP directory.

| Command | Operands | Meaning and evidence |
| --- | --- | --- |
| `$FB` | None | Enable this channel's echo: `$12E8` / `$1363`. Sets its enable bit and clears its disable bit. |
| `$FC` | None | Disable this channel's echo: `$12FC` / `$1377`. Preserves the other channel bits and echo volumes. |
| `$FD` | ADSR1, ADSR2 | Write the channel's DSP envelope registers: `$12CA` / `$134B`. GAIN, SRCN, and tuning remain unchanged. Hashire also saves the two bytes for restoration after SFX. |
| `$FE`, Hashire | Two ignored bytes | `$0F77` calls the track-pointer increment entry `$0D6F`. Dispatch has already consumed the first operand; this skips the second. |
| `$FE`, Albert | Volume | `$0F0A` stores `$03D7`. The music volume calculation multiplies by this byte at `$1340`, before squaring the combined level. It is independent of `$E5`/`$E6`. |

Both versions initialize master volume to `$B0` and tempo to `$20`. Albert's
additional volume starts at `$FF`. The implementation composes the two volume
controls using the existing N-SPC square-law gain representation. Changing
`$FE` leaves an ongoing `$E6` source-volume fade running.

Hashire's gate counter is `max(1, (length * rate) >> 8)` at `$0CC4`.
Albert unconditionally adds one after the multiply at `$0D81`. Both retain the
standard two-tick key-off gap and tie lookahead. Their shared duration table is
`32 65 7F 98 B2 CB E5 FF`; the velocity table is
`0A 19 28 3C 50 64 7D 96 AA B9 C8 D4 E1 EB F5 FF`.

Instruments retain the standard six-byte layout and big-endian pitch scale.
Negative SRCNs select DSP noise, using the low five bits as its clock rate
(`$0DA0` / `$0E2A`); these rows must not terminate instrument discovery.
The following DSP directory bounds the instrument table.

BGM requests use port 0, strip bit 7, and reserve `$7D`–`$7F` for driver controls.
The other ports feed separate SFX paths. Hashire's voices 6 and 7 must not be
read as extra BGM section pointers. Albert contains additional players using
other cursors and track-state arrays; the first standard reader and dispatch
belong to BGM.

Validation uses relocated synthetic driver fragments and musical streams in
`NinSnesModuleTests.cpp`, covering recognition, song selection, track count,
command boundaries, gate timing, echo, ADSR/GAIN, volume fades, and noise.
Local binary inputs beside the disassemblies start at ARAM `$0200`. Padding the
missing prefix and using the available 64 KiB image permits scan/render and
MIDI/SoundFont export checks for both games; it does not recover the missing
captured song-request state. Game binaries are not included in the repository.
