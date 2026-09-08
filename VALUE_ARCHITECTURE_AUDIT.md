# Value architecture simplification audit

Starting revision: `79f812a54` (`core-rewrite-refactor`).

The goal is to make format implementations and the shared architecture easier
to understand, while retaining SequenceVM, conversion behavior, source
inspection, and the separation between source formats and export targets.
Reductions should remove responsibilities or redundant representations, not
compress code or hide format behavior behind a new framework.

## Baseline and verification

- The value formats contain 71,728 lines across their source and header files.
- The existing `vgmtrans-value-core-tests` suite passes at the starting revision.
- The legacy/value parity harness self-test passes at the starting revision.
- No real-file parity corpus is configured in `cmake-build-debug`, and no game
  corpus was found in the workspace. Synthetic tests cannot establish parity
  for every game; preserve that distinction when assessing these changes.
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

## Further investigation

- Continue auditing export lowering, instrument selection, envelope projection,
  and remaining format-local helpers for redundant state and work.
- SonyPS2 still approximates key/velocity-dependent regions during scanning
  under a 3,000-region budget chosen for SF2 table limits. Moving this policy
  to export needs a source-neutral representation of that response; merely
  renaming the limit would not remove the coupling.
- Real-file parity remains unverified. An optional corpus-path question is
  pending; the absence of a corpus does not block further code investigation.

## Design decisions retained after inspection

- Keep SequenceVM and the compiled command representation. Flattening every
  command into a vector of operations would reintroduce an intermediate
  instruction list and add allocation to simple commands. Most commands need
  only one body; remove forwarding helpers without adding that representation.
- Keep source-driver initialization rules explicit for now. Several apparent
  startup guards initialize a whole song, others initialize one track, and
  Sony PS2 also resets section state. Per-command hooks also perform real
  driver work. A new lifecycle hook needs a stronger benefit than removing a
  few boolean guards.
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

- Keep version opcode tables as direct byte-layout descriptions where the
  versions differ substantially. Factoring short repeated tails into extra
  dispatch rules would trade visible data for more decoding logic.
