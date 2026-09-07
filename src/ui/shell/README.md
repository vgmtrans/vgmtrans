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
Collection export and stitching support the core's loop, dynamic envelope,
used instrument, and modulation policies listed by `help export`.
`export-asset` uses the core defaults; export a collection to customize policies.

Single and double quotes preserve spaces in command arguments. Quoted and
unquoted fragments can be combined. Backslashes are literal, including in
Windows paths. There is no variable expansion, globbing, command substitution,
or host shell execution inside the command language.

## Implementation

`vgmtrans-shell.cpp` owns and configures a `core::Session`, parses process
arguments, and handles terminal input and history. `commands.cpp` borrows that
session and output streams for each command. A single constant command table
supplies dispatch, argument counts, help, and completion.

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
