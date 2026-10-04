# Value architecture simplification audit

Current work is on `core-rewrite-cleanup2`, based on `core-rewrite` revision
`cb9e0b766`. Thirty-eight validated batches remove 1,183 net production lines
from `src/value`, plus 12 unused UI presentation lines. All 44 CTest suites pass.
The original corpus preserves all 3,922 artifacts; the expanded archive replay
preserves another 858 artifacts across 73 input closures. Together the real inputs
cover 37 of 38 registered families. All export records match batch 37 exactly;
72 expanded raw inventories match byte-for-byte. One Sony input differs only in
previously proven concurrent sample-pool ID scheduling. Its narrowly mapped
inventory matches retained original evidence; all raw differences are preserved.
The cleanup2 records appear at the end of this document.

Earlier audit starting revision: `e9f4450c3` (`core-rewrite-refactor`).

The goal is to make format implementations and the shared architecture easier
to understand, while retaining SequenceVM, conversion behavior, source
inspection, and the separation between source formats and export targets.
Reductions should remove responsibilities or redundant representations, not
compress code or hide format behavior behind a new framework.

## Baseline and verification

- The value formats contain 71,728 lines across their source and header files.
- The existing `vgmtrans-value-core-tests` suite passes at the starting revision.
- The legacy/value parity harness self-test passes at the starting revision.
- No real-file parity corpus was available for the initial checks. Synthetic
  tests cannot establish parity for every game; preserve that distinction
  when assessing these changes.
- The initial include audit found no MIDI/SF2/DLS exporter or legacy-core
  includes in value formats, sequence code, or synth code.
- After the first two reductions, the value core suite, standalone Square PS2
  and Sony PS2 suites, and parity self-test all pass (4 CTest targets). The
  build completes without compiler warnings.

## Changes

### Remove unused decoded operand classifications

Ordinary command operands carried roles such as `Duration`, `Pitch`, `Level`,
`NoteKey`, and `Count`. No production consumer used these classifications.
They were transient, discarded after decoding, and did not affect source-map
presentation. Names and display settings already described these values.

Remove those enum cases and their arguments throughout the formats. Keep roles
used by source links, source-channel attribution, and AKAO instrument discovery.
For example, `event.u8("duration", SemanticOperandRole::Duration)` becomes
`event.u8("duration")`; an encoded jump target still declares `JumpTarget`.
The equal-valued-operand regression test still verifies that a jump does not
turn an unrelated numeric operand into a source link.

### Read SNES samples directly from instrument records

Many format-local helpers copied SRCNs into vectors, sometimes sorted and
deduplicated them, and passed them to a catalog reader that did that work again.
The shared reader now accepts a record range and a projection such as
`&Patch::srcn`. Formats retain their own instrument validation and sample
selection rules. The existing catalog implementation continues to determine
sample order, validate streams, and preserve directory ranges.

Remove the repeated collectors and redundant AKAO/Konami sample-reader wrapper
APIs. A synthetic catalog test covers unordered records, duplicate SRCNs,
invalid streams, and exact directory/payload ranges.

### Share fixed-width record reads

The sixteen sequential and positioned integer readers repeated bounds checks,
cursor updates, numeric conversion, source ranges, and field annotation. Two
private helpers now own that work. Their public named readers are unchanged,
and the helpers remain implemented in the `.cpp` file.

Keep separate bounds policies: sequential reads stop after the first failure;
positioned reads can recover other complete fields from a damaged record.
Variable-length, 24-bit, and raw-byte reads retain their specific behavior.
Tests cover all eight integer readers in both access modes, including signed
values, endian order, annotations, partial consumption, and diagnostic count.
The same four CTest targets pass after this change, with no compiler warnings.

### Accumulate source ranges in one place

`SourceRange::include` now owns the common covering-range operation. It keeps
the first source, ignores invalid and foreign ranges, and includes zero-length
anchors. Source maps, inspection, sequence extents, AKAO banks, and synth
builders use the same operation instead of maintaining local min/max loops.

Synth builders no longer wrap already-nullable ranges in `optional`, or store
separate observed copies of instrument and region ranges. Explicit ranges
remain authoritative. The range regression test covers disjoint records,
anchors, and source boundaries; the existing builder and inspection tests
cover their ownership policies. Core, Square PS2, Sony PS2, UI, and parity
self-tests all pass (5 CTest targets), with no compiler warnings.

### Keep compiled operations beside their public methods

Remove the second layer of private templates behind ordinary compiler-cursor
operations. Output, state updates, waits, and counted repeats now construct
their small command bodies directly. Scalar output parameters use the
emitter's concrete types instead of instantiating templates just to convert
them later. Source instrument identities own their strings when compiled.

This removes 88 production lines and 23 private helper names without adding an
abstraction. Existing tests cover command ordering, state updates, repeats,
flow conflicts, and source-free execution. An additional lifetime test checks
temporary instrument domains, envelope policy, and absent versus declared
level quantization. The same five CTest targets pass without compiler warnings.

### Use one arithmetic path for sequence motion

Timed motion now shares its startup and completion rules whether its step is
computed or supplied by the driver. Fixed-point automation normalizes its
current raw value and delegates step calculation to that same implementation;
it no longer computes the step a second time and changes the plan's mode.
Remove unused getters and callback overloads inherited from legacy helpers.

Regression tests cover delay boundaries, integer truncation, exact final
targets, custom steps, zero-length motion, and negative rounding on retarget.
A separate generated comparison against the previous header checked 7,808,386
integer, floating-point, and fixed-point transitions under AddressSanitizer
and UndefinedBehaviorSanitizer with no differences or sanitizer findings.
All five CTest targets pass, with no compiler warnings. This removes 54
production lines while keeping the existing format-facing motion plans.

### Remove duplicate VM loop and analysis paths

Loop handling already changes the executor's current command. Remove the
extra action enum and result wrapper that repeated whether that command still
exists. Requested loop repeats and temporary replays while coordinating the
sequence endpoint now share their setup. Repeat snapshots use their existing
map value directly instead of a one-field wrapper with a forwarding comparison.

Analysis and prepass rendering use one silent-pass path. Tests verify that
analysis executes once both with and without a prepass hook, and invokes that
hook exactly once when present. Existing VM tests cover finite repeats,
inferred and declared loops, loop counts, synchronized cutoffs, section
playlists, and sustained notes. All 17 configured CTest targets have now been
rebuilt and pass, including the additional standalone format suites.

### Keep tempo-relative modulation state in its existing event types

Vibrato, tremolo, and pan rates now use one collection keyed by target and
pitch layer. Only vibrato has multiple layers. This removes separate storage,
lookup, replacement, and tempo-update paths while preserving derived event
order. Delays retain their event values directly instead of splitting ticks
and update policy into parallel fields and rebuilding the events later.

Tests exercise concurrent rates, independent vibrato layers, replacement with
fixed-clock modulation, both delay types, future-note delay policy, and tempo
source attribution. All 17 CTest targets pass after rebuilding, without compiler
warnings. The resolver is 42 lines shorter.

### Let side-effecting reads and automation run directly

Record reads annotate fields, and automation startup/callback ticks update
state even when callers do not need their return values. Remove `nodiscard`
from these operations and from automation-intent emission. Queries, explicit
failure results, and unbound tick results retain their checks. The decoded
field API already allowed most such calls; remove the redundant casts there
as well. This removes 145 discard wrappers across formats, shared helpers,
and their tests without changing the operations performed.

Remove two discarded pure queries: a playlist annotation ID lookup and a
temporary automation-output view. The latter's fade already declares its end
tick. All 17 CTest targets pass after rebuilding, without compiler warnings.

### Retire the obsolete dynamic-envelope entry point

Production export already uses instrument-variant materialization, which
combines envelope and stereo variants in one pass. Remove the compatibility
header, result alias, and wrapper used only by tests. Those tests now exercise
the production entry point with explicit envelope options. Also correct the
MIDI resolution comment: its fallback is seven-bit output, not a legacy hint.

The complete default build succeeds, including the Qt application and shell
executable. All 17 CTest targets pass; the build has no compiler warnings.

### Give each control-flow operation one name

Remove two aliases with identical implementations. `return_()` declares the
default return transition, which a runtime handler may override;
`discoverTarget()` adds a reachable bytecode block without choosing the runtime
path. The former `discoverReturn()` name obscured that it set a default runtime
transition, while `mayBranchTo()` duplicated decoder-only target discovery.
Update all format and test callers. Distinct jump semantics retain their named
helpers. The full application build and all 17 CTest targets pass without
compiler warnings.

### Use one value representation for decoded source fields

Decoded operands now use `SourceValue` directly instead of a second variant
that the source-map projection immediately converted. Runtime arguments retain
their typed values; source addresses retain their numeric value, display style,
and relationship role. This removes the conversion visitor and duplicate
address handling in source projection and AKAO reference discovery. String
literals also produce text fields instead of requiring an explicit string.

A focused cursor test covers encoded signed offsets, resolved address links,
booleans, signed enums, strings, fractions, and signed reads. The full build and
all 17 CTest targets pass without compiler warnings.

### Limit synth routing to implemented physical modulation

Remove unused pitch, filter, and pan routes from the modulation intermediary;
ordinary tuning and pan still use the synth model's dedicated fields. Replace
the optional general-purpose source enum with its two actual choices: the
destination's default controller and channel pressure. None of the removed
routes had a producer in value code. Retain the unknown-destination sentinel.

DLS generators and modulators now share one destination/scale mapping. The
controller becomes a direct connection source for rates and attenuation, or
controls the existing oscillator source for depth. Preserve the established
unsupported DLS vibrato-delay modulation behavior.

This removes 139 production lines. All 17 CTest targets and the full build pass
without compiler warnings. An independent before/after comparison exercised
4,608 physical-modulation configurations across instrument/region scope,
waveform, depth mode, delay, rate, gain, conversion policy, and observed scaling:
all 9,216 generated SF2/DLS exports are byte-for-byte identical.

### Bound physical duration conversion before casting

The tempo map converted an arbitrarily large floating-point tick duration to
an integer before clamping it. UBSan confirmed an out-of-range conversion for
the largest finite double. Saturate against the remaining `u32` duration
capacity before casting. Preserve the existing half-down rounding and invalid
input handling. Remove the per-change insertion-order field because the stable
sort already preserves insertion order for equal tick/sequence pairs.

Regression coverage includes tied tempo writes, redundant writes, exact tempo
boundaries, half-tick rounding near the maximum, huge finite durations, and
invalid durations. The UBSan reproducer now returns the maximum tick count
without errors. The full build and all 17 CTest targets pass. A broader rebuild
also exposed a shadowed delay variable in the earlier modulation refactor;
rename it, leaving the final build free of compiler warnings.

### Write SoundFont indexes from actual table positions

Build each preset/instrument table together with its bags, generators, and
modulators. Indexes come from the serialized record buffers; remove the five
instrument counting/global-zone helpers and the separate preset generator
prediction. An empty global zone is omitted based on the records actually
produced. This removes 122 production lines and eliminates the requirement to
keep counting and writing paths synchronized when export behavior changes.

Keep generator ordering, terminal records, 16-bit index checks, and shared
sample-map envelope variants. Add a regression that rejects an oversized
generator table. The full build and all 17 CTest targets pass without compiler
warnings. A before/after comparison across 4,608 configurations, each containing
mixed instruments, multiple regions, and an envelope variant, produces identical
bytes for all 9,216 SF2/DLS outputs.

### Retain decoded samples through SoundFont writing

Remove the SoundFont-only sample record and the pass that moved every decoded
sample into it. The sample-header writer calculates its frame offsets directly,
including padding, and checks their range before narrowing. Names, tuning, PCM,
and loop data continue to use the shared decoded-sample record. This removes
another type, an unused local-index field, and 34 production lines.

The full build and all 17 CTest targets pass without compiler warnings. Extend
the 4,608-configuration differential test to three sample lengths, multiple
sample references, loop offsets, tuning, and an empty name. All 9,216 SF2/DLS
outputs remain byte-for-byte identical to the earlier exporter.

### Make pan gain authoritative

Remove `PanPerformanceEvent::hasLinearGain`. Every producer already uses unit
gain by default, and both renderer consumers reset to unit gain when the flag
is absent. The flag adds no needed state; it only allows an explicitly supplied
gain to be silently ignored. Use the gain directly and combine the emitter's
pan overloads with a unit-gain default.

Extend MIDI regression coverage to check non-unit gain followed by omitted
gain, then non-unit gain followed by explicit unit gain, under both modulation
conversion policies. MP2k retains its asymmetric physical gain. The full build
and all 17 CTest targets pass without compiler warnings.

### Narrow the emitter to the operations formats use

Remove eleven event-object overloads whose only callers were the corresponding
scalar helpers. The scalar operations now append their event directly. Retain
structured overloads where formats use additional event options, and keep
quantized and unquantized controller calls distinct. Marker emission accepts
its text directly, simplifying the CPS and Sony PS2 call sites. This removes
61 production lines and eleven public overloads without changing the event
model or the scalar vocabulary formats already use.

The full build and all 17 CTest targets pass without compiler warnings.

### Share DLS articulation writing across scopes

Instrument and region modulation now pass through one local connection writer,
preserving instrument-before-region ordering. Filtering, scaling, and omitted
connections no longer have two implementations in the articulation builder.
Remove the DLS alias for the shared decoded-sample type as well.

The full build and all 17 CTest targets pass without compiler warnings. The
4,608-configuration comparison produces identical bytes for all 9,216 SF2/DLS
outputs, including instrument and region modulation together.

### Remove the repeat-state forwarding layer

`RepeatCounter` now operates directly on the VM's map of remaining plays. Remove
the private `RepeatState` wrapper and impossible null-state checks: counters
can only be constructed by `VmApi` with an existing map. Preserve the public
counter operations and map snapshots used for finite-repeat loop detection.
This removes one class and 31 production lines.

The full build and all 17 CTest targets pass without compiler warnings. Existing
VM coverage exercises finite repeats, nested calls and repeats, counter reuse,
repeat breaks, loop candidates, and section transitions.

### Remove unused sample-builder alias and retained-lookup APIs

Repository-wide call-site inspection found that `SamplePoolBuilder::alias` and
the finalized `SampleRefLookup` were used only in tests. Remove both features;
formats already resolve sparse keys during construction and retain concrete
`SampleRef` values. Finalization releases the temporary key map instead of
returning another object that every scanner discards. SNES catalog alias
resolution remains its own live, format-specific behavior.

Adapt builder tests to retain coverage of sparse and missing keys, duplicate
diagnostics, multiple source records per sample, references surviving
finalization, and stable draft views. The full build and all 17 CTest targets
pass without compiler warnings. This removes 50 production lines.

### Keep modulation export units out of general synth math

Move SoundFont/DLS frequency, timecent, delay-floor, and controller-range
conversions from `synth/SynthMath` into `export/synth/ModulationScaling`. Keep
physical amplitude, envelope, and pan helpers in general synth math. The MIDI
modulation normalizer now shares its conversion units with export lowering
directly. Keep the seconds-range helper private and simplify bounded timecent
conversion with a clamp.

The full build exposed a legacy modulation dependency on these helpers; update
its include while preserving those entry points and their behavior. The final
full build and all 17 CTest targets pass without compiler warnings. All 9,216
SF2/DLS outputs in the 4,608-configuration comparison remain byte-identical.

### Consolidate flat portamento lowering state

Make glide time optional in `PortamentoPerformanceEvent`: absent time retains
the current setting while a previous key can still trigger a transition. Remove
the separate control event and its duplicate renderer branch. Remove the
seven-bit time event, which had only a synthetic test producer. That test now
uses the physical settings event emitted by formats and verifies both timing
and the full 14-bit result. Format interpreters continue to emit structured
transition intent and physical settings.

The full build and all 17 CTest targets pass without compiler warnings. An
independent comparison of 2,592 MIDI exports covers current/restored timing,
linked notes, rendering hints and policies, physical timing modes, zero/short/
long durations, transposition, existing bends, and tempo changes. All bytes and
diagnostic counts match the previous implementation.

### Assemble ordinary sample-decoder output once

PCM8, PCM16, BRR, PSX ADPCM, Konami delta PCM, and OKI ADPCM now return PCM
vectors from their codec loops. One local adapter validates the source range
and assembles the unchanged rate, channel, and loop metadata. Keep predictor-
header parsing, waveform generation, and GBA resampling explicit where their
requirements differ. Combine the two Konami wrappers into one decoder whose
table directly identifies their one differing delta. Remove the unused source
argument from NDS waveform generation. This removes 54 production lines.

The full build and all 17 CTest targets pass without compiler warnings. A
50,400-result before/after comparison covers all codec enum values, empty and
truncated data, out-of-range and overflowing offsets, channels, rate defaults,
byte order, reverse playback, and loop metadata. PCM and metadata are identical.
The updated decoder was also compiled directly with AddressSanitizer, UBSan,
and float-cast-overflow checks for that comparison; no errors were reported.

### Reuse source-annotation construction and derive the latest parent

Fallback sample, instrument, and region annotations now use the same methods
as explicit annotations, sharing ownership, parent selection, and sample links.
Remove the separately retained latest instrument annotation: the ordered source
list already supplies it. Extend the instrument-builder test to check parent
selection after multiple instrument annotations. This removes 15 production
lines without changing the format-facing API.

The full build and all 17 CTest targets pass without compiler warnings,
including fallback ownership, sample links, explicit ranges, and annotation
parent tests.

### Use scan-builder defaults without forwarding overloads

Replace the forwarding constructor and collection overload with default
arguments. The existing collection implementation already supplies missing
resolver and identity values, so remove the separate default-key helper and
its extra name copy. This removes 18 production lines and keeps scanner calls
unchanged.

The full build and all 17 CTest targets pass without compiler warnings,
including collection identity and session resolution coverage.

### Represent note-envelope intent with one discriminator

Rename scalar automation's `Envelope` motion to `NoteEnvelope` and remove its
redundant `restartsOnNote` flag. All producers paired the envelope motion with
that flag; the other motions always left it false. The motion now communicates
the complete distinction and cannot disagree with a second setting. Keep the
separate LFO-context restart flag, which controls actual oscillator behavior.

Update the existing core and format assertions to check the explicit motion.
The full build and all 17 CTest targets pass without compiler warnings.

### Discover Prism patches directly from the sample directory

Prism's synth discovery already included every valid directory entry, making
the collected sequence-program set irrelevant after validation. Replace the
set union and second directory scan with one ordered traversal. Remove the
program-set plumbing from sequence decoding and two unused channel arguments;
runtime channel configuration remains in its existing typed settings.

Extend the scanner fixture to cover unreferenced patches, SRCN 255, duplicate
sample data, invalid addresses, and misaligned loops. The expanded discovery
test passes against the previous implementation as well as the simplification.
Retain source-instrument identity coverage through emitted instrument events.
The full build and all 17 CTest targets pass without compiler warnings.

### Let source identities supply ordinary export addresses

Remove explicit instrument addresses that duplicate the shared identity
resolver's sequential mapping in 11 formats: CPS1, GraphResSnes, HeartBeatPS1,
KonamiArcade, KonamiTMNT2, NamcoSnes, NinSnes, PrismSnes, SoftCreatSnes,
TamsoftPS1, and TriAcePS1. Remove the now-unused packed-address temporaries.
Keep explicit mappings where bank/program policy differs, including drum banks
and formats that retain an eight-bit program in bank zero. This removes 31
production lines and keeps routine target-address arithmetic out of formats.

The full build passes without compiler warnings. NinSnes tests initially
searched only explicit addresses; update their melodic checks to use resolved
addresses, while retaining checks for explicit drum-bank policy. All 17 CTest
targets pass, including synth export, instrument selection, and format suites.

### Initialize VM tracks through ordinary performance emission

Use `PerformanceEmitter` for initial track state instead of independently
constructing each event and assigning sequence numbers in a second pass.
Add its small mono-mode operation to cover the one initial event that lacked
an emission method. Initialization still emits only declared values, preserves
order, and emits master gain once per sequence. This removes 29 production
lines while sharing event construction and provenance rules.

Extend existing VM coverage for initial channel pan, the full eight-bit bend
range, and song-wide ordering across tracks. Verify that initial events have
track identity but no invented source command, source annotation, or automation.
The full build and all 17 CTest targets pass without compiler warnings.

### Remove duplicate MIDI oscillator and pitch-bend state

Use one LFO started flag: configuration and startup already occurred together
at every renderer boundary. Keep the emitted bend's stable event index and
read its value directly instead of maintaining a second cache. The track
retains insertion order throughout rendering, and only the bend writer sets
this index. Return the effective bend range from its emission helper rather
than retaining another copy. Remove the unused conversion-policy argument and
its forwarding through range refresh. This removes three state fields and 15
production lines.

The full build and all 17 CTest targets pass without compiler warnings.
Independent comparisons retain identical MIDI bytes and diagnostic counts for
3,456 LFO/controller scenarios and 2,592 portamento scenarios. The LFO matrix
covers all generated waveforms, phase/restart and delay policies, zero-depth
behavior, layered bends, repeated and same-tick bends, sensitivity changes,
channel pan resets, and both modulation export policies. The updated renderer
also passes that matrix when compiled directly with AddressSanitizer, UBSan,
and float-cast-overflow checks.

### Remove redundant sample references and unread format state

Use the SNES builder's existing reference entries to resolve canonical samples,
rather than maintaining a second vector containing the same references. Remove
the unread Sega Saturn velocity-table cache and Prism sequence-list address;
the actual velocity-table source field and sequence discovery remain intact.

Add shared-builder coverage for BRR aliases with matching and differing loop
positions, retained concrete references, and separate directory/payload source
annotations for every SRCN. The full build and all 17 CTest targets pass without
compiler warnings.

### Use the shared readers directly in format code

Delete five format-local bounds helpers that exactly duplicate ByteReader.has,
and use the shared method in HeartBeat PS1, Suzuki PS1, Sony PS1, and Sega
Saturn. Remove 25 remaining C-style discard casts around annotation reads;
these reads intentionally retain fields even when their values are unused.
The full build and all 17 CTest targets pass without compiler warnings.

### Share bounded byte-pattern searching

Let the shared masked-pattern scanner accept an exclusive end offset. Remove
the two local linear searches in KonamiArcade and KonamiTMNT2; both now use the
same anchored scanner as other formats. Bounds still require a complete match
and are capped by the available source bytes.

Extend scanner tests for exact end boundaries, crossing matches, empty and
reversed ranges, oversized limits, and all-wildcard patterns. The full build
and all 17 CTest targets pass without compiler warnings.

### Share ROM definition integer parsing

Give MAME ROM metadata one integer parser and remove the copies in CPS,
KonamiArcade, and KonamiTMNT2. Encryption keys use the same parser while
retaining their distinct missing/invalid-attribute diagnostics. Source formats
continue to own their address and range validation; decimal-only metadata in
other extractors retains its existing policy.

Extend ROM metadata coverage for decimal, hexadecimal, leading zeroes, the
unsigned limit, missing values, prefixes without digits, signs, whitespace,
partial values, and overflow. The full build and all 17 CTest targets pass
without compiler warnings.

### Resolve VM jump destinations and loop points in one path

Replace four jump helpers with one ordered implementation: resolve the target,
report any missing destination, then apply the explicit jump semantics. Normal
and finite branches stop there; candidate loops require a previous visit while
declared loops can begin at the current tick. Both loop forms share LoopPoint
construction and the existing loop policy. This removes 43 production lines
and three private methods without changing SequenceVM's execution model.

The full build and all 17 CTest targets pass without compiler warnings. An
independent before/after comparison produces identical serialized execution
traces for 13,824 programs covering all jump semantics, calls/returns, repeat
state, runtime overrides, missing targets, one to three tracks, loop inference,
all loop policies, and zero to two extra repeats. Traces include event ordering,
note timing, loop markers, source spans, and diagnostic text/ranges. The updated
VM also completes that matrix under AddressSanitizer, UBSan, and float-cast-
overflow checks.

### Let SNES catalogs own input normalization and empty-result checks

Accept sample-number ranges directly, with the existing projection reserved for
instrument records. Pass the collected vector by value into catalog assembly
so it can sort in place; moved and projected inputs no longer need a second
copy. NinSnes and NamcoSnes use integer ranges for complete directory scans,
and SoftCreatSnes passes its existing set. Remove redundant empty-input guards
from 16 formats; their existing empty-catalog check covers the same case. This
removes 51 production lines without another format-facing abstraction.

Extend the shared test across mutable/const/moved vectors, spans, sets, integer
ranges, and empty projected records. Verify that retained inputs are unchanged,
empty inputs read no source bytes, and all forms retain identical ordering and
validation. The full build and all 17 CTest targets pass without compiler
warnings.

### Retain miscellaneous assets only where binding uses them

Remove the unused miscellaneous-asset list from BoundCollection. Selected
miscellaneous assets are still resolved, validated, and exposed to the format
binder, and the retained snapshot keeps their payloads alive. Exporters need
only the resulting bound instruments and sequence runtime, so they no longer
receive a second, unused view of the binding inputs.

The existing typed-miscellaneous-asset binder test verifies that its selected
payload is available during binding. The full build and all 17 CTest targets
pass without compiler warnings.

### Apply modulation conversion policy during shared synth preparation

Lower modulation under the requested conversion policy before the SF2 or DLS
writer receives it. Remove two destination-filter functions and the conversion
argument threaded through both writers' chunk assembly. Sequence simulation
keeps fixed no-boost tremolo attenuation while discarding oscillator and
controller records, as before. This removes 60 production lines.

Filtering before SF2 layout also lets presets whose only difference was
suppressed modulation share an instrument table. Regression coverage checks
instrument/region attenuation, controller suppression, and distinct native
modulation versus shared simulation presets with preserved envelope offsets.

An independent 21,504-file comparison covers absent/present vibrato and tremolo,
all six waveforms and unspecified shape, fixed/controller depth, gain modes,
fixed/varying rates, delays, both conversion/scaling policies, and observed
controller maxima of zero, 38, and 127. All 10,752 DLS files and 5,928 SF2 files
are byte-identical. The other 4,824 SF2 files have identical resolved preset
generators/modulators, sample data/headers, and metadata, with smaller shared
instrument tables. Directly instrumented synth preparation/lowering/writers
pass AddressSanitizer, UBSan, and float-cast-overflow checks and reproduce the
updated bytes. The full build and all 17 CTest targets pass without warnings.

### Construct source annotations through one entry point

Make SourceMapBuilder::annotation own construction directly; remove the private
add/allocateId forwarding methods and let field/pointer helpers use their
ordinary fluent operations. Share envelope-stage labeling in one local helper,
preserving stage order, omitted optionals, infinite-stage booleans, and physical
seconds. This removes 24 production lines without another public abstraction.

Add coverage for empty envelopes, zero sustain/attack, finite stages, infinite
stages, derived ranges, and display hints. Existing source-map tests cover
custom allocation, duplicate IDs, fields, hierarchy, and links. The full build
and all 17 CTest targets pass without warnings.

### Allocate variant addresses with one forward scan

Replace repeated searches from bank zero with a reservation bitmap and a
monotonic cursor through the fixed 128-by-128 portable address space. Each
address is examined at most once across the whole materialization, and
allocation no longer grows a tree of assigned address pairs. This is an
algorithm simplification rather than a line-count reduction (two additional
production lines). Preserve both existing bank projections and program clamping.

A before/after matrix returns identical addresses for 65,536 allocations with
mixed bank/program and source-identity reservations. Add an integration test
for sparse remaining addresses, reservations through MIDI/DLS and SF2 bank
projections, clamped programs, the last portable address, exhaustion diagnostics,
and fallback to the base instrument. The full build and all 17 CTest targets
pass without warnings.

### Share synth artifact preparation and borrow standalone banks

Use one synth artifact wrapper for SF2 and DLS. Assemble export inputs directly
there and derive filename/media type from the selected format for both success
and failure, removing the extra input adapter and duplicated export wrappers.
Standalone export borrows the immutable bank from its snapshot instead of
copying every instrument, region, and local sample before read-only export.
This removes 26 production lines. The full build and all 17 CTest targets pass
without warnings, including standalone, collection-bound, selection, failure,
modulation-policy, and playback preparation coverage.

### Share Sony PS2 two-byte channel-message decoding

Handle Sony's compressed A0 dictionary note before the ordinary two-byte
channel-message family, removing its duplicate data-byte reader branch.
Uncompressed polyphonic pressure still consumes both bytes and permits later
commands; running status and omitted-delta flags retain the same read order.
This removes 11 production lines. The full build and all 17 CTest targets pass
without warnings, including the Sony PS2 MIDI decoding fixtures.

### Establish record-reader bounds once

Normalize the record window in its constructor so begin <= position <= end <=
source size, then remove six redundant bounds conditions from sequential,
positioned, raw-byte, and peek reads. Reversed windows become empty at their
bounded starting offset, and offsets beyond the source become empty at EOF.
This prevents an observed reversed-record size of 4,294,967,294 bytes and an
exception from rawBytes on an out-of-source empty window. Valid-window behavior,
sticky sequential failure, positioned recovery, and diagnostic deduplication
remain unchanged. Positioned reads reject relative offsets outside the window
before address arithmetic, and successful/truncated reads share their cursor
update. This removes three production lines and the separate overflow path.

Regression tests exercise normal, reversed, empty, out-of-source, and integer-
limit windows. A direct reader build passes AddressSanitizer and UBSan across
82,944 windows and 1,990,656 mixed operations, checking cursor, field, finished
record, and diagnostic ranges after every operation. The consolidated positioned
read path also reproduces identical serialized cursor states, fields, and
diagnostics across that matrix. The full build and all 17 CTest targets pass
without warnings.

### Retain lowered modulation as one value

Resolved synth instruments and regions now retain LoweredSynthModulation
directly instead of unpacking and redeclaring its two vectors. Their writers
consume that concrete type, and SF2 layout compares it as a value. This removes
five production lines, two temporary unpacking steps, and an implicit generic
scope requirement. All 21,504 synth outputs are byte-identical to the preceding
modulation-policy revision. The full build and all 17 CTest targets pass without
warnings.

### Store loop visits without duplicating command identity

Loop detection now stores each state's visit tick directly. The loop start's
source command comes from the replay destination, which already identifies the
visited command, eliminating the separate VisitRecord type and its duplicate
command field. Defaulted comparisons replace manually repeated field lists for
track and playlist visit states. This removes ten production lines.

All 13,824 before/after VM traces are identical across jump semantics, loop
policies, repeat counts, calls, missing destinations, and multiple tracks,
including event provenance, source spans, markers, and diagnostics. A direct
VM build passes the same matrix under AddressSanitizer and UBSan. The full
build and all 17 CTest targets pass without warnings.

### Keep one precise modulation maximum

Represent observed modulation maxima as optional normalized values. Remove
MidiModulationMaximum and its independently maintained controller byte; derive
that byte only when export scaling needs it. Collection stitching now merges
ordinary optional values, and one private MIDI scaler handles both precise
source amounts and already-quantized controls. This removes 51 production lines
and the synth fallback for manually populated controller-only maxima.

All 9,312 before/after modulation scenarios are identical, including all 127
MIDI rounding boundaries and adjacent floating-point values, absent and zero
maxima, all modulation targets, both scaling policies, and precise versus
quantized controls. The direct analysis/scaling build passes that matrix under
AddressSanitizer and UBSan. All 21,504 SF2/DLS outputs are byte-identical. Add
regression coverage for sub-byte maxima, zero versus unobserved controls, and
the shared MIDI/synth headroom decision. The full build and all 17 CTest targets
pass without warnings.

### Decode directly into the final synth sample table

Combine sample decoding, phase inversion, and sample-index construction in one
pass. Remove the separate materialization pass and the owner/index fields it
required on every decoded sample. Copy PCM only when both polarities are
retained; an inverted-only sample moves into its final slot. Referenced samples
are a set, replacing hand-written deduplication and linear membership scans.
Internal pool views now borrow a required pool reference. This removes 38
production lines while retaining sample ordering and partial-export behavior.

All 1,536 before/after scenarios produce identical prepared sample tables,
region indexes, diagnostics, and SF2/DLS bytes. These cover local/external pools,
repeated inputs, null input views, missing samples/sources, stereo restrictions,
selection/filtering policies, and both polarities. A direct preparation build
passes the matrix under AddressSanitizer and UBSan. Add a regression test for
owner-qualified phase references, inverted-only and shared originals, PCM
saturation, unused invalid samples, and stable table order. The full build and
all 17 CTest targets pass without warnings.

### Reduce collection queries to the information callers use

Collection removal asks directly whether any member is being removed, replacing
an optional asset result that no caller consumed. Missing-role diagnostics use
one value construction with their existing conditional severity and message.
This removes 20 production lines and repeated control flow. The full build and
all 17 CTest targets pass without warnings, including source/asset removal,
collection reconciliation, and missing-reference diagnostic coverage.

### Simplify extraction ownership and queueing

Give archive handles their existing library close functions directly as
unique_ptr deleters, removing three custom closer types and their redundant
null checks. Queue extracted children directly: SourceStore assigns each one a
fresh ID, so the separate queued-ID set could never reject a child. Remove its
plumbing and the single-use child-admission wrapper while preserving persistent
scanned-source tracking. This removes 39 production lines. Add the stitching
test's missing direct set include exposed by the narrower Session header. The
full build and all 17 CTest targets pass without warnings.

### Express arcade decryption through shared bit operations

Replace Kabuki's two hand-expanded bit-pair permutations with one four-pair
loop whose key order is explicit at each call. Use standard fixed-width
rotations for Kabuki and CPS3 instead of two more local helpers. This removes
28 production lines. Before/after comparison passes 41,943,040 permutation
checks under AddressSanitizer and UBSan, covering every effective key and
selector, both key orders, zero/all-one bytes, and each source bit independently.
The full build and all 17 CTest targets pass without warnings, including the
existing Kabuki and CPS3 address-path fixtures.

### Use explicit chip-voice visitors

Annotation and validation visitors now accept the concrete supported voice type
instead of introducing type aliases and conditional template branches. Adding
another voice alternative will require an explicit handler rather than silently
skipping its annotations or validation. CPS and Konami TMNT2 assign voice values
directly without spelling the variant wrapper. This removes 12 production
lines. The full build and all 17 CTest targets pass without warnings.

### Write MIDI tempos directly from the global map

Write the conductor track's tempo events in one pass over PerformanceTempoMap,
removing the separate source-track insertion path, duplicate-output bitmap,
event-membership query, and retained track identity. This removes 29 production
lines and fixes simultaneous changes from different tracks being written in an
order that disagreed with the tempo used for physical duration calculations.
The conductor's end tick now also covers every retained tempo point.

The 2,304-scenario comparison found 1,024 ordering disagreements before this
change and none afterward. Non-tempo MIDI bytes remain identical. The direct
renderer/tempo-map build passed that matrix under AddressSanitizer and UBSan.
Add a regression for cross-track ordering, repeated values, initial tempo, and
conductor duration. Update the Capcom SNES fixture to locate tempo metadata
directly while preserving its note/controller ordering checks; its old fixed
index caused the initial core-test crash. The full build and all 17 CTest
targets now pass.

### Restore the AKAO sustain fixture's sample identity

The resumed audit's unchanged baseline aborted in the new sustain-only AKAO
fixture: its articulation omitted the sample-pool ID required by a resolved
sample reference. Supply a valid fixture ID without changing production code
or the sustain assertions. The full build and all 20 current CTest targets pass.

### Store the tempo map in its final point representation

Build the tempo map's owned points directly from temporarily sorted source-event
references. Remove the separate Change type, retained execution-order field,
deduplication state, and repeated point conversion. Include implicit initial
tempo once during construction. This removes 30 production lines while keeping
the public value-returning points API safe for temporary maps.

All 13,824 before/after cases produce identical tempo points, tick durations,
tempo lookups, and physical-time conversions. The matrix includes equal-order
events, multiple tracks, repeated values, zero tempo/PPQN, initial tempos, and
large tick/time bounds, and passes with direct tempo-map instrumentation under
AddressSanitizer and UBSan. Add regression coverage for source-event lifetimes,
implicit/explicit initial tempo, and the first explicit repeated value. The
full build and all 20 CTest targets pass without warnings.

### Register runtime hooks beside their implementations

Keep optional lifecycle-hook registration inside CompiledCommandRuntime and
implement the four small hooks directly at their registration sites. Remove
the separate templated installer and forwarding methods for tick, prepass,
section start, and performance finalization. The format-facing construction
API and optional-hook behavior are unchanged. This removes 23 production
lines. The full build and all 20 CTest targets pass without warnings, including
compiled prepasses and the formats that use section and finalization hooks.

### Remove unused pitch-transition voice identity

Remove NoteSpan's pitchBendVoice field, its initialization/propagation, and the
obsolete comment describing its former purpose. No code read this identity;
MIDI voice linking and range planning already use the continuation and bend
state. This removes five production lines. The full build and all 20 CTest
targets pass, including linked and mixed-mode pitch-transition regressions.

### Share ordered collection of global performance events

Tempo, transposition, and time-signature handling now borrow typed events
through one orderedPerformanceEvents helper. Remove the renderer's separate
transpose record and two collection/sorting implementations. Convert time
signatures directly when writing the conductor. This removes 34 production
lines and fixes simultaneous cross-track transpose/meter changes previously
ordered by track enumeration rather than execution sequence.

An independent MIDI parser checks 1,152 before/after scenarios against expected
global order: 512 previously disagreed and none disagree now. All 384 cases
without cross-track tick collisions remain byte-identical; events other than
transposed note keys and time signatures are unchanged throughout. The direct
renderer/tempo-map build passes under AddressSanitizer and UBSan. Add a focused
regression for distinct and equal sequence values across tracks. The full build
and all 20 CTest targets pass without warnings.

### Declare a region's source range once

Remove 21 duplicate range assignments across 17 format synth builders. Each
affected region immediately supplies the same range through its sole source
annotation call, which already records the durable range even without a source
map. The source call is now its single definition. This removes 19 production
lines without adding a builder API. The full build and all 20 CTest targets
pass, including range derivation with annotations disabled and format source
inspection coverage.

### Search sorted tempo and transpose points directly

Use upper_bound for point-in-time tempo and global-transpose lookup, selecting
the last change at or before the requested tick. This replaces two prefix scans
with logarithmic searches and removes 10 production lines. A linear reference
agrees on 1,572,864 tempo queries, including empty maps, repeated ticks, initial
values, and large ticks. All 1,152 MIDI scenarios remain byte-identical. Direct
changed-component builds pass under AddressSanitizer and UBSan, and the full
build and all 20 CTest targets pass without warnings.

### Preserve source voice duration limits through MIDI lowering

Create native-portamento fragments by copying the source note and overriding
only their timing, pitch, and continuation flags. This removes eight lines of
manual field copying and preserves source attributes previously omitted from
generated notes, including the physical duration limit.

MIDI rendering now collects absolute hardware stop times from original note
events and applies one boundary to all fragments of a continuing voice. This
also covers limits declared by later events sharing a note identity, which
portamento lowering merges. Extensions cannot bypass a timer or recreate an
expired voice; a tighter continuation limit shortens all overlapping fragments,
and a genuine new attack resets the boundary. Source performance data remains
unchanged. The additional renderer state handles correctness missing from the
former per-note clamp without adding format code or public model fields.

Add regressions for tempo changes, zero limits, delayed slides, fresh attacks,
same-pitch extensions, linked transitions, and later limits with overlapping
fragments. All 20 CTest targets pass. Direct renderer/lowering builds pass under
AddressSanitizer and UBSan across 3,458 physical-limit scenarios, including
unidentified notes and saturated end ticks. All 864 uncapped before/after MIDI
scenarios remain byte-identical.

### Construct and validate emitted headers once

PerformanceEmitter now chooses the command or automation's source header and
then stamps its tick and execution sequence in one place. Append uses that
validated header before updating the automation lifetime, removing a duplicate
binding check. This removes eight production lines while preserving source
attribution, ordering, and invalid-binding rejection. The full build and all
20 CTest targets pass, including automation provenance, motion interruption,
and VM initialization ordering coverage.

### Track used synth instruments as a membership set

Replace the temporary used-instrument list with a set. Insertions now deduplicate
directly, and final filtering uses contains instead of another linear search.
The separate bank-ordered instrument list still determines export order, and
identity/address fallback retains its existing all-match behavior. This removes
two production lines and the manual duplicate-check branch. The full build and
all 20 CTest targets pass, including used-only SF2/DLS preparation and semantic
instrument selection.

### Read source-link offsets without a temporary address wrapper

Remove the single-use operandAddress helper from command source projection.
Its caller now checks the unsigned operand directly when creating a source
range, preserving the rejection of non-address values without constructing
and immediately unwrapping an optional Address. This removes seven production
lines. The full build and all 20 CTest targets pass, including encoded/resolved
operand fields and call, jump, and repeat source links.

### Keep one address search per instrument-selection policy

Used-instrument filtering and variant materialization now resolve their fallback
address before using the ordinary address search. Remove the duplicate search
bodies, the private selector/public forwarding pair, and the redundant list
alias. Callers now reach the implementation through selectSynthInstruments.
This removes 12 production lines while keeping the policies distinct: filtering
retains every matching instrument in bank order, variants choose the first with
address fallback, and ordinary performance lookup requires an exact identity.
A new regression covers these differences with duplicate and conflicting
identity/address matches. The full build and all 20 CTest targets pass.

### Start timing queries at the relevant tempo point

Physical-duration conversion now obtains the starting tempo through the shared
lookup and begins iteration at the first subsequent change. Remove the repeated
prefix scans and their per-point before-start branch. Sampled pitch lookup also
uses the preceding sample directly after upper_bound; a separate end-of-curve
branch returned the same value. Together these remove 14 production lines.

All 312,320 before/after timing and sampled-pitch query results are byte-identical,
including empty and repeated points, zero tempos/divisions, half-tick rounding,
extreme durations, and saturated tick boundaries. The direct changed-component
build passes under AddressSanitizer and UBSan. The full build and all 20 CTest
targets pass.

### Let stable sorting preserve modulation source order

Remove the original event index from tempo-relative modulation's EventRef.
Timeline construction already visits tracks and their events in order, so
stable sorting by tick and sequence preserves the same tie order without
storing and comparing those positions. Use a range loop to collect relevant
events directly. This removes seven production lines and one temporary field.
The full build and all 20 CTest targets pass, including cross-track tempo
changes, independent modulation layers, and physical delay policies.

### Make note-end status optional for format authors

Remove nodiscard from the two mutating note-end setters. Every production format
intentionally discarded their boolean result, so the annotation required noise
without exposing an unhandled format decision. Remove 25 discard casts across
18 files in 17 formats; calls now read directly as setNoteEnd or setPreviousNoteEnd.
The return values and implementation are unchanged, including the ability to
check whether a previous note existed. The full rebuild is warning-free and all
20 CTest targets pass. Verify that each format edit only removes its outer cast.

### Keep one temporary default-transition state in the compiler cursor

Replace the cursor event's partial CommandFlow and separate declaration flag
with one optional CommandTransition. Assemble the durable flow record, including
its continuation address, only when the command is finished. Truncation still
discards executable behavior and forces an end transition. This removes five
production lines, one temporary flag, and the unused intermediate continuation.
Strengthen the ignored-command fixture with a declared jump so it verifies that
ignore clears control flow as well as behavior and presentation. The full build
and all 20 CTest targets pass, including conflicting transitions and truncation.

### Reuse the reader's ranged value for encoded command fields

EncodedSemanticField now extends RangedValue with only its source-field name
and display policy. Remove the duplicate value/range/validity members and the
compiler helper that copied them individually. Raw field readers return the
reader result with its presentation metadata directly, while format callers
retain the same value, range, and validity access. This removes 15 production
lines. The full rebuild and all 20 CTest targets pass, including raw/resolved
source fields and truncated command handling.

### Preserve the full internal clock in physical timing

Remove four 16-bit narrowings from shared tempo and modulation calculations.
Timebase already supports a 32-bit internal PPQN independently of the requested
MIDI division; physical timing must use that full value. The new regression
fails before the fix and verifies tick duration, forward/reverse duration
conversion across tempo changes, and modulation rates/delays at divisions of
100, 65,536, 131,072, and 2^30 with a separate MIDI division of 48. Source-layout
fields that actually encode 16-bit divisions remain unchanged.

The full build and all 20 CTest targets pass. The existing 312,320 timing and
sampled-pitch queries remain byte-identical for divisions through 65,535, with
the changed PerformanceModel compiled under AddressSanitizer and UBSan.

### Remove duplicate visit and tick bookkeeping from SequenceVM

Use try_emplace to record command and playlist visits while retrieving any
earlier visit. This removes the separate lookup/insertion paths without
changing which arrival kinds count as loops or how repeat state distinguishes
visits. Source spans now reuse the executor's saturating tick calculation
instead of repeating it locally. This removes 12 production lines and adds
no new VM concepts. The full build and all 20 CTest targets pass, including
finite repeats, inferred/declared loops, playlists, and source playback spans.

### Read packed 24-bit values through the shared byte reader

Add le24 and be24 beside the existing endian-specific integer readers. Replace
13 manual reads across CPS, HeartBeatPS1, KonamiArcade, KonamiTMNT2, NDS, and
SonyPS1, removing two format-local reader helpers. RecordReader's 24-bit read
now uses its ordinary numeric path with an explicit three-byte width, removing
the separate bounds/range/field bookkeeping. This removes 11 production lines
overall while making format layouts read directly as integer fields.

Coverage verifies both byte orders, the unsigned maximum, exact end boundaries,
truncated and overflowing offsets, and record-local bounds and source fields.
The full rebuild is warning-free and all 20 CTest targets pass.

### Validate bound bank identities against the immutable snapshot

Remove the temporary vector of copied bank IDs and format strings from
collection binding. Original metadata remains available in the snapshot under
the fixed collection member order, so validation can compare with that source
directly. Missing members still prevent binding, and callbacks receive a span
that cannot resize the prepared bank list. This removes three production lines,
one temporary container, and its string copies. The full build and all 20 CTest
targets pass, including binding failures and rejection of identity changes.

### Share PlayStation exponential-envelope rate selection

Replace three eight-case switches with one amplitude-band rate-offset table.
Decay and release select their decrement directly; sustain also calculates
the lower band boundary instead of listing it in every branch. Remove the
decay-rate correction that cannot trigger for the register's four-bit field.
This removes 86 production lines without changing the emulated steps or the
format-facing envelope API.

All 3,200 before/after PS1/PS2 envelopes match bit-for-bit, including every
attack rate/mode, decay rate/sustain level, sustain rate/mode/direction, and
release rate/mode, plus mixed register values. Both versions and the shared
synth math are compiled with AddressSanitizer and UBSan for the comparison.
The full build and all 20 CTest targets pass.

### Construct SNES envelopes directly in the shared model

Remove SnesEnvelopeSeconds, its private conversion entry point, and the second
pass that normalized and copied every field. Construct Envelope directly and
represent unbounded stages as infinity where they are known, eliminating the
temporary negative-duration sentinels. Compute native key-off release once
from the retained release level. This removes 62 production lines, one
intermediate representation, and two private helpers.

All 131,072 before/after envelopes match bit-for-bit under AddressSanitizer and
UBSan. The comparison covers every ADSR register combination, every GAIN mode
and rate, and GAIN independence while ADSR is enabled. The full build and all
20 CTest targets pass.

### Let format code use shared envelope helpers directly

Remove the forwarding driverEnvelope functions and declarations from Compile,
GraphRes, and Hudson SNES. Their call sites now use snesDspEnvelope directly,
with formerly implicit zero GAIN values visible where needed. Keep wrappers
that perform actual driver-specific normalization. CPS also relies on the
shared fade conversion's existing infinity handling instead of branching
around it, and its local stage helper no longer accepts an unused policy flag.
This removes 21 production lines and three format-facing helper names. Update
the Hudson fixture to compare native release with its distinct GAIN release
instead of testing a deleted forwarding function. The full build and all
20 CTest targets pass.

### Avoid an unnecessary integer limit in SNES GAIN evaluation

Keep the elapsed hardware-update count as a rounded double instead of narrowing
it to u64. The envelope reaches an endpoint within 2,047 updates, so a larger
finite interval needs no additional integer representation or saturation path.
This also fixes a sanitizer-confirmed floating-to-integer overflow when the
elapsed interval produces an infinite intermediate count.

Add coverage for all four GAIN modes and 32 rates at the largest finite double,
including stopped counters, plus fractional/complete hardware periods. The
direct sanitized comparison preserves 30,720 ordinary GAIN queries and verifies
all 128 large-interval cases; the 131,072 envelope comparisons still match.
The full build and all 20 CTest targets pass.

### Keep SoundFont sample-index limits out of shared synth preparation

Remove the shared 16-bit sample-index clamp. Prepared sample references now
retain 32-bit indexes, which DLS writes directly; SoundFont uses its existing
checked table-index writer at the actual serialization boundary. Previously,
every sample after index 65,535 silently referenced sample 65,535 in both
exporters. This removes three production lines and one helper while correcting
the export-layer separation.

A regression fails before the fix and verifies distinct PCM at sample 65,536,
the emitted DLS wave link, SoundFont rejection of that unrepresentable link,
and successful SoundFont output at index 65,535. The full build is warning-free
and all 20 CTest targets pass.

### Share the identical NDS and OKI ADPCM nibble arithmetic

Use one magnitude calculation and index-adjustment table for NDS IMA and OKI
ADPCM. Their step tables, predictor widths, clipping rules, nibble order, and
output scaling remain explicit in their respective decoders. Replace the NDS
PSG duty switch with its eight-value table. This removes 33 production lines.

The before/after decoder comparison matches PCM and metadata exactly for
363,280 NDS ADPCM cases, 2,048 OKI cases, and 240 PSG cases. Coverage includes
every predictor index and packed byte, predictor extrema, long streams,
empty input, masked duty parameters, and sample-rate defaults. Both decoder
versions and their shared synth dependencies were compiled with AddressSanitizer
and UBSan. The full build and all 20 CTest targets pass.

### Derive sample bit-depth metadata from the codec

Remove Sample::bitsPerSample and 18 repeated assignments in format and platform
code. Its only consumer was source annotation; the existing codec-name lookup
now supplies the matching inspector bit depth as well. Format authors specify
the codec once, avoiding inconsistent values such as an eight-bit PCM sample
retaining the old sixteen-bit default. Existing codec display conventions are
preserved, and decoding and container output do not consult this metadata.
This removes 18 production lines and one independently maintained model field.

The existing sample-builder fixture now checks eight-bit metadata without a
separate bit-depth assignment. The complete 310-step rebuild is warning-free
and all 20 CTest targets pass.

### Remove redundant pitch-slide activity flags

AKAO SNES now uses its remaining step count to determine whether a slide is
active. Suzuki SNES uses its retained automation binding, which is cleared on
completion or interruption. Remove both separately maintained activity flags,
their synchronization branches, and guards already established by the callers:
AKAO starts with a valid pitch base and positive step count, while Suzuki's
three activation paths all supply a valid note and nonzero duration. This
removes 16 production lines without merging source arithmetic and output
bindings, which still serve distinct roles.

The full build is warning-free and all 20 CTest targets pass, including AKAO's
sampled driver curve and Suzuki's automatic portamento, slides across ties,
repeated slide continuation, and interruption fixtures.

### Share synth LFO timing and depth lowering

Consolidate vibrato/tremolo timing generators, timing modulators, and
fixed-versus-controller depth selection inside lowerSynthModulation. Keep
channel-pressure routing, tremolo attenuation, and record order explicit.
Sequence-event simulation now constructs only the retained static no-boost
attenuation instead of building and then removing the other records. This
removes 49 production lines without adding a format-facing API.

All 313,600 before/after comparisons preserve exact ordered generator and
modulator vectors under AddressSanitizer and UBSan. Coverage combines absent
and present LFOs, all waveform choices, fixed/controller depths, both gain
modes, constant/varying rates and delays, zero/nonzero depths, and both export
policies. The full build is warning-free and all 20 CTest targets pass.

### Resolve BRR aliases directly while constructing sample references

Remove the catalog's index and canonicalIndex methods. Their production caller
already has the current sample; it now searches only preceding samples and
reuses the corresponding concrete reference. The catalog becomes a plain
decoded value, and alias construction loses an extra SRCN lookup and two
optional-index branches. Data/loop identity and per-SRCN annotations remain
unchanged. This removes 20 production lines.

The Tales loop-position regression now parses real directory bytes and checks
the references returned by sample construction. Shared-builder coverage also
checks separate annotations for aliases and missing SRCNs. The full build is
warning-free and all 20 CTest targets pass.

### Use signed byte reads directly in format fields

Replace 63 immediate unsigned-byte-to-signed-byte casts across 16 format files
with the existing ByteReader::s8At. Signed tuning, pan, envelope, and DSP fields
now state their representation at the read. Keep casts that implement wrapping
arithmetic and reads through Prism's custom runtime memory unchanged. No new
reader API or source semantics are introduced; shorter expressions also remove
five production lines after formatting.

The full build is warning-free and all 20 CTest targets pass.

### Calculate MIDI stereo compensation directly

Remove redundant hard-left and hard-right branches from lowerStereoBalance:
its angle calculation already produces the exact endpoint positions. Equal
channels still select center, including silence. Compute the combined MIDI
channel gain directly instead of retaining two temporary channel amplitudes,
and remove the unreachable zero-gain fallback. This removes 16 production
lines while preserving quantization and gain compensation.

All 1,329,449 before/after comparisons match pan and gain bit-for-bit under
AddressSanitizer, UBSan, and float-cast-overflow checks. Coverage includes every
signed eight-bit channel pair at four scales, constant-sum positions, MIDI pan
rounding boundaries, signed zero, subnormal and extreme finite values, and one
million deterministic random pairs. Existing renderer tests cover pan gain
headroom, phase-inverted channel magnitudes, and expression independence.
The full build is warning-free and all 20 CTest targets pass.

### Derive Saturn instrument pan directly from hardware channel gains

Replace the SegSat direct-output parser's MIDI pan quantization and compensation
with the physical equal-power angle and gain. Decode DIPAN's four attenuation
bits as one 3 dB step count. This removes 45 production lines and the parser's
dependency on MIDI pan resolution. It also fixes off-center regions: the old
code stored a linear balance fraction while compensating for a different,
quantized pan angle. Retain centered-voice normalization and silent-side and
direct-level semantics. Clarify the existing equal-power Region::pan contract.

An exhaustive scan regression reconstructs both output channel gains for all
256 DISDL/DIPAN combinations, including mute, both endpoints, and every level.
It fails on the old implementation at direct-output byte 33. The full build
is warning-free and all 20 CTest targets pass with the correction.

### Finalize synth source annotations in one pass

Combine fallback-source creation and final property annotation in each synth
builder. Each value now gets its fallback, if needed, and derived fields in
the same traversal. Property projection only appends fields, so annotation
allocation order, IDs, parent links, and sample links remain unchanged. Final
projection still occurs after all format-authored source fields and regions.

The sample builder now stores its per-sample source lists directly instead of
wrapping each list in an otherwise empty EntryState. Together these changes
remove 29 production lines, two private methods, and one state type. Explicit
ranges, multiple source records, source-free derived values, and detached
builders retain their existing behavior.

Existing synth-builder tests cover fallback and explicit records, ownership,
sample links, final region counts, sparse keys, and detached construction. The
full build is warning-free and all 20 CTest targets pass.

### Give unknown source quantization one representation

ValueQuantization already defines zero levels as continuous or unknown. Remove
the optional wrapper from level/expression events and MIDI rendering state;
an absent wrapper and a present zero had identical behavior. Give the emitter
and compiler cursor default quantization arguments instead of retaining three
extra overloads. Format calls remain unchanged, and native level counts remain
explicit values independent of the destination's controller resolution.

This removes 18 production lines and two test lines. Existing assertions now
inspect the native level count directly; no additional test code is committed.
A temporary check confirms identical MIDI bytes across automatic and forced
volume/expression resolutions, both modulation modes, unknown quantization,
the 128-level boundary, and larger native scales. It also checks returning to
unknown quantization after a precise source controller.
The full build is warning-free and all 20 CTest targets pass.

### Route pan LFO simulation through one path

Pan LFOs are simulated for both modulation conversion policies. Handle that
target before the policy split instead of implementing it in both branches.
The remaining simulation branch handles only tremolo; vibrato routing and
source-controller output keep their existing rules. This removes 14 production
lines and the duplicate pan configuration/depth-update path.

A temporary before/after comparison produces identical MIDI bytes with all six
modulation targets, both policies, primary and additional pitch layers, physical
and normalized depths, and automatic/forced controller resolutions. Existing
pan-law and modulation regressions pass. The full build is warning-free and all
20 CTest targets pass; no test code is added.

### Keep source-range query rules on SourceRange

Move containment, point lookup, and intersection rules onto SourceRange. Source
maps and source inspection now share the same source-aware queries instead of
maintaining four local implementations. Range containment still includes empty
ranges at the end boundary, while byte queries remain half-open and match
zero-size anchors. Invalid and foreign sources retain their existing behavior.

This removes 20 production lines. A temporary sanitized comparison checks all
four previous implementations against the shared methods over 21,609 range
pairs, including empty ranges and extreme offsets. The full build is warning-free
and all 20 CTest targets pass; no test code is added.

### Construct MIDI instrument selection once

Resolve the selected instrument once, adjust the address for source identities,
then construct the MIDI selection in one place. This removes three parallel
result constructions and the unnecessary address resolver call for an already
explicit bank/program. Exact identity lookup, first-match behavior, missing
instrument fallback, forced bank selection, and pitch-bend ranges are unchanged.

This removes 10 production lines. A temporary sanitized comparison checks
1,160,250 selections, including null banks, duplicate identities, explicit
addresses, unknown identities, and extreme bank/program values. The full build
is warning-free and all 20 CTest targets pass; no test code is added.

### Keep scan draft operations beside their public methods

Put sequence assignment, miscellaneous payload assignment, and synth builder
access directly in the draft methods. Remove six private ScanResultBuilder
methods that only forwarded those operations to the same result-owned slots.
Draft creation now uses the public synth accessors, and SourceRange::include
already ignores invalid initial ranges without an extra guard.

This removes 32 production lines without changing the format-facing API, slot
lifetime, creation order, duplicate-assignment checks, or the validation pass
before materialization. Existing registry and synth-builder tests cover draft
construction, incomplete drafts, empty assets, and retained views across result
growth. The full build is warning-free and all 20 CTest targets pass; no test
code is added.

### Read executing-command metadata without format-local indexes

Expose the current command's SourceRange through VmApi. NinSnes and RareSnes
now use that metadata directly for sequence-defined instruments instead of
building address-to-range maps over every parsed command. Their program state
constructors need only format settings. Rare presets retain a SourceRange when
later reuse requires it, and format commands no longer capture and forward
their own source address through playback methods.

This removes 17 production lines, two runtime maps, and their initialization
walks. It also corrects source attribution when Battlemaniacs melodic and
percussion tracks interpret the same address as different-length commands;
the old global address map retained only the first interpretation. Untouched
default patches now remain unattributed instead of borrowing command zero's
range. Playback still reads no source bytes.

A 13-line regression added to the existing Rare opcode test fails with the old
map and passes with the current-command range. The full build is warning-free
and all 20 CTest targets pass.

### Make synth range accumulation explicit at each call

Remove the two recordRange wrappers that dispatch between included and observed
ranges using a boolean. Builders now call SourceRange::include on the named
range directly. This removes ten production lines and makes the distinction
visible at each update without another helper or boolean argument.

Explicit-range precedence and instrument/region range ownership remain unchanged.
Existing synth-builder tests cover those rules, invalid/foreign ranges, fallback
records, and detached builders. The full build is warning-free and all 20 CTest
targets pass; no test code is added.

### Express delayed output through the emitter

Add PerformanceEmitter::after(ticks), using the emitter's existing saturating
tick arithmetic and copied output view. SonyPS1, HeartBeatPS1, SonyPS2, and
SegSat no longer calculate output ticks in local helpers. Remove three local
aliases of Effects::wait as well. Delayed output retains its source command,
automation binding, active-note state, and source spans without advancing the VM.
SegSat's event-count loop handling and distinct tempo timing remain explicit.

This removes seven format-local helpers and 20 production lines. A temporary
sanitized check covers 48 boundary timing cases, chained delays, sustained notes,
source spans, and retained automation bindings. The full build is warning-free
and all 20 CTest targets pass; no test code is added.

### Prepare MIDI once before constructing artifacts

Apply optional observed-range modulation scaling while preparing the lowered
MIDI. Artifact construction now encodes that prepared value directly and chooses
its diagnostics in one place. Remove the complete MidiSequence copy previously
made for every MIDI artifact, including exports with scaling disabled. Repeated
MIDI requests reuse the same scaled sequence without repeating modulation
analysis or scaling. The canonical performance still supplies modulation maxima.

This removes 13 production lines and two policy arguments from artifact writing.
A temporary sanitized comparison checks exact bytes and all diagnostic fields
across 1,440 scenarios: six modulation targets, both conversion/scaling policies,
three level resolutions, missing and replacement performances, zero and fractional
amounts, and repeated encoding. The full build is warning-free and all 20 CTest
targets pass; no test code is added.

### Use existing settings types for read-only program state

SonyPS1 and SonyPS2 now pass their existing RuntimeConfig types directly to the
compiled runtime adapter. Remove the two ProgramState structs and constructors
that only copied those same fields. Playback borrows a const settings reference;
the adapter's ordinary copy-construction path preserves per-render ownership.

This removes 12 production lines and two redundant types without changing the
runtime API, initialization timing, program lookup, or collection binding. The
full build is warning-free and all 20 CTest targets pass; no test code is added.

### Represent an unattributed diagnostic range once

Diagnostics and collection issues now contain SourceRange directly. A missing
SourceId denotes an unattributed location, matching other source-backed values.
Remove three range-to-optional conversion helpers, repeated conditional wrapping,
and redundant validation guards. Reporting APIs accept ranges directly; shell
output and source removal use the same range-validity rule.

This also fixes fallback attribution: explicitly passing an empty SourceRange
no longer suppresses the scanned file's range or the VM's current command range.
Locations with a source ID retain source-membership and byte-bound checks,
including zero-size anchors and out-of-bounds diagnostics. Optional read results
and extracted-source origins keep their distinct absence semantics.

This removes 33 production lines. A seven-line regression inside the existing
missing-runtime session test fails before the change and passes afterward;
adapting the shared assertion helper leaves a net six added test lines. The full
build is warning-free and all 20 CTest targets pass.

### Read AkaoSnes pitch boundaries from the current track

Replace the program-wide terminal-pitch address set and its full command walk
with a short look-ahead in Playback. The track already owns the decoded command
graph; playback inspects at most two commands to recognize an end or envelope-off
followed by an end or backward jump. Note commands occupy one byte, so a jump
before their continuation is a jump back to or before that note.

This removes 23 production lines, the extra set, and its initialization pass.
Existing coverage checks terminal envelope boundaries, backward loops, sounding
note transitions, and rests. The full build is warning-free and all 20 CTest
targets pass; no test code is added.

### Use VM ordering directly for AkaoSnes shared tempo

Tempo commands and fade ticks reach ProgramState in SequenceVM's chronological
track order. Remove the duplicate order counters, final sort, and prepass-only
linear search. Both passes now query the same ordered tempo history directly;
equal-tick changes retain their execution order, including initial-track rules.

This removes 17 production lines and the second lookup algorithm. A temporary
sanitized comparison checks identical MIDI bytes for 48 scenarios with one to
eight tracks, competing tempo commands, fades, and active LFOs under both
modulation policies. The full build is warning-free and all 20 CTest targets
pass; no test code is added.

### Move prepared MIDI tracks into stitched output

Composition now retimes each prepared MIDI track in place and moves it into the
result. Remove the extra full-track copy and the sourceTrack/track distinction.
The private preparation workspace owns these tracks exclusively, and subsequent
stitching work reads only the banks, instruments, samples, and collection IDs.
Failure still discards the private workspace.

This removes one production line and avoids copying every event and payload in
every stitched track. Existing stitch coverage checks PPQN conversion, repeated
collections, boundary controller state, modulation scaling, and instrument
variants. The full build is warning-free and all 20 CTest targets pass; no test
code is added.

### Resolve synth modulation scaling before container layout

Shared synth preparation now applies observed-range scaling alongside physical
modulation lowering for both instrument and region scopes. SF2 and DLS writers
encode the prepared amounts directly. Remove scaling settings from nine writer
functions, including DLS's repeated per-region calculation of instrument amounts.
No public API is added.

SoundFont layout can also share instruments when their modulation becomes
identical after scaling. A 12-line addition to the existing envelope-variant
fixture fails before the change and verifies shared layout while preserving both
presets and their envelope offsets afterward.

This removes 12 production lines. A temporary sanitized comparison produces
identical bytes in 2,304 scenarios for each of SF2 and DLS, covering instrument
and region scopes, fixed/controller depths, both conversion/scaling policies,
waveforms, delays, and absent, zero, fractional, or full observed maxima. The
full build is warning-free and all 20 CTest targets pass.

### Use prepared synth values directly

SoundFont layout now borrows its selected prepared instruments, as its presets
already do, instead of copying every region and modulation vector. The prepared
data outlives layout and encoding. Attenuation writing accepts the decoded sample
directly, removing a fabricated source Sample used only to carry attenuation.
Sample decoding also consumes the temporary reference set directly instead of
copying it into a second set.

This removes two production lines and three unnecessary copies/constructions.
The temporary sanitized SF2/DLS comparison remains byte-identical across 2,304
scenarios per exporter. The full build is warning-free and all 20 CTest targets
pass; no test code is added.

### Choose synth display names while writing them

DLS now supplies the instrument, wave, or bank fallback directly to its INFO
writer. Remove the sample-name rewrite pass and the separate owning-string
fallback helper. SoundFont's name helper and INFO writer borrow string views
instead of copying source names. Fallback names remain specific to each record.

This removes 13 production lines. A temporary sanitized comparison checks
identical SF2 and DLS bytes across 2,304 scenarios with empty, ordinary, long,
UTF-8, and embedded-null names. The full build is warning-free and all 20 CTest
targets pass; no test code is added.

### Honor note-selected instruments when preparing variants

Variant preparation now resolves a fresh note's explicit instrument before
falling back to the track selection. Envelope and signed-stereo variants use
that actual base instrument. Portable address allocation reserves note-requested
addresses too, preventing generated variants from occupying them.

Use the generated instrument's own address instead of retaining a duplicate in
VariantRecord. An optional variant address replaces the temporary address/flag
pair, and the one-use address wrapper is removed. Track restoration and tied
voice behavior remain in their existing paths.

This removes eight production lines. Existing selection and allocation fixtures
cover both fixes with four net added test lines; the note-selection case fails
before the change. The full build is warning-free and all 20 CTest targets pass.

### Use the performance track's source identity in MoriSnes

MoriSnes now looks up voice limits using PerformanceTrack::sourceTrackNumber,
which SequenceVM already preserves. Remove the duplicate source-to-output track
map, its construction pass, and the reverse search during finalization. The
shared state constructor now only accepts the driver configuration it needs.

This removes 11 production lines. Existing voice ownership, stealing, gating,
and release-clock coverage passes. The full build is warning-free and all 20
CTest targets pass; no test code is added.

### Keep repeat metadata in one command-address map

SuzukiPS1, SuzukiSnes, and PandoraBoxSnes now keep repeat metadata in one map
instead of separate start, end, and break maps. Their linear layout walks give
each command address exactly one role, which its opcode already identifies.
SuzukiPS1 and PandoraBoxSnes also build RepeatInfo directly in the open frame,
removing duplicated fields and the copy into a second representation at closure.
Unclosed repeats remain unpublished, preserving their ignored-command behavior.

This removes 22 production lines without adding a shared framework. Existing
repeat, final-pass break, and infinite-loop coverage passes. The full build is
warning-free and all 20 CTest targets pass; no test code is added.

### Let modulation measurements represent their own absence

Collection preparation and stitching now retain MidiModulationUsage directly.
Its four optional maxima already distinguish unobserved controllers from
observed zero values; an outer optional supplied no further information.
Stitching merges the measurements directly, and MIDI scaling owns the early
return for empty usage instead of requiring every caller to check it.
An empty usage value also leaves synth modulator amounts unchanged.

This removes nine production lines and the nested presence checks. Existing
unobserved/zero/quantization-boundary, MIDI/synth scaling, and stitched-export
coverage passes. The full build is warning-free and all 20 CTest targets pass;
no test code is added.

### Finalize decoded tracks in one pass

TrackDecodeSession now accumulates the track annotation range while assembling
the final commands, removing a separate walk and its helper. Reserve the known
command count before assembly. Empty tracks retain their original start anchor,
and command order, source ownership, and trackless annotations are unchanged.
The decode bound also uses min directly; its maximum default needs no special
branch.

This removes ten production lines. Existing source hierarchy, command ordering,
track-range, and malformed-command coverage passes. The full build is
warning-free and all 20 CTest targets pass; no test code is added.

### Give immediate motion resets one operation

SequenceLinearMotion and SequenceFixedPointAutomation now use reset for setting
an immediate source value and cancelling motion. Remove the equivalent
setCurrent/setCurrentRaw aliases and the binding adapter's type-dependent
setter selection. Fixed-point retargeting also uses reset before installing
its new plan, removing the otherwise unused preserving-motion setter.
Rounding remains configured independently, and timed binding interruption
still occurs through setCurrentAt.

This removes 11 production lines and three redundant motion entry points.
Existing delay, completion, all three fixed-point rounding modes, bound-value
replacement, and format fade coverage passes. The full build is warning-free
and all 20 CTest targets pass; no test code is added.

### Use existing instrument entry handles directly

HudsonSnes and SuzukiSnes now use an empty InstrumentSetBuilder::Entry for their
not-yet-created drum kit instead of wrapping that already-nullable handle in
optional. Both still create a kit only after finding a valid drum region.
Remove InstrumentSetBuilder::find, which has no callers; formats that group
instruments use getOrAdd, and other formats retain their returned entry.

This removes 14 production lines and one unused public method. Existing builder
grouping, entry validity, and drum-kit coverage passes. The full build is
warning-free and all 20 CTest targets pass; no test code is added.

### Keep performance event ordering on its header

PerformanceEventHeader::order supplies the existing tick/execution-order key.
Timeline sorting, command-event lookup, normalized-wheel context queries, and
MIDI voice-link checks use it directly instead of rebuilding two-field
comparisons. Standard range projections remove repeated comparator bodies.
Equal keys retain stable insertion order. Delayed transitions still sort by
realization.startTick, which can differ from their source header tick.

This removes 34 production lines. Existing same-tick global ordering, tempo
ties, modulation updates, and mixed pitch-transition coverage passes. The full
build is warning-free and all 20 CTest targets pass; no test code is added.

### Report only usable motion results

Motion ticks no longer carry an unread previous value or an unused active()
query. shouldApply directly identifies running and finished ticks. Remove its
unused delayed-step option and the corresponding callback arguments: delayed
ticks leave the value unchanged, so change-only callbacks cannot apply them.
Linear callbacks use changed directly; fixed-point callbacks compare the raw
values once without an additional status branch. HeartBeatSnes likewise uses
the tick's change result directly.

This removes eight production lines. A temporary sanitized comparison checks
identical values, statuses, callback counts, and callback values across 217,800
linear/fixed-point scenarios, including delays, retargeting, completion, and
all rounding modes. The full build is warning-free and all 20 CTest targets
pass; no test code is added.

### Make compiler invocation paths direct

CompilerCursor's public invocation methods now lead directly to command
composition and control-flow validation. Remove two private forwarding
templates; the typed invokeFlow overload reuses its public callable overload.
Stored argument ownership, effect aggregation, and default flow are unchanged.

This removes ten production lines. Existing typed/callable invocation, composed
effects, conflicting flow, source lifetime, and malformed-command coverage
passes. The full build is warning-free and all 20 CTest targets pass; no test
code is added.

### Declare motion starting values at construction

Linear and fixed-point motion values accept an initial source value, and
PerformanceBoundValue inherits those constructors. Eight formats now declare
their starting volume, pan, and tempo directly instead of resetting freshly
constructed values. Version-dependent values remain in constructor initializers.
Remove NinSnes's constructor-only PitchState::reset and zero-depth LFO reset,
which repeat the member defaults. Prepass and live-motion resets remain intact.

This removes 33 production lines. A temporary sanitized comparison matches
140,544 constructor/reset scenarios across integer, floating-point, fixed-point,
and bound motion values. The full build is warning-free and all 20 CTest targets
pass; no test code is added.

### Track MIDI controller values on the channel

Volume, expression, and pan duplicate suppression now uses the last value
written to the MIDI channel. Separate caches per automation could suppress a
necessary sample after an intervening source write changed that controller.
Every write now updates channel state; ordinary source commands still force
their repeated writes to survive. Pan simulation also avoids redundant
quantized volume updates when its gain compensation changes.

Remove the automation-controller map, its helper type, two forwarding helpers,
and the separate volume-emitted flag. This removes 31 production lines. The
existing controller fixture now uses the emitter and checks restoration after
interleaved writes; it is 46 lines shorter despite covering the regression.
The regression fails before the fix. The full build is warning-free and all
20 CTest targets pass, including seven/fourteen-bit resolution and pan/LFO
coverage.

### Build SNES instruments from one validated sample list

Return a plain vector of validated BRR samples and accept a span when building
sample values. Remove the catalog wrapper and its cached directory range;
compute that range from the retained entries when constructing annotations.
WolfTeam already filtered its catalog after reading it, leaving the old range
covering rejected entries. The focused filtering regression fails before this
change and passes afterward.

GraphRes and Prism filter the validated list directly. Prism reads tuning and
ADSR at instrument construction, removing its temporary Patch type and collection
pass. Compile, Itikiti, and Neverland also leave BRR validation to the shared
reader. Existing instrument-table boundaries and format-specific discovery
rules remain intact. BRR loop construction uses the reader's validated bounds.

This removes 66 production lines. Existing fixtures cover sorted SRCNs,
alias identity, loop lengths, malformed samples, and Prism's unreferenced
instruments; the short filtering regression checks the annotation actually
built for retained samples. The full build is warning-free and all 20 CTest
targets pass.

### Return repeat-break effects directly

Remove BranchResult and its duplicate taken flag. Both counted-repeat helpers
now return Effects; formats that apply branch-only changes inspect flowOverride,
which is present exactly when the repeat branch is taken. Capcom note attributes
and Suzuki PS1 octave restoration use the same rule as repeat-until.

This removes 10 production lines and one public result type. Existing tests
cover side effects on the final pass, finite branches to previously visited
commands, and format repeat behavior. The full build is warning-free and all
20 CTest targets pass.

### Keep live and saved track positions in one value

The executor and synchronized-loop checkpoints now use the same TrackPosition:
command index, pending time, tick callback source, and delayed-command state.
Saving and restoring copies that value directly, removing a second field list
and manual synchronization. Section changes reset the position as a whole;
format state, call/repeat state, and the advancing clock retain their existing
boundary rules.

This removes eight production lines. A temporary before/after comparison matches
3,456 scenarios spanning checkpoint offsets, waits, delayed commands, tick
callbacks, and loop counts (trace digest 76111302ff6e9327). Existing section and
HOSA loop tests pass. The full build is warning-free and all 20 CTest targets
pass.

## Restrict sampled pitch output to pitch bindings

Move `sample()` from `PerformanceAutomationBinding` to `PitchSlideBinding`.
Scalar fades never supported this operation: the general binding forwarded to
an emitter method that rejected every non-pitch automation at runtime. The pitch
binding now updates its own intent directly, using its existing validation and
preserving sampled endpoints, ordering, replacement, and track ownership checks.

This removes ten production lines and an unsupported operation from the general
API. Existing pitch sampling and lifecycle coverage passes without additional
test scaffolding. The full build is warning-free and all 20 CTest targets pass.

## Use the VM's current emitter directly in Square PS2

Remove `atEvent()` and `eventTick()` from Square PS2 playback. SequenceVM already
executes delayed commands with the correctly positioned emitter, so immediate
notes, controllers, envelopes, and LFO changes can use `out` directly. Future
fade endpoints use `out.after(duration)` and retain their automation ownership.
Simple controller commands use existing cursor emission helpers, and the
single-call envelope publishing wrapper is folded into its caller.

This removes 34 production lines without adding a new abstraction. One focused
fixture checks delayed level/pan fades, endpoint values and ownership, and
immediate level quantization. The full build is warning-free and all 20 CTest
targets pass, including existing portamento, envelope, LFO, and song-loop coverage.

## Remove redundant pitch and repeat state

Akao SNES now uses one constant reference pitch instead of storing and resetting
the same value on every track. Its separate eligibility flag is named
`noteAllowsPitchBend`, reflecting the actual distinction between melodic and
percussion notes. Pitch calculations and note lifecycle behavior are unchanged.

Suzuki PS1 stores its saved repeat-end octave as an optional value, eliminating
the parallel validity array and its synchronization. A compact sequence test
checks entry-octave restoration, a final-pass break restoring octave zero, and
reuse of the same repeat slot with a different exit octave. It passes both
before and after the refactor. Existing Akao pitch coverage also passes; the
full build is warning-free and all 20 CTest targets pass.

## Remove SegSat's test-only MIDI velocity adapter

Remove `segSatMidiVelocity()` and its public declaration. Runtime conversion
already uses `segSatLinearGain()`; only three saturation assertions kept this
extra MIDI-specific API alive. Those existing assertions now check the driver's
linear gain directly, retaining overflow/underflow coverage with greater
precision and no additional fixture or helper. The full build is warning-free
and all 20 CTest targets pass.

## Share saturating timeline arithmetic

Promote the emitter's existing `addTicks()` operation to a constexpr performance
utility and reuse it wherever the VM, tempo map, MIDI lowering, instrument
variants, stitching, MP2k, and SegSat already clamp tick addition. Remove the
VM's separate timing wrapper and the copied overflow branches. Both arguments
accept full-width ticks, so this also covers source-span durations without
narrowing. Explicit overflow rejection during collection retiming remains a
different policy and is unchanged.

This removes 22 production lines and keeps one clamping rule shared by notes,
waits, fades, sampled pitch, and portamento overlap. No new test scaffolding is
needed for this extraction. The full 175-step build is warning-free and all 20
CTest targets pass.

## Resolve pitch writes directly into their consumer

The pitch resolver now visits resolved events directly. Start-pitch queries
retain only the latest primary and held bends; final lowering appends events
directly into its output. Sorting uses references to pending writes, and a
query includes only writes through its requested tick and execution order.
This removes the copied write buffer, complete intermediate output buffer, and
second scan that previously answered each query. Future writes cannot produce
backdated output, so excluding them preserves the queried prefix exactly.

Remove `PitchBendWrite::owner`, which duplicated its event header's automation
ID. Retain `PitchBendLayer::owner`: source automation provenance does not imply
that a transition owns the live layer, so that state has a separate meaning.

Production line count is essentially unchanged (one line added), but the query
path no longer constructs a full resolved timeline. A temporary before/after
comparison matches event headers, note and pitch fields, and MIDI bytes in 600
cases across all three rendering modes, including sampled, delayed, interrupted,
linked, and multi-layer pitch. The harness is not committed. The full build is
warning-free and all 20 CTest targets pass.

## Share PlayStation sample inspection and construction

Square PS2, Suzuki PS1, TriAce PS1, HeartBeat PS1, HOSA, and Tamsoft now use
shared PSX ADPCM operations to inspect adjacent sample offsets and add their
annotated samples. Remove each format's duplicate sample-reference map;
`SamplePoolBuilder::find()` already resolves the same relative source offsets.
An empty validated stream list also covers an empty offset list, eliminating
four redundant rejection branches while keeping validation before draft creation.

The common code preserves stream order, boundary handling, sample names,
PS1/PS2 rates, loop flags, and optional annotation parents. Format-specific
instrument loop offsets remain explicit: TriAce uses pool-relative offsets,
while Suzuki and Square use sample-relative offsets with different override
conditions. Sony VAB retains its distinct index, naming, and annotation rules.

This removes 91 production lines. One focused test exercises unterminated and
incomplete streams, loop coordinates, sample references, hardware rate, and
annotations with and without a parent. The full build is warning-free and all
20 CTest targets pass, including the existing format bank and loop fixtures.

## Keep the NDS predictor inside its encoded sample

An NDS ADPCM sample's `encodedData` now includes its four-byte predictor
header. The decoder reads that complete bounded stream instead of reaching
backward into the surrounding source file. This removes the implicit SWAV
layout dependency from decoding and the scanner's separate predictor-header
range check. Encoded-byte metadata now includes those four required bytes;
decoded PCM, rates, channels, and loop coordinates remain unchanged.

Derive the ADPCM loop length directly from the non-loop byte count. The old
loop-start bound check could never fail: both encoded lengths are nonnegative
16-bit word counts, and the total already includes the loop offset. The shared
decoder contract now explicitly requires codec headers inside `encodedData`.

This removes 13 production lines. Extend the existing decoder and SWAR tests
with exact PCM, a predictor-only stream, and a truncated predictor; test code
grows by seven lines. A temporary comparison matches PCM and metadata in
589,824 cases across all initial index bytes, predictor extremes, varying
payloads, empty payloads, and relocated streams. The harness is not committed.
The full build is warning-free and all 20 CTest targets pass.

## Share record-reader truncation handling

`RecordReader::require()` and `requireAt()` now use one failure operation to
emit the first truncation diagnostic and retain failure state. Sequential
reads return immediately after failure; their first truncated read consumes
the remaining bytes directly. Positioned reads still recover valid fields
without clearing failure. This removes duplicated error construction and
unnecessary cursor arithmetic, with a net reduction of two production lines.
Existing reader, compiler, and format tests cover those policies; no tests
were added. The full build is warning-free and all 20 CTest targets pass.

## Resolve PlayStation loop offsets on the inspected stream

`PsxAdpcmStream::loopAt()` now bounds a relative byte offset and converts it
to decoded loop coordinates while preserving the stream's enable flag.
Suzuki PS1, TriAce PS1, and Square PS2 keep their distinct override conditions
and address rules, but no longer repeat the block arithmetic or recheck the
existence of samples already admitted by their pool builder. Square still
requires an enabled, nonempty loop; Suzuki still treats zero as no override.

This removes seven production lines and substantially shortens the three
format branches. Four lines of assertions extend existing fixtures to cover an
unaligned offset, the stream boundary, and a disabled loop. The full build
is warning-free and all 20 CTest targets pass.

## Resolve Akao articulations once and select playable coverage

An articulation whose PSX stream failed inspection retained a default sample
index of zero. It could consequently link to the first valid sample and
falsely satisfy collection coverage. Each articulation now retains its actual
`SampleRef`, empty when no usable stream exists. Source links, collection
selection, and instrument binding all use that same reference.

The resolver selects sample-pool entries directly, removing the coverage
provider and selection projections, the separate articulation-binding type,
and the redundant retained articulation count. Coverage comes from playable
articulations, so another pool can fill a rejected sample's gap; unresolved
gaps remain diagnostic. Preferred sample sets, local-source priority, PSF
isolation, and source-table binding order retain their existing policies.

This removes 85 production lines. Consolidated collection-selection tests
exercise real resolver inputs for local preference, fallback, and missing
coverage. The existing scan/bind/export fixture now includes an incomplete
stream and checks that it has no sample link; that assertion failed before
the fix. Test code grows by 14 lines overall. The full build is warning-free
and all 20 CTest targets pass.

## Resolve synth sample requests in one table

Synth preparation now keeps one table from requested sample variants to
optional decoded indexes. Decoding fills each existing entry directly instead
of maintaining a separate requirement set and inserting the same keys into
another index map. Failed requests retain an empty index; every region's key
is present by construction. This removes one container type and the second
insertion path without changing the public model; line count grows by one.

A temporary comparison matches all prepared sample values, region indexes,
modulation, and diagnostics in 3,072 before/after scenarios covering selection,
filtering, missing sources/pools, rejected codecs, mono restrictions, duplicate
owners, phase inversion, and sample-start trimming. Both implementations run
under AddressSanitizer and UBSan. The harness is not committed; existing synth
regressions cover the retained behavior. The full build is warning-free and
all 20 CTest targets pass. No test code was added.

## Find loop hints through the VM's existing visit ordering

Loop-candidate lookup now uses the visit map's command/call-stack ordering
instead of scanning every recorded state. An empty repeat map is the first
possible repeat state for that prefix; checking the returned command and stack
preserves the previous first-match behavior, including ignoring finite-repeat
counters. The lookup visits logarithmically many map entries and needs no
additional index or state. Production line count is unchanged.

Existing VM regressions cover unvisited targets, active repeat counters,
preserved markers, and coordinated loop stopping. No test code was added.
The full build is warning-free and all 20 CTest targets pass.

## Simplify retained MIDI oscillator state

Future-note LFO delay is now an ordinary value. Before the first delay update,
both current and future delay are zero; every subsequent update already sets
the future value. There was no distinct absent state to preserve at note
restart. One `canSampleImmediately()` predicate now serves the command-restart,
note-vibrato, and note-tremolo paths.

Oscillators also borrow their waveform from the immutable lowered performance
instead of copying its optional sample table on every configuration event.
That performance outlives all per-track rendering state, and configuration
only receives its retained events. This removes repeated waveform allocation
without changing the public performance model. Net production reduction is
three lines; no test code is added.

A temporary comparison produces identical MIDI bytes and diagnostic counts
in 3,072 scenarios across both modulation policies, waveform/sample tables,
restart modes, inactive oscillators, current/future delay changes, physical and
tick delays, and note extensions. Both renderer versions run under
AddressSanitizer and UBSan; the harness is not committed. The full build is
warning-free and all 20 CTest targets pass.

## Retain only Suzuki PS1 native envelopes for sequence playback

Suzuki PS1 now constructs each synth instrument where its record is decoded.
Sample preflight reads only the offsets needed to inspect streams, removing
the intermediate vector of full instrument records. The scan-to-sequence
handoff retains bank, program, and the two native ADSR registers; sample
metadata and source annotations no longer accompany every sequence runtime.

The compact settings also serve as read-only program state through the existing
compiled-runtime adapter, removing a forwarding state type. Bank/program lookup
order, native registers for rejected samples in otherwise playable banks,
fractional tuning, loop overrides, and published annotations retain their rules.
This removes 21 production lines without adding another model or adapter.

A seven-line extension to the existing scan fixture renders an attack-rate
command and verifies that it starts with the scanned bank's other ADSR fields.
Existing dynamic-command tests cover program changes and bank switches. The
full build is warning-free and all 20 CTest targets pass.

## Construct export variant regions in one pass

Instrument variant preparation now applies envelope overrides and appends
stereo layers into one candidate region vector. It no longer copies an entire
instrument for every attack or builds an intermediate stereo input vector.
The candidate's envelopes determine whether a non-stereo variant differs from
the base, removing the separately accumulated envelope-difference flag.

Only an actual new variant copies the instrument and replaces its regions.
Existing variants still match effective region values, preserving deduplication
across different override histories. Address allocation, active-voice warnings,
cleared/inherited stages, silent stereo layers, and note selection are unchanged.
Production line count is unchanged; one construction pass and one state flag
are removed, with no new types or public API.

All 20 CTest targets pass, including existing envelope, stereo, and address
exhaustion regressions. A temporary comparison checks variant addresses,
region values, note selections, and complete diagnostics in 2,048 preparations
across all four options, partial masks, cleared/restored fields, multiple lanes,
repeated and held notes, signed/zero gains, and non-finite envelope values.
Both implementations run under AddressSanitizer and UBSan without findings.
The full build is warning-free; no test code is added.

## Derive RIFF chunk sizes when serializing

RIFF chunks now retain their identifier and logical payload only. The writer
derives the declared size and appends the alignment byte, removing the separate
size field and the factory that kept it synchronized with a pre-padded buffer.
SoundFont and DLS construct ordinary chunk values directly. DLS pool offsets
still use storage size including padding; SoundFont INFO strings still add
their required even-length padding inside the declared payload.

This removes five production lines, one stored invariant, and one factory.
Payload and storage sizes retain their 32-bit overflow checks. A compact
17-line addition to the existing synth test file checks odd/even siblings and
nested container lengths; existing exporter tests cover the format-specific
rules. A temporary sanitized comparison matches bytes and storage sizes for
4,096 nested RIFF cases. The full build is warning-free and all 20 CTest targets
pass. The comparison harness is not committed.

## Retain only HeartBeatPS1 pitch limits for sequence playback

HeartBeatPS1 runtime tones now contain only key ranges and the native upward
and downward pitch-wheel limits. Sample locations, tuning, ADSR, mix values,
flags, and source records are decoded when building the bank instead of being
retained in every sequence configuration. All tone records still contribute
sample boundaries, including tones unused by a program; referenced tones keep
their original playback lookup order even when their samples are unplayable.
Instrument metadata is complete before insertion into the shared builder.

A bank whose programs reference no valid tones previously published an empty
sound bank before returning failure. Program filtering now rejects it before
creating the draft. The regression reproduced that publication before the fix.
This removes 20 production lines. The existing fixture gains 17 net test lines
covering rejected-bank publication and scanned asymmetric pitch limits; its
existing tuning/reverb checks also pass. The full build is warning-free and
all 20 CTest targets pass. Next work prioritizes shared structural changes to
format authoring, as requested, over further isolated format cleanups.

## Share sequence assembly with custom track decoders

SequenceDecodeSession no longer requires its built-in reachability walker.
Formats can reuse its TrackDecodeScope, append a completed TrackProgram, and
customize its existing header and track-pointer annotations. The ordinary
addTrack path uses those same operations. Header access returns the existing
AnnotationBuilder, which is inert without a source map, instead of requiring
formats to unwrap an optional ID and reconstruct the builder themselves.
No new model, callback protocol, or traversal policy is introduced.

Prism, Rare, Wolf Team, Akao, Suzuki PS1, TriAce PS1, and Square PS2 now share
this assembly. Their stateful walkers, track analysis, runtime settings,
track bounds, source ownership, and annotation parents remain explicit and
unchanged. Rare and Wolf Team retain their header-parented tracks by copying
the common scope; Square PS2 sets each track's own bytecode limit on that copy.
Format-specific header labels and categories are preserved. Prism's pointer
fields now list the common destination before its additional channel fields.
AkaoSnes and PandoraBoxSnes also use the direct header builder.

The change removes 95 production lines overall; the shared implementation
grows by only two lines. The existing exceptional-walker compiler test now
also verifies sequence assembly, custom annotation fields, and inherited
asset ownership, with two net test lines added. The full build is warning-free
and all 20 CTest targets pass, including the existing format and source-map
checks. No additional test fixture or harness is committed.

## Build command source fields directly

CompilerCursor now constructs the existing SourceField representation directly.
Previously every displayed value became a SemanticOperand carrying names,
ranges, display modes, and an optional second encoded value, then a separate
projector translated those operands back into source fields. SemanticOperand
now contains only a tagged value and its role. Ordinary values stay solely in
the field list; channel and instrument/control-flow analysis retain the tagged
values in their original order and types.

This removes the operand-to-field translation function and six intermediate
metadata members without changing the format-facing compiler API. Encoded and
resolved fields are recorded together when resolvedValue is called, preserving
field order, empty-name rules, source links, and truncated-command metadata.
Quest's nested remote commands concatenate both their source fields and tagged
values. The change removes 26 production lines across the shared compiler and
projection code, including that small compound-command adjustment.

A temporary comparison matches annotation fields, links, ownership, channel
attribution, and diagnostics for 4,096 generated cases before and after the
change. Both versions run under AddressSanitizer and UBSan without findings.
The committed tests reuse the existing compiler cases and add an 11-line Quest
case to its existing fixture helpers, checking both nested forwarding fields
and the embedded operand. The comparison harness remains uncommitted.
The full build is warning-free and all 20 CTest targets pass.

## Infer sequence bounds from existing source ownership

ScanResultBuilder now infers an unspecified sequence range from its owned source
annotations and decoded commands. Ownership already includes header children and
track roots, so parsers no longer need to return a separate header range merely
for their scanner to reconstruct the same span. Headerless command ranges use
the input source when no base range was supplied.

Inference runs after every draft finishes: synth builders can still add source
annotations during materialization. Explicit ranges retain their exact bounds,
including valid zero-length anchors. SoftCreatSnes keeps its explicit range
because its owned annotations also describe driver state outside those bounds;
existing container and playlist-specific ranges remain explicit as well.

Fourteen format modules use the shared default. Thirteen SequenceParse header
fields disappear, and Prism, Chun, and WolfTeam return SequenceProgram directly
instead of maintaining format-specific wrappers around it. This removes 34 net
production lines and three types without adding a new format-facing API.

Verification: the warning-free full build and all 20 CTest targets passed. A
focused core fixture checks inherited ownership, foreign-source filtering,
unannotated commands, and explicit bounds. Existing header checks now inspect
the source map. Total committed test growth is 27 lines. A temporary comparison
of 395 scanned sequence ranges across 36 formats matched the previous output
exactly; its capture hook and outputs remain outside committed source. This is
fixture coverage, not real-file corpus parity.

## Share playback context and bind runtimes directly to Playback

SequencePlayback provides the borrowed track, emitter, and VM references used
by compiled format methods. Its TrackState alias supplies the runtime's state
factory type. Thirty-nine playback implementations across 37 formats now name
that context once instead of repeating its fields and repeating the state type
again in their compiler cursor. Typical declarations become:

```cpp
struct Playback : SequencePlayback<TrackState> { /* driver methods */ };
using Cursor = CompilerCursor<Playback>;
// Supply ProgramState only when the format needs song-wide state.
auto runtime = makeCompiledRuntime<Playback, ProgramState>(config);
```

Runtime creation and runtime-family identity now depend on Playback and
ProgramState directly. CompilerCursor no longer carries a runtime state type,
and Mori's sound effects no longer need a synthetic cursor type merely to
construct their runtime. The runtime header no longer includes the compiler;
CompilerCursor supplies the combined compiler/runtime authoring surface.

The shared context owns no state and introduces no allocation. The adapter
still borrows optional program state and refreshes its emitter/VM references
for commands, wait polling, and ticks. HOSA explicitly names its VM handle type
and resolves it into program-owned playback state in its constructor. Akao SNES
keeps its profile reference, and Prism's child playback uses the same context.

Verification: the warning-free full build and all 20 CTest targets passed,
including existing state-factory, prepass, polling, playback, and runtime-family
checks. A temporary standalone compilation confirmed that the runtime header
needs neither CompilerCursor, BytecodeDecode, nor RecordReader. No new tests
were added; adapting the existing fixtures removes 15 test lines. Production
code is 117 lines smaller.

## Preserve source command identity independently of playback tracks

Performance event headers now carry a SourceCommandRef: the decoded track and
its local command ID. SequenceProgram resolves that reference directly, and
performanceEventsForCommand compares the complete origin. Moving an event to
another playback track no longer changes which source command it identifies.
This keeps source attribution separate from playback placement without making
source annotations mandatory.

This reproduced and fixed two SegSat defects. Merging its tempo track into
channel zero previously redirected note source lookup into the tempo stream.
Also, a volume command with the same local ID as a tempo command could replace
that tempo event during collection preparation. Controller lookup now checks
the source stream. Instrument-variant warning deduplication likewise retains
both components of a command origin; its fallback keys use explicit categories
instead of overlapping bit-packed identities.

The source reference does not yet eliminate copied channel programs. It makes
that subsequent representation work independent of playback-track renumbering.
This correctness foundation adds 20 net production lines. Tests reuse existing
VM, model, instrument-variant, and SegSat fixtures; the larger mechanical test
diff adapts explicit emitter arguments and wraps longer initializers.

Verification: both SegSat failures were reproduced before their respective
fixes. The warning-free full build and all 20 CTest targets passed. Existing
checks also cover same-numbered commands from different source tracks in one
playback track, including warning deduplication without source annotations.

## Execute shared command streams with independent playback tracks

TrackProgram now declares the source track numbers that execute its commands.
The ordinary case still declares one number. SonyPS1, SonyPS2, HeartBeatPS1,
SegSat, and NamcoSnes declare all their channels or voices on one decoded
stream instead of copying the complete command vector for each one. SonyPS2
also assembles and sorts each song's section commands once, moving the commands
from temporary sections into the shared song stream.

SequenceVM expands those declarations in order and gives every playback track
its own cursor, flow state, note state, and format state. Source command
references retain the decoded stream's identity. Playlist entries still follow
playback order, and validation resolves their starts against the corresponding
shared commands. This preserves scheduling, channel-specific initialization,
prepasses, and section transitions. Shell source inspection shows each decoded
stream once with its declared track numbers.

TrackStateContext supplies the sequence, decoded track, and current source
track number together. Format states can keep borrowing the original decoded
track, as AkaoSnes does; no temporary metadata-only track substitutes for it.
This removes the runtime adapter's separate constructor combinations for
sequence, track, and settings. One pair of construction helpers now serves both
program state and track state. Default construction and settings-only states
remain supported, and unused runtime settings are still rejected.

The change removes 29 net production lines, including shell inspection. More
substantially, each shared stream has one command vector rather than up to
sixteen channel copies. No new storage wrapper or alternate VM was introduced.

Verification: the full build is warning-free and all 20 CTest targets pass.
A temporary before/after capture matched all 291 MIDI renderings from the
existing fixtures. The committed playlist fixture now uses shared commands
while retaining its independent cursor, state-persistence, and boundary checks;
it also checks source lookup and expanded playlist validation. Removing an
unused helper type offsets those additions: total test line count is unchanged.
The MIDI capture hook was removed, and its harness and results remain ignored.
Real-file corpus parity is still unverified.

## Share variable-length integer decoding across parsers and records

ByteReader now supplies one consuming base-128 integer reader. SonyPS1,
SonyPS2, HeartBeatPS1, and KonamiPS1 use it instead of maintaining separate
four-byte decoding loops. SonyPS2 also stops returning an unused byte count
alongside every decoded value. RecordReader delegates the byte reading to the
same primitive while retaining its source fields and truncation diagnostics.

The byte limit remains explicit: the four format parsers accept at most four
bytes, while RecordReader permits the whole record window and preserves its
existing unsigned accumulation for longer encodings. The shared reader stops
at both the supplied window and the actual source bounds. Failed reads retain
their consumed cursor position; RecordReader keeps its sticky failure behavior.
The change removes 33 net production lines.

A temporary before/after comparison matches values, cursor positions, validity,
source fields, and diagnostics for 32,768 generated cases, including missing
terminators and longer record encodings. Source.cpp and RecordReader.cpp were
compiled into both comparison executables with AddressSanitizer and UBSan;
neither reported findings. The harness and results remain ignored. An 11-line
addition to an existing reader fixture checks the distinct length policies.
The full build is warning-free and all 20 CTest targets pass.

## Reuse sequence assembly for five more format parsers

HOSA, KonamiPS1, SonyPS1, SoftCreatSnes, and TamsoftPS1 now assemble their
programs through SequenceDecodeSession. Custom track decoders borrow its
TrackDecodeScope instead of rebuilding the reader, safety limits, sequence
ownership, and source-map context from separate arguments. The existing
session supplies header ownership and final runtime attachment; no new
builder, callback protocol, or decoding policy was introduced.

Format-specific behavior remains explicit: HOSA ends tracks at their declared
bounds, SonyPS1 retains one trackless source stream for its playback channels,
SoftCreat retains its state-dependent walker and split-byte pointer fields,
and Tamsoft retains its discovered voice seeds and delayed starts. Header
labels, kinds, annotation order, parents, ownership, and fields are preserved.
This removes 35 net production lines and adds no committed test code.

Verification: all 20 CTest targets pass and the full build is warning-free.
Temporary before/after captures match all 291 rendered MIDI files and all 540
deterministic scan snapshots, including 45 scans of the five migrated formats.
The scan snapshot covers source annotations, fields, links, sequence ranges,
track identities, command flow, and diagnostics. Two unrelated concurrent-scan
fixtures are excluded because their allocated IDs depend on scheduling. The
comparison caught and corrected HOSA's nonstandard header kind. Capture hooks
were removed; the harness and results remain ignored. Real-file corpus parity
remains unverified.

## Represent instrument selection as one explicit choice

InstrumentPerformanceEvent now carries one InstrumentSelection: either a
logical InstrumentAddress or a source-domain InstrumentIdentity. The old bank,
program, and optional sourceInstrument fields could describe competing
selections, requiring consumers to repeat which one took precedence. The
model now makes that choice explicit, and lookup helpers accept the selection
directly instead of constructing temporary performance events for note-level
preset overrides.

MIDI rendering, synth usage filtering, instrument variants, and pitch-range
lookup share matchesInstrumentSelection. Address assignment remains in the
existing export policy. Ordinary lookup still requires an exact source
identity and returns its first match; variants retain address fallback, while
synth filtering retains all matches in bank order. Default selection remains
bank/program zero. Existing format-facing instrument emitter calls are
unchanged, as are force-bank and envelope-preservation options.

Production code is 28 lines smaller. The committed test changes adapt existing
fixtures and assertions to the explicit choice, adding eight net lines through
wrapping and named identity lookups; no new fixture or test function was added.
The existing policy fixture still exercises exact identity, missing-identity
fallback, direct addresses, duplicate matches, and note-level overrides.

Verification: the full build is warning-free and all 20 CTest targets pass.
Temporary before/after captures match all 291 MIDI renderings, 63 SoundFont2
exports, and 29 DLS exports from the existing fixtures. Capture hooks were
removed and their harnesses/results remain ignored. This verifies fixture
output parity, not a real-file corpus.

## Preserve attack-time instrument variants across linked notes

A pre-existing envelope defect treated key-changing ties as fresh attacks
because their linkage lives in PitchTransitionIntent rather than the note's
extendsPrevious flag. Restoring an envelope before a linked note selected the
base preset too early under native MIDI portamento. Pitch-bend rendering
ignored that tie-level preset but consumed the reset needed by the next fresh
attack, leaving that note on the old envelope variant.

The performance model now exposes the existing MIDI onset, predecessor-order,
lane, and cancellation checks for reuse. A linear-time lookup identifies
linked note IDs before variant preparation and used-instrument filtering.
Variants retain the original voice's preset across both kinds of continuation;
synth filtering retains instruments selected by attacks, including a leading
tie with no preceding voice. MIDI still resolves its bend base after native
portamento splits. Formats require no additional state or export knowledge.

The fix adds 52 production lines. These derive continuity from finalized
automation instead of introducing another mutable note flag that would need
synchronization when slides are canceled. Two existing fixtures gain 43 net
test lines, covering envelope restoration under both MIDI policies and exact
used-instrument filtering. No new fixture framework is introduced.

Reachability: a temporary test using valid ItikitiSnes bytecode reproduces both
failures through its normal decoder and SequenceVM. The source sequence sets
an attack override, plays a note, enables portamento, restores ADSR, changes
pitch, disables portamento, and plays a fresh note. Nine format families
contain both linked-note and dynamic-envelope paths; that identifies potential
exposure, not measured incidence across games. Collection exports enable
dynamic-envelope variants by default. A real-file corpus is still unavailable.

History: the extendsPrevious-only check dates to b9745f179 (August 1),
note-level preset restoration to 27437df2b (August 2), and separately linked
key changes to a783df96e (August 2), all before this audit. This correction
supersedes the deferred finding in ccdb49476.

Verification: the full build and all 20 CTest targets pass. An ignored sanitizer
harness checks 1,296 combinations of predecessor identity/order, lane, start
and end ticks, and cancellation reason. Captures of this implementation match
all 63 SF2 and 29 DLS fixture exports and the 290 unchanged MIDI renderings;
the remaining MIDI fixture was extended and now exercises both policies.
The probes, captures, and comparison harnesses remain ignored.

## Prototype deferred key/velocity response sampling

An ignored SonyPS2 prototype separates native response evaluation from generic
key/velocity grid traversal for both ordinary sample regions and Setb notes.
Across 1,440 cases and five sampling steps, its 141,477 generated regions match
the pre-change emitters exactly: ranges, tuning, envelope, pan, attenuation, and
LFO properties. The probe runs under ASan/UBSan, including stack-use-after-return
detection after factory settings leave scope. Program-region sample references
also remain intact after rebinding to a resolved sample owner/index.

This establishes the arithmetic and ownership of captured format settings,
not a finished model or export change. Unsplit velocity ranges must retain the
driver's selected center: choosing a generic midpoint changes compensation for
velocity quantization even when the native curve is linear. The prototype
keeps that choice within the format response and copies no sample-set vectors.

The production implementation below completes the preparation and ownership
requirements established by this prototype.

## Defer native region responses until synth export

SonyPS2 now retains one region per native sample or Setb note. Each optional
`RegionResponse` owns immutable driver settings and evaluates physical region
parameters at a key/velocity. Native curve math, selected centers, quantization,
and source annotations stay in the format. Baseline values remain available
for inspection. Constant responses require no retained callback; sample-set
vectors and borrowed reader/storage references are never captured.

The existing shared synth-preparation module owns the grid traversal and
bank-wide sampling budget. SF2 and DLS retain the established 3,000-region
sampling policy; another exporter can choose its own budget or step. Budget
warnings now accompany export. Static zones count toward the budget, and an
unrepresentable native count still produces a warning without dropping zones.
This is a conservative sampling budget, not a replacement for container table
overflow checks or a new limit on static banks.

Responses are materialized before attack-time envelope and signed-stereo
variants, then cleared. Direct container export samples them independently.
`ResolvedSynthRegion` owns its resulting `Region`, preserving resolved sample
bindings through expansion and moves; instrument metadata remains borrowed.
Callbacks leave sample/phase/start-frame settings, ranges, and the response
unchanged. The sampler copies a callback once per native region, not per zone.

SonyPS2 loses 110 production lines, two sizing structs, and its separate sizing
prepass. The complete production change adds eight lines, including the shared
API and ownership documentation, with no additional architecture files. The
existing SonyPS2 fixture retains three native regions instead of 273 generated
zones. Tests add 69 lines: one compact shared-export case and extensions of the
existing SonyPS2, envelope, and stereo fixtures.

All 20 CTest targets pass. All 96 pre-existing synth-export captures (65 SF2,
31 DLS) retain identical byte fingerprints. The production response functions
also match the old emitter in 1,440 cases / 141,477 regions under ASan/UBSan,
including expired factory settings and rebound sample references. Large
comparison harnesses and capture hooks remain outside the committed code.
An additional end-to-end comparison matches 20 SonyPS2 MIDI/SF2/DLS artifacts
through direct and collection exports, using both the existing fixture and a
widened fixture that triggers budget-driven coarsening. Real-file parity
remains unverified without a corpus.

## Review follow-up: MIDI voice ownership

Both reported regressions were confirmed. The used-only bank mismatch came
from bbb5abb2f: the Itikiti bytecode `10 00 25 04 37 08 10 01 3F 08 00`
started a native-portamento note under program 1 while the bank retained only
program 0. The overlapping-note truncation came from 522e10749: a capped
voice's later fragment incorrectly shortened an unrelated note from 100 to
15 ticks. The first case reaches the real format decoder under PreserveFormat;
the second is demonstrated with a constructed performance, without an
established real-game path combining overlap and a hardware timer.

MIDI lowering now resolves the preset belonging to each source attack before
splitting notes. Continuations inherit that preset, including envelope and
stereo variants, while the next independent attack retains the source's
intervening selection. Source events remain intact; the resolved addresses
belong to the temporary MIDI performance. Chronological source traversal also
removes two redundant sorts in pitch lowering.

The renderer now associates note IDs and their predecessors with a voice's
own MIDI fragment indices. Hardware clipping and same-voice extensions use
those indices, preserving absolute stop times without touching an unrelated
note. This replaces the track-wide clipping suffix and last-note assumption.
The existing continuation helper now retains predecessor IDs instead of
only membership; formats acquire no new state or flags.

Focused tests extend the existing paired-export fixture and cover overlapping
notes on the same and different lanes, both pitch policies, mid-note splitting,
and linked source notes. Existing tests retain coverage of later limits,
tempo changes, zero-duration attacks, and suppressed post-stop fragments.
The full build and all 20 CTest targets pass. The focused renderer/lowering
cases also pass ASan/UBSan. All 292 pre-existing serialized MIDI fixture
captures match. An ignored real-decoder probe checks
18 serialized MIDI files against six SF2/DLS pairs, parsing actual note-on
programs and bank headers with ordinary, envelope, and stereo presets, both
with and without a subsequent independent attack. Those pairs have no
decoding, preparation, or synth-export diagnostics. Larger probes and output
captures remain ignored.

## Share ranked collection-candidate selection

SonyPS1, SonyPS2, SquarePS2, and TamsoftPS1 now use `bestMatches` for
highest-score selection and ordered ties. Compatibility and affinity remain
format-owned: mismatched body sizes and driver IDs are excluded, SonyPS2 still
rejects ambiguous weak bank matches, and Tamsoft retains its music-bank and
sole-generation fallback rules. Filename/path interpretation is deliberately
unchanged because these drivers use different archive and directory policies.

The helper lives in the existing collection-discovery header. Negative scores
reject candidates; zero remains a valid fallback. Results borrow the candidate
vector, with temporary vectors rejected at compile time. No collection policy
object or scoring framework is introduced. Five repeated loops become one,
removing 37 production lines overall. The committed test adds ten lines.

All 20 CTest targets pass. Captures retain identical collection keys, names,
member IDs, binder presence, and reported issues across 61 discovery results
from the four formats. An ignored ASan/UBSan probe verifies 488,281 ordered
score combinations, including empty inputs, invalid candidates, zero-score
fallbacks, and ties, plus compile-time rejection of temporary input vectors.
The capture hook was removed from Session after verification.

## Use discovery assets directly instead of copied metadata

SonyPS1 now reads sequence metadata through borrowed asset pointers and uses
`AssetWithData` for banks and sample pools. This removes three local record
types and their copying passes. Matching reads source IDs, offsets, and native
sample sizes from their owners; external-sample requirements are checked on
the selected bank. Durable binders still capture only stable asset IDs.

`AssetWithData::sourceId()` now returns the existing `SourceId` value directly.
SonyPS1, KonamiPS1, and Akao use its validity check instead of wrapping it in
another optional. Source ID zero remains valid, an invalid ID cannot establish
a source match, and a valid ID remains available when its source file is absent.
The change removes 75 production lines and adds ten lines to the existing
source-discovery test.

All 20 CTest targets pass. All 82 captured resolver results match, including
collection keys, names, member order, binder presence, and issue details.
An ignored ASan/UBSan probe compares 15,000 resolver results across 5,000
synthetic asset sets: missing source files and IDs, path fallbacks, reordered
assets, empty banks, missing retained data, ambiguous sample bodies, Akao
sample-set coverage, and SonyPS1's existing sequence-offset ranking. The
results are identical. Temporary capture instrumentation was removed.

## Use one motion plan and derive live motion state

Fixed-point fades now use `SequenceMotionPlan` directly. The fixed-point
state's `toRawTarget` factories convert targets to accumulator units and
preserve driver-supplied fixed steps. Seven format families obtain those
plans from their motion state, removing the separate `SequenceFixedPointMotion`
type and its repeated fields and translation. Retargeting still rounds the
current source value before calculating the next step.

Live linear motion no longer retains its construction mode. Remaining ticks
identify timed motion; its step is cleared on completion, so zero remaining
ticks with a nonzero step identifies target-driven motion. Delays, constant
values, explicit timed steps, and exact final-target snaps retain their behavior.
The change removes 27 production lines and adds seven lines to the existing
fixed-point regression test.

All 20 CTest targets pass, and all 304 serialized MIDI fixture captures match.
An ignored ASan/UBSan comparison checks 10,672,560 transitions against the
previous header, including signed/unsigned integers, floating-point values,
five fractional scales, three rounding modes, delays, supplied steps,
retargeting, completion, clearing, and non-finite floating-point values.
All returned statuses, values, change flags, raw callbacks, and active states
match. Temporary MIDI capture instrumentation was removed.

## One LFO delay value and modulation event path

Vibrato and tremolo delays now use `LfoDelay` inside the existing modulation
context. Independent delay commands are `VibratoDelay` and `TremoloDelay`
modulation targets. This removes two performance event types, their duplicated
fields, the MIDI simulator's private delay type, and separate delay-controller
normalization functions. Delay commands update only delay state; they do not
apply the other context defaults or restart an already-running oscillator.

Tempo-relative rates and delays now share one ordered map of active controls.
The map borrows original event pointers until all derived events have been
computed, avoiding another copy of each retained LFO context. Target/layer
ordering, cross-track tempo order, fixed-clock replacement, track-end limits,
and future-note-only updates are preserved. Embedded rate/depth context delays
still do not independently contribute synth delay observations or MIDI delay
controllers. Non-primary pitch layers retain their own delay in simulation.

Nineteen format families use the shared delay value. Itikiti passes its existing
context delay directly to the independent delay emitter, and NDS emits its three
rate targets without a dispatch switch. Physical timing formulas and driver
counter explanations remain intact. This change removes **107 production lines**;
existing test migrations add **one line net**, with no new committed test harness.

Validation: all 20 test targets pass (10.76 seconds). Captured exports match the
baseline exactly: 304 MIDI and 100 SF2/DLS files. An ignored differential probe
compares 2,400 generated performances, including their resolved event ordering,
physical timing, modulation profiles, and 4,800 MIDI exports under both policies.
A further 432 comparisons verify independent-layer delay commands against the
same delay supplied in LFO context. The final probes and changed tempo resolver,
modulation profile, and MIDI renderer run with ASan/UBSan. Temporary capture hooks
are removed before committing. This is fixture/generated-input coverage; no
real-file corpus was available.

## Share synth builder source bookkeeping

`SamplePoolBuilder` and `InstrumentSetBuilder` now use one internal
`SynthBuilderSources` implementation for asset-level annotations, source range
selection, and diagnostics. Their format-facing APIs, move-only ownership,
entry types, grouping rules, and finalization remain distinct. The shared state
holds the asset owner, optional source map/diagnostic sinks, and explicit versus
observed source spans. Format authors continue using the existing builders.

This removes **42 production lines** from the two existing synth builder files.
No format caller changes or new committed tests are needed. In particular,
source annotations still infer their kind from their label when no explicit
kind is supplied, and source ranges are still tracked without a source map.

An ignored ASan/UBSan probe, including the changed builder implementation,
compares 1,600 generated cases against the baseline. Final ranges, source
annotations and links, diagnostics (including their asset owners), sample
references, and moved-builder results match exactly. Cases cover absent sinks,
invalid and zero-length source ranges, explicit spans, duplicate keys, and
multiple annotations per entry.

All 20 test targets pass (10.79 seconds). All 542 scan/source-map captures and
100 SF2/DLS exports match the baseline. Temporary capture hooks are removed
before committing; no permanent verification harness was added.

## Sample native regions only when variants need them

Instrument variant preparation now samples a bank when a fresh note attack
first requires an envelope or signed-stereo override. Ordinary notes, unused
banks, and envelope updates that do not reach a new attack retain their native
responses until synth export. This removes unconditional sampling and its
synth-table warnings from otherwise unaffected MIDI preparation.

The existing sampling block runs immediately before applying overrides. A set
of bank indexes ensures it runs once per bank; all original instruments share
one resolution before any generated variants are appended. Sampling also
precedes the empty-region check, preserving behavior for responses whose ranges
produce no zones. Format callbacks, shared sampling policy, sample bindings,
and final export representations are unchanged.

This adds six production lines, including the lifecycle documentation. The
benefit is removing an unconditional preparation phase, rather than reducing
source line count. No new API, helper type, or format-specific branch is added.
Two existing tests gain nine lines net to check ordinary notes and an untouched
bank. The audit's one-note probe now performs zero evaluations, retains one
native region, creates no variants, and reports no sampling warning; it
previously evaluated 1,849 regions despite needing no variant.

All 20 tests pass (10.92 seconds). The 304 MIDI and 100 SF2/DLS fixture captures
match the baseline exactly. An ignored comparison runs both implementations
with ASan/UBSan over 144 generated cases, including coarsened grids, static and
responsive instruments, multiple banks, repeated overrides, active-voice-only
updates, linked notes, and signed stereo. Prepared regions and note instrument
addresses match for both complete and used-only synth selection. Temporary
probes and capture hooks are not committed. Real-file parity remains unverified.

## Keep Prism decode state and flow beside their operands

Prism's stateful discovery walker now follows the compiler's decoded control
flow instead of maintaining a second opcode dispatch and rereading branch
operands. Duration-mode and subtrack-mode changes update decode state in their
existing command cases. The walker retains its breadth-first order, first
reachable interpretation, single return-address behavior, and discovery of the
encoded continuation after an infinite repeat. Its visit key uses structural
comparison rather than repeating every decode-state member in a tuple.

This removes **23 production lines** and, more importantly, removes a second
place that had to know instruction layouts and version-specific branch aliases.
The removed raw reads could throw after the compiler had already diagnosed a
truncated jump, repeat, call, or subtrack-mode command. Such commands now stop
through the compiler's existing unsupported/end transition.

All 21 CTest targets pass. New regression cases cover state carried through
calls, finite/infinite repeat discovery, and 43 truncated control/mode encodings
across the three driver profiles. An ignored differential probe compiles both
old and new Prism implementations with ASan/UBSan: 1,800 generated decoded
programs and source maps match, as do 180 serialized MIDI outputs. The changed
malformed-input behavior is covered separately by the regression cases. No
real-file corpus is configured; these checks establish fixture/generated-input
coverage only.

## Fold bytecode discovery into its only consumer

`decodeBytecode` exposed a generic command-buffer protocol solely to implement
`TrackDecodeScope::decode`. The traversal now lives directly in that scope,
using the concrete session's command count and the already-clamped input bound.
Format-facing APIs and the discovery policy stay unchanged: sequential
continuations are decoded first, pending blocks are visited last-in-first-out,
and already decoded offsets are skipped.

This removes **12 production lines**, an unused generic extension point, and a
redundant command counter. An ignored ASan/UBSan differential probe compares
30,000 generated graphs, including empty/multiple roots, cycles, truncated
bounds, and command caps. Callback order, emitted command order, entry points,
and limit behavior match. All 21 CTest targets pass.

## Use the same scan work loop for one worker

The scanner previously special-cased a worker count of one by scanning index
zero and returning. On a machine reporting one hardware thread, multiple
applicable formats therefore silently lost every scan after the first.
Removing this shortcut lets the existing task loop cover all worker counts;
with one worker it runs serially and creates no asynchronous workers.

This removes **5 production lines** and fixes the skipped-format behavior
without adding a thread-count setting or another execution path. An ignored
ASan/UBSan probe runs the existing session tests with
`std::thread::hardware_concurrency()` replaced at link time to return one.
The old implementation fails the assertion that an unknown-format source is
offered to every processor; the simplified implementation passes all session
tests. All 21 CTest targets also pass in the normal build.

## Retain Mori and Neverland playback data through the existing ownership contract

Mori SNES and Neverland SNES stored borrowed `ByteReader` values in their
persistent runtimes. A program copied out of its scan/session snapshot could
therefore read freed source bytes during later playback. Their sequence/runtime
entry points now take `RetainedSource`, and scanners pass `input.retain()`.
Session scans share their immutable storage; callers with borrowed buffers
explicitly capture it before creating a deferred runtime.

Mori now uses one owning runtime configuration for songs and sound effects.
The sound-effect state reads its script address from the existing layout,
removing a second configuration type and a separately assembled script list.
One `DriverConfig` constructor supplies the same lightweight reader/table view
to playback and immediate synth analysis. Synth analysis does not copy or retain
source bytes. The production changes remove **17 lines** while fixing ownership.

New tests copy programs out of scan results, release the input and result, then
render and verify that releasing the last program releases its retained bytes.
Existing direct-playback fixtures now release their decode buffers before
rendering, including Mori's hardware-only sound effect. Ignored ASan/UBSan
probes compile the old/new scanner and runtime implementations: both old
implementations fail the new ownership assertion, and both new implementations
pass their complete format suites. All 21 CTest targets pass. These are generated
fixtures, not real-file corpus validation.

## Reuse retained sources and layouts in Prism and SoftCreat runtimes

Prism now keeps one `RuntimeConfig` containing its retained source and parsed
layout. Program/track state borrow that immutable configuration, whose lifetime
is owned by `makeCompiledRuntime`. Child scores share it too. This removes the
custom byte-by-byte ARAM capture, copied table addresses, `RuntimeData`, and a
second per-track configuration vector. The small wrapping read methods remain:
a 16-bit driver read at `$FFFF` still reads its high byte from `$0000`.
Source-track numbers still index the layout's dense track order; logical channel
aliases and physical channel flags retain their original meanings.

SoftCreat's sequence entry point now accepts the retained source its runtime
already needs, so Session scans share their immutable bytes instead of copying
ARAM again inside sequence assembly. Together these changes remove **37
production lines**. The ownership regression helper is shared by Mori,
Neverland, Prism, and SoftCreat; direct SoftCreat fixtures also release their
decode buffers before rendering. The warning-free full build and all 21 CTest
targets pass.

## Real-file regression baseline supplied during the audit

A supplied corpus contains 1,547 RSN archives, 120 SPC files, and thousands
of PSF/PSF2 files, including test inputs organized by format. Earlier sections'
lack of corpus coverage describes verification at those earlier points; this
new corpus is now available for subsequent changes.

Ignored comparison runners compile the old/new runtime and scanner units with
ASan/UBSan, using the existing core/export libraries. They scan whole archives,
render every discovered sequence with PlayOnce and zero extra repeats, and
export MIDI, SF2, and DLS with the normal default of one extra repeat.
All **1,323 artifact files compare byte-for-byte**, with identical discovery,
render, and export logs across **441 sequences in 14 archives**:

| Format | Archives | Sequences | Matching artifacts |
| --- | ---: | ---: | ---: |
| Mori SNES | 4 | 132 | 396 |
| Neverland SNES | 3 | 147 | 441 |
| Prism SNES | 3 | 96 | 288 |
| SoftCreat SNES | 4 | 66 | 198 |

The archives cover Gokinjo, Combatribes, CB Chara Wars, Shien, Estpolis/Lufia I
and II, Energy Breaker, Dual Orb I and II, Cosmo Gang the Video, Plok, Equinox,
Maximum Carnage, and Ken Griffey Jr. Baseball. A separate runner compares Prism
from before the discovery-walker change: all 288 exports from its three
archives also match the current implementation, with identical logs.

No standalone PlayOnce render produced diagnostics. Exports retain existing
limitations: repeated mid-note envelope/stereo warnings, and a command-limit
warning in Plok's "Flea Pit" MIDI export (`$2C59`, tick 34337, limit 32768).
Both sides produce identical warnings; matching output does not resolve those
limitations. Across the separate export artifacts, the logs contain 68,166
mid-note envelope messages and 60 phase/pan messages. Warning origin and
aggregation deserve a follow-up, preserving useful source attribution.

These checks compare the value implementation before and after the changes;
they do **not** establish legacy/value equivalence. Original music files are
unchanged and are not added to the repository.

## Remove SoftCreat's second decoded-command buffer

SoftCreat's stateful walker now places first interpretations directly in its
existing `TrackDecodeSession`. Its local map records only the initial decode
state needed to diagnose incompatible revisits. This removes the extra
`DiscoveredCommand` type, a second full command map, and the final transfer loop.
The walker still follows the newly decoded flow for each state and preserves
the permitted per-note-volume suffix difference; source projection still occurs
in address order when the session finishes.

This removes **9 production lines** without a new shared API. All 21 CTest
targets pass. An ASan/UBSan build of the changed decoder repeats the four real
SoftCreat archive comparisons: all 198 MIDI/SF2/DLS artifacts from 66 sequences
match byte-for-byte, with unchanged logs and diagnostics, including the existing
Plok export command-limit warning.

## Share SculptSoft's bounded-phrase discovery

The Standard/Extended and Late SculptSoft decoders duplicated the same pending
phrase queue, visited entry/end pairs, command map, synthetic return insertion,
and playback boundary wrapping. They now use one format-local
`decodePhraseTrack` helper beside the existing phrase reader/stack. The two
opcode decoders remain separate and pass their own typed playback context.

Each stored command carries its incoming and outgoing fine-pitch mode. The
late decoder therefore reuses a decoded state change when revisiting a command,
rather than interpreting its raw opcode again. Conflicting entry modes still
stop that discovery path and report the same diagnostic. Ordinary commands and
synthetic returns check the exclusive phrase boundary before executing;
fine-pitch bytes retain their existing boundary behavior. A real command at a
phrase end still wins over the synthetic return.

This removes **65 production lines**, the late walker's separate interpretation
map, and its extra forwarding function. A new regression covers compatible and
conflicting overlapping fine-pitch streams and real/synthetic phrase ends.
Both old and new implementations pass the complete format suite, including
those cases, when compiled with ASan/UBSan. The warning-free full build and all
21 CTest targets pass.

Real-file comparisons cover Bugs Bunny Rabbit Rampage, NHL Stanley Cup,
Rocko's Modern Life, Return of the Jedi, and Secret of Evermore: **124 sequences
and all 372 MIDI/SF2/DLS artifact files match byte-for-byte**, with identical
logs. Eight Return of the Jedi sequences retain existing rotating-allocation
or overlapping-voice diagnostics; the other four archives render without
warnings. The shared walker does not change those playback limitations.

## Capture compiled command arguments directly

The command body now owns its argument pack directly in its lambda capture.
This removes the intermediate tuple, `std::apply`, and unpacking lambda.
The existing `storedCommandValue` conversion still turns borrowed string views
into owned strings, and invocation still passes const references to typed
playback methods. The full rebuild exercises every compiled format and all
21 CTest targets pass. That rebuild also exposed a shadowed Neverland track
layout name introduced by the retained-source change; the local name is fixed.

## Initial direct legacy/value corpus comparisons

The unmodified parity harness ran 27 independent summary, MIDI, and synth
checks on seven RSN archives and two PSF files. One check passed: the AKAO
summary for FFIX's "The Place I'll Return to Someday". The other checks stop
at their first difference or diagnostic; they do not establish whole-archive
parity or complete DLS coverage. These results were recorded before the
command-capture cleanup.

| Input | First significant findings |
| --- | --- |
| Mega Man X | Full-sustain decay representation, MIDI tuning/controllers and loop endpoints, deduplicated synth instruments |
| Final Fantasy IV | Value discovers 65 collections versus legacy's 64, adding "The Package Opens..." |
| Final Fantasy VI | Full-sustain decay representation, controller/event and loop-end differences, an additional exported preset |
| Axelay | Different percussion/region coverage, MIDI note/controller differences, different preset coverage |
| Contra III | Different percussion/region coverage; MIDI and synth checks stop on existing mid-note envelope warnings |
| Star Fox | Value discovers 53 collections versus legacy's 49 |
| Super Metroid | Decay/tuning differences, MIDI controller and loop-end differences, synth instrument deduplication |
| FFVIII: Balamb Garden | Different region source attribution, MIDI controllers/endpoints, synth instrument deduplication |
| FFIX: The Place I'll Return to Someday | Summary passes; MIDI endpoints/redundant bends and exported sample PCM differ |

The harness compares some destination structures directly and rejects even
warning diagnostics. Synth deduplication, format-preferred sample filtering,
instrument variants, and coordinated loop endpoints need to be distinguished
from musical regressions before changing production behavior. No parity
exceptions or expected failures were added.

## Update session chunk handles directly

Source removal now updates the session's chunk handles directly and removes empty
chunks in one pass, instead of staging a second chunk list and tracking whether
each handle changed. The assets and source maps behind those handles remain
immutable and shared. Collection reconciliation also constructs each replacement
once, preserving an existing collection's ID when its key matches.

This removes 25 production lines and the extra staging state. The source-map
regression test now also checks that retained snapshots keep removed assets and
their original links, and that surviving assets keep their addresses. The full
Debug build and all 21 CTest targets pass without compiler warnings.

## Keep instrument-variant state together by lane

Instrument variant materialization now keeps envelope overrides, pan, and the
sounding voice's endpoint in one lane-state map. This removes three independent
lookup/default paths and the helper that switched between lane-specific and
track-wide voice queries. Instrument selection resets only each lane's envelope;
pan and sounding voices continue. The global stereo query explicitly checks all
lanes, while lane events inspect their own state directly.

The production change removes four lines, but its main benefit is replacing
three partially overlapping state tables with one. A new multi-lane regression
checks independent pan/envelopes, reset behavior, and active-voice diagnostics
after an instrument change. The full Debug build and all 21 CTest targets pass
without compiler warnings.

## Remove export-only intermediates from playback and synth dispatch

Deleted `SynthCollectionView`: synth dispatch now accepts the existing
`SynthExportInput` directly. Each caller states its actual inputs and policies,
without translating through another nearly identical view and a seven-argument
adapter. Playback now uses its own request and consumes MIDI/SoundFont results
directly, removing the temporary `ExportRequest` and two file-artifact wrappers
whose filenames and media types were discarded.

This removes nine production lines, one input representation, and three
unnecessary playback intermediates. Canonical performance ownership, paired
modulation policy, standalone-bank defaults, and failure diagnostics are
preserved. The full Debug build and all 21 CTest targets pass without compiler
warnings, including playback failure cases, used-instrument filtering, and
collection export policy coverage.

## Project synth sample relationships at finalization

Sample links now come from the same `annotateSynthValue` projection as the
finished instrument and region properties. The builder no longer maintains
those links incrementally when regions are appended or source annotations are
added. This removes two member helpers and three synchronization sites while
preserving the completed source graph, link deduplication, and annotation
ownership. Region parenting still records the explicitly established source
hierarchy at annotation creation time.

This removes 13 production lines. Production callers finalize their synth
builders before publishing the source map. The existing mixed call-order test
now also checks that every instrument annotation gets the complete sample-link
set and that a region annotated before its instrument retains its sample link.
The full Debug build and all 21 CTest targets pass without compiler warnings.

## Analyze modulation usage once for paired exports

Collection export now prepares modulation before MIDI and synth serialization,
and both consume the workspace's one observed controller range. The MIDI
rendering helper no longer independently analyzes canonical performance or
applies a second copy of the policy. The shared phase is named
`prepareModulation`, reflecting its role for both outputs; stitching still
combines per-song observations before scaling the complete result.

This removes six production lines and one redundant performance traversal from
paired observed-range exports. The synth fallback still depends on whether a
companion MIDI performance is available. A regression checks MIDI-only,
synth-only, and paired outputs under both scaling policies, including matched
controller expansion and synth-depth reduction. All 21 CTest targets pass and
the full Debug build is warning-free.

## Simplify source-parent traversal state

Parent-cycle validation now records the traversal that first visited each
annotation. Reaching that same traversal means a cycle; reaching an earlier
traversal means the remaining chain has already been checked. This removes the
separate path buffer and the pass that marked every visited node complete.
The source-map ownership index also needs only a visited flag: its cached owner
already distinguishes a resolved owner from an unresolved, unowned cycle.

The two walks remain separate because ownership stops at an explicit owner,
while validation must check that owner's parents too. This removes 12
production lines and two pieces of traversal bookkeeping. Regression coverage
checks shared ancestors, unowned chains, multi-node and explicitly owned
self-cycles, missing parents, and reversed annotation order. The full Debug
build and all 21 CTest targets pass without compiler warnings.

## Trim portamento segments in place

Pitch-transition lowering now removes superseded or empty segments from the
existing note, adjusts the surviving overlaps, and appends its new segment.
It no longer builds a replacement vector by copying every retained segment.
Filtering precedes the edits because overlap adjustment cannot turn an empty
retained segment into an audible one.

This removes six production lines and one temporary collection/allocation per
rewrite. Existing native-portamento, mixed pitch-bend, physical note-limit,
and linked-voice regressions pass with all 21 CTest targets. The full Debug
build is warning-free.

## Share collection reference admission checks

The sequence member and the three asset lists now use one local check for
missing and wrong-type references. The optional sequence still resets itself;
each list uses ordinary erase-if. This removes the separate sequence validation
branch and the callback-based list-filter wrapper. The unused grammatical
article parameter is gone too; every supported role used the same article.

This removes six production lines while preserving issue text, diagnostic
order, and the treatment of missing versus wrong-type members. Existing
collection reconciliation regressions and all 21 CTest targets pass. The full
Debug build is warning-free.

## Remove active-position source lookup

Removed the unused `SourceStore::sourceAt` API. Sources already use stable
`SourceId` values for lookup and ordered snapshots for enumeration; this method
introduced another index whose meaning changed whenever a source was removed.
A repository-wide reference search found only its declaration and definition.

This removes 16 production lines, the scan over active entries, and its separate
out-of-range path. The full Debug rebuild and all 21 CTest targets pass without
compiler warnings.

## Keep scan normalization with its ID allocator

`normalizeScanResult` now implements its asset-ID loop directly in
`ScanTypes.cpp`, beside the allocator it uses. Its declaration was already in
`ScanTypes.h`. Removed the one-use `assignMissingAssetIds` forwarding layer,
`Scan.cpp`, and the corresponding build entry.

This removes 21 C++ lines and one CMake entry without changing the public scan
API or ID-assignment order. The regenerated full Debug build and all 21 CTest
targets pass without compiler warnings.

## Write sequence output to its existing destination

The VM's semantic pass now fills the supplied performance sequence completely,
including its tracks. It no longer returns tracks separately from diagnostics
and source spans, or takes a second reference to the song state already in its
capture. Prepass, analysis, and final rendering still follow their existing
execution order.

MIDI track serialization now appends its header and encoded data directly to
the file buffer. It retains the payload buffer needed to know the track length,
but removes the complete-track buffer and the extra copy into the file.
Together these changes remove five production lines and two intermediate
output protocols. The full Debug build is warning-free and all 21 CTest targets
pass, including VM scheduling, prepass, and MIDI serialization regressions.

## Decode synth pools directly

- Removed the temporary `SamplePoolView` type and pool list from shared
  SF2/DLS preparation. Each bank or external pool now decodes directly into
  the prepared sample table and its existing reference index.
- The reference index stays with preparation instead of being moved into
  the decoder and returned. Pool order, filtering, phase variants, decoded
  sample offsets, diagnostics, and region indexes remain unchanged.
- Removed 15 production lines and one intermediate representation. Existing
  regressions cover mixed local/external pools, inverted samples, filtered
  samples, start offsets, and container index limits.
- Validation: warning-free Debug build; all 21 CTest targets pass (10.65 s).

## Keep stitched timing with its part

- Each internal stitch part now retains its own start tick. Removed
  `ComposedMidi` and its separate positional list of start times; composition
  returns MIDI directly, and result assembly reads each part's timing and banks
  together without matching two containers by index.
- Removed four production lines, one private type, and one parallel vector.
  Timeline arithmetic, overflow handling, bank mapping, and emitted MIDI/SF2
  behavior are unchanged.
- Validation: warning-free Debug build; all 21 CTest targets pass (10.89 s),
  including stitching with different PPQN values, dynamic instrument variants,
  used-only samples, modulation scaling, and channel-state boundaries.

## Build snapshot indexes with their storage

- Removed the private snapshot `Index` wrapper and `buildIndex` handoff.
  Snapshot storage constructs its three lookup maps directly from its owned
  sources, assets, and collections. Lookup methods access those maps directly.
- Shared immutable ownership and stable asset references are unchanged, as are
  invalid-ID filtering and first-entry lookup behavior. No public API changed.
- Removed 16 production lines and one private type. Validation: warning-free
  Debug rebuild of all affected formats and applications; all 21 CTest targets
  pass (10.79 s), including snapshot sharing and retained-revision lifetimes.

## Prepare scanned assets in one admission pass

- Folded the missing-runtime diagnostic into the existing per-asset preparation
  loop, alongside sample-filter policy. Removed the separate diagnostic asset
  walk and its forwarding call to source-range attribution.
- Scan workers now execute and capture failures directly in their work loop;
  removed the one-use `scanAt` callback. Worker bounds, exception capture,
  registry-ordered admission, ID normalization, and diagnostics are preserved.
- Removed 13 production lines and two one-use helpers. Validation: warning-free
  Debug build; all 21 CTest targets pass (10.88 s), including concurrent scans,
  ordered admission, missing runtime errors, and source-backed diagnostics.

## Keep one portamento segment start time

- Removed the separate `PortamentoSegment::startTick`. Each segment's event
  header now receives its actual start when the segment is created; bend
  sampling, trimming, and output all read that same tick.
- Removed three production lines and the deferred synchronization between two
  time fields. Source attribution and event sequencing are unchanged.
- Validation: warning-free Debug build; all 21 CTest targets pass (10.63 s),
  including delayed/mixed pitch transitions, linked voices, and physical limits.

## Share plain annotation assignment

- Ten `AnnotationBuilder` setters now use one private, typed member-assignment
  helper. This removes repeated annotation lookup, absent-builder handling, and
  fluent-return boilerplate while retaining the named format-facing methods.
- Label/kind derivation, field accumulation, and link deduplication keep their
  explicit implementations. Strings are still owned by the annotation; the
  helper performs assignment only when the annotation exists.
- Removed 22 production lines. Validation: warning-free Debug rebuild of all
  affected formats and applications; all 21 CTest targets pass.

## Construct track source hierarchy from its decode scope

- `TrackDecodeSession` now accepts its existing `TrackDecodeScope` directly.
  Removed the seven-argument unpacking path and `createTrackAnnotation` helper;
  source-track creation and trackless ownership are explicit constructor paths.
- Removed ten production lines. Expanded the hierarchy regression to cover
  tracked/trackless sources with and without a parent, plus annotation-free
  decoding for all four cases. Command ownership, root ownership, and optional
  parent links remain unchanged.
- Validation: warning-free Debug build; all 21 CTest targets pass (10.63 s).

## Render the selected performance directly

- Removed the private MIDI-rendering wrapper and its fallback between prepared
  and canonical performances. Standalone export, collection export, and playback
  now call the renderer with the performance they already selected, after the
  existing success check.
- Collection export reuses its bank view after MIDI rendering to select a
  specific synth bank. Removed the second pointer vector; performance and
  modulation preparation still see the complete collection.
- Removed eleven production lines and one helper. Validation: warning-free
  Debug build; all 21 CTest targets pass (10.67 s), including paired/standalone
  output equivalence, selected-bank filtering, output-order independence,
  playback, and rendering failures.

## Write sample headers from their referencing regions

- Removed the SoundFont writer's intermediate sample-header record type and
  helper, copied loop table, and separate assignment flags. The writer retains
  each sample's first referencing region and derives pitch/loop values directly
  when serializing that sample.
- Removed 15 production lines. Added a serialized-output regression for first
  region precedence, loop overrides, sample pitch correction, and unreferenced
  samples' default pitch and padded loop offsets.
- Validation: warning-free Debug build; all 21 CTest targets pass (11.05 s).

## Define envelope fields once in the shared model

- Moved the export layer's field-to-member table beside `EnvelopeFields`.
  Compiler emission, explicit-envelope detection, and export variant updates
  now use that same table. Removed the compiler's six-way assignment branch and
  the export-only field record/alias; format-facing commands remain unchanged.
- Removed 15 production lines. Added VM coverage for all six individual stage
  updates, exact field masks, and future/active voice scope. Existing variant
  and synth-validation tests cover clearing, restoration, and invalid values.
- Validation: full Debug rebuild followed by a warning-free incremental build
  after changing table iteration to references; all 21 CTest targets pass
  (10.79 s).

## Keep source-map indexes with their chunk

- Removed `SourceMap::Index` as a separately shared object. Each immutable
  `Part` constructs and owns its annotation indexes alongside the shared values;
  joined maps retain one pointer per part instead of separately pairing values
  and indexes. This removes one private representation and an ownership pairing
  without adding allocations or rebuilding indexes during joins.
- Annotation sequences still retain the underlying values independently.
  Added a regression that destroys the session and joined map while retaining
  an annotation view, checking order, contents, and pointer identity. Existing
  snapshot/removal tests cover unaffected chunks and earlier revisions.
- Removed three production lines. Validation: full Debug rebuild; all 21 CTest
  targets pass (10.79 s after the final incremental build).

## Assemble RIFF files in the final byte buffer

- The shared RIFF writer now reserves its size field, appends children directly
  into the returned buffer, and fills the size afterward. Removed the complete
  intermediate payload buffer and its final copy for SF2, DLS, and WAV output.
  Child layout, root/child padding rules, and overflow errors remain unchanged.
- This adds two production lines but removes a full-file staging allocation and
  copy, with no new API or helper. Expanded the RIFF regression with a 65,537-byte
  child to check high size bytes and odd-payload alignment, alongside existing
  nested-chunk and exporter checks.
- Validation: warning-free Debug incremental build; all 21 CTest targets pass
  (10.79 s).

## Declare runtime hooks where they are stored

- Removed eight standalone callback aliases used only by `SequenceRuntime`'s
  corresponding fields. Hook signatures are now visible on the runtime itself;
  state factories remain closures and execution hooks remain function pointers.
  The one test naming a callback alias now derives its type from the runtime.
- Removed nine production lines and eight names from the shared API without
  changing callback signatures or the typed format adapter.
- Validation: warning-free full Debug build and all 21 CTest targets pass.

## Finalize each VM track with its cutoff

- `VmTrackExecutor::finish` now closes active notes at the appropriate endpoint
  and applies the optional common sequence cutoff before returning its track.
  Removed the coordinator's separate note-closing pass, track-clipping step,
  and the executor's public note-closing wrapper.
- Source spans are trimmed after all tracks finish, preserving their indexes
  while active notes update them. Early-ending tracks retain their shorter note
  durations; ordinary completion still retains scheduled release tails.
- Removed five production lines. Existing VM regressions cover inactive-track
  note closure, common loop cutoffs, section clipping, finalized source spans,
  and release-tail automation. Warning-free Debug build; all 21 CTest targets
  pass (10.79 s).

## Encode variable-length MIDI values in output order

- Replaced the two-loop integer-packing scheme with one backward fill of a
  fixed byte array, followed by appending its written suffix. Continuation bits
  are assigned with each group; no packed intermediate integer or sentinel
  loop is needed. The array accommodates all ten groups of the input `u64`.
- Removed four production lines. Added exact output checks at every one-to-four
  byte SMF delta boundary, including zero and the largest four-byte value.
- Validation: warning-free Debug build; all 21 CTest targets pass (10.79 s).

## Remove the VM access adapter and analysis handoff

- Removed `VmApiAccess`. The internal track runtime now constructs its own
  borrowed `VmApi`, retaining the private constructor and removing the separate
  access type, forward declaration, and redundant runtime argument at call sites.
- Analysis now returns its completed prepass directly from that branch. Removed
  a second analysis-mode check and the transfer of prepass diagnostics into an
  otherwise unused sequence. The typed analysis adapter still exposes only the
  final program state and requested diagnostics, without a second playback pass.
- Removed seven production lines. Existing tests cover execution with and
  without prepass hooks, exact command/hook counts, prepass diagnostics, wait
  polling, and per-tick callbacks. Warning-free full and incremental Debug
  builds; all 21 CTest targets pass (10.50 s).

## Create synth source annotations through one path

- Asset, sample, instrument, and region annotations now share source-range
  observation, optional annotation-sink handling, owner assignment, explicit
  kind overrides, and entry-source registration in `SynthBuilderSources`.
  Instrument/region range inference and the most recent instrument parent stay
  explicit at their respective call sites.
- Removed the two one-use range-recording methods and three copies of entry
  annotation setup: 26 fewer production lines, with no format-facing API changes.
- Extended existing regressions for invalid source ranges, explicit and inferred
  kinds, and asset bounds without an annotation sink. Existing ownership,
  parent-order, fallback-annotation, and explicit-range checks also pass.
- Validation: warning-free Debug builds; all 21 CTest targets pass (10.78 s).

## Read requested export kinds without a staging vector

- Collection export now borrows the request's kind list, or a static MIDI
  default, through a local span. Removed `requestedKinds` and the allocation/copy
  it performed for every collection, including the default single-artifact path.
  Request order and repeated entries retain their existing meaning.
- Removed seven production lines. The paired-export regression now verifies
  that an empty request produces the same MIDI as an explicit paired request
  under both modulation-scaling policies; existing reversed-order checks pass.
- Validation: warning-free Debug builds; all 21 CTest targets pass (10.78 s).

## Check required draft payloads directly

- Scanner finalization now directly checks the sequence and miscellaneous
  alternatives for their required payloads. Removed a generic visitor,
  compile-time type dispatch, and its nested conditions: eight fewer production
  lines. The separate validation pass still runs before any draft is consumed.
- Extended the incomplete-draft regression to fail once for a missing program
  and again for a missing miscellaneous payload, then complete both. It verifies
  preservation of an earlier instrument bank and the supplied sequence track,
  and acceptance of an explicitly empty miscellaneous payload.
- Validation: warning-free Debug builds; all 21 CTest targets pass (10.78 s).

## Keep operand construction compatible with the current Apple compiler

- The recent Linux-warning cleanup used parenthesized aggregate construction
  through `emplace_back`, which the current Apple toolchain rejects. Construct
  the operand in place, then assign its value and role. This preserves operand
  and field capture without adding a constructor to the shared value type.
- Validation: warning-free macOS Debug build; all 22 CTest targets pass
  (10.91 s). Linux CI was not run locally.

## Simplify shared chunk storage and indexed annotation queries

- `SharedSequence` retains and filters the incoming chunk list directly. One
  prefix-offset table supplies both chunk starts and total size, removing the
  extra chunk-list allocation, stored size counter, and first-chunk lookup case.
  Shared values and their reference lifetimes remain unchanged.
- Source, asset, and parent annotation queries now share one indexed traversal
  across source-map parts. Each query names its index directly; publication
  order and the invalid-asset guard remain intact.
- Removed 15 production lines. Added no permanent test code. Temporary checks
  covered empty/null chunks, checked boundaries, backing identity, and indexed
  result order across chunks; those checks passed and were removed after review
  to avoid growing the suite for these structural refactors.
- Validation: warning-free macOS Debug builds; all 22 existing CTest targets
  pass (10.91 s).

## Use the typed executor as runtime identity

- Removed the separate `SequenceRuntime::family` pointer and its template token.
  `CompiledCommandRuntime<Playback, ProgramState>::execute` already identifies
  the same compatible family independently of captured configuration. Collection
  binding now compares those executor pointers after checking that both exist.
- This removes one stored field, one identity mechanism, and duplicate factory
  initialization (7 production lines). Missing and incompatible runtime
  diagnostics remain unchanged. No permanent test changes; the existing binding
  fixture exercises configuration replacement and rejects a foreign executor.
- Validation: warning-free macOS Debug build; all 22 existing CTest targets
  pass (10.77 s).

## Give each VM track one state owner

- Merged the state-only `VmTrackRuntime` into `VmTrackExecutor`. `VmApi` now
  borrows that executor and the current command directly, removing the runtime
  factory and its separately supplied sequence reference. The format-facing
  API and compiled playback interface remain unchanged.
- Narrowed `LoopDetector` to recording visits and returning prior visit ticks.
  The executor owns flow decisions and uses its current tick as the loop end,
  eliminating the intermediate `LoopPoint` record. Call-stack/repeat identity,
  finite-branch handling, loop markers, and replay policies are preserved.
- Removed two internal types and 37 production lines. No test changes.
- Validation: warning-free macOS Debug build; all 22 existing CTest targets
  pass (10.75 s).

## Round shared fixed-point state without offsetting the accumulator

- `SequenceFixedPointAutomation` now divides once into whole and fractional
  parts. Floor and nearest rounding adjust the whole part by at most one,
  removing the negative-value formula and full-accumulator rounding offsets.
  Nearest still rounds ties away from zero; zero fractional bits remain exact.
- UBSan confirmed that the old floor path negated `INT32_MIN`. The old nearest
  path also overflowed when adding/subtracting half a unit near signed limits.
  The replacement adds one production line and six lines to the existing
  rounding fixture; no new test framework or fixture was introduced.
- Validation: an ephemeral UBSan probe passed every signed/unsigned 16-bit
  accumulator value, signed/unsigned 32-bit boundaries, and zero-fraction cases.
  Warning-free macOS Debug build; all 22 CTest targets pass (10.99 s).

## Invoke compiled bodies at their construction boundary

- Removed `invokeCommand`, which was called only by the captured command body.
  Body construction now owns both typed invocation and conversion of a `void`
  result to empty `Effects`. Its result check reuses `CommandResult`, the same
  rule used by `invokeFlow`, including owned argument conversions.
- Removed one private helper and five production lines. Format APIs, immutable
  captures, invocation order, and effect composition are unchanged. No tests added.
- Validation: warning-free macOS Debug build; all 22 CTest targets pass (10.85 s).

### Emit VM loop markers through the ordinary performance emitter

- Removed `addLoopMarker` and its manual event-header construction. Local,
  synchronized, and playlist loop markers now use `outputAt(...).marker(...)`,
  sharing the same source attribution, playback placement, and sequence-order
  assignment as other VM output. Source commands and marker timing are unchanged.
- Removed one helper and 15 production lines. No test changes.
- Validation: warning-free macOS Debug build; all 22 CTest targets pass (10.84 s).
- Also evaluated replacing note/automation counters with event order or vector
  positions. Retained the counters: event order is wider than the IDs, and
  section transitions trim storage before further emission. Avoid widening the
  model or introducing identity reuse merely to remove two counters.

### Keep MIDI track rendering with its state and output

- Replaced `RenderTrackState` and 41 free rendering helpers with one internal
  `MidiTrackRenderer`. It retains the output track, MIDI channel, options, and
  tempo map alongside the existing voice/controller state. Helpers become
  private operations on that renderer; callers supply only changing inputs.
- One `render` method now owns per-track pitch-range changes, event ordering,
  and final LFO flushing. Sequence-wide lowering, channel assignment, shared
  timelines, and global metadata remain in `renderMidiSequence`. Pure numeric
  and oscillator helpers remain independent. No source-format API changed.
- Removed 33 production lines and 258 repeated call arguments, with no extra
  state wrapper or permanent test changes. All 41 moved helper bodies were
  compared after substituting member access and removing the fixed arguments.
- Validation: warning-free macOS Debug build; all 22 CTest targets pass (10.39 s).
  Temporary captures of the existing core fixtures produced 262 MIDI files
  before and after, all byte-identical. Probe sources, executables, and the
  temporary formatter installation were removed.
- The attempted real-file comparison could not open its five archives because
  the corpus was unavailable. The capture result above covers existing
  fixtures, not real-file or legacy/value equivalence.

## Allocate scan identities before constructing references

- Removed `normalizeScanResult` and the allocator's three `reserveAfter`
  overloads. Scanning now allocates IDs once, before constructing assets and
  their references; admission validates the completed result. This removes a
  late mutation pass and its atomic compare/exchange reservation machinery.
- All 38 registered production scanners already use `ScanResultBuilder`, which
  allocates each asset ID at draft creation. Direct test scanners also allocate
  through `ScanInput.ids`, except deliberately malformed validation fixtures.
  Collection and annotation ID reservation had no callers.
- This deliberately retires the old custom-scanner contract: callers must use
  `ScanInput.ids` (directly or through the builder), rather than relying on
  admission to assign missing IDs or advance past manually numbered assets.
  Late assignment could not repair annotations or collection references, and
  post-scan reservation could not prevent concurrent scanners from choosing
  overlapping IDs. Missing IDs now retain the existing structured validation
  error instead of being silently filled in.
- Removed 54 production lines. Extended the existing invalid-ID admission
  fixture to cover both duplicate and missing IDs; removed a redundant
  normalization call from a range-validation fixture (12 net test lines).
- Validation: warning-free macOS Debug rebuild (300 build steps); all 22 CTest
  targets pass (10.54 s), including concurrent scanning, source ownership,
  collection resolution, and all format fixtures.

## Keep identity allocation with session state

- `SessionState` now owns the scanner allocator alongside its admitted values.
  Moving a `Session` transfers that existing state, so the allocator no longer
  needs custom move operations that copy atomic counters. The two remaining
  allocation operations are simple inline atomic increments; removed
  `ScanTypes.cpp` and its build entry.
- Collections are created only by `SessionState`, on the session's thread.
  Their ID counter now lives there as an ordinary integer. Removed collection
  allocation from the scanner API, allocator arguments from collection
  creation/reconciliation, and the defensive search through every existing
  collection. Both creation paths share the counter; reconciliation retains
  existing IDs, and removal does not reset it.
- Removed 53 C++ lines and one CMake entry. Added only two lines to the existing
  snapshot fixture to move-construct and move-assign a populated session before
  scanning another source. Its existing assertions check continued admission,
  source-map ownership, and preservation of earlier immutable snapshots.
- Validation: warning-free regenerated macOS Debug build; all 22 CTest targets
  pass (10.52 s), including concurrent format scans and mixed discovered/manual
  collections.
  The corpus was unavailable, so no real-archive comparison was run
  for these identity changes.

## Derive PSF image bounds from the byte buffer

- The shared PSF executable-image loader no longer stores an end address beside
  its start address and bytes. Overlay growth follows the buffer size directly,
  and the first payload uses the same copy/zero-tail path as later libraries.
  This removes a separately maintained bound and the special first-overlay
  implementation across PSF1, SSF, GSF, and DS executable payloads.
- Overlay input is now a byte span rather than a separately passed pointer and
  count. Declared image size remains distinct because GSF may omit a zero-filled
  tail, including one that overwrites bytes from an earlier library.
- Removed ten production lines and added no permanent tests. A temporary
  differential probe ran the old and new functions through 200,000 generated
  operations, comparing both resulting bytes and exceptions. It covered
  overlapping and disjoint ranges, lower-address insertion, empty payloads,
  zero tails, and the 32-bit address boundary under ASan and UBSan. All matched;
  the probe source, executable, and debug bundle were removed.
- Validation: warning-free full macOS Debug build; all 22 CTest targets pass
  (10.42 s), including SSF extraction, miniGSF library overlays, and GSF sparse
  tails.

## Share whole-file source loading

- Moved the session's binary file reader into the source layer and reused it
  for PSF libraries. Both callers now share opening, sizing, reading, and error
  checks instead of maintaining the same algorithm twice. Session resolution,
  deduplication, and retained-byte behavior are unchanged. CUE loading retains
  its bounded reads, which avoid loading entire disc images.
- PSF library I/O failures now use the shared path-bearing source-file error
  inside the existing library warning. Library resolution, recursive loading,
  overlay order, and exception handling are unchanged.
- Removed 17 production lines and added no tests. Existing session fixtures
  exercise binary reads, path resolution, caching, and reloads; miniGSF and
  miniPSF2 fixtures load their libraries from disk.
- Validation: warning-free full macOS Debug build; all 22 CTest targets pass
  (10.55 s).

## Keep decode addresses intact until bounds checking

- Track discovery now queues the existing `Address` values directly and keeps
  source positions at their original width. The decoder receives a 32-bit
  offset only after it passes the bytecode window check. This removes the
  manual entry-point conversion loop and four production lines.
- The audit found that narrowing before validation made a 4 GiB source appear
  empty and allowed oversized entry points or sequential continuations to wrap
  into valid bytecode. Source-size clamping and address checking now happen
  before narrowing. Exceptional walkers also retain exact source positions in
  their shared decode session.
- Added 13 lines to the existing control-flow fixture for oversized entry
  points and continuations. A temporary ASan/UBSan probe reproduced all three
  failures against the old implementation and passed against the new one,
  including an actual 4 GiB virtual-memory source span. Probe sources,
  executables, and debug bundles were removed.
- Validation: warning-free macOS Debug build; all 22 CTest targets pass
  (10.91 s).

## Resolve VM loop options once per render

- Resolve the requested loop policy against the program's default at the
  render boundary. The sequence coordinator, track executors, and playlist
  runner now read the same immutable options value during both the prepass
  and ordinary playback.
- Removed the track executor's repeated default resolution, two separately
  stored policy fields, and the playlist runner's extra policy argument.
  This reduces duplicated state and responsibilities without changing the
  public API or adding another options type. Production line count is unchanged.
- Validation: warning-free macOS Debug build; all 22 CTest targets pass
  (10.98 s), including default and explicit loop policies, requested replays,
  synchronized loops, and section playlists. No tests were added.

## Share the PCM WAVE format record

- WAV and DLS now use the same encoder for their 16-byte PCM format record.
  DLS appends its own empty extension, preserving its 18-byte WAVEFORMATEX
  payload; standalone WAV retains its minimal header. Removed 15 production
  lines without introducing a new file, data type, or policy layer.
- Both writers retain the same channel and sample-rate defaults. DLS now uses
  WAV's byte-rate overflow check instead of silently wrapping the field.
  Added 11 lines to the existing DLS fixture for this regression and its
  extension field; no new test fixtures were added.
- A temporary before/after probe compared 60 complete WAV/DLS files across
  five channel counts and six sample rates, including defaults and the highest
  representable byte rates. All bytes matched. Probe sources and binaries were
  removed.
- Validation: warning-free macOS Debug build; all 22 CTest targets pass
  (10.97 s).

## Derive source classification and liveness from existing state

- Removed `SourceKind`: the parent relationship already identifies every
  admitted derived source. `derived()` now reads that relationship directly,
  eliminating a flag that could disagree with the source family. Session's
  user-input path detaches a copied description's extraction parent; derived
  admission continues to validate and assign its parent.
- Removed `SourceStatus` and `SourceFile::active()`. Only SourceStore consumed
  that status, and removal already clears the entry's byte pointer. Store
  queries now use that pointer directly; descriptions and snapshots no longer
  carry a redundant lifecycle field. Empty byte vectors remain active, and
  retaining shared inspection bytes does not keep a removed store entry active.
- Removed two enums, two stored fields, their duplicate updates, and 14 net
  production lines. Extended two existing session fixtures by eight net lines
  for root/child classification, reimported descriptions, empty files, and
  retained bytes. No new fixture or test infrastructure was added.
- Validation: warning-free full macOS Debug build; all 22 CTest targets pass
  (10.37 s), including nested archive removal/reload, snapshot lifetime, and
  missing/removed parent handling.
  The supplied corpus volume was unavailable during this change.

## Borrow RIFF children instead of copying their payloads

- RIFF and LIST assembly now accept a read-only span of child chunks and a
  string view for the four-byte type. They only serialize those inputs and
  never retain them, so ownership no longer crosses this boundary.
- Fixed SF2/DLS chunk lists use arrays, including the fixed four INFO and nine
  preset-data records. This removes the initializer-list-to-vector copies
  that previously copied payloads even when callers wrote `std::move`.
  Variable-length region, instrument, and wave lists remain vectors and pass
  directly to the same span API. No new helper or custom type was introduced;
  production line count grows by two required includes.
- A temporary before/after probe produced eight complete SF2/DLS files from
  empty, odd-sized, and large PCM inputs with normal and phase-inverted regions.
  All output bytes matched. For a 1 MiB source, cumulative allocated bytes
  fell from 48,249,785 to 39,858,201 for DLS and from 46,146,847 to 37,755,979
  for SF2. These are probe allocation totals, not peak-memory measurements.
  Probe sources, binaries, and generated files were removed.
- Validation: warning-free macOS Debug build; all 22 CTest targets pass
  (10.08 s). Existing RIFF fixtures only needed their call syntax updated;
  no tests or test lines were added.

## Keep wait predicates as capture-free function pointers

- Removed `CommandPredicate` and its owning `std::function`. The compiler's
  `duringWaitWhen` API installs a capture-free function that reads the current
  typed Playback state; it has no closure data to retain. `CommandExecution`
  now stores that function directly as a nullable pointer, like the runtime
  hooks. Command bodies still own their captured operands and behavior.
- Format calls and VM polling stay unchanged. On this x86-64 Apple Clang 15
  build, `CommandExecution` shrinks from 112 to 64 bytes and `SourceCommand`
  from 208 to 160 bytes (48 bytes, about 23%, per decoded command). This also
  removes one alias and one net production line. A temporary size probe was
  removed.
- Existing compiler and format fixtures cover gated commands, wait boundaries,
  and rejection of time advancement during a wait. No tests were added or
  changed.
- Validation: warning-free full macOS Debug build (326 steps); all 22 CTest
  targets pass (10.12 s).

## Share exact signature matching across byte and string inputs

- Both `matchesBytes` overloads now use the same bounded span comparison.
  String signatures expose their byte representation to that path, preserving
  unsigned matching for high-bit bytes. `std::ranges::equal` replaces both
  hand-written loops and their per-byte reader calls.
- Removed 16 production lines without adding types or changing scanner call
  sites. Empty signatures retain the same boundary rules: they match at the
  source end, but not beyond it. Masked search remains separate because its
  wildcard and anchor-search rules differ.
- Validation: warning-free macOS Debug build (52 steps); all 22 CTest targets
  pass (11.01 s). No tests were added or changed.

## Keep only the tempo map's physical timing state

- `PerformanceTempoMap` retains its normalized pulse count directly instead of
  copying a full `Timebase` and repeating the zero-division fallback and numeric
  conversion in all three timing queries. The MIDI division hint is not part
  of its retained state.
- Millisecond-to-tick conversion derives completed ticks from `cursor -
  startTick`; it no longer updates a second counter alongside the current
  position. Tempo boundaries, half-down rounding, and saturation stay explicit.
- Removed four net production lines with no API or test changes. Existing
  timing fixtures cover tempo boundaries, large internal divisions, initial
  tempo ownership, rounding limits, and invalid durations.
- Validation: warning-free macOS Debug build (186 steps); all 22 CTest targets
  pass (9.76 s).

## Evaluate scoped threads and C++23

- `runFormatScans` is the only standard-thread launch site in the value core.
  Replacing its `async`/`future<void>` workers with `jthread` would remove the
  explicit join loop and `<future>`: four net production lines. Scheduling,
  per-format exception handling, and ordered admission would remain. Existing
  async futures already wait during unwinding; no measured speedup or new
  cancellation behavior follows from changing the worker type alone.
- [Xcode 26 release notes](https://developer.apple.com/documentation/xcode-release-notes/xcode-26-release-notes)
  confirm native `jthread` and stop-token support without experimental flags.
  Xcode 26 requires macOS 15.6 or later on the development machine. This is
  separate from the application's macOS 13 deployment target, which would
  still need verification with the new SDK. C++23 is not required for `jthread`.
- The [Josuttis reference implementation](https://github.com/josuttis/jthread)
  at `0fa8d394254886c555d6faccd0a3de819b7d47f8` passes temporary AppleClang 15
  probes for joining scan work, joining during exception unwinding, and
  cooperative stopping. Its two headers total 740 lines and define types in
  `std`; adoption would require compatibility integration and further review.
  [jthread-lite](https://github.com/nonstd-lite/jthread-lite) explicitly lacks
  functional cancellation. Prefer native support over a dependency for this
  small cleanup; no library was added.
- C++23 is a feasible future baseline, with incremental benefits from range
  collection, byte swapping, and enum conversions. `expected` is not a direct
  replacement for result objects that retain diagnostics on success, and the
  shared builders do not have substantial CRTP or const-overload duplication
  to remove with explicit object parameters. Retain the current standard until
  adopting a feature provides a concrete improvement.
- All 225 value core/format translation units pass a warning-free C++23
  **syntax-only** check using this machine's `-std=c++2b` spelling (71.1 s).
  This did not link the application or run it under the new language mode.
  With AppleClang 15 and the configured macOS 14.5 SDK, isolated probes targeting
  macOS 13 compile, link, and run on this macOS 14 host for `byteswap`, `expected`,
  optional `transform`, `ranges::to`, `views::zip`, `to_underlying`, and string
  `contains`. `jthread`, `move_only_function`, and explicit object parameters
  remain unavailable. No production or permanent test code changed; temporary
  probe files were removed.

## Validate source parents at the insertion boundary

- `SourceStore::add` now checks that a supplied parent exists and remains
  active. `addDerived` only selects the parent before delegating. This removes
  a local variable and ensures both public insertion paths enforce the same
  ownership rule, with two fewer production lines.
- Direct insertion previously admitted an orphan when its member path was
  empty, or could index a missing parent's member table for a named member.
  A temporary before/after probe confirmed orphan admission before the change
  and rejection without mutation afterward. The existing parent-validation
  fixture now exercises both entry points with missing, self, and removed
  parents; test code grew by five net lines.
- Validation: warning-free macOS Debug build (26 steps); all 22 CTest targets
  pass (10.44 s). Temporary probes were removed.

## Inline named operands replace the command-table experiment

- The first table implementation (`8cc140397`) shortened 80 commands but added a
  second dispatch mechanism beside the ordinary compiler blocks. Replacing it
  with named operands in `CompilerCursor` keeps every opcode in its original
  switch, including aliases, version checks, and control flow. `CommandTable`,
  its entries, decoder callbacks, optional lookup, and implicit member-pointer
  assignment convention are removed.
- Existing `invoke`, `invokeFlow`, `set`, and `add` actions now accept encoded
  fields alongside ordinary C++ values. For example:

  ```cpp
  case 0xd9:
    return cursor.command("Vibrato", SequenceSemantic::Modulation)
        .invoke<&Playback::vibrato>(Byte{"delay"}, Byte{"depth"}, SignedByte{"rate"});
  ```

  The compiler reads fields left to right through the existing annotated
  readers, then captures only their decoded values. Operand names, displays,
  and roles use the same argument order as those readers. State assignment
  still uses `set`; runtime flow still uses `invokeFlow`. Actions compose with
  the existing builder operations, including `duringWaitWhen`, without table
  options or a second command representation. SequenceVM and exporters are
  unchanged.
- Applied to 267 command blocks across 25 formats. Relative to the table commit,
  this removes **247 format lines and 45 shared C++ lines**, or **292 production
  lines overall**. Relative to the pre-table baseline (`f6c4aedbd`), the combined
  change removes **448 format lines and 372 production C++ lines overall**.
  The benefit beyond line count is that routine operands need no local names,
  explicit read sequencing, table registration, or a separate dispatch path.
- Keep direct reads for format-specific decoding, reused values, and source
  inspection. Do not mix immediate reads and named operands in one call: C++
  evaluates immediate reads before entering the action. Already-decoded values
  can freely accompany named operands. The format migrations preserve encoded
  read order and leave driver calculations and discovery logic explicit.
- Reused existing compiler fixtures for ordered and truncated reads, signed and
  variable-length fields, state updates, inline handlers, mixed ordinary/encoded
  arguments, during-wait execution, and execution after source destruction.
  An existing inline handler also receives a plain pair, guarding against tuple
  deduction accidentally splitting one compound argument into several. Test
  code grows by **five net lines** from the table commit (eight from pre-table);
  no test files or permanent harnesses were added.
- Validation: warning-free macOS Debug build; all 22 CTest targets pass. A
  temporary before/after decoder probe compares **184,320 cases**: every opcode,
  eight payload patterns, lengths 1–10, and nine configurations spanning the
  original six formats and their HeartBeat/Suzuki versions. Field values,
  ranges/displays, semantic operands, flow, presentation, execution metadata,
  and diagnostics match exactly. This probe compares decoded metadata; the
  existing playback suites and the real-file comparisons below exercise bodies.
- Real-file before/after comparisons use the supplied music corpus
  and the pre-table decoders/`CompilerCursor` from `f6c4aedbd`, linked against
  the same loaders, VM, and exporters. Of 25 inputs, 24 yield **1,184 sequences
  across 21 formats**. All **1,184 MIDI, 47 SF2, and 47 DLS files match byte for
  byte**, with no empty artifacts. MIDI covers every discovered collection;
  sound banks cover the first two collections per input. Both HeartBeat SNES
  versions and all three Suzuki SNES variants are represented. Scan, rendering,
  and export diagnostics match, including the existing export limitations.
  The remaining PSF ("Chocobo's House") yields no sequences in either
  version, with identical diagnostics. These are value-before/value-after
  checks, not assertions of legacy/value parity or full game coverage.
- Temporary probe sources, objects, binaries, and export payloads were removed.

## Immediate cursor reads replace named operands

The named-operand experiment reduced forwarding code but introduced a second
reading convention: `event.u8(...)` consumed bytes immediately, while `Byte{...}`
consumed them inside a binding operation. Mixing them could silently change source
order. This iteration removes that distinction and deletes `CommandOperands.h`.

`CompilerCursor` now exposes all source reads, annotations, lookahead, and source
diagnostics directly. Its `Event` builds playback behavior and presentation.
`invoke` and `invokeFlow` accept a braced tuple whose types come from the playback
method or typed lambda. C++ evaluates those initializers in written order; the
binding operation receives only decoded values. Zero-argument bindings remain
ordinary calls. SequenceVM, command bodies, and exporters retain their existing
architecture.

```cpp
return cursor.command("Vibrato", SequenceSemantic::Modulation)
    .invoke<&Playback::vibrato>({cursor.u8("delay"), cursor.u8("depth"), cursor.s8("rate")});
```

The same immediate reader works inside arithmetic, conversions, and direct output
operations. Local variables remain for dependent fields, validation, reordered
playback parameters, reused values, and snapshots of the cursor position.
Reader-only helpers take the cursor; helpers that also build behavior receive the
event as needed. Multiple loose playback arguments are no longer accepted. Other
multi-input event helpers were inspected: no current call supplies multiple direct
source reads as separate arguments.

The migration covers 38 format families. Relative to `4003a7763`, format code is
341 lines smaller and shared production C++ is 39 lines smaller: 380 fewer lines.
Relative to pre-table `f6c4aedbd`, production C++ is 752 lines smaller (789 fewer
format lines and 37 additional shared lines). Existing tests grow by 13 net lines;
there are no new permanent test files or harnesses.

Validation:

- Warning-free macOS Debug build; all 22 CTest targets passed. The core target
  passed again after extending its existing ownership fixture to cover both
  direct output and a bound `string_view`. Existing fixtures exercise field
  order, compound arguments, truncation, control flow, and source-free playback.
  One compile-time assertion rejects loose multi-argument binding.
- A textual audit found identical normalized source-read/annotation expressions
  and lexical order for all 2,120 calls in the changed format files.
- Before/after shell comparisons used 35 corpus inputs and detected 1,357
  sequences across 30 formats. All 1,357 MIDI, 60 SF2, and 60 DLS artifacts were
  byte-identical, with no empty artifacts. Inventory, diagnostics, and inspection
  output (source trees for up to two sequences per input) also matched.
- Existing failures remain visible: Super Metroid fails NinSnes scanning,
  the selected MP2k GSF reports missing sample references during export, and
  the previously noted Chocobo PSF yields no sequences. Star Fox supplies
  successful NinSnes coverage. These comparisons establish preservation against the previous value
  implementation, not exhaustive format coverage or legacy/value parity.
- Temporary executables and comparison scripts were removed after validation.

## Review follow-ups for immediate cursor reads

The binding API now rejects an empty argument group when the playback method or
lambda requires values. Its small internal argument holder requires the exact
argument count before constructing the tuple; it performs no decoding. Eight
lines of compile-time checks cover both `invoke` and `invokeFlow` for methods and
lambdas in the existing cursor test file.

Akao consumes and annotates its subcommand opcode once, before dispatch. Shared
helpers now construct complete commands instead of accepting both cursor and
event. This removes the subcommand builder wrappers, the preservation helper,
and repeated command setup at callers. Labels, operand order, branch roles, and
version-specific layouts remain intact. The format file is 43 lines smaller.

Examples beside `CompilerCursor` show independent inline reads, a transformed
value, and locals for validation or reordered arguments. They also explain that
braces do not order multiple reads inside a single expression.

Validation: warning-free macOS Debug build and all 22 CTest targets passed.
Before/after comparisons using Final Fantasy VII, SaGa Frontier, and Final
Fantasy VIII inputs matched all 85 MIDI, three SF2, and three DLS artifacts byte
for byte. Inventories, diagnostics, and full source trees for up to two sequences
per input also matched. Temporary comparison tools and exports were removed.
No new permanent test files were added.

## Fade declarations use the source motion plan

`PerformanceBoundValue::begin` now takes the emitter, performance target, converted
endpoint, and source motion plan. The plan supplies duration, delay, and timed or
step-based progression for both the motion and its performance intent. This
replaces the overload that accepted an independently constructed binding.

Migrated 19 declarations across eight formats and removed KonamiSnes's second
timed-versus-step decision. Raw and converted endpoints remain explicit; driver
tick ordering, zero-duration handling, and raw-only motion are unchanged. The
performance model still retains high-level intent and linked individual events.
The benefit is fewer facts to keep consistent, with little change in line count.

Validation: warning-free macOS Debug build and all 22 CTest targets passed.
Ten lines of assertions in existing fixtures cover delayed intent, converted
endpoints, and Konami's step-based intent. Temporary probes also checked all
three motion modes with delayed, linked output points. No new permanent test
files were added.

## Execution streams and musical channels

SequenceVM now schedules execution streams. Each stream owns its instruction
position, waits, call stack, repeat counters, and optional typed format state.
Each of its channels owns instrument/controller state, active notes, and a
performance track. Ordinary formats retain one stream per channel without
changing their Playback methods.

SonyPS1, SegSat, and SonyPS2 now execute interleaved commands once. Decoders route
musical commands with `event.channel(number)` and express leading time with
`event.delay(ticks)`. Playback methods use the selected channel and current
emitter directly; they no longer filter channels, pass deltas through musical
handlers, or construct delayed emitters. SegSat's duration extension and counted
phrase state, and SonyPS2's tempo and section state, belong to the stream.
SonyPS2 MIDI, SE voice streams, and song playlists all use this model. Playlist
commands now declare one `streamStarts` entry per executor, rather than repeating
the same entry for each output channel.

Source-channel annotations remain independent of routing: a channel-tagged loop
still controls the whole stream. Source references retain their decoded command
identity, while source playback spans are recorded once per execution. An
undeclared destination skips the musical body but consumes the leading delay.
Global commands use the stream's first output channel. Tick hooks still run for
each channel during waits. Entry output runs once per channel before the first
delay, including at playlist section boundaries.

Loop detection records arrival before a command's leading delay. This keeps the
complete loop interval, including silence before its first event. The migration
also exposes a prior value-core timing error: declaring a loop while scheduling
its delay afterward could stop playback before consuming that delay. The new
interleaved readers execute that boundary at its event time; SegSat's legacy
reader also advances time before processing the loop. Export comparisons must
therefore distinguish this correction from unintended changes.

This checkpoint deliberately retains HeartBeatPS1 and NamcoSnes as independent
streams sharing decoded command storage. It does not introduce a second VM or
change the performance/export interfaces. The three migrated format files lose
50 lines and their handlers have fewer responsibilities, but total production
code grows by 119 lines because stream/channel ownership and lifecycle are now
explicit. Test code grows by 59 lines, with no new permanent test files. Treat
this as a readability and execution-model improvement, not yet a large reduction
in total code.

Validation: warning-free macOS Debug build and all 22 CTest targets passed.
One compact VM test covers shared execution, independent channel state, source
attribution, absent destinations, and initialization before delays. The existing
loop fixture now covers delayed commands across two channels for declared,
candidate, and inferred loops, including one additional playback repetition.

Temporary before/after export comparisons covered 47 generated input files,
48 sequences, and 93 MIDI/SF2/DLS artifacts. Eighty-eight artifacts matched byte
for byte. The five changed MIDIs belong to three infinite-loop fixtures with
nonzero loop-end delays: SonyPS1 and SonyPS2 retain the previously truncated
trailing time; SegSat also retains notes previously discarded at that cutoff.
All SF2/DLS outputs matched. Finite repeats, SegSat counted-event phrases and
duration extensions, SonyPS2 section resets and SE voice routing, and interleaved
same-tick events were included.

The music corpus was unavailable, so real-archive parity remains unverified
for this checkpoint.

## Stream construction and section entry cleanup

Kept `TrackProgram`, `TrackState`, the track-facing helpers, and the term "track"
for multi-track sequences. Comments distinguish decoded source tracks from the
playback tracks/channels served by a stream; no blanket channel rename was made.

Stream state now receives `StreamStateContext`, containing the sequence and its
decoded source track, without an arbitrary first channel's number. Track state
continues to receive its existing track identity through `TrackStateContext`.

Section entry has two ordered hooks at one execution boundary: first
`StreamState::beginSection(bool first)`, then each track's
`Playback::beginSection(bool first)`, before the first command delay. Track
resets and initial output share the Playback hook; the separate state-only track
reset hook and `beginPlaybackSection` convention are gone. `first` means the
stream's first actual entry, so inactive tracks initialize when they first play.
Per-command driver work still belongs in `beforeCommand`.

SonyPS2 no longer coordinates section output through `initialized`,
`sectionStarted`, or `resetSectionState` flags, and section pan no longer needs
an optional value. NinSnes resets live beside playback setup; Quest's initial
volume/pan setup no longer needs an `initialized` guard on every command.
No additional formats were migrated to shared execution streams.

This follow-up removes 17 production lines. Existing VM fixtures now exercise
stream construction, once-per-stream entry, initial/later entry, and inactive
tracks that start later. Replacing the playlist probe's manual erased callbacks
with its typed Playback keeps test growth to five lines, with no new test files.

Validation: warning-free macOS Debug build and all 22 CTest targets passed.
All 98 MIDI/SF2/DLS artifacts from 52 generated inputs (53 sequences) matched the
pre-cleanup executable byte for byte. This includes five additional NinSnes
Sunsoft/Quest fixtures with later sections and track reactivation.
The music corpus was unavailable for this checkpoint.

## HeartBeatPS1 migration and completion review

HeartBeatPS1 now executes one stream serving its declared playback tracks.
Decoder routing selects the musical channel; every event carries its leading
source delay. Musical handlers no longer accept channel/delta arguments, filter
channels, construct delayed emitters, or return a wait. LFO/envelope helpers use
the current emitter directly. Source-only events need no playback handler.

Tempo and PPQN belong to stream state, initialized from the sequence itself;
the runtime settings no longer duplicate them. Track state retains instruments,
controllers, and voice history. Initialization uses beginSection instead of an
initialized flag checked on every command. Channel-tagged loop commands remain
global, including loops encoded on channels outside the declared track count.
HeartBeatPS1's sequence implementation loses 39 lines in this pass.

One compact case in the existing HeartBeatPS1 tests exercises finite/infinite
repeats across two tracks, initialization before a leading delay, same-tick
source order, portamento after a tempo change, ignored-event timing, and delayed
loop/end boundaries. It reuses the existing container fixture. Permanent test
code grows by 41 lines; no test files or permanent export-comparison harnesses
were added.

NamcoSnes assessment: it has a shared command clock and flow, not physically
independent sequencers. Its masked note/control commands address several voices
in one opcode, and its active/slur masks can also change unselected voices.
Its per-voice tick processing must continue during waits. The current scalar
routing API cannot express these broadcasts followed by one shared wait.
Executing an Effects-returning body once per selected voice would require rules
for combining waits/branches; splitting stream and voice command phases would
introduce another execution convention. Neither is justified by removing a few
mask/global-event guards. Retain its current independent executions for now,
document that compatibility arrangement at construction, and revisit only with
a simpler, demonstrably useful broadcast design. No NamcoSnes playback behavior
was changed.

The final shared-code pass consolidated initial and subsequent playlist entry
through one local beginSection operation. Comments now distinguish decoded
tracks, execution streams, and output tracks accurately, including the playlist
completion policy. TrackProgram, TrackState, TrackStateContext, and ordinary
track terminology remain intact. No additional lifecycle API was introduced.

Validation: warning-free macOS Debug build and all 22 CTest targets passed.
Before/after comparisons covered 74 generated inputs, 75 sequences, and 186
MIDI/SF2/DLS artifacts. 180 artifacts matched byte for byte. The six differing
MIDIs are standalone/collection exports of three HeartBeatPS1 cases: an explicit
terminal delay adds three ticks, and a delayed infinite-loop boundary retains
four previously truncated ticks. Parsing every MIDI event confirmed that only
end-of-track timestamps changed; notes, controllers, and every other event
matched. All SF2/DLS outputs matched. HeartBeatPS1's legacy reader also advances
time before processing commands.
The music corpus was unavailable, so this does not claim real-archive parity.

This pass removes 37 production lines overall. Relative to the pre-refactor
core-rewrite checkpoint, the four migrated sequence implementations lose 106
lines, while production code overall grows by 65 lines and tests by 105.
The benefit is a more accurate ownership model and simpler musical handlers,
not a large total line-count reduction. The four direct migrations and focused
lifecycle cleanup form a coherent stopping point; forcing NamcoSnes through a
new broadcast abstraction would weaken that result.

## Execution stream real-archive verification

The music corpus became available for this follow-up.
Compared the saved pre-change executable from `86bcee165` against `fa9bcaf88`,
covering the complete stream/channel change and lifecycle cleanup. This is a
before/after value-architecture comparison, not a claim of legacy parity or
exhaustive corpus coverage. No production changes or permanent tests were needed.

The run attempted 329 inputs and compared 926 unique sequences from 326 inputs:

| Format | Sequences |
| --- | ---: |
| SegSat | 346 |
| NinSnes | 266 |
| SonyPS1 | 106 |
| HeartBeatPS1 | 76 |
| NamcoSnes | 62 |
| SonyPS2 | 35 |
| CapcomSnes | 35 |

Selection sampled PSF/PSF2/SSF inputs across game folders, up to five evenly
spaced sequences per input, all 76 Dragon Warrior VII miniPSFs, and every
discovered sequence in the selected SNES archives. Exports used one extra loop
repeat, standalone and collection MIDI, and SF2/DLS for up to three collections
per input. Supplemental exports recovered the pre-existing failures below.
The 2,835 artifact comparisons include those supplemental exports and multiple
export paths for the same sequence; they are not 2,835 distinct songs.

All 477 SF2 and 477 DLS comparisons matched byte for byte. Of 1,881 MIDI
comparisons, 1,358 matched byte for byte. The remaining 523 match the previously
identified correction to delayed loop/end boundaries: 168 change only
end-of-track timestamps, and 355 also retain final events or extend final
note-offs at the old cutoff. Parsing every MIDI event, including metadata and
SysEx, confirmed identical ordered events strictly before each track's old
cutoff, identical MIDI headers, and no earlier track endings. Asset/collection
inventories matched for every attempted input. No new export diagnostics or
unexplained changes were found.

Successful SNES archives include Super Mario World, Ogre Battle, Tactics Ogre,
Wagyan Paradise, Street Fighter 2, Albert Odyssey 2, Benkei Gaiden, and Hashire
Hebereke. These exercise unchanged formats and NinSnes section behavior in
addition to the four migrated formats. All their compared exports matched.

Three inputs failed scanning in both executables and are excluded from the
926-sequence comparison. Super Metroid reports an invalid performance automation
binding; Porky Pig's Haunted Holiday and Batman Revenge of the Joker report no
supported music data. These are existing limitations, not successful checks.

Six inputs encountered matching export failures in both executables. Quiz
Nanairo Dreams has a title unusable as a macOS filename; Ogre Battle and Street
Fighter 2 have collection titles containing slashes. Temporary user collections
with safe names and the same members allowed their exports to be compared.
Syphon Filter and the two Atelier Marie/Elie inputs lack discovered sound banks;
their MIDI-only exports succeeded and matched. The workarounds changed neither
the source archives nor production code.

The 35 SonyPS2 inputs expose MIDI assets. Inspection of their embedded SQ members
found only trivial single-MIDI song wrappers, so nontrivial SonyPS2 song playlists
and SE voice routing remain covered by generated fixtures rather than this real
archive run. The observed endpoint changes and these coverage limits preclude
claiming universal byte parity, but this run found no regressions in the tested
scope.

No corpus data or new permanent comparison harness was added to the repository.

## Further investigation

- Keep test growth proportional to behavioral risk. Prefer existing coverage
  for structural refactors and extend existing fixtures for meaningful gaps;
  use temporary probes when additional investigation needs no lasting regression.

- Per the user's clarification, prioritize shared architecture over individual
  format cleanup: remove layers and duplicated state from compilation, VM
  execution, session ownership, and export preparation. Use formats and the
  corpus to verify shared changes. Line count alone is not sufficient justification.
- Shared-stream channel routing must preserve unconditional time advancement
  and startup output. Source-channel metadata alone cannot gate execution:
  some channel-encoded loop commands control every playback track. Moving
  future-dated output to execution after a wait also changes initialization
  timing, so it requires more than removing channel guards.
- Static instrument discovery can include every reachable bytecode branch,
  while recipe analysis may depend on executed driver state. Preserve that
  distinction when considering a shared replacement for format reference sets.
- Keep future instrument selection distinct from sounding-voice state when
  simplifying export preparation. The review follow-up above now preserves
  that distinction through native-portamento fragments and used-only banks.
  Preserve bend-base inheritance after portamento splits and the existing
  cancellation, onset, lane, and predecessor rules in further simplification.
- The shared native-region response model is now in production for SonyPS2.
  Further adopters should remove actual native grid/budget machinery and retain
  immutable value captures. Preserve expansion before attack-time variants and
  owned prepared regions; avoid layering another generic response framework on
  top of the two shared synth-preparation helpers.
- Use the supplied music corpus for subsequent regression checks.
  Extend the current before/after value comparisons to additional formats and
  run the existing legacy/value parity modes where applicable. Keep baseline
  differences separate from regressions introduced by a simplification.
- Investigate the Plok "Flea Pit" MIDI command-limit warning and the volume of
  existing envelope/stereo export warnings. Do not hide meaningful limitations
  merely to make corpus runs appear clean.

## Design decisions retained after inspection

- Keep source-link intent separate from VM jump policy. A temporary capture
  found 39 of 89 fixed targets with intentionally different roles: source
  pattern calls may execute as VM jumps, and repeat targets may use declared
  loops. Only 49 matched the obvious inference, with one unannotated target.
  Automatic source-link fallback would add override/deduplication rules while
  leaving the mixed cases explicit. Do not add that mechanism merely to remove
  role arguments from the simpler commands. Existing fixtures pass unchanged.
- Keep SequenceVM and the compiled command representation. Flattening every
  command into a vector of operations would reintroduce an intermediate
  instruction list and add allocation to simple commands. Most commands need
  only one body; remove forwarding helpers without adding that representation.
- Keep source-driver initialization rules explicit. Stream entry now has a
  dedicated hook because initialization must precede an interleaved stream's
  first delay, including SonyPS2 section changes. Per-command hooks still perform
  real driver work in other formats; do not turn every such hook into startup
  code merely to remove boolean guards.
- Keep explicit draft types and scanner finalization validation. A generic
  draft framework would complicate four small author-facing types. The first
  finalization pass checks all required programs/payloads before consuming any
  draft, so folding it into the materialization loop would weaken recovery.
- Keep immutable chunk storage and source ownership. These preserve snapshot
  lifetimes and stable references; replacing them with copied flat vectors is
  not a sound line-count reduction.
- Keep export selection policies distinct where behavior differs. Ordinary
  performance lookup uses exact source identity; variant materialization also
  supports address fallback. A shared search must preserve those policies.
- Keep export-specific lowering in the export layer. The existing neutral
  performance and synth data are the extension point for future targets;
  adding speculative Furnace interfaces would add concepts without serving a
  current conversion.
- Keep the tempo map's value-returning `points()` contract unless its ownership
  API is deliberately redesigned. Existing callers retain the result of a
  temporary tempo map; replacing that result with a borrowed span would dangle.

- Keep version opcode tables as direct byte-layout descriptions where the
  versions differ substantially. Factoring short repeated tails into extra
  dispatch rules would trade visible data for more decoding logic.

## Cleanup2: direct ownership and fewer bookkeeping layers

Starting revision: `cb9e0b766` on `core-rewrite`. Work continues on
`core-rewrite-cleanup2`. The first batch removes 167 production lines from
`src/value`; test changes add six lines overall. SequenceVM, compiled command
bodies, typed format playback, immutable snapshots, and the neutral performance
and synth models remain the architecture's boundaries.

- Scanner drafts now point directly to stable pending values of their own kind.
  The generic draft slot, repeated variant/index lookup, duplicate handle ID,
  and unrelated recipe/preparer fields no longer sit between format code and
  its draft. Validation still checks all required payloads before consuming
  any draft. Existing growth coverage now exercises all four kinds and links.
- Collection membership is queried directly from explicit choices and resolved
  inputs. Snapshot association queries no longer allocate member vectors.
  Source removal retains its set-lookup traversal rather than searching every
  removed asset against every member. Session operations call their registry
  and snapshot cache directly; private one-line forwarding methods are gone.
- VM automation identities come from their append positions. A separate counter
  and emitter constructor argument were redundant because automation history
  remains intact throughout playback, including section changes. Trimming still
  happens only after retained driver bindings finish their work.
- Stream executors live directly in a reserved vector. Their channel buffers
  retain the stable addresses used by driver bindings. The private loop-detector
  wrapper is gone: command arrival and jump policy use the executor's visited
  state map directly. Exact state, hinted loops ignoring repeat state, declared
  loops, and finite branches keep their existing distinct rules.
- MIDI rendering reads resolved instruments directly rather than projecting a
  second selection structure. Simulated pitch layers share one advancing clock
  per flush instead of rediscovering the next tick for every sample. Selection
  warnings consume their pending flags rather than retaining another reported
  flag; warning wording, attribution, and admission behavior are unchanged.
- Sample tuning is the explicit `pitchCents` scalar rather than a one-field
  `Tuning` wrapper. Synth range inclusion lives with the existing shared range
  bookkeeping. The unused SNES directory-entry index is gone. SPC and RSN
  extraction use the existing signature and byte readers instead of local copies.

Validation of this combined batch:

- Warning-free macOS Release build, including Qt and the shell; all 44 CTest
  suites pass, including every format suite, Qt model tests, and the parity
  harness self-test. A final incremental rebuild includes the settled edits.
- A temporary renderer comparison covers 10,368 generated cases: three pitch
  layers, staggered starts, all six waveforms, delays, phase resets, reversal,
  stepped depth, zero/positive rate and depth, source/instrument sensitivity,
  forced bank selection, and both modulation policies. MIDI bytes and diagnostic
  counts match the original implementation in every case.
- A temporary SPC extraction comparison covers 6,000 generated inputs, including
  truncated snapshots and malformed extended title chunks. Extracted bytes,
  source metadata, titles, and declined inputs match the starting implementation.
- Before/after shell comparisons attempt 126 local game inputs under two
  conversion settings: defaults, and simulated modulation with used-only banks,
  pitch-bend transitions, and RPN tuning. They discover 856 sequences across
  NinSnes, SculptSoftSnes, PrismSnes, KonamiSnes, MoriSnes, PandoraBoxSnes,
  CapcomSnes, SonyPS1, and NDS. All 3,920 produced artifacts match byte for byte:
  1,708 MIDI, 1,106 SF2, and 1,106 DLS. Source/asset/collection inventories,
  diagnostics, and command exit statuses also match, including existing failures.
  Only 54 inputs produce artifacts; unsupported inputs and partial exports are
  not counted as successful conversions. These are value-before/value-after
  comparisons, not a new claim of universal legacy parity.

A global annotation-ID index remains worthwhile: replacing it with per-chunk
searches would reduce lines but weaken large-session scaling. Collection binding
and completed preparation also retain distinct responsibilities: removing their
ownership would introduce partial-state and lifetime rules. Cursor emission
helpers have actual format callers and save typed callback/capture boilerplate;
removing them wholesale would make format code longer and less intuitive.
Further passes will probe stable draft storage, invocation overloads, export
filenames, and the remaining source/execution/preparation responsibilities.

## Cleanup2: optional source values and local export work

The second batch is committed through `9ae12e699`. It removes another 47
production lines from `src/value`, bringing the branch to 214 net lines removed
at this revision. The additional 64 test lines cover an existing scanner failure,
filename handling, and a subtle pitch representation distinction.

- Pitch transitions store optional samples directly. Missing samples mean a
  linear glide; a supplied list means a sampled source curve. The empty supplied
  list retains its previous interpolation fallback, sparse MIDI endpoints, and
  native-portamento warning. Three curve wrapper types and variant access rules
  disappear without changing the format-facing pitch-slide binding API.
- Native YM2151 settings use `optional<Ym2151Voice>` and the explicit `ym2151`
  field. A one-choice variant and its visitors did not represent a real choice.
  The retained hardware settings remain independent of export targets. Another
  native chip is the point at which a sum type would become useful.
- Pending scan values live directly in a deque. Appending preserves references
  held by draft handles and synth entries without one allocation per draft.
  This has a capacity tradeoff: on the current libc++ implementation, the
  720-byte variant uses 16-element deque blocks, reserving 11,520 bytes for the
  first published draft. This is transient scan capacity, not durable asset
  storage. The user-declared default destructor intentionally suppresses moves:
  builder callbacks and domain builders refer to this builder's members.
- A failed scanner produces an ordinary error diagnostic in its empty scan
  result. Workers no longer retain exception pointers for serial rethrow.
  Standard exception text, including empty text, retains its exact attribution
  and registry order. Non-standard exceptions now report a failure and allow
  later scanners to finish instead of escaping admission.
- Target-neutral preparation contexts now live beside asset recipes in
  `model/AssetPreparation.h`. Scan drafts no longer include the export layer.
  Member-method invocation overloads use default empty argument tuples while
  preserving required-operand compile-time rejection. Handler-only overloads
  remain because generic callables cannot use the introspected argument path.
- Stitched parts retain only prepared ownership and their start tick. MIDI is
  rendered and scaled locally during composition, then its tracks move into the
  result. Bank mappings come from the authoritative resolved performance.
  Unique-part preparation collects synth banks, shared pools, and modulation
  maxima once; repeated playlist entries reuse those prepared values.
- MIDI artifact titles use the same filename sanitization as SF2 and DLS.
  The earlier real-file comparison exposed existing export failures for titles
  containing slashes. The regression fixture checks all three filenames and
  unchanged MIDI bytes for punctuation in a collection title.

Validation:

- Warning-free full macOS Release build (352 steps); all 44 CTest suites pass.
  A final incremental build reports no work remaining and all 44 pass again.
  Intermediate tests from overlapping header edits produced mixed-layout
  binaries; those are excluded from validation. Further batches wait for edits
  to settle before building.
- The expanded VM comparison covers 31,104 programs, including linear,
  nonempty sampled, and explicitly empty sampled pitch curves. Both original
  and current implementations run clean under AddressSanitizer, UBSan, and
  float-cast-overflow checks; their 60,675,546-byte traces match exactly.
- A stitched-export comparison covers 1,920 successful cases: mixed source
  clocks and MIDI divisions, repeated collections, sparse banks, shared external
  pools, both bank styles, modulation policies, used-only selection, and envelope
  variants. MIDI/SF2 bytes, part IDs/start ticks/maps, and ordered diagnostics
  match. A separate timeline-overflow case preserves failure diagnostics and
  an empty returned part list.

Keep gate ownership, prepared-bank lifetimes, and source physical pitch timing
explicit. Collapsing those distinctions would replace visible state with more
policy branches. The next pass removes unused lookup APIs and repeated
collection assembly, and tests whether private source-inspection construction
can rely on the existing admission boundary.

## Cleanup2: one collection value and direct source identities

The third batch removes another 122 production lines from `src/value`, bringing
this branch to 336 net production lines removed. It
removes four lookup APIs, the intermediate collection description, the private
inspection factory, and the SNES directory wrapper. Format playback still uses
SequenceVM and retains its original source attribution.

- Resolution and publication use `Collection` directly. The session assigns
  or preserves its identity and sets its origin; callers no longer assemble
  the same selection, issues, and inputs into a second model. Existing lifecycle
  coverage now checks that incoming IDs/origins cannot override publication,
  discovered IDs survive reconciliation, and user collections stay independent.
- Private source-inspection construction relies on scanner admission for byte
  bounds, source identity, and owned annotations. Its constructor expands the
  inspection range while collecting roots. The public session boundary still
  requires a live source-backed asset. Retained inspections keep shared bytes
  and their immutable annotation graph after later session changes.
- Scalar automation targets are mandatory values: every producer supplies a
  target and every consumer requires it. Tick callbacks drop an unused erased
  source-command argument while the emitter and VM keep exact command context.
- Removed unused performance query helpers and source-program lookup methods.
  The latter served fixtures and the parity diagnostic helper; their consumers
  now inspect the ordinary command vectors. Provenance assertions still compare
  complete source identities, including moved events and shared streams. Parity
  diagnostics preserve bounds checks for missing or unowned source commands.
- SNES sample references use a direct 256-entry table for byte-sized source
  keys. The private entry vector and linear search disappear. This changes
  temporary storage from a 24-byte vector plus 12 bytes per entry to a fixed
  2,048 bytes; it removes heap allocation and becomes smaller at dense catalogs.
  Alias detection still compares start address, loop flag, and loop address.
  Adding a duplicate key still fails before assigning an alias reference.
- Directory callers use the existing entry-reading function directly. MIDI
  time-signature denominators use integer bit width instead of logarithms;
  all 256 possible denominator values retain identical results.

Validation:

- Full macOS Release build, including Qt, shell, and parity harness; all 44
  CTest suites pass. The build caught two missed lookup callers in a format
  fixture and parity diagnostics; both were migrated before the successful
  settled build. No warnings remain in the repaired build.
- The sanitizer VM matrix still matches all 31,104 programs. Observable tick
  hooks, event/source-span ordering, scalar endpoints, and all pitch-curve forms
  produce identical 82,265,613-byte traces; ASan, UBSan, and float-cast-overflow
  checks are clean for both implementations.
- Independently compiled SNES implementations match across 1,658 catalog cases
  and 9,216 directory reads. Their 46,387,767-byte catalog traces include every
  reference query, sample, annotation field/link/owner/parent, and diagnostic.
  Cases cover all 256 keys, reversed/rotated/shuffled input, sparse selections,
  unused labels, loop aliases, duplicate keys, and truncated source readers.

The next pass probes initialization hooks and private inspection traversal,
while keeping actual per-command behavior and delayed initialization explicit.

## Cleanup2: initialize once and express ordinary work directly

The fourth batch removes another 31 production lines from `src/value`, bringing
the branch to 367 net production lines removed. The added 50 test lines cover
independent extractor/member failures that previously escaped the scan boundary.

- SuzukiPS1, WolfTeamSnes, CompileSnes, and TriAcePS1 initialize through
  `beginSection(first)` instead of checking a private flag before every command.
  Their initialization flags disappear. These formats have no initial command
  delay or section playlist; initialization still occurs lazily when each
  stream first runs, with the same command context and event ordering. Keep
  KonamiPS1 and TamsoftPS1's delayed initialization and actual per-command hooks.
- Linear LFO fades construct their reusable plan directly, removing an unused
  custom-step configuration method. Motion completion has one clearing path;
  equal signed-zero values retain the original current value's sign.
- Typed dependency candidates project private data from the ordinary candidate
  path. Manual order and container eligibility have one implementation.
  Private inspection roots, ancestor walks, and sorting use the admitted graph
  directly; public missing-ID queries retain their existing behavior.
- Non-standard extractor and queued archive-member path exceptions produce
  source-attributed errors and let later independent work continue, matching
  existing standard-exception isolation. Direct file-loading callbacks retain
  their existing propagation contract. The new fixture verifies exact failure
  order, messages, severity, source ranges, and a later member's successful scan.
- SF2 and DLS call their existing scalar/string operations directly instead of
  three private forwarding helpers. SF2 envelope length derives from its actual
  generator array. Target-specific envelope, tuning, and loop rules remain explicit.
- Pulse synthesis uses indexed harmonic loops. Two mutable counters and their
  callbacks disappear; coefficient and PCM arithmetic retain their original order.

Validation:

- Warning-free macOS Release build (173 steps), including shell, Qt, and parity
  harness; all 44 CTest suites pass.
- Four-format startup captures match exactly: 30 VM renders, 90 MIDI outputs
  across all three pitch-transition policies, and complete event-header and
  source-span traces (33,423 bytes).
- Motion/LFO probes match 100,080 cases and 1,993,920 state observations,
  including signed zero, infinities, and NaNs. Their 55,803,565-byte traces are
  identical; both versions run clean under ASan, UBSan, and float-cast-overflow.
- All existing synth-export fixtures pass in independently linked old/new
  writer captures. Nineteen successful SF2/DLS artifacts and ordered diagnostics
  match in a 14,307,486-byte trace; expected overflow failures also pass.
- Pulse synthesis preserves exact PCM and metadata across 7,068 generated PSG
  cases: harmonic-count boundaries, zero/default sample rates, duty masking,
  short/full loops, and channel defaults. The 1,711,044-byte outputs match, and
  ASan, UBSan, and float-cast-overflow checks are clean.
- The real-file corpus was replayed against committed revision `691f68f04`
  during this batch: all 252 input/policy comparisons preserve every existing
  artifact (1,708 MIDI, 1,106 SF2, 1,106 DLS). The only differences recover the
  two MIDI files for the slash-containing title `Autumn Sky / Womanheart`,
  changing its two command statuses from failure to success. No existing file
  disappears or changes bytes, and no unrelated inventory/diagnostic changes occur.

The next pass removes the standalone registry layer owned only by Session and
the unused catalog projection now superseded by dependency candidates. Keep
the VM's separate delay/source-position, section-entry, and musical-state duties:
collapsing them would obscure real scheduling and lifetime rules.

## Cleanup2: registration belongs to the session

The fifth batch removes another 91 production lines from `src/value`, bringing
the branch to 458 net lines removed. Test changes add no lines overall.

- Removed `FormatRegistry` and its two files. Session is its sole production
  owner and already controls the registration/sealing lifecycle. It now owns
  the two ordered vectors and sealing flag directly; readers use `formats()`
  and `extractors()`. Registration retains the exact incomplete, duplicate,
  accepted-source-format, and late-mutation checks and messages. All six sealing
  sites remain. The module/extractor values themselves remain copyable.
- Removed the unused `AssetCatalog::assetsWithData` projection. The discovery
  fixture now exercises typed dependency candidates through the actual format
  selection path, retaining missing-source IDs, private-data filtering, borrowed
  pointer identity, temporary-list safety, and tie-order assertions.
- A synchronized loop checkpoint's start tick is an ordinary value. Its existing
  nonempty snapshot vector already establishes checkpoint presence; the second
  optional presence state and its redundant condition disappear.
- Invalid VM runtimes return at the entry boundary. Normal playback, prepass,
  and finalization are no longer indented beneath the successful-runtime branch.
  Warning values are constructed directly at their two actual emission sites.
- Removed five stale includes from export assembly/scaling. Keep `<variant>`
  where `std::get_if` is still used; dependency cleanup follows actual use.

Validation:

- Warning-free full macOS Release build (147 steps), including Qt, shell, and
  parity harness; all 44 CTest suites pass. Existing registration mutation,
  source extraction, format inventory, and parallel-scanner fixtures pass after
  moving ownership. A repository-wide search finds no remaining removed API
  references outside historical audit records and the old design visualization.
- A temporary synchronized-loop comparison covers 64,802 programs: absent and
  replacement checkpoints, zero-tick starts, all policies/repeat counts,
  coordinator position among one to four streams, interleaved channels,
  command delays/waits, active-note closure, playlists, silent prepass, and
  empty/missing runtimes. Old/new traces match exactly (93,001,679 bytes).
  Both VM and supporting units run clean under ASan, UBSan, and float-cast-overflow.

Further work now probes the transient relationship value representation and
linearizes instrument-variant preparation without changing attack/gate policy.

## Cleanup2: store relationship values and prepare variants directly

The sixth batch removes another 16 production lines, bringing the branch to
474 net lines removed from `src/value`. The operand change adds three lines to
preserve tag ordering explicitly; the variant pass removes three lines while
removing considerably more nesting. No permanent test infrastructure is added.

- `SemanticOperand` holds an optional unsigned value rather than the complete
  display-value variant. Only unsigned relationships are consumed after decode;
  complete display values remain in `SourceField`. Nonnumeric tags still occupy
  their original positions, so an invalid first channel and repeated invalid
  bank/program tags retain their previous behavior. Signed values, booleans,
  and floating-point values are never coerced into relationship addresses.
  The x86-64 operand representation shrinks from 40 to 24 bytes.
- Instrument variant preparation uses early exits for unchanged, unresolved,
  regionless, and reused selections. The optional pending variant handle and
  three nesting levels disappear. Native responses still sample the entire bank
  once before regionless checks, warning order stays exact, and attacks and
  continuations retain distinct instrument/gate behavior.
- Four synth draft accessors return their owned builder members inline beside
  `id()`. The format-facing signatures are unchanged; a reader no longer needs
  to visit the implementation file for these ordinary member accesses.

Validation:

- Warning-free full macOS Release build (279 steps), including shell, Qt, and
  parity harness; all 44 CTest suites pass.
- An independent operand probe matches 29,044 cases and 5,148,914 trace bytes.
  It covers all six display-value alternatives, unsigned bounds, nonfinite
  values, repeated tags, all pointer/control-flow roles, complete annotation
  fields and links, Akao's actual reference collector, Nin prefix ordering,
  and truncated reads. ASan, UBSan, and float-cast-overflow checks are clean.
- Independent variant preparation captures match all existing fixtures and
  2,056 generated cases: 2,073 preparations and 42,141,206 serialized bytes.
  Cases exercise dynamic/stereo policy, native response budgets, selection
  fallbacks, envelope clearing/restoration, ties and lane order, used presets,
  address exhaustion, voice deadlines, ordered diagnostics, and MIDI policies.
  ASan and UBSan report no findings. Libc++ container annotations were disabled
  consistently in this temporary probe when linking its unchanged unsanitized
  baseline archive; allocation/access and undefined-behavior checks remain active.
- The frozen sixth-batch executable matches committed `691f68f04` across all
  252 real-file input/policy comparisons: all 3,922 artifacts, inventories,
  diagnostics, and exit statuses are identical. This includes the two MIDI
  files recovered by title sanitization; all 3,920 original baseline artifacts
  remain byte-identical.

Further boundary review keeps typed playback, stream-state contexts, pure
motion, and bound performance values. Playback serves many driver families;
three require shared-stream specialization. Ten families use bound values to
interrupt emitted automation independently of their source arithmetic. Folding
these responsibilities together would either couple pure motion to performance
containers or repeat interruption rules in the formats. Required draft validation
also remains a separate pass before consuming any payload. Moving publication
into every materialization branch saves little and repeats the publication rule.

## Cleanup2: return ordinary values and remove access adapters

The seventh batch removes another 105 production lines from `src/value`,
bringing the branch to 579 net lines removed. CMake drops one header entry;
existing tests gain 18 lines overall for builder ownership/range contracts.

- `SamplePoolBuilder::finish()` returns a `SamplePool`, and
  `InstrumentSetBuilder::finish()` returns an instrument vector. The two
  result-only wrapper types disappear. Scanner materialization reads the
  builders' existing ranges after finalization, retaining explicit range
  precedence, inferred bounds, and bank-to-local-sample fallback. Entries and
  sparse lookups still invalidate at finish; incomplete required scanner
  payloads remain repairable before any materialization begins.
- Removed `SessionSnapshotAccess` and its header. The immutable snapshot takes
  its owning values directly, and Session emplaces it without an intermediate
  factory result. The former factory was already publicly callable and did no
  validation. Immutable storage, indexing, and revision caching are unchanged.
- Removed `SharedSequenceAccess`. Its two owners construct shared sequences
  directly through friendship; chunk construction remains private, and the
  same storage constructor still normalizes chunks and calculates offsets.
- Source level counts are ordinary `u32 sourceLevels` fields and arguments.
  The single-field `ValueQuantization` type disappears from core and 25 format
  implementations. Counts still describe distinct native source values:
  128 means codes 0–127; zero means continuous/unknown. MIDI preserves the
  exact greater-than-128 resolution threshold and zero-count tremolo reset.
- Tempo-relative modulation filtering and derived event headers sit at their
  actual use sites, removing two single-use private adapters. LFO interval
  timing drops an impossible zero-after-increment branch: the strict loop
  upper bound already prevents overflow.
- PSF executable payload/address offsets and output source format share one
  version-specific layout value. Separate support/format/offset branches and
  repeated base-path construction disappear. PSF2's filesystem path remains
  explicit; mixed library overlays and GSF declared zero tails are unchanged.

Validation:

- Full macOS Release build, Qt/shell/parity included, and all 44 CTest suites
  pass. The initial build caught three modulation helpers still consumed by
  legacy `src/main`; their complete original public API was restored before
  the successful warning-free 157-step repair build. Future API-removal searches
  include all of `src` and `tests`, rather than only the value implementation.
- Existing MIDI fixtures pass in independently captured before/after runners;
  all 49 MIDI outputs and ordered diagnostics match exactly (15,736 bytes).
  Another 19,584-case renderer comparison matches 3,402,640 bytes, covering
  level counts from zero through `u32` maximum, all nine resolution pairs,
  both modulation policies, layered LFOs, tremolo resets, controller repetition,
  and pan/headroom changes.
- Tempo-relative fixtures pass in independent sanitizer runners. Four captured
  inputs, repeated recomputation, complete context/provenance/event ordering,
  and eight MIDI traces match exactly (6,587 bytes); sanitizers report no findings.
- Independently compiled PSF extractors match 9,984 cases and 456,931 bytes of
  extracted source metadata, payloads, diagnostics, and exact exceptions. Cases
  include all 256 version bytes and truncated headers, every supported layout,
  CRC/header/decompression failures, load-address boundaries, GSF declared sizes,
  titles, missing/no-path libraries, numbered libraries, and mixed-version
  overlays. ASan, UBSan, and float-cast-overflow checks are clean.
- The frozen seventh-batch executable matches every sixth-batch corpus result:
  all 252 comparisons and 3,922 artifact hashes, with identical inventories,
  diagnostics, and exit statuses.

The next pass flattens dependency request wrappers and investigates using the
ordinary diagnostic value for collection problems. Preserve missing owner
references in the collection while exporting only live owner attribution.

## Cleanup2 eighth batch: direct requests and ordinary collection diagnostics

This batch removes another 36 net production C++ lines, bringing cleanup2 to
615 removed. Focused attribution coverage adds 52 net test lines; dependency
documentation grows by four lines. The format-facing scanner DSL is unchanged.

- Sequence assets and drafts retain `bankRequests` directly; sound banks and
  drafts retain `sampleRequests`. The single-vector `SequenceRecipe` and
  `BankRecipe` wrappers disappear. Native format recipe types remain independent.
- Collection and dependency messages use the ordinary `Diagnostic` value.
  `CollectionIssue` and its field-by-field export projection disappear. Missing
  and engaged-invalid asset references remain stored on immutable collections;
  binding copies the messages and clears only attribution to absent assets.
  Severity still controls presentation, while resolution status controls
  whether preparation proceeds. Message fields and ordering remain unchanged.
- `ObjectRefs` constructors live inline beside `ObjectRef` in `CoreTypes.h`.
  Their exact aggregate construction and signatures are unchanged. Collection
  diagnostics no longer need a source-annotation header for ordinary object IDs.
- Collection binding uses its required reference directly, removing the
  redundant pointer alias and repeated dereferences.

Tradeoff: on the tested x86_64h/libc++ build, `CollectionIssue` occupied 88 bytes
and `Diagnostic` occupies 112 bytes. The additional 24 bytes per uncommon
collection message buy removal of a separate model and conversion path, and
use the established source/object attribution vocabulary. No message strings
or reference semantics are dropped to reduce this size.

Validation:

- The complete 377-step macOS Release build, including Qt, shell, and parity,
  is warning-free. All 44 CTest suites pass.
- The attribution fixture checks automatic and manual collections, distinct
  ordered messages, live/missing/engaged-invalid/absent owners, exact diagnostic
  fields, immutable original references, and preparation despite error severity
  when dependencies are resolved.
- Frozen before/after runners match all 252 trace lines across 14 dependency
  scenarios: automatic/manual direct, all, ambiguous, missing, wrong-type,
  exception, and empty requests. They compare statuses, membership, target
  placements, alternatives, stored references, complete ordered binding
  diagnostics, and prepared bank order.
- The frozen eighth-batch executable matches all seventh-batch corpus results:
  252 comparisons and 3,922 artifact hashes, with identical inventories,
  diagnostics, and exit statuses.

Further review found a reconstructible fixed linear LFO fade plan, redundant
initial-tempo storage, and an idempotent repeat-counter guard. These are queued
for the next batch with before/after timing and state comparisons. The MIDI
planner's chronological output remains a tested contract; removing its sort
would shift that responsibility onto consumers, so the proposed change was
rejected. Adding merge machinery to its renderer would increase conceptual
overhead and was also rejected.

## Cleanup2 ninth batch: canonical timing state and direct private operations

This batch removes another 41 net production C++ lines, bringing cleanup2 to
656 removed. Snapshot boundary/lifetime coverage adds 51 test lines. The
public fade and rendering behavior remain unchanged.

- LFO depth fades retain their configured duration rather than an optional
  motion plan containing duplicate target, step, delay, and mode. Restart
  constructs the same fixed linear plan from the duration and target. Zero
  still disables future restarts without interrupting an active fade; depth
  reset cancels both. The probe's state layout shrinks from 80 to 56 bytes.
- Tempo maps retain their canonical points and normalized pulse count.
  Nondefault initial tempo already resides in a tick-zero point, so the
  additional initial-tempo member disappears. Explicit tick-zero events still
  win, and queries before any point use the default 500,000 microseconds.
  The probe's object layout shrinks from 40 to 32 bytes.
- `RepeatCounter::firstVisit()` queries its counter directly. The sole separate
  `active()` guard disappears; AkaoSnes finishes a counter idempotently. Its
  independent native loop-break arithmetic remains explicit.
- MIDI note boundaries state the simple tremolo/pan restart conditions directly.
  Two forwarding predicates, two unreachable restart guards, and the held-output
  forwarding helper disappear. Held flags use `std::exchange`. The zero-value
  pitch guard remains to preserve negative-zero state, and vibrato retains its
  real per-layer restart eligibility scan.
- All immutable snapshot indexes use typed ID keys and direct pointers to their
  owned values. Source and collection queries now follow the existing asset
  pattern without vector-position bookkeeping. Const shared storage keeps these
  pointers stable across snapshot copies/moves; first duplicates still win and
  invalid IDs remain excluded. Copyable `AssetCatalog` retains its source indices
  because its source vector has a different lifetime/copy contract.

Validation:

- The complete 338-step macOS Release build, including Qt, shell, and parity,
  is warning-free. All 44 CTest suites pass.
- ASan/UBSan/float-cast-overflow comparisons match 4,032 tempo cases and
  7,138,566 trace bytes, including zero/nondefault/maximum initial tempo,
  coincident writes, large divisions, saturated duration conversions, and
  nonfinite duration queries.
- The extended motion/LFO comparison matches 102,128 cases and 56,315,712 bytes
  under the same sanitizers. It covers reusable fade cancellation/restart,
  reconfiguration, delays, target/duration extremes, and signed-zero/Inf motion
  observations. The VM matrix matches all 31,104 cases and 82,265,613 bytes.
- Independently compiled renderer runners match 75,043 renders and
  144,890,623 serialized bytes of complete output events/order, encoded MIDI,
  and ordered diagnostics. Coverage includes the prior 19,584 LFO/level cases,
  55,296 restart/held/delay/clock/note-boundary/coincident-control/global-reverb/
  external-warning cases, 32 signed-zero cases, and 131 captures from both
  unchanged MIDI fixture suites.
- The focused snapshot fixture checks ID zero, the highest valid ID, duplicate
  first-value identity, invalid/missing lookup, preserved vector order, and
  copied/moved snapshot pointers in all three domains.
- The frozen ninth-batch executable matches all eighth-batch corpus results:
  252 comparisons and 3,922 artifact hashes, with identical inventories,
  diagnostics, and exit statuses.

Next: consolidate source-field capture/conversion, centralize C-string text
conversion (which currently becomes boolean in ordinary annotations), and move
inspection subset preparation into the inspection owner. Keep owned tempo-point
query results: a reference getter would dangle in an ordinary range-for over a
temporary tempo map under the project's C++20 lifetime rules.

## Cleanup2 tenth batch: one field-capture path and inspection-owned selection

This batch removes another five net production C++ lines, bringing cleanup2 to
661 removed. Focused source-value and inspection-isolation coverage adds 66
test lines. The improvement is removal of duplicate responsibilities and
unnecessary text copies, rather than a substantial physical line reduction.

- RecordReader uses one field-capture path for decoded and derived values.
  Capture checks run before display-value construction; CompilerCursor's
  disabled record capture no longer constructs text values to discard them.
  Returned raw text moves from its existing local string. A 65,536-byte raw
  read uses one allocation instead of three without capture, and three instead
  of four with capture, in the independent allocation probe.
- Common source-value conversion owns mutable and const C-string pointers as
  text. Previously, ordinary annotations converted pointer variables to bool,
  while literals became text. Null text pointers represent an absent value;
  empty strings remain strings. Arrays retain their explicit length, including
  embedded zeros. CompilerCursor delegates its text-pointer conversion here,
  removing the duplicate path and its undefined null-pointer string view.
  Cursor-specific Address/enum normalization remains pending further review.
- SourceInspection selects and owns its asset's annotation graph directly.
  SessionState's single-use subset-construction API disappears. Annotation
  ordering, inherited ownership, expanded byte range, roots, and shared source
  lifetime are unchanged. The existing fixture now scans another asset and
  verifies its annotations stay outside the retained inspection.

Validation:

- The complete 354-step macOS Release build, including Qt, shell, and parity,
  is warning-free. The initial core test caught an indexing error in the new
  fixture: the raw decoded command has two derived fields, while opcode is
  added later during annotation projection. Correcting that assertion required
  only a two-step test rebuild. All 44 CTest suites then pass.
- Independent before/after reader/cursor runners match 15,372 cases and
  19,784,777 serialized bytes, comparing returned values, ranges, captured
  fields, diagnostics, and truncation. Both use the settled conversion headers;
  ASan, UBSan, and float-cast-overflow checks are clean.
- The source-value comparison identifies exactly the nine intended pointer
  corrections among its 26 original cases; all other values retain their
  types, ownership, and explicit lengths. A new safe cursor-null case replaces
  a formerly undefined operation. Independent normal and sanitizer runs agree.
- The frozen tenth-batch executable matches every ninth-batch corpus result:
  252 input/policy comparisons and 3,922 artifact hashes, with identical
  inventories, diagnostics, and exit statuses.

### Pre-existing format coverage limitation

An independent inventory found a value recognition/parsing gap for Org. Legacy
registers OrgScanner and recognizes the Org-02 signature; the initial
`core-rewrite` value registration and all value sources contain no corresponding
module or signature. A temporary valid 122-byte, one-note Org fixture parses
through legacy OrgSeq's header reader (one track/note, PPQN 4, wait time 100),
while the frozen initial value shell returns no sources, assets, or collections
and reports no supported music data. This predates every cleanup2 commit.

The legacy track loader accesses its MIDI object during ordinary loading, so
the probe establishes legacy recognition/header parsing, not functioning
end-to-end legacy conversion. Directory renames such as FFT to SuzukiPS1 and
PS1 to SonyPS1 retain their signatures; value additionally has SculptSoftSnes
and WolfTeamSnes. Directory counts alone cannot establish equivalent support.
The corpus is behavior-preservation coverage for sampled value inputs, not
proof that every legacy feature is implemented or audibly equivalent.

Next: use existing typed IDs directly throughout session/catalog/source-map
indexes and validation, retaining each owner's existing pointer/index lifetime
contract. Investigate ordinary optional modulation ranges instead of infinity
sentinels, and remove private renderer forwarding/state parameters whose
callers always supply the renderer's own state.

## Cleanup2 eleventh batch: typed indexes and simpler private configuration

This batch removes another 31 net production C++ lines, bringing cleanup2 to
692 removed. Two tests migrate to the existing asset lookup; no test behavior
or public export policy changes.

- Session, catalog, source-map, removal, and validation indexes use SourceId,
  AssetId, and SourceAnnotationId directly. This removes 57 key unwrappings and
  two rewrappings without changing validity filters, first-duplicate behavior,
  ordering, or ownership. Copyable catalog source indexes remain vector indexes;
  pointer indexes retain their existing stable owners. SessionState uses its
  canonical asset lookup and existing ordered diagnostic append operation.
- Playback construction contexts name their immutable decoded graph
  `sourceTrack`, distinguishing it from mutable playback track state. The unused
  VmApi::sequence() accessor disappears; it exposed an unfinished performance
  whose tracks had not yet been assembled.
- LevelScale's pure forwarding alias disappears at eight calls in seven formats.
  The direct clamp names the actual operation. SNES loop validation reuses its
  existing alignment predicate instead of repeating it.
- The MIDI track renderer owns its fixed modulation conversion policy and pan
  cache. Private methods stop forwarding these same values through every call.
  Two single-use forwarding helpers become direct expressions. External
  instrument reservations use the existing InstrumentAddress value directly.
- Modulation analysis stores optional ranges instead of infinity sentinels plus
  a private observation/query protocol. Filtering, signed-zero observations,
  waveform conflicts, and positive-delay requirements remain explicit. The
  temporary observation structure grows from 56 to 72 bytes: 48 additional
  stack bytes across three observations, with no persistent model growth.

Validation:

- The complete 354-step macOS Release build, including Qt, shell, and parity,
  is warning-free. All 44 CTest suites pass.
- Frozen index/removal runners match 337 scenarios and two duplicate-map
  rejections: 12,528 lines and 559,997 bytes. Coverage includes invalid and
  duplicate IDs, high IDs, inherited/explicit/missing/cyclic parents, retained
  invalid annotation entries, catalog copy/move ownership, ordered diagnostics,
  cross-source links, removals, retained views, and repeated session scans.
- Independent renderer runners match 75,043 renders and 144,890,623 bytes of
  complete ordered events, encoded MIDI, and diagnostics. Instrument preparation
  matches 3,729 cases and 47,055,798 bytes, including sparse/external addresses,
  ordering, filtering, stitching, and maximum numeric bounds.
- Modulation profiles and normalization match 43,911 cases and 37,728,599 bytes,
  including negative/subnormal/signed-zero/maximum/nonfinite values, zero-only
  delays, waveform conflicts, event order, and absent physical values. Focused
  changed profile/address units and probes also pass ASan, UBSan, and
  float-cast-overflow checks. Unchanged archive code is not sanitizer-instrumented.
- The frozen eleventh-batch shell matches every tenth-batch corpus result:
  252 input/policy comparisons and 3,922 artifact hashes, with identical
  inventories, diagnostics, and exit statuses.

### Further candidates evaluated and retained boundaries

Moving enum/Address conversion into generic SourceValue conversion removes only
one net line across four headers and adds generic dependencies. Forwarding the
existing string branch adds a line. Allocation probes show no savings: the
existing copied string is already moved into conversion. Both temporary drafts
match 15,394 cases and 19,801,663 bytes under focused sanitizers; neither offers
enough clarity improvement to adopt.

Replacing bulk collection membership checks with repeated contains() calls
saves a helper but scales poorly: measured removals of 256 IDs are approximately
6.3–6.8 times slower, and 4,096 IDs approximately 101–127 times slower. Keep the
single membership traversal and typed removal set.

A narrower sounding-voice instrument variant saves 24 bytes per voice, but its
working draft adds five lines, another selection vocabulary, an adapter back to
the event identity, and fixture-specific comparisons. Keep the shared identity
unless profiling establishes that voice memory warrants this complexity.

Owning annotation vectors directly in SourceMap::Part and retaining them via an
aliasing shared pointer saves one allocation and 40 bytes per part, with no line
reduction. An annotation-only retained view would then retain all indexes too:
the 4,096-row probe retains 246,136 additional bytes and 4,107 additional blocks.
Keep the independently retained annotation vector.

### Startup lifecycle edge under investigation

The expanded startup probe found that beginSection executes before beginCommand
checks a zero command budget. A temporary Hudson migration matches 46,080
positive-budget cases and 158,401,515 bytes, including masks, versions, track
orders, empty leading tracks, provenance, DSP, and MIDI; focused sanitizers are
clean. It differs in 10,728 of 15,360 zero-budget cases by emitting initial
reverb despite no executed command. Hudson therefore retains its flag for now.

The same lifecycle distinction applies to the fourth batch's four startup-hook
migrations. Their earlier comparisons omitted zero command budgets. Production
format budgets are positive and the corpus remains exact, but this public VM
edge needs a regression test and a lifecycle correction before those migrations
can be considered behavior-preserving for zero-budget programs. A centralized
entry-budget rule is being evaluated; no claim of completed repair is made here.

Next: resolve this lifecycle edge and evaluate whether SourceMap's private owner
cache can use AssetId's existing invalid sentinel instead of redundant optional
presence, preserving the public optional query and cycle behavior.

## Cleanup2 twelfth batch: budgeted lifecycle and explicit ownership

This batch removes another 13 net production C++ lines, bringing cleanup2 to
705 removed. Regression and ownership fixtures add 99 net test lines. Several
improvements reduce redundant state or allocations without reducing lines.

- Section-entry hooks keep their original placement, stream-before-track order,
  and first-command provenance before a leading delay. Their existing entry
  condition now requires remaining command budget. beginCommand still performs
  counting, loop-state recording, stopping, and warning generation. This repairs
  the zero-budget distinction introduced by the fourth batch's four migrations;
  exhausted later playlist sections also stop emitting denied startup output.
- Hudson initial echo uses its immutable driver's mask in the first-section hook.
  Its duplicate per-track flag and constructor copy disappear. The five native
  decoders involved do not route command bodies to undeclared channels. Generic
  section hooks still initialize declared playback tracks independently of body
  routing when budget remains; equivalence to old per-command initialization is
  not claimed for arbitrarily rerouted synthetic format programs.
- SourceMap's private annotation-owner cache stores AssetId directly. Its invalid
  sentinel already represents absence; optional conversion happens only at the
  public query. Zero and the highest valid ID remain valid owners. The cache
  shrinks from eight to four bytes per annotation, saving exactly 16,384 bytes
  for 4,096 rows, without changing allocation counts or retained-view ownership.
- SegSat program state borrows the configuration already owned by its immutable
  runtime factory. It retains only its actual finalization responsibility. The
  unused SequenceProgram constructor argument and optional deep configuration
  copy disappear. State shrinks from 64 to eight bytes; representative creation
  drops from 15 allocations to zero. The default no-configuration path remains.
- SourceInspection keeps one item hit-test API for annotations and projected
  fields. The obsolete annotation-only projection disappears. Collection::members
  owns its membership projection directly, removing a free-function API and
  forwarding step while preserving chosen order and alternative exclusion.
- Standalone MIDI callers use the existing SequenceRenderOptions directly; the
  identical SequenceExportRequest alias and its duplicate vocabulary disappear.
  Qt, shell, Session, and export call sites use the same settings type. Behavior
  still depends on whether a sequence belongs to a collection, rather than on
  the alias's former standalone-only description.
- Instrument validation uses InstrumentIdentity directly; stitched sample
  deduplication uses AssetId directly. Numeric-only envelope/binary writer headers
  depend on Types instead of the source/diagnostic model. SharedSequence retains
  empty-chunk filtering but removes null handling: its only two friends and its
  public value constructor always supply non-null shared owners.

Validation:

- The initial 368-step macOS Release build found two stale variable references in
  later UI fixture lifetime assertions after the hit-test migration, and a lambda
  parameter newly shadowing Collection::id after moving the membership body.
  Updating those references and naming the parameter asset required a 48-step
  warning-free rebuild. All 44 CTest suites pass, including Qt, shell, and parity.
- The four earlier startup implementations and repaired hooks match 16 frozen
  fixture captures at default/zero/one budgets and empty tracks: 107,038 bytes.
  Hudson matches all 61,440 cases and 198,908,451 bytes across versions, masks,
  track orders, empty-leading tracks, zero/positive budgets, provenance, DSP,
  and MIDI. Focused format/VM/probe sanitizer checks are clean.
- Generic hook/delay/readiness/flow/loop runners match 41,472 positive-budget
  cases and 67,542,785 bytes. Another 10,368 zero-budget cases differ only by
  suppressing denied entry output. A 7,776-case later-section matrix is sanitizer
  clean: 4,950 cases match exactly; 2,826 remove precisely 8,469 denied entry
  markers and corresponding sequence-number gaps, preserving all other event
  values/ticks, source spans, warnings, loop behavior, and boundaries. All 972
  ample-budget cases match byte-for-byte.
- The new VM regression checks entry order/provenance, a three-tick leading
  delay, tick callbacks, budgets zero/one/two/four, denied later sections, and
  empty tracks retaining declared defaults. The complete focused VM fixture
  also passes ASan, UBSan, and float-cast-overflow checks.
- Frozen owner-cache runners preserve the full 12,528-line, 559,997-byte
  index/validation/removal/session trace. The extended branch/cycle fixture
  covers zero/highest-valid owners, invalid explicit owners inheriting from
  parents, and visible invalid annotation rows excluded from lookup/ownership.
  Focused ordinary and ASan/UBSan runs pass.
- SegSat matches 1,168 complete performance/source-span/ordered-diagnostic/MIDI
  captures and 1,917,675 bytes in Release and focused sanitizer runs. Coverage
  includes all four volume models, zero to three banks, copied-runtime retention,
  overrides, prepass, analysis finalization, no-config rendering, and irregular
  track counts. Configuration remains factory-owned through all those lifetimes.
- The startup sanitizer pass exposed two existing Compile fixture borrows from
  temporary performances. A broader search found the same issue in Namco and
  GraphRes. Four sites now retain the performance before borrowing event pointers.
  Independent frozen Namco/GraphRes fixtures reproduce heap-use-after-free before
  the repairs and pass ASan/UBSan/float-cast-overflow afterward. Unchanged archive
  code is not fully instrumented; mixed libc++ container annotations are disabled
  where required, without disabling ordinary heap/UB checks.
- The frozen twelfth-batch shell matches every eleventh-batch corpus result:
  252 input/policy comparisons and 3,922 artifact hashes, with identical
  inventories, diagnostics, and exit statuses.

Retain HeartBeat's existing borrowing wrapper: replacing it with owned Config
would introduce vector copies, and moving the same reference into every track
would add dependency boilerplate. Retain the tested modulation-profile empty()
query; a variable-name-only search missed a chained call, caught before integration.

Next: remove read-only configuration copies in Nin/Quest, Rare/WolfTeam, and
GraphRes/Neverland/SoftCreat without a new adapter. Temporary layout/allocation
measurements show substantive reductions while mutable working tables stay owned.
Evaluate direct private playlist-command references, one bounded SemanticOperand
query, and a decode-session start range instead of an unused borrowed byte reader.


## Cleanup2 thirteenth batch: borrow immutable playback settings

This batch removes three net production lines, bringing cleanup2 to 708 removed.
The principal improvement is clearer ownership: playback state retains mutable
working values and borrows read-only settings already owned by its runtime
factory. Two regression extensions add 13 test lines.

- Nin's baseline envelope map and Intelligent transpose table borrow immutable
  configuration. Working envelopes, voice records, and percussion records remain
  owned and reset independently. ProgramState shrinks from 2,696 to 2,672 bytes;
  IntelligentState shrinks from 176 to 168. A 32-envelope/seven-transpose-entry
  state needs 33 allocations instead of 66: the remaining map is actual working
  state. Quest borrows Config and owns only the three condition bytes that its
  commands modify, alongside its existing working instruments. Its state shrinks
  from 10,600 to 2,872 bytes, and configuration-vector copies disappear.
- GraphRes, Neverland, and SoftCreat track states borrow const Layout from the
  same immutable runtime owner as their existing byte reader. State sizes fall
  from 288 to 240, 416 to 128, and 776 to 272 bytes, respectively. GraphRes and
  Neverland each avoid one layout-vector allocation per channel. GraphRes now
  invokes the existing copied-program/source-release fixture; Neverland and
  SoftCreat already had that lifetime coverage.
- Rare borrows its SRCN table; WolfTeam borrows instrument pitches, its pitch
  table, and per-track stream starts. Mutable playback state remains independent.
  Rare state shrinks from 528 to 520 bytes; WolfTeam program state falls from
  160 to 32 and track state from 136 to 128. Populated state factories each drop
  from two allocations to one.
- SemanticOperand owns its bounded unsigned32 query. Source-channel projection,
  instrument links, and AKAO reference discovery use the same check, preserving
  their distinct rules for absent/overflowing and repeated operands. Address
  links continue to retain full 64-bit values.
- TrackDecodeSession retains its initial SourceRange instead of a byte reader
  that it never reads and a separate offset. It shrinks from 96 to 88 bytes.
  Empty and out-of-bounds tracks preserve their full entry address in both
  executable metadata and the source annotation; a regression assertion covers
  the annotated entry above the 32-bit boundary.

Validation:

- The initial 116-step Release build stopped on an invalid Address equality
  expression in the new fixture. Comparing its value fixes the fixture; the
  following 55-step build is warning-free and all 44 CTest suites pass.
- Nin's full fixtures and generated settings/conditions/envelope probes match
  1,414 complete performance/MIDI captures and 6,745,680 bytes in Release and
  before/after focused ASan, UBSan, and float-cast-overflow runs. Generated
  configs test repeated rendering, independent analysis, copied runtime after
  original input/layout/program release, and immutable-default/working-table
  reset isolation. All three Nin implementation units and the VM are instrumented.
- Rare's 912 generated cases and WolfTeam's 2,040 cases match 6,541,826 bytes,
  including 4,354 VM captures and 13,062 MIDI exports across three pitch policies.
  All existing suites pass in both sanitizer runners. Cases cover every native
  profile/variant, populated/truncated/fallback tables, pitch and stream bounds,
  duplicate streams, prepass/reset, repeated renders, and copied runtimes after
  source/layout/original program release.
- Focused GraphRes/Neverland/SoftCreat format suites pass ASan/UBSan, including
  their copied-program/source-release checks. Probe builds use frozen twelfth-
  batch headers and archives plus local format replacements, so the decode-
  session layout change cannot cause an archive/header mismatch.
- Operand projection/reference discovery matches all 29,044 cases and 5,148,914
  bytes; decode sessions match 4,032 source/hierarchy/start/empty/command cases
  and 467,034 bytes. Focused changed decode code and the complete compiler-cursor
  fixture pass ASan, UBSan, and float-cast-overflow. Unchanged archives are not
  fully instrumented; mixed libc++ container annotations are disabled consistently.
- The frozen thirteenth-batch shell matches the twelfth batch for all 252
  input/policy comparisons, 3,922 artifact hashes, inventories, diagnostics,
  and exit statuses.

Rejected: replacing playlist ordinals with pointers removes only three lines.
Its 46,656-case sanitizer trace is exact (41,141,541 bytes), but repeat-map nodes
increase from 40 to 48 bytes and a depth-12 repeat probe allocates 8,552,936 bytes
instead of 7,406,048, with identical allocation counts. Keep compact ordinals.

Next: remove the unused misc-asset byte payload and private data, including the
ceremonial draft lifecycle that requires formats to fill them. Source ranges and
annotations already provide inspection, and no production consumer reads either
value. Preserve ordered publication and sequence-payload retry safety. Also
investigate moving the VM's existing visit snapshots directly into its visited
map and removing redundant per-format sequence-range inference.


## Cleanup2 fourteenth batch: misc assets are source markers

This batch removes 72 net production lines, bringing cleanup2 to 780 removed.
Fixtures add ten net lines. The main change removes a format-authoring concept
rather than replacing it with another helper.

- MiscAsset is a name, identity, and source range for supplemental inspection.
  Its byte payload and private data have no production readers or semantic
  writers. Original bytes and annotations already belong to SourceStore and
  SourceMap. Removing those values eliminates the entire ScanMiscDraft class,
  required-payload presence/duplicate checks, data setter, and includeMisc(draft)
  projection. Formats call misc(name, range) and receive AssetId directly.
- Ordinary MiscAsset values remain queued with audio drafts, preserving asset
  order and ID allocation. Required sequence programs are still checked in a
  complete first pass before consuming any draft; repair/retry remains safe.
  A dependency-data query on misc now returns the same missing-data exception,
  while actual sequence/bank/sample private-data protocols remain intact.
- CPS, KonamiTMNT2, and Namco stop copying source-table bytes; SonyPS2 drops its
  ceremonial empty-payload calls. Owned annotations and collection membership
  are unchanged. MiscAsset shrinks from 136 to 80 bytes, but Asset remains
  720 bytes: this is not a claim of per-variant storage reduction. The discarded
  raw byte buffers are actual removed allocations.
- Rare and Namco use the builder's existing owned-annotation/command range
  inference instead of repeating it privately. SoftCreat keeps its explicit
  range because the owned Selected Song annotation legitimately lies outside
  its chosen sequence bounds.
- VM visit snapshots are inserted directly into visited_ instead of copied
  from an extra local. An inferred loop copies the canonical stored key into
  handleLoop's owned optional before clearing the map, then moves it back when
  needed. The visit tick and loop behavior remain unchanged. executeNext drops
  an inactive early-return whose wait block and command loop already skip the
  same state before the existing final return.
- Nin's immutable initial percussion array borrows the same Config owner as
  its other defaults; its mutable working array and reset assignment stay owned.
  ProgramState shrinks from 2,672 to 2,632 bytes and IntelligentState from 168
  to 128. Prism/Neverland/Itikiti constructors request only their actual Config,
  dropping an unused SequenceProgram argument through the existing factory path.
- SoundFont writes six individual bytes with vector::push_back. The writeU8
  pure-forwarding API disappears; signed/endian encoding helpers remain.

Validation:

- The full 321-step macOS Release build is warning-free; all 44 CTest suites pass.
- Misc before/after observable traces match across eight mixed/session scenarios:
  1,229 lines and 80,994 bytes, with ASan/UBSan clean on both sides. Coverage
  includes ordered publication and sequence retry, collection deduplication,
  owner/field/annotation graphs, diagnostics, hit tests, original inspection
  bytes, asset-only/source-family removal, retained views and final byte release.
  Before/after runners use fully consistent thirteenth/fourteenth headers and
  archives, without mixing the changed public MiscAsset layout.
- Focused registry/synth-builder/session/source-map fixtures pass ASan/UBSan with
  changed ScanResultBuilder.cpp and test units instrumented. The initial combined
  run encountered libc++ container-overflow across an instrumented std::function
  clone and Release SessionState vector copy. Frozen13 reproduces the same write
  and stack. Both revisions pass with container annotations disabled; 14 also
  passes with _LIBCPP_HAS_NO_ASAN compiled consistently. Ordinary heap/lifetime
  and UB checks remain active. Remaining archives are Release, so this is not
  whole-core sanitizer coverage.
- Seventy-seven frozen sanitizer range cases preserve Rare/Namco explicit versus
  inferred bounds: every Rare profile and Namco version, header-only/zero-track,
  pointer/truncation, backward/forward/unreachable commands, multi-track, and
  zero-size anchors. SoftCreat's distinct bounds remain explicit.
- The exact final VM matches 171,938 cases and 206,284,725 trace bytes under
  ASan, UBSan, and float-cast-overflow: budgets, calls/repeat maps, jump kinds,
  leading delays/readiness, hooks, playlists, synchronized checkpoints/prepass,
  and nested large state. A 48-case allocation probe drops from 5,590 to 3,585
  allocations and 247,936 to 177,128 requested bytes; no case grows. An eight-call-
  frame/sixteen-repeat-slot example drops 344 to 217 allocations.
- Nin's full/nonzero-default/reset-isolation probes preserve 1,414 complete
  performance/MIDI captures and 6,745,680 bytes with focused sanitizers. Complete
  Prism/Neverland/Itikiti suites also pass with changed constructor units under
  ASan, UBSan, and float-cast-overflow using frozen 13 public headers/archives.
- The frozen fourteenth-batch shell preserves all 252 input/policy comparisons,
  3,922 artifact hashes, inventories, diagnostics, and exit statuses against 13.

Next: remove internal diagnostic factories whose only callers can construct their
complete Diagnostic directly; remove the cursor's duplicate absent/default flow
representation while preserving explicit runtime fallthrough; evaluate three
single-field runtime configuration wrappers and narrow hardware/export header
requirements. These candidates have concrete callers and temporary differential
proofs; public format APIs and mutable working values remain the boundary.

## Cleanup2 fifteenth batch: complete diagnostics and direct runtime values

This batch removes 69 net production lines, bringing cleanup2 to 849 removed.
No permanent test lines change. SequenceVM and export boundaries remain intact.

- Remove four public collection diagnostic factories and their generic private
  helper. Their three callers construct the complete Diagnostic where its
  severity, owner, range, and message are known. The unused optional-asset
  branches disappear; incomplete and ambiguous dependency behavior stays exact.
- CompilerCursor::Event stores its default CommandTransition directly. Its
  private setters only declare non-fallthrough transitions; the existing
  fallthrough value also expresses an undeclared default. Runtime flowOverride
  stays optional because explicitly overriding with fallthrough is meaningful.
  ignore still resets the default and finish still resolves continuation.
  The member shrinks 32 to 24 bytes; Event stays 224 because of alignment.
- VM jump handling uses startTick initialized to the current tick, then replaces
  it when the relevant prior visit exists. Remove the optional whose absent
  branch immediately selected that same current tick.
- Capcom, Itikiti, and Falcom pass their existing version, echo-delay value, or
  PatchTable through makeCompiledRuntime directly. Three single-field Config
  wrappers disappear. Falcom's borrowed patch span retains the same owned runtime
  settings, and mutable playback values remain in state.
- PerformanceAutomationBinding owns its output operation directly; remove the
  private emitter forwarding method. at composes that operation with emitter.at.
  Cross-track rejection, clearing, and automation identities are unchanged.
- Remove private MIDI key/velocity/channel-message forwarders, use existing
  data7/LevelScale conversion and MidiEvent aggregates, and include only the
  actual envelope/modulation models in two hardware/export headers. The MIDI
  renderer header drops unused synth and span includes. All legacy users remain.
- Infallible private NDS PSG and SNES noise decoders return DecodedSample.
  The public decoder remains optional for the encodings that can fail.
- ScanResultBuilder explicitly deletes copying instead of declaring a no-op
  destructor solely to suppress moves. The source-map callback and pending sinks
  require a stable builder address. Before this change, type traits advertised
  copy/move construction through std::deque's unconstrained copy declaration,
  while actual construction already failed on noncopyable pending values.
  Traits now report the real contract and misuse fails at the deleted constructor.
  This clarifies ownership; it is not a repair for an executable copy-time UAF.
  SequenceProgramConfig also drops an empty runtime initializer.

Validation:

- The complete 352-step macOS Release build is warning-free; all 44 CTest suites
  pass. Fully matching public headers, core/formats/fmt archives and the shell
  were frozen after this build, avoiding mixed-layout runtime factories.
- Diagnostic traces match 4,056 cases, 53,640 lines, and 2,369,690 bytes under
  ASan/UBSan: all statuses, defaults, empty/custom ambiguity, missing/invalid IDs,
  attribution, ordering, and sticky failures. Changed units compile with Werror.
- Cursor traces match 175,616 cases and 36,193,594 bytes with focused sanitizers;
  the complete cursor fixture passes. Coverage includes ignore/reset, read and
  operation order, truncation, status, continuation, and duplicate-transition
  errors. The exact final VM preserves 171,938 cases and 206,284,725 trace bytes.
- Typed settings preserve 360 constructor/lifetime cases and 570,504 bytes,
  including copied runtimes and original source/layout/program destruction.
  Complete Capcom/Itikiti/Falcom fixtures and 38 performance/114 MIDI captures
  across three policies pass focused ASan/UBSan/float-cast-overflow checks.
- Automation binding traces preserve 2,880 cases and 340,260 bytes, including
  default, active, cleared and cross-owner bindings; retagging, counters, errors,
  realization, ordering, and ticks through u64 max. The complete VM fixture also
  passes focused sanitizers with the changed emitter unit.
- Sample decoding preserves 1,337 results and 14,810,013 bytes under focused
  sanitizers, including all 256 PSG/noise parameters, rates, lengths, channels,
  loops and ranges. All 42 direct header consumers across legacy, value and tests
  compile with their actual configured flags; the renderer unit is warning-free.
- MIDI preserves 75,043 complete renders and 144,890,623 bytes: event payloads,
  priorities, normalized amounts, diagnostics, order and serialized bytes.
- The frozen fifteenth shell preserves all 252 input/policy comparisons and
  3,922 artifact hashes, inventories, diagnostics and exit statuses against 14.
  Sanitizer claims cover the changed units and focused fixtures; remaining
  linked archives are Release, not a fully instrumented application.

Additional rejected simplifications: keep Session's parent reset when removing
a source, because admission permits an unowned cross-source parent and the child
must detach when that parent's source disappears. A frozen sanitizer probe
exercises that case. Keep the emitter's optional last-note identity: absent
means latest-any, while an engaged invalid identity requests an exact lookup.
Keep synth lowering's inner tremolo guards: physical LFO eligibility does not
mean native export policy, and simulation still needs fixed no-boost attenuation.

Next: narrow the construction contexts to fields their state constructors read,
borrow Namco's immutable DriverData from its runtime owner, and evaluate whether
configured state factories need to retain settings for states that ignore them.

## Cleanup2 sixteenth batch: smaller initialization interfaces

This batch removes 42 net production lines, bringing cleanup2 to 891 removed.
The existing synth fixture gains one net line; cumulative tests gain 427 lines.

- Stream state constructors take the existing SequenceProgram directly. Remove
  StreamStateContext, whose unused sourceTrack field left only that program
  reference. HeartBeatPS1, SonyPS2, factories and fixtures use song defaults
  directly. TrackStateContext drops its unread sequence field while retaining
  the source track and channel number its constructors actually read. The
  transient track-construction value shrinks 24 to 16 bytes, not retained state.
- Configured factories select their constructor and retained settings together.
  Replace the private context/config state-creation overload with the factory
  selector; only constructors that consume Config retain its shared owner.
  States that ignore Config use the existing context/default constructor helper
  directly. One private trait also states the existing unused-Config rejection.
  The three no-config factories use that helper's function specializations
  directly instead of captureless forwarding lambdas. Constructor precedence
  remains context+config, config, context, then default; public format authoring
  still uses the same makeCompiledRuntime overloads. Factory target introspection
  has no consumers. These changes remove 13 header lines without new public types.
- Namco ProgramState borrows immutable DriverData from that owned runtime Config,
  keeping mutable echo state separate. It shrinks 168 to 136 bytes and avoids
  an extra RetainedSource owner on state creation/copy. Readers and analysis
  projections retain their original ownership contracts.
- Synth projection's three overloads become private functions in the existing
  anonymous namespace. All production projection already runs inside the builder
  at finish; remove the public declarations and unused RegionEntry::value getter.
  The physical-envelope fixture exercises ordinary instrument/region construction
  and finish, preserving every field/order/zero/infinity/unit assertion. Live
  sample/instrument inspection and entry-level source annotation APIs remain.
- Remove the unused scanner format getter. Two headers require numeric Types
  rather than CoreTypes. Session's three remaining eager export/playback wrappers
  pass snapshot() directly, consistently with its other wrappers; their results
  own their data. SessionState's shared-vector helper becomes concrete sharedAssets
  because both of its private callers only operate on Asset; filtering remains
  generic only in its actual predicate.

Validation:

- The full 349-step macOS Release build is warning-free; all 44 CTest suites pass.
  The complete final headers, tests, core/formats/fmt archives and shell are frozen.
- Final factory/context overlays pass complete cursor and VM fixtures under
  ASan/UBSan/float-cast-overflow. All 148 configured/unconfigured construction
  combinations preserve 12,844 trace bytes: precedence, void stream, borrowed
  settings, runtime copy, original-owner release, and state isolation. The
  measured 136 allocations/23,392 requested bytes stay identical; captures already
  fit libc++'s std::function storage, so no closure-allocation saving is claimed.
- Context narrowing preserves 171,938 VM cases and 206,284,725 trace bytes with
  focused sanitizers. Complete HeartBeatPS1/SonyPS2 fixtures and 160 varied song
  defaults preserve 15,439 bytes, including five direct performance/fifteen MIDI
  captures. Changed native format closures, context headers, and VM compile
  together against frozen before archives. All 45 configured syntax units pass.
- Namco preserves 6,368 complete performance/MIDI captures and 82,858,671 bytes:
  all three versions, mono/stereo, all 256 generated control configurations,
  repeats, loop policies, truncation, copied runtimes after source/program release,
  repeated-render resets, independent echo and owned analysis projections. Focused
  ASan/UBSan/float-cast-overflow checks include final source-owner expiry.
- Complete synth-builder fixtures pass focused ASan/UBSan with changed builder
  and test units. Both numeric-only includes pass 35 actual configured callers
  and nine self-contained header checks. No legacy consumer includes either
  narrowed header. Release archive boundaries remain uninstrumented.
- The frozen sixteenth shell preserves all 252 input/policy comparisons and
  3,922 artifact hashes, inventories, diagnostics and exit statuses against 15.

Retain symmetric CommandFlow constructors rather than moving their useful authoring
surface into verbose test aggregates or a test-only factory. Retain the live
sequenceUsesSemantic helper used by Hudson's unsupported-command fixture. Also
retain dependency eligibility's canonical asset lookup: a hand-built catalog can
contain duplicate or invalid IDs, so a listed candidate's metadata need not be
the metadata selected by catalog.asset(id).

Next: remove unused source/error authoring overloads and redundant reader queries,
and construct adapted instruments directly instead of deep-copying then replacing
their source region vectors. The latter may add a few lines while making ownership
clearer and avoiding substantial work; line count alone does not decide it.


## Cleanup2 seventeenth batch: one reader window and direct adaptation

This batch removes 21 net production lines across eleven files, bringing cleanup2
to 912 removed. Existing fixture calls change without adding test lines;
cumulative tests retain their 427-line increase.

- RecordReader describes its consumed window through range(). Remove begin()/size()
  queries with no production users; bytes() slices that same range directly.
  Position, capture policy, complete-window finish, sticky read failure and
  positioned-field recovery retain their existing meaning.
- Remove unused authoring overloads: SourceMapBuilder::source, scanner error,
  and the asset-level synth source-record/error helpers. Ordinary annotation,
  warning/Diagnostic and range-based source APIs remain, as do actual entry-level
  SourceRecord projection and AKAO record annotation. Fixtures declare the
  existing SourceRole explicitly rather than retaining an unused facade.
- Playlist visit insertion owns its freshly constructed state directly rather
  than copying an intermediate immutable value. On an allowed replay, copy the
  previous key before clearing the map, then move it into the reset visit table.
  Duplicate-stop boundaries and the repeat-count state remain unchanged.
- Construct adapted instruments from the retained metadata and adapted regions.
  Remove copying every source region, then replacing that vector and clearing
  identity/output preference. Name derivation stays unchanged. This adds four
  lines but makes retained values and ownership explicit and avoids substantial
  discarded work. All nine current Instrument fields are accounted for; future
  metadata additions must also be considered at this explicit adaptation boundary.
- The cursor's private by-value storedCommandValue helper already deduces an
  unqualified value type. Remove its repeated remove_cvref_t expressions while
  retaining owned string_view conversion and direct construction for movable
  values, including explicit move constructors.

Validation:

- The full 354-step macOS Release build is warning-free; all 44 CTest suites pass.
  Final headers, tests, core/formats/fmt archives and shell are frozen together.
- Reader/authoring traces preserve 16,344 cases, 281,238 lines and 20,953,451 bytes
  with focused ASan/UBSan checks, including invalid source identities, both
  diagnostic policies, window/truncation/capture combinations and positioned
  recovery. Complete synth-builder and source-map fixtures pass with changed
  units instrumented; six actual configured translation units are Werror-clean.
- Final playlist/VM/cursor overlays preserve 46,656 cases and 41,141,541 trace
  bytes under ASan/UBSan/float-cast-overflow. Complete cursor/VM fixtures pass.
  Six allocation cases at nested depths 0/1/2/4/8/12 drop from 171,340 to 98,798
  allocations and 7,765,760 to 4,864,080 requested bytes; no case grows. The
  depth-twelve case drops 163,843 to 94,210 allocations with identical output.
- Direct adaptation preserves 2,205 complete preparations and 80,376,319 bytes:
  every Instrument metadata field, YM2151 operators/LFO, regions, voice selection,
  source attribution, address assignment, diagnostics and both MIDI pitch policies.
  Complete variant/resolved fixtures and 48 additional SF2/DLS sample cases pass
  focused sanitizers. The independent MIDI matrix preserves 75,043 renders and
  144,890,623 bytes. Linked baseline archives remain Release; these are focused
  changed-unit checks, not a fully instrumented application.
- Twenty-four adaptation allocation cases cover 1/4/32/3,000 source regions and
  1/4/16 envelope/stereo variants; no case grows. With long names, three
  allocations disappear per variant. Sixteen 3,000-region variants avoid
  19,203,072 requested bytes. This measures cumulative allocation requests during
  preparation, not peak resident memory.
- The frozen seventeenth shell preserves all 252 input/policy comparisons and
  3,922 artifact hashes, inventories, diagnostics and exit statuses against 16.

Retain MaskBytePattern's constexpr string and unsigned-byte representations:
a single runtime view loses an existing constexpr construction path without
removing logic. Retain separate SequenceCollectionOptions enablement/name because
disabled sequences can retain their metadata. Retain the VM's owned channel
grouping rather than adding borrowed spans, reserve/destruction ordering and a
separate flattened owner. Retain source interval boundaries and late attack
resolution in the MIDI planner; ties, expired intervals, orphan attacks and LFO
restarts need those distinct meanings. SequenceProgramConfig remains decode
configuration because its commandKindPrefix is not runtime playback state.

Next: evaluate constructing ScanResult only when finalizing pending drafts, simplify
WAV's exact RIFF capacity bound, and remove a duplicate polling guard only where
SequenceVM already guarantees eligibility. Continue tracing actual data ownership
and responsibility rather than treating every optional field as removable.


## Cleanup2 eighteenth batch: finalize one result and poll eligible commands

This batch removes five net production lines across seven files, bringing cleanup2
to 917 removed. The existing synth fixture gains nine meaningful diagnostic-order
lines; cumulative tests gain 436 lines.

- ScanResultBuilder keeps pending assets and a stable diagnostics vector while
  scanning, then constructs the complete ScanResult locally after required-program
  preflight. Remove the unused retained output staging value. Synth builders keep
  the same stable sinks; finish moves diagnostics only after all callbacks and
  annotations are complete. Ordinary return also permits named-return elision.
  Builder size shrinks 576 to 528 bytes. Missing-program repair remains safe before
  any consumption. If a later finalization fails after consumption, unpublished
  partial assets now release immediately rather than at builder destruction;
  these destructive failures do not establish a repair/retry contract.
- TrackDecodeSession keeps its annotation identity directly. Its invalid ID already
  expresses absence, as in TrackProgram and SourceCommand. Remove the duplicate
  optional representation and value_or/dereference protocol. Parent and root-owner
  options retain their actual presence semantics. The transient session shrinks
  88 to 80 bytes, with no net line change.
- SequenceVM alone checks whether the pending command supplies a wait predicate
  before selecting the channel and invoking the runtime. Remove the compiled
  bridge's duplicate guard; its responsibility is constructing the typed playback
  and calling that known predicate. Missing runtime callbacks, leading delays,
  budgets and bodyless command execution retain their existing VM behavior.
- Clarify note-reset comments: the shared rule is applied after the planner resolves
  attacks versus continuations and also controls pan LFOs; explicit vibrato/tremolo
  overrides apply on continuations. The actual fields and behavior stay intact.
- WAV checks its complete RIFF payload budget before PCM multiplication, replacing
  two checks with the exact bound. Both former rejection paths used the same
  exception. Upfront reservation uses size_t{44} + dataBytes, correcting u32
  capacity overflow at the final four accepted lengths. RIFF payload encoding
  and accepted PCM counts remain unchanged.

Validation:

- The full 334-step macOS Release build is warning-free; all 44 CTest suites pass.
  Final headers, tests, core/formats/fmt archives and shell are frozen together.
- Scanner traces preserve 1,283 lines and 84,355 bytes under focused sanitizers:
  required-program repair, mixed ordered assets, duplicate samples, inspection,
  finalization diagnostics and exact errors for manually finished domains/custom
  allocator failures. Complete registry/synth fixtures pass ASan/UBSan/float-cast-
  overflow; the changed unit is Werror-clean. A weak-owner probe confirms repair
  retains data, successful output owns it, and destructive partial failure releases
  it earlier as described above. The permanent fixture checks finalization warnings
  follow scan warnings with exact ranges.
- Warm scanner construction still allocates zero. Cold construction defers five
  one-time empty SourceMap initialization allocations/224 requested bytes to
  finalization. This is not a completed-scan allocation saving.
- Annotation traces preserve 45,360 cases, 162,540 lines and 5,306,744 bytes:
  valid/invalid source IDs, track numbers, large starts through u64 max, tracked/
  trackless/unmapped decoding, absent/valid/invalid parents and owners, empty and
  populated commands, and five allocator policies including invalid IDs. Complete
  cursor fixtures pass with changed CommandSourceMap units and ASan/UBSan/float-
  cast-overflow. Invalid allocated IDs still remain visible in the map and never
  enter its lookup indexes; skipped invalid-ID range updates were already inert.
- Polling preserves 62,208 cases and 39,796,849 trace bytes with focused sanitizers,
  including delay/readiness/budget combinations, routing and undeclared channels,
  shared/independent streams, source tracks, startup/tick/beforeCommand hooks,
  bodyless commands and missing predicate/runtime callbacks. Complete cursor/VM
  fixtures pass. Linked archive boundaries remain Release.
- WAV preserves 505 complete captures and 7,160,770 bytes with both changed units
  and full synth-export fixtures instrumented. All channel/rate exception strings
  match. Numeric predicates match 1,196,614 boundary/extreme/random counts; the
  exact maximum is 2,147,483,629 PCM values/4,294,967,258 data bytes. Configured
  arithmetic proves the four formerly wrapped reservation capacities become
  4,294,967,296/298/300/302. No multi-GiB allocation was attempted.
- The frozen eighteenth shell preserves all 252 input/policy comparisons and
  3,922 artifact hashes, inventories, diagnostics and exit statuses against 17.

Retain the LFO reset representation: its eighteen configurations express thirteen
distinct attack/continuation policies, exceeding even three completed booleans.
Portamento can change that final attack decision, and explicit overrides differ
from inherited defaults on ties. An independent 270-case planner probe confirms
those distinctions. Retain preparation view vectors and typed callback wrappers:
lazy views defer failure until after prefix mutation, and early cardinality checks
change malformed-input error precedence. Retain short explicit endian reads and
immutable chunk iteration rather than adding a new width/order or borrowed-view
lifetime protocol to replace a few straightforward operations.

Next: evaluate delaying cursor operation type erasure until the durable command-body
boundary, remove dead arrival writes after a stream has no command, and make the
shared byte-reader bounds predicate as direct as its other bounds checks. These
candidates preserve the existing authoring DSL and execution/storage models.


## Cleanup2 nineteenth batch: borrow callback inputs and erase commands once

This batch removes seven net production lines across six files, bringing cleanup2
to 924 removed. The existing SSF extraction fixture gains three net lines checking
that returned metadata survives the callback input; cumulative tests gain 439 lines.

- ExtractionInput borrows SourceFile metadata for its synchronous callback, as it
  already borrows the reader and sibling SourceStore. Each extracted output still
  owns its metadata. Session's local resolved SourceFile stays owning: adding
  children can reallocate the store's entries before later children or exception
  handling use that original metadata. ScanInput stays owning because a scan
  builder can survive its caller's temporary input. Correct the draft comment to
  describe builder-owned pending assets. ExtractionInput shrinks 280 to 40 bytes.
- Standalone MIDI export transfers its locally rendered PerformanceSequence into
  preparation. It uses only the independent modulation profile and diagnostics
  afterward. Collection preparation retains its source-performance copy because
  playback and inspection expose it. No new owner or preparation wrapper is added.
- Cursor operation construction returns its owned concrete lambda. Append composes
  that lambda and erases only the final durable command body, avoiding an erased
  std::function captured inside another erased std::function. The DSL, ownership,
  execution semantics and stored Event/CommandBody sizes remain unchanged.
- Remove five VM arrival writes immediately after clearing its command. Arrival
  is read only for a present command. Every section or synchronized restart
  establishes arrival itself; valid control-flow and fallthrough updates remain.
- ByteReader::has directly short-circuits its two bounds conditions, preserving
  subtraction safety and every accepted offset/length.

Validation:

- The complete 310-step macOS Release build is warning-free; all 44 CTest suites
  pass. Freeze matching headers, tests, core/formats/fmt archives and shell. The
  nineteenth shell preserves all 252 input/policy comparisons and 3,922 artifact
  hashes, inventories, diagnostics and exit statuses against 18.
- Extraction preserves 6,000 SPC cases/554,073 bytes and 9,984 PSF cases/456,931
  bytes, including outputs inspected after their source/input scope expires.
  Complete registry, session, source-map, SegSat, TriAce, MP2k, CUE, SonyPS2 and
  SquarePS2 fixtures pass with Session and all six extractor implementation units
  rebuilt consistently under ASan/UBSan/float-cast-overflow; eight configured units
  pass Werror syntax checks. Input construction allocates zero for short metadata
  before and after; long metadata falls from five allocations/560 requested bytes
  to zero, and 40 attributes/24 segments from 222 allocations/21,808 bytes to zero.
- Standalone MIDI preserves 13,954 complete artifact captures/73,829,902 bytes in
  Release and focused sanitizers, covering 864 option combinations across eight
  source patterns, unavailable or wrong-type assets, factory/executor/finalizer
  errors, empty sequences/tracks, repeated exports and copied snapshots. Complete
  session/collection-export fixtures pass. Every 16 allocation case improves: an
  80,000-note/80,000-long-marker case avoids 160,013 allocations/75,531,024 requested
  bytes; numeric notes with source spans avoid 30,730,208 requested bytes.
- Real standalone export also preserves all 856 sequence assets in the 126-input
  inventory from snapshots after their Sessions and SourceStores were destroyed:
  16,218,005 captured bytes with SHA256
  1c048b2428f933094d9677d3ec183405df8c62c14377590401a6f155f62e0725.
  Every case reduces allocations; this inventory avoids 9,897 allocations and
  533,822,224 requested bytes in total. Its largest SculptSoftSnes song has 98,340
  events/243,636 source spans and avoids 42,446,784 requested bytes. The candidate
  export unit and real-input probe also pass focused sanitizers; unchanged archives
  remain Release. These are cumulative allocation requests, not peak-RSS claims.
- Combined cursor/VM fixtures pass focused sanitizers. VM traces preserve 171,938
  cases/206,284,725 bytes including synchronization, sections, nested calls/repeats
  and zero command budgets. Cursor traces preserve 175,616 cases/36,193,594 bytes;
  363 additional ownership/copy/execution-exception cases preserve 2,699,232 bytes,
  including expired decoded inputs, long strings/embedded NULs, stateful copies,
  waits and preceding side effects. All 84 construction/copy allocation cases
  improve or stay equal: 3,006 allocations/1,251,936 requested bytes become
  1,980 allocations/1,191,744 bytes. CommandBody remains 48 bytes and Event 224.
- ByteReader preserves 48,114 slice/bounds cases plus fixed-width read outcomes,
  80,179 lines and 4,817,370 bytes, with its implementation/probe instrumented:
  source sizes through 1,024, valid/invalid IDs and offset/length extremes through
  u64 max. All fixed-width read exception strings match.

Cursor construction-failure boundary:

Delaying erasure changes intermediate copy/move and allocation stages. Successful
execution and execution exceptions are exact; arbitrary reuse of a command after
append construction fails is not an equivalence claim. Neither implementation
provides general append rollback: the former final boxing can already discard
its previous body on allocation failure. Production decoders do not catch failed
append and reuse the incomplete Event; scanning discards that failed decode.
Ordinary captured native values have nonthrowing moves.

A 1,512-case instrumented throwing-copy/move probe records 880 both-success cases
with equal recovered execution, 168 shared copy failures and 320 shared move
failures with equal reuse; 32 shared move failures retain fewer earlier operations
in the candidate, and 112 fail only at its additional move stages. First/composed
handler moves change 4 to 5/6; owned-argument moves 7 to 8/9; copy count stays one.
A separate 208-case native allocation-failure probe records 180 both-success,
13 shared failures with equal reuse, six shared failures with fewer retained earlier
operations, and nine failures only in the former extra allocations; no candidate-
only native allocation failures occur. Keep the simpler final-erasure boundary
without introducing conditional erasure or a rollback protocol, and retain this
explicit limitation rather than claiming all construction side effects are equal.

Retain bulk decoded-field copying: real inputs have no non-opcode value strings
beyond short-string storage, while per-field movement adds vector growth and a new
private authoring protocol. Retain the intentional PreparationFailure channel;
throwing arbitrary Diagnostic values would widen callback failure semantics.

Next: remove unused SourceMap convenience queries and move flattened portamento
state into the MIDI planner. Keep actual source portamento enable, physical pitch
settings and musical slide intent in the neutral performance model. Continue
probing ownership and dead state without reducing distinct runtime responsibilities
merely because their fields look similar.


## Cleanup2 twentieth batch: keep MIDI controls private and queries purposeful

This batch removes 31 net production lines across seven files, bringing cleanup2
to 955 removed. Six existing fixtures lose 19 mirrored/obsolete-query lines;
cumulative tests still gain 420 lines against the branch baseline.

- Remove four unused public SourceMap queries: intersecting(), containing(), at()
  and linksTo(). No production callers, chained uses or member-function pointers
  use them. The fixture checks the stored link's exact role/target directly.
  SourceRange predicates, SourceInspection.itemAt() and the actually used source,
  asset, hierarchy, role and semantic indexes remain. No generic query framework
  replaces the removed convenience surface.
- Remove PortamentoPerformanceEvent from the target-neutral PerformanceEvent
  variant. All three producers are in MIDI lowering; source formats emit physical
  pitch settings and PitchTransitionIntent instead. Place the unchanged flat
  representation in detail::MidiPortamentoEvent next to the other planner events.
  Remove the impossible source passthrough branch. Keep the live source
  PortamentoEnablePerformanceEvent used by SonyPS2 and other consumers.
- PerformanceAutomationBinding::clear() invalidates only its owner. That already
  defines absence, and every operation gates index access on that owner. Remove
  the redundant private index reset; copies and subsequent assignment retain
  their ordinary value semantics. No new sentinel or optional is introduced.

Validation:

- The full 354-step macOS Release build is warning-free and all 44 CTest suites
  pass. Freeze the complete matching headers, tests, core/formats/fmt and shell,
  because deleting a variant alternative changes neutral tags even though sizes
  do not change. The twentieth shell preserves all 252 input/policy comparisons
  and 3,922 artifact hashes, inventories, diagnostics and exit statuses against 19.
- SourceMap's complete fixture passes with its changed implementation and fixture
  under ASan/UBSan/float-cast-overflow against frozen 19. The changed model is
  Werror-clean; existing shared test-helper unused warnings are suppressed for
  that fixture. Final files byte-match the tested overlay. Retained graph storage,
  indexing, fields, links and ownership behavior do not change.
- For portamento, rebuild all 44 candidate value-core units and both probes/full
  MIDI renderer and pitch fixtures under focused sanitizers, paired with a fully
  matching baseline. Preserve 75,043 captures/144,890,623 MIDI, structured-event and
  diagnostic trace bytes: 74,912 generated renders plus 131 fixture renders.
  Another 7,776 targeted cases preserve 6,001,440 bytes of neutral source notes,
  settings, enables, tempos and slide intent, native planned controls/attacks and
  MIDI. Eight directly touched implementation/test units pass strict warning-free
  syntax checks. There are no production neutral variant-index consumers.
- PerformanceEvent has 22 alternatives instead of 23. PerformanceEvent and the
  private MidiTrackEvent both remain 352 bytes; no layout-size saving is claimed.
  Four format fixtures still check their meaningful source slide intent and
  exported behavior; the positive MIDI planner assertion now names its private
  type. The type system itself excludes flat MIDI controls from source events.
- Clear-invalidation preserves 11,520 cases/123,264 lines/2,856,866 bytes, including
  binding indexes zero/one/three/eight, inherited output associations, foreign
  tracks, scheduling methods and tick extremes. Another 128 lifecycle cases
  preserve 832 lines/9,970 bytes across repeated clear, interruption, emptied
  automation storage and every pitch-binding option after the old track is
  destroyed. Both probes and the changed emitter unit pass focused sanitizers;
  unchanged core/fmt archive boundaries remain Release.

Retain actual musical compatibility contracts: source portamento enable, physical
pitch settings, channel pan, global transpose, legato, mono defaults and time
signatures all have format authors. Retain diagnostic annotation presence: an
explicit invalid reference is diagnosed, while absence requests no lookup. Keep
AssetPrivateData's standard erased immutable owner rather than replacing it with
manual RTTI/tag synchronization solely for a smaller representation.

Next: eliminate the second VM playback-track count, assess direct MIDI track
serialization into its final buffer, and test whether source inspection can own
its complete annotation/field child projection. These should remove duplicate
responsibilities without adding a framework or hiding format behavior.


## Cleanup2 twenty-first batch: count once and serialize into the final buffer

This batch removes 22 net production lines across six files, bringing cleanup2
to 977 removed. One existing VM fixture drops its mirrored helper-count clause;
its actual rendered-track assertion remains. Cumulative tests still gain 420 lines.

- The VM's existing allocation position already counts every declared output
  channel while constructing executors. Keep that position as size_t, casting
  explicitly only when assigning TrackId, and reserve the result tracks with it.
  Remove SequenceProgram.playbackTrackCount() and the second traversal. The
  size_t count retains the previous reservation extent, while IDs retain their
  existing u32 conversion behavior. No parallel counter or channel view is added.
- MIDI appends each sorted track's encoded messages directly to the complete SMF
  byte vector and fills its four-byte track length afterward. Delete the owned
  trackData buffer and its complete copy. Message expansion/order, delta encoding,
  clipping and header narrowing stay in their existing functions. Four local
  lines are added to remove an entire buffer owner; no generic patch abstraction
  or new intermediate model is introduced.
- DLS writes its fixed-size articulation connections directly after the eight-byte
  art2 header, then fills the count from the complete record bytes. Delete the
  connections buffer and copy without adding a mutable record counter. Generator,
  modulator, envelope and instrument/region scope order and filtering remain.
  This adds three local lines, following makeRiff's existing fill-after-write
  approach. Wave preparation and offset/error-precedence stages remain distinct.
- Removing the unused SourceMap range queries in 20 also removes the only users
  of SourceRange::contains(SourceRange) and intersects(SourceRange). A fresh,
  independently repeated whole-source/test/member-pointer audit finds no callers.
  Delete those two unused inline predicates and their comments. Retain the live
  point query containsOffset(), range accumulation, validity and endpoints. NDS's
  unrelated private subtraction-based contains(bounds,offset,size) stays intact.

Validation:

- The full 373-step macOS Release build is warning-free and all 44 CTest suites
  pass. Freeze matching headers, tests, core/formats/fmt and shell. The twenty-first
  shell preserves all 252 input/policy comparisons and 3,922 artifact hashes,
  inventories, diagnostics and exit statuses against 20.
- Complete VM fixtures and both changed implementation units pass ASan/UBSan/
  float-cast-overflow against matching frozen 20. Preserve 171,938 general,
  zero-budget, synchronized, playlist and nested cases/206,284,725 trace bytes.
  Another 640 geometry/runtime/budget/playlist cases preserve 255,208 bytes;
  20 seeded allocation positions around the u32 boundary preserve 2,179 bytes of
  IDs and initial global event placement. Existing count state changes type, not
  retained storage. Configured implementations/fixture are warning-free.
- MIDI preserves 85,681 complete captures/231,627,507 bytes in both Release and
  focused sanitizers against frozen 20, SHA256
  8ddf2579a9e8daed5acf9991174ec9a91ff35bfa775b41e3a2cc1e0e8923097f.
  These are 75,043 renderer/fixture captures plus 10,638 raw encoder captures:
  10,584 main boundary cases, seven empty-track counts, eleven payload lengths and
  36 allocation cases. Raw cases include coincident priorities, note clipping and
  zero duration, unknown message kinds, timebase/tick extremes, embedded-NUL/high-
  byte names, meta/SysEx length boundaries and 65,535/65,536 tracks. All ten existing
  fixed expected-SMF encoding tests pass independently in both builds.
- All 36 MIDI allocation cases improve. Four tracks each containing 100,000 notes
  plus 128-byte markers avoid 59 allocations/75,632,068 requested bytes:
  511,534,149 becomes 435,902,081. This removes a per-track serialized owner rather
  than changing the necessary sorted wire-message representation.
- DLS preserves 972 private articulation captures plus all existing synth fixtures
  (19 successful SF2/DLS artifact/ordered-diagnostic captures and overflow asserts),
  24,708,750 bytes in Release and focused sanitizers, SHA256
  fcdf5525dfc56ea8566db1c67490c14cf5e00ebd6b6c9e98583f32eb8405af11.
  Cases cover every destination/source, amount clamp boundaries, all modulation
  scopes, absent/negative/zero/finite/Inf/NaN envelope times, sustain/pan boundaries,
  unsupported routes and count high bytes through 65,536 generators/modulators per
  scope. All 36 allocation cases save five allocations: default 470 requested
  bytes becomes 375; large dual scopes 13,500,630 becomes 10,944,631. Changed units
  and probes are warning-free; one preexisting unused fixture-helper warning stays.
- Probe/changed-unit sanitizer checks link unchanged matching Release archives;
  they do not establish that every linked archive is instrumented. Allocation
  counts are cumulative requests, not peak-RSS measurements.

Rejected source-inspection centralization:

A concrete prototype replaced annotation-only children with SourceInspectionItem
children, centralizing opted-in field projection and removing Qt's field-leaf
branch. Its 2,400 admitted graphs preserve 111,088 lines/5,144,449 bytes under focused
sanitizers, including source-released inspections, field leaves, hierarchy/order
and derived/foreign-field behavior. Qt/shell temporary units are Werror-clean.
The simple version removes seven net lines; using the known field owner directly
in its comparator reduces the saving to four, while still doing field work the
shell does not need. Every nonempty parent adds one vector allocation: 3,072
requested bytes at 256 field-only children and 49,152 at 4,096. Refined complete-
print medians over three runs are 36.58 to 48.32 microseconds for repeated-offset
256 fields, 509.69 to 917.50 for 4,096, and 267.82 to 387.15 for 512 annotation
children plus 256 fields. Contiguous 256 fields are closer, 35.93 to 38.43.

Retain the existing distinction: core annotation traversal serves the shell's
annotation intent, while Qt projects field children for presentation. The proposed
private comparator optimization also works in the current Qt boundary and does
not uniquely justify changing the shared API. Avoid broadening work or adding a
second projection protocol for a four-line gain.

Next: test consuming fresh ranged cursor operands instead of copying their returned
strings, and correct a discovered MIDI signed-pitch clipping overflow before adding
its wire-format bias. Continue probing actual ownership and failure boundaries.


## Cleanup2 twenty-second batch: consume cursor reads and assemble DLS lists directly

Four production files remove two net lines, bringing cleanup2 to 979 removed.
The MIDI overflow regression fixture adds 32 lines; cumulative tests gain 452.
Three changes are LOC-neutral but remove ownership or invalid-state concepts.

- CompilerCursor's private decoded() consumes the fresh RangedValue returned by
  RecordReader and moves its value into the caller. Source fields still receive
  their independently owned values. This removes a second returned-string copy,
  without changing RecordReader, adding a move-aware public API, or borrowing
  from the reader. All ten callers pass freshly returned ranged values.
- Session's private FormatScan requires a borrowed FormatModule reference instead
  of a nullable pointer. Registry registration is sealed before scanning; entries
  are appended, moved during vector growth, then read. No rebind or assignment is
  needed. Remove dereference syntax and the impossible null default, retaining
  module lifetime, concurrency, exception handling and ordered serial admission.
- DLS builds the existing lrgn/lins LIST payloads directly, appending each completed
  child with appendChunk(). Remove the two intermediate vector<Chunk> owners and
  the subsequent complete serialization pass. The existing LIST type, child
  ordering, padding and RIFF helpers remain; no new abstraction or callback is
  introduced. Wave preparation and validation still precede both paths.
- MIDI clips a signed pitch offset to [-8192,8191] before adding 8192. The previous
  addition overflows for large positive s32 input even though pitchBend()'s
  convenience API accepts s16: raw MidiChannelMessage accepts s32. UBSan reproduces
  the preexisting INT_MAX overflow. Correct clipping produces 7f/7f and preserves
  every previously defined result. Add eleven fixed wire-byte cases for signed
  extrema, clipping endpoints, zero, and the previous overflow threshold. No
  equivalence claim is made for undefined original inputs.

Validation:

- The full 102-step macOS Release build is warning-free; all 44 CTest suites pass.
  Freeze matching headers, tests, core/formats/fmt and shell. All 252 input/policy
  comparisons preserve 3,922 artifact hashes, inventories, diagnostics and exit
  statuses against 21.
- Reader/cursor probes preserve 15,372 cases/19,784,777 bytes and 175,616 cases/
  36,193,594 bytes against frozen 21 under focused sanitizers. Another 1,458 targeted
  cases preserve 218,785 bytes: empty, truncated and out-of-range reads, capture
  modes, empty/embedded-NUL names, invalid source IDs, independent string mutation,
  and returned values and annotation fields after reader/cursor/source destruction.
  Complete cursor and VM fixtures pass; twelve configured cursor/runtime fixture
  and HeartBeat PS1/Sony PS2 compile entries are Werror-clean. Of 32 allocation
  cases, eighteen improve and none grow: 67 allocations becomes 49, and 2,407,960
  requested bytes becomes 1,605,720. Scalar reads keep their existing semantics.
- Full Registry, Session and SourceMap fixtures pass before/after with Session.cpp
  and fixture/probe units instrumented. A 720-case scanner/admission matrix preserves
  9,141 lines/744,259 bytes: 0/1/2/5/9 modules, known-format policies, empty/nonempty
  sources, standard/empty-what/nonstandard throws, reused IDs, foreign ranges,
  missing owners and missing runtimes. Configured Session compilation is Werror-clean.
- MIDI preserves 216,759 captures/236,608,471 bytes in Release and focused sanitizers,
  SHA256 c6d42b95418a564a49a09624af5a111b49deaaf4e47b4c8fcb35a2291b889477.
  These include the prior 85,681 complete renderer/encoder captures, all 65,536 s16
  offsets on channels 0 and 255, and six defined s32 edge cases. All ten existing
  encoding fixtures and the new eleven-case fixture pass. INT_MAX is checked
  separately: the original fails UBSan, the corrected writer clips it correctly.
- DLS preserves 19 complete synth artifacts/ordered diagnostics, 972 articulation
  edge captures, and 140 nested-list cases, 41,481,550 bytes in Release and focused
  sanitizers, SHA256 8051496d271a928ab137ab42dd6ed3de2584d4685bac342f7bd7b733b914ebf8.
  Nested cases vary 0–10 instruments and 0–1,000 regions, empty/odd/long names,
  local stereo PCM, loop overrides/zero lengths, rich envelopes, global/region/
  unsupported modulation and both bank styles. Of 140 allocation cases, 112 improve
  and 28 empty-list cases remain equal. Ten instruments with 1,000 regions each
  avoid eleven allocations/480,480 requested bytes: 36,627,233 becomes 36,146,753.
- Physical modulation lowering bounds a production rgn2 payload to 550 bytes
  (at most seven generators and eight modulators per scope, two scopes, and six
  fixed articulation connections). Immediate lrgn append cannot introduce a new
  oversized-child failure; its complete LIST receives the same parent size check.
  lins may reject an oversized individual instrument before constructing later
  pure children, but their only explicit failure is the identical RIFF-size error.
  No source callbacks or retained spans cross that boundary. Huge allocations and
  allocator-failure ordering are not claimed equivalent.
- Changed/probe/fixture sanitizer instrumentation links matching unchanged Release
  archives; it does not establish whole-archive instrumentation. Allocation totals
  measure requested bytes rather than peak RSS.

Further probes retain real boundaries. Bypassing decoded() for rawBytes() would
lose source fields: CompilerCursor's RecordReader disables field capture, leaving
add() as their sole owner. Value-role reads already add no semantic operand.
Moving command-span assignment into noteOn() would require a new current-span
protocol and change startup/tick-hook and immediate noteOn/noteOff associations.
The current post-body event boundary handles these distinctions directly.

Next: remove a newly identified unused sample-index object kind and ObjectRef hash,
and test whether SF2's sole smpl child can be written into sdta without a second
complete PCM buffer. Continue testing concrete improvements against frozen 22.


## Cleanup2 twenty-third batch: remove unused selectors and duplicate PCM ownership

Five production files remove nine net lines, bringing cleanup2 to 988 removed.
The LFO regression adds 27 test lines and an obsolete selector mirror drops two;
cumulative tests gain 477 lines. The SF2 writer grows eleven lines to remove a
large serialized owner, while the other simplifications reduce the overall code.

- Delete ObjectKind::SampleIndex and ObjectRefs::sampleIndex: no format or model
  creates this speculative selector, and its sole test mirrors the distinction
  from an actual owned sample. Keep live InstrumentIndex/InstrumentProgram and
  ObjectRefs::sample, which have real source producers. Delete the unused
  std::hash<ObjectRef> specialization; no hashed ObjectRef container or hash call
  exists. Id hashing remains. ObjectKind::Misc shifts from 9 to 8; this internal
  enum has no serialized ordinal or raw-numeric consumer. Compare its semantic
  identity rather than claiming unchanged underlying numbers.
- SF2's private sdtaChunk writes the LIST type, smpl header, PCM and required
  padding directly into one payload. Delete the complete encoded-smpl owner and
  its copy into sdta. Declare/check the cumulative PCM-plus-padding frame count,
  reserve the known twelve-byte prefix plus data size, and use existing header/
  PCM writers. Fill the fixed 46-frame zero padding with one vector insert rather
  than a counter loop. Retain INFO/SDTA/PDTA outer child preparation: streaming the
  whole RIFF could reorder SDTA-size and PDTA table-index errors.
- AssetCatalog's constructor is its sole asset-index insertion path and always
  excludes invalid IDs. Remove asset()'s duplicate invalid-ID gate and use the
  same filtered-index lookup as SessionSnapshot. Keep SessionState::asset()'s
  gate: direct low-level callers can append malformed, invalid-ID assets even
  though production admission rejects them. Do not change insertion semantics
  just to move that guard to another place.
- SessionState removal callers already build valid-only source/asset sets.
  Remove six repeated validity conjuncts before membership probes; keep the
  actual source-input and asset-ID filtering. A comment records that private
  boundary. Preserve optional object/annotation presence, ObjectRef variant
  checks, and all annotation membership: malformed visible annotation rows can
  contribute invalid IDs to removedAnnotations, unlike the two removal sets.
- SequenceLfoDepthFadeState divides its nonnegative target magnitude by the
  actual u32 duration. Delete the s32 narrowing cast that turns durations above
  INT_MAX into negative divisors. The unsigned quotient always fits the target's
  s32 range. This corrects large configured durations while retaining native
  source widths, delay, restart, initial-depth and physical-clamp behavior. Add
  a fixed first-tick/delay regression covering INT_MAX, INT_MAX+1 and UINT_MAX,
  largest-depth one-tick completion and negative initial depth. No new motion
  policy or state is introduced.

Validation:

- The full 373-step macOS Release build is warning-free and all 44 CTest suites
  pass. Freeze matching headers, tests, core/formats/fmt/Qt value archives and
  shell. The twenty-third shell preserves all 252 input/policy comparisons and
  3,922 artifact hashes, inventories, diagnostics and exit statuses against 22.
- Object-reference removal passes complete Registry, Session, SourceMap and
  SynthBuilder fixtures plus the Qt UI fixture under focused sanitizers. Rebuild
  five graph/session/synth units, both Qt value implementation units and moc.
  Another 720 tagged-owner/link/admission/inspection cases preserve 16,599 lines/
  924,757 bytes using semantic ObjectKind names, SHA256
  e9989f3762347c6295e1ca30558731c3db8b01efc8a6ec0331840165afa79c37.
  Strict graph/session/synth compilation is Werror-clean. Qt's existing locale/
  missing embedded-icon notices are identical and both fixtures pass.
- SF2 preserves all 19 complete synth artifacts/ordered diagnostics plus 203
  private PCM/header/padding/capacity cases, 22,091,734 bytes in Release and focused
  sanitizers, SHA256 9ada803f202d6be77897fdc4cc2818b5ebc3e045f7585b859a9d85001ceb688f.
  Existing explicit overflow assertions pass and input PCM/name values remain
  unchanged. All 203 allocation cases improve. For 128 samples of 16,384 frames,
  28 allocations becomes one and 20,999,591 requested bytes becomes 4,222,348.
  The empty table improves from three allocations/28 bytes to one/12 bytes.
  The subsequent padding-insert comparison preserves all these bytes and counts.
- Eighty numeric size edges confirm rejection exactly when twice the cumulative
  frame count exceeds u32. Maximum even child size is 4,294,967,294; the twelve-
  byte LIST/child prefix is added in size_t. The existing outer parent check still
  runs after PDTA creation. The child-size check moves only before pure PCM writes,
  crossing no source callbacks or distinct explicit error. Native allocation
  failure ordering/huge physical allocations are not claimed equivalent.
- Catalog/removal guard proof preserves 2,385 cases/108,152 lines/3,621,847 bytes,
  SHA256 0b4a0cfd162b59d69cf69a993512c1b0d1aa2b1a50eb7d48d3642eeb1cab80db.
  These are the prior 337 typed-ID/index scenarios plus 2,048 malformed removal
  combinations: duplicate/invalid/highest IDs, invalid source ranges and annotation
  rows, every source/annotation/object link kind, diagnostic references, repeated
  removal, lookup and retained snapshot ownership. Complete Registry, Session,
  SourceMap and SynthBuilder fixtures pass before/after with changed Catalog/State,
  graph/session, fixture and probe units instrumented. Strict compilation is
  Werror-clean. The only addition after proof is the explanatory private-set comment.
- LFO preserves 1,572,864 native-duration cases/66,983,771 bytes (every u16 duration,
  clamped/s32-max targets, delay and signed initial depth) and 5,040 lifecycle/binding
  cases/2,429,465 bytes under focused sanitizers. These cover reconfiguration,
  clear/reset, restarts, physical suppression and automation attribution. Of 267
  defined large-duration boundary cases, 69 intentionally correct the old negative
  divisor; they are not parity claims. A separate old max-target/negative-initial/
  UINT_MAX combination reproduces signed overflow; the candidate is clean. The
  permanent regression fails the old implementation and passes the new one.
  Eighteen configured format/model syntax entries are warning-free.
- Focused instrumentation links matching unchanged Release archives; it does not
  establish complete-archive instrumentation. Allocation counts measure cumulative
  requests, not peak RSS. An existing unused synth-fixture helper warning remains
  limited to the temporary fixture probe; the complete application build is clean.

A proposed direct BytePattern slice was rejected before implementation. slice()
throws on invalid bounds, while matchesBytePattern() promises false; its has()
gate is necessary even though MaskedBytePattern also checks candidate length.
Keep that failure boundary rather than deleting a guard on an incorrect premise.

Next: probe SourceMap's similarly filtered query indexes, shared linear-motion
narrowed duration division, and DLS's one-use encoded PCM child owner against
frozen 23. Keep positive/negative arithmetic and exporter failure order explicit.

## Cleanup2 twenty-fourth batch: one wave payload and full-width arithmetic

Four production files remove two net lines, bringing cleanup2 to 990 removed.
Two regression fixtures gain 42 lines; cumulative tests gain 519. The two numeric
fixes change one expression each rather than introducing policies or state.

- DLS waveList writes the encoded PCM directly after its existing fmt child.
  Delete dataChunk and its complete temporary PCM payload. Check the child size,
  prepare the existing INFO child, reserve the complete wave size once, then
  write the data header/PCM and append INFO. Retain the wave-pool/offset staging
  and existing parent checks: their error order is distinct from the child copy.
  This is line-neutral and removes an owner rather than hiding it in a helper.
- SequenceLinearMotion divides by the full duration as s64, independently of
  ValueType. Narrowing ticks to ValueType could produce a zero or negative
  divisor. A signed wide divisor also preserves descending integral fades;
  simply removing the cast would introduce unsigned arithmetic. Retain numerator
  arithmetic, integer truncation, delays, supplied-step modes and exact target
  completion. The regression covers ascending/descending UINT_MAX durations,
  unsigned-byte duration 256 and signed-short duration 65536.
- SF2 reverb conversion clips the scaled double before casting to s16. The
  former intermediate s32 conversion overflowed for valid large finite sends,
  before its intended clipping could occur. Keep nonfinite input handling and
  the existing signed-16 limits. The public export fixture confirms that 1e7 and
  DBL_MAX pass synth validation and serialize the saturated generator correctly.
- Delete unused sptr/uptr aliases. Whole source/tests/documentation and ignored
  build/project searches find no type consumer. Unrelated third-party C variables
  with the same spelling use their own pointer declarations. Keep the fixed-width
  integer aliases and size_t used throughout the codebase.

Validation:

- The full 559-step macOS Release build is warning-free; all 44 CTest suites pass.
  Freeze matching source headers, tests, core/formats/fmt/Qt value archives and shell.
  The twenty-fourth shell preserves all 252 input/policy comparisons and 3,922
  artifact hashes, inventories, diagnostics and exit statuses against 23.
- DLS preserves 5,298 private wave captures, 33 wave-pool/order/error traces and
  19 complete synth artifacts: 23,186,119 bytes in Release and focused sanitizers,
  SHA256 cc7b42e007f42d7087babf61a2fff9305174da38cc6d75577ea49f719e6a66b3.
  This includes 3,276 valid wave cases, 2,016 invalid-rate failures, six exhaustive
  signed-PCM/long-name captures, unchanged inputs and existing overflow assertions.
  Across 5,292 allocation cases, 3,198 improve and 2,094 remain equal; none grow.
  Requested bytes improve in 3,250 and remain equal in 2,042. Aggregate savings
  are 8,216 allocations/20,805,616 requested bytes. A 16,384-value PCM wave with a
  one-character name improves from 19 allocations/131,404 bytes to 16/32,982.
  The initial reserve draft excluded INFO and regressed 104 small-wave cases;
  reject that draft and include the complete known wave payload in the reserve.
  Forty alignment/reserve boundaries and nine data-size edges retain the maximum
  even child size of 4,294,967,294. Huge physical allocations and native allocation
  failure ordering are not claimed equivalent.
- Motion preserves 1,166,688 defined cases/128,756,673 bytes under focused
  sanitizers: 1,048,576 native-duration cases, 102,128 all-mode/lifecycle cases,
  14,688 floating-point bit-level cases and 1,296 unsigned modular boundaries.
  A separate 2,433-case narrowed/large-duration matrix intentionally corrects
  336 output lines. Independently reproduce both old zero-divisor failures
  (u8/256 and descending s16/65536); the candidate finishes on the declared target
  tick cleanly. Complete model/VM fixtures pass; 28 configured syntax entries are
  Werror-clean. The numerator's existing overflow/custom-scalar contracts are
  outside this divisor fix; no broader arithmetic change is claimed.
- SF2 preserves 393,239 defined private conversions, eleven public valid-bank
  exports and 19 existing synth artifacts: 18,246,938 bytes in Release and focused
  ASan/UBSan/float-cast-overflow, SHA256
  bfc0b257d967b9db3a9c029d1be9249b292dfdba7058fd6e32cf459a4abef483.
  Extending the public fixture with both large sends produces matching candidate
  Release/sanitizer captures of 18,248,450 bytes, SHA256
  6406185af9044d75a1f3ad89f75fe90d413c14bf2d5302dc11778ce951781a8c.
  The original exporter fails UBSan running that same public fixture. Corrected
  undefined conversions are not included in old/new parity claims.
- Focused sanitizer probes rebuild changed writers/resolvers, probes and fixtures
  while linking matching unchanged Release archives. Allocation figures measure
  cumulative requests rather than peak RSS.

Reject deleting SourceMap's invalid-ID gates merely because its stored indexes
filter invalid IDs. The values match across malformed/joined/copied/removed graphs,
but these multi-part queries would replace immediate sentinel rejection with a
linear search. At 512 parts, invalid assetOwner lookup grows from about 1.80 ns to
1,196 ns and annotationsForAsset from 2.82 ns to 1,097 ns (median of three runs).
The six-line reduction removes no responsibility and degrades that established
fast path. Keep it. Also retain canonical prepared Region storage and nullable
SynthBankSelection pointers: flattening adds a second field projection, and the
selection vector requires assignment during filtering.

Next: remove the full PerformanceEvent variant from the private buffer that only
ever holds derived ModulationPerformanceEvent values; continue probing ownership
and state boundaries rather than introducing another general framework.

## Cleanup2 twenty-fifth batch: canonical channel identity and complete export inputs

Twenty-three production files remove 24 net lines, bringing cleanup2 to 1,014
removed. Tests remain at 519 net added lines. This pass removes redundant authoring
arguments and retained state; the export ownership changes add six lines.

- PerformanceEmitter exposes the source channel number already owned by its
  PerformanceTrack. Delete eleven numeric identity copies across AkaoSnes,
  ChunSnes, GraphResSnes, HeartBeatSnes, ItikitiSnes, KonamiArcade, MoriSnes,
  NinSnes (ordinary and Quest), RareSnes and SoftCreatSnes. Playback reads the
  canonical number through out.sourceTrackNumber(), rather than retaining it
  in mutable format state. Constructors become default/config-only when context
  did nothing else. Keep Chun's rounding setup; Akao's borrowed decoded track;
  Itikiti's derived hardware voiceBit; Mori SFX's script lookup; and all unrelated
  source-state setup. Namco's state-local selection, HOSA handles, Sony encoded
  identity and mutable/derived voice assignments remain distinct responsibilities.
  The new accessor adds no state or forwarding layer on SequencePlayback.
- Drop 73 repeated SignedDecimal arguments from signed CompilerCursor reads in
  ten format files. The existing reader defaults supply exactly that display
  and Value role. Naturally reflow the affected argument lists; do not introduce
  another reading convention. Preserve raw/annotation display settings and
  role-bearing signed reads. Separately use the existing role-only derived()
  overload for Itikiti's effective instrument program, removing one repeated
  Default argument while retaining InstrumentProgram attribution.
- TempoRelativeModulation's private derived buffer stores concrete
  ModulationPerformanceEvent values. Its sole producer always creates that
  type; the final move-iterator insertion constructs the durable PerformanceEvent
  variant as before. Remove the unnecessary temporary discriminator without
  changing public types, chronological ordering or recomputation.
- Export orchestration retains an optional complete SynthExportInput, matching
  its optional lowered MIDI value. Keep bank selection local to preparation;
  engagement now means the actual writer input exists, rather than consulting
  a moved-from optional vector's flag. MIDI/WAV-only and blocked synth paths
  avoid constructing a useless synth input/name. An empty successful selection
  remains engaged. Preserve rendering/error gates, selected-bank filtering and
  diagnostic order.
- Append collection WAV artifacts directly into the final Artifact vector.
  Delete the temporary artifact-vector owner and its bulk-move insertion.
  Record the per-request origin for sample filename numbering, preserving zero
  after earlier exports and on repeated WAV requests. Count the known samples
  across banks/pools and reserve once before appending. These two plain count
  loops avoid capacity regressions while keeping one output owner; no generic
  traversal or new public API is introduced. Retain the empty-sample artifact.
- Delete unused BoundCollection::baseName(). Whole repository and generated-build
  searches find no caller or member-pointer use. Its private name still transfers
  directly into PreparedCollection's live public baseName.

Validation:

- The full 165-step macOS Release build is warning-free; all 44 CTest suites pass.
  Freeze matching headers, tests, core/formats/fmt/Qt value archives and shell.
  The twenty-fifth shell preserves all 252 input/policy comparisons and 3,922
  artifact hashes, inventories, diagnostics and exit statuses against 24.
- Channel identity preserves ten complete format suites: 270 captured renders/
  2,152,184 bytes including complete neutral events, automations, source spans,
  diagnostics and three MIDI pitch policies. Another 2,016 program cases and
  four direct-emitter cases preserve 3,360 factory calls/32,010,924 bytes, SHA256
  bee61eaf93ba7b286054a1c3b01a15cbbcd08c1bf0d28b87c3a041c18f69f95c.
  Cover independent/shared streams, large native channel numbers, zero/small
  budgets, startup delays/polling/hooks, section state survival and copied/config-
  bound runtimes after the original settings die. at()/after()/automation/pitch
  outputs retain source identity; cross-track bindings reject in both versions.
  This also establishes Akao's bound tempo emitter has the same owner as out.
  Nineteen configured units are Werror-clean. Five state sizes shrink by eight
  bytes: Mori 144->136, Rare 288->280, KonamiArcade 344->336, SoftCreat 272->264
  and Akao 592->584. Six other states retain padding; Mori SFX remains 48.
- Signed defaults preserve nine complete format suites and exact source/performance/
  MIDI captures: 31,822 lines/1,962,320 bytes, SHA256
  963ab6f07303515debb50d253b274ffa9bd66576d8e374ea4304e91d410072d7.
  Cover 46 source maps, 1,341 annotations, 18,678 fields, 1,075 links, 285 renders
  and 855 MIDI policy encodings. Twenty before/after production syntax checks
  are Werror-clean. The Itikiti role-only overload is included in its refreshed
  paired fixture capture.
- Typed derived storage preserves 8,640 programs across first resolution,
  recomputation and edited-tempo resolution: 25,920 exact comparisons/
  134,901,240 bytes. Include all modulation targets/layers, missing/fixed/source
  clocks, nonfinite/extreme values, waveform tables, same-tick ordering, expired
  tracks and removal of old derived copies. Complete model/VM/modulation/MIDI-
  modulation fixtures pass against frozen 24. Temporary elements shrink from
  352 to 344 bytes. Across 384 allocation cases, allocations remain 30,496;
  requested bytes decrease from 84,744,064 to 83,961,472, with no growing case.
  All three configured resolver syntax entries are Werror-clean.
- Export ownership preserves 19,044 public calls/723,958 artifacts:
  299,894,512 bytes, SHA256
  06e78b9e5141a333ece55207affe1a30df3f8aa01e561804c4c1fd08d6a8e807.
  Release comparisons cover both isolated changes and their combination; focused
  sanitizer comparisons cover the baseline/final combination. Include complete
  Collection/Session fixtures and 18,240 repeated generated policy exports:
  normal/error/empty/exhaustion/multi-bank states, reordered/duplicate/invalid
  requests, selected banks, source/codec/rate/stereo failures, preparation warnings/
  exceptions and ordered attributed diagnostics. All three configured export
  syntax entries are Werror-clean.
- Of 404 allocation cases (up to 16,384 samples, 16 banks/pools and 4,096-character
  names), combined ownership improves 398 and preserves six; none grow in count
  or requested bytes. Aggregate savings are 3,270 allocations/152,652,960 requested
  bytes. The largest repeated-WAV case improves from 245,866 to 245,835 allocations
  and 46,807,483 to 37,370,427 requested bytes. The initial unreserved direct-WAV
  draft regressed requested bytes in 101 cases; a leading MIDI artifact before
  16,384 samples requested an extra 1,572,672 bytes. Reject that draft and reserve
  the full known group once. Native bad_alloc can occur earlier with reserve;
  huge physical allocations and allocation failure timing are not parity claims.
- Focused ASan/UBSan/float-cast-overflow runs rebuild changed units, probes and
  fixtures with matching unchanged Release archives. The channel lifecycle probe
  additionally rebuilds frozen VM/emitter units consistently to avoid mixed
  libc++ container-annotation false positives. Requested-byte sums are not peak RSS.

Reject reusing synthAmountFromSeconds() for SF2 envelope conversion just to remove
three lines. Keep its positive-infinity policy and lround calculation: the other
helper uses round. Of 377,384 cases in each floating-point rounding mode, ordinary
nearest rounding matches, but downward/upward/toward-zero differ in 816/881/3,415
boundary values. All 208 public envelope exports with default rounding match;
that narrower evidence does not justify changing the other defined results.
The prematurely applied source change was immediately reverted and is absent
from this commit.

Next: inspect the remaining copied channel identities, SourceStore's unused count
query, and the MIDI writer's aggregate-only message forwarding helper. Probe SF2
attenuation clipping before rounding while preserving its existing lround policy.


## Cleanup2 twenty-sixth batch: one ownership index and direct value transfer

Twelve production files remove 17 net lines; three test files add 14 net lines.
Cleanup2 now removes 1,031 net production lines, including 944 from architecture
code. Preserve SequenceVM and the format/export boundary.

Accepted changes:

- Remove three more copied logical channel numbers. CompileSnes never reads its
  stored trackNumber after construction. TriAcePS1 looks up its layout directly
  with the constructor context and reads startup identity from out. SculptSoftSnes
  reads logical ownership from out while retaining mutable DSP voice assignment,
  priority and stolen-voice state. No new channel facade or identity storage.
- Remove SourceStore::sourceCount(), whose only production caller was the reserve
  in sourceFiles(). Keep its exact active-entry count there, so removal history
  does not inflate returned capacity. Existing assertions inspect source metadata
  or the snapshot; remove one redundant count assertion next to the empty view.
- Delete MIDI's addMessage() aggregate-only forwarding helper and its unused
  utility include. The nine sites directly append {tick, priority, bytes}; retain
  real channel clipping, wire factories, stable sorting and target serialization.
- Accumulate pan depth/rate in the final SequenceModulationProfile. Remove a
  parallel LfoObservation pan object with unused delay, waveform and gain facts.
  Vibrato/tremolo retain their observation state because conflict resolution and
  conditional presence require it.
- Source-map ownership validation stores each asset's source and annotation
  presence together. Remove a second keyed assetsWithAnnotations set/reserve;
  retain separate identity validation and all ordered malformed-input diagnostics.
  The final at() is guarded by valid metadata range, which guarantees first-pass
  insertion even for invalid and duplicate IDs. This private change adds two
  lines while deleting a container and its per-ID ownership bookkeeping.
- Derive executor startup directly from the immutable program.sectionPlaylist.
  Remove the private startsActive parameter/default and its caller's temporary;
  the only call always supplied the same derived value. Actual section entry,
  pre-command delay, tick provenance and command-budget state remain distinct.
- Move completed analysis diagnostics into the caller's vector. The local
  performance is discarded immediately; do not allocate/copy its strings again.
  Preserve prefixes, order and complete attributes with ordinary move iterators.
  This ownership correction adds two lines, including the explicit iterator
  include; no generic append wrapper or rollback protocol is introduced.
- Clip SF2 attenuation in floating point before lround. Retain sum order, the two
  multiplications, target scale and lround policy. Huge finite values are valid
  synth inputs, but need not be rounded outside long's domain before saturation.
  Add a regression that saves/masks the floating-point environment, exports
  validated 1e20/DBL_MAX attenuation, checks max 1440 without FE_INVALID, and
  restores the full environment before asserting.

Validation:

- The full 345-step macOS Release build is warning-free; all 44 CTest suites pass.
  Freeze matching headers, tests, core/formats/fmt/Qt value archives and shell.
  The twenty-sixth shell preserves all 252 input/policy comparisons and 3,922
  artifact hashes, inventories, diagnostics and exit statuses against 25.
- Three complete channel-format suites plus generated TriAce and Sculpt lifecycle
  cases preserve 2,151 renders/7,776,966 bytes: complete IR, automations, source
  spans, diagnostics and three MIDI policies. Include raw u32 identity comparisons,
  late startup, zero/small budgets, copied runtimes, early ownership, DSP allocation,
  handoff, default/reset, priority and release tails. Compile state shrinks 344->336
  and TriAce 88->80 bytes; Sculpt base/derived sizes retain their padding. Thirteen
  configured affected-unit syntax checks are Werror-clean. Focused sanitizer runs
  rebuild matching VM/emitter and all relevant TriAce units consistently.
- Source lifetime/removal trace preserves 631,901 lines/39,740,633 bytes, SHA256
  759f1eea34ee2668b611e4faa9c332fc6cbb034c9d24b4cb8c691ebe185573b8.
  Cover 16,384 lifecycle steps, copy/move/assignment, parent and scoped aliases,
  active empty bytes, invalid/removed IDs and error text, retained readers/metadata
  after store destruction, and 28 capacity/history cases through 4,096 tombstones.
  Capacity, order and metadata remain exact. Complete Session/Cue fixtures pass;
  initial concurrent runs collided on an existing fixed fixture path, so rerun
  sequentially. Both configured Source.cpp syntax checks are Werror-clean.
- MIDI preserves 216,761 captures/236,608,547 bytes, SHA256
  463b5a29eb87be131503a963ec315e12b1125bd7e1b53084ca5426995d40d86d.
  Include 75,043 renderer captures, 141,718 raw-encoder captures, complete renderer/
  pitch/encoding fixtures, signed-16 pitch sweep, int32 boundaries, timebases,
  channel/note-duration combinations, track counts and meta/SysEx lengths.
  All 36 allocation traces remain exact; all three configured syntax checks pass.
- Pan ownership preserves 43,911 profiles/37,728,599 bytes, SHA256
  a4f7b987c97661bf6ccb5cd7a3015f68746c13c0178a71e1dcc38f4801466c2c.
  Capture complete profiles, controller amounts with/without supplied profiles,
  modulation usage and application to banks. Include all eight targets, ordering,
  conflicting/absent shapes, delays, signed zero, negative/nonfinite/large values,
  and complete modulation fixtures. All three configured syntax entries are clean.
- Ownership validation preserves 16,496 malformed/lifecycle cases: 687,778 lines/
  95,930,807 bytes, SHA256
  fa419a7700d1be14957b79928bb54605df824f4827a574268e413e56373511e3.
  Cover all asset types, invalid/zero/duplicate/reused IDs, invalid/foreign/missing/
  removed/out-of-bounds ranges, inherited/explicit/unknown owners, backward/shared/
  cyclic/missing parents, invalid annotations, links/fields and ordered diagnostics.
  Complete Session/Registry/SourceMap/SynthBuilder suites and both configured
  validation syntax checks pass. Of 144 allocation cases through 16,384 assets,
  128 improve count and 110 improve requested bytes; none grow. Aggregate savings
  are 31,786 allocations/2,782,640 requested bytes. Fully owned largest case
  improves 65,540->49,155 allocations and 2,097,152->1,703,936 bytes; no-annotation
  count improves by one with bytes equal.
- Executor construction preserves the broad 171,938-case VM matrix/206,284,725
  bytes across independent/shared streams, command calls/repeats, implicit/explicit
  loops, synchronized checkpoints, delay/polling hooks, startup budgets and
  playlists. Complete VM/Cursor fixtures, including prepass/analysis, pass.
  All three configured syntax checks pass for the combined final VM draft.
- Analysis transfer preserves 840 cases/3,226,575 bytes, SHA256
  d3d99fa9c2e8afc63de223465ce4b2cbc1d1a0139c8bcff5a3fb61730b4fbd64.
  Preserve prefixes, complete diagnostic attributes, final program state and
  finishPrepass counts. Separate O2 allocation traces improve 13,078->10,933
  allocations and 7,091,168->4,667,504 requested bytes; 588 cases improve, none
  worsen. Removing copies can change native bad_alloc staging; no new exception
  rollback guarantee is claimed.
- SF2 clipping preserves 691,644 private integer results and 405 public/fixture
  artifacts under all four rounding modes: result/artifact projection 27,005,050
  bytes, SHA256 da212bae608cefa358e91ffb3f24203bd44d9396b7008fe1f8740863a7f724da.
  Cover half-unit/nextafter boundaries, both contributions, cancellation, signed
  zero, subnormals and safe values through 1e15 dB. Saved configured assembly
  confirms add, multiply 10, multiply 2.5, then the same lround call.
- Treat attenuation exception flags separately from output parity. Of ordinary
  records, 118,854 lose only FE_INEXACT (nine retain FE_UNDERFLOW). Forty private
  and forty public huge-finite cases preserve 27,030,730 projected bytes, SHA256
  aa7752d3003a41a31375aa4ccc1a63fe49d7c8aa725fa00d45056c31b29a99e4. On this Darwin
  build old and new outputs both saturate correctly; old raises FE_INVALID in
  32 cases and new in none. Intermediate overflow/inexact from unchanged
  arithmetic remains. Do not claim a demonstrated Darwin byte-output fix.
  The saved-environment permanent regression fails its own assertion with old
  code and passes new Release/sanitizer code. The new 407-artifact/full numeric
  trace is 29,773,138 bytes, SHA256
  3c786f70c41f8380776c114f6dca09b66752ac39c37de2efaa0bac0077485e0f.
  Both actual MIDI/SF2 writers pass all six configured Werror checks.
- Focused ASan/UBSan/float-cast-overflow proof rebuilds changed units, probes and
  fixtures with matching unchanged Release archives; it is not whole-core
  instrumentation. Mixed libc++ container annotations are disabled where needed;
  channel/VM lifetime probes instead rebuild the matching involved units.
  Allocation sums are requested bytes, not peak RSS.

Read-only decisions: keep active-note/event history for analysis because playback
can consult it. Suppressing only silent-pass source spans would introduce another
pass-mode flag and bookkeeping path, without simplifying current responsibility.
Keep explicit song-wide motion owner numbers: a format can interrupt its output
binding while source motion continues on that track. No owns()/belongsTo() API.
The include audit still finds zero export headers under value formats; physical
modulation lowering stays exporter-side.

Next: probe merging validation's remaining batch-ID index with its source facts,
transferring channel buffers into decode-owned streams directly, and removing
source-role categories left behind by field projection when no producer uses them.


## Cleanup2 twenty-seventh batch: consolidate validation facts and initialize owned results

Five production files in `src/value` remove three net lines; the Qt presentation
file removes another 12. Cleanup2 removes 1,034 net value production lines,
including 947 architecture lines and 87 format lines. No new public abstraction
or permanent test machinery is needed for these ownership changes.

Accepted changes:

- Use the same private asset-source map for batch ID membership and source-map
  ownership validation. Delete the remaining second keyed batch-ID set. Identity
  diagnostics still run before ownership diagnostics; the first valid metadata
  range remains authoritative for duplicate IDs. Entries without a valid source
  remain distinguishable from absent IDs. This adds four lines while removing
  a container and its duplicate keys; invalid-ID annotation behavior is preserved.
- SonyPS2 SE decoding moves its collected voice vector into the already-created
  stream. Initializer-list assignment copied the channel buffer despite
  `std::move(voices)`. The track decoder always creates one stream, including
  malformed/empty command cases; absent voices return before this assignment.
  The previous outer one-stream vector reused capacity, so the demonstrated
  saving is the channel buffer copy, not an outer stream allocation.
- CompilerCursor initializes the durable qualified command kind directly from
  its category or derived label. Remove the intermediate owned category string;
  prefixes, labels, categories and completed command presentation still own
  the strings they need. Keep the expression wrapped for readability; this is
  line-count neutral.
- GBA DirectSound initializes its source PCM from the compressed decoder or an
  uncompressed zero-filled buffer. Delete the zero-filled buffer allocated and
  then discarded on the compressed path. Retain truncated-data zero padding,
  reverse handling, loop metadata, mixer-rate checks and resampling order.
- Delete SourceRole::Padding/Opcode/Operand, SourceValueDisplay::Ascii and
  SourceLinkRole::DerivedFrom. They have no producers or relationship consumers.
  Remove their remaining Qt display/color branches. Real opcode fields and the
  live field role remain. All other presentation categories have actual users.

Validation:

- The full 355-step macOS Release build is warning-free; all 44 CTest suites pass.
  Freeze matching headers/tests, core/formats/fmt/Qt value archives and shell.
  All 252 original input/policy comparisons preserve 3,922 artifact hashes,
  complete inventories, diagnostics and exit statuses against batch 26.
- Validation preserves the same 16,496 cases/687,778 trace lines/95,930,807 bytes
  from batch 26, SHA256
  fa419a7700d1be14957b79928bb54605df824f4827a574268e413e56373511e3.
  Full Session/Registry/SourceMap/SynthBuilder fixtures and both configured Werror
  checks pass. Of 144 allocation cases, 128 improve count and 92 improve bytes;
  none grow. Save 84,552 allocations/4,049,024 requested bytes in aggregate.
  Fully owned 16,384-asset case improves 49,155->32,770 allocations and
  1,703,936->1,179,648 bytes. Keep the separate ordered diagnostic passes.
- SonyPS2 preserves 480 generated parse cases, 388 performances and the complete
  format/Session fixtures: 4,811,627 bytes, SHA256
  a9ab19ce0944bea7b9bd9d3fb4e3fb48e32a1fac6a0ddac8977c23d682d202a2.
  Capture complete source graphs, commands, IR, automations, diagnostics and MIDI.
  Cover 0/1/2/8/17 voices, duplicates, truncated commands, source-map on/off,
  note families and budgets 0/1/3/100. Six configured format Werror checks pass.
  Eleven channel-vector sizes improve nine allocations/1,317,000 requested bytes
  to zero; all 11 preserve the original voices buffer instead of copying it.
- Cursor preserves 24,192 paired command cases and full fixtures: 10,448,906
  bytes. Overwrite the original prefix/label/category after construction to
  verify completed command ownership. All three configured fixture Werror checks
  pass. O2 allocation count improves 105,576->98,664 and requested bytes
  9,089,280->8,481,024; 6,912 cases improve and none grow.
- PCM preserves 68,663 decoded results/20,488,565 bytes, SHA256
  7678834c30c8eecd1769fcf9b44b799ddd72a15f84a04f7aea9a94137b97a8f4.
  Include complete ValueSample/MP2k fixtures and 67,200 generated GBA combinations
  of compression, 0..115 encoded bytes, offsets, truncation, loop starts/lengths,
  reverse/enabled state, sample/mixer rates and step sizes. Full PCM, rates,
  channels and loop metadata are exact. All three configured decoder Werror
  checks pass. Of 176 allocation cases through 65,536 source samples, 80 improve
  and none grow: save 80 allocations/1,385,536 requested bytes in aggregate.
- Presentation preserves 4,958 captures/1,322,338 bytes, SHA256
  5c4a8e91e4a21b65b23c6a12b12de548c5052c5776f5ede2dd1298663e91e474.
  Include 3,192 field displays, 1,728 role/semantic/field presentations and 38
  source-map/command captures, eight retained link roles with all three target
  alternatives, deduplication, ownership and lookup order. Rebuild matching enum,
  SourceMap, Qt presentation and utility units together; do not mix new ordinal
  headers with old enum-producing archives. Six configured Werror checks pass.
  Layouts stay 200/88/64 bytes for annotation/field/link and one byte per enum.
  Remaining enum ordinals change internally: DataBlock/Command 12/13->11/12,
  SoundBank..Sample 16..20->13..17, Enum 12->11 and Related 8->7. Repository-wide
  use/serialization/reflection/generated-source searches find no persisted numeric
  contract. The rebuilt local HexView cache still has 432 distinct style keys
  and identical displayed styles. Do not claim stable external enum ordinals.
- Focused ASan/UBSan/float-cast-overflow comparisons are clean. Decoder and fixture
  units use matching unchanged Release archives with libc++ container annotations
  disabled; Sony lifecycle probes rebuild involved VM/emitter units consistently.
  Presentation rebuilds its involved units without core/archive mixing; system
  Qt frameworks remain unsanitized. This is focused instrumentation, not a
  whole-program sanitizer claim. Allocation sums are not peak RSS; removing
  temporary allocations does not preserve native bad_alloc timing.

### Expanded archive regression coverage

Use the user's `/Volumes/emu/vgm` archive read-only. The broad inventory contains
24,935 files. Select 59 compact closures from the curated test suite, PSF2 and
SSF archives, plus unsupported DSF containers and three natural Tamsoft TSQ/TVB
pairs. Preserve library and companion paths; load each group together. Compare
complete inventories, then independently export first/middle/last collection IDs
(up to three) as MIDI, SF2 and DLS under default/events policies with `--loops 1`.
Four workers and a 90-second per-invocation bound keep the replay finite.

The original `core-rewrite` executable versus batch 26 preserves all 59 source/
asset/collection inventories, diagnostics and exit statuses. Of 750 independent
export pairs, 708 succeed and 42 fail in both; all 714 artifact payloads match
(191,631,854 bytes per binary). The only result-record differences are two NinSnes
Star Fox MIDI filenames: quoted `"Come in, Corneria!".mid` becomes
`_Come in, Corneria!_.mid`, as intended by title sanitation in commit `9ae12e699`.
The payloads remain identical. No unclassified differences or timeouts.

Batch 26 versus 27 then preserves all 59 inventories and all 750 export records
exactly, including complete stdout/stderr, filenames, statuses and all 714
artifact sizes/hashes. Combined with the original corpus, real inputs now cover
33 of 38 registered families; the new replay itself detects 32, adding 24 beyond
the original nine. Directory labels are not recognition evidence: FFT inputs
actually detect SuzukiPS1, and Megaman8 detects SonyPS1. Remaining successful
real-input gaps are Akao, AsciiShuichiSnes, KonamiTMNT2, TriAcePS1 and WolfTeamSnes.

Keep matching pre-existing failures visible: six MP2k invalid sample-reference
exports; 24 standalone Tamsoft missing-bank SF2/DLS exports; six SegSat library-
only collection exports without a retained bank; and six selected Tamsoft paired
exports without decodable samples. BGM_02.TSQ+BGM.TVB succeeds in all six exports.
Five DSF version-0x12 inputs remain unsupported by both original and cleanup
container extraction. One PSF2 input publishes banks without collections. These
cases are capability boundaries, not cleanup regressions or successful coverage.

Proof sources, manifests, raw records and summaries remain under
`/tmp/vgmtrans-cleanup2/volume-coverage27`. Preserve full names/sizes/SHA256 and
raw diagnostics; prune equal successful artifact bytes after comparison while
retaining failed/mismatched bytes. Earlier corpus generations had accumulated
35.3 GB of identical artifacts. Verify each duplicate against its recorded
hash before deleting it; retain all records, manifests and frozen executables.

Next: inspect direct PSF2 decompression into its final member buffer, remaining
fixed-channel stream assignments, RareSnes' one-vector recipe wrapper/result copy,
and the stitcher's single-use optional channel visitor. Preserve named ID types
only where they fit the actual comparison contract; changing a private ordered
variant key to equality-only AssetId would require extra comparison machinery.


## Cleanup2 twenty-eighth batch: remove intermediate wrappers and buffers

Nine value production files remove 11 net lines. Cleanup2 now removes 1,045 net
value production lines: 954 architecture lines and 91 format lines. The existing
RareSnes fixture changes three field accesses; no new permanent test machinery
is needed. The cumulative production diff is 1,708 added/2,753 removed lines
across 142 value C++ source/header paths, including renamed paths.

Accepted changes:

- Remove RareSnes' SequenceRecipes wrapper around a single patch vector. The
  collected program state, sequence result and synth reader use that vector
  directly. Move the completed vector into the returned sequence result after
  analysis finishes; keep analysis before moving the program. Patch identity,
  discovery order and per-track source interpretation remain format-local.
- Initialize SonyPS1, SonyPS2 MIDI and SegSat's channel lists in the stream already
  created by track decoding. Delete outer one-stream initializer-list assignment,
  which copied each 16-channel buffer. Empty and truncated tracks still have the
  required stream. Keep SonyPS2 SE's distinct collected-voice behavior.
- Decompress each PSF2 block into its final member buffer. The buffer already
  reserves the declared file size. Resize only the current block, then pass its
  offset and expected size to uncompress; keep the same exact-size/status checks
  and failure messages. Delete the temporary decoded block and append copy.
  Partial output remains private and is discarded when extraction throws.
- Let the stitcher's existing variant visitor collect channels directly into
  its ordered set. Delete the single-use optional-returning channel helper and
  its second conditional. Meta/SysEx events still contribute no channel; actual
  channel-bearing event alternatives keep their existing attribution.

Validation:

- The full 59-step Release build is warning-free; all 44 CTest suites pass.
  Freeze the complete matching value headers/sources, tests, core/formats/fmt/Qt
  archives and shell as pass28. All 252 original input/policy comparisons preserve
  all 3,922 artifact hashes, inventories, diagnostics and exit statuses against
  batch 27.
- RareSnes' complete fixtures and 480 generated lifecycle cases preserve 501
  performance captures/58,665,892 bytes, SHA256
  d143713eedfd63934102cc5c6030421688dd03f839876f6afc0c92e1def0bfbe.
  Compare complete patch fields/source ranges, IR, automations, diagnostics,
  source annotations, three MIDI policies, instrument regions and decoded BRR
  PCM. Cover melodic/percussion profiles, stereo signs, track/patch counts,
  direct/table SRCNs and truncated tails. Four paired format units and all three
  final affected C++ units pass configured Release Werror checks. Actual decode
  allocations improve 394,792->394,528 and requested bytes
  304,268,032->304,108,032: each of 264 nonempty results avoids one 40-byte-per-
  patch buffer copy. Empty results do not regress; object sizes do not change.
- Fixed-channel decoding preserves all three full format suites and 576 generated
  cases: 527 performance renders/8,490,833 bytes. Capture complete source graphs,
  command/IR/automation/diagnostic data and MIDI, including empty/truncated input,
  native channel routing and startup before the first delay. Sixteen matching
  format units per side pass configured Release Werror checks. The isolated
  assignment improves two allocations/128 requested bytes to one/64, retaining
  outer stream storage and the identical ordered 16-channel list.
- PSF2 extraction preserves 2,468 captures/2,961,628 bytes, SHA256
  3452ba5125ea6c97da39de0e5d901cce6a665730248c6e0f7645536bccb2ffc8.
  Compare 2,464 generated filesystem/public extraction cases and four real PSF2
  inputs from Sony, Square, Onimusha and Growlanser, with complete member paths,
  bytes, metadata, segments and failures. Cover zero/truncated/oversized directory
  and block tables, invalid offsets, corrupt compression and short/long declared
  lengths; run the full SquarePS2 fixture. Three configured production Werror
  checks pass. Of 176 allocation cases through 32,768 output bytes and varied
  block sizes, 160 improve and 16 remain equal; none grow. Save 134,742 allocation
  requests/617,504 requested bytes in aggregate.
- Stitching preserves 68,003 captures/3,866,597 bytes, SHA256
  0809dfd1d3d34b9f919a42eec15d97e6bdf44ef56fcfe7feba272da4c11d8bcc.
  Include 67,329 channel-set cases and 674 complete public stitches: all five
  MIDI alternatives, all 256 channel values, all 65,536 channel pairs, duplicate/
  reversed/random events, complete fixtures and generated retiming/initial-state
  cases. Preserve reserved channel 10 handling, ports, bank selection, modulation
  policies, used-only selection and early failure paths. All three configured
  production Werror checks pass.
- Focused paired ASan/UBSan/float-cast-overflow probes pass. Changed units and full
  fixtures use matching frozen headers and unchanged Release archives; matching
  VM/emitter units are rebuilt for sequence lifecycle probes. Mixed libc++
  container annotations are disabled where needed. This is focused instrumentation,
  not a whole-program sanitizer claim. Requested allocation totals are not peak
  RSS. Removing copies can change native bad_alloc staging; no rollback promise
  is added.

### Extend real-input coverage and classify scanner scheduling variability

Add 14 compact closures from the supplied archive: six Akao FF7/8/9 PSF groups,
three TriAcePS1 Star Ocean 2/Valkyrie groups, two AsciiShuichiSnes archives and
three WolfTeamSnes archives. Actual detected asset records confirm four previously
missing families. Original core-rewrite versus batch 27 preserves all 14 raw
inventories, 144 independent export records and 144 artifacts/26,608,695 bytes
per binary. All succeed. Combined with the previous corpus, real inputs cover
37 of 38 registered families. The volume has no relevant KonamiTMNT2 arcade
archive; its synthetic fixture coverage remains, without a real-input claim.

Replay all 73 expanded closures against the fully integrated pass28 binary.
All 894 independent export records match pass27 exactly: complete normalized
stdout/stderr, filenames, statuses, sizes and SHA256. There are 852 successes
and the same 42 previously documented failures, producing 858 exact artifacts/
218,240,549 bytes per binary. No timeout occurs. Together with the original
corpus, all 4,780 exported artifact payloads remain exact.

Seventy-two complete raw inventories match. Growlanser II Hoot Hoot.psf2 swaps
only the AssetIds allocated by concurrent SonyPS1/SonyPS2 sample-pool scans;
the collection member still identifies the correct SonyPS2 pool and all six
exports match. Keep this raw difference. Sixteen repeated invocations of each
unchanged executable reproduce both orders: original core-rewrite 15/1, pass27
15/1 and pass28 14/2. All 48 inventories match after renumbering only declarations
and their collection member references using unchanged ordered asset metadata.
Source/collection IDs, labels, ranges, diagnostics, status and every other byte
remain exact. This is demonstrated pre-existing scheduling variability, not
raw inventory equality or a newly introduced regression.

Evidence is retained under `/tmp/vgmtrans-cleanup2/volume-coverage27`, including
`gaps`, `references/pass28.json`, `pass27-vs-pass28-summary.json` and the
`volume-36/classification.json` and `scheduling-repeats` records. The expanded
replay retains failures and raw variability rather than normalizing unproven IDs.
Earlier verified duplicate-artifact pruning removed 86,282 files/35,294,939,704
bytes while retaining every recorded hash and frozen executable; correct the
prior batch's rough storage estimate to 35.3 GB.

Read-only decisions: keep the private synth owner key's existing u32 alternative.
Replacing it with equality-only AssetId would require ordering machinery for its
ordered variant map and add a new contract for two value-access deletions. Keep
DLS and SF2's distinct loop policies and completed-object ownership. A smaller
private helper is useful only when it represents an actual shared policy.

Next: analyze whether serial format scanning removes scheduling overhead and
nondeterministic IDs without sacrificing throughput; measure before changing the
explicit concurrency test. Probe transferring owned analyzed member results
without changing generic projector dispatch, borrowing immutable Akao articulation
records only within preparation, and eliminating the redundant SF2 loop wrapper.


## Cleanup2 twenty-ninth batch: consume owned analysis results and borrow preparation inputs

Sixteen production files remove three net lines. Cleanup2 now removes 1,048 net
value production lines: 954 architecture lines and 94 format lines. The cumulative
value C++ diff is 1,756 added/2,804 removed lines across 151 source/header paths.
Two existing core fixtures add 24 lines for actual ownership and saturation
contracts; Akao/Suzuki fixtures adjust their existing inputs/field paths without
adding lines. Standard containers and reference_wrapper replace redundant owners;
no new public type, callback phase, state language or export target framework.

Accepted changes:

- analyzeCompiledProgram transfers a directly selected data member from its
  owned completed state into the durable result. Delete the copy of collected
  Mori/Nin/Rare synth-analysis containers. Generic callable/member-function
  projectors still receive const ProgramState&, and references they return still
  become owned copies. Wrapped member pointers retain that generic path. The
  return type and erased-state/runtime protocol stay unchanged. Four extra lines
  make the ownership distinction explicit rather than silently changing all
  projector overload selection to rvalue invocation.
- Akao's preparation lookup borrows immutable articulation records from its
  selected retained sample pools. Use reference_wrapper<const AkaoArticulation>
  so mapped entries remain non-null; keep ordered lookup and last-valid-definition
  overwrite behavior. Delete copies of complete source-field owners. Regions
  copy their source ranges, sample references, tuning, loops and envelopes;
  runtime ADSR records own their values. No lookup reference escapes preparation.
- Remove SuzukiSnes' one-vector SequenceRecipes and ItikitiSnes' one-set
  ReferencedPrograms types. Sequence results expose drums and referencedPrograms
  directly, and synth readers accept the corresponding const vector/set. Keep
  Suzuki's 128-row limit and validation, and Itikiti's implicit program 0,
  group-adjusted program discovery and nullable standalone command collector.
- Construct TrackProgram's default one stream with the explicit vector count
  constructor. Initializer-list construction copied the stream's default channel
  vector. Normal track decoding uses that default and sets its channel number
  directly, avoiding another explicit initializer-list owner. The normal path
  and default construction each avoid one allocation without changing layout or
  adding lines. The exceptional empty-start-list branch keeps its explicit
  initialization; this is not a claim that every stream copy is eliminated.
- DLS envelope conversion clips double timecents to the s32 interval before the
  same lround call. Preserve log2, multiply-by-1200 and multiply-by-65536 order,
  absent/nonpositive/nonfinite sentinels and positive-infinity saturation. This
  makes the lround argument representable on 32-bit-long platforms; the previous
  clipping happened after conversion. Add a public valid-bank fixture covering
  huge attack and positive subnormal release. On Darwin the fixture passes old
  code too; it establishes a portable contract, not a demonstrated local byte fix.
- Delete the private SF2 loop wrapper around region.loop.value_or(sample.loop).
  State the fallback directly at its two call sites. Keep DLS's separate enabled
  zero-length-loop normalization; it represents a real target policy.

Validation:

- The full 341-step Release build is warning-free; all 44 CTest suites pass.
  Freeze complete matching sources/headers/tests, core/formats/fmt/Qt archives and
  shell as pass29. All 252 original corpus comparisons preserve 3,922 artifacts
  and complete inventories/diagnostics/statuses against pass28. All 894 expanded
  export records preserve 858 artifacts/218,240,549 bytes per binary, including
  exact names/stdout/stderr/statuses. There are 852 successes and the same 42
  failures, with no timeout. All 4,780 artifact payloads remain exact.
- Seventy-two expanded raw inventories match; only Growlanser's already-proven
  concurrent sample-pool AssetId ordering differs. Keep the raw mismatch. All 50
  initial/repeated inventories match after renumbering only asset declarations
  and their collection member IDs by unchanged ordered metadata. Fresh 16 runs
  per binary observe original orders 15/1, pass28 15/1 and pass29 16 in one of the
  already-observed original orders. No claim that pass29 itself reproduced both
  orders. Source/collection IDs, ranges, labels, warnings and statuses stay exact;
  link the earlier original/pass27/pass28 scheduling evidence. Classification is
  retained in volume-36/classification.json beneath pass28-vs-pass29 comparisons.
- Analysis preserves 176 native performance renders and 162 complete collected
  results/1,622,179 bytes across full Mori/Nin/Rare suites, including both Nin
  Quest collectors. Rebuild four actual collector units with configured Release
  Werror and matching sanitizer VM/emitter. Native return-only allocations fall
  from 164/7,512 requested bytes to zero: 91 calls improve and 71 are already
  empty; none worsen. This is the selected-member transfer only, not whole-decode
  allocation accounting. All real member result types are nothrow movable.
- The actual-VM projector matrix preserves 171 successes/5,878,523 bytes, SHA256
  97b8711d3593abf0129acd8ff4a7180d8a69f30e60cc49d4a603d1ea9e881550.
  Cover scalar/pointer/reference_wrapper/const members, wrapped selectors,
  member functions, external references and overloaded callables. Observe owned
  results after state destruction. Full Cursor fixtures and the new external-
  reference regression pass before/after with sanitizers: preserve the external
  vector, mutate/destroy it, then verify the result remains owned. Eight custom
  copy/move combinations plus projector/factory/bad-state failures bound the
  contract: direct member copy/move exception and moved-from destructor staging
  can change. No rollback or borrowed-pointer lifetime repair is promised.
- Akao's six full fixtures and 7,680 generated preparation/lifetime cases preserve
  10,327,716 bytes, SHA256
  0578857ac73ded1f70f01d83bd758170d0af269545d7cc9efa25f249be85d0da.
  Compare complete regions/sample/loop/envelope/source values, ordered diagnostics,
  prepared ADSR copies, native EnvelopePerformanceEvent values/fields/scope/
  attribution/order and MIDI. Destroy original snapshot/sample-pool owners and
  reuse heap storage before observing results; destroy prepared bank/private data
  before executing the copied native sequence runtime. Six Akao production units
  and the full fixture pass configured Release Werror. The changed resolver,
  instrument and native sequence units plus probes are sanitizer-instrumented.
- Akao preparation allocation requests improve 779,712->265,632 and requested
  bytes 121,966,848->43,826,688. Of 7,680 cases, 1,344 improve count and 2,688 improve
  bytes; none grow. The mapped payload shrinks 96->8 bytes and map pair 104->16,
  while the map owner stays 24 bytes. These are scoped operator-new requests and
  object layouts, not peak RSS. Arbitrary slow ADSR combinations stopped in the
  existing native envelope loop are excluded; no exhaustive register claim.
- Direct-format wrappers preserve both complete suites and 310 generated cases:
  320 rendered performances/33,307,183 bytes, with raw commands, source graphs,
  diagnostics, regions/envelopes and BRR PCM. Suzuki covers three revisions,
  0/1/16/128/129 rows, missing tracks, invalid key/volume and 30 shortened sources;
  28 throw the same bounded-reader exception and two partially decode. Itikiti
  covers five groups, 0/1/16/256 program changes, no/two tracks, static alternative
  discovery and program 0. All eight format units per side pass configured Release
  Werror and sanitizer comparison. Result layouts stay 496/496 bytes; all 310
  allocation traces stay 149,853 requests/33,451,048 bytes. No copy-saving claim
  for removing those nominal container wrappers.
- Default/normal stream construction preserves full VM/Cursor fixtures and 800
  generated tracks plus default/copy ownership: 103,430 trace bytes, SHA256
  b024d313e22eb2e04f9a1c69e16785b92d72c76fd36dae5db9a49d04bd899a9d.
  Cover track indices through u32 max, tracked/trackless source maps, empty and
  varied source lengths, and budgets 0/1/3/100. All 801 measured constructions
  save one allocation/four requested bytes; default is three requests/32 bytes
  before and two/28 after. Six actual configured source/command-projector Werror
  checks pass. Rebuild matching projector, VM, emitter, timing and full fixtures
  together for the clean ASan/UBSan/float-cast-overflow comparison.
- DLS numeric results preserve 394,324 private cases under all four rounding modes:
  5,126,228 bytes, SHA256
  0c4dae129ce4bede57b205253350e4b408676b7e754940a043dc2dba708f044e.
  Complete fixture/generated artifacts including the new assertion preserve 280
  captures/14,480,418 bytes, SHA256
  a5ac5aa4812f9c4c72aeaa5184ba3f9513c58ea1e5eab910c8eefd5f3ebf7ea9.
  Before/after floating flags also match within each configuration. Twelve
  pre-existing quiet-NaN classification FE_INVALID differences separate Release
  from sanitizer flags; keep that projection separate from results. Old finite
  positive arguments span -84,462,796,800..80,530,636,800, with 98,376 outside the
  s32 interval; all fit Darwin's 64-bit long. New arguments fit either 32-bit or
  64-bit long. Do not claim an actual Windows run or local byte-output correction.
- SF2 loop inlining preserves the full synth fixture and 336 targeted configurations
  across SF2/DLS: 695 captures/14,813,045 bytes, SHA256
  a0b29790ecf2f11ef0e7d98de5749c1768b0f6f264aae728c176e58f38e31a93.
  Cover absent/enabled/disabled overrides, zero-length/full/short loops, shared/
  distinct samples, filtering and unreferenced headers. Independent assertions
  verify first-region SF2 header precedence and DLS's distinct enabled zero-length
  extension. Nine fresh configured Debug/Release/RelWithDebInfo Werror checks
  pass across both final writers and the modified synth fixture.
- Focused sanitizer proofs use matching changed units and unchanged frozen Release
  archives; disable mixed libc++ container annotations where necessary. This is
  not whole-program instrumentation. Native allocation-failure timing can change
  when temporary owners disappear; no broad exception-equivalence claim.

### Rejected larger scheduling simplification

Prototype unconditional serial format scanning in isolation, removing 29 lines
of worker-count, atomic task queue, future ownership and thread includes. Keep
all original per-format exception attribution and registry-order admission. The
production source and explicit concurrency fixture remain unchanged.

Measure 73 real-input closures five times each with paired frozen executables,
seeded shuffled candidate/side order and one shell process at a time: 365 pairs/
730 invocations. Sum of per-input median scan times grows 12.490->19.558 seconds
(+56.6%). The 64 MiB NDS input grows 1.464->6.318 seconds (4.32x), and all six Akao
closures grow 2.3..2.6x. Some small/native archive inputs improve, but the large
regressions rule out this deletion. These are observed end-to-end scan timings,
including loading/extraction/inventory output, on this machine; independent agent
probes may run concurrently. No cross-machine performance claim.

The rejected prototype also changes raw asset-ID allocation order in 27 paired
inventories across six multi-format cases. Preserve all raw records; no exported-
payload equivalence claim is made for this rejected draft. Do not add ID-rebasing
namespaces or a scan-mode flag just to replace the current bounded worker model.
Evidence stays under `/tmp/vgmtrans-cleanup2/serial-scan-prototype29`.

Read-only decisions: SharedSequence's chunk owners preserve immutable revision
storage without copying every prior asset/annotation or adding one pointer per
item. DependencySelection is one request's answer; ResolvedInputs accumulates
several requests and retains earlier ambiguity/failure. A consuming protocol only
to save their copies would add API overhead. SequencePreparer's optional replacement
separates no replacement from an explicitly invalid/incompatible runtime; mutable
context-only callbacks would require new change tracking. Source instrument
address preferences remain distinct from unique assigned export addresses;
allocation scratch stays unpublished on failure. Preserve those actual roles.

A live numeric-policy issue remains separate: MP2k deliberately supplies minimum
LFO rate 0 Hz, while synthAmountFromHertz currently passes log2(0) to lround.
Measured Darwin returns 0 with FE_DIVBYZERO|FE_INVALID in every rounding mode;
a positive subnormal can also underflow before log2. A finite frequency floor or
native-LFO disable policy would change output meaning and shared legacy consumers.
No such policy change is included here; evidence stays in
`/tmp/vgmtrans-hertz-zero-inventory`.

Next: separate Akao's prepared runtime from its scanned instrument binding recipe,
and probe direct typed emitter appends without enlarging every variant vector
growth path. Keep the already demonstrated concurrency/ownership boundaries.


## Cleanup2 thirtieth batch: retain prepared meaning and stamp typed events

This batch removes 12 net production lines across nine files, bringing cleanup2
to 1,060 removed: 958 architecture lines and 102 format lines. The physical diff
is 14 additions/26 deletions. Cumulatively, production C++/headers are 1,769
additions/2,829 deletions across 154 paths; architecture is 1,453/2,411 across 88,
and formats are 316/418 across 66. The Hudson corpus fixture moves its sole query
into the test, adding two net test lines; cumulative tests gain 559 lines. Keep
SequenceVM, target-independent source events, and all current format families.

- AkaoSoundBankData retains only its scanned version and instrument binding
  recipe. Bank preparation stores the already existing AkaoRuntimeConfig directly;
  sequence preparation reads that prepared config. The completed bank no longer
  copies and retains an unused scan recipe alongside its owned ADSR map. Source
  snapshot data stays immutable, selected articulation references stay temporary,
  and the runtime still owns its copied native defaults. Remove three lines.
  Correct the common private-data comment to cover prepared as well as scanned
  assets; no common ownership implementation changes.
- PerformanceEmitter's private append receives the concrete event, stamps its
  header directly, and push_back performs the existing conversion into the event
  variant. Remove the explicit visitor/erased-input step while retaining the
  shared vector growth path. All 23 private calls pass concrete rvalues, and all
  22 native alternatives have nothrow move constructors. This adds two lines;
  public payload ownership, the neutral variant and event representation stay
  unchanged. Do not claim fewer payload moves or heap requests.
- Remove sequenceUsesSemantic from the production declaration and implementation.
  Its only caller was the optional Hudson native corpus fixture, which now applies
  any_of to tracks and keeps the live trackUsesSemantic query used by NDS. Remove
  six production lines. The unused sequence-wide function symbol/API disappears;
  no repository production caller relies on it.
- Sequence range accumulation delegates invalid-range rejection to SourceRange's
  existing include policy. Retain source equality before inclusion: an invalid
  base must not adopt a command from a foreign source. This expression change is
  LOC-neutral; anchors, unsigned endpoint behavior and reader attribution stay.
- Format registration has one options declaration with a default empty argument.
  Remove the empty-options forwarding overload and four-line body, saving five
  lines. Existing omitted/explicit-options calls and the two-argument function
  type/symbol remain. The unused one-argument function-pointer overload and binary
  symbol disappear. Repository/generated/Qt adapter callers use direct calls;
  this is not a claim of compatibility with an external binary SDK.

Validation:

- The complete 363-step macOS Release build, including Qt/shell/parity, finishes
  without warnings. All 44 CTest suites pass. Freeze all value headers/sources,
  tests, core/formats/fmt/Qt archives and the shell before independent replays.
  The shell SHA256 is
  6a8abc1e13c5a646c0862f8e765b5920264f81580c525fb0ec6af149ef474ed4.
- Original corpus comparison against frozen 29 preserves 252 input/policy records
  and 3,922 artifacts exactly. Expanded `/Volumes/emu/vgm` replay preserves all
  73 raw inventories, 894 export records, 858 artifact names/sizes/hashes and
  218,240,549 bytes per binary. All 852 successful exports and 42 pre-existing
  failures match; the five unsupported DSF inventory failures also match.
  No inventory normalization or repeat is necessary in this pair. All previous
  concurrent-ID variability evidence stays intact. Combined inputs cover 37/38
  registered families; KonamiTMNT2 still has synthetic coverage only.
- Akao compares 8,640 generated/lifetime/multi-bank cases: 17,186,372 exact bytes,
  SHA256 7a11cbd3bc18b1d8ccd314dc67a20d9158389c9da110c7149bd2d68303237ccf.
  Cover 0/1/2/3/8 banks, reverse order, overlapping IDs/last-bank ADSR precedence,
  different versions/recipes and destruction of pool/snapshot/prepared bank data
  before native event/MIDI observations. Another 72 invalid/type/kind/duplicate/
  missing-pool cases preserve error precedence, order and full attribution:
  10,284 bytes, SHA256
  abf7686a57c0e494899d452f54eb90a3c17854d4e7b5aba8aa275818ecd3757f.
  All six Akao units and six complete fixtures rebuild independently on both
  sides and pass focused sanitizers; seven configured Release Werror checks pass.
- Akao bank preparation's scoped allocation matrix covers 10,368 preparations:
  391,200->358,944 requests and 75,345,280->73,907,584 requested bytes. All 4,608
  improvements save both requests and bytes; no measured case grows. Scanned
  bank private payload is 88->64 bytes and prepared payload is 88->24. This is
  neither peak-RSS nor allocator-failure timing equivalence.
- Typed append preserves 3,762 captures, including 528 allocation-failure patterns
  and 66 nonfinite cases: 6,009,794 bytes, SHA256
  b937f108879e8a7a5af0922b304d6c8b4def694ec621122357e28fc716a708d5.
  The 90 actual bad_alloc results retain partial events, counters and automation
  end times. Both sides allocate 2,244 requests/2,313,168 bytes. Six full core
  fixtures preserve 67 captured renders/82,438 bytes, SHA256
  b382d6bb2309509e5c43f18d0858290a48d9e0da14c36044627455fc86613430.
  Rebuild matching VM/emitter/timing for Release and clean focused sanitizer runs;
  the changed unit is configured Release-Werror-clean.
- Typed append's 96 shuffled public benchmark samples emit 25,165,824 events in
  bounded 8,192-event blocks. Ordinary, payload and all-22-type mixed patterns
  cover reserved and growing vectors. Complete captured first-block outputs match.
  Median paired ratios range .957..998; distributions overlap 1, so no robust
  speedup claim. No material throughput regression is observed in these cases.
- The accepted push_back emitter object grows 31,475->34,465 text bytes (+2,990,
  9.5%); sections grow 49,216->52,728 (+7.1%). The linked standalone benchmark
  text grows 72,893->75,873 (+2,980). Accept that small code-size cost for removing
  explicit variant visitation. Reject direct typed emplace_back: it instantiates
  every native vector growth path, growing object text to 48,337 (+53.6%). The
  standalone executable's smaller LINKEDIT is not a whole-app saving.
- Sequence range/query changes preserve complete SequenceModel/Hudson fixtures,
  with the actual optional Super Bomberman 2 corpus enabled. Another 11,520 cases
  cover valid/invalid reader/base/command IDs, foreign first commands, zero-size
  anchors and unsigned endpoint extremes: 244,896 bytes, SHA256
  40faa9c58bebe41a93202e54d62f3d93c9a7916e273507affde0c10afcc0a0a6.
  Release and focused sanitizer traces match. Six configured Werror checks pass
  across Debug/Release/RelWithDebInfo production and corpus fixture sources.
- Registration preserves 79 complete ordered registry/error-state captures:
  130,060 bytes, SHA256
  c24e93dcf95933d4b89f1dba04dce68cb30d4e954d8b3e572ec25152af1f64a0.
  Cover omitted/lvalue/temporary/two-argument-pointer options, strict valid/
  missing/malformed/directory databases, all default search locations, skipped
  directories, invalid-first no-fallback, cwd failure, duplicate/sealed registries
  and partial state. Full existing CUE/KonamiArcade fixtures pass all four runs;
  54 configured Werror checks cover 18 implementation/Qt/shell/parity/fixture
  caller units across three configurations.
- Focused sanitizers instrument matching changed units/probes/full fixtures;
  other frozen archives remain Release, with libc++ container annotations disabled
  consistently where needed. Do not claim whole-program instrumentation.

Intentional format-private phase tightening: a manually unprepared Akao bank with
AkaoSoundBankData and no bank callback previously supplied empty defaults. It now
fails with one ordinary missing-retained-data sequence error at source 77,
offset 20, size 48. AkaoModule always installs the required typed bank preparer,
and binding stops before sequence preparation after any bank error. No repository
consumer depends on bypassing that phase; no second accepted retained-data type
or fallback protocol is introduced.

Evidence: `sequence-query-policy30`, `typed-event-assembly30`,
`akao-prepared-runtime29`, `pass30-baseline`, `comparison-pass30.json` and
`volume-coverage27/pass29-vs-pass30-{summary.json,report.md,classification.json}`
under `/tmp/vgmtrans-cleanup2`; registration evidence stays in
`/tmp/vgmtrans-registration-default30`.

Read-only findings: retain physical modulation profiles versus normalized export
usage, and immutable snapshots versus mutable session bookkeeping. Retain
AssetPrivateData.empty(): ScanResultBuilder::setPrivateData uses it to reject a
second payload even though typed preparation reads data through get<T>().

Next: probe the duplicate standalone MIDI lookup/helper boundary, inactive motion
target writes, and MIDI rendering's separately owned pointer timeline. Measure
full-event sorting cost before accepting that larger owner deletion.


## Cleanup2 thirty-first batch: consolidate lookup and remove inactive writes

This batch removes 16 net production lines across three files, bringing cleanup2
to 1,076 removed: 974 architecture lines and 102 format lines. The physical diff
is 136 additions/152 deletions, primarily relocating an unchanged helper body.
Cumulatively, production C++/headers are 1,876 additions/2,952 deletions across
154 paths; architecture is 1,560/2,534 across 88, and formats are 316/418 across
66. No fixture changes; cumulative tests still gain 559 lines.

- exportSequenceMidi performs its one asset lookup and type check before choosing
  standalone or collection rendering. Remove the sole private standalone helper,
  which looked up the same asset again, saving six lines. Keep missing/wrong-type
  diagnostics, collection selection and ambiguity, standalone simulation and
  owned Artifact results. Move the unchanged exportCollectionImpl definition into
  the existing private helper section, remove its forward declaration and the
  reopened anonymous namespace, saving another eight lines. Its function body
  is byte-identical; private dependencies precede it and public dependencies are
  already declared in the headers. Public export entry points are now contiguous.
- SequenceLinearMotion initializes and clears only active motion state. Every
  begin assigns target before ticking; an inactive target is never observed.
  Remove the constructor target initializer and clear's target write, saving one
  line. Keep current/step/delay/tick state, public motion policies, fixed-point
  wrappers and interruption behavior. No object-size or heap saving claim;
  private inactive target bytes may differ. Native consumers use s32/double;
  exotic user-defined ValueType copy/destructor side effects are not promised.
- SourceStore::bytes directly returns the span over entry(id).bytes, removing its
  single-use alias and one line. Preserve the same checked lookup, owner, bounds
  and borrowed lifetime; no allocation or API change.

Validation:

- The complete 161-step macOS Release build finishes without warnings; all 44
  CTest suites pass. Freeze matching value sources/headers, fixtures, four
  archives and shell before replays. Shell SHA256 is
  f2153b76bb4f099f9abb7c379bf6f15d9ee0e1ee02161832312b9e7e36b25e86.
- Original corpus against frozen 30 preserves all 252 input/policy records and
  3,922 artifacts exactly. Expanded archive replay preserves all 894 export
  records, 858 artifact names/sizes/hashes and 218,240,549 bytes per binary.
  All 852 successful exports, 42 existing export failures and five unsupported
  DSF inventory failures retain status and diagnostics. Combined real inputs
  still cover 37/38 registered families; KonamiTMNT2 has synthetic coverage only.
- Expanded raw inventories match in 72 cases. Onimusha 2/053.psf2 swaps SonyPS1
  and SonyPS2 sample-pool IDs 0/1, with the SonyPS2 collection member following
  its correct pool; source IDs, bank 2, sequence 3, collection 0, ranges, names,
  ordered warnings and every other byte remain unchanged. Fresh 16-run order
  counts are original 13/3, batch 30 11/5 and batch 31 8/8. All 50 initial and
  repeated inventories agree after substituting only asset-declaration IDs and
  corresponding collection-member IDs by unchanged ordered asset metadata.
  Retain the raw diff and every repetition. This repeats previously established
  concurrent scanner scheduling on a different input; no broader normalization
  or stable ID order claim. All six Onimusha exports are exact.
- Standalone/export-boundary proof rebuilds four complete fixtures independently
  on each side: SequenceExport, CollectionExport, ResolvedInstrument and Session.
  Release and focused sanitizer captures preserve 9,504 ordinary cases and
  1,680,444 bytes, SHA256
  557df15473104ed8b73ca8e54d89d8e7f2168590e73d725285bd18cf5fb8e365.
  Cover invalid/missing/wrong/duplicate IDs, zero/one/multiple collections,
  chosen members, loops, missing executors, standard/nonstandard exceptions,
  filenames, ordered full diagnostics, callback side effects and observation
  of returned artifacts after snapshot/source/program/callback owners die.
- Standalone fault injection preserves 2,960 Release traces (262,600 bytes,
  SHA256 2b006346fec2e663cdf94548951fb4532a2ec0a779220c635a68c32f7c111b26)
  and 2,896 sanitizer traces (240,200 bytes,
  SHA256 7e52fbdec233ab4db47aaa660bdcbabb51dfa0b626ed0e1b59647789d12caa00).
  Compare before/after within each build mode, including propagated allocation
  failure and failures caught by rendering/preparation. Optimization and sanitizer
  builds have different allocation sequences; do not compare those across modes.
  Ordinary allocation counts stay exact: Release 52,164 requests/5,760,072 bytes,
  sanitizers 51,300/5,366,088. No storage or allocation improvement claim.
- Motion preserves 144,648 arithmetic/lifecycle/bound-LFO cases and 70,294,645
  bytes: 100,080 broad cases, 43,848 lifecycle cases and 720 bound-LFO cases.
  Include default construction, clear/inactive ticks, reset/rebegin, copying,
  signed zero, NaN/infinity, interruptions and provenance. Complete SequenceModel
  fixtures pass on both sides; matching frozen-30 emitter/timing/core refresh
  preserves bound observations under ASan/UBSan/float-cast-overflow. The pure
  arithmetic baseline header is unchanged from 29 to 30. Twenty actual consumer
  Release/Werror checks pass across ten implementations and nine format families.
- Source alias is configured-Werror-clean in Debug/Release/RelWithDebInfo; full
  existing session/source fixtures and all 44 integrated suites cover its checked
  lookup/lifetime contract. Do not add an implementation-mirroring fixture.
- Focused sanitizers cover the changed units, relevant matching arithmetic/
  emitter/timing units and complete probes/fixtures. Other frozen archives remain
  Release; libc++ annotations are disabled consistently where needed. No whole-
  application instrumentation, Windows run or exhaustive format parity claim.

Rejected larger renderer deletion: in temporary files, collapse the separately
owned pointer delivery timeline into the mutable planner's variant event vector,
filter local reverb, append global reverb copies and stable_sort those variants.
This removes an owner/indirection but sorting 352-byte payloads instead of 8-byte
pointers is expensive. Across 24 isolated per-track preparation cases with seven
samples each, the 131,072-event/no-global case grows about 12.0->40.5..40.8 ms;
requested bytes grow 2,097,152->46,137,344 (22x), despite one fewer request. With
just eight globals, reserved capacity gives 12.72->41.88 ms; unreserved capacity
requests 138,414,848 bytes. These measurements exclude existing planner execution
and global-owner construction and use old then new sample order, not randomized
whole-renderer timing or peak application memory. Microchecks preserve ordering
and variant indices; stop before full fixtures/sanitizers because the scoped cost
already rejects the design. Keep production renderer unchanged. Adding conditional
merge machinery would recover complexity rather than simplify it.

Evidence: standalone-midi-boundary31, inactive-motion-target30,
source-bytes-alias31, pass31-baseline, comparison-pass31.json and
volume-coverage27/pass30-vs-pass31-{summary.json,report.md}, including the retained
volume-35 classification/repetitions, under /tmp/vgmtrans-cleanup2. Rejected
renderer evidence stays in /tmp/vgmtrans-renderer-timeline-owner31.

Next: replace the private single-use SF2 modulation return record with its direct
writer, assess Sony PS2's copied program/track split data with lifetime proofs,
and remove redundant SNES predicates only after verifying both inspection modes
and malformed source/diagnostic order. Keep SequenceVM and source/export ownership
boundaries; do not add move/view APIs without a meaningful reduction.


## Cleanup2 thirty-second batch: write records directly and copy into final owners

This batch removes 13 net production lines across five files, bringing cleanup2
to 1,089 removed: 984 architecture lines and 105 format lines. The physical diff
is 15 additions/28 deletions. Cumulatively, production C++/headers are 1,891
additions/2,980 deletions across 157 paths; architecture is 1,570/2,554 across 88,
and formats are 321/426 across 69. No fixture changes; tests still gain 559 lines.

- The SF2 writer encodes one SynthModulator directly into its target byte buffer.
  Remove the private SfModulatorRecord and the optional-return/unpack boundary;
  the resolved source/destination guard and s16 amount clamp precede all writes.
  Keep the exact five little-endian record fields, including zero amount-source
  and linear transform. Invalid SynthSource values retain the existing default-
  controller fallback; this introduces no new input-validation or numeric policy.
  Remove ten lines. Keep the DLS connection representation that multiple stages
  genuinely inspect and modify, and keep the neutral SynthModulator model.
- SNES format validation relies on the directory reader's inspected-success
  guarantee. Remove Hudson/Suzuki's stream-presence checks after inspect=true,
  Nin's equivalent conditional check, and Nin's separate start>loop comparison
  already included by loopAddressIsBlockAligned. Remove three lines. Keep the
  optional stream for inspect=false discovery, all format-specific alignment,
  Suzuki's sample-start/DIR rule, tuning bounds and unused volume/ADSR/tuning
  sentinels. This changes no platform helper, source metadata or sample policy.
- Sony PS2 sequence preparation copies each immutable selected program directly
  into its final RuntimeConfig vector, then stamps the final element's bank.
  Remove the temporary owning ProgramRuntimeInfo and its second copy of pitch-
  bend zones. This is LOC-neutral; the prepared runtime still owns its program
  table and original scanned tables remain immutable. Keep selected order,
  foreign-format exclusion, bank clamping and first matching program precedence.
  Prefer this form over moving a local copy: the destination is now visibly the
  only new owner, with no additional borrow API or durable layout change.

Validation:

- Complete macOS Release integration finishes all 52 steps without warnings;
  all 44 CTest suites pass. Freeze matching sources/headers/tests, four archives
  and shell. Shell SHA256 is
  e5e2bb8fbdfde480bab84309c398e707251082eee914c4485844601fb84ef24a.
- Original corpus against frozen 31 preserves all 252 input/policy records and
  3,922 artifacts exactly. Expanded archive replay preserves all 894 export
  records, 858 artifact names/sizes/hashes and 218,240,549 bytes per binary.
  All 852 successful exports and 42 existing failures remain exact; the five
  unsupported DSF inventory failures also remain exact. Combined real input
  coverage is still 37/38 registered families; KonamiTMNT2 remains synthetic-only.
- Seventy-one expanded raw inventories match. Onimusha 2/053.psf2 (volume 35)
  and Growlanser II/Hoot Hoot.psf2 (36) only swap concurrent SonyPS1/SonyPS2 pool
  IDs 0/1 with the coherent selected-pool reference. Both initial pairs and four
  automatic same-binary repeats, eight records total, match under the existing
  narrow asset-declaration/member-ID mapping. Every other source/parent/asset/
  collection value, label/range, status and the two ordered Sony warnings stay
  exact. All six exports per input match. Preserve raw differences and link prior
  original/cleanup repetition evidence; no unnecessary new repetition or broader
  normalization. The difference is the same established scheduling pattern.
- SF2 preserves 29,700 raw record cases in Release and focused sanitizers:
  2,136,645 bytes, SHA256
  3657a680e13874711df7ecb067446d0ae7df1fc84f857cd8805c36862abf0186.
  Include all seven destinations, Unknown/invalid enums, pressure/default/invalid
  source fallback, s32/s16 extremes, empty/three-byte prefixes, five capacities
  and injected allocation requests 0..4. Independent field/LE/routing assertions
  pass; all 4,725 actual bad_alloc cases preserve partial bytes/capacity/failure.
  Allocation traces stay identical at 18,270 successful requests/193,725 bytes
  per runner; no heap or throughput improvement claim.
- SF2 public proof preserves 3,768 artifact captures: 17,023,170 bytes, SHA256
  a837e6bc76a760d3a5eff15e3842caef714e9651f0b9745e1f4c4fc37e58ed4e.
  Cover 24 complete SynthExport fixture captures, 672 loop/alias cases and 3,072
  instrument/region modulation cases across all four rounding modes. The complete
  Modulation suite also passes; its internal checks are not additional captures.
  Native/simulated policies, gain/depth/controller choices and zero/subnormal/
  huge/nonfinite rates retain artifacts and ordered diagnostics. No floating-point
  flag equality or correction of the known zero-Hz policy is claimed. Actual
  Debug/Release/RelWithDebInfo production Werror checks pass after application.
  The writer/probes/two full suites are sanitizer-instrumented; other frozen
  core/fmt units remain Release with matching libc++ annotation settings.
- SNES comparison refreshes against matching frozen 31: 69,060 cases and 8,405,042
  exact bytes. Three full Hudson/Nin/Suzuki suites and 182 captured native
  performance/MIDI renders pass. Cover 66,240 direct Nin header cases across both
  inspection modes/23 profiles, plus 1,380 synth cases with sparse invalid and
  later valid rows. Direct Nin coverage includes 7,728 admitted uninspected rows
  without a stream and 2,806 inspected rows with guaranteed streams. Ordered
  diagnostics, annotation/field/link/ID/range data, bank/sample values and decoded
  PCM stay exact. Six paired configured Release/Werror checks and focused
  ASan/UBSan/float-cast-overflow checks pass; no platform contract change.
- Sony compares 1,920 cases and the complete SonyPS2 fixture in before/move-local/
  direct-final-owner forms under Release and focused sanitizers. Six traces match
  67,632,968 bytes, SHA256
  ecf3ce69e826d4b3a4278688f7f1722a3c95c1d064babb5e0095c3d5cb1db05b.
  Cover 0/1/2/4/16/128/256/257 banks, 0/1/4/16 programs, 0/1/2/4/16 zones,
  reversal, duplicates, empty/overlapping zones, missing/wrong retained data and
  foreign-format banks. Full config/source tables and ordered diagnostics match;
  native IR capture covers headers/order and selected payloads plus exact MIDI,
  not a serialization of every hypothetical event field.
- Sony destroys source tables, snapshots, contexts and MIDI input bytes before
  inspecting returned owned RuntimeConfig, then destroys runtime/program before
  observing durable performance/MIDI. Preparation allocations drop
  461,328->238,224 requests and 60,781,376->53,084,288 requested bytes; 336 cases
  improve, none grow, and both candidate forms have the same counts. Maximum
  saving is 4,112 requests/394,752 bytes. ProgramRuntimeInfo stays 48 bytes and
  RuntimeConfig 24. This is scoped preparation allocation accounting, not peak
  RSS or arbitrary allocator-failure timing equivalence. Three configured Release
  Werror checks pass. Resolver, Sony sequence, runner and full fixture are
  instrumented; other matching frozen-31 archives remain Release.

Rejected validator reduction: remove the three explicit invalid-annotation-ID
clauses because SourceMap::find cannot index invalid IDs. A temporary comparison
passes four full Session/Registry/SourceMap/SynthBuilder suites and 16,496 malformed
cases in Release and focused sanitizers: 95,930,807 exact bytes, SHA256
fa419a7700d1be14957b79928bb54605df824f4827a574268e413e56373511e3.
Six actual three-configuration Werror checks pass. Nonetheless it saves only one
line and routes invalid references through every immutable Part lookup, losing
immediate rejection. Scan validation reads result.sourceMap; it does not implicitly
join existing source maps. A snapshot's multi-part map can still be copied into a
public result with an invalid diagnostic annotation. Prior measured query-gate
regressions establish the index-traversal concern, but no new validator timing
claim is made. Keep current predicates and no additional find fast-path/API.

Evidence: sony-runtime-owner31, redundant-snes-checks32, reference-resolution32,
pass32-baseline, comparison-pass32.json and volume-coverage27/pass31-vs-pass32
summary/report/classification files under /tmp/vgmtrans-cleanup2. SF2 private/public
and exact application evidence stays in /tmp/vgmtrans-sf2-modulator-write31.

Next: independently prove a Sony track's immutable zone view across program
switches, held notes and VM passes before any application. Separately measure a
fixed-domain SRCN set against the current ordered vector normalization, including
sparse workloads, template code size and public range/consumption boundaries.


## Cleanup2 thirty-third batch: one target mapping and immutable track views

Three production files remove 20 net lines, bringing cleanup2 to 1,109 removed:
1,005 architecture lines and 104 format lines. Physical diff is 25 additions/
45 deletions; cumulative production C++/headers are 1,913/3,022 across 157 paths,
architecture 1,589/2,594 across 88, and formats 324/428 across 69. No fixture
changes; cumulative tests still gain 559 lines. Preserve SequenceVM and neutral
source performance/synth models.

- SF2 has one private destination mapping containing its generator and default
  controller numbers. Replace two parallel SynthDestination switches and two
  optional results with one reused SfModulationDestination lookup/guard. Both
  generator and modulator writers read that target mapping; channel pressure
  overrides its default controller and invalid source enums retain the old
  fallback. Remove 18 lines. This record represents reusable target routing
  policy, unlike the deleted single-use full modulator/amount result. Keep SF2
  routing local; introduce no cross-target mapping framework or enum-index table.
- Sony PS2 TrackState borrows a span<const PitchBendZone> from its immutable
  selected ProgramRuntimeInfo. An empty span resets selection. The ProgramState's
  RuntimeConfig owns that table for the whole semantic/render pass; playback
  never mutates it, no track state escapes, and loop checkpoints copy only source
  position. Remove the per-track copied vector and per-program-switch copies.
  Add the direct span include, so the change adds one production line. Keep
  mutable RPN defaults and ActiveNote's copied scalar bounds: held/sustained
  voices must retain their old program's pitch interpretation after selection
  changes. No new view/config/shared-owner framework or public API.
- EnvelopeUpdate::replace delegates to existing set(values, All). Keep both
  semantic names, explicit masks, clear-versus-inherit behavior and restore.
  Remove duplicated result assembly and three lines. Envelope remains the same
  trivially copyable, nothrow-movable native scalar value; no storage/heap change.

Validation:

- Full macOS Release integration completes 349 steps without warnings. All 44
  CTest suites pass; freeze complete matching value sources/headers/tests, four
  archives and shell. Shell SHA256 is
  d230f329aed940db0e4dc8c06cbfde006f77d929aaee0088b3e38ee9bc2a59b4.
- Original corpus against frozen 32 preserves 252 input/policy records and all
  3,922 artifacts exactly. Expanded `/Volumes/emu/vgm` replay preserves all 894
  export records, 858 artifact names/sizes/hashes and 218,240,549 bytes per binary.
  All 852 successes, 42 existing export failures and five unsupported DSF
  inventory failures retain status and diagnostics. Real inputs still cover
  37/38 families; KonamiTMNT2 has synthetic coverage only.
- Seventy-two expanded raw inventories match. Growlanser II/Hoot Hoot.psf2 only
  swaps the two concurrent Sony pool IDs 0/1 and the coherent selected-pool
  reference. Four initial/automatic-repeat records agree under the unchanged
  narrow declaration/member-ID mapping and match retained original evidence.
  All source/parent/asset/collection metadata, bank 2, sequence 3, collection 0,
  labels/ranges, status and two ordered Sony warnings stay exact; six exports
  match. Retain raw differences; no new repetitions or broader normalization.
- SF2 proof uses complete matching frozen 32 before/after units and preserves
  29,799 private captures (29,700 modulator and 99 actual instrumentChunks
  generator cases): 2,151,999 bytes, SHA256
  da6e2171c788caab562cffab8f9793d70751c6d8ed17c2eaf72c61b9638af785.
  All four Release/sanitizer streams match. Verify all seven routes, Unknown and
  invalid destination/source enums, pressure/default fallback, signed amount
  extremes/clipping, prefixes, capacities and independent complete LE fields.
  All 4,725 injected allocation failures preserve partial bytes/capacity/ordering;
  ordinary modulator requests stay 18,270/193,725 bytes per runner.
- SF2 preserves 3,768 public artifact captures/17,023,170 bytes in all four runs,
  SHA256 a837e6bc76a760d3a5eff15e3842caef714e9651f0b9745e1f4c4fc37e58ed4e.
  Complete SynthExport/Modulation suites pass; cover 24 fixture captures, 672 loop/
  alias cases and 3,072 instrument/region native/simulated modulation cases in
  all four rounding modes, including zero/subnormal/huge/nonfinite frequencies.
  Three configured production Werror checks pass before and after exact live
  application. No heap/speed improvement or full numeric/FENV-flag parity claim.
- Sony zone views preserve 1,296 interleaved-track/lifecycle cases and the complete
  SonyPS2 fixture under Release and ASan/UBSan/float-cast-overflow. Four native/
  MIDI/source-span traces match 155,987,686 bytes, SHA256
  639fca93f89f72e5b722115c0a31bce30db3b006b1f97934c6ff54541e5bb65e.
  Cover 1/4/16 channels, 1/8 switch rounds, six empty/disjoint/overlap/extreme/
  inverted/32-entry zone shapes, finite/infinite MIDI loops and ordinary/two-
  section/repeating-song forms, wheel extremes, embedded/external runtime,
  first-match duplicate programs, pending banks, overlapping equal-key voices,
  held/sustained/released notes and reset/all-notes/all-sound-off.
- A focused Sony assertion proves a sustained old-program note retains positive
  256/negative 128 bounds after program change while a new attack adopts
  1,024/512. Source/config/scanned program/snapshot die before rendering from a
  copied owning runtime; runtime/program/ProgramState die before durable output
  observation. Trace includes all relevant note/bend/range/selection fields and
  other selected native payloads, headers/order, source spans and diagnostics;
  it does not serialize every hypothetical IR variant.
- Sony TrackState shrinks 112->104 bytes. Each mode saves 22,032 scoped render
  allocations/819,720 requested bytes: 1,188 cases reduce count, all 1,296 reduce
  bytes, none grow. Maximum saving is 32 requests/3,296 bytes. Release totals
  are 3,654,323->3,632,291 requests and 3,186,842,824->3,186,023,104 bytes;
  sanitizer totals differ from Release, so compare before/after within each
  mode. No peak-RSS or arbitrary allocator-failure timing claim. Paired actual
  Release Werror checks pass. Frozen-31 model/Sequence/fixture/runtime contracts
  are verified unchanged to 32; original matching archives remain the actual
  probe authority, with 33 integrated replays validating composition.
- Envelope forwarding preserves 3,072 mask/bit-pattern/rounding-mode cases and
  emitter captures in all four Release/sanitizer traces: 762,656 bytes, SHA256
  ade6b644597a5463afed6acd2282635a0d325188e33f8c9ecc63eb8de81cd91c.
  Cover 64 optional-stage masks, signed zero, subnormals, extrema, infinity and
  distinct quiet-NaN payloads, source value preservation, all-fields mask,
  moved input, restoration and exact emitted scope/header identity. Three full
  InstrumentVariant/ResolvedInstrument/MP2k suites pass. Twelve actual two-caller
  Debug/Release/RelWithDebInfo Werror entries pass; scalar type traits are checked.
  The paired O2 PerformanceEmitter and MP2kSequence object files are byte-identical,
  including text/exception/unwind/data sections. This is scoped optimized-object
  evidence, not a timing or whole-application object-equivalence claim.
- Focused sanitizers cover complete changed writer/sequence/caller/probe/fixture
  units, with matching VM/timing/resolution where relevant. Remaining frozen
  archives are Release and libc++ annotations are disabled consistently. No
  whole-program instrumentation or Windows run claim.

Rejected pointer-kind cache: store commandKindPrefix + '-track-pointer' as the
session's final owned string and build the constructor header from config directly.
A temporary 96-case prefix/pointer/null-map comparison preserves annotation kinds
and labels, but adds a line and grows three ordinary sparse paths. A 16-character
prefix with zero pointers/map grows 5 requests/344 bytes to 6/376; eight pointers
improve 51/8,584 to 44/8,360. A 23-character zero-pointer prefix keeps six requests
but grows 376->392 bytes. Null-map initialization can be delayed to avoid unused
allocation, but a lazy protocol would recover overhead. Retain the clear owned
prefix; no full parity/sanitizer claim for the rejected draft.

Evidence: sf2-routing33-frozen32 under /tmp/vgmtrans-, and
sony-ps2-pitch-zones32, envelope-replace-forward33, sequence-pointer-kind33,
pass33-baseline, comparison-pass33.json and volume-coverage27/pass32-vs-pass33
summary/report/retained-classification files under /tmp/vgmtrans-cleanup2.

Next: prove the DLS mapping plus direct guarded connection writers independently.
Keep the SRCN fixed-set draft temporary while comparing sparse microcosts with
native app workloads; preserve its moved-vector/failure boundaries. Probe the
expression helper's missing source-level parameter only if existing default
calls retain their closure/allocation behavior.


## Cleanup2 thirty-fourth batch: direct writers and one SRCN catalog path

Ten production files remove 36 net lines, bringing cleanup2 to 1,145 removed:
1,037 architecture lines and 108 format lines. Physical diff is 105 additions/
141 deletions; cumulative production C++/headers are 2,009/3,154 across 158 paths,
architecture 1,683/2,720 across 89, and formats 326/434 across 69. No fixture
changes; cumulative tests still gain 559 lines. Keep SequenceVM and export-neutral
format models.

- DLS has one private destination mapping for oscillator, destination and default
  controller. Generator and modulator writers reuse that policy and write their
  guarded connections directly, without single-use optional connection factories
  or constructing a fake generator to mutate its result. Preserve the named
  DlsConnection wire record, pressure override, invalid-source fallback and the
  unsupported VibratoDelay modulator rule. Remove 22 lines, including four from
  the direct writer protocol. Keep this mapping within the DLS exporter.
- Used-instrument sound-bank export owns collection count, uniqueness, zero-input
  error and the sole-collection path in one policy branch. Full-bank export proceeds
  to independent binding without an unnecessary membership-count scan. Line count
  is unchanged; sample binding may still consult collection relationships.
- MIDI planning derives the next generated automation ID while it already reads
  every source automation for sequence order and transitions. Compute the ID before
  pitch-kind, interruption or zero-extent filters; remove the second traversal and
  two lines. Preserve valid-ID and unsigned wrap behavior.
- Remove ScanResultBuilder's unused public diagnostic forwarding method; warning()
  appends the same owned value directly. Keep the general diagnostics() sink and
  the unrelated live VmApi::diagnostic API. Remove five lines.
- Move the normalized outer CUE text into its final C++20 istringstream. Line count
  is unchanged. Keep fields(line) copying: getline reuses line and its capacity.
- CompilerCursor::Event::emitExpression gains the existing emitter's optional
  sourceLevels argument, matching emitLevel. Square PS2 uses the named helper with
  resolution 128 instead of a custom runtime lambda; remove four format lines.
  Its signed-byte conversion moves to decoding: negative values remain zero and
  all nonnegative (value+1)/128 results are exact dyadics.
- SNES catalog reading uses bitset<256> membership, projects each input once in
  input order, narrows to u8, and walks the unique SRCNs in ascending order with
  unchanged entry/stream validation. One header implementation replaces projected
  scratch-vector collection plus the second normalization implementation; remove
  three lines. Keep the concrete by-value vector overload as an inline adapter,
  preserving lvalue copies, rvalue consumption, exact function-pointer signature
  and bare braced/empty calls. No new range or ownership protocol.

Integrated validation:

- Full macOS Release completes 282 steps without warnings. All 44 CTest suites
  pass. Freeze matching complete value sources/headers/tests, four archives and
  shell before replay. Shell SHA256 is
  49099482613f28ac4cad1c97a979e2d4fbc31d6e3fcb639740c8f9160dd9dd80.
- Against frozen 33, the original corpus preserves all 252 input/policy records
  and 3,922 artifact names/hashes. The expanded /Volumes/emu/vgm replay preserves
  all 73 raw inventories and 894 export records, including 852 successes and 42
  existing export failures. All 858 artifact names/sizes/hashes and 218,240,549
  bytes per binary match. The five unsupported DSF inventory failures retain
  status and diagnostics. No ID normalization or fresh scheduling repetitions
  were needed. Real coverage remains 37/38 families; KonamiTMNT2 is synthetic only.
- Source SHA256 and the exact approved ten-file diff remain unchanged through
  build, tests, freeze and both comparisons. Proofs below were reviewed before
  live application; integrated replay validates their composition.

Focused validation and costs:

- DLS preserves 35,640 private captures (5,940 generator, 29,700 modulator),
  2,606,499 bytes, SHA256
  ec2e151b4e33e2519ba381780f2a53563bf45991924f437c2c302291f4f09571,
  in all four Release/sanitizer streams. Independently check all five LE fields,
  seven routes, unsupported/invalid destinations, pressure/default/invalid sources,
  signed16.16 extremes/clipping, prefixes and capacities. All 4,995 injected
  allocation faults preserve partial bytes and failure order. The skipped scale
  calculation on unsupported VibratoDelay is pure, bounded signed64 arithmetic.
- DLS preserves 3,768 public captures/17,023,170 bytes, SHA256
  a837e6bc76a760d3a5eff15e3842caef714e9651f0b9745e1f4c4fc37e58ed4e,
  in all four modes, with complete SynthExport/Modulation suites. Three actual
  configured Werror checks pass after application. Broad captures use matching
  frozen32; verified 28 transitive value dependencies establish frozen33 authority,
  with only EnvelopeModel's already-proven scalar forwarding change. No private
  signature compatibility, heap-saving, performance or full FENV-flag claim.
- Export preserves 10,032 generated bank calls, 10,100 total public calls and
  10,000 artifact captures in four Release/sanitizer streams: 3,212,474 bytes,
  SHA256 f107235f7dabd3c08eb6e97a5f021a11c64e3dc52c7994b9949f4d9ee24bf757.
  Cover 19 malformed/preparation/runtime/sample states, six collection layouts,
  SF2/DLS, used/full selection and eight policies, plus wrong types, missing IDs
  and retained snapshot repetitions. Preserve exception types/messages and error
  order. Four complete CollectionExport/SequenceExport/ResolvedInstrument/Session
  suites and three configured Werror entries pass. No allocation/timing claim.
- Planner preserves 2,160 ID/sequence/intent/end-reason/extent/voice/policy cases,
  1,638,000 bytes, SHA256
  d962b54bae89af617a44f94eceed8d0a626780dfe24b12b296c5b973bb5fbaa9,
  in four Release/sanitizer traces. Complete MidiPitch/MidiRenderer/MidiModulation
  suites pass. Captures include relevant note/attack/pitch/layer/portamento fields,
  generated headers/IDs/order/annotations, complete MIDI and diagnostics; no claim
  to serialize every hypothetical variant. Six configured three-mode Werror checks
  pass. Paired O2 object text shrinks 80 bytes; no speed or whole-object claim.
- Builder preserves 480 message/range/source-ID/count cases and general sink values
  before/after warnings: 5,446,386 bytes, SHA256
  96e6d602543e52b27cb938c3ee62a4214804203c622435f6fcde90ba62db2a7a,
  in four modes. Both sides retain 6,336 allocations/8,388,096 requested bytes within
  each mode. Four full Registry/Session/SourceMap/SynthBuilder fixtures and 83 actual
  configured Release Werror consumers pass. The initial strict script paired the old
  builder cpp with the new header; retain that harness failure and its corrected
  source-path result. No production failure or arbitrary bad_alloc staging claim.
- CUE preserves 1,224 parser/extractor cases and the full fixture in four modes:
  810,978 bytes, SHA256
  1d46447d43f87829b85dfaf878a5a8ec5c0608f1fc6e89e5d36b55fcdfc6097c.
  BOM, CRLF, final-line, NUL, quoted/backslash paths, missing archive members,
  multi-track and malformed/duplicate index cases remain exact after input-store
  destruction. Complete fixtures also cover disk sectors, session parentage,
  redirects and source-family removal. Parse and extraction scopes each save
  1,196 requests/14,300,480 bytes; 28 SSO cases are unchanged, none grow. Six
  configured three-mode Werror checks pass. No RSS or bad_alloc staging claim.
- Expression preserves 4,092 generated source/IR/MIDI cases (all 256 encoded bytes)
  and full Cursor/Square fixtures under paired focused sanitizers: 35,491,160
  generated bytes, SHA256
  fe2d49c0dd97dd20a6e037d071cac257a914f353717d7e644b3d858d21748f20,
  plus 9,673 Cursor and 2,317 Square fixture bytes. Owners die before playback.
  All 256 construction/composition/body-copy allocation cases match; the capture
  grows 8->16 bytes but stays inline in the 48-byte CommandBody. Twelve actual
  paired Release Werror checks pass. Four default production callers grow 32
  text bytes each; Square is unchanged. No blanket code-size/performance claim.
- The retained-vector SRCN candidate preserves 4,230 generated cases (1,658
  catalog, 2,572 compatibility), 47,263,070 bytes, with full SynthBuilder fixture
  and four supplementary direct/function-pointer ownership calls. Cover sized,
  nonsized, single-pass and projected ranges, signed/unsigned extrema, narrowing,
  duplicates, throwing projection prefixes, malformed/wrapping/truncated entries,
  exact ordered catalog/range fields and post-call vector state. Paired focused
  sanitizer traces and all 23 actual configured Release consumers pass.
- Across 112 SRCN allocation settings, numeric-vector paths keep their copy/move
  allocations; 72 nonempty projected/nonvector settings each lose one scratch
  allocation and 12 empty ones remain allocation-free. A fully matching frozen33
  native shell loses 3,888 text bytes/4,152 file bytes. Eight SPCs from seven driver
  families, two policies and 48 MIDI/SF2/DLS payloads match exactly.
- SRCN fixed-domain iteration retains a bounded sparse cost: malformed singleton
  helper cases are about 1.9x the old helper. Earlier broad native timing overlapped
  expression compilation, so its roughly 3% Pandora/events slowdown prompted a
  clean isolated repeat. After three warmup pairs, 24 shuffled measured pairs have
  median ratio 0.9959 (range 0.9464..1.0177), with exact outputs; that slowdown did
  not reproduce. This one-stratum check establishes no all-platform speed claim.
- Projected SRCN allocation-failure staging changes intentionally: the old scratch
  vector could throw before/during projection; fixed membership allocates only for
  output. External side effects under bad_alloc may stage differently. Native
  projectors are plain reads, and failed scans discard their result. Deterministic
  projection order/throw prefixes stay exact; add no rollback protocol.
- Focused ASan/UBSan/float-cast-overflow instruments changed units, instantiated
  helpers, probes and relevant complete fixtures; other matching frozen archives
  remain Release. libc++ container annotations are disabled consistently. No
  whole-program instrumentation or Windows-run claim.

Rejected SRCN drafts: array<bool,256> adds a 256-byte stack object and costs roughly
11x on empty normalization; the pure generic bitset version changes moved-vector
post-state and removes the exact numeric function-pointer API. Keep the existing
inline by-value boundary. Correct the older draft benchmark count: each draft has
112 settings x six pairs = 672 paired measurements/1,344 executions.

Evidence: dls-routing34/write and used-bank-export-scope34 under /tmp/vgmtrans-,
and planner-source-ids34, scanner-diagnostic-surface34, cue-stream-owner34,
expression-levels34, fixed-srcn-domain33, pass34-baseline, comparison-pass34.json
and volume-coverage27/pass33-vs-pass34 under /tmp/vgmtrans-cleanup2. Exact source
hashes and complete build/freeze authority are in pass34-baseline/provenance.json.

Next: prove that generated planner changes can finish growing before borrowed
transition pointers are published, removing reserve-bound coupling and unnecessary
scans of earlier generated changes. Probe TriAce extraction directly into its final
image, small dead ROM database surface, AKAO reference collection without temporary
program vectors, and the redundant void guard around dependent stream-hook discovery.


## Cleanup2 thirty-fifth batch: final owners and one hook eligibility rule

Six production paths remove 15 net lines (48 additions/63 deletions), bringing
cleanup2 to 1,160: 1,043 architecture and 117 format lines. One existing Konami
fixture assertion changes without net lines; cumulative tests still gain 559.
Cumulative production C++/headers are 2,057 additions/3,217 deletions across 161
paths, architecture 1,691/2,734 across 91, formats 366/483 across 70. Retain
SequenceVM and the export-neutral source models.

- MIDI planner generates all key changes before publishing pointers into that
  vector. Delete its all-note reserve and in-loop pointer publication; register
  the completed vector once. This adds one line but removes a lifetime constraint
  and scans of earlier generated changes that cannot match the current note:
  collectNotes admits one span per unique valid logical note ID. Preserve source
  transition inspection, generated IDs, sequence order and coincident sorting.
- Compiled runtime discovers beginSection with its existing dependent requires
  expression alone. Forming StreamState& already rejects void/const void; remove
  the redundant outer type guard and nesting (two lines). Keep state factories,
  callback body and the wider playback construction guards.
- TriAce PS1 extraction lazily builds its final image at the first accepted SLZ
  sequence and appends each decoded sequence directly. Delete the nested payload
  vector and second assembly loop (four lines). Sequence segment count supplies
  its index; scan the immutable input only. No-match inputs still produce nothing.
- Remove unused RomDatabase size/empty queries (two lines). Its sole size use was
  a mirrored fixture assertion; check find("fixture") and the actual name instead.
  Parse/load/find/storage and all dispatch/group checks remain.
- AKAO reference collection resolves the last valid bank/table first, then inserts
  valid program operands directly into existing ordered sets (five lines). Delete
  the temporary program vector and replace the now-two-role switch with plain
  conditions. Preserve program-zero's usesIndividual flag, noAttack membership,
  exclusion from individual IDs and final-bank table attribution.
- Byte-pattern matching delegates source bounds to ByteReader and pattern
  validity/length to MaskedBytePattern::matches. Delete its duplicate validity
  check and wrapper branch (three lines); the search helper remains unchanged.

Integrated validation:

- Full macOS Release completes 139 steps without warnings; all 44 CTest suites
  pass. Matching complete value sources/headers/tests, four archives and shell
  are frozen before comparisons. Shell SHA256 is
  c7c48ef76ada2ef8720e78d11850082a5d533269c0c88923c2c3aa19114b63a5.
- Against frozen34, the original corpus preserves all 252 input/policy records
  and 3,922 artifact names/hashes.
- The /Volumes/emu/vgm replay preserves all 73 raw inventories and 894 export
  records: 852 successes and 42 matching existing failures. All 858 artifact
  names/sizes/hashes and 218,240,549 bytes per binary match. Five unsupported
  DSF inventory failures retain status/diagnostics. No ID normalization or fresh
  scheduling repetitions are needed. Real coverage remains 37/38 registered
  families; KonamiTMNT2 has synthetic coverage only.
- All seven approved source/fixture hashes and their exact diff remain unchanged
  through build, tests, freeze and replay. Focused proofs below were reviewed
  before application; archive replay checks the composed changes.

Focused validation and costs:

- Planner preserves 75,059 actual renderer captures (74,912 generated and 147
  complete MidiRenderer/Pitch/Modulation fixture captures), 144,898,178 bytes,
  SHA256 9d16fe441c88baed7fb17b097bd986d2ba137eef0250acf8a9e588822eced47a,
  in four Release/sanitizer streams. An additional 8,334 planned-event/MIDI
  boundary traces preserve 17,118,254 bytes, SHA256
  3f70efd72e6569cad5433bd001ed23c5a6e4e980742ed1d5ba002e2bb576a458.
  Cover duplicate/invalid/mixed IDs, ties, voices/lanes, sampled/scalar and
  current/future transitions, explicit boundaries and coincident ordering.
  Six actual three-configuration production Werror checks pass unsuppressed;
  three further live checks pass. Fixture-only unused helper/range-loop warnings
  are suppressed. Configured O3/NDEBUG planner text shrinks 480 bytes.
- Planner allocation accounting covers 70 complete planner cases, including
  output observation/destruction, with input/resolution/tempo prepared first.
  At 65,536 independent notes, requested payload is 82,548,344 -> 69,441,144 bytes
  and peak 73,183,960 -> 60,076,760; one continuation saves about 13.1 MB too.
  Dense 1,024-note chains add ten requests and 204,600 requested bytes (~6.8%),
  while peak remains 2,956,048. Covered 1,024-note changes eliminate 204,800
  unused reserved bytes; a single note eliminates 200. These are scoped payload
  counts, excluding allocator overhead, not RSS or full-export memory claims.
- Clean native-allocator timing runs after all other compilers/runners stop:
  seven shuffled epochs x 70 cases = 490 paired observations, warmed and taking
  median seven trials. Dense 1,024-note paired ratios are 0.8908 pitch bend and
  0.4422 portamento; independent 65,536-note ratios are 0.9550/0.9601. Retain
  unfavorable strata: dense 16-note pitch bend 1.0487, explicit-covered 1,024
  pitch bend 1.0364, one-continuation 65,536 pitch/portamento 1.0341/1.0300.
  Accept the measured localized costs for simpler ownership, lower sparse
  allocation and elimination of impossible generated-transition scans. No
  aggregate application-wide or cross-platform speed claim. Reject deque storage:
  its block allocations and dense cost add a storage policy without removing
  more responsibility; cold-storage comparison is separate from actual planning.
- Runtime hook proof covers 1,152 cases/24 signature-state shapes and complete
  VM/Cursor/SonyPS2/HeartBeatPS1/SegSat suites. All 12 runtime arms pass; paired
  traces preserve 3,668,984 bytes, including 1,120 generated and 74 fixture renders
  plus matching throws. Keep callable-with-bool compatibility: ignored nonvoid
  results, implicit conversion/default arguments, generic/static/callable members,
  const/noexcept/ref qualifiers, inaccessible/deleted/absent/wrong hooks and void
  states. Factory/config/prepass/playlist/budget/wait/provenance paths stay exact.
  Ten actual configured Release Werror checks pass; all five object text payloads
  are byte-identical. No runtime allocation or speed claim.
- TriAce preserves 1,952 cases/122 styles and 7,411,350 bytes, SHA256
  f71c33de009721b7294a424271e8fd280cd4680db89985a1f0b530d94393177a,
  in all four streams; full TriAce fixtures and six actual configured Werror
  checks pass. Cover SLZ0..3/run mode2, references, padding, implicit tails,
  truncation/malformed data, overlapping/nested signatures and multiple accepted
  layouts. Observe final metadata/segments/bytes after input/store destruction.
  Three Star Ocean2/Valkyrie dependency closures preserve 10,198,290 bytes per
  variant of complete images, maps, collections, retained inspection and both
  policy MIDI/SF2/DLS outputs after the original session dies.
- TriAce allocation matrix loses 1,152 requests/54,528 requested bytes; no request
  count/traffic grows. Peak increases 1..248 bytes in 944 cases and falls by up to
  8,355 in 48. Each native image loses one 24-byte request but peak rises 248
  bytes because final metadata now exists earlier. Returned retained sizes match.
  Final output allocation and decoded-buffer release move earlier; arbitrary
  bad_alloc staging/rollback parity is not claimed. Initial harness ObjectRef and
  missing nothrow allocation-header overload errors were corrected; all final
  variants reran. These failures were in probes, not production changes.
- RomDatabase passes 18 actual configured Werror checks and complete Konami
  fixtures in both sides' Release/sanitizer modes. Its implementation stays
  matching frozen Release; fixture/Source/Session are instrumented. An initial
  strict harness missed copied relative fixture headers; final checks use them.
- AKAO collector preserves 279,948 cases/18,249,357 bytes, SHA256
  daa51adc69da1d6776e58c4e8ed8c7d8446c1b0044326e941a095676cee5fb69,
  in all four streams; full AKAO fixtures and six actual configured Werror checks
  pass. Exhaust operand orders, duplicate tags, prior states, absent/nonunsigned/
  overflowing values and explicit zero/noAttack rules. Requests decrease
  196,818 -> 84,750; requested bytes 3,227,376 -> 2,712,000, with no increases.
  Bank2 noAttack and individual insertions now interleave per program; failed
  allocation partial state can differ. Successful sets/flags/attribution are exact.
- Byte matcher preserves 518,401 generated boundary cases, 1,036,801 bytes,
  SHA256 4f7d8342d908fb5592117d54352e602179f0adb482037b20b22bcc6f4f94a259,
  in all four streams against an independent oracle, with zero scoped match
  allocations. Cover source/pattern/mask lengths and representations, embedded
  zero/high bits, invalid source IDs and u64 offsets/extrema. Zero-length settings
  contain duplicates; byte/text dual representation agrees, so no contradictory
  representation-priority claim. Full Session/AkaoSnes/NinSnes/SculptSoftSnes
  fixtures and 24 actual configured Werror checks pass. Instantiated inline
  matcher and all three actual caller units are instrumented. Initial temporary
  harness shared-include, dependency-link and fixture-entry-name errors were
  corrected before final four-arm runs. An application gate first expected empty
  probe stderr, then accepted its exact allocation-count line before mutation;
  post-copy diff check rejected only Git's hunk-header function context, then
  verified exact source bytes/content with that metadata suffix excluded.
- Focused ASan/UBSan/float-cast-overflow covers changed units/helpers, probes and
  complete relevant fixtures; Source/Session and planner renderer/resolution/
  emitter/model dependencies are also instrumented where stated. Other matching
  frozen34 archives remain Release, with consistent libc++ annotation opt-out.
  No whole-application sanitizer, Windows, universal exception-stage or RSS claim.

Evidence: /tmp/vgmtrans-planner-keychanges-owner35 and, under
/tmp/vgmtrans-cleanup2, stream-hook-requires35, triace-final-owner35,
rom-database-surface35, akao-program-owner35, byte-pattern-boundary35,
pass35-baseline, comparison-pass35.json and volume-coverage27/pass34-vs-pass35.
Exact source/fixture hashes and build/freeze authority are in
pass35-baseline/provenance.json.

Next: remove AKAO's never-populated analysis scratch field and redundant sort/
unique of the builder's set-derived requirements, keeping zero filtering after
source annotations. Probe active-note map cell reuse, preserving sustain and
source-span behavior, and remove the unused physical-Hz pan helper if its complete
consumer audit confirms the live cycles-per-tick path remains sufficient.

## Cleanup2 thirty-sixth batch: reuse active state and remove redundant owners

The architecture continues to use SequenceVM and neutral performance/synth
values. This batch removes unused APIs and scratch data, reuses the existing
active-note cell, and makes one private renderer lifetime explicit. It removes
20 net production lines: 14 architecture lines and six AKAO lines. The existing
level fixture gains one net line to generate its own input independently.
Cumulative production reduction is 1,180 lines (1,057 architecture, 123 formats);
tracked C++/header totals are 2,064 additions/3,244 deletions across 162 value
paths. Architecture totals are 1,696/2,753 across 91 paths; format totals are
368/491 across 71. Tests have 984 additions/424 deletions across 43 paths.

### Replace the existing active-note cell directly

`PerformanceEmitter::noteOn` finishes a previous same-key note and assigns the
new note into the existing map slot. It no longer erases and allocates a new
node for a repeated key. The ordinary aggregate assignment resets release and
source-span state; the fields and assignment are nonthrowing ordinary values.
New keys still create their own cell. The note/span closure and new emitted
event keep their existing successful order. No new owner/helper/protocol is
introduced. This change removes one line.

The selected implementation uses `state.notes[key] = Note{...}`. A separately
proven `insert_or_assign` draft had the same successful traces and allocations
but generated more emitter text. The actual configured Release emitter text is
34,465 to 33,777 bytes after this change and the unused pan-hertz removal below
(688 bytes smaller). Pan removal alone yields 33,809 bytes; the slot assignment
therefore also removes 32 bytes relative to that draft. Seven unchanged
consumer objects have identical text. This is an object-text observation, not
a linked application size or speed claim.

The temporary successful proof includes 1,270 generated cases: 486 direct
emitter cases, 768 SequenceVM cases, and 16 neutral/clocked pan cases. It also
runs all nine relevant core and five native suites (SonyPS1, SonyPS2,
HeartBeatPS1, SquarePS2, KonamiPS1), including 84 fixture captures. The selected
assignment independently reproduces 5,298,430 successful trace bytes. Generated
traces alone are 5,042,300 bytes, SHA-256
`abb2b037b9b3000f35b565257c85e42a7133454a77d6d3b75633225b2f07354d`.
Twenty-six configured strict checks pass, including eight fresh selected-source
and consumer checks. Changed units, probes, and fixtures are instrumented with
ASan/UBSan/float-cast checks for the focused success replay; linked unchanged
frozen archives are Release. This is not whole-application sanitizer coverage.

Release allocation probes reserve output before accounting and cover 180 cases.
Ninety-six repeated-key cases save 6,612 requests/476,064 requested bytes in
aggregate (72 bytes per eliminated libc++ node); 84 controls are unchanged.
Successful event/map/span snapshots match. No app RSS or timing claim follows.

Eight Release fault-injection cases explicitly check partial state. Five are
identical. Three change failure staging: invalid automation binding or a
repeated-key output-vector allocation failure retains the already-finished old
cell; the old repeated-key map allocation failure disappears because that
allocation disappears. Event/span closure and consumed IDs at the surviving
failure sites retain their order. Neither version promises transactional
rollback or catch-and-reuse after failed emission. SequenceVM does not catch
these errors, and native collection handling discards the failed performance.
The failure differences are accepted and recorded; they are not described as
arbitrary allocation-failure equivalence.

Temporary evidence: `/tmp/vgmtrans-cleanup2/active-note-cell36/proof.json`,
`report.md`, `assignment-cost-results.json`, and `applied-files.json`. The
selected two-file diff SHA-256 is
`126acb8825176d721c8415a9c35bb0352ee003305a151430d374fb3377fb5955`.

### Remove the unused pan-hertz and inverse MIDI14 convenience APIs

`PerformanceEmitter::panLfoRate(double hertz, ...)` has no consumers across
tracked C++/ObjC++ source/tests. Remove its declaration/body (nine lines), while
retaining the clocked pan helper used by eight native families, neutral pan
rate/context frequency, generic modulation, and export simulation. Physical
hertz remains expressible in the neutral model. The focused emission/VM/pan
proof above includes this deletion.

`LevelScale::linearFromMidi14` also has no production consumer. Its sole test
use generated input for the inverse roundtrip. Remove five production lines;
the existing exhaustive fixture now computes its normalized-square input
itself and keeps testing `midi14FromLinear`. The 7-bit inverse and live 7/14-bit
output conversions stay. All 16,384 valid input values produce exact matching
input double bits and MIDI output bytes in before/after Release and focused
sanitizer arms: 163,840 bytes, SHA-256
`1732ec421fe76b251185de28ae45e9d62a2b30b674cc31242be4146a872d31d7`.
All 19 existing SequenceModel fixtures pass in all four arms, and six actual
Debug/Release/RelWithDebInfo production strict checks pass without warning
suppression. The changed inline helper/existing inline calls and test unit are
instrumented; linked unchanged core/fmt archives remain frozen Release.
Evidence: `/tmp/vgmtrans-cleanup2/unused-midi14-inverse36/` and
`/tmp/vgmtrans-cleanup2/unused-pan-hertz36/`.

### Borrow immutable pitch-layer events inside the MIDI renderer

The private `PitchBendLayers` map now stores pointers to the already-owned
immutable planned events instead of copying 72-byte events into every map node.
The planned track lives through the final pitch flush and renderer destruction;
its vector is complete before references are taken. The sole private apply
caller passes those const elements. Returned MIDI contains values, never these
pointers. One lifetime comment states the owner. This adds one net production
line and removes redundant event ownership without a public lifetime contract.

Before/after Release and focused sanitizer public replays match 94,523 captures,
164,746,078 bytes, SHA-256
`5aa4686f3214786e4350b481ff92442a3fc54029bf9dd4cf89c0229afaa43331`.
These include complete existing MIDI renderer/pitch/modulation suites, layered
replacement/erasure, sensitivity reinterpretation, all three pitch policies,
both modulation policies, copy/move/repeat/temporary owners, final flush, and
post-activation exception exits followed by independent valid retries. Separate
private tests compare an independent owning-map oracle: 7,776 sequences,
933,120 observations, 26,593,920 bytes, SHA-256
`bb63f38b59bcb26ebfb05f737a3579dc8a4b5c3db90c8d11c287333fbd3b6480`.
They compare result bits and FENV flags in four rounding modes, including
nonfinite values only at the private floating query (never MIDI integer casts).
No broad public FENV parity claim is made. Six actual three-configuration strict
checks and three final live checks pass unsuppressed; copied fixture-only unused
helper/range-loop warnings are suppressed only in the temporary fixture arms.

Actual libc++ 170006 node requests change from 112 to 48 bytes, saving 64 bytes
per active layer with the same request count. Fourteen scoped cases cover empty,
replacement/clear, and one through 4,096 layers; 4,096 nodes request/peak
458,752 to 196,608 bytes. Inputs are outside accounting. This is allocator
traffic/peak owned bytes, not RSS, timing, or cross-platform layout evidence.
The sanitizer arms instrument the complete changed renderer plus planner,
resolution, emitter/model, probes and full three fixtures; remaining matching
frozen archives are Release with consistent container-annotation opt-out.
Evidence: `/tmp/vgmtrans-renderer-pitch-layer-borrow36/results.json`, SHA-256
`69c567a7e0d5521da7e2a9755ed66d9f6a3ca77a3858ed510e41a7ead948d6a9`.

### Consume AKAO's existing sorted requirements directly

Remove `AkaoSequenceAnalysis.requiredArticulations`, which parsing never fills.
Collection construction uses `AkaoInstrumentSetBuild.requiredArticulations`
directly, retains the module's removal of zero, and deletes another sort/unique
pass and unused include. The sole build origin already assigns from its ordered
set, including IDs with no bound region. Keep the build's separate binding and
requirement outputs: they answer different questions. Source annotation counts
remain before zero filtering. Six production lines disappear.

The changed analysis struct shrinks 168 to 144 bytes; the parse result shrinks
640 to 608 bytes on this target. All six AKAO implementation units plus format
registration rebuild with matching headers in every overlay. All four Release
and focused sanitizer arms match 2,688 builder plus 576 actual module-scan
cases: 24,796,023 bytes, SHA-256
`df9b8d476634ab967a14114cb8b2b519ec47e221ce0a5687ab96cf90fae75793`.
The full AKAO fixture passes in all four arms, as do 48 actual configured
three-configuration strict checks. Six FF7/FF8/FF9 native closures match
30,366,414 bytes per variant across images/maps/collections, retained inspection,
and both-policy MIDI/SF2/DLS outputs after original session death.

Scoped allocation traffic is unchanged: all 3,264 generated cases total
588,555 requests/133,431,900 bytes per arm. Direct native module and session
scopes total 287,810 requests/129,884,758 bytes per variant. Struct storage and
responsibility reductions are established; allocation/RSS/speed savings and
arbitrary bad_alloc-stage equivalence are not claimed. Changed AKAO/registration,
source/session, probes and full fixtures are instrumented in focused sanitizer
arms; other linked matching frozen units are Release.
Evidence: `/tmp/vgmtrans-cleanup2/akao-analysis-owner36/proof-summary.json`,
`native-proof.json`, `strict-results.json`, and `applied.json`.

### Integrated verification and next probes

The exact seven-path source/test authority builds in 172 warning-free Release
steps, and all 44 CTest suites pass. Complete source/header/test copies and four
archives are frozen in `/tmp/vgmtrans-cleanup2/pass36-baseline` with parent
`c2deaecc6907224215eb0b50079897d2c9c5e1d9`. The frozen shell SHA-256 is
`8f5e2be50d154ef5b36234544792f09d76c8142c4f5098970cf854675c7e5c3a`.
Against frozen35, the original corpus preserves all 252 input/policy records
  and 3,922 artifact names/hashes.
The /Volumes/emu/vgm replay preserves all 73 raw inventories and 894 export
  records: 852 successes and 42 matching existing failures. All 858 artifact
  names/sizes/hashes and 218,240,549 bytes per binary match. Five unsupported
  DSF inventory failures retain status/diagnostics. No ID normalization or fresh
  scheduling repetitions are needed. Real coverage remains 37/38 registered
  families; KonamiTMNT2 has synthetic coverage only.

Next isolated proofs examine private PSF reserved-data borrowing through
recursive synchronous library loads, a separate pre-existing PSF offset-width
bug at the 4 GiB boundary, playback title ownership, redundant region counting,
and smaller source-author/typed-adapter surfaces. A sparse mapped parser input
can exercise the offset boundary without physically copying 4 GiB. These are
future candidates, not changes certified by this batch. Retained public owners
include ScanResultBuilder's SourceFile, TempoMap's returned point vector, and
copied RegionResponse callable state; replacing them with borrowed views would
change valid lifetime or mutable-callback behavior.

## Cleanup2 thirty-seventh batch: private PSF views and smaller authoring surfaces

This batch removes seven net production architecture lines with no test edits.
It simplifies private ownership and format-author operations, while keeping
SequenceVM, typed factories, source inspection and neutral export preparation.
Some ownership changes are LOC-neutral; region count retention adds one line.
Cumulative value reduction is 1,187 lines (1,064 architecture/123 formats):
2,077 additions/3,264 deletions across 162 tracked value C++/header paths.
Architecture totals are 1,709/2,773 across 91 paths; formats remain 368/491
across 71. Test totals remain 984 additions/424 deletions across 43 paths.

### Borrow private PSF reserved bytes during synchronous extraction

`PsfData.reserved` is a span into the immutable parser input rather than a
vector copied from it. Only PSF2 unpacking consumes this section. Its private
owner, either the extraction ByteReader or a temporary library byte vector,
lives through the entire synchronous load/unpack call and recursive library
loads. `load(parsePsf(readFileBytes(...)), ...)` completes before the temporary
vector dies. Nothing returns or stores PsfData/reserved views. Executables,
tags, filesystem entries, output bytes and metadata remain owned values.
The two substitutions are LOC-neutral; PsfData shrinks 80 to 72 bytes here.

All four before/after Release/focused sanitizer arms match 1,318 generated
cases, 472,341 bytes, SHA-256
`cc71714364e613e0af398d161f03ed9f6bc136c548b487c5bbc5913c30932185`.
These exercise reserved data in all supported PSF versions, malformed sizes,
CRC/tags, PSF2 nested/case overlays, empty files and names, INI, primary/numbered
library ordering/gaps, recursion limits and wrong libraries. Complete existing
SquarePS2/SonyPS2/MP2k/SegSat/NDS suites pass. Seven natural archive closures
cover versions 1/2/0x11/0x22/0x24 and the unchanged unsupported DSF 0x12 case:
33,743,861 exact output bytes per variant. No native 0x25 input was found;
that version remains synthetic-only in this focused probe. Returned metadata,
paths/attributes and payloads are examined after stores/metadata/buffers die
or are overwritten. Forty-two actual three-configuration strict checks pass.

Scoped generated allocation calls fall 19,223 to 17,402, requested bytes
105,471,214 to 14,788,684. No request count, traffic or owned-byte peak grows;
775 cases reduce peak, by at most 1,052,910 bytes. Retained payload is exact.
These are scoped allocator observations, not process RSS/speed/cross-platform
layout claims. Removed reserved-vector allocation can change injected bad_alloc
staging; arbitrary allocation-failure equivalence is not claimed. Focused SAN
instruments the changed extractor, probes/full fixtures and matching
Source/Session; remaining frozen36 units are Release with consistent container
annotation opt-out. Evidence:
`/tmp/vgmtrans-cleanup2/psf-reserved-owner37/proof-summary.json`,
`native-proof.json`, `proof-report.md`, and `applied.json`.

### Correct the PSF executable offset's pre-existing integer-width bug

This is a separate correctness patch, not part of the reserved-view equivalence
claim. Widen `reservedSize` to size_t before adding the 16-byte header. The old
addition occurred in 32-bit unsigned arithmetic, despite assigning to size_t;
reserved sizes starting at 0xfffffff0 could wrap the executable offset while
the preceding u64 file-bounds admission succeeded. It is one LOC-neutral edit.

The separate ordinary borrowed-before/borrowed-fixed proof preserves all
1,318 cases, allocation metrics and five full platform suites in Release/SAN.
A sparse anonymous mapping exercises the actual parser: 4,294,967,401 bytes,
reserved=0xfffffff0, real compressed executable beginning at 0x100000000.
Only the header and executable/tag tail are touched; no owning-reserved variant,
SourceStore copy or physical 4 GiB vector is used. The old offset is zero and
produces `PSF executable CRC32 mismatch`; the widened offset yields the exact
32-byte owned image, title and origin after munmap. Both Release/SAN arms agree
per version. Six actual three-configuration strict checks pass. A separate
55-case arithmetic grid includes 20 wraps. This is a targeted macOS parser
boundary correction, not portable mmap/RSS or owning-4GiB-baseline evidence.
Evidence: `/tmp/vgmtrans-cleanup2/psf-exe-offset37/proof-summary.json`,
`proof-report.md`, `candidate.patch`, `combined-baseline.patch`, `applied.json`.
Final PsfExtractor SHA-256:
`38dac0f3fd16d4c2ae69777e5d59f4f0272e9bee982c5cf5353d00b2f6830da4`.

### Remove unused instrument-event authoring and predicted note index scratch

The emitter's event-shaped instrument overload had no external source/test
consumer; two ordinary overloads merely forwarded to it. Remove that public
choice and append directly from the live native identity/address overloads.
Five lines disappear. Formats retain identity/address, forced bank selection
for addresses, and envelope modes. Raw neutral InstrumentPerformanceEvent and
resolved handles remain in the model/export stages. This deliberately narrows
unused authoring of an already-prepared InstrumentHandle and identity combined
with forceBankSelect=true; it is not a claim that the scalar overloads express
every theoretical variant field combination. Handles belong to preparation.

Separately, noteOn records the actual last event after note returns instead of
predicting its index beforehand. Successful note appends exactly one event as
its final operation; its automation updates append no intervening event and
there is no callback/hook. This removes one scratch local/line. The existing
same-key slot assignment and its batch36 failure boundary remain.

Independent focused sanitizer proofs cover 1,270 generated emission/VM/pan
cases plus all nine relevant core/five native suites, 5,298,430 successful trace
bytes. The instrument change adds 72 identity/force/envelope/clock/binding
cases (374,270 bytes), yielding 1,342 generated cases/5,672,700 bytes including
fixtures. Release allocation/failure probes remain exact: instrument612/14
cases, index180/8 cases. There are no new failure-staging differences relative
to frozen36. Twenty-four actual configured Release strict checks pass, and
seven unchanged consumers have identical text. Instrument alone removes 16
emitter text bytes; last-index alone adds 16. No combined text, linked app size,
allocation saving or speed claim is made. Changed emitter/probes/full fixtures
are instrumented in the focused SAN replay; other matching36 archives remain
Release with consistent annotation opt-out. Evidence:
`/tmp/vgmtrans-cleanup2/emitter-authoring37/proof.json`,
`instrument-final.diff`, `index-final.diff`, and `applied-files.json`.

### Cast song-wide state only when playback consumes it

CompiledCommandRuntime previously any_cast program state before deciding which
Playback constructor existed. Move the cast into the constructor branch that
actually borrows ProgramState. Context-only playback no longer validates an
unused slot. This removes one local/line and makes the optional dependency
visible where it is used. Constructor precedence, factories/config ownership,
stream/track validation, typed projection and finishPrepass/finalize callbacks
remain. Collection runtime-family checks still use execute-function identity.

The focused SAN replay matches 960 valid-state lifecycle cases, including 128
caught constructor throws; full VM/Cursor and AKAO/Mori(main+SFX)/SquarePS2/
SegSat suites match 2,801,447 trace bytes. Release separately matches all 960
lifecycle/family cases, including the same context-only Playback with different
unused ProgramState types retaining distinct runtime-family identities.
Twenty configured Release strict checks pass; six paired negative factory
compilations retain rejection. Six actual format object text sizes sum to 560
bytes smaller; this is not a whole-binary, heap or speed claim. Changed adapter
instantiations, probes and full fixtures are instrumented in focused SAN;
remaining matching36 archives are Release.

Fourteen malformed-state cases and four typed-final-hook negative calls per
version record deliberate boundaries. Context-only playback accepts bad/absent
unused ProgramState when no later typed callback consumes it; track/stream
casts now precede a requested program cast; direct ContextOnly,void execution
can instantiate without an unused void reference, while makeCompiledRuntime
with void remains rejected by its state factory. Invalid track/stream and
requested typed state still throw before callbacks mutate state. These are
asserted contract changes, not blanket invalid-state equivalence. Evidence:
`/tmp/vgmtrans-cleanup2/conditional-state-cast37/proof.json`,
`final.diff`, `strict-results.json`, and `applied-files.json`.

### Move playback title to its final owner and retain the sampled region count

PreparedCollection's local baseName now moves into playback.title; the later
owning synth input copies from that final title. No public borrowed string or
new owner is introduced; other collection export naming remains separate.
This is LOC-neutral. All four Release/focused SAN streams match 12,386,931
bytes, SHA-256
`289b3e18ab0f1551fdfd986b6537b1789192fdd9e839a23fc4aae1a3455be8fb`.
The proof includes 8,512 generated playback calls and 8,512 returned copy/move
captures after source/snapshot destruction, full Collection/Sequence/
ResolvedInstrument/Session suites, 8,650 public calls and 80 artifact captures.
Among 70 scoped allocation rows, 42 save exactly one allocation and 28 remain
unchanged; <=22-byte titles and binding failures are unchanged. A 65,554-byte
title saves 65,568 requested bytes. Peak/RSS/timing and arbitrary title-copy
bad_alloc-stage equivalence are not claimed.

Region sampling now keeps the current count beside the step that produced it,
rather than recomputing step1 and the final warning count. This adds one line;
there is no new wrapper/type/policy. All four streams match 12,256
step/budget/error/warning cases plus 56 public/full SF2/DLS captures:
16,727,892 bytes, SHA-256
`8d00bbd5012bb05bc596b424f26ff8531d46ac2ce9e551edce5fc9fcd3414868`.
Budgets cover exact/over/coarsest/max0, static-only oversized banks, invalid
ranges and existing diagnostic prefixes. Counting never invokes response
callbacks; public exports separately do. Both changed complete TUs/probes/full
fixtures are instrumented in focused SAN, remaining matching36 units Release.
Each candidate passes six actual three-configuration production strict checks
without suppression. Evidence: `/tmp/vgmtrans-playback-title-owner37/` and
`/tmp/vgmtrans-region-count-owner37/` results/applied records.

### Remove the unused scanner source-ID getter

Delete the public source() convenience getter and use input_.source.id for its
sole internal caller, preserving owning SourceFile and metadata-ID semantics.
The initial dot/member-pointer inventory missed that bare self-call; a complete
bare/this/qualified/chained inventory corrected the draft before compilation
or mutation. No external consumer exists. One line disappears across two files.
The 256-case metadata/reader identity matrix matches all four Release/SAN arms:
18,616 bytes, SHA-256
`fab1fdfc6bc173be2f98f000db9a5330a602d483c1eaafbbe42cd8bfba5f1fd5`.
Full Registry/Session/SourceMap/SynthBuilder/Qt scan-inspection fixtures and 96
configured strict consumer checks pass. Standalone Qt emits identical raw
326-byte locale fallback/missing bundled icon warnings in all four arms; probe
and core stderr are empty. No normalization hides these fixture warnings.
Changed builder/probe/fixtures and matching SourceMap/Inspection/Source/
RecordReader/Session/SynthBuilder are instrumented; other matching core/Qt
archives/frameworks remain Release. Evidence:
`/tmp/vgmtrans-cleanup2/scanner-source-getter37/proof-summary.json`,
`complete-source-inventory.txt`, `proof-report.md`, and `applied.json`.

### Integrated verification and next checks

The exact eight-path production authority builds in 295 warning-free
Release steps, and all 44 CTest suites pass. Complete source/header/test copies
and four archives are frozen in `/tmp/vgmtrans-cleanup2/pass37-baseline` with
parent `0f6e509716882b049de07bee874b74b4f60f945b`. Frozen shell SHA-256:
`ee32d89dd8111f00817f942d2c787b261dd425925e09047e3d1ff922ddb91727`.
Against frozen36, the original corpus preserves all 252 input/policy records
  and 3,922 artifact names/hashes.
The /Volumes/emu/vgm replay preserves all 894 export records: 852 successes
  and 42 matching existing failures. All 858 artifact names/sizes/hashes and
  218,240,549 bytes per binary match. Seventy-two raw inventories match exactly,
  including five unchanged unsupported DSF failures. Onimusha2/053.psf2 differs
  only in pool IDs0/1 and the matching member reference: source metadata/IDs/
  parentage, bank/sequence IDs2/3, collection0, asset order/names/ranges, status
  and both ordered Sony warnings are unchanged. The existing narrow declaration/
  member-ID mapping compares four current initial/automatic-repeat records to
  retained original scheduling evidence; canonical SHA-256 is
  385c5d5905f4791847f2a86635c70b0aefbdcdd66dcbb488d1ff2a95a3f231a1.
  No broader normalization or new additional repetitions are used; raw records
  and diffs remain. Real coverage remains 37/38 families; KonamiTMNT2 is synthetic
  only. See volume-coverage27/pass36-vs-pass37-summary.json and the matching
  volume-35/classification.json.

Next work includes private primary-library tag borrowing and a separately
reproduced MIDI clipping-before-integer-conversion correction. The temporary
MIDI draft preserves 873,184 ordinary helper observations and 376 public
captures/all three existing MIDI suites; a public regression fails before and
passes after. Its authority must refresh to frozen37 before application. Large
finite source values currently reach unsafe integer casts; derived NaN from
extreme interpolation/LFO arithmetic remains a distinct future scope. Existing
LevelScale final integer clamps stay because its public inputs do not enforce
a NaN policy. Source/tempo point owners and copied mutable RegionResponse
callables remain; reducing them would change valid behavior.

## Cleanup2 thirty-eighth batch: narrow modulation dependencies and clip MIDI before conversion

Parent is `ac4020692`, the committed thirty-seventh batch. Seven production
paths add 17 lines and delete 13: net **+4 architecture lines**, with no format
changes. The existing MIDI renderer fixture adds 35 lines. The cumulative
production reduction is **1,183 lines**: 1,060 architecture and 123 format lines.
Cumulative C++/header totals are 2,094 additions/3,277 deletions across 165 value
paths; architecture is 1,726/2,786 across 94 paths and formats 368/491 across 71.
Tests are 1,019 additions/424 deletions across 43 paths. The small line increase
buys explicit, narrower dependencies; reducing conceptual coupling is preferable
here to keeping fewer declarations through a broad include.

### Borrow the immutable primary PSF library tag

`primaryLibName` returns the selected string's address instead of allocating an
optional copy. Both actual callers consume it synchronously while their local
`PsfData` owns the immutable tags, including through recursive library loading.
Missing tags remain distinct from present-empty tags. `tryOpenLibrary` still
owns the string it normalizes; returned extracted values own their payloads.
The three substitutions are line-neutral and introduce no public borrowed API.

Against matching frozen37, 1,942 cases preserve four Release/focused-SAN traces
of 1,470,687 bytes, SHA-256
3019096193d63156d324e555ed7e3162173280961e0d28c7f85104de34d9dfe9.
Absent/empty/SSO/long/duplicate/backslash tags, ordered and recursive libraries,
immutable inputs, retained output after input destruction, and all five complete
SquarePS2/SonyPS2/MP2k/SegSat/NDS fixture suites pass. Forty-two actual configured
compiler checks pass with Werror in Debug, Release and RelWithDebInfo. Eight
natural archive closures preserve 34,798,705 captured bytes per variant after
input-store destruction; these focused captures do not independently establish
export parity.

Scoped generated C++ allocation requests fall from 30,247 to 29,971 and requested
payload from 20,350,996 to 20,140,372 bytes. No measured case grows; 153 peaks fall,
by at most 4,112 bytes, and retained output is exact. Among natural cases NiGHTS
saves one 32-byte request and 32 requested/live peak bytes; the other selected
closures are unchanged. The private return representation changes from 32 to
8 bytes, while PsfData remains 72. Removing the copy removes an allocation-failure
site; arbitrary bad_alloc staging equivalence is not claimed. Instrumented
extractor/probes/native/five fixture TUs and Source/Session link other matching
frozen37 Release objects, with consistent libc++ container-annotation opt-out.
Scoped counters exclude allocator metadata, aligned allocation and zlib malloc;
there is no whole-program, Windows, process-RSS or timing claim. Evidence:
/tmp/vgmtrans-cleanup2/primary-lib-name-owner38/proof-summary.json and applied.json.

### Keep modulation analysis headers independent of sequence execution

SequenceModulationProfile.h now includes the physical ModulationModel and
explicit optional, with forward declarations for its sequence/event parameters.
ModulationAnalysis.h needs only optional and forward declarations. Their cpp
files explicitly include the complete PerformanceModel they consume. No body,
public signature, layout, validation or ownership changes. The four include-only
paths add five physical lines and require no consumer include repairs.

Local quoted-header closure shrinks from 14 project headers/2,322 lines to
2/114 for SequenceModulationProfile, 14/2,313 to 1/29 for ModulationAnalysis, and
17/2,554 to 8/642 for ModulationScaling. These are dependency inventories, not
compile-time measurements. All 156 actual configured checks pass with empty
output: 23 translation units plus three self-contained headers, before/after in
Debug, Release and RelWithDebInfo. Consumers include value production, legacy
modulation, core/native fixtures, parity and Qt. The initial temporary draft's
missing ModulationPerformanceEvent declaration was caught and corrected before
application; all final checks use final sources. The final include order was
checked in each configuration. A separate one-line SynthValidation pan comment
removes a false claim that performance pan is validated elsewhere; validation
policy is unchanged and one comment line disappears. Evidence:
/tmp/vgmtrans-modulation-header-boundary38/results.json, configured-werror.json,
include-closure.json and applied-files.json. No standalone runtime or sanitizer
claim is attached to this include/comment change.

### Clip target ranges before narrowing MIDI values

The private data7/data14 helpers, physical pitch-wheel conversion and coarse
RPN tuning now clamp the floating value to the MIDI target range before integer
conversion. This removes redundant narrowing operations and fixes a reproduced
bug: large finite source pitches/transposes could wrap to the wrong note, large
wheel/controller amounts could lose their limit, and coarse tuning could perform
an undefined floating-to-int conversion. Four expressions change, with no net
production lines. Earlier arithmetic order and the zero wheel-range return stay.

The matching frozen37 proof preserves 873,184 defined-domain helper observations
per arm across four floating rounding modes, including half/nextafter boundaries
and ten pitch ranges. An independent floor/fraction/copysign oracle checks
half-away-from-zero rounding. It also preserves 376 public encoded MIDI captures
and diagnostic messages plus all three complete MIDI renderer/pitch/modulation
fixture suites. All four ordinary traces are 1,753,572 bytes with SHA-256
943e924a2b0f2987ff85b47aca55a232a3ef80595c8850f0809084be99aa3efb.
Six actual configured production Werror compilations pass across all three
configurations, retaining their flags without added warning suppression.

Thirteen deliberately large finite public cases cover notes, signed int
transpose endpoints, coarse tuning, bends, finite bend arithmetic overflow and
native portamento time. The old Release implementation matches 3/13 expectations;
old focused SAN matches 2/13 and aborts on two coarse tuning cast cases. The
candidate matches 13/13 in both modes with empty stderr. The new permanent
fixture in ValueMidiRendererTests fails before in Release and SAN, then passes
after in both; its expected note/wheel/RPN/portamento limits are explicit. All
22 ordinary compile/link/run matrix statuses pass. Initial temporary harness
repairs (missing MIDI exporter declaration and a rounding-mode-dependent oracle)
are retained separately; the final four arms were rebuilt against frozen37.

The full renderer plus planner/resolution/emitter/model/probes and all three
fixture TUs are instrumented in focused SAN; remaining frozen37 objects are
Release with consistent container-annotation opt-out. There is no full-app or
cross-platform sanitizer claim. Finite intermediate arithmetic can still
overflow before clipping, preserving its earlier floating flags. Extreme
interpolation or LFO arithmetic can still derive NaN, which clamp passes through;
this is not a whole-finite-IR safety policy or broad FENV parity claim. No timing,
allocation or RSS claim. Evidence:
/tmp/vgmtrans-cleanup2/midi-numeric-boundaries38/refresh37/proof.json,
extreme-results.json, regression-results.json and applied.json.

### Integrated verification

- Complete Release build: 92 steps, no warnings; all 44 CTest suites pass.
- Frozen38 contains matching complete value headers/sources, tests and four
  archives; executable SHA-256 `d6a7ef9314539301c34533a329b467e1536062427a660aef7ce2bfac8f7fcb53`.
- Against frozen37, the original corpus preserves all 252 input/policy records
  and 3,922 artifact names/hashes.
- The /Volumes/emu/vgm replay preserves all 894 export records: 852 successes
  and 42 matching existing failures. All 858 artifact names/sizes/hashes and
  218,240,549 bytes per binary match. Seventy-two raw inventories match exactly,
  including five unchanged unsupported DSF failures. Onimusha2/053.psf2 differs
  only in pool IDs0/1 and the matching member reference: source metadata/IDs/
  parentage, bank/sequence IDs2/3, collection0, asset order/names/ranges, status
  and both ordered Sony warnings are unchanged. The existing narrow declaration/
  member-ID mapping compares four current initial/automatic-repeat records to
  retained original scheduling evidence; canonical SHA-256 is
  385c5d5905f4791847f2a86635c70b0aefbdcdd66dcbb488d1ff2a95a3f231a1.
  No broader normalization or new additional repetitions are used; raw records
  and diffs remain. Real coverage remains 37/38 families; KonamiTMNT2 is synthetic
  only. See volume-coverage27/pass37-vs-pass38-summary.json and the matching
  volume-35/classification.json.
- The eight approved source/test hashes and exact diff remain held through build,
  freeze and replay. Live source changes are excluded during these operations;
  independent future drafts compile only against frozen owners. All raw proof,
  commands, statuses, comparisons and application records remain under /tmp.

### Continuing audit

A direct map lookup at each of two PSF loader boundaries may make the private
primary-library wrapper unnecessary; a separate frozen38 draft is being tested.
An unrestricted std::lerp replacement was rejected. It fixes finite overflow and
missed endpoints, but a mixed-sign interior boundary matrix finds 93 valid cases
with 186 MIDI policy outputs one wheel step farther from the exact rational
oracle; the old expression is correct in these cases. All natural exports remain
exact, which does not erase this public regression. Initial/dyadic matrices and
raw generated, arithmetic and archive results are retained in
/tmp/vgmtrans-cleanup2/pitch-linear-interpolation37. A narrower endpoint-only
correction is being drafted separately; it is excluded from this batch. Public NaN policy, required
source/tempo owners, mutable RegionResponse copies, used-instrument filtering and
source command flow remain distinct responsibilities; their guards or owners are
not deleted solely to reduce lines.
