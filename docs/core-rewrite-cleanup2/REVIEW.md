# Cleanup2 audit and follow-up fixes

The cleanup substantially improves the shared architecture, but only partly
achieves the broader ambition of making the whole core simpler. Its strongest
changes remove things a contributor previously had to understand: duplicate
registries, collection representations, wrapper types, and command-authoring
choices. SequenceVM remains intact, and format implementations remain separate
from MIDI, SF2, and DLS encoding.

The branch also missed substantial duplicate storage and ordering work, and left
a known, reachable numeric problem unresolved. This follow-up fixes all three.
These issues predate cleanup2.

This review compares `cb9e0b766` with the original branch tip `bc9043eb5` and records
the initial follow-up through `28e68088c` on `codex/core-rewrite-cleanup2-audit`,
dated October 10, 2026. Further findings and fixes are recorded in the
[continued audit](FOLLOWUP.md).

## How well it met the goals

| Goal | Assessment |
| --- | --- |
| Reduce architectural concepts | **Achieved meaningfully.** Session owns registration; collections use one representation; one-field recipes and several redundant state holders disappear. |
| Make format code easier to write | **Improved, with modest scope.** One `invoke` API accepts ordinary handlers or handlers returning flow effects. Absent song state uses `void`. Several builders return the values callers actually want. Most native decoding and playback logic remains. |
| Retain SequenceVM | **Achieved.** The scheduler, stream/channel model, loop handling, and typed playback adapters remain. |
| Keep formats independent of exporters | **Achieved for the inspected boundaries.** Formats and neutral sequence/model/synth/scan code do not include `value/export` headers. Physical modulation values are converted into destination units in the export layer. |
| Reduce code without losing clarity | **Mostly achieved.** Removing duplicate concepts is useful; later one-line reductions have much less value. Lines removed are supporting evidence, not a sufficient measure of simplicity. |
| Avoid regressions | **Supported by the checks below, within their coverage.** The sampled comparisons do not establish equivalence for every supported driver or input. |

Independently counted `.cpp` and `.h` lines, including comments and blank lines:

| Area | Before | After cleanup2 | Reduction |
| --- | ---: | ---: | ---: |
| Shared architecture | 24,514 | 23,224 | 1,290 / 5.26% |
| Native formats | 73,935 | 73,585 | 350 / 0.47% |
| All `src/value` | 98,449 | 96,809 | 1,640 / 1.67% |

These are the cleanup branch's numbers, before this follow-up. They describe a
useful incremental simplification, rather than a dramatic reduction of the entire
core. The large resolved-instrument and voice-continuity redesigns described in
older review documents already exist at the starting commit and should not be
credited to cleanup2.

The initial follow-up adds 21 net production lines to `src/value`, including bounds
checks and contract comments. Its simplifications remove duplicated data and work;
the new regression tests and comparison tool are additional test code.

## What the cleanup did well

The removal of `FormatRegistry`, `DesiredCollection`, and single-vector recipe
wrappers makes ownership and data flow easier to follow. Stable typed scanner
drafts eliminate generic slot plumbing. Commands have fewer overlapping authoring
APIs, while native handlers still express their driver's behavior directly.

Several changes improve correctness as well as size: deferred field names own
their strings, source inspection retains its owner, and MIDI event storage stops
growing before consumers borrow its elements. Direct output buffers remove
unnecessary copies without changing the neutral models. These are better examples
of the original goal than simply shortening expressions.

The branch also correctly retained distinctions that serve real behaviors:
immutable snapshot ownership, finite repeats versus song loops, shared stream
state versus channel state, and independent note, envelope, and LFO lifetimes.
Collapsing those distinctions would push hidden special cases into format code.

## Fixes made during this audit

### One retained performance for export

`PreparedCollection` used to retain both the original rendered performance and a
fully copied, resolved performance. Playback then kept the original event tree
after encoding MIDI and SoundFont, although the production UI only used its source
timing spans for highlighting.

Preparation now consumes the rendered performance. It retains the resolved events,
modulation profile, rendering diagnostics, and bank owners it needs. Playback keeps
the encoded outputs and `sourceSpans`. Raw native-event analysis still uses
`renderSequence` or `renderCollection`; format tests that inspect those events now
request that path explicitly. This preserves the distinction between native events
and export adaptations without requiring every export to retain both.

The lifetime test covers preparation copies, moves, destroyed prior owners,
rendering failures, and the preserved source timeline. Existing playback tests
also check the resulting timing spans and encoded outputs.

A 100,000-note preparation probe requested **35,200,088 fewer allocation bytes**
(about 33.6 MiB): 216,600,864 before versus 181,400,776 after. This measures
allocations during construction, not peak process memory or a general speedup.
The principal improvement is a simpler ownership contract; it does not depend on
a net line reduction.

Commit: `af8d6ef8e` — Consume rendered events during export preparation.

### One ordering step for the MIDI timeline

MIDI planning sorted the full event values. Rendering then built a pointer list,
added song-wide reverb controls, and sorted again. The first sort had no effect on
the eventual order: both sorts were stable and used the same tick/sequence key.

Planning now supplies the completed events and rendering orders the combined
timeline once. This also avoids moving large event records and their owned data.
The existing pitch, portamento, coincident-event, and reverb tests pass unchanged.

For the same 100,000-note probe, MIDI rendering requested **35,200,000 fewer
allocation bytes**: 126,438,305 before versus 91,238,305 after. This is a separate
stage from the preparation measurement above; the two savings should not be
interpreted as a measured reduction in peak memory. The change removes a redundant
responsibility without adding another abstraction.

