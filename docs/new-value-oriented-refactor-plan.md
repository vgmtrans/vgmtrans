The following document provides a spec for a major refactor of the value-oriented rewrite residing in src/value.
The guiding line is:

> **Driver code should look like driver code. The VM should own playback policy. The source map should record what the parser/driver already learned, not become a parallel model of the app.**

This aligns with the principle that the default authoring surface should become an imperative `CommandCursor`/`Flow` API backed by `SequenceVM`, with typed command structs kept only as an advanced escape hatch.

---

## 1. Primary goals

### 1.1 Driver authoring must be simple

A format author should be able to write this:

```cpp
case 0xc4: {
  auto pan = cmd.name("Pan")
                .semantic(SequenceSemantic::Pan)
                .u8("pan");

  rt.pan(PanScale::fromMidi7(pan.value));
  return cmd.next();
}
```

not this:

```cpp
struct Pan {
  static Pan parse(...);
  void describe(...);
  void references(...);
  Effects execute(...);
};

map.op<0xc4, Pan>("Pan");
```

Typed command structs can remain for special cases, but they should not be the normal authoring style.

### 1.2 The VM remains the execution authority

Format code should identify local command semantics:

```cpp
return cmd.jump(destination);
return cmd.call(destination);
return cmd.declaredLoop(destination);
return cmd.countedRepeatUntil(slot, count, destination);
```

But the format driver should not implement global policies for:

```text
call stack behavior
repeat-counter policy
declared loop handling
loop candidate handling
stop-all-tracks-at-first-loop behavior
command budgets
missing target diagnostics
render/export loop policy
```

Those remain `SequenceVM` responsibilities.

### 1.3 Source mapping supports visualization without becoming the app model

The source map should answer practical source questions:

```text
What bytes form this header?
What bytes form this command?
What operands belong to this command?
What command type should HexView color this as?
What pointer target does this field reference?
What instrument/sample/sequence object does this range belong to?
```

It should **not** become:

```text
a universal VGMItem replacement
a UI widget model
a piano roll model
a graph database
a future-proof ontology of all game audio data
```

### 1.4 No `SequenceTimeline` for now

Piano roll is a future view. Do not design the dynamic timeline layer yet.

The current design should still leave good origins behind so a future piano roll can connect events back to source commands, but there should be no `SequenceTimeline`, no piano-roll projection, and no visualization service in the first pass.

### 1.5 No `StaticSequenceMap` yet

For now, use:

```text
SequenceProgram
+
SourceMap
```

If that proves insufficient, add a sequence-specific index later.

Avoid introducing `StaticSequenceMap` until there is a clear missing need.

---

# 2. High-level architecture

The first-pass architecture should be:

```text
SourceStore
  raw source bytes and derived source bytes

Format scanner / parser
  produces semantic assets
  produces lightweight SourceMap annotations

Sequence driver
  written using VmCommandCursor
  produces SequenceProgram source commands
  records command annotations into SourceMap

SequenceVM
  executes SequenceProgram through dialect handlers
  owns playback/control-flow policy

Export / simple UI consumers
  use semantic assets, SequenceProgram, and SourceMap
```

The key flow for sequences:

```text
source bytes
  -> cursor decode pass
  -> SequenceProgram + SourceMap command annotations
  -> SequenceVM render/export pass
```

The key flow for non-sequence data:

```text
source bytes
  -> ParseCursor + SourceBuilder
  -> semantic assets + SourceMap annotations
```

---

# 3. Non-goals for the first pass

Do **not** build these yet:

```text
SequenceTimeline
PianoRoll model
VisualizationService
StaticSequenceMap
large universal SourceRole taxonomy
large universal SequenceSemantic taxonomy
scriptable source queries
persistent UI layout metadata
plugin-defined widgets
```

Do build enough that those things can be added later without undoing the core architecture.

---

# 4. CommandCursor is the center

## 4.1 Basic shape

The primary sequence authoring API should be:

```cpp
CommandFlow readCommand(Runtime& rt, VmCommandCursor& cmd);
```

A driver registers one readable command reader:

```cpp
auto dialect = makeCursorDialect<NdsRuntime>({
  .id = DialectId{"nds-sseq"},
  .displayName = "Nintendo DS SSEQ",
  .timebase = Timebase{48},
  .readCommand = readNdsCommand,
});
```

Most drivers should look like ordinary bytecode interpreters:

```cpp
CommandFlow readNdsCommand(NdsRuntime& rt, VmCommandCursor& cmd) {
  const uint8_t op = cmd.opcode();

  if (op <= 0x7f) {
    auto velocity = cmd.name("Note")
                       .semantic(SequenceSemantic::Note)
                       .derived("key", op)
                       .u8("velocity");

    auto duration = cmd.varLen("duration");

    rt.note(
      clampMidiNote(op + rt.state.transpose),
      LevelScale::linearFromMidi7(velocity.value),
      duration.value
    );

    return rt.state.noteWait ? cmd.wait(duration.value) : cmd.next();
  }

  switch (op) {
    case 0x80: {
      auto duration = cmd.name("Rest")
                         .semantic(SequenceSemantic::Rest)
                         .varLen("duration");

      return cmd.wait(duration.value);
    }

    case 0x81: {
      auto raw = cmd.name("Program")
                    .semantic(SequenceSemantic::Program)
                    .varLen("raw");

      const uint32_t bank = raw.value >> 7;
      const uint32_t program = raw.value & 0x7f;

      cmd.derived("bank", bank)
         .derived("program", program)
         .instrumentRef(bank, program);

      rt.instrument(bank, program);
      return cmd.next();
    }

    case 0x94: {
      auto dest = cmd.name("Jump")
                     .semantic(SequenceSemantic::Jump)
                     .le24RelativeAddress("destination", rt.sequenceBase);

      return cmd.jump(dest.value);
    }

    case 0x95: {
      auto dest = cmd.name("Call")
                     .semantic(SequenceSemantic::Call)
                     .le24RelativeAddress("destination", rt.sequenceBase);

      return cmd.call(dest.value);
    }

    case 0xfd:
      return cmd.name("Return")
                .semantic(SequenceSemantic::Return)
                .ret();

    case 0xff:
      return cmd.name("End")
                .semantic(SequenceSemantic::End)
                .end();

    default:
      return cmd.name("Unknown Opcode")
                .semantic(SequenceSemantic::Unknown)
                .warning("Unknown NDS SSEQ opcode stopped playback")
                .stop();
  }
}
```

That is the authoring experience to optimize for.

## 4.2 Name and slug behavior

Use implicit slugification.

The normal API should be:

```cpp
cmd.name("Pan");
cmd.name("Pitch Bend Range");
cmd.name("Repeat Until");
```

The cursor derives local command kind from the display name:

```text
"Pan"              -> "pan"
"Pitch Bend Range" -> "pitch-bend-range"
"Repeat Until"     -> "repeat-until"
```

The dialect namespace can qualify it internally:

```text
nds-sseq.pan
nds-sseq.pitch-bend-range
capcom-snes-v1.repeat-until
```

But format authors should not usually write that.

If a command truly needs an override, provide one:

```cpp
cmd.name("End of Track").kind("end");
```

or:

```cpp
cmd.kind("repeat-break").name("Repeat Break");
```

But this is optional and uncommon.

Important relationships must never be inferred from kind strings. This is authoritative:

```cpp
cmd.instrumentRef(bank, program);
cmd.target(destination, SourceLinkRole::JumpTarget);
```

This is not:

```cpp
if (kind == "program") parse operand named "raw"
```

Kind is for classification, display, filtering, tests, and compatibility. Structured references are the real data.

## 4.3 Compact sequence semantic enum

HexView needs to color sequence commands by broad musical/driver meaning. Add a compact enum now, grow later.

First pass:

