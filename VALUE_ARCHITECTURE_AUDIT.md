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

## Further investigation

- Continue auditing export lowering, instrument selection, envelope projection,
  and remaining format-local helpers for redundant state and work.
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
