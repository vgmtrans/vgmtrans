# Value-Oriented Rewrite Plan

This document describes the next design target for the value-oriented rewrite in
`src/value`. The current proof of concept has useful pieces: value assets,
explicit ownership, source ranges, diagnostics, deterministic registration,
export services, CapcomSnes support, and NDS support. It also exposed a serious
ergonomics problem in the sequence layer.

The current sequence design parses driver bytecode into a shared generic
`Command` variant, then restores real driver behavior later through a
`MidiSequenceProfile`. That makes format code feel like adapter code. For one
CapcomSnes opcode, the reader has to jump between byte decoding, generic command
definitions, MIDI profile hooks, display defaults, and profile registration.

The new target is:

```text
Source bytes
  -> format-local typed source commands
  -> erased value records in SequenceProgram
  -> SequenceVm execution through the registered dialect
  -> target-neutral PerformanceSequence
  -> MIDI / analysis / future exporters
```

Format authors should be able to read a command definition and understand the
source driver: operands, state changes, UI description, control-flow behavior,
and musical meaning should live together.

## Goals

- Keep scan output as value-like snapshots with explicit ownership.
- Keep format modules small, readable, and source-driver oriented.
- Avoid pointer-heavy inherited object graphs.
- Preserve source ranges, decoded operands, diagnostics, and HexView links.
- Make parsed assets repeatably exportable without rescanning source bytes.
- Centralize common sequence execution mechanics: jumps, calls, repeats, loop
  analysis, dry runs, command limits, and loop policy.
- Keep registration deterministic and copyable.
- Keep MIDI-specific concerns out of parsed source data.
- Leave a realistic path for non-MIDI exports, including tracker-style exports.
- Preserve current synth progress unless there is a concrete reason to redesign
  it.

## Non-goals

- Do not implement tracker export in this branch.
- Do not build a generic music theory framework that hides source-driver details.
- Do not require every format to implement a large visitor hierarchy.
- Do not make command authoring more abstract than legacy `readEvent()` code.
- Do not rewrite the synth model just for symmetry with the sequence redesign.

## Readability and Comments

The rewrite should be more liberal with explanatory comments than the current
proof of concept. Brief comments are especially valuable where the code is doing
one of these things:

- Mirroring source-driver behavior that is not obvious from the opcode name.
- Preserving a legacy quirk for parity.
- Explaining a driver formula, table lookup, state bit, or timing rule.
- Separating decode flow from playback flow.
- Translating source-driver behavior into target-neutral performance events.
- Making an intentional tradeoff in the value model or export path.

Comments should explain why the code exists or how the driver works, not repeat
the syntax. Prefer a short comment near the relevant command or helper over a
large block elsewhere. If a command needs many comments to be understandable, the
command probably needs to be split or renamed.

Small format modules are also a readability requirement. Shared core machinery
should remove traversal, registration, UI derivation, and export plumbing from
format code, so format files can stay focused on source layout, opcodes, driver
state, and driver math.

## Target Architecture

The project-level pipeline should be:

```text
SourceStore
  -> FormatModule scan
  -> Project
      -> Collection
      -> SequenceProgramAsset
      -> InstrumentSetAsset
      -> SampleCollectionAsset
      -> MiscAsset
  -> ExportService
      -> SequenceProgram + SequenceDialect -> PerformanceSequence -> MIDI
      -> SequenceProgram + SequenceDialect -> future tracker/native export
      -> InstrumentSetAsset + SampleCollectionAsset -> SynthExportData -> SF2/DLS
      -> SampleCollectionAsset -> WAV
```

Format modules produce parsed assets. Export services decide how to lower those
assets for a selected output.

## Core Ownership

`src/value/core` should own:

- IDs, source ranges, byte readers, source storage, and diagnostics.
- `Project`, `Collection`, `Asset`, metadata, and lookup helpers.
- `SequenceProgram`, `TrackProgram`, and erased source-command records.
- `SequenceDialect` registration and command dispatch descriptors.
- `SequenceVm` traversal, loop policy, and diagnostics.
- `PerformanceSequence` and target-neutral performance events.
- `InstrumentSetAsset`, `SampleCollectionAsset`, sample metadata, and decoded
  sample types.
- Registries and orchestration services such as `FormatRegistry`,
  `ScanService`, and `Session`.

`src/value/export` should own:

- MIDI rendering from `PerformanceSequence`.
- Synth export preparation and resolution.
- SF2, DLS, WAV, and future export containers.
- Export policy.

