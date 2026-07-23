# VGMTrans Value-Oriented Core Architecture Guide

This document explains the new value-oriented VGMTrans core under `src/value`. It is written for someone who has not read the new code yet. It focuses on how the architecture fits together, how the main pieces cooperate, and what the ported formats show about the intended authoring style.

---

## 1. The architecture in one sentence

The new core turns source bytes into plain, copyable data records, keeps all long-lived ownership inside a `Session`, groups related records into exportable `Collection`s, and exports through shared sequence and synth models instead of through format-specific parser objects.

The important shift is this:

```text
Old style, roughly:
  long-lived parser objects own behavior and relationships

New style:
  scanners produce data records
  SessionState owns those records
  snapshots expose read-only copies
  shared engines render/export those records
```

A format scanner no longer needs to create a web of live objects that know about each other. It emits values: a sequence, an instrument set, a sample collection, source annotations, match facts, diagnostics, and extracted child sources. The session validates and accepts those values, then later decides which values belong together.

---

## 2. Big-picture flow

A normal load/scan/export cycle looks like this:

```text
User file bytes
  ↓
SourceStore
  stores user-loaded sources and derived sources
  ↓
FormatRegistry
  offers each source to registered format modules
  ↓
FormatModule::scan
  produces ScanResult values
  ↓
Normalization and validation
  reject an invalid result before durable mutation
  ↓
SessionState
  assets, match facts, explicit collections, source map, diagnostics
  ↓
Collection resolution
  groups assets into exportable collections
  ↓
SessionSnapshot
  read-only copy of the current session state
  ↓
Collection preparation
  may build transient collection-specific values
  ↓
Export
  collection → MIDI, SF2, DLS, WAV, or future formats
```

There are two important sub-flows inside that larger flow.

### Sequence flow

```text
source bytecode
  ↓
CompilerCursor reads one command once
  ↓
SourceCommand with source metadata, stored flow,
  and ordered typed executable actions
  + SourceMap command annotation
  ↓
SequenceVm invokes a generated typed executor
  ↓
PerformanceSequence, a target-neutral musical performance
  ↓
MIDI renderer or another future sequence exporter
```

The compiler cursor exists only while decoding. Its field reads record source metadata, and its event operations append source-free typed actions in written order. Returning the event finalizes the command. The shared VM schedules commands while the compiled dialect invokes their generated executors; neither layer can reopen command bytes. MIDI conversion remains a later layer.

All five ported sequence families use the compiler cursor and source-free command execution.

The prototype's examples, line counts, acceptance review, limitations, and adoption recommendation are recorded in [Compiler-cursor prototype review](compiler_cursor_prototype_review.md).

### Synth/sample flow

```text
source instrument and sample data
  ↓
format synth parser
  ↓
InstrumentSetAsset and SampleCollectionAsset
  ↓
collection resolution pairs them with sequences
  ↓
physical modulation is lowered to target control records
  ↓
SF2 / DLS / WAV exporters
```

The sample bytes usually stay in `SourceStore`. A `Sample` records where the encoded bytes are and how to decode them. Exporters decode only when they need to write WAV, SF2, or DLS output.

---

## 3. What “value-oriented” means in this branch

In this codebase, “value-oriented” means the durable core state is made of ordinary records rather than live parser objects. Those records can be copied into a snapshot, inspected by tests, and passed to exporters without asking the original parser to stay alive.

The most important rules are:

1. **Bytes live in one place.** `SourceStore` owns source bytes. Assets refer back to byte ranges using `SourceRange`.
2. **Objects point to each other by IDs.** Assets, collections, commands, and source annotations use small ID values instead of pointers.
3. **Scanning and exporting are separate.** Scanners discover and describe data. Exporters consume already-scanned values.
4. **Mutable state is contained.** `SessionState` is the single owner of mutable discovered values. `SessionSnapshot` is read-only.
5. **Format code describes local meaning.** Shared systems handle storage, matching, loop policy, export policy, validation, and diagnostics.

This makes the new core much easier to test. A test can construct a small source, scan it, inspect the snapshot, and verify the resulting values without needing the old UI/root object graph.

---

## 4. Directory map

The `src/value` directory is organized around architectural layers:

| Directory | Main job |
|---|---|
| `base` | Small common types, IDs, source ranges, diagnostics, source storage, byte reading. |
| `platform` | Concrete shared platform structures, such as SNES sample-directory and BRR stream readers. |
| `model` | Durable data model: assets, collections, match facts, snapshots, source map. |
| `scan` | Format module API, scan result types, scan result builder, parse cursor, collection resolver helpers. |
| `session` | Mutable session state and orchestration: add sources, scan, validate, resolve collections, export. |
| `sequence` | Parsed sequence programs, command cursor, bytecode walkers, sequence dialects, VM, performance events. |
| `synth` | Target-neutral instrument, region, sample, envelope, loop, codec, and sample decoding model. |
| `export` | Collection export to MIDI, SF2, DLS, and WAV. |
| `validation` | Checks sequence, synth, and scanner output before durable admission. |
| `formats` | Ported format modules: Akao, Akao SNES, Capcom SNES, Konami SNES, and Nintendo DS. |
| `extractors` | Source extractors such as PSF, SPC, and RSN that create derived sources. |

The key design choice is that these layers mostly point downward. Format modules use base, model, scan, sequence, and synth helpers. The session owns storage. Export reads snapshots. This keeps most code from needing to know about the whole application.

---

## 5. Core vocabulary

The code uses several repeated words. Here they are in plain language.

| Term | Meaning |
|---|---|
| `Source` | One byte container known to the session. It can be a user-loaded file or a derived child source extracted from another file. |
| `SourceRange` | A source ID plus offset and size. It answers “which bytes did this value come from?” |
| `Asset` | A scanned item: sequence, instrument set, sample collection, or miscellaneous payload. |
| `Collection` | An export unit. Usually one sequence plus the instrument/sample assets needed to play it. |
| `MatchFact` | A small fact emitted by a scanner to help decide which assets belong together. |
| `SourceMap` | A durable map from byte ranges to meaning: headers, tables, commands, fields, pointers, instruments, samples, and so on. |
| `Diagnostic` | A warning, error, or informational message, usually tied to a source range or object. |
| `Session` | The mutable workspace. It owns sources, assets, facts, collections, source maps, diagnostics, and registries. |
| `SessionSnapshot` | A copyable read-only view of the current session state. UI, tests, and export read this. |
| `SequenceProgram` | A parsed source-level sequence, made of tracks and decoded source commands. |
| `SequenceDialect` | The driver-specific behavior needed to execute a `SequenceProgram`. |
| `PerformanceSequence` | Target-neutral musical events produced by the sequence VM. MIDI conversion happens later. |

---

## 6. The source layer

### 6.1 `SourceStore`

`SourceStore` owns all bytes that the value core refers to. It stores both:

- user-loaded files; and
- derived sources extracted from those files, such as archive members, PSF executable data, SPC RAM, or RSN contents.

A derived source is not a temporary buffer. It becomes a real source in the session. It records its parent and the source range it came from. That matters because another format module can scan the derived source exactly like it scans a user-loaded file.

This is the basis for composable extraction:

```text
RSN archive source
  ↓ extractor
SPC derived source
  ↓ extractor
ARAM derived source
  ↓ Capcom SNES scanner
sequence/instrument/sample assets
```

The source family removal logic also starts here. Removing a parent source removes the derived sources that came from it and then causes the session to remove assets, facts, source annotations, and diagnostics that came from that source family.

### 6.2 `ByteReader`

`ByteReader` is a small checked view over source bytes. It does not own bytes. It knows the `SourceId`, so it can produce correct `SourceRange` values.

It provides helpers such as:

- `u8At`, `s8At`
- `le16`, `be16`, `le32`, `be32`
- `slice`
- `range`
- `has`

The intended pattern is simple: scanner code uses `has()` or `RecordReader` when malformed data should be handled gracefully, and direct reads when the caller has already checked the range.

### 6.3 `SourceRange`

`SourceRange` is one of the most important small types in the new design. It is copied into assets, annotations, diagnostics, samples, sequence commands, and links.

Instead of storing “this sample contains a copy of these bytes,” the model stores “this sample’s encoded bytes are here.” Exporters later ask `SourceStore` for the bytes when needed.