Commit: `97a8d69b9` — Order MIDI events once at the rendering boundary.

### Defined conversion for zero and extreme modulation values

MP2k permits a physical LFO range starting at zero. The exporter passed this into
`log2`, followed by integer rounding. On the tested build, the resulting invalid
math produced a zero absolute-cents base, corresponding to about 8.176 Hz, rather
than an approximation of a stopped oscillator. Subnormal rates and very large
finite depths also lacked safe numeric boundaries.

The shared synth conversion now checks the rate before division/logarithms and
uses **−16000 absolute cents** for stopped or unrepresentably slow LFOs. That floor
matches the SoundFont LFO bound used by
[FluidSynth](https://github.com/FluidSynth/fluidsynth/blob/master/src/synth/fluid_gen.c).
Base generators, controller ranges, and MIDI normalization use the same mapping.
Depth conversion saturates before multiplication, rounding, or narrowing can
produce an out-of-range result.

This is an explicit very-slow-oscillator approximation for synth export, not exact
oscillator disabling. DLS uses the same shared approximation. Native physical
values and the sampled-event modulation policy remain unchanged.

New regressions fail on the old implementation and pass with the fix. They cover
zero, subnormal and small positive rates, extreme finite depths, floating-point
exception flags, serialized SF2/DLS generator and controller amounts, and matching
MIDI controller normalization.

Commit: `8d86ec187` — Bound physical synth modulation before numeric conversion.
Additional coverage: `ba27d0f00` — Check MIDI rate normalization against the synth
frequency floor.

### Repeatable comparison tooling

The historical audit references extensive temporary corpus outputs. Those paths
are not a durable way to repeat the checks. The new
[`tests/compare_value_exports.py`](../../tests/compare_value_exports.py) compares two
shell builds using a private input list. It records input and binary hashes,
exports, raw logs, exit statuses, and per-file comparisons. It reports differences
strictly, including intentional fixes, filename changes, and timeouts.

The [test guide](../../tests/README.md) documents the command and interpretation.
No game data is added to the repository.

## Validation

The original cleanup tip passed **44/44 CTest suites**. The follow-up also passes
**44/44**, including the Qt tests, after a complete configured Release build on
macOS Intel. The suites cover the shared core and individual formats. Their
default invocation uses synthetic fixtures; the optional private-corpus CMake
paths were empty.

Fresh real-input comparisons cover 21 detected value families: AkaoSnes,
AsciiShuichiSnes, CapcomSnes, ChunSnes, CompileSnes, GraphResSnes, HeartBeatSnes,
HudsonSnes, KonamiSnes, MP2k, MoriSnes, NDS, NamcoSnes, NinSnes, PandoraBoxSnes,
PrismSnes, SculptSoftSnes, SoftCreatSnes, SonyPS1, SuzukiSnes, and WolfTeamSnes.
Each comparison exports MIDI, SF2, and DLS with zero extra loops, under both synth
modulation and sampled modulation with used instruments, pitch-bend transitions,
and RPN tuning.

| Comparison | Fresh evidence | Result |
| --- | --- | --- |
| Pre-cleanup revision → cleanup2 | 21 inputs, 42 policy/input runs, 2,618 exported files | All file contents match. Four MIDI files have intentionally sanitized filenames: two titles under both policies. Their bytes are unchanged. |
| Cleanup2 → ownership and numeric fixes | 34 inputs, 68 runs, 4,508 exported files | 838 SF2/DLS files change only in LFO frequency bases and controller spans. Every changed RIFF leaf was checked. All MIDI files and all sampled-modulation outputs match. Logs and statuses match. |
| Before → after removing the duplicate MIDI sort | The same 34 inputs and 68 runs, 4,508 exported files | Every file, filename, log, and exit status matches. |

The first large GBA synth comparison exceeded its three-minute limit. Repeating
it with a fifteen-minute allowance completed on both revisions; the table uses
the completed comparison. Seven runs in the second row report existing partial
export failures on both revisions, including GBA sample-decoding/table limits and
NDS collections with no exportable samples or instruments. Matching failures are
not successful conversions, and their causes need separate format-specific
investigation.

The review concentrates on the changed ownership and execution boundaries,
compiler/runtime adapters, scanning and preparation, and MIDI/synth lowering. It
does not establish every native opcode's audible accuracy or replace a full
legacy/value parity campaign. The historical claim of 37 of 38 registered families
is broader than this fresh sample and remains a historical result.

## Remaining opportunities and limits

**The largest remaining complexity is export planning.** At the cleanup tip,
`MidiTrackPlanner.cpp` and `PerformanceMidiRenderer.cpp` total 2,243 lines. Voice
continuity, hardware durations, pitch transitions, modulation, and controller state
interact there. Future simplification should remove demonstrably duplicated
decisions and data, with overlapping-voice and coincident-control fixtures. Moving
that logic between files or merging distinct lifetimes would not itself improve
the model.

**Format coverage has a pre-existing exception.** Legacy Org recognition has no
equivalent registered value implementation. Cleanup2 documents this gap but does
not close it. Thus the premise that every legacy format is already supported by
the rewrite needs that qualification. Adding and verifying a format is separate
from simplifying the shared architecture.

**Concurrent discovery does not promise repeatable numeric IDs between sessions.**
The branch identifies this, especially with Sony inputs. Stable IDs within a
session and deterministic identity across independent scans are different
contracts. Enforcing the latter needs an explicit ordering policy; serializing
all scanning would impose a documented performance cost. The comparison tool
retains raw inventories so such differences remain visible.