Format directories should own:

- Source layout detection.
- Local command structs.
- Opcode switches or opcode tables.
- Driver state structs.
- Driver math and version-specific behavior.
- Instrument and sample table parsing.
- Small source-specific helpers that are not shared yet.

## Sequence Source IR

Replace the current global `Command` variant as the primary sequence storage
with a source-preserving program:

```cpp
struct SequenceProgram {
  DialectId dialect;
  Timebase timebase;
  SequenceBehavior behavior;
  std::vector<TrackProgram> tracks;
  std::vector<InstrumentRef> referencedInstruments;
};

struct TrackProgram {
  TrackId id;
  u32 sourceTrackNumber = 0;
  Address startAddress;
  std::vector<SourceCommand> commands;
  AddressIndex addressIndex;
};
```

`SourceCommand` is an erased value record. It is not an executable object and it
does not have a vtable.

```cpp
struct SourceCommand {
  CommandId id;
  CommandHandlerId handler;
  CommandKindId kind;
  u8 opcode = 0;
  SourceRange range;
  ByteSpan bytes;
  OperandSpan operands;
};
```

Implementation note: avoid one heap allocation per command. Store command bytes,
operands, strings, and presentation data in track-level or program-level pools.
Use small inline storage only where it is demonstrably simpler. A
`std::vector<u8>` and `std::vector<Operand>` inside every command should be a
transitional implementation, not the final shape.

`CommandKindId` should be backed by a deterministic dialect string table. The UI
can still show names like `capcom.volume` or `nds.notewait`, but VM dispatch
should use `CommandHandlerId`, not string lookup.

## Format-local Commands

Format authors should write local command structs. The common pattern is:

```cpp
struct Volume {
  u8 raw;

  static constexpr CommandKind kind = "capcom.volume";
  static constexpr std::string_view name = "Volume";

  static Volume parse(CommandReader& in) {
    return Volume{.raw = in.u8("volume")};
  }

  void describe(CommandInfo& out, const Context& ctx) const {
    const auto gain = capcom::trackGain(ctx.version, raw);
    out.field("raw", raw);
    out.field("amplitude", gain.amplitude);
  }

  Effects execute(TrackState&, Emit& out, const Context& ctx) const {
    out.level(LevelEvent{
        .scope = LevelScope::Track,
        .gain = capcom::trackGain(ctx.version, raw),
    });
    return Effects::none();
  }
};
```

The parser should remain compact and source-shaped:

```cpp
DecodedCommand decodeCommand(TrackCursor& cur, const Context& ctx) {
  const auto begin = cur.offset();
  const u8 opcode = cur.u8("opcode");

  if (opcode >= 0x20) {
    return (opcode & 0x1f) == 0
        ? cur.command<Rest>(begin, opcode)
        : cur.command<Note>(begin, opcode);
  }

  switch (opcode) {
    case 0x00: return cur.command<ToggleTriplet>(begin, opcode);
    case 0x01: return cur.command<ToggleSlur>(begin, opcode);
    case 0x02: return cur.command<DottedNote>(begin, opcode);
    case 0x04: return cur.command<NoteAttributes>(begin, opcode);
    case 0x05: return cur.command<Tempo>(begin, opcode);
    case 0x06: return cur.command<DurationRate>(begin, opcode);
    case 0x07: return cur.command<Volume>(begin, opcode);
    case 0x08: return cur.command<Program>(begin, opcode);
    case 0x16: return cur.command<Jump>(begin, opcode);
    case 0x17: return cur.command<End>(begin, opcode);
    case 0x18: return cur.command<Pan>(begin, opcode);
    case 0x1a: return cur.command<Lfo>(begin, opcode);
    default:   return cur.command<UnknownOpcode>(begin, opcode);
  }
}
```

This gives format code the same local readability as legacy `readEvent()`, while
stored scan output remains a plain value snapshot.

## Command Decode and Replay

Use one pure `parse(CommandReader&)` per command.

The decode path reads source bytes, records command bytes, records decoded
operands, and assigns the command handler:

```cpp
template <class Command>
DecodedCommand TrackCursor::command(u32 begin, u8 opcode) {
  RecordingCommandReader reader{*this, begin, opcode};
  Command value = Command::parse(reader);

  SourceCommand record{
      .id = nextCommandId(),
      .handler = handlerIdFor<Command>(),
      .kind = kindIdFor(Command::kind),
      .opcode = opcode,
      .range = reader.rangeFrom(begin),
      .bytes = reader.byteSpan(),
      .operands = reader.operandSpan(),
  };

  return DecodedCommand{
      .command = record,
      .decodeFlow = Command::decodeFlow(value),
  };
}
```