```cpp
enum class SequenceSemantic : uint8_t {
  Unknown,

  // timing / sound
  Note,
  Rest,
  Wait,

  // instrument
  Program,

  // common controls
  Level,
  Pan,
  Pitch,
  Tempo,
  Modulation,
  Portamento,

  // flow
  Jump,
  Call,
  Return,
  End,
  Loop,
  Repeat,
  RepeatBreak,

  // miscellaneous state/meta
  State,
  Meta,
  Unsupported,
};
```

This is intentionally small. It should cover the initial Capcom/NDS needs without pretending to model every future event type.

Later additions can be made only when a real format needs them.

Usage:

```cpp
cmd.name("Volume").semantic(SequenceSemantic::Level);
cmd.name("Pan").semantic(SequenceSemantic::Pan);
cmd.name("Pitch Bend").semantic(SequenceSemantic::Pitch);
cmd.name("Tempo").semantic(SequenceSemantic::Tempo);
cmd.name("Repeat Until").semantic(SequenceSemantic::Repeat);
```

HexView can color command ranges by `SequenceSemantic`.

## 4.4 CommandCursor responsibilities

`VmCommandCursor` should combine the things that are currently scattered across command structs:

```text
byte reading
operand naming
operand source spans
derived fields
command display name
implicit local kind
sequence semantic
source references
diagnostics
VM-aware control-flow construction
```

It should not own global playback policy.

## 4.5 Cursor API sketch

```cpp
class VmCommandCursor {
public:
  CommandPhase phase() const noexcept;

  SourceId source() const noexcept;
  Address address() const noexcept;
  uint8_t opcode() const noexcept;

  SourceSpan commandSpan() const noexcept;
  SourceAnnotationId annotation() const noexcept;

  // Identity / classification
  VmCommandCursor& name(std::string_view displayName);
  VmCommandCursor& kind(std::string_view localKindOverride);
  VmCommandCursor& semantic(SequenceSemantic semantic);

  // Reading
  ReadValue<uint8_t> u8(std::string_view name);
  ReadValue<int8_t> s8(std::string_view name);
  ReadValue<uint16_t> u16le(std::string_view name);
  ReadValue<uint16_t> u16be(std::string_view name);
  ReadValue<uint32_t> u24le(std::string_view name);
  ReadValue<uint32_t> u24be(std::string_view name);
  ReadValue<uint32_t> varLen(std::string_view name);

  ReadValue<Address> address16be(std::string_view name);
  ReadValue<Address> address16le(std::string_view name);
  ReadValue<Address> le24RelativeAddress(std::string_view name, Address base);

  // Source facts
  VmCommandCursor& derived(std::string_view name, SourceValue value);
  VmCommandCursor& detail(std::string_view name, SourceValue value);
  VmCommandCursor& target(Address address, SourceLinkRole role);
  VmCommandCursor& instrumentRef(uint32_t bank, uint32_t program);
  VmCommandCursor& sampleRef(uint32_t sampleIndex);

  // Diagnostics
  VmCommandCursor& warning(std::string_view message);
  VmCommandCursor& error(std::string_view message);
  VmCommandCursor& unsupported(std::string_view message);

  // Flow helpers
  CommandFlow next();
  CommandFlow wait(uint32_t ticks);
  CommandFlow stop();
  CommandFlow end();
  CommandFlow jump(Address destination);
  CommandFlow call(Address destination);
  CommandFlow ret();

  CommandFlow loopCandidate(Address destination);
  CommandFlow declaredLoop(Address destination);
  CommandFlow countedRepeatUntil(uint8_t slot, uint32_t totalPlays, Address destination);
  RepeatBreakFlow countedRepeatBreak(uint8_t slot, Address destination);
};
```

## 4.6 `ReadValue<T>`

Cursor reads should return both value and source span.

```cpp
template <class T>
struct ReadValue {
  T value;
  SourceSpan span;

  operator T() const noexcept {
    return value;
  }
};
```

This lets code stay terse:

```cpp
auto volume = cmd.u8("volume");
rt.level(LevelScale::linearFromMidi7(volume.value));
```

while the source map records:

```text
operand name: volume
operand span
operand value
parent command annotation
```

---

# 5. SequenceVM integration

## 5.1 Keep the VM branch’s architecture

The architectural path remains:

```text
SequenceProgram
  -> SequenceDialect
  -> SequenceVM
  -> PerformanceSequence / export
```

The change is authoring surface, not ownership of playback policy.

## 5.2 Decouple command handler from command kind

The feedback’s best structural point is still this: one executable handler can execute many command kinds, which avoids one typed command class per opcode while preserving VM architecture.

Use:

```cpp
struct CommandKind {
  CommandKindId id;

  std::string localKind;       // usually slugified from displayName
  std::string displayName;     // "Pan"
  SequenceSemantic semantic;   // Pan, Note, Jump, etc.

  CommandPlaybackStatus playbackStatus;
};

struct CommandHandler {
  CommandHandlerId id;
  std::string name;

  ExecuteSourceCommand execute;
};

struct SourceCommand {
  SourceCommandId id;

  CommandHandlerId handler;
  CommandKindId kind;

  uint8_t opcode;
  Address address;

  SourceSpan span;
  std::vector<uint8_t> bytes;

  SourceAnnotationId annotation;
};
```

For NDS:

```text
Source command kind: note
Source command kind: rest
Source command kind: program
Source command kind: jump
Source command kind: call
Source command kind: return
Source command kind: end

All use handler:
  nds-sseq.bytecode-driver
```

The handler reruns the saved bytes through `readNdsCommand`.

The UI does not need to rerun the handler just to inspect basic command facts, because those facts are recorded during decode.

## 5.3 One generic handler per dialect is normal

The default adapter should produce something like:

```cpp
template <class Runtime, class ReadCommand>
SequenceDialect makeCursorDialect(
    DialectSpec spec,
    RuntimeFactory<Runtime> runtimeFactory,
    ReadCommand readCommand);
```

Internally:

```text
Decode:
  read source bytes with decode-mode cursor
  create SourceCommand
  create SourceAnnotation
  record operands, name, semantic, references, diagnostics
  record static flow references directly in SourceMap

Render:
  locate SourceCommand
  create render-mode cursor over SourceCommand.bytes
  call same readCommand
  let runtime output events
  translate CommandFlow to VM step/effects
```

---

# 6. Decode phase vs render phase

This distinction is essential.

The feedback warned that running the same command reader during decode and render can be dangerous if decode mutates real playback state or infers static flow only from dynamic choices.

## 6.1 Decode phase

Decode phase records static facts.

```text
reads command bytes
records command size
records operands
records derived fields
records command name/kind/semantic
records source references
records possible static flow targets
records diagnostics
creates SourceCommand
creates SourceAnnotation
does not emit real musical events
does not consume real VM repeat counters
```

For example:

```cpp
cmd.countedRepeatUntil(slot, totalPlays, destination);
```

in decode mode records:

```text
this command is Repeat
possible fallthrough
possible repeat target
repeat slot
repeat count
target source link
```

It does not decide whether this playback pass jumps.

## 6.2 Render phase

Render phase executes actual playback behavior.

```text
re-reads SourceCommand.bytes
mutates real runtime state
emits performance events
chooses actual branch path
consumes VM repeat counters
uses VM call stack
uses VM loop policy
produces render diagnostics
```

The author usually does not branch on `cmd.phase()`. The cursor and runtime sinks are phase-aware internally.

## 6.3 Rule

> Decode records all statically visible source facts. Render chooses actual playback behavior.

That rule avoids the biggest risk of the hybrid approach.

---

# 7. CommandFlow

`CommandFlow` is the driver-facing result of one command.

```cpp
enum class FlowKind : uint8_t {
  Next,
  Wait,
  Stop,
  End,
  Jump,
  Call,
  Return,
  LoopCandidate,
  DeclaredLoop,
  CountedRepeatUntil,
  CountedRepeatBreak,
};
```