This keeps assets small and gives the UI a reliable way to jump from parsed meaning back to the original file.

---

## 7. IDs and object references

The core uses typed IDs for durable references:

- `SourceId`
- `AssetId`
- `CollectionId`
- `TrackId`
- `CommandId`
- `SourceAnnotationId`

The IDs are small values. They are not owning pointers. This matters for snapshots: a snapshot can be copied or moved without invalidating object references, because relationships are stored as IDs and rebuilt indexes.

`ObjectRef` is a slightly richer reference used by source annotations and diagnostics. It can point at a whole asset, a sequence, a sequence track, an instrument, a playable region, a sample, or a miscellaneous object. Instrument, region, and sample references use their durable dense indexes, never a format's sparse table key.

The architecture uses IDs as a safety boundary. Format code can describe relationships, but it does not decide where data is stored in memory or how long it lives.

---

## 8. Assets and metadata

All durable parsed objects are assets. The main asset types are:

```text
Asset
  = SequenceProgramAsset
  | InstrumentSetAsset
  | SampleCollectionAsset
  | MiscAsset
```

Every asset has `AssetMetadata`:

- ID
- format name
- display name
- source range

The metadata gives exporters, UI, tests, and diagnostics a common way to name and locate assets.

### 8.1 `SequenceProgramAsset`

A sequence asset contains a `SequenceProgram`. This is not MIDI. It is a source-level model of the original driver commands.

A `SequenceProgram` contains:

- a dialect ID;
- a timebase;
- a source base address;
- per-program driver profile/configuration;
- playback behavior defaults; and
- tracks.

Each track contains decoded `SourceCommand` records. A `SourceCommand` keeps:

- opcode;
- named source operands with values and source ranges;
- decode-time control flow;
- ordered executable actions, each containing positional values and an executor slot, for compiled commands;
- source address;
- encoded size;
- source range;
- optional source annotation ID.

An operand's name is author-facing vocabulary for source inspection and analysis. It also keeps a display rule, optional generic role, exact source range, and—when useful for presentation—both encoded and resolved values. Compiler-cursor execution does not consume this named list. Each action stores only the positional values required by its generated typed executor, so playback needs neither operand IDs nor string lookup.

Complete source-free commands do not retain encoded bytes. The executor signature intentionally receives no `TrackProgram` or byte reader, making source reparsing during playback impossible.

### 8.2 `InstrumentSetAsset`

An instrument set contains `Instrument` records. Each instrument has:

- an optional source-domain `InstrumentIdentity`;
- an optional explicit `InstrumentAddress` for formats that require a particular export bank/program;
- name;
- source range;
- regions;
- default reverb;
- physical vibrato/tremolo specifications, with raw generators and modulators available only as an escape hatch.

`resolveInstrumentAddress` is the only identity-to-address policy. It uses an explicit address when present; otherwise it maps the identity key sequentially across 128-program banks. MIDI, SF2, DLS, and parity summaries all use that resolver, so a format cannot accidentally assign different programs to different targets.

A `Region` describes one playable zone: key range, velocity range, sample reference, tuning, envelope, optional region-local loop, pan, and attenuation.

### 8.3 `SampleCollectionAsset`

A sample collection contains `Sample` records. A sample records:

- name;
- codec;
- encoded data source range;
- sample rate;
- channels;
- bit depth;
- loop information;
- pitch/tuning;
- codec parameter;
- attenuation.

The encoded bytes are not copied into the sample. They stay in `SourceStore`.

### 8.4 `MiscAsset`

A miscellaneous asset is a simple payload for data that does not yet fit the sequence or synth model. It gives the architecture an escape hatch without forcing every source structure into a permanent model too early.

---

## 9. Source map

`SourceMap` is the bridge between parsed values and source bytes. It is not the main data model. It is a durable annotation layer that answers questions like:

- Which bytes are the SDAT header?
- Which bytes are this sequence command?
- Which operand is the tempo value?
- Which pointer points to this track?
- Which source range belongs to this sample?
- Which command selected this instrument?

A source annotation contains:

- ID;
- primary source range;
- role, such as header, table, pointer, command, instrument, or sample;
- label and description;
- optional sequence meaning, such as note, rest, tempo, pan, jump, or end;
- optional playback status;
- owner object reference;
- parent annotation;
- fields;
- links.

Fields record named values inside an annotation. Links connect an annotation to another byte range, annotation, or object.

A key rule appears directly in the code comments: persistent source annotations must be source-backed. In other words, a stored annotation should have a real primary source range. Derived facts that do not have their own bytes should be fields on a source-backed annotation rather than standalone annotations.

This rule keeps the source map practical. It does not become a second application model. It stays anchored to bytes.

### 9.1 `SourceMapBuilder`

Format scanners usually do not construct `SourceAnnotation` records by hand. They use `SourceMapBuilder` and `AnnotationBuilder`:

```cpp
sourceMap.header("SSEQ Header", range)
         .kind("sseq-header")
         .owner(ObjectRefs::sequence(id))
         .field("data_offset", dataOffsetRange, dataOffset, SourceValueDisplay::Address);
```

The synth content builders use the same annotation API. Calling `source()` on a sample, instrument, or region entry supplies the appropriate role and exact `ObjectRef`; the format can then add fields, parents, descriptions, and unusual links with the ordinary `AnnotationBuilder`. This is how future logical TreeView nodes and source-oriented HexView records can meet without either view reinterpreting format rules.

Semantic sequence decoders do not call annotation-builder methods. They return command presentation data and self-describing operands; `CommandSourceMap` projects the command label, opcode, fields, derived values, control-flow links, and instrument links generically.

### 9.2 Why this matters

The source map is one of the strongest parts of the new architecture. It makes debugging, HexView integration, tests, and diagnostics all point to the same parsed snapshot. A command that fails to export can still point back to the exact source bytes that produced it.

---

## 10. Scanning

Scanning is the process of turning one source into values.

A format definition owns the scanner and any source-free dialects it needs:

```text
FormatDefinition
  module
    name
    scan(input)
    optional collection resolver
    optional collection preparer
  zero or more sequence dialects
```

`Session::registerFormat` registers the whole definition. The mutable format and dialect registries are not exposed separately.

Recognition belongs at the start of `scan`, which returns an empty result when the source does not match. This ensures layout/signature discovery runs once. `canScan` remains nullable as a migration adapter for older modules and should not be added to new ones.

### 10.1 `ScanInput`

A scan receives:

- a `SourceFile` value;
- a `ByteReader` over the source bytes;
- the session ID allocator.

The scanner does not receive mutable session state. It cannot directly append assets to the session. It can only return a result.

### 10.2 `ScanResult`

A scan result can contain:

- assets;
- match facts;
- explicit collections;
- source map annotations;
- diagnostics;
- extracted sources.

This is a useful boundary: scanners can produce partial results and warnings, but the session decides whether the result is valid enough to accept.

### 10.3 `ScanResultBuilder`

`ScanResultBuilder` is the main convenience API for format authors. It hides repetitive metadata setup, ID allocation, simple collection creation, match facts, diagnostics, source map access, and handle validation.

A simple scanner can read naturally:

```cpp
ScanResultBuilder result(input, "CapcomSnes");
const std::string displayName = result.sourceDisplayName();
const auto sequence = result.reserveSequence();

result.sequence(sequence, displayName, layout.sequenceHeaderRange)
      .program(decodeCapcomSnesSequence(
          input.reader, layout, sequence.id,
          &result.sourceMap(), &result.diagnostics()));

result.sourceCollection(displayName)
      .sequence(sequence);

return result.finish();
```

The builder has typed handles such as `ScanSequenceRef`, `ScanInstrumentSetRef`, and `ScanSampleCollectionRef`. A scanner can reserve handles before parsing. This is important when assets need to refer to each other before all of them have been built.

`sourceDisplayName()` applies the common title, filename, path, and format-name fallback. `sourceCollection()` is for formats that produce one collection per source; unlike `collection()`, changing the display name does not change that collection's identity.

For example, an instrument set may need to refer to a sample collection that has not been committed yet. The scanner can reserve a sample collection ID, use it in regions, and then commit the sample collection later in the same scan result.

The builder tracks whether reserved handles were actually committed. That catches a common class of scanner bugs before the result is accepted.