The replay path reconstructs a stack-local typed command from the stored command
bytes and executes it:

```cpp
template <class Command>
Effects replayTypedCommand(const SourceCommand& record,
                           TrackState& state,
                           Emit& out,
                           VmApi& vm,
                           const Context& ctx) {
  ReplayCommandReader reader{record.opcode, record.bytes};
  Command value = Command::parse(reader);
  return value.execute(state, out, vm, ctx);
}
```

`parse()` must be deterministic and must not depend on scanner-only state.
Version, table layout, and other stable behavior inputs belong in the dialect
context.

## Decode Flow vs Playback Flow

Separate static bytecode discovery from runtime execution.

Decode needs to know which source addresses to decode:

```cpp
struct DecodeFlow {
  std::optional<Address> fallthrough;
  std::vector<Address> staticTargets;
  bool terminal = false;
};
```

Runtime needs to know what happens when playback reaches a command:

```cpp
struct Step {
  enum Kind { Next, End, Jump, Call, Return } kind;
  Address destination;
};
```

This distinction matters. NDS needs graph discovery so calls and jumps expose
reachable command bytes. CapcomSnes may choose a simpler linear-plus-target
decoder at first, but playback traversal and loop policy should belong to
`SequenceVm`, not to the scanner.

## SequenceVm

Core should own sequence execution mechanics:

- Address lookup.
- Program counter advancement.
- Track clocks.
- Jump, call, return, repeat, and repeat-break state.
- Loop detection and first-loop tick analysis.
- Play-once and preserve-loop policy.
- Dry-run modes for loop and stop-tick analysis.
- Command execution limits.
- Source-linked diagnostics.
- Source command to performance event links.

Command execution should return explicit effects:

```cpp
struct Effects {
  u32 advanceTicks = 0;
  Step step = Step::next();
};
```

The `Emit` sink records semantic events. The VM owns the current tick and
advances it from `Effects::advanceTicks`. A command should not mutate the VM
clock directly.

Control helpers must be constrained:

```cpp
class VmApi {
public:
  Step next();
  Step end();
  Step jump(Address destination);
  Step call(Address destination);
  Step return_();

  RepeatResult repeatUntil(u8 slot, u32 count, Address destination);
  RepeatBreakResult repeatBreak(u8 slot, Address destination);

  u64 tick() const;
  void diagnostic(Diagnostic diagnostic);
};
```

Avoid broad opaque custom control callbacks. They make loop policy, dry runs, and
deterministic replay hard to reason about. If a driver needs special behavior,
add a narrow VM primitive with tests.

## Performance IR

`PerformanceSequence` is target-neutral rendered playback. It is not the source
bytecode and it is not MIDI.

Performance events should use musical units:

- Ticks.
- Continuous pitch, such as MIDI semitone coordinates.
- Normalized velocity.
- Linear gain.
- Stereo image or balance.
- Microseconds per quarter.
- Instrument identity.
- Articulation, modulation, envelope, marker, and diagnostic events.

Do not put MIDI-specific fields such as `midi14`, `midiPan`, or MIDI expression
values into performance events. MIDI quantization belongs in the MIDI renderer.

Each performance event should link back to a command:

```cpp
struct PerformanceEventHeader {
  CommandId sourceCommand;
  u64 tick = 0;
};
```

Prefer command links over duplicating raw source operands into every event. The
UI can look up decoded operands from `SourceCommand`. Exporters can still receive
source-aware diagnostics through the event header.

## MIDI Rendering

The MIDI path becomes:

```text
SequenceProgram + SequenceDialect
  -> SequenceVm
  -> PerformanceSequence
  -> MidiRenderer
  -> MidiExporter
```

`MidiSequenceProfile` should be removed as a target architecture. New driver
behavior should move into local command `execute()` methods and shared music
helpers, not through profile hooks.

The MIDI renderer owns:

- Channel assignment.
- MIDI controller quantization.
- MIDI volume, expression, pan, pitch bend, RPN, and tempo encoding.
- Pending note extension for events that request `extendsPrevious`.
- MIDI-specific loop markers.
- End-of-track events.

The renderer may need policy inputs, such as whether to use MIDI controller
events for modulation or rely on synth modulators. That policy belongs to export
requests, not scanners.