```cpp
struct CommandFlow {
  FlowKind kind = FlowKind::Next;

  uint32_t waitTicks = 0;
  std::optional<Address> destination;

  uint8_t repeatSlot = 0;
  uint32_t repeatTotalPlays = 0;
};
```

The cursor constructs this. Format code does not manually fill it.

Examples:

```cpp
return cmd.next();
return cmd.wait(duration.value);
return cmd.jump(destination.value);
return cmd.declaredLoop(destination.value);
return cmd.countedRepeatUntil(slot, count + 1, destination.value);
```

The VM converts `CommandFlow` into the internal step/effects model.

---

# 8. Examples

## 8.1 NDS note

```cpp
if (op <= 0x7f) {
  cmd.name("Note")
     .semantic(SequenceSemantic::Note)
     .derived("key", op);

  auto velocity = cmd.u8("velocity");
  auto duration = cmd.varLen("duration");

  rt.note(
    clampMidiNote(op + rt.state.transpose),
    LevelScale::linearFromMidi7(velocity.value),
    duration.value
  );

  return rt.state.noteWait ? cmd.wait(duration.value) : cmd.next();
}
```

Produces:

```text
SourceCommand:
  name: Note
  kind: note
  semantic: Note
  opcode
  bytes
  source span

SourceAnnotation:
  role: Command
  semantic: Note
  operand fields: velocity, duration
  derived field: key
```

## 8.2 NDS program

```cpp
case 0x81: {
  auto raw = cmd.name("Program")
                .semantic(SequenceSemantic::Program)
                .varLen("raw");

  const uint32_t bank = raw.value >> 7;
  const uint32_t program = raw.value & 0x7f;

  cmd.derived("bank", bank)
     .derived("program", program)
     .instrumentRef(bank, program);

  rt.instrument(bank, program);
  return cmd.next();
}
```

Produces structured instrument reference data. The UI should not infer that relationship from `"program"`.

## 8.3 NDS pan

```cpp
case 0xc4: {
  auto pan = cmd.name("Pan")
                .semantic(SequenceSemantic::Pan)
                .u8("pan");

  rt.pan(PanScale::fromMidi7(pan.value));
  return cmd.next();
}
```

HexView colors this as `Pan`.

## 8.4 NDS pitch bend

```cpp
case 0xc0: {
  auto bend = cmd.name("Pitch Bend")
                 .semantic(SequenceSemantic::Pitch)
                 .s8("bend");

  rt.pitchBend(PitchBend::fromSigned7(bend.value));
  return cmd.next();
}
```

## 8.5 NDS tempo

```cpp
case 0xe1: {
  auto bpm = cmd.name("Tempo")
                .semantic(SequenceSemantic::Tempo)
                .u16le("bpm");

  rt.tempo(Tempo::bpm(bpm.value));
  return cmd.next();
}
```

## 8.6 Capcom repeat until

```cpp
case 0x0e:
case 0x0f:
case 0x10:
case 0x11: {
  cmd.name("Repeat Until")
     .semantic(SequenceSemantic::Repeat);

  const uint8_t slot = cmd.opcode() - 0x0e;
  cmd.derived("slot", slot + 1);

  auto count = cmd.u8("count");
  auto destination = cmd.address16be("destination");

  if (count.value == 0) {
    return cmd.declaredLoop(destination.value);
  }

  return cmd.countedRepeatUntil(
    slot,
    static_cast<uint32_t>(count.value) + 1,
    destination.value
  );
}
```

Format code identifies repeat semantics. The VM owns repeat policy.

## 8.7 Capcom repeat break

```cpp
case 0x12:
case 0x13:
case 0x14:
case 0x15: {
  cmd.name("Repeat Break")
     .semantic(SequenceSemantic::RepeatBreak);

  const uint8_t slot = cmd.opcode() - 0x12;
  cmd.derived("slot", slot + 1);

  auto attributes = cmd.u8("attributes");
  auto destination = cmd.address16be("destination");

  auto branch = cmd.countedRepeatBreak(slot, destination.value);

  if (branch.taken()) {
    rt.applyNoteAttributes(attributes.value);
  }

  return branch;
}
```

In decode mode:

```text
branch.taken() is false or inert
target is still recorded
VM repeat state is not consumed
```

In render mode:

```text
branch.taken() reflects actual VM repeat state
```

---

# 9. SourceMap and SourceAnnotations

`SourceMap` is the shared source-description layer produced during scanning and decoding.

It exists so that consumers such as HexView, source outlines, inspectors, validation views, and future debugging tools can answer source-level questions without depending on format-specific parser code.

It should describe:

```text
where data came from
what byte ranges represent
what fields and operands were decoded
what semantic category a sequence command belongs to
what other source/object a field or command references
what diagnostics attach to the source
```

It should not describe:

```text
UI widgets
colors
layout
piano roll data
export behavior
playback policy
the complete semantic model of an asset
```

Semantic assets remain the authoritative parsed model. `SourceMap` records source-backed facts and relationships about those assets.

## 9.1 SourceSpan

All source annotations are grounded in source bytes.

```cpp
using SourceId = StrongId<struct SourceTag, uint32_t>;

struct SourceSpan {
  SourceId source;
  uint64_t offset = 0;
  uint64_t length = 0;
};
```

An empty span is allowed for derived values that have no direct byte range:

```cpp
SourceSpan{}; // derived field, synthetic relationship, or unknown location
```

Common examples:

```text
sequence command bytes
opcode byte
operand bytes
header range
table range
table row range
pointer field range
instrument definition range
sample header range
sample payload range
unknown/misc block range
diagnostic source range
```

## 9.2 SourceRole

`SourceRole` describes the structural role of an annotation.

Keep the first-pass enum compact, but expressive enough for Capcom SNES, NDS, and general file visualization.

```cpp
enum class SourceRole : uint8_t {
  Unknown,

  Source,
  Section,
  Header,

  Table,
  TableRow,
  Field,
  Pointer,

  Payload,
  Padding,
  DataBlock,

  Command,
  Opcode,
  Operand,

  Instrument,
  Sample,

  Diagnostic,
};
```

Guidelines:

```text
Header:
  top-level or section-level metadata header.

Section:
  named region of a source file.

Table:
  repeated structured records.

TableRow:
  one entry inside a table.

Field:
  scalar or small decoded value.

Pointer:
  field whose main purpose is addressing another source location.

Payload:
  arbitrary payload bytes known to belong to something.

DataBlock:
  known or partially known block that is not better described yet.

Command:
  sequence command instruction.

Opcode:
  opcode byte or opcode field within a command.

Operand:
  operand bytes within a command.

Instrument:
  source range defining an instrument or instrument row.

Sample:
  source range defining sample metadata or sample payload.

Diagnostic:
  source-backed parse/render/validation issue.
```

This enum should grow only when a real format or consumer proves a new structural category is needed.

## 9.3 SequenceSemantic

`SequenceSemantic` describes the broad musical or driver-level meaning of a sequence command.

This is separate from `SourceRole`.

For example:

```text
SourceRole::Command
SequenceSemantic::Pan
```

means:

```text
this byte range is a sequence command,
and semantically it changes pan.
```

First-pass enum:

```cpp
enum class SequenceSemantic : uint8_t {
  Unknown,

  // timing / sound
  Note,
  Rest,
  Wait,

  // instrument
  Program,

  // common controls
  Level,
  Pan,
  Pitch,
  Tempo,
  Modulation,
  Portamento,

  // flow
  Jump,
  Call,
  Return,
  End,
  Loop,
  Repeat,
  RepeatBreak,

  // miscellaneous state/meta
  State,
  Meta,
  Unsupported,
};
```

This is intentionally broad.

Examples:

```cpp
cmd.name("Note").semantic(SequenceSemantic::Note);
cmd.name("Rest").semantic(SequenceSemantic::Rest);
cmd.name("Volume").semantic(SequenceSemantic::Level);
cmd.name("Expression").semantic(SequenceSemantic::Level);
cmd.name("Pan").semantic(SequenceSemantic::Pan);
cmd.name("Pitch Bend").semantic(SequenceSemantic::Pitch);
cmd.name("Tempo").semantic(SequenceSemantic::Tempo);
cmd.name("Program").semantic(SequenceSemantic::Program);
cmd.name("Jump").semantic(SequenceSemantic::Jump);
cmd.name("Call").semantic(SequenceSemantic::Call);
cmd.name("Return").semantic(SequenceSemantic::Return);
cmd.name("End").semantic(SequenceSemantic::End);
cmd.name("Repeat Until").semantic(SequenceSemantic::Repeat);
cmd.name("Repeat Break").semantic(SequenceSemantic::RepeatBreak);
```

HexView can color sequence commands by `SequenceSemantic`.

Format code does not specify colors. It only specifies semantic category.

## 9.4 SourceValue

`SourceValue` is the decoded value stored in source fields.

```cpp
using SourceValue =
    std::variant<std::monostate, bool, uint64_t, int64_t, double, std::string>;
```

This is intentionally scalar. Complex parsed objects should live in semantic assets, not inside `SourceAnnotation`.

Examples:

```text
magic = "SSEQ"
version = 0x0100
track_count = 16
program = 24
bank = 2
pan = 64
destination = 0x1234
sample_index = 7
```

## 9.5 SourceValueDisplay

A field may optionally indicate how a scalar should be displayed.

```cpp
enum class SourceValueDisplay : uint8_t {
  Default,
  Hex,
  Decimal,
  SignedDecimal,
  Boolean,
  Address,
  Percent,
  Cents,
  Decibels,
  MidiNote,
  Ascii,
  Enum,
};
```

This is not a widget instruction. It is a formatting hint for generic inspectors.

Examples:

```cpp
field.display = SourceValueDisplay::Hex;
field.display = SourceValueDisplay::Address;
field.display = SourceValueDisplay::MidiNote;
```

If omitted, consumers choose a default.

## 9.6 SourceField

`SourceField` represents a decoded value inside an annotation.

```cpp
struct SourceField {
  std::string name;

  // May be empty for derived values.
  SourceSpan span;

  SourceValue value;
  SourceValueDisplay display = SourceValueDisplay::Default;

  // Optional semantic category for command operands or derived fields.
  std::optional<SequenceSemantic> sequenceSemantic;
};
```

Examples:

```text
Command: Program
  Field: opcode, span = [0x1000, 1], value = 0x81, display = Hex
  Field: raw, span = [0x1001, 1], value = 0x92
  Field: bank, span = empty, value = 1
  Field: program, span = empty, value = 18

Command: Pan
  Field: opcode, span = [0x1030, 1], value = 0xc4, display = Hex
  Field: pan, span = [0x1031, 1], value = 64, semantic = Pan

Header
  Field: magic, span = [0x0000, 4], value = "SSEQ", display = Ascii
  Field: version, span = [0x0004, 2], value = 0x0100, display = Hex

Pointer table row
  Field: offset, span = [0x0020, 4], value = 0x140, display = Address
```

Derived fields are important. Driver code should be able to record values that are useful for inspection even if they do not occupy their own byte range.

Example:

```cpp
auto raw = cmd.varLen("raw");

const uint32_t bank = raw.value >> 7;
const uint32_t program = raw.value & 0x7f;

cmd.derived("bank", bank);
cmd.derived("program", program);
```

## 9.7 SourceLinkRole

`SourceLinkRole` describes relationships from one annotation or field to another source/object.

First-pass enum:

```cpp
enum class SourceLinkRole : uint8_t {
  PointsTo,

  JumpTarget,
  CallTarget,
  LoopTarget,
  RepeatTarget,

  UsesInstrument,
  UsesSample,

  DerivedFrom,
  Related,
};
```

Guidelines:

```text
PointsTo:
  generic pointer/offset relationship.

JumpTarget:
  sequence jump target.

CallTarget:
  sequence call/subroutine target.

LoopTarget:
  declared loop or loop-candidate target.

RepeatTarget:
  finite repeat or repeat-break target.

UsesInstrument:
  command or table row references an instrument.

UsesSample:
  instrument/region/table row references a sample.

DerivedFrom:
  decompressed/extracted/generated data comes from another span.

Related:
  fallback relationship when no more specific role exists.
```

Do not infer important relationships from command names or slugified kinds. Use structured links.

Good:

```cpp
cmd.instrumentRef(bank, program);
cmd.target(destination, SourceLinkRole::JumpTarget);
```

Bad:

```cpp
if (cmd.kind() == "program") {
  infer instrument reference from operand named "raw";
}
```

## 9.8 ObjectRef

`ObjectRef` points from source annotations to semantic objects without making annotations own those objects.

```cpp
enum class ObjectKind : uint8_t {
  Asset,
  Sequence,
  SequenceTrack,
  SequenceCommand,
  Instrument,
  Sample,
  Misc,
};
```

```cpp
struct ObjectRef {
  ObjectKind kind;

  AssetId asset;
  uint32_t index0 = 0;
  uint32_t index1 = 0;
};
```

Suggested constructors:

```cpp
namespace ObjectRefs {
  ObjectRef asset(AssetId asset);

  ObjectRef sequence(AssetId sequenceAsset);
  ObjectRef sequenceTrack(AssetId sequenceAsset, uint32_t trackIndex);
  ObjectRef sequenceCommand(AssetId sequenceAsset, uint32_t commandIndex);

  ObjectRef instrument(AssetId instrumentSetAsset, uint32_t instrumentIndex);
  ObjectRef sample(AssetId sampleSetAsset, uint32_t sampleIndex);

  ObjectRef misc(AssetId miscAsset);
}
```

Examples:

```cpp
row.owner(ObjectRefs::instrument(instrumentSetId, i));

cmd.owner(ObjectRefs::sequenceCommand(sequenceAssetId, commandIndex));

sampleData.owner(ObjectRefs::sample(sampleSetId, sampleIndex));
```

The semantic asset remains authoritative. The annotation simply says:

```text
these source bytes correspond to that semantic object
```

## 9.9 SourceTarget

Links may point to a source span, another annotation, or a semantic object.

```cpp
using SourceTarget =
    std::variant<SourceSpan, SourceAnnotationId, ObjectRef>;
```

Examples:

```text
pointer field -> SourceSpan target
jump command -> SourceSpan or command annotation
program command -> ObjectRef instrument
instrument row -> ObjectRef sample
decompressed source -> SourceSpan compressed parent
```

## 9.10 SourceLink

```cpp
struct SourceLink {
  SourceLinkRole role;
  SourceTarget target;
  std::string label;
};
```

Examples:

```cpp
row.link(
  SourceLinkRole::PointsTo,
  SourceSpan{sourceId, trackAddress, 1},
  "Track Start");

cmd.target(destination, SourceLinkRole::JumpTarget);

cmd.instrumentRef(bank, program);

instrumentRow.link(
  SourceLinkRole::UsesSample,
  ObjectRefs::sample(sampleSetId, sampleIndex),
  "Sample");
```

## 9.11 SourceAnnotation

`SourceAnnotation` is the core source-map record.

```cpp
using SourceAnnotationId = StrongId<struct SourceAnnotationTag, uint32_t>;

struct SourceAnnotation {
  SourceAnnotationId id;

  SourceSpan span;
  SourceRole role = SourceRole::Unknown;

  std::string label;

  // For sequence command coloring/classification.
  std::optional<SequenceSemantic> sequenceSemantic;

  // Usually slugified from label.
  // Optional override through cursor.kind() or AnnotationBuilder::kind().
  std::string localKind;

  // Semantic ownership, if applicable.
  std::optional<ObjectRef> owner;

  // Optional hierarchy for source outlines and containment.
  std::optional<SourceAnnotationId> parent;

  std::vector<SourceField> fields;
  std::vector<SourceLink> links;

  std::vector<DiagnosticId> diagnostics;
};
```