In the normal case, synth parsers let the result reserve each asset while creating its content builder:

```cpp
auto samples = result.samples();
auto instruments = result.instruments();
```

The overloads that accept an existing handle remain available when another object needed the ID first. Moving either completed builder into `sampleCollection(name, ...)` or `instrumentSet(name, ...)` commits an ordinary value asset. The content builder retains no lifecycle after that explicit commit. For a committed sample builder, the scan result keeps only its source-key lookup so a later instrument table can resolve sparse format indexes through the ordinary collection handle.

### 10.4 Random-access records

`RecordReader` handles both sequential records and fixed layouts. Its `*At` and `rangeAt` methods read relative to the record start, report malformed fields, and return ranged values that can be passed directly into source map fields.

---

## 11. Admission and validation

A scanner returns a `ScanResult`, but the session does not blindly trust it. The session fills in default diagnostic ranges, validates the complete result against the source store and existing assets, and only then moves it into `SessionState`.

The validation layer checks things such as:

- asset IDs are present and unique;
- asset IDs do not reuse existing IDs;
- asset primary ranges belong to the source being scanned;
- source ranges point at active source bytes and are in bounds;
- source map annotations have valid ranges;
- annotation parent and link IDs are valid;
- diagnostics do not point at missing annotations;
- match facts point at known assets and sources;
- extracted source origins are valid;
- sequence, instrument, and sample data pass their own model checks.

`ScanResult` is both the scanner output and the staging value. There is no second commit-shaped copy of the same data. Validation is the admission gate between “a format scanner tried to parse something” and “the session now owns this data,” and all validation completes before durable vectors are changed.

That boundary is healthy. It means individual format modules can stay focused on parsing rather than duplicating session bookkeeping rules.

---

## 12. Session and state

`Session` is the mutable workspace. It owns:

- `SourceStore`
- one private `SessionState`
- `FormatRegistry`
- `SequenceDialectRegistry`
- `ScanIdAllocator`
- the set of sources already scanned

The public session API is small:

- add a source;
- remove a source;
- scan one source;
- scan pending sources;
- get a snapshot;
- export a collection or all collections.

Mutation calls return after changing the session. Callers request a snapshot explicitly when they need to publish or inspect the resulting state, which lets a batch of mutations produce one snapshot rather than one full copy per operation.

### 12.1 Registries are sealed before use

The session seals the format and dialect registries before adding, scanning, removing, or exporting. This prevents the meaning of already-scanned assets from changing halfway through a session because a new format or dialect was added late.

### 12.2 Scanning a source scans its derived sources

When `Session::scanSource` scans a source, it also scans any derived sources created by extractors during that scan. This happens through a queue.

This design lets extractors and normal format modules compose without special cases. A PSF extractor can produce executable bytes. Then the Akao scanner can scan those bytes as a normal source.

### 12.3 One state owner, organized by value type

`SessionState` stores flat vectors of assets, match facts, explicit collections, annotations, resolved collections, and diagnostics. It keeps only the indexes needed for identity checks and asset lookup.

The important boundary is not one class per vector. It is one private class per consistency domain. A scan admission, source removal, or asset removal can affect several value types at once, so `SessionState` owns those transitions:

- `appendScan` publishes one already-validated result;
- `removeSources` and `removeAssets` compute the affected IDs once;
- dependent facts, explicit collections, annotations, links, and diagnostics are removed or scrubbed together;
- collection reconciliation preserves stable identities and marks collections stale when referenced assets disappear.

This keeps cross-vector invariants visible in one place and removes store-to-store coordination code without creating a public root-object API.

### 12.4 Snapshot creation

`Session::snapshot()` copies the current source and state contents into a `SessionSnapshot`. The snapshot is copyable and read-only. It contains:

- sources;
- assets;
- match facts;
- collections;
- source map;
- diagnostics.

It also builds indexes for fast asset and collection lookup by ID.

Snapshot creation is a projection, not another validation boundary. `SessionState`
already owns the invariants for admitted assets, annotations, diagnostics, and
collections, so rebuilding the same checks while copying state would create two
sources of truth. The raw constructor remains private. Tests that need a small
synthetic export input use a fixture builder under `tests/`; production callers
can only obtain snapshots from `Session`.

UI and tests read explicit snapshots rather than mutable state. Export operations create one stable snapshot for the duration of the export. Internal collection rebuilding instead borrows a lightweight `MatchContext`, and source inspection selects annotations directly, so neither path builds and discards a complete snapshot.

---

## 13. Collections and matching

A `Collection` is the unit that gets exported. It may contain:

- one sequence;
- zero or more instrument sets;
- zero or more sample collections;
- miscellaneous assets;
- status and issues.

Collections can be complete, incomplete, ambiguous, or stale.

The architecture separates scanning from collection matching. This is important because some formats know the full collection while scanning, while others only know partial relationships.

### 13.1 Explicit collections

The simplest case is when the scanner already knows what belongs together. Nintendo DS SDAT is a good example. The SDAT layout tells which sequence uses which bank and wave archives.

In that case, the scanner can directly emit an explicit collection:

```cpp
result.collection(name, key)
      .sequence(sequenceAsset)
      .instrumentSet(bankAsset)
      .samples(psgSamples)
      .samples(waveArchiveSamples);
```

These explicit collections are stored and later reconciled like any other desired collection.

### 13.2 Match facts

Some formats cannot bind everything during scanning. For those, scanners emit facts. A fact says something small and durable, such as:

- this sequence has ID 12 in this domain;
- this sample collection covers articulation IDs 40 through 59;
- this sequence requires articulation IDs 41, 42, and 55;
- this asset came at source offset 0x1234;
- this asset belongs to a named collection.

Facts are not collections. They are evidence used by a resolver.

### 13.3 Resolvers

A resolver reads a `MatchContext` and returns the collections that should exist now. The context is a lightweight borrowed view of the source store, assets, and match facts; it does not copy a public snapshot or expose mutable state.

The helper `MatchFactIndex` makes resolver code easier by giving typed access to facts and assets. `CollectionAssembly` helps build collections while handling duplicate suppression and common missing-role issues.

### 13.4 Stable collection keys

Every resolved collection has a `CollectionKey`:

```text
resolver id + resolver-specific value
```

The key is the durable identity of a collection. When more sources are loaded, the resolver may return a collection with the same key but more complete membership. `SessionState` uses the key to update the existing collection rather than creating a duplicate.

### 13.5 Collection-dependent preparation

Akao sequences and sample collections are scanned separately. After the resolver chooses the sample collections, export may need a collection-specific instrument set whose regions point at those chosen samples.

`FormatModule::prepareCollection` receives the source store, current snapshot, and resolved collection. It returns transient instrument sets and diagnostics. Exporters consume those values directly; they never enter `SessionState`, receive persistent IDs, or participate in collection rebuilding and stale removal.

The remaining Akao debt is source rereading during preparation. Durable symbolic articulation data would let preparation operate only on snapshot values, but that is independent of the removed second asset lifecycle.

---

## 14. Sequence architecture

The sequence architecture is the largest design change in the branch. It separates four concerns that used to be easy to mix together:

1. decoding source commands;
2. recording source annotations;
3. executing source-driver behavior;
4. converting musical events to MIDI.

The result is this pipeline:

```text
ByteReader
  ↓
format decoder + source-map projection
  ↓
TrackProgram / SequenceProgram
  ↓
global SequenceVm scheduler
  ↓
PerformanceSequence
  ↓
PerformanceMidiRenderer
  ↓
MidiSequence
  ↓
MidiExporter
```

### 14.1 `SequenceProgram`

`SequenceProgram` is an immutable parsed source program, not a live player. It owns driver profile/configuration and tracks of `SourceCommand` records. Every compiled command has an opcode, named source operands, stored decode flow, its source address/range, an annotation ID, and an ordered list of typed executable actions.

The stored flow lets walkers and validators inspect control flow without executing format code. Named operands serve source presentation and analysis; playback consumes positional executable values and performs no string lookup. A format profile belongs to this program value, so one registered executor family can handle every version of the driver.

### 14.2 `SequenceDialect`

A source-free dialect is the small piece of driver-specific behavior needed by the VM. It contains:

- an ID;
- a timebase;
- default behavior;
- an optional program-state factory for genuinely shared driver state;
- a function to create per-track state;
- a function to execute one semantic command.

The dialect executor receives the command, program state, track state, `PerformanceEmitter`, and `VmApi`. It does **not** receive a `TrackProgram`, source bytes, or global registry context. For compiler-cursor formats, the shared adapter creates the format's concrete `Playback` and invokes the command's generated typed thunk. Runtime-relevant version data may initialize program or track state; source-only version choices should be resolved while decoding.

`makeCompiledDialect<TrackState, Playback>(dialect)` fills the mechanical track-state factory and executor hooks. The format's dialect declaration therefore only spells out its identity, timebase, defaults, and any genuinely format-specific program state.

The dialect is registered once in `SequenceDialectRegistry`; export looks it up by family ID. Capcom SNES, for example, registers `capcom-snes` once and resolves its V1/V2/V3 source conversions before playback.

### 14.3 Compiler-cursor decode and execution

The target authoring model is one imperative opcode block. A command's label, source reads, conversion, discovery flow, and playback behavior appear in reading order:

```cpp
case Volume: {
  auto event = cursor.command("Volume", SequenceSemantic::Level);
  return event.emitLevel(volumeGain(event.u8("volume")));
}
```

State changes use typed member pointers, without IDs or a second executor definition:

```cpp
case Transpose: {
  auto event = cursor.command("Transpose", SequenceSemantic::State);
  return event.set<&TrackState::transpose>(event.s8("semitones"));
}
```

Behavior that cannot be expressed as simple state reads and output values uses a nearby `Playback` method:

```cpp
case Note: {
  auto event = cursor.command("Note", SequenceSemantic::Note);
  const u8 key = event.opcodeBits<0, 5>("key");
  const u32 duration = event.varLen("duration");
  return event.invoke<&Playback::note>(key, duration);
}
```

`CompilerCursor` wraps `RecordReader`. Field reads automatically record names, values, exact ranges, display rules, and roles. Calls such as `emitLevel`, `set`, `invoke`, `jump`, or `end` append typed executable actions and update discovery flow. Control-flow operations also set `AffectsControlFlow` and assign the matching target role to the decoded address; formats should not repeat either declaration. Every call returns the same `Event&`, so actions may be chained or written as separate statements. Returning the final event expression converts the builder into the decoded command.

Use the cursor constructor without an end offset when commands may read through the complete `ByteReader`. Pass an explicit end only when a real source subrange constrains command reads.

This makes compound behavior explicit without inventing compound operations:

```cpp
case PitchBendRange: {
  auto event = cursor.command("Pitch Bend Range", SequenceSemantic::Pitch);
  const u8 semitones = event.u8("semitones");
  return event
      .set<&TrackState::pitchBendRangeSemitones>(semitones)
      .emitPitchBendRange(semitones);
}
```

The non-chained form compiles identically:

```cpp
event.set<&TrackState::something>(value);
event.emitExpression(expression);
return event.wait(duration);
```

Simple output values that depend on runtime state remain local through deferred values. `state()` reads the member when the consuming action executes, so it observes earlier actions in the same command. `select()` explicitly supplies both results without hiding whether the false case emits zero or emits nothing:

```cpp
const auto modulationEnabled =
    event.state<&TrackState::modulationEnabled>();

event.set<&TrackState::tremoloAmount>(amount);
return event.emitModulation(
    ModulationPerformanceTarget::TremoloDepth,
    event.select(modulationEnabled, amount, 0.0));
```

Deferred values deliberately support only constants, track-state reads, and selection. They are read-only and have no side effects. Do not grow them into a general expression language: use a captureless inline handler for complex one-off behavior or a nearby `Playback` method for substantial or reused behavior.

Each action selects a generated typed thunk and receives positional values. Deferred expression shape and state-member identity live in that generated type; only literal constants enter the durable action arguments. The compiled dialect executes actions in written order, combines elapsed time, and rejects conflicting control-flow results. There is no execute switch, operand-key declaration, captured closure, or playback-time string lookup.

Captureless inline handlers are available for small exceptional behavior through `event.invoke(handler, arguments...)`. The handler type generates a thunk; no closure object is stored. Captures are forbidden, so decoded values remain explicit IR arguments. Prefer named operations or a nearby `Playback` method whenever they are clearer.

Malformed reads automatically discard accumulated actions and turn the command into a terminal truncated record. Complete commands retain no source bytes. `CommandSourceMap` projects the temporary decoded record into an annotation, so ordinary command blocks do not manually build fields or links.

Do not introduce a command class hierarchy, handler-per-opcode files, binary-schema DSL, declarative command table, typed command variant taxonomy, or format-visible microcode language. The internal action list exists only to preserve the order of ordinary imperative statements. Keep the complete source-driver operation visible in one local block, and do not hide multiple side effects behind one convenient-sounding operation.

Capcom's former adjacent decode/execution profile was the predecessor to this model and was removed after the compiler-cursor migration.

### 14.4 Bytecode walkers

The code provides linear and reachable bytecode walkers. They accept a command decoder and build address-indexed tracks:

- `decodeLinearBytecodeTrack`
- `decodeReachableBytecodeBlocks`

A linear track is mostly decoded in byte order. A reachable track follows stored static targets such as jumps and calls.

Formats keep one `TrackDecodeScope` in their sequence decode context and call `tracks.linear(...)` or `tracks.reachable(...)`. The scope holds sequence-wide discovery limits and source-map ownership, then wraps the underlying walker with the shared track lifecycle: create the track annotation, project each decoded command, and finalize the track's source range. Relative-address bases, semantic bounds, and diagnostics remain in the format decoder because they affect command meaning rather than track discovery. Exceptional walkers call `tracks.begin(...)`, append decoded commands, and finish the session without rebuilding that lifecycle.

### 14.5 `SequenceVm`

For source-free dialects, `SequenceVm` owns one program state and one runtime per track. It repeatedly executes the active track with the lowest `(tick, stable track order)`. At equal ticks, an earlier track keeps control while it consumes zero-time commands until it waits or ends. This explicit rule matches multi-channel drivers and makes shared state deterministic.

The VM also owns shared playback policy:

- advancing ticks;
- jump behavior;
- call stack behavior;
- return behavior;
- repeat counters;
- loop detection;
- loop export policy;
- command limits;
- warnings for missing targets;
- stopping all tracks at the first loop for legacy drivers that opt into that behavior.

Semantic loop detection compares the complete execution state that determines recurrence: command address, call stack, and repeat counters. A finite repeat therefore does not need an address-based suppression exception, and the same repeat command can be reused safely after its counter has been cleared. The command limit remains an emergency guard rather than normal loop control; its diagnostic includes track, address, tick, executed-command count, and limit.

Semantic tracks share one finite export boundary. Each track records when it has completed the requested number of infinite-loop repeats, while shorter tracks keep looping until every track has reached its own endpoint or ended naturally. The scheduler then uses the longest endpoint, trims events at that common boundary, and clips notes that cross it. This prevents both early truncation by a short auxiliary loop and a lone long track continuing after every other track has stopped.

### 14.6 `PerformanceSequence`

The VM does not output MIDI directly. It outputs a `PerformanceSequence` made of target-neutral events:

- note;
- tempo;
- time signature;
- instrument;
- level;
- expression;
- pan and source-engine stereo balance;
- master level;
- reverb;
- tuning;
- pitch bend;
- pitch bend range;
- portamento;
- legato;
- modulation;
- marker.

These events use musical or normalized values rather than MIDI controller bytes. For example:

- level is linear gain;
- legacy pan is -1.0 to 1.0, while stereo balance is independent left/right source gain;
- pitch bend is semitones;
- modulation amount is normalized.

Instrument events normally carry a source-domain identity rather than a pre-encoded bank/program pair. Level and expression events may carry neutral source quantization (the number of distinct source values), not a destination bit width. Legacy cursor dialects still populate their older compatibility fields.

This is what makes future non-MIDI sequence export more plausible. The VM output is not locked to MIDI’s limitations.

### 14.7 MIDI rendering

`PerformanceMidiRenderer` converts `PerformanceSequence` into `MidiSequence`. This is where target-specific choices happen:

- channel assignment;
- bank select style;
- 7-bit or 14-bit volume/expression;
- pan conversion;
- pitch bend quantization;
- global transpose handling;
- MIDI controller choices;
- end-of-track writing.

It resolves source instrument identities against the collection's instrument sets, then uses the shared instrument-address resolver for MIDI bank/program selection. The synth export preparation layer uses the same resolved address for SF2 and DLS.

For stereo balance, the renderer chooses the nearest discrete equal-power MIDI pan position and emits expression compensation based on that exact quantized position. Format code emits only the source engine's left/right gains. This keeps MIDI rounding out of Capcom SNES, Akao, and Akao SNES and prevents pan and loudness from being calculated with different rounding.

Full source-format modulation range is the default. The renderer can use modulation usage analysis when the caller explicitly requests observed-sequence-range scaling to trade unused range for more controller resolution.

---

## 15. Synth architecture

The synth model is shared by SF2, DLS, WAV, and future exporters. It describes instruments and samples in neutral terms.

### 15.1 Instruments and regions

An `Instrument` has an optional source-domain identity, an optional explicit export address, and a set of regions. Most formats need only the identity; explicit addressing is reserved for source formats with meaningful banks. A region describes when and how a sample should play:

- key range;
- velocity range;
- sample reference;
- root key;
- coarse/fine tuning;
- envelope;
- optional region-local loop;
- pan;
- attenuation.

This is flexible enough for the ported formats. Akao exposed an important need: loop information can belong to an articulation or region, not only to a raw sample. The model handles that with `Region::loop`, which can override `Sample::loop`.

### 15.2 Instrument modulation

Ordinary instrument modulation is described in physical units: vibrato depth in cents, tremolo depth in decibels, rate ranges in hertz, and delay ranges in seconds. Format code therefore states the source driver's behavior directly instead of constructing SF2/DLS-style generator and modulator records. Export preparation lowers these specifications once into the existing low-level records, preserving target output and modulation-scaling policy. `customGenerators` and `customModulators` remain a narrow escape hatch for uncommon routing.

### 15.3 Samples

A `Sample` records encoded source data and decode settings. The encoded bytes stay in `SourceStore` until export. This keeps scanning cheap and preserves source-backed diagnostics.

Sample codecs currently include PCM, SNES BRR, NDS IMA ADPCM, NDS PSG, PSX ADPCM, and OKI ADPCM.

SNES formats share `SnesBrrCatalog`: it validates referenced SPC DIR entries and BRR streams, keeps SRCN order, resolves aliases that point to the same stream, builds neutral samples, and emits the standard DIR/BRR source annotations. Formats retain only their instrument-table rules and any genuinely format-specific sample-index behavior.

### 15.4 Synth content builders

Normal synth conversion uses two independent content builders:

- `SampleCollectionBuilder` maps format source keys to stable, dense `SampleRef` values while building one sample collection.
- `InstrumentSetBuilder` builds one instrument set, assigns dense instrument and region ownership, and projects sample-use links.

There is deliberately no object that owns “the synth.” Collections still decide which independent instrument sets and sample collections belong together, so NDS-style many-to-many relationships require no special mode.

Format code constructs the durable model values directly:

```cpp
auto sample = samples.find(info.srcn);
if (!sample) {
  instruments.warning("Instrument sample was not found", info.range);
  continue;
}

auto instrument = instruments.add(info.index, Instrument{
    .identity = instrumentIdentity(info),
    .name = instrumentName(info),
});
instrument.source(instrumentName(info), info.range, info.kind)
    .fields(info.fields);
instrument.region(*sample, Region{
    .rootKey = rootKey(info),
    .envelope = envelope(info),
}).source("Region", info.range, info.regionKind);
```

`add` diagnoses an accidental duplicate grouping key. `getOrAdd` explicitly allows several source entries to contribute to one instrument, such as a percussion kit. The grouping key is temporary lookup state and never silently becomes the instrument identity or export address. `append` accepts values that need no lookup key, and pre-populated regions remain valid.

KonamiSnes is a representative mixed case. It uses `getOrAdd` to state directly that several percussion entries form one kit, while `RecordReader` retains every encoded instrument field. Its unusual transformed-address sample lookup and sample-zero fallback stay in one plainly named Konami helper. The generic builder handles grouping, dense owners, ranges, and sample links without learning that format rule.

NDS demonstrates separate asset lifetimes. When a SWAR sample builder is committed, `ScanResultBuilder` retains its source-key lookup for the rest of that scan. A later SBNK receives up to four ordinary sample-collection handles and resolves the encoded archive slot and source sample number with `sampleByKeyOrWarning()`. Invalid SWAR entries can therefore leave holes without shifting later references, and several banks can reuse one SWAR without a combined synth owner. The temporary source keys never enter the durable model or the NDS format interface.

`source()` returns the existing `AnnotationBuilder`. The shared code supplies dense owners, basic fallback annotations, range accumulation, and order-independent sample links; the format supplies meaningful labels, fields, table structure, and descriptions. A region owner contains both its instrument and region indexes, so several disjoint source records can all point to one logical region.

`value()` is a narrow escape hatch for unusual format adjustments, not a second construction path. `regionAt()` similarly permits an exact source record to be attached to a region that was already present in an ordinary `Instrument` value. The builder rechecks final model ranges during `finish()`, but additions should normally use `add`, `append`, and `region()` so stable indexing and links stay automatic.

Both builders work without a source-map sink for detached value construction. Their `source()` calls remain harmless and still contribute ranges. Detached code can retain a `SampleRefLookup`; ordinary scan code gets the same behavior automatically from `ScanResultBuilder` after committing a sample builder.

### 15.5 Decoded samples

`DecodedSample` is the temporary PCM form used by WAV, SF2, and DLS exporters. It contains interleaved signed PCM16, sample rate, channels, and loop information.

The distinction is clean:

```text
scanned model:
  Sample = source range + codec metadata

export-time model:
  DecodedSample = PCM bytes ready to write
```

---

## 16. Export architecture

Export is collection-centered. A caller asks to export a `Collection`, not a raw sequence or a raw sample collection.

`ExportRequest` chooses:

- output kinds: MIDI, SF2, DLS, WAV;
- loop policy;
- number of sequence loop repeats;
- MIDI options;
- modulation scaling policy.

If no output kind is specified, the default is MIDI.

The user-facing sequence-loop count defaults to one extra repeat after the initial playthrough, matching the application setting. Low-level `SequenceVmOptions` still defaults to zero so tests and internal callers can explicitly render only the initial pass.

`modulationScaling` defaults to `FullFormatRange`, matching source-driver semantics. `ObservedSequenceRange` is an explicit export optimization and applies consistently to MIDI controllers and SF2/DLS modulators.

### 16.1 Export preparation

Export first resolves one collection into typed pointers:

- sequence program;
- instrument sets;
- sample collections;
- diagnostics for missing or wrong-type references.

Export resolves these references privately through the snapshot's typed asset lookup.
SF2 and DLS then consume the same private synth-export input and result values;
only their container writers differ.

### 16.2 MIDI export

MIDI export needs a sequence and a registered dialect. It renders like this:

```text
SequenceProgram
  ↓ SequenceVm
PerformanceSequence
  ↓ PerformanceMidiRenderer
MidiSequence
  ↓ MidiExporter
.mid bytes
```

If rendering fails, export returns an artifact with diagnostics rather than crashing or losing the rest of the export request.

### 16.3 WAV export

WAV export walks the collection’s sample collections and decodes each sample from `SourceStore`. It produces one WAV artifact per sample.

### 16.4 SF2 and DLS export

SF2 and DLS export consume instrument sets and sample collections. They may also use sequence modulation analysis when the request asks for observed-range modulation scaling. That lets synth modulators and MIDI controller values remain aligned.

### 16.5 Partial failure model

`Artifact` carries diagnostics even when no bytes were produced. This is a good fit for game audio data, where one collection may be partially valid. A user can still get a MIDI file even if the sound bank has a problem, or get sample WAVs even if the sequence cannot be rendered.

---

## 17. Validation architecture

Validation is split by boundary and model:

- scan validation checks a normalized `ScanResult` before it enters `SessionState`;
- sequence validation checks `SequenceProgram` structure;
- synth validation checks instrument and sample model structure;
- collection reconciliation checks collection references against admitted assets.

