# core-rewrite-cleanup2: branch guide

This guide covers changes made after `core-rewrite` commit `cb9e0b766` through
code commit `528db5857`. It excludes the earlier core rewrite and cleanup work.
The branch contains 83 recorded audit batches. The architecture still uses
SequenceVM, typed format playback, immutable snapshots, and neutral performance
and synth models.

The [follow-up audit and fixes](REVIEW.md) independently assess these changes and
record the subsequent corrections. The [continued audit](FOLLOWUP.md) records
additional fixes after that review. This guide and the archive retain the original
cleanup2 scope described above.

Production C++ source/header changes remove **1,640 net lines from `src/value`**:
1,290 architecture lines and 350 format lines. UI changes remove another 22 lines.
Tests gain 1,038 net C++ source/header lines across 53 changed files; this includes
API migrations and removal of obsolete checks as well as new regressions.

## Documents and their scope

| Document | What belongs here |
| --- | --- |
| [This guide](README.md) | Final implemented changes in this branch, API migrations, and validation summary. |
| [Branch audit archive](AUDIT.md) | All cleanup2 batch notes, including intermediate experiments, rejected approaches, corrections, proposals, and evidence references. |
| [Branch proposal status](PROPOSALS.md) | An index of this branch's investigations, implemented ideas, rejected approaches, and remaining findings. |
| [Existing architecture audit](../../VALUE_ARCHITECTURE_AUDIT.md) | Earlier work followed by appended cleanup2 notes. Preserved in place; use the branch-only archive for this branch. |
| [Existing design review](../VALUE_CORE_REVIEW.md) and [instrument prototype](../value-core-review/resolved-instrument-prototype.html) | The substantive proposals already existed at `cb9e0b766`; cleanup2 made small alternatives-wording updates. They are not a description of everything implemented by cleanup2. |
| [Asset-resolution guide](../../src/value/scan/AssetResolution.md) | The maintained guide for writing dependency selection and preparation code against the current API. |

All existing documentation and code are preserved. This directory adds a separate
guide, proposal index, and copy of the cleanup2 notes. The archive preserves those
notes verbatim, including intermediate statements and later corrections. Git at
`cb9e0b766` supplies the exact pre-branch versions of the existing documents.

## 1. Session, scanning, and immutable storage

Session owns its registered formats, extractors, and sealing state directly.
`FormatRegistry` and its two files are removed. Registration validation, ordered
admission, and bounded concurrent scanning remain in Session.

Scanner draft handles point to their typed pending values in stable deque
storage. Generic slot lookups, duplicate handle identities, per-draft owners,
and an intermediate retained result are removed. Required programs and payloads
are checked before consuming drafts; completed results are constructed at finish.
`MiscAsset` is now supplemental inspection metadata, so it has no unused payload,
private data, or separate `ScanMiscDraft` authoring protocol.

Scanner failures become attributed diagnostics in ordinary results. Extraction
and member-path failures are isolated so later work can continue. Source,
catalog, snapshot, annotation, and validation indexes use typed IDs directly;
duplicate ownership indexes and private access/forwarding adapters are removed.
Snapshots keep their immutable shared chunks and stable lifetime contracts.

Source inspection owns its selection independently of unrelated snapshots.
Unused source-map queries and presentation tags are removed, while the actual
hierarchy, attribution, role, and semantic indexes remain. Object-reference
helpers live beside their base types; whole assets have one reference spelling.

`ExtractionInput` and `ScanInput` borrow source metadata during use. Returned
assets and extracted members retain the metadata they need. A caller must keep
borrowed metadata alive and stable while an input or builder reads it.

## 2. Collections and preparation

Dependency resolution and publication use `Collection` directly; the separate
`DesiredCollection` representation disappears. Session assigns discovered IDs
and origins while preserving user collection identities. Membership and status
queries live on Collection, and snapshot association can inspect membership
without constructing a temporary member vector.