Notes:

```text
span:
  the primary byte range represented by this annotation.

role:
  structural category, such as Header, Table, Command, Operand, Instrument, Sample.

label:
  human-readable name, such as "Program", "Track Pointer Table", "Instrument 3".

sequenceSemantic:
  broad command/event category for sequence command coloring and filtering.

localKind:
  slugified name by default, e.g. "Pitch Bend Range" -> "pitch-bend-range".
  It is useful for filtering/tests but not authoritative for relationships.

owner:
  semantic object that this source range belongs to.

parent:
  optional containment for source outlines.

fields:
  decoded values inside this annotation.

links:
  references from this annotation to source spans, annotations, or semantic objects.

diagnostics:
  parse/render/validation issues attached to this annotation.
```

## 9.12 Slugification

Annotations and commands should default to slugifying their label.

```cpp
cmd.name("Pan");
// localKind = "pan"

cmd.name("Pitch Bend Range");
// localKind = "pitch-bend-range"

cmd.name("Repeat Until");
// localKind = "repeat-until"
```

The dialect namespace can qualify local kinds internally:

```text
nds-sseq.pan
nds-sseq.pitch-bend-range
capcom-snes-v1.repeat-until
```

Format code should not normally write the slug.

If needed:

```cpp
cmd.name("End of Track").kind("end");
```

or:

```cpp
annotation.kind("track-table");
```

But this should be the exception.

## 9.13 SourceMap

`SourceMap` owns annotations and indexes them for simple consumers.

```cpp
class SourceMap {
public:
  const SourceAnnotation& get(SourceAnnotationId id) const;

  std::span<const SourceAnnotation> annotations() const;

  std::vector<SourceAnnotationId> annotationsForSource(SourceId source) const;

  std::vector<SourceAnnotationId> intersecting(SourceSpan span) const;
  std::vector<SourceAnnotationId> containing(SourceSpan span) const;
  std::vector<SourceAnnotationId> at(SourceId source, uint64_t offset) const;

  std::vector<SourceAnnotationId> ownedBy(ObjectRef object) const;

  std::vector<SourceAnnotationId> withRole(SourceId source, SourceRole role) const;
  std::vector<SourceAnnotationId> withSequenceSemantic(
      SourceId source,
      SequenceSemantic semantic) const;

  std::vector<SourceLink> linksFrom(SourceAnnotationId id) const;
  std::vector<SourceAnnotationId> linksTo(SourceTarget target) const;
};
```

The implementation can start simple.

Required indexes for first pass:

```text
by annotation id
by source id
by source range
by owner ObjectRef
```

Optional indexes when needed:

```text
by SourceRole
by SequenceSemantic
reverse links
```

## 9.14 SourceAnnotation hierarchy

The parent field is intentionally simple.

It supports source outlines such as:

```text
SSEQ Header
Track Pointer Table
  Track 0
  Track 1
Track 0 Commands
  Note
  Program
  Pan
Instrument Table
  Instrument 0
  Instrument 1
Sample Table
  Sample 0
  Sample 1
Unknown Control Block
```

This is not a UI tree model. It is source containment.

Consumers may ignore the hierarchy and query by range instead.

## 9.15 Overlapping annotations

Overlapping annotations are allowed.

Examples:

```text
A table row may overlap an instrument annotation.
A command annotation contains opcode and operand fields.
A diagnostic annotation may overlap a command annotation.
A payload annotation may overlap a sample annotation.
```

First pass does not need layers.

If overlapping annotations become difficult for consumers to prioritize, add a minimal priority/hint field later. Do not add it preemptively.

## 9.16 Relationship to SequenceProgram

For sequences, the source map and `SequenceProgram` work together.

`SequenceProgram` owns the executable source commands:

```cpp
struct SourceCommand {
  SourceCommandId id;

  CommandHandlerId handler;
  CommandKindId kind;

  uint8_t opcode;
  Address address;

  SourceSpan span;
  std::vector<uint8_t> bytes;

  SourceAnnotationId annotation;
};
```

The corresponding `SourceAnnotation` owns inspection/source facts:

```text
label
local kind
sequence semantic
fields
links
diagnostics
owner
parent
```

A command should generally have:

```text
one SourceCommand
one primary SourceAnnotation
zero or more SourceField operands
zero or more SourceLink references
```

Example:

```text
SourceCommand:
  id = 42
  opcode = 0x81
  address = 0x1234
  bytes = [0x81, 0x92]
  annotation = 77

SourceAnnotation 77:
  role = Command
  label = "Program"
  localKind = "program"
  sequenceSemantic = Program
  fields:
    opcode = 0x81
    raw = 0x92
    bank = 1
    program = 18
  links:
    UsesInstrument -> ObjectRef::instrument(...)
```

No separate `StaticSequenceMap` is introduced in this pass.

Static sequence relationships live in:

```text
SourceCommand
SourceAnnotation
SourceAnnotation.links
SequenceProgram.tracks
```

If those prove insufficient, add the smallest missing sequence-specific index later.

---

# 10. SourceMapBuilder and Annotation Authoring

`SourceMapBuilder` is the parser-facing API for creating source annotations.

It should be ergonomic enough that format authors actually use it for headers, tables, pointers, instruments, samples, misc blocks, and sequence commands.

It should not expose UI concepts.

## 10.1 SourceMapBuilder

```cpp
class SourceMapBuilder {
public:
  AnnotationBuilder source(std::string_view label, SourceSpan span);

  AnnotationBuilder section(std::string_view label, SourceSpan span);
  AnnotationBuilder header(std::string_view label, SourceSpan span);

  AnnotationBuilder table(std::string_view label, SourceSpan span);
  AnnotationBuilder row(std::string_view label, SourceSpan span);

  AnnotationBuilder field(
      std::string_view label,
      SourceSpan span,
      SourceValue value);

  AnnotationBuilder pointer(
      std::string_view label,
      SourceSpan span,
      SourceTarget target);

  AnnotationBuilder command(
      std::string_view label,
      SourceSpan span,
      SequenceSemantic semantic = SequenceSemantic::Unknown);

  void diagnostic(
      SourceSpan span,
      Severity severity,
      std::string_view message);
};
```

Suggested default roles:

```text
source()  -> SourceRole::Source
section() -> SourceRole::Section
header()  -> SourceRole::Header
table()   -> SourceRole::Table
row()     -> SourceRole::TableRow
field()   -> SourceRole::Field
pointer() -> SourceRole::Pointer
command() -> SourceRole::Command
```

## 10.2 AnnotationBuilder

```cpp
class AnnotationBuilder {
public:
  SourceAnnotationId id() const;

  AnnotationBuilder& role(SourceRole role);
  AnnotationBuilder& label(std::string_view label);

  // Slug override. Usually unnecessary.
  AnnotationBuilder& kind(std::string_view localKindOverride);

  AnnotationBuilder& parent(SourceAnnotationId parent);
  AnnotationBuilder& owner(ObjectRef owner);

  AnnotationBuilder& sequenceSemantic(SequenceSemantic semantic);

  AnnotationBuilder& field(
      std::string_view name,
      SourceSpan span,
      SourceValue value,
      SourceValueDisplay display = SourceValueDisplay::Default);

  AnnotationBuilder& derived(
      std::string_view name,
      SourceValue value,
      SourceValueDisplay display = SourceValueDisplay::Default);

  AnnotationBuilder& link(
      SourceLinkRole role,
      SourceTarget target,
      std::string_view label = {});

  AnnotationBuilder& diagnostic(
      Severity severity,
      std::string_view message);
};
```

Convenience helpers may be added on top:

```cpp
AnnotationBuilder& pointsTo(SourceSpan target, std::string_view label = {});
AnnotationBuilder& usesInstrument(ObjectRef instrument);
AnnotationBuilder& usesSample(ObjectRef sample);
```

