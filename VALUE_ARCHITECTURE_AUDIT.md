# Value architecture simplification audit

Starting revision: `e9f4450c3` (`core-rewrite-refactor`).

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

## Further investigation

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