## Future Tracker Export

Tracker export should not be forced through flattened MIDI-like events. Some
tracker formats can represent loops, pattern breaks, effect memory, note cuts,
and row effects better than MIDI can.

Allow two future paths:

```text
SequenceProgram + SequenceDialect -> structured tracker/native lowering
SequenceProgram + SequenceDialect -> PerformanceSequence -> flattened exports
```

The source command model must therefore preserve command identity, source ranges,
operands, and control-flow commands. The performance model is useful, but it
should not be the only export input.

## HexView and Interactive UI

Do not build a parallel command UI structure manually during sequence parsing.

The sequence UI should derive command rows from `SequenceProgram`:

- Source range.
- Command kind.
- Display name.
- Decoded operands.
- Description fields.
- Diagnostics.
- Performance events emitted from the command.

`ItemTree` should continue to represent asset, header, table, track, instrument,
region, and sample hierarchy. Command nodes can be generated from
`TrackProgram.commands` when a sequence view is opened. If existing UI code still
needs command nodes stored in `ItemTree`, generate them from `SourceCommand`
records in one shared helper rather than hand-building them in format parsers.

Presentation should not be the authoritative model. Store source bytes,
operands, ranges, and event links. Derive display text from the dialect whenever
possible.

## Sequence Dialect Registration

Replace separate scan/profile registration with one sequence dialect descriptor:

```cpp
struct SequenceDialect {
  DialectId id;
  Timebase timebase;
  SequenceBehavior defaultBehavior;
  DecodeOneCommand decodeOneCommand;
  CreateTrackState createTrackState;
  std::span<const CommandHandler> handlers;
};
```

A format module that creates a `SequenceProgram` must register the dialect that
can interpret it. A `SequenceProgramAsset` stores `DialectId`, not a MIDI profile
key.

Builder example:

```cpp
SequenceDialect capcomSnesDialect(CapcomSnesEngineVersion version) {
  return SequenceDialectBuilder<TrackState, Context>("capcom-snes:v2")
      .context(Context{.version = version})
      .timebase(Timebase{.ppqn = kCapcomSnesPpqn})
      .defaultBehavior(capcomDefaultBehavior())
      .decode(decodeCommand)
      .commands<
          Rest,
          Note,
          ToggleTriplet,
          ToggleSlur,
          DottedNote,
          NoteAttributes,
          Tempo,
          DurationRate,
          Volume,
          Program,
          Jump,
          End,
          Pan,
          Lfo,
          UnknownOpcode>();
}
```

Registration must be deterministic. Descriptors should be copyable and should
not hold hidden ownership of parsed data.

## Shared Music Helpers

Avoid a global command vocabulary. Share boring math and units instead.

Good shared helpers:

- Duration math: dotted, triplet, fractional gate.
- Tempo conversion.
- Linear gain and dB conversion.
- Pan and stereo-image conversion.
- Pitch and tuning helpers.
- Event builder helpers.
- Small optional mixins for repeated command families.

Bad shared helpers:

- A generic `VolumeCommand` whose behavior depends on a profile.
- A generic `DriverSpecificCommand` that hides known source commands behind
  strings.
- Mixins that make command definitions harder to read than hand-written code.

## Synth Handling

The synth side does not need the same redesign as sequences right now.
`InstrumentSetAsset`, `Instrument`, `Region`, `SampleCollectionAsset`, `Sample`,
and `DecodedSample` are a reasonable value-oriented foundation. They already
separate parsed source assets from export-stage resolution through
`SynthExportData`.

Keep the current synth model as the first target, with these rules:

- Parsed synth assets remain source-oriented value data.
- Samples keep encoded data in `SourceStore` through `SourceRange`; exporters
  decode samples when needed.
- Regions keep source ranges and source sample references.
- Envelopes, tuning, pan, attenuation, reverb, generators, and modulators stay
  normalized enough for SF2/DLS sharing.
- Preserve raw driver fields when they are needed for UI, diagnostics, parity,
  or future exports.
- Do not branch in format parsers based on SF2, DLS, WAV, MIDI, or tracker
  export choices.

If raw synth information starts getting lost, add explicit source metadata rather
than replacing the whole synth model. For example, SNES ADSR/Gain bytes can be
preserved beside normalized `Envelope` data if the UI or parity tests need to
show those exact bytes.

## Synth Export Preparation

Keep export-stage synth preparation outside parsed assets:

```text
InstrumentSetAsset + SampleCollectionAsset + SourceStore
  -> decodeSynthSamples()
  -> resolveSynthInstruments()
  -> SF2 / DLS / tracker-instrument exporters
```

`SynthExportData` currently fills this role:

- Decode encoded samples to PCM16.
- Build a flat export sample index.
- Resolve region sample references.
- Report diagnostics for missing samples, invalid loops, unsupported codecs, and
  unsupported generator/modulator mappings.

Do not introduce `ResolvedInstrumentSet` or `SynthSet` until helper functions
stop being enough. If the resolved view grows stateful or needs to be reused by
many exporters, add a named resolved model then.

## Synth Modulation and Sequence Interaction

Sequence modulation and synth modulation are connected, but they are not the
same model.

Parsed synth assets should describe what an instrument can do:

- Base generators.
- Synth modulators.
- Envelope behavior.
- Region placement.

Sequence execution should describe what the driver actually sends during
playback:

- Vibrato depth/rate events.
- Tremolo depth/rate events.
- Portamento and articulation events.
- Observed controller ranges.

Export policy decides how to combine them:

- Full theoretical format range.
- Observed sequence range.
- Future policy: prefer synth modulators.
- Future policy: prefer explicit MIDI events.

The current `ModulationScalingPolicy` can stay, but it should eventually consume
observed ranges from `PerformanceSequence` analysis rather than from
MIDI-specific events.

## Format Implementation Targets

CapcomSnes and NDS should remain the proving formats.

CapcomSnes validates:

- Packed note/rest opcodes.
- Mutable note state.
- Slur/tie and pending-note extension.
- Portamento state.
- Pan and volume curves that vary by driver version.
- Repeat slots and repeat-break side effects.
- LFO commands where one opcode has subcommands.
- Source ranges for SNES RAM addresses.
- Instrument tables, BRR samples, ADSR/Gain, and SF2/DLS export.

NDS validates:

- Graph-style SSEQ decoding.
- Calls, jumps, returns, and track starts.
- Variable-length note operands.
- Driver commands that should no longer be stringly typed
  `DriverSpecificCommand`s.
- SBNK/SWAR synth and sample assets.

## Implementation Separation

The redesign should prefer clean replacement over incremental compatibility.
There is no requirement to preserve the prior value-oriented sequence code except
where it helps us understand behavior or compare parity. Keeping old code around
as reference is useful; mixing old concepts into the new design is not.

Use these rules:

- Put new sequence architecture in new files with new names, such as
  `SequenceProgram`, `SourceCommand`, `SequenceDialect`, `SequenceVm`, and
  `PerformanceSequence`.
- Do not add new behavior to old `CommandSequence`, global `Command`,
  `MidiSequenceProfile`, or profile-registry files unless the change is part of
  deleting or quarantining them.
- Do not generate compatibility `Command` records from new commands as a normal
  migration strategy.
- Do not make new exports depend on old MIDI lowering. Build the new
  `PerformanceSequence` and MIDI renderer path directly.
- It is acceptable to delete prior value-oriented CapcomSnes and NDS sequence
  code once the new implementation has enough reference tests or legacy code can
  be consulted elsewhere.
- If an old file is kept, decide explicitly whether it is reference-only,
  retained synth/export infrastructure, or code intended to survive. Avoid files
  that are half old design and half new design.
- Use namespaces, filenames, or directory boundaries that make old and new
  sequence code easy to distinguish during review.

The synth and export-support files are different. Code such as `SynthModel`,
`SampleDecoder`, `SynthExportData`, SF2/DLS/WAV exporters, `ProjectModel`,
`Source`, and scanning infrastructure may be kept and edited when it still fits
the target architecture. The separation rule is mainly about preventing the old
sequence command/profile model from seeping into the replacement.

## Migration Plan

### Phase 1: Establish The New Sequence Area

- Create the new sequence model files for `SequenceProgram`, `TrackProgram`,
  `SourceCommand`, command IDs, command byte/operand storage, and dialect IDs.
- Keep this code separate from the old `CommandSequence` and
  `MidiSequenceProfile` design while those files exist.
- Delete obsolete profile/lowering files once the new path owns the behavior.
- Add helpers to derive command UI rows from `SourceCommand`.

### Phase 2: Add Dialect Registration

- Introduce `SequenceDialect` and `SequenceDialectRegistry`.
- Register sequence dialects from the same module that registers scan logic.
- Store `DialectId` in new sequence assets.
- Do not route dialects through old profile lookup.