Sequences store `bankRequests`; banks store `sampleRequests`. The one-vector
`SequenceRecipe` and `BankRecipe` wrappers disappear. Collection messages use
ordinary `Diagnostic` values. Selected targets, placements, and typed resolution
status remain meaningful; unused alternative-candidate vectors and their
transport protocol are removed.

Preparation contexts move from the export directory to `model/AssetPreparation.h`.
Prepared-bank iteration uses the existing owned bank range rather than a second
pointer projection. Lookup, artifact admission, and preparation use their existing
authoritative results instead of duplicate optional state and unreachable fallback
paths. Validation and ownership phases that serve different purposes remain.

Header dependencies are narrower: Session exposes export option types rather than
the complete export implementation, modulation analysis imports neutral performance
types, and synth export declarations forward-declare SourceStore. Consumers include
the complete definitions where they actually construct or use them.

## 3. CompilerCursor, runtime adapters, and SequenceVM

Commands use one `invoke` path for handlers returning either `void` or `Effects`.
`invokeFlow`, unused reader overloads, and redundant argument/type-erasure stages
are removed. Captures still own values needed after decoding. Deferred encoded
field names own their strings; ignored raw bytes move directly to their source
field owner. Relationship operands retain only an optional unsigned value, while
display fields retain their complete values and nonnumeric relationship tags
retain their positions.

Decoded commands no longer carry a second `CommandPlaybackStatus` classification.
Labels, semantics, categories, actual bodies, and flow describe their behavior.
An empty body is queried directly; it can still accompany executable static flow,
delays, or coordinator signals. Opcode guards use the reader's existing `ok()`.

VM stream executors live directly in reserved storage. Visit detection uses the
existing visited-state map; automation identities come from append positions;
output-channel count comes from the allocation traversal. Playlist replay retains
its existing visit entry with a map node rather than copying its repeat-state key.
`LoopPolicy` has the two actual choices, `PlayOnce` and `Preserve`.

State construction uses the existing program and the smaller track context.
`void` represents absent song state instead of `EmptyCompiledProgramState`.
Eligible initialization uses section-entry hooks, with command-budget checks.
Typed runtime adapters, meaningful per-command hooks, finite repeats, wait
predicates, and optional runtime flow overrides remain.

## 4. Performance, pitch, modulation, and MIDI

Pitch curves use optional source samples: absence selects a linear glide and a
supplied list preserves the source staircase. The three curve wrapper types are
removed. Source level quantization is a `sourceLevels` count instead of a one-field
wrapper. Format code reads its canonical source channel from the emitter.

Motion and LFO state derive activity and timing from existing values rather than
parallel flags, plans, counters, or scratch owners. Fixed-point automation names
the actual signed 32-bit Q8 representation. Active-note replacement reuses its
existing map slot. Pan and channel-pan authoring have fewer overlapping overloads.

Flattened portamento used only during MIDI lowering becomes private to that
export layer. Source pitch writes route directly to the transition lowerer.
Generated event vectors finish growing before consumers borrow their elements.
Global reverb comes from its existing global owner; external-preset warnings come
from actual instrument selection rather than a second recognition pass. Renderer
configuration and scratch state stay with their owner, and output tracks are
populated directly in their final reserved vector.

MIDI track encoding writes into the final file buffer. Bank-select messages are
constructed directly. Stitching prepares unique parts once, renders/scales locally,
moves completed tracks into the result, and derives bank/channel information from
the authoritative prepared values and retiming walk.

## 5. Synth models, samples, and SF2/DLS/WAV

Sample tuning is the scalar `pitchCents`. Native YM2151 settings are
`optional<Ym2151Voice>` instead of a variant with one possible alternative.
`SamplePoolBuilder::finish()` returns a SamplePool and instrument-set finish returns
the instrument vector. Result-only wrappers and redundant source-range authoring
steps are removed.