`ValidationReport` only composes ordinary `Diagnostic` values. Admission
failures are transferred directly into session diagnostics, preserving their
codes and source ranges; exceptions are reserved for unexpected failures rather
than validation control flow.

Snapshots do not run a second whole-session audit. They are read-only copies of
state that has already crossed those gates. This keeps invariant ownership at
the mutation boundary and prevents test-only invalid-snapshot construction from
becoming production API.

---

## 18. Format authoring model

One of your stated goals is that format code should be intuitive and simple. The new architecture mostly supports that goal by giving format authors three clear jobs:

1. scan source structure;
2. decode sequence commands in driver terms;
3. build neutral instrument/sample values.

The format author should not need to own global state, update UI objects, implement collection lifecycle, write MIDI directly, or manage long-lived parser objects.

### 18.1 A simple format scanner

For a format where everything is known in one source, the scanner pattern is:

```text
find layout
reserve needed asset IDs
parse sequence
parse instrument set
parse samples
create explicit collection
return result
```

Capcom SNES follows this shape.

### 18.2 A container format scanner

For a container format where layout gives dependency relationships, the scanner pattern is:

```text
parse container layout
parse dependencies first
parse banks/instrument sets
parse sequences
create one explicit collection per sequence
return result
```

Nintendo DS SDAT follows this shape.

### 18.3 A loosely matched format scanner

For a format where sources may arrive separately or relationships are incomplete, the scanner pattern is:

```text
scan sequences and sample collections independently
emit facts about each asset
resolver groups assets later
optional export preparation builds collection-dependent values
```

Akao follows this shape.

### 18.4 Sequence command readers

Command code should resemble a driver interpreter. Every ported sequence format uses the compiler-cursor form: one opcode switch reads source fields and appends the command's effects in order. Returning the event compiles discovery and playback behavior together, replacing separate metadata, operand, flow, and execution switches.

The compiler-cursor form is the target because it keeps ordinary commands near the original interpreter's size while retaining source-free VM execution. It avoids a command class, visitor, command table, and universal operation taxonomy.

`CompilerCursor` owns checked field reads, source metadata, truncation, ordered generated actions, and the small set of literal VM/output operations. Formats own the opcode switch, conversions, persistent `TrackState`, and concrete `Playback` methods. Chained and separate calls are two layouts of the same imperative authoring paradigm.

Compiled dialects may also provide typed track initialization, a typed per-tick `Playback::tick()`, and a typed whole-sequence prepass state. The shared VM performs that prepass by executing the already-compiled commands, so formats can collect history-dependent ranges without reopening bytes or maintaining a second control-flow interpreter. Konami SNES uses this for vibrato controller ceilings.

### 18.5 Where complexity still appears

The architecture removes a lot of accidental complexity from format code, but it does not remove real format complexity. The hard parts still appear in the right places:

- NDS has malformed SDAT recovery and multiple track-address rules.
- Capcom SNES has driver-specific pitch, pan, volume, slur, portamento, and repeat behavior.
- Akao has version-specific bytecode, articulation tables, sample-set matching, and collection-dependent instrument construction.

The difference is that those complexities now sit in format-specific helpers rather than leaking into session storage, UI object ownership, or MIDI export.

---

## 19. Representative ported formats

### 19.1 Nintendo DS SDAT

The NDS module shows the “container with known relationships” path.

The scanner finds SDAT offsets, parses layout, annotates the SDAT header and sections, then scans in dependency order:

1. universal PSG samples;
2. referenced SWAR wave archives;
3. referenced banks;
4. sequences;
5. explicit collections for each sequence.

The collection key includes source ID, SDAT offset, and sequence index. That makes each collection stable and unique within a source.

NDS sequence code uses one compiler-cursor opcode switch. Each block reads a source command once and appends direct output actions, typed track-state operations, local `Playback` behavior, or VM control flow. Pitch-bend range, for example, visibly chains a track-state assignment and a range event rather than hiding both behind one method. The cursor records source operands for annotation while separately compiling ordered positional actions. Playback receives no field names or source bytes. Normal SSEQ decoding follows reachable blocks, so subroutines and jump targets are discovered without a playback prepass.

`Allocate Track`, the optional bootstrap rest, and `Open Track` form the SSEQ track-directory prefix rather than a musical track. A small bounded bootstrap reader consumes that prefix once and returns the primary and secondary track starts; ordinary command decoding begins afterward. One `TrackDecodeScope` owns the reader, discovery limit, asset ownership, and source-map parent for every track. The surrounding NDS context keeps only SSEQ address semantics, recovery policy, and diagnostics, so decoding an individual track requires its index and start address. The public header exposes only the dialect and whole-sequence parser, not track-walker machinery. Malformed SDAT recovery deliberately remains a specialized walker because malformed files can contain overlapping call-target bytes; it uses the scope's `begin`/`append`/`finish` lifecycle while preserving its specialized traversal and canonical command decoder.

Container-specific range recovery belongs to `NdsLayout`, beside FAT and section parsing, rather than in the sequence interpreter. `NdsModule` scans the discovered layout in dependency order and uses indexed optional bank/sample handles instead of parallel lookup maps. SWAR, SBNK, and PSG parsing remain together in `NdsSynth`: their compact shared record layouts are expressed with small helpers and a single instrument-type switch rather than a hierarchy of event or instrument classes. These synth entry points commit their models through `ScanResultBuilder`, so format code does not construct asset metadata or maintain a separate optional diagnostics path. Fixed SWAV records use `RecordReader` for one source-aware field pass, while archive and bank cursors keep every relative pointer bounded to its FAT entry.

NDS exposes one `FormatDefinition` containing both its scanner and compiler-cursor dialect. Recognition occurs inside `scan`, so SDAT signature discovery runs once instead of being duplicated by `canScan`.

### 19.2 Capcom SNES

The Capcom SNES module shows the “single source, explicit collection” path.

The scanner finds the layout, reserves sequence/instrument/sample IDs, parses instrument/sample information when the instrument table and SPC DIR are detected, always emits the sequence, and creates one collection for the source. It owns asset metadata while `decodeCapcomSnesSequence` returns only the neutral `SequenceProgram`. If synth information is unavailable, the collection still has the sequence and a warning.

Capcom uses one imperative compiler-cursor opcode switch. Each case reads its operands, performs version-dependent conversion, records control flow, and appends its executable effects in source order. Simple state and output commands remain entirely inline; only notes, note attributes, repeat breaks, portamento, and modulation gating call nearby `Playback` methods because they depend on execution history. There is no command-definition table, decode lambda, execute lambda, playback-time operand lookup, or second opcode selection. Complete commands retain no source bytes, and engine-version decisions are resolved during decode rather than reopened during playback.

`TrackDecodeScope::linear` owns walking, command projection, and track annotation finalization. The whole-sequence parser creates one scope and changes only the source-map parent for each track pointer; its production path does not rebuild the transitional per-track input bag. Encoded/resolved cursor fields preserve both the driver byte and its interpreted value in one semantic operand and source annotation.

The track state owns duration rate, transpose, octave flags, slur state, modulation, portamento, and previous-note information. Loop and repeat commands return VM flow helpers rather than implementing export loop policy. Driver math is local to the value implementation and contains no dependency on the old parser architecture. Pan commands retain Capcom's source-engine left/right gains; shared export code performs MIDI pan quantization and expression compensation. Vibrato events retain both normalized controller amounts and the driver's physical semitone depth and hertz rate, allowing `SequenceEventSimulation` to render pitch-bend motion without depending on SF2/DLS modulators.

The layout uses the shared masked-pattern matcher. The synth path parses the instrument table once, supplies its referenced SRCNs to the shared BRR catalog, and directly commits the instrument and sample assets; it has no intermediate format-specific sample records, sample-construction pass, or SRCN lookup maps. Capcom exposes one public header instead of separate module, layout, sequence, synth, and types headers. Its `scan` function performs recognition and layout discovery once; it does not register a duplicate `canScan` probe. One `FormatDefinition` registers both scanner and dialect. Sequence performance and synth instruments use `capcom-snes.instrument` identities; MIDI/SF2/DLS addressing is assigned in export code.

### 19.3 Akao

Akao is the most important architecture stress test in this branch.