### Phase 3: Build The New CapcomSnes Sequence Parser

- Add `cur.command<T>()` and `CommandReader`.
- Implement CapcomSnes local command structs in new files.
- Start with simple commands: tempo, duration rate, volume, program, pan, master
  volume, end, unknown.
- Use old CapcomSnes value code and legacy `readEvent()` as references, not as
  dependencies.
- Do not emit old generic `Command` compatibility records.

### Phase 4: Add PerformanceSequence, SequenceVm, And MIDI Renderer

- Add target-neutral performance event types.
- Add event-to-command links.
- Add `SequenceVm` early enough that CapcomSnes command execution uses the new
  traversal contract rather than old MIDI lowering traversal.
- Add a MIDI renderer from `PerformanceSequence`.
- Compare rendered MIDI against legacy output and, when useful, against the old
  value POC output. The comparison harness should not require the new code to
  call the old code.

### Phase 5: Move Driver Semantics Into Commands

Implement CapcomSnes behavior in local command structs and helpers in this order:

1. Tempo.
2. Volume and master volume.
3. Pan.
4. Program.
5. Duration and note attributes.
6. Note/rest timing.
7. Portamento.
8. LFO, vibrato, and tremolo.
9. Repeat-break side effects.
10. Reverb, envelope, and unknown/no-op commands.

Do not move this behavior through a compatibility profile first. If old
`CapcomSnesProfile` code is needed for reference, consult it outside the active
new implementation rather than keeping it wired into the build.

### Phase 6: Remove Or Quarantine Old Sequence Code

- Delete old value-oriented sequence files when their behavior is covered by the
  new implementation and tests.
- If deletion is temporarily inconvenient, move old code behind clearly named
  reference or legacy boundaries and prevent new code from including it.
- Remove duplicated MIDI lowering dry-run logic when `SequenceVm` covers the
  behavior.
- Keep static decode graph discovery separate from playback traversal.

### Phase 7: Build The New NDS Sequence Parser

- Implement NDS local command structs in new files.
- Replace NDS string-based driver commands with typed source commands.
- Use decode-flow helpers for SSEQ graph discovery.
- Validate calls, returns, notewait, pitch bend, expression, tempo, and track
  start behavior.

### Phase 8: Delete The Old Command/Profile Architecture

- Delete or fully quarantine `CommandSequence`, the global `Command` variant,
  `MidiSequenceProfile`, and `MidiSequenceProfileRegistry`.
- Remove old value-format registration paths that register sequence profiles.
- Keep only deliberate debug projections, and keep them one-way from the new
  model.

### Phase 9: Revisit Synth Only Where Needed

- Keep current synth model during sequence redesign.
- Add raw/source metadata fields only where tests or UI prove they are needed.
- Promote `SynthExportData` helpers into a named resolved model only if exporter
  sharing becomes awkward.

## Testing Plan

Core sequence tests should cover:

- Command byte and operand recording.
- Deterministic dialect registration.
- Command replay from stored bytes.
- Decode graph discovery.
- VM jump, call, return, repeat, repeat-break, loop, and end behavior.
- Loop policies.
- Command limits and diagnostics.
- Event-to-command links.

Format tests should cover:

- CapcomSnes command decoding by opcode.
- CapcomSnes command execution and state changes.
- CapcomSnes MIDI parity through the new performance renderer.
- CapcomSnes synth and sample export parity.
- NDS command graph decoding.
- NDS command execution and MIDI parity.

Synth tests should cover:

- Instrument and region parsing.
- Sample table parsing and source ranges.
- Sample decoding.
- Region sample resolution.
- SF2 and DLS normalized export parity.
- Modulation scaling with full-format and observed-range policies.

## Review Checklist

Before merging the redesigned value pipeline, verify:

- Format modules stay small, readable, and source-driver oriented.
- Non-obvious driver behavior and framework mechanics have brief explanatory
  comments.
- Format modules do not branch on export kind.
- Sequence assets store `DialectId`, not MIDI profile names.
- Source commands preserve source-driver shape.
- MIDI-specific values do not appear in parsed source commands.
- `PerformanceSequence` is not just MIDI with different names.
- `SequenceVm` owns playback traversal and loop policy.
- Static decode discovery is separate from runtime playback flow.
- HexView command data derives from `SourceCommand`.
- Synth parsed assets remain source-oriented.
- Synth export resolution stays outside parsed assets.
- CapcomSnes and NDS parity tests pass or have documented exceptions.