Generic sample and SNES SRCN lookups return SampleRef directly, using its existing
empty state. The SNES catalog consumes a range/projection, collects byte-sized
membership once, validates unique SRCNs in order, and resolves them through a
fixed reference table. The directory wrapper and vector-copy adapter disappear.

Export writers assemble RIFF children and PCM directly into their final buffers
where the staging value served no other purpose. SF2 and DLS each reuse one
destination mapping and write guarded records directly. SF2 avoids another complete
serialized PCM owner. WAV writes decoded PCM directly. PSX decoded-offset arithmetic
uses the existing frame conversion. User sample-filter preference lives in export
policy; native formats continue to recommend concrete neutral filters.

## 6. Native formats and extraction

Across the format implementations, remove copied immutable settings, channel
numbers, unread version fields, redundant recipes, inspection indexes, and second
decoded-command buffers. Examples include Nin/Quest configuration, Rare's command
map and patch list, AKAO prepared configuration, HOSA instrument ownership,
SonyPS2 playlist flags and event lookup, SquarePS2 bank identity, and Mori inspection
scratch. Driver-specific mutable state and version-dependent decoding remain.

SegSat interprets its gain controllers and note velocity during typed playback,
removing a post-playback reinterpretation pass and its controller-address maps.
Other small authoring changes keep local operand decoding beside the native command
and construct existing values directly.

PSF extraction borrows reserved bytes, decodes PSF2 blocks into their final member
buffers, stores tags directly in their final map, and declares executable offsets,
load-address offsets, and source formats together. Each PSF2 library retains its
whole-library staging boundary. MAME extraction uses logical member identity and
its final extracted-source owner. SPC/RSN extraction reuses shared signatures/readers
and constructs only the final owned title. UI inspection similarly reuses existing
source-range and source-kind rules.

## 7. Correctness fixes and new regression coverage

The branch includes behavior corrections, so cumulative byte identity with the
starting branch is not the intended contract. Important fixes include:

- Attributed scanner/extractor/member failures instead of escaping or aborting
  later work; safe MIDI output filenames for punctuation and path separators.
- Section initialization obeying command budgets, including a budget regression
  introduced by an earlier cleanup batch and subsequently fixed.
- Owned temporary source text and deferred field labels; retained partial ignored
  payloads; snapshot copy/move indexes retaining their correct owners.
- Linear pitch interpolation returning its declared endpoints, and integer motion
  and LFO fades retaining durations wider than the animated value's integer type.
- MIDI clipping before narrowing or signed bias; saturating note endpoints and
  PPQN conversion at the final representable tick.
- SF2 pitch/root/residual bounds and large finite modulation values handled before
  unsafe narrowing; DLS numeric conversion bounded for its integer target.
- PSF executable-offset arithmetic widened before addition; nested MAME archives
  recognized by their actual logical member name.
- SegSat loop markers preserved when a loop destination is a controller, eliminating
  phantom gains on channels where that controller did not execute.
- Nin optional note suffixes treating end-of-ARAM as absence while preserving
  mandatory-operand failures.
- GBA DirectSound enforcing its output-frame ceiling before a product can wrap.

Tests exercise public parser, rendering, encoding, ownership, and export behavior.
Some edits merely migrate existing fixture calls or remove checks for deleted APIs.
Focused temporary comparisons, sanitizer runs, and allocation/timing probes are
recorded in the audit; they do not amount to whole-program sanitizer or universal
performance/exception guarantees.

## Calls and types affected when writing format code

