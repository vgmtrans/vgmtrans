# Continued core audit

This continues the [initial audit](REVIEW.md), starting at `28e68088c` on
`codex/core-rewrite-cleanup2-audit`. The work focuses on stitched export, physical
modulation, and repeated processing between SequenceVM and export preparation.

The main findings are two correctness problems affecting exported playback and
another substantial source of unnecessary allocation. The fixes preserve the
existing architecture; they add no new rendering stage or format-facing abstraction.

## Fixes

### Stitched songs inherited the previous song's settings

A standalone MIDI file can omit its initial tempo and meter when they are the
standard defaults. Simply placing that file after another song does not restore
those defaults. For example, a song meant to start at 120 BPM could continue at
the preceding song's 80 BPM. Mono mode and master volume could also carry over,
even though stitching already reset pitch, tuning, and most channel controllers.

Each later part now restores:

- **Song state:** 120 BPM and 4/4 before its own opening tempo or meter events.
- **Channel state:** poly mode before its own channel commands. This uses CC127;
  CC126 with value zero still enables mono mode, according to the
  [MIDI Association's controller reference](https://midi.org/midi-1-0-control-change-messages).
- **Port state:** full master volume once per MIDI port. Repeating this reset on
  every channel would overwrite a song's opening master-volume command.

The source's explicit opening commands take precedence over these defaults.
Master-volume serialization is shared by ordinary rendering and stitching through
one small MIDI message factory.

The new regression reads the serialized MIDI tracks. It covers two 17-track songs,
default and explicit opening settings, and enabled/disabled MIDI port metadata.
It failed before the fix and passes afterward. Existing repeated-part, bank,
instrument-policy, and modulation-scaling checks also pass.

Commit: `a141c48d7` — Restore independent song state at stitched MIDI boundaries.

### Modulation analysis discarded stopped oscillators

The initial audit made conversion of zero Hz safe. Continuing upstream exposed
another problem: sequence analysis excluded zero from observed frequency ranges.
A stop → run → stop sequence could therefore become a fixed running oscillator
in synth export. A sequence that only specified zero Hz could lose its physical
modulation profile altogether.

Range observation now accepts every finite, nonnegative value. This also removes
the flag that treated zero-delay observations differently from zero-rate
observations. MIDI normalization and companion synth generation receive the same
complete range.

New regressions cover permanently stopped oscillators and stop/run/stop sequences
for both vibrato and tremolo. They check the observed ranges, synth bases and
controller spans, and emitted MIDI controls. They fail before the fix and pass
afterward. Synth export still uses the documented very-slow approximation for
zero Hz; this does not claim exact oscillator disabling.

Commit: `e8135cd28` — Retain stopped LFO rates in sequence modulation profiles.

### Already chronological events were sorted again

Both VM completion and instrument preparation invoked a stable sort even when
events already had the required order. Those sorts allocated large temporary
buffers and moved event records that were already in place.

Each boundary now first checks its ordering requirement. VM completion still
orders future-dated events by tick; preparation still orders by tick and source
sequence after any format finalizer changes. Unordered input takes the same
stable-sort path as before. Neither ordering contract has been removed.

In the same 100,000-note preparation probe used in the initial audit, requested
allocation bytes fell from **181,400,776 to 111,000,776**: **70,400,000 bytes, or
67.1 MiB, avoided**. Allocation count fell by two, matching the two eliminated
sort buffers. Three runs reproduced these counts. This measures allocation
traffic during preparation, not peak process memory or a universal speedup.

Commit: `bc9a6e1f8` — Skip sorting performance events that are already chronological.

### Decode errors incorrectly blamed codec support

WAV and synth export labeled every failed sample decode “Unsupported sample
codec.” The decoder can also reject invalid ranges or malformed data for a
supported codec. The diagnostic now says “Could not decode sample data” and
retains its source location. It reports the known failure without inventing a
cause or introducing a larger error API.

This improves diagnosis; it does not resolve the underlying partial export
failures observed in the corpus.

Commit: `2ac03ae40` — Describe sample decode failures without assuming an unsupported codec.

## Verification

The final complete macOS Intel Release build succeeds, and **44/44 CTest suites
pass**, including the Qt tests.

A fresh comparison against the start of this continuation covers **21 format
families, 21 private inputs, 42 policy/input runs, and 1,642 exported files**.
Both policies export MIDI, SF2, and DLS with zero extra loops. The second uses
sampled modulation, used instruments, pitch-bend transitions, and RPN tuning.
This uses the earlier representative sample with *Aria of Sorrow* as the MP2k
input instead of *Drill Dozer*.

Only three exported files changed: the MIDI, SF2, and DLS for *Dragon Quest VI's*
“Overture” (HeartBeatSnes). Detailed inspection confirmed that the differences
are confined to **67 MIDI LFO-rate controller values and the matching synth
frequency bases/controller spans**. Notes, other controls, sample data, and all
sampled-modulation outputs match. This is the expected correction when an observed
zero rate becomes part of the shared range.

Exit statuses and logs matched in that comparison, which preceded the final
diagnostic wording change. Three runs still report matching partial failures.
These are not successful conversions. A separate repeat of both policies on the
GBA input verifies the final diagnostic change: **1,308 exported files remain
byte-for-byte identical**, statuses match, and the only log differences are the
intended replacements of the misleading message.

The new stitched-boundary behavior is covered by the serialized synthetic
regressions; the ordinary-export corpus comparison does not exercise stitching.
These checks do not establish audible accuracy for every native opcode or
cross-platform equivalence. The broader coverage gaps described in the initial
report remain.
