# Compiler-cursor prototype review

## Verdict

Adopt the compiler-cursor architecture as the target sequence-authoring model, with strict guardrails on framework growth.

The NDS prototype satisfies the important criterion that the rejected designs did not: an ordinary command is again readable as one short interpreter block. It still gives the value architecture source-once decoding, durable annotations, source-free playback, shared scheduling, and testable control flow.

The result is not free. It adds 761 lines of generic compiler-cursor infrastructure and relies on templates plus a process-local executor registry. That cost is acceptable only because it is centralized and because the format surface is substantially simpler. If later formats force the cursor into a symbolic language or universal command taxonomy, revise it rather than protecting this implementation.

## Representative command comparison

### Original architecture

```cpp
case 0xC1:
  vol = readByte(curOffset++);
  addVol(beginOffset, curOffset - beginOffset, vol);
  break;
```

This is concise and locally understandable. Its costs are outside the block: the live cursor mixes parsing and playback, output is MIDI-oriented, and other lifecycles may need to read the command again.

### Adjacent semantic decode/execution profile

```cpp
profile[0xc1 - 0x80] = commands.command(
    "Volume", SequenceSemantic::Level,
    [](Decode& d) {
      d.resolved("linear_gain", d.rawU8("volume"),
                 LevelScale::linearFromMidi7);
    },
    [](Operands a, Playback& p) {
      p.out.level(a.f64("linear_gain"));
      return Effects{};
    });
```

Decode and playback are adjacent, but the author still has to mentally join two lambdas. The playback lambda repeats the operand identity with a string lookup. This is the design flaw that made even trivial commands feel inflated.

### Rejected standard-command variant

```cpp
case 0xc1: {
  const double gain = decode.resolved(
      "linear_gain", decode.rawU8("volume"),
      LevelScale::linearFromMidi7);
  return decode.command("Volume", SequenceSemantic::Level,
                        standard_command::Level{.linearGain = gain});
}
```

This reduced the NDS file, but understanding `Level` required finding a shared variant and its interpreter elsewhere. It shifted complexity out of view and created a taxonomy that format authors had to classify commands into.

### Compiler cursor

```cpp
case 0xc1: {
  auto event = cursor.command("Volume", SequenceSemantic::Level);
  return event.emitLevel(
      LevelScale::linearFromMidi7(event.u8("volume")));
}
```

The source read and effect are in execution order. `u8` records the field and range. `emitLevel` stores the converted gain and selects a generated executor. Playback performs no field-name lookup and cannot see the source bytes.

State remains equally direct:

```cpp
case 0xc3: {
  auto event = cursor.command("Transpose", SequenceSemantic::State);
  return event.set<&TrackState::transpose>(event.s8("semitones"));
}
```

Commands with several effects keep those effects separate and visible:

```cpp
case 0xc5: {
  auto event = cursor.command("Pitch Bend Range", SequenceSemantic::Pitch);
  const u8 semitones = event.u8("semitones");
  return event
      .set<&TrackState::pitchBendRangeSemitones>(semitones)
      .emitPitchBendRange(semitones);
}
```

Every operation appends one typed action and returns the same builder. Chained calls and separate statements compile to the same ordered action list. This keeps `set` and `emit` honest instead of hiding two effects behind one convenience method.

Runtime-history-dependent behavior has one explicit indirection, to a concrete method beside the switch:

```cpp
case Note: {
  auto event = cursor.command("Note", SequenceSemantic::Note);
  const u8 key = event.opcodeValue("key", cursor.opcode());
  const u8 velocity = event.u8("velocity");
  const u32 duration = event.varLen("duration");
  return event.invoke<&Playback::note>(key, velocity, duration);
}
```

That indirection is useful: it marks the small set of commands whose result cannot be reduced to literals at compile time because it depends on prior track state.

## Line counts

Line counts include comments and blank lines. They measure burden imperfectly, but they expose where complexity moved.

| NDS sequence implementation | `.cpp` | `.h` | Total |
|---|---:|---:|---:|
| Original architecture | 438 | 34 | 472 |
| Value cursor before `SemanticCommand` | 662 | 48 | 710 |
| Adjacent semantic decode/execute | 625 | 35 | 660 |
| Rejected `StandardSequenceCommand` | 399 | 26 | 425 |
| Compiler cursor with composable actions | 504 | 28 | 532 |