| Earlier spelling or representation | Current spelling or representation |
| --- | --- |
| `FormatRegistry` | Register through Session; inspect `formats()` / `extractors()`. |
| `DesiredCollection` | `Collection`; Session owns publication identity. |
| `SequenceRecipe` / `BankRecipe` wrappers | `bankRequests` / `sampleRequests`. |
| `CollectionIssue`, dependency alternatives | `Diagnostic`, chosen targets, and `ResolutionStatus`. |
| `ScanMiscDraft` payload/data | `misc(name, range)` returns AssetId. |
| Builder result wrappers | SamplePool / instrument vector directly. |
| Optional sample lookup and `SampleRef::none()` | SampleRef directly; `empty()` / default construction. |
| Whole-root sequence/misc ObjectRef kinds | `ObjectRefs::asset(id)`; actual subobject references remain. |
| Unused source-map spatial/link convenience queries | Retained indexed queries and `SourceInspection::itemAt`. |
| `invokeFlow` | `invoke`; return `Effects` when needed. |
| `hasOpcode()` / `CommandExecution::valid()` | Initial `ok()` / contextual check of `body`. |
| `CommandPlaybackStatus` | Existing presentation and actual execution/flow. |
| `StreamStateContext` / empty song-state placeholder | SequenceProgram / `void` song state. |
| Curve variant, `ValueQuantization`, `Tuning` | Optional samples, `sourceLevels`, `pitchCents`. |
| `SequenceFixedPointAutomation<s32, 8>` | `SequenceFixedPointAutomation`. |
| `LoopPolicy::Default` | `LoopPolicy::PlayOnce`; Preserve remains explicit. |
| `ValueFormatOptions` | Optional MAME database path passed to `registerValueFormats`. |
| `SequenceExportRequest` alias | `SequenceRenderOptions`. |
| Export-layer preparation header | `value/model/AssetPreparation.h`. |

These include intentional source/API narrowings. Repository consumers were migrated
and rebuilt together; compatibility with external callers or old object files is
not established. Removed convenience overloads and transitive includes may also
require callers to name their defining header or the retained explicit form.

## Validation and remaining limits

The latest code has a warning-free macOS Release build and all 44 CTest suites pass,
including format suites, core tests, Qt model tests, shell tests, and the parity
self-test. The final batch compares the actual batch 82 and 83 executables:

| Corpus | Compared records | Exact retained artifacts |
| --- | --- | --- |
| Original selection, 126 inputs under two policies | 252 | 3,922 |
| Expanded selection, 73 input closures | 894 exports | 858 |

Expected failures and partial exports remain separately identified. These are
value-before/value-after checks; the parity self-test and existing legacy modes do
not establish universal legacy equivalence. Real inputs cover 37 of 38 registered
value families. The branch also found pre-existing missing value recognition for
legacy Org; that gap is recorded, not implemented here.

In the final expanded replay, 72/73 raw inventories match. The remaining Sony input
differs only in sample-pool IDs 0/1 and the matching collection reference. The same
new executable produces both assignments, and unchanged concurrent scanner code
permits that ordering. In 38 additional batch 82 runs and eight original-binary runs,
the older executables kept one assignment. The narrow source-based admission,
inconclusive baseline-variation checks, and raw records are retained; all six
exports for that input match. The original strict replay wrapper returned status 1
on its unknown-case guard after completing the replays; separate review closed
that inventory case. It is not counted as raw byte equality.

Code changes and review work are paused after commit `528db5857`. The subsequent
documentation separation does not resume the architecture audit.

## Reading the implementation

Start with [Session](../../src/value/session/Session.h), the
[dependency guide](../../src/value/scan/AssetResolution.md),
[CompilerCursor](../../src/value/sequence/CompilerCursor.h), and
[SequenceVm](../../src/value/sequence/SequenceVm.h). Then follow the
[performance model](../../src/value/sequence/PerformanceModel.h) and
[synth model](../../src/value/synth/SynthModel.h) into
[export preparation](../../src/value/export/Export.cpp).
[Test instructions](../../tests/README.md) explain how to run focused suites.

For the exact source changes, compare `cb9e0b766` with `528db5857` in Git.
For a particular experiment or batch, use the branch audit archive; its temporary
evidence paths are local records, not checked-in tooling or shipped app features.
