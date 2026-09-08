# VGMTrans shell

The shell uses `src/value` exclusively. It loads files into one session for
inspection and conversion, using the same registered formats and extractors as
Qt. No legacy root, file objects, or converters are linked into the executable.

Build without Qt:

```sh
cmake -S . -B build/shell -DENABLE_UI_QT=OFF -DENABLE_SHELL=ON
cmake --build build/shell --target vgmtrans-shell vgmtrans-shell-tests
ctest --test-dir build/shell -R vgmtrans-shell --output-on-failure
```

Start an interactive session with `vgmtrans-shell [FILE...]`. Type `help` to
list commands, or `help export` for conversion options. The terminal supports
command completion and history; Ctrl-C clears the current input and Ctrl-D exits.

```text
load "music archive.rsn"
sources
assets
collections
assets 0
tree 0 3
events 0 0 20
instruments 1
samples 1 0
read 1 0x100 32
export 0 "output directory" all --loops 0
export-asset 0 "output directory" midi
create "My collection" 0 1
stitch "joined output" 0 1 --loops 0
remove source 0
quit
```

Use the **IDs shown by the listing commands** for sources, assets, and
collections. Each kind has its own ID space. IDs remain stable as other files
are loaded or removed, but are local to the session. Track, instrument, and
sample indexes are zero based. `create` takes asset IDs and requires one sequence
and at least one sound bank; sample pools are optional.

`tree` shows the asset's source annotation graph, including fields and structures
outside its primary header range. `events` shows decoded commands in source
order, rather than a rendered playback timeline. `dump` writes an entire source,
including a derived source extracted from an archive or container.

For scripts, supply repeated `-c` / `--command` arguments or feed commands on
stdin. Positional files are loaded before any `-c` commands run. `--` ends
option parsing for filenames that begin with a dash.

```sh
vgmtrans-shell "music archive.rsn" -c collections -c 'export all output midi'
vgmtrans-shell "music archive.rsn" < commands.txt
vgmtrans-shell "music archive.rsn" -c 'tree 0 8' | less
```

Script mode has no prompt, banner, history, or automatic pager. It stops on the
first failed command and returns status 1; success and `quit` return 0. The
interactive session continues after command errors. Scan and export diagnostics
are written to stderr; command results go to stdout. Warnings alone do not fail
a command. `diagnostics` displays the retained scan diagnostics on stdout.

Exports default to MIDI. Choose `sf2`, `dls`, `wav`, or `all` explicitly for
other outputs, or combine formats, such as `export 0 output midi sf2`.
WAV means individual samples, not a recording of the sequence.
`export all` writes one `collection-<id>` subdirectory per collection, so repeated
titles do not collide. Existing output files are replaced. Empty or failed
artifacts are reported without writing placeholder files; successful artifacts
from a partial export are still saved, and the command returns failure.

Single and double quotes preserve spaces in command arguments. Quoted and
unquoted fragments can be combined. Backslashes are literal, including in
Windows paths. There is no variable expansion, globbing, command substitution,
or host shell execution inside the command language.

## Conversion options

All conversion settings in Qt's **Options** menu are available as export flags.
Options apply to the current command; each export starts with the core defaults.
Use `help export`, `help export-asset`, or `help stitch` to see choices and defaults.
Both `--option value` and `--option=value` work. If a setting is repeated, the last
value wins.

| Qt option | Shell flag and choices | Shell default |
| --- | --- | --- |
| Bank Select Style | `--bank-select gs\|mma` | `gs` (CC0 only) |
| Pitch Transition Rendering | `--pitch-transitions preserve\|portamento\|pitch-bend` | `preserve` |
| Tuning Rendering | `--tuning pitch-bend\|rpn` | `pitch-bend` |
| Modulation Conversion | `--modulation synth\|events` | `synth` |
| Dynamic Envelope Conversion | `--dynamic-envelopes` / `--no-dynamic-envelopes` | Ignore |
| Sequence Loops | `--loops N` (extra repeats after the first playthrough) | `1` |
| Export used instrument data only | `--used-instruments` / `--all-instruments` | All |
| Terminate previous voice on new attack | `--terminate-previous-voice` / `--no-terminate-previous-voice` | Off |
| Skip MIDI channel 10 | `--skip-channel-10` / `--use-channel-10` | Skip |
| Sample Filtering | `--sample-filter auto\|none\|snes\|psx` | `auto` (format recommended) |

`rpn` selects coarse/fine tune RPNs. `snes` and `psx` select the SNES S-DSP and
PlayStation SPU low-pass filters. `--dynamic-envelopes` creates instrument variants.
The existing `--simulate-modulation` flag is an alias for `--modulation events`.
`--modulation-scaling full|observed` selects the modulator range (default: `full`).
The shell retains the core's default of exporting all instrument data; use
`--used-instruments` to match Qt's default.

`export` and `stitch` accept all options. `export-asset ... midi` accepts loops,
bank select, pitch transitions, tuning, voice termination, and channel 10 options;
standalone MIDI always simulates modulation as MIDI events.
`export-asset ... sf2` and `dls` accept all options supported by the core request.
Sample filtering applies to SF2/DLS only: WAV export always writes the original
decoded samples, and `export-asset ... wav` accepts no conversion options.
Settings apply to the relevant outputs, as in Qt.

```text
export 0 output midi sf2 --pitch-transitions pitch-bend --tuning rpn --sample-filter snes
export-asset 0 output midi --bank-select mma --use-channel-10 --loops 0
stitch output 0 1 --modulation events --dynamic-envelopes --used-instruments
```

## Implementation

`vgmtrans-shell.cpp` owns and configures a `core::Session`, parses process
arguments, and handles terminal input and history. `commands.cpp` borrows that
session and output streams for each command. A single constant command table
supplies dispatch, argument counts, help, and completion.
`ExportOptions.cpp` maps flags directly into core export requests and keeps option
parsing, target support, and help in one table.

Commands mutate the session through its public API, read immutable
`SessionSnapshot` / `SourceInspection` values, and persist the `Artifact` values
returned by the core. Parsing, extraction, collection discovery, binding,
sequence rendering, sample decoding, and export policy stay in the core. There
is no mirrored workspace model, global session, or rescan on inspection/export.

The `vgmtransshell` library links only to `vgmtransvaluecore` and fmt; the
executable adds `vgmtransvalueformats` and the bundled linenoise terminal editor.
The command tests use a tiny synthetic format through a real session to exercise
source lifetime, stable IDs, inspection, collection creation, and byte-for-byte
export/stitch results. CLI tests exercise the executable's batch and stdin paths.