The compiler-cursor NDS sequence is 178 lines smaller than the old value cursor and 128 lines smaller than the semantic decode/execute version. It is 60 lines larger than the original architecture. Most of that remaining difference is not ordinary opcode code: it includes reachable-block discovery, exact source annotations, invalid-target diagnostics, and malformed overlapping-SDAT recovery.

The rejected standard-command version is shortest by raw count, but its table omits the shared operation definitions and interpreter needed to understand behavior. It failed the locality criterion despite the smaller format file.

Broader production counts are:

| Scope | Lines |
|---|---:|
| Current value NDS format directory | 1,713 |
| Original NDS format directory | 1,586 |
| Generic `CompilerCursor.h` | 761 |
| Focused compiler-cursor tests | 322 |

The two NDS directory totals are not like-for-like: the value implementation also owns durable source maps, neutral assets, bounded recovery, and source-free VM integration. The important author-facing comparison is the sequence implementation and the individual command blocks.

## Acceptance review

| Criterion | Result |
|---|---|
| One primary block per opcode | Pass |
| Ordinary commands are two to five readable lines | Pass |
| No separate decode and execute lambdas | Pass |
| No operand IDs or keys | Pass |
| Source names appear only at field reads | Pass for playback; analysis may inspect source operands by name |
| No playback string lookup | Pass |
| No playback source-byte access | Pass |
| No format-visible `std::any` or slot management | Pass |
| Multiple effects remain explicit and ordered | Pass |
| Control-flow target describes discovery and execution together | Pass |
| Driver behavior is concrete and locally named | Pass |
| VM still owns scheduling, loops, calls, and repeats | Pass |
| Automatic bounded truncation | Pass |
| NDS summary and synth parity | Pass on all 40 Castlevania collections |
| NDS MIDI migration parity | Pass relative to the pre-cursor value implementation; one original/value mismatch predates this migration |

## Costs and limits

### Central framework size

`CompilerCursor.h` is large. Roughly half is the deliberately explicit set of checked fields and executable operations; the remainder is generated typed dispatch, action composition, and track-walker integration. This is preferable to spreading the same mechanics across formats, but it must not become a home for driver-specific semantics.

The rule should be: express state, output, time, and flow as separate operations and compose them in source order. Use `invoke<&Playback::method>` for substantial or reused driver behavior. Captureless inline handlers are permitted as an escape hatch, but decoded values must remain explicit arguments and no closure is stored.

### Executor registry

Each command action stores a small automatically assigned slot, not a closure or manual ID. The registry is keyed by the format's concrete `Playback` type and stores generated function thunks. It is thread-safe and process-local. Slot stability across application versions is intentionally not promised because sequence-program serialization is outside the architecture's goals.

### Templates

Member pointers give compile-time checking for state and local methods, but an incorrect signature can produce a template diagnostic. The ordinary path uses shallow calls (`set`, `toggle`, `invoke`) rather than expression templates or macros. If real format work exposes unreadable errors, add focused constraints and static assertions; do not add a declarative schema to hide them.

### Multiple historical paths

The repository temporarily contains the compiler cursor, Capcom's semantic profile, and older cursor dialects. That is migration state, not three supported authoring choices. New work should use the compiler cursor. Once affected formats migrate, delete the other paths rather than maintaining compatibility layers.

Preserving the old cursor in live code is not a requirement. The pre-compiler implementations remain recoverable from Git, including the pre-semantic NDS cursor before `2180c74e4` and the pre-prototype branch state at `bd975187e`.

## Recommendation for the next migration

Use the compiler cursor for one medium-complexity format before attempting Akao SNES. Konami SNES is a useful next stress test because it has real control flow and driver state without Akao SNES's full dialect matrix.

During that migration:

- keep one opcode switch and local `Playback` type;
- prefer explicit chained actions; use `invoke` for substantial format-specific semantics;
- reject any helper that makes an ordinary command require a second definition;
- measure command-block and file-level readability again; and
- delete obsolete path-specific infrastructure when its last format leaves it.

Proceed only while the format code remains the clearest layer in the system. That is the architecture's primary constraint.
