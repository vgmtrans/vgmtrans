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