But the core API should remain small.

## 10.3 ParseCursor integration

A general `ParseCursor` should return values with spans.

```cpp
template <class T>
struct ReadValue {
  T value;
  SourceSpan span;

  operator T() const noexcept {
    return value;
  }
};
```

```cpp
class ParseCursor {
public:
  SourceId source() const noexcept;
  uint64_t offset() const noexcept;

  SourceSpan span(uint64_t offset, uint64_t length) const;
  SourceSpan currentSpan(uint64_t length) const;

  ReadValue<uint8_t> u8(std::string_view name);
  ReadValue<int8_t> s8(std::string_view name);

  ReadValue<uint16_t> u16le(std::string_view name);
  ReadValue<uint16_t> u16be(std::string_view name);

  ReadValue<uint32_t> u32le(std::string_view name);
  ReadValue<uint32_t> u32be(std::string_view name);

  ReadValue<std::vector<uint8_t>> bytes(std::string_view name, size_t count);
};
```

The `name` passed to `ParseCursor` is useful for debugging, but it does not automatically create annotations. Format code decides which reads become source fields.

Example:

```cpp
auto version = r.u16le("version");
header.field("Version", version.span, version.value, SourceValueDisplay::Hex);
```

## 10.4 Header example

```cpp
auto header = source.header("SSEQ Header", headerSpan);

auto magic = r.bytes("magic", 4);
header.field(
  "Magic",
  magic.span,
  ascii(magic.value),
  SourceValueDisplay::Ascii);

auto version = r.u16le("version");
header.field(
  "Version",
  version.span,
  version.value,
  SourceValueDisplay::Hex);

auto trackTableOffset = r.u32le("track_table_offset");

SourceSpan trackTableSpan{
  sourceId,
  trackTableOffset.value,
  trackTableSize
};

header.field(
        "Track Table Offset",
        trackTableOffset.span,
        trackTableOffset.value,
        SourceValueDisplay::Address)
      .link(
        SourceLinkRole::PointsTo,
        trackTableSpan,
        "Track Table");
```

This records source structure that is irrelevant to sequence rendering but essential for HexView and source inspection.

## 10.5 Track pointer table example

```cpp
auto table = source.table("Track Pointer Table", tableSpan);

for (uint32_t track = 0; track < trackCount; ++track) {
  SourceSpan rowSpan = r.currentSpan(4);

  auto row = source.row(fmt::format("Track {}", track), rowSpan)
                   .parent(table.id())
                   .owner(ObjectRefs::sequenceTrack(sequenceAssetId, track));

  auto offset = r.u32le("offset");
  Address entry = sequenceBase + offset.value;

  row.field(
       "Offset",
       offset.span,
       offset.value,
       SourceValueDisplay::Address)
     .link(
       SourceLinkRole::PointsTo,
       SourceSpan{sourceId, entry, 1},
       "Track Start");

  programBuilder.addTrack(track, entry, row.id());
}
```

This gives the UI a visible track table while also giving the sequence program its track entry points.

## 10.6 Instrument table example

```cpp
auto table = source.table("Instrument Table", tableSpan);

for (uint32_t i = 0; i < instrumentCount; ++i) {
  SourceSpan rowSpan = r.currentSpan(instrumentEntrySize);

  auto row = source.row(fmt::format("Instrument {}", i), rowSpan)
                   .role(SourceRole::Instrument)
                   .parent(table.id())
                   .owner(ObjectRefs::instrument(instrumentSetId, i));

  auto program = r.u8("program");
  auto sampleIndex = r.u16le("sample_index");
  auto volume = r.u8("volume");
  auto pan = r.u8("pan");

  row.field("Program", program.span, program.value);
  row.field("Sample Index", sampleIndex.span, sampleIndex.value);
  row.field("Volume", volume.span, volume.value);
  row.field("Pan", pan.span, pan.value);

  row.link(
    SourceLinkRole::UsesSample,
    ObjectRefs::sample(sampleSetId, sampleIndex.value),
    "Sample");
}
```

The instrument asset owns the actual parsed instrument model. The source map records how the source bytes map to that model.

## 10.7 Sample header and payload example

```cpp
auto sample = source.section("Sample 3", sampleSpan)
                    .role(SourceRole::Sample)
                    .owner(ObjectRefs::sample(sampleSetId, 3));

auto format = r.u8("format");
auto sampleRate = r.u16le("sample_rate");
auto loopStart = r.u32le("loop_start");
auto loopEnd = r.u32le("loop_end");
auto dataOffset = r.u32le("data_offset");

sample.field("Format", format.span, format.value);
sample.field("Sample Rate", sampleRate.span, sampleRate.value);
sample.field("Loop Start", loopStart.span, loopStart.value);
sample.field("Loop End", loopEnd.span, loopEnd.value);

SourceSpan payloadSpan{
  sourceId,
  dataOffset.value,
  sampleDataLength
};

sample.field(
        "Data Offset",
        dataOffset.span,
        dataOffset.value,
        SourceValueDisplay::Address)
      .link(
        SourceLinkRole::PointsTo,
        payloadSpan,
        "Sample Data");

source.section("Sample 3 Data", payloadSpan)
      .role(SourceRole::Payload)
      .owner(ObjectRefs::sample(sampleSetId, 3));
```

This lets HexView distinguish sample metadata from sample payload bytes.

## 10.8 Misc block example

```cpp
auto block = source.section("Unknown Control Block", blockSpan)
                   .role(SourceRole::DataBlock)
                   .owner(ObjectRefs::misc(miscAssetId));

auto flags = r.u16le("flags");
auto count = r.u16le("count");
auto dataOffset = r.u32le("data_offset");

block.field("Flags", flags.span, flags.value, SourceValueDisplay::Hex);
block.field("Count", count.span, count.value);

block.field(
       "Data Offset",
       dataOffset.span,
       dataOffset.value,
       SourceValueDisplay::Address)
     .link(
       SourceLinkRole::PointsTo,
       SourceSpan{sourceId, dataOffset.value, count.value * entrySize},
       "Data");
```

This supports visualizing useful source structure even when the bytes do not yet have a richer semantic asset model.

## 10.9 Sequence command annotation through VmCommandCursor

Sequence command annotations should usually be created by `VmCommandCursor`, not by direct `SourceMapBuilder` calls.

For example:

```cpp
case 0xc4: {
  auto pan = cmd.name("Pan")
                .semantic(SequenceSemantic::Pan)
                .u8("pan");

  rt.pan(PanScale::fromMidi7(pan.value));
  return cmd.next();
}
```

The cursor should create something equivalent to:

```cpp
auto annotation = source.command(
    "Pan",
    commandSpan,
    SequenceSemantic::Pan);

annotation.kind("pan");

annotation.field(
    "opcode",
    opcodeSpan,
    opcode,
    SourceValueDisplay::Hex);

annotation.field(
    "pan",
    pan.span,
    pan.value);
```

Driver code should not need to write that manually.

## 10.10 Sequence command with references

Example:

```cpp
case 0x81: {
  auto raw = cmd.name("Program")
                .semantic(SequenceSemantic::Program)
                .varLen("raw");

  const uint32_t bank = raw.value >> 7;
  const uint32_t program = raw.value & 0x7f;

  cmd.derived("bank", bank)
     .derived("program", program)
     .instrumentRef(bank, program);

  rt.instrument(bank, program);
  return cmd.next();
}
```

The cursor records:

```text
SourceAnnotation:
  role = Command
  label = "Program"
  localKind = "program"
  sequenceSemantic = Program

Fields:
  opcode
  raw
  bank
  program

Links:
  UsesInstrument -> matching instrument ObjectRef if resolved,
                    or unresolved bank/program reference if not yet resolved
```

If the referenced instrument cannot be resolved at decode time, the source link can still be represented later by one of these approaches:

```cpp
struct UnresolvedInstrumentRef {
  uint32_t bank;
  uint32_t program;
};
```