The scanner does not try to bind everything immediately. It scans sample collections and sequences separately, then emits facts:

- sample-set IDs when present;
- sequence IDs;
- source offsets;
- articulation coverage from sample collections;
- required articulation IDs from sequences.

The resolver creates one collection per sequence. It chooses sample collections by preferred sample-set ID and by required articulation coverage. It handles PSF-like sources more narrowly, treats missing and zero sample-set IDs as the same anonymous set, sorts attached sample collections by articulation range, and marks collections incomplete when coverage is missing.

The export preparer currently re-reads the selected sequence and sample collections, builds an articulation map, and creates a bound instrument set. Durable symbolic articulation/sample bindings would remove that remaining parse-once violation.

---

## 20. How to add a new format

A new format usually follows this path:

### Step 1: Register a format definition

Create a `FormatDefinition` containing a module with:

- a name;
- `scan`;
- optional resolver;
- optional collection preparer.

Add any source-free dialects the format needs, then pass the definition to `Session::registerFormat`. Do not add separate module and dialect registration entry points.

Put recognition at the start of `scan` and return an empty `ScanResult` for non-matches. Do not add `canScan`; it exists only for unmigrated modules.

### Step 2: Decide how collections are discovered

Use explicit collections when the scanner already knows the grouping.

Use match facts and a resolver when relationships may be discovered across multiple files or depend on incomplete evidence.

Prefer durable symbolic references and transient export preparation when an asset depends on collection membership. Prepared values should not be inserted into `SessionState`.

### Step 3: Parse source structure

Use `ByteReader` for random access and `RecordReader` for sequential records. `RecordReader` performs bounds checks, returns ranged values, records fields, and reports truncation. Use focused shared platform readers such as `SnesSampleDirectory` instead of copying stream walkers into a format.

### Step 4: Build assets

Use `ScanResultBuilder` to allocate IDs and attach metadata. A source decoder should return the narrow neutral value it owns—for example, Capcom's sequence decoder returns `SequenceProgram` while the scanner registers it as a sequence asset.

### Step 5: Compile sequence commands

Define:

- one imperative compiler-cursor opcode switch, with small version-specific dispatch helpers only when necessary;
- one complete source block per command;
- operand names, display rules, and ordinary roles directly on field reads (flow operations label their own targets);
- a per-program profile in `SequenceProgram::config`;
- a program-state type only when the driver has real shared state;
- a `TrackState` type for driver-local mutable playback state; and
- a sequence-scoped `TrackDecodeScope` using `linear` or `reachable` discovery.

Use `CompilerCursor` to keep IR construction, source projection, generated executor registration, truncation handling, and type erasure out of format code. Ordinary literal commands use `emit...`, `set`, `add`, `toggle`, or a VM flow operation. VM flow operations own their playback annotation and address-target role. Chain operations—or write them as separate statements—when a source command has several effects. Use `state()` and `select()` when a simple output value depends on track state. Put substantial or reused stateful driver behavior in a nearby typed `Playback` method. A captureless inline `invoke` is available for genuinely clearer one-off behavior. Playback must not implement global loop export policy or read source bytes.

### Step 6: Build synth data in the neutral model

Build instruments, regions, samples, envelopes, loops, tuning, and physical modulation descriptions as ordinary values. Add them through `SampleCollectionBuilder` and `InstrumentSetBuilder` so dense references, source owners, range accumulation, diagnostics, and sample links remain shared machinery. Keep encoded sample bytes in `SourceStore` by storing `SourceRange`s. Use raw generators and modulators only when the source behavior cannot be expressed by the standard vibrato/tremolo model.

### Step 7: Add tests around values

The new model is very testable. Good tests can inspect:

- assets and metadata;
- collections and statuses;
- source map annotations;
- command fields;
- VM performance events;
- MIDI events after rendering;
- exported artifacts and diagnostics.

Capcom SNES also keeps an exact decoded-command and neutral-performance golden for its representative fixture. Real-corpus snapshots should use the same boundary when a redistributable or local corpus is available.

The parity executable keeps format policy in small `ParitySuite` descriptors and shares collection-map runners for summaries, MIDI, SF2, and DLS. A play-once MIDI comparison uses the earlier End-of-Track tick for each corresponding track as an exclusive audible horizon, clipping notes that cross it. It does not guess that selected unmatched controller kinds are harmless tail setup.

---

## 21. Component-by-component breakdown

### 21.1 `CoreTypes`

`CoreTypes` provides IDs, source ranges, ranged values, object references, severity, and diagnostics.

The important design point is that these types are small and copyable. They are the shared language between scanners, session state, snapshots, source maps, validators, and exporters.

### 21.2 `SourceStore`

`SourceStore` is the only owner of source bytes. It supports user-loaded sources, derived sources, source family removal, and byte readers.

It is the base of source-backed diagnostics and source maps.

### 21.3 `SessionSnapshot`

`SessionSnapshot` is the read-only product of the session. It stores copies of current sources, assets, facts, collections, source map, and diagnostics. It builds indexes for lookup by asset ID and collection ID.

This is the object most non-mutating code should use.

### 21.4 `SourceMap`

`SourceMap` stores source annotations and builds indexes by annotation ID, source ID, and parent annotation. It supports queries for annotations that intersect, contain, or appear at a byte offset, as well as owner and link queries.

It is the main mechanism for HexView-style explanation of parsed bytes.

### 21.5 `FormatRegistry`

`FormatRegistry` stores registered format modules. The session offers every source to modules in insertion order. That includes derived sources.

Insertion order matters because extractors are registered before normal formats in `ValueFormats.cpp`.

Formats enter through `FormatDefinition` and `Session::registerFormat`, which registers one module and all of its source-free dialects together.

### 21.6 `ScanResultBuilder`

`ScanResultBuilder` is the intended scanner authoring API. It creates assets, explicit collections, facts, diagnostics, source annotations, and extracted sources. It tracks reserved handles and validates that referenced handles were committed. Parsers can append directly to its diagnostic collection instead of creating a temporary vector and copying diagnostics back into the result.

This is a major readability win for format code.

### 21.7 `SessionState`

`SessionState` is the private consistency boundary for accepted scanner output. It owns flat vectors of discovered values, the small indexes required for identity and lookup, collection reconciliation, and coordinated source/asset removal.

It is deliberately not part of the public session API. `Session` handles source bytes, registries, validation, and orchestration; `SessionState` handles durable discovered values as one atomic conceptual state.

### 21.8 `MatchContext` and resolver helpers

`MatchContext` gives collection resolvers a borrowed read-only view of sources, assets, and facts without materializing a `SessionSnapshot`. Resolver helpers make those facts readable by format-specific collection resolvers.

`MatchFactIndex` is especially useful because it combines fact filtering, payload typing, asset lookup, and optional source lookup.

### 21.9 `SequenceProgram`

`SequenceProgram` records decoded source commands, program profile/configuration, and playback defaults. It is the stable parsed form of a sequence.

Each compiler-cursor command stores its opcode, named source operands, source provenance, decode flow, and ordered executable actions. Every action has an automatically assigned executor slot and positional arguments. The `AddressIndex` lets the VM resolve runtime control flow by source addresses instead of assuming vector order is execution order.

### 21.10 `CompilerCursor` and `RecordReader`

`RecordReader` is the small imperative checked reader for one source record. It owns sequential cursor bounds, exact field ranges, captured field metadata, and truncation diagnostics without introducing a schema language. `CompilerCursor` adds command presentation, source operands, discovery flow, ordered executable actions, and generated typed executor selection. Its flow methods supply the corresponding control-flow presentation and target metadata. `CommandSourceMap` performs automatic projection.

`SequenceDecodeSession` owns the repeated sequence-header, track-pointer, track, and command-annotation lifecycle for ordinary linear formats. A format still reads its pointer table explicitly and supplies the track number, pointer range, and decoded start address, keeping ordering, endianness, relocation, and null rules visible. `TrackDecodeScope` owns the corresponding single-track walk and command projection.

### 21.11 `SequenceDialect`

A source-free dialect connects a `SequenceProgram` to executable driver behavior. It packages program/track state factories, a byte-free command executor, timebase, and default behavior. `CompiledCommandDialect` is the compiler-cursor adapter: it is the only layer that sees `std::any`, creates the concrete `Playback`, dispatches every action in order, and combines their `Effects` for the VM. Formats normally apply that adapter with `makeCompiledDialect`, rather than naming and assigning its two generic hooks themselves.