or by storing the relationship in a format-specific reference table that is resolved after all assets are known.

Do not infer the instrument relationship from the command kind.

## 10.11 Sequence command with flow target

Example:

```cpp
case 0x94: {
  auto destination = cmd.name("Jump")
                        .semantic(SequenceSemantic::Jump)
                        .le24RelativeAddress("destination", rt.sequenceBase);

  return cmd.jump(destination.value);
}
```

The cursor records:

```text
SourceAnnotation:
  role = Command
  label = "Jump"
  localKind = "jump"
  sequenceSemantic = Jump

Fields:
  opcode
  destination

Links:
  JumpTarget -> target SourceSpan or target SourceAnnotation
```

During early decode, the target annotation may not exist yet. In that case, record a target `SourceSpan` first. A later fixup pass may replace or supplement it with the resolved target `SourceAnnotationId`.

## 10.12 Repeat command annotation

Example:

```cpp
case 0x0e:
case 0x0f:
case 0x10:
case 0x11: {
  cmd.name("Repeat Until")
     .semantic(SequenceSemantic::Repeat);

  const uint8_t slot = cmd.opcode() - 0x0e;
  cmd.derived("slot", slot + 1);

  auto count = cmd.u8("count");
  auto destination = cmd.address16be("destination");

  if (count.value == 0) {
    return cmd.declaredLoop(destination.value);
  }

  return cmd.countedRepeatUntil(
    slot,
    static_cast<uint32_t>(count.value) + 1,
    destination.value);
}
```

The cursor records:

```text
SourceAnnotation:
  role = Command
  label = "Repeat Until"
  localKind = "repeat-until"
  sequenceSemantic = Repeat

Fields:
  opcode
  slot
  count
  destination

Links:
  RepeatTarget or LoopTarget -> destination
```

Decode mode records the target relationship without consuming render repeat state.

Render mode asks the VM for the actual repeat behavior.

## 10.13 Diagnostics from annotations

Parsers and command cursors should be able to attach diagnostics directly.

```cpp
cmd.warning("Unsupported vibrato shape ignored");
cmd.error("Jump target outside sequence data");
source.diagnostic(pointer.span, Severity::Warning, "Pointer target is outside file");
```

A diagnostic should become both:

```text
a Diagnostic record
a link from the affected SourceAnnotation, if one exists
```

Suggested structure:

```cpp
enum class Severity : uint8_t {
  Info,
  Warning,
  Error,
};
```

```cpp
struct Diagnostic {
  DiagnosticId id;

  Severity severity;
  std::string message;

  std::optional<SourceSpan> span;
  std::optional<SourceAnnotationId> annotation;
  std::optional<ObjectRef> object;
};
```

## 10.14 SourceMap finalization and fixups

Some relationships cannot be resolved immediately.

Examples:

```text
jump target command annotation is decoded later
instrument object is discovered after the sequence
sample object is discovered after the instrument bank
derived source is created after parent file annotation
```

Allow a finalization step:

```cpp
class SourceMapBuilder {
public:
  SourceMap finish(SourceMapFixupContext context);
};
```

Possible fixups:

```text
SourceSpan target -> SourceAnnotationId target, when an exact command/section exists
bank/program reference -> ObjectRef::instrument, when collection matching is complete
sample index reference -> ObjectRef::sample, when sample set is known
diagnostic span -> nearest containing annotation
```

Fixups should add information, not erase the original source-span relationship.

For example, a jump can keep both:

```text
JumpTarget -> SourceSpan{source, address, 1}
JumpTarget -> SourceAnnotationId{targetCommand}
```

That makes HexView robust even if annotation resolution is imperfect.

## 10.15 Consumer expectations

Simple consumers can rely on these conventions.

For a sequence command annotation:

```text
role == SourceRole::Command
sequenceSemantic has value
label is display name
localKind is slugified or overridden
fields contain opcode and operands
links contain static references
owner may point to SequenceCommand
```

For a pointer:

```text
role == SourceRole::Pointer or Field
links include PointsTo
field value usually displays as Address
```

For an instrument row:

```text
role == SourceRole::Instrument or TableRow
owner points to ObjectRef::instrument(...)
links may include UsesSample
```

For sample data:

```text
role == SourceRole::Sample or Payload
owner points to ObjectRef::sample(...)
```

For misc data:

```text
role == SourceRole::DataBlock, Section, Field, Pointer, or Payload
owner may point to ObjectRef::misc(...)
```

These conventions are enough for first-pass HexView, source outline, and inspector consumers without introducing a larger visualization service.

---

# 11. SequenceProgram shape

Keep `SequenceProgram` as the sequence-specific source/execution model.

```cpp
struct SequenceProgram {
  DialectId dialect;

  std::vector<SequenceTrackEntry> tracks;
  std::vector<SourceCommand> commands;

  SequenceProgramBehavior behavior;
};
```

```cpp
struct SequenceTrackEntry {
  TrackId track;
  Address entryAddress;

  std::optional<SourceCommandId> entryCommand;
  std::optional<SourceAnnotationId> sourceAnnotation;
};
```

```cpp
struct SourceCommand {
  SourceCommandId id;

  CommandHandlerId handler;
  CommandKindId kind;

  uint8_t opcode;
  Address address;

  SourceSpan span;
  std::vector<uint8_t> bytes;

  SourceAnnotationId annotation;
};
```

No `StaticSequenceMap` yet.

Static flow references can live in `SourceAnnotation.links`.

Example:

```text
Jump command annotation:
  role: Command
  sequenceSemantic: Jump
  link: JumpTarget -> target SourceSpan or target command annotation

Call command annotation:
  role: Command
  sequenceSemantic: Call
  link: CallTarget -> target SourceSpan or target command annotation

Repeat command annotation:
  role: Command
  sequenceSemantic: Repeat
  link: RepeatTarget -> target SourceSpan or target command annotation
```

If later we need a specialized flow graph for source navigation, add it then.

---

# 12. Minimal command kind model

Keep command kind lightweight.

```cpp
struct CommandKind {
  CommandKindId id;

  std::string localKind;       // slugified from displayName unless overridden
  std::string displayName;     // "Program"
  SequenceSemantic semantic;   // Program

  CommandPlaybackStatus playbackStatus;
};
```

`localKind` is useful, but not sacred.

Slugification is acceptable:

```cpp
cmd.name("Program");          // localKind = "program"
cmd.name("Repeat Until");     // localKind = "repeat-until"
cmd.name("Pitch Bend Range"); // localKind = "pitch-bend-range"
```

Override only when needed:

```cpp
cmd.name("End of Track").kind("end");
```

This resolves the earlier overemphasis on stable kind strings.

---

# 13. Sequence output during render

Even without designing a piano roll, the VM still needs a target-neutral output model for export.

The cursor-backed runtime should emit the same kind of target-neutral events the VM branch already uses or wants:

```cpp
class SequenceOut {
public:
  void note(uint8_t key, Level velocity, uint32_t duration);
  void rest(uint32_t duration);

  void instrument(uint32_t bank, uint32_t program);
  void level(Level value);
  void pan(Pan value);
  void pitchBend(PitchBend value);
  void tempo(Tempo value);

  void marker(std::string_view label);
};
```

Each emitted event can internally carry origin metadata:

```cpp
struct EventOrigin {
  SourceCommandId command;
  SourceAnnotationId annotation;
};
```

This does not require a `SequenceTimeline`. It simply preserves the link between output events and source commands, which will be useful later.

---

# 14. Simple consumers

No `VisualizationService` yet.

Consumers can directly use `SourceMap`.

## 14.1 HexView

HexView needs:

```cpp
sourceMap.intersecting(visibleSpan);
sourceMap.at(sourceId, hoveredOffset);
sourceMap.get(annotationId);
```

Coloring rule:

```text
if annotation.role == Command and annotation.sequenceSemantic exists:
  color by SequenceSemantic
else:
  color by SourceRole
```

For first pass:

```text
Note       -> note color
Rest/Wait  -> timing color
Program    -> instrument color
Level      -> level/control color
Pan        -> pan/control color
Pitch      -> pitch/control color
Tempo      -> tempo color
Jump/Call/Return/End/Loop/Repeat -> flow color
Unknown/Unsupported -> warning-ish color
```

The theme can live in UI code. Format code only supplies `SequenceSemantic`.

## 14.2 Source outline

A simple source outline can be generated from annotations with parents:

```text
Header
Track Pointer Table
Track 0
Track 1
Instrument Table
Sample Table
Unknown Control Block
```

No custom model needed yet.

## 14.3 Command inspector

Given a command annotation:

```text
display label
opcode
operands
derived fields
links
diagnostics
```

All of that comes from `SourceAnnotation`.

---

# 15. Typed command escape hatch

The existing typed command approach should stay available, but demoted.

Use it for:

```text
unit tests
generated tables
very regular instruction sets
commands where parse/execute split is genuinely clearer
advanced internal VM tests
```

But default documentation and new drivers should use:

```cpp
driver.read(readCommand);
```

not:

```cpp
struct Program { ... };
map.op<0x81, Program>("Program");
```

The feedback explicitly argues for this demotion of typed command structs to an escape hatch, and I agree.

---

# 16. Driver helper levels

Avoid one giant switch as the only option, but keep the switch/lambda style primary.

## Level 1: small declarative helpers for boring commands

```cpp
driver.op(0xc1, "Volume")
      .semantic(SequenceSemantic::Level)
      .u8("volume")
      .emit([](auto& rt, uint8_t volume) {
        rt.level(LevelScale::linearFromMidi7(volume));
      });

driver.op(0xfd, "Return")
      .semantic(SequenceSemantic::Return)
      .returns();

driver.op(0xff, "End")
      .semantic(SequenceSemantic::End)
      .ends();
```

The helper slugifies `"Volume"` to `"volume"`.

## Level 2: switch/lambda reader for normal commands

```cpp
driver.read(readNdsCommand);
```

This is the default.

## Level 3: typed commands for special cases

```cpp
map.op<0x81, ProgramCommand>("Program");
```

This is allowed, not preferred.

The feedback also identified this three-level authoring model as a way to avoid making every driver either a giant switch or a typed-command framework.

---

# 17. Diagnostics

Keep diagnostics simple and source-linked.

```cpp
enum class Severity : uint8_t {
  Info,
  Warning,
  Error,
};
```

```cpp
struct Diagnostic {
  DiagnosticId id;

  Severity severity;
  std::string message;

  std::optional<SourceSpan> span;
  std::optional<SourceAnnotationId> annotation;
  std::optional<ObjectRef> object;
};
```

Cursor examples:

```cpp
cmd.warning("Unsupported command option ignored");
cmd.error("Jump target is outside sequence data");
cmd.unsupported("Command stops playback");
```

Parser examples:

```cpp
source.diagnostic(pointer.span, Severity::Warning, "Pointer target is outside file");
```

---

# 18. Implementation phases

## Phase 1: Add lightweight SourceMap

Implement:

```text
SourceSpan
SourceValue
SourceField
SourceRole
SourceLinkRole
ObjectRef
SourceLink
SourceAnnotation
SourceMap
SourceMapBuilder
```

Keep enums small.

Do not add layers, presentation hints, `VisualizationService`, `StaticSequenceMap`, or `SequenceTimeline`.

## Phase 2: Add SourceMap usage for non-sequence data

Add annotation calls for:

```text
headers
track pointer tables
instrument tables
sample headers
misc sections
```

This proves the source map is useful beyond sequences.

## Phase 3: Add `VmCommandCursor`

Implement cursor reads, naming, slugification, semantic classification, operand recording, source links, diagnostics, and VM flow helpers.

## Phase 4: Refactor command kind vs command handler

Split command identity from execution handler.

Allow many command kinds to share one handler.

## Phase 5: Add cursor-backed dialect adapter

Implement:

```cpp
makeCursorDialect(...)
```

Decode pass:

```text
cursor reads source bytes
cursor records annotations
SourceCommand is produced
static links are recorded
runtime output sink is disabled
VM repeat state is not mutated
```

Render pass:

```text
cursor reads SourceCommand.bytes
runtime output sink is active
VM flow helpers make actual playback decisions
```

## Phase 6: Convert NDS

NDS is the readability proof.

Success criteria:

```text
NDS command code is mostly one readable switch/helper set
no typed command struct per opcode
Program/Note/Rest/Jump/Call/Return/End are easy to understand
HexView can color commands by compact SequenceSemantic
instrument references are structural
VM render behavior is preserved
```

## Phase 7: Convert Capcom SNES

Capcom is the hard proof.

Success criteria:

```text
repeat-until code is readable
repeat-break code is readable
slur/portamento/note attributes remain readable
declared loops remain VM-owned
loop candidates remain VM-owned
finite repeats do not become infinite loops
decode phase does not consume render repeat state
```

## Phase 8: Add focused tests

Test the hybrid failure modes:

```text
cmd.name() slugifies expected localKind
cmd.kind() can override slug
HexView semantic is recorded for Note/Level/Pan/etc.
Program command records structured instrument reference
Jump command records static target link
Call command records static target link
RepeatBreak records target in decode mode even when not taken
RepeatUntil does not mutate VM repeat state in decode mode
render phase uses VM repeat state
typed command API still works as an escape hatch
SourceMap can annotate headers/pointers/instrument rows/misc blocks
```

The feedback’s test focus around phase-sensitive decode/render behavior is especially important because that is the main risk of this architecture.

---

# 19. Guardrails

## 19.1 Format code describes facts, not widgets

Good:

```cpp
cmd.name("Pan").semantic(SequenceSemantic::Pan);
source.header("Header", span);
row.link(SourceLinkRole::UsesSample, ObjectRef::sample(...));
```

Bad:

```cpp
cmd.setHexColor(...);
source.addTreeWidgetNode(...);
source.addContextMenuAction(...);
```

## 19.2 SourceAnnotation does not replace semantic assets

Good:

```text
InstrumentSet owns instruments.
SourceAnnotation identifies bytes and fields for an instrument row.
```

Bad:

```text
SourceAnnotation becomes the instrument model.
```

## 19.3 Structured links are authoritative

Good:

```cpp
cmd.instrumentRef(bank, program);
cmd.target(destination, SourceLinkRole::JumpTarget);
```

Bad:

```cpp
if (cmd.kind == "program") infer instrument reference from operand names.
```

## 19.4 Decode is static, render is dynamic

Decode records possible source facts.

Render chooses playback behavior.

Do not let decode consume actual repeat counters or mutate real playback state.

## 19.5 Add only proven sequence-specific indices

Do not add `StaticSequenceMap` yet.

If `SourceMap + SequenceProgram` cannot support a real consumer, then add the smallest missing sequence-specific structure.

---

# 20. Final first-pass target

The first pass should make this possible:

```cpp
case 0xc4: {
  auto pan = cmd.name("Pan")
                .semantic(SequenceSemantic::Pan)
                .u8("pan");

  rt.pan(PanScale::fromMidi7(pan.value));
  return cmd.next();
}
```

And from that one local piece of driver code, the system gets:

```text
readable driver behavior
source command span
opcode/operand source spans
slugified command kind: pan
sequence semantic: Pan
HexView coloring category
source inspector fields
rendered pan event
origin link back to command
VM-owned control flow
```

That is the right balance.

The design is no longer annotation-first. It is:

```text
CommandCursor-first authoring
SequenceVM-owned playback
lightweight SourceMap for inspection and HexView
semantic assets remain pure
future visualization layers deferred until proven
```

This should let you revive the best parts of the VM branch without reintroducing the authoring friction that made you abandon it.