The implementation uses `std::any` only to erase the concrete program and track runtime-state types. Program-specific version data is not stored in dialect context; it lives on the immutable program.

### 21.12 `SequenceVm`

`SequenceVm` is the shared interpreter for parsed sequences. Source-free dialects run through a global `(tick, stable track order)` scheduler with one program state and per-track runtimes. The VM handles command limits, loops, calls, returns, repeats, diagnostics, and event emission. Recurrence keys include command, call stack, and repeat-counter state; the command limit is only a contextual emergency guard.

### 21.13 `PerformanceModel`

`PerformanceModel` is the neutral musical output from the VM. Its events keep source command and source annotation IDs, so output can be traced back to source bytes. `StereoBalancePerformanceEvent` stores source-engine left/right gains without MIDI quantization.

This model is the main bridge to future non-MIDI sequence outputs.

### 21.14 `SynthModel`

`SynthModel` contains the durable neutral values. `SampleCollectionBuilder` and `InstrumentSetBuilder` are temporary authoring helpers around those values; they do not add a draft model or a combined synth lifecycle. Exceptional collection-specific values may be prepared transiently for export, but never enter `SessionState`.

`SynthModel` is the neutral instrument/sample layer. It represents instruments, regions, samples, envelopes, loops, tuning, physical modulation, and decoded PCM. Export preparation lowers standard modulation to raw generator/modulator records; the model retains custom records only as an escape hatch. `resolveInstrumentAddress` provides the one export-address policy shared by MIDI, SF2, and DLS.

It is designed for SF2/DLS/WAV export without making those formats leak into scanners.

### 21.15 `Export`

Export resolves a collection, renders the sequence only when needed, decodes samples only when needed, and produces artifacts with diagnostics.

The export layer is careful to share work. For example, if both MIDI and observed-range modulation are requested, the sequence render result can be reused.

### 21.16 `Validation`

Validation checks values at the boundaries. The most important boundary is scan admission. A scanner can be generous and return diagnostics, but invalid durable references should be rejected before entering `SessionState`.

---

## 22. Design strengths

### 22.1 Clear ownership

The architecture makes ownership easy to state:

- `SourceStore` owns bytes.
- `SessionState` owns durable discovered values.
- snapshots own read-only copies.
- exporters own temporary output data.
- format scanners own only temporary parse state.

That clarity is a major improvement over a graph of long-lived parser objects.

### 22.2 Better testability

Values are easy to inspect. Tests can assert on assets, collections, source annotations, command fields, performance events, and export artifacts without needing UI state.

The existing tests under `tests/core` and `tests/formats` reflect this direction.

### 22.3 Format code can be local and readable

One imperative compiler-cursor switch is a strong fit for sequence drivers. Keeping each command's reads and effects in the same case makes the driver visible without coupling playback to source encoding or scattering one command across decode and execute definitions.

Return presentation metadata with each decoded command and let `CommandSourceMap` project annotations. Command decoding should not call annotation-builder methods.

The scan builder also makes simple scanners short and clear.

### 22.4 Matching is more explicit

The old matcher style was event-driven and mutable. The new style is fact-based and snapshot-based. This makes matching rules easier to test and reason about.

Akao benefits the most from this because it has real matching logic rather than simple same-file grouping.

### 22.5 Source traceability is built in

Source ranges and source annotations appear throughout the model. That means diagnostics, UI, and export can point back to bytes without rediscovering them.

### 22.6 Export is no longer MIDI-first internally

The VM outputs neutral performance events. MIDI is one renderer, not the sequence model itself. This is important for future conversion into new formats.

---

## 23. Tradeoffs and risks

### 23.1 Versioned dialect families remain

Akao and Konami SNES still register several dialect values because some execution policy differs by driver version. Their immutable programs also carry a version profile, so future consolidation is possible if it removes more code than it adds.

### 23.2 `std::any` hides some type checking

`SequenceDialect` type-erases program and track runtime states through `std::any`. A mismatch can become a runtime issue rather than a compile-time issue.

Keep casts in the one state factory/executor family for a format, cover them with focused tests, and never use `std::any` as a program-configuration property bag.

### 23.3 Source map conventions need discipline

The source map is flexible. That is useful, but flexibility can lead to inconsistent annotation names, roles, fields, and detail kinds across formats.

A small style guide for source annotations would help future formats feel consistent in HexView and tests.

### 23.4 Akao preparation still rereads source data

The second asset lifecycle is gone, but Akao export preparation still re-reads selected source structures. Durable symbolic bindings would make this step pure over snapshot values and complete the parse-once goal.

### 23.5 Collection resolution depends on good keys

Stable collection keys are central. Poorly chosen keys can cause duplicates, unexpected updates, or collections that fail to merge when more sources arrive.

New format modules should treat collection-key design as part of the format port, not as an afterthought.

---

## 24. Recommendations

### 24.1 Write a short format-authoring guide

The branch would benefit from a contributor-facing guide with one page each for:

- simple scanner with explicit collection;
- scanner with match facts and resolver;
- compiler-cursor sequence decoder with one opcode switch;
- synth parser using `Instrument`, `Region`, and `Sample`;
- source map annotation conventions.

### 24.2 Keep command compilation boring

Read each operand once in a short opcode block and finish with the command's effect. Literal output, state, and VM flow belong directly in that block. Keep simple state-derived values local with `state()` and `select()`. Use a captureless inline handler for complex one-off behavior and a nearby concrete `Playback` method for substantial or reused runtime-history behavior.

Keep opcode knowledge in one switch plus small version dispatch helpers. Avoid command-kind and operand-ID enums, parallel size/metadata tables, analysis interpreters, declarative command tables, and per-opcode handler classes.

### 24.3 Prefer explicit collections when possible

If the source format already tells you what belongs together, emit an explicit collection. Do not force every format through match facts just for consistency.

Use match facts when there is genuine uncertainty or cross-source matching.

### 24.4 Keep export policy out of scanners

Scanners should not decide MIDI channel behavior, loop export behavior, or SF2/DLS details. They should emit neutral sequence and synth values. Export options and shared renderers should handle target-specific policy.

### 24.5 Make source annotations consistent

A future small convention document should define common detail-kind prefixes, field names, and roles for headers, tables, pointers, commands, operands, instruments, regions, and samples.

This will help UI and tests stay stable as more formats are ported.

### 24.6 Finish Akao symbolic binding

Add durable symbolic sample bindings so export preparation no longer re-reads sequence and sample structures. Collection rebuilding is already independent of `SourceStore`.

### 24.7 Migration roadmap and invariants

The remaining order is intentional:

1. add symbolic Akao articulation/sample binding and remove preparation source reads;
2. migrate remaining layouts and synth parsers to `RecordReader` and shared platform primitives;
3. remove the remaining `canScan` recognition adapters;
4. standardize source annotation naming and roles.

Architectural acceptance criteria:

- every source structure is parsed once;
- compiler-cursor VM execution has no byte access;
- collection rebuilding has no source access;
- each driver has one primary opcode block per command and no separate execution mapping;
- driver profile data lives on `SequenceProgram`;
- format code emits transitions rather than per-tick fade events;
- value-format directories contain no MIDI, SF2, or DLS policy;
- value formats include no old-architecture headers;
- real-corpus decoded-command and performance parity supplements the self-test.

---

## 25. Mental model recap

The new architecture is easiest to understand as a set of stages:

```text
1. SourceStore owns bytes.
2. Format modules scan bytes into plain assets, facts, annotations, diagnostics, and extracted sources.
3. The session normalizes and validates each ScanResult before durable mutation.
4. SessionState owns accepted values and their cross-value consistency.
5. Collection resolution groups assets into export units.
6. Export preparation builds any transient collection-specific values.
7. SessionSnapshot exposes a read-only view.
8. SequenceVm globally schedules source-free compiled commands into neutral performance events.
9. Exporters turn collections into files.
```

The core idea is not just “use modern C++.” The deeper idea is to make parsed game audio data durable, inspectable, and independent of the parser that found it. That gives VGMTrans a better base for testing, UI explanation, multi-source matching, and future export formats.
