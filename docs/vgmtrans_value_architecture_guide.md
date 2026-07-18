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
  stores own those records
  snapshots expose read-only copies
  shared engines render/export those records
```

A format scanner no longer needs to create a web of live objects that know about each other. It emits values: a sequence, an instrument set, a sample collection, source annotations, match facts, diagnostics, and extracted child sources. The session commits those values, then later decides which values belong together.

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
ScanCommit
  validates the result before durable storage
  ↓
Session stores
  assets, match facts, explicit collections, source map, diagnostics
  ↓
Collection resolution
  groups assets into exportable collections
  may materialize collection-dependent assets
  ↓
SessionSnapshot
  read-only copy of the current session state
  ↓
Export
  collection → MIDI, SF2, DLS, WAV, or future formats
```

There are two important sub-flows inside that larger flow.

### Sequence flow

```text
source bytecode
  ↓
format decoder reads each command once
  ↓
SequenceProgram with named operands and stored flow
  + SourceMap command annotations
  ↓
global SequenceVm scheduler
  ↓
PerformanceSequence, a target-neutral musical performance
  ↓
MIDI renderer or another future sequence exporter
```

The sequence parser does not directly write MIDI. It records semantic commands in a `SequenceProgram`; semantic executors never receive command bytes. The shared VM schedules those commands and produces musical events. MIDI conversion is another layer after that.

> **Migration status:** Capcom SNES uses this semantic path. Akao, Akao SNES, Konami SNES, and NDS still use the legacy two-phase cursor adapter while they are migrated. New format code should use the semantic path; the cursor API is compatibility infrastructure, not the target architecture.

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
4. **Mutable state is contained.** `Session` and its stores mutate. `SessionSnapshot` is read-only.
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
| `session` | Mutable stores and orchestration: add sources, scan, commit, resolve collections, export. |
| `sequence` | Parsed sequence programs, command cursor, bytecode walkers, sequence dialects, VM, performance events. |
| `synth` | Target-neutral instrument, region, sample, envelope, loop, codec, and sample decoding model. |
| `export` | Collection export to MIDI, SF2, DLS, and WAV. |
| `validation` | Checks that scanner output and snapshots are internally consistent. |
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

The intended pattern is simple: scanner code uses `has()` or `ParseCursor` when malformed data should be handled gracefully, and direct reads when the caller has already checked the range.

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

`ObjectRef` is a slightly richer reference used by source annotations and diagnostics. It can point at a whole asset, a sequence, a sequence track, an instrument, a sample, or a miscellaneous object.

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
- named operands with resolved values and source ranges;
- decode-time control flow;
- source address;
- encoded size;
- source range;
- optional source annotation ID.

An operand's name is both its author-facing vocabulary and its durable identity. It also keeps a display rule, optional generic role, exact source range, and—when conversion is needed—both the encoded and resolved value. Execution consumes the resolved value. This prevents driver conversion formulas from being duplicated between source annotation and playback without forcing formats to declare parallel numeric operand IDs.

Semantic commands do not retain encoded bytes. `commandBytes` and byte spans remain temporarily on `TrackProgram` only for unmigrated cursor dialects. The semantic executor signature intentionally receives no `TrackProgram` or byte reader, making source reparsing during playback impossible.

### 8.2 `InstrumentSetAsset`

An instrument set contains `Instrument` records. Each instrument has:

- a source-domain `InstrumentIdentity`;
- name;
- source range;
- regions;
- default reverb;
- synth generators and modulators.

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

Semantic sequence decoders do not call annotation-builder methods. They return command presentation data and self-describing operands; `CommandSourceMap` projects the command label, opcode, fields, derived values, control-flow links, and instrument links generically. Legacy command readers still do this indirectly through `VmCommandCursor` until migrated.

### 9.2 Why this matters

The source map is one of the strongest parts of the new architecture. It makes debugging, HexView integration, tests, and diagnostics all point to the same parsed snapshot. A command that fails to export can still point back to the exact source bytes that produced it.

---

## 10. Scanning

Scanning is the process of turning one source into values.

A format definition owns the scanner and its optional semantic executor family:

```text
FormatDefinition
  module
    name
    scan(input)
    optional collection resolver
    optional materializer
  optional sequence dialect
```

`Session::registerFormat` registers both halves together. Capcom SNES uses this unified path. Direct module/dialect registration remains only for unmigrated formats.

Recognition belongs at the start of `scan`, which returns an empty result when the source does not match. This ensures layout/signature discovery runs once. `canScan` remains nullable as a migration adapter for older modules and should not be added to new ones.

### 10.1 `ScanInput`

A scan receives:

- a `SourceFile` value;
- a `ByteReader` over the source bytes;
- the session ID allocator.

The scanner does not receive mutable session stores. It cannot directly append assets to the session. It can only return a result.

### 10.2 `ScanResult`

A scan result can contain:

- assets;
- match facts;
- explicit collections;
- source map annotations;
- diagnostics;
- extracted sources.

This is a useful boundary: scanners can produce partial results and warnings, but the session decides whether the result is valid enough to commit.

### 10.3 `ScanResultBuilder`

`ScanResultBuilder` is the main convenience API for format authors. It hides repetitive metadata setup, ID allocation, simple collection creation, match facts, diagnostics, source map access, and handle validation.

A simple scanner can read naturally:

```cpp
ScanResultBuilder result(input, "CapcomSnes");
const auto sequence = result.reserveSequence();
const SourceRange sequenceRange = /* format header range */;

result.sequence(sequence, displayName, sequenceRange)
      .program(decodeCapcomSnesSequence(
          input.reader, layout, sequence.id, sequenceRange,
          &result.sourceMap(), &result.diagnostics()));

result.collection(displayName, capcomCollectionKey(input.source.id))
      .sequence(sequence);

return result.finish();
```

The builder has typed handles such as `ScanSequenceRef`, `ScanInstrumentSetRef`, and `ScanSampleCollectionRef`. A scanner can reserve handles before parsing. This is important when assets need to refer to each other before all of them have been built.

For example, an instrument set may need to refer to a sample collection that has not been committed yet. The scanner can reserve a sample collection ID, use it in regions, and then commit the sample collection later in the same scan result.

The builder tracks whether reserved handles were actually committed. That catches a common class of scanner bugs before the result is accepted.

### 10.4 `ParseCursor`

`ParseCursor` is a checked parser helper for non-sequence data. It records diagnostics for malformed reads and returns ranged values. Those ranged values can be passed directly into source map fields.

It fits the same philosophy as `VmCommandCursor`, but for structured headers and tables rather than sequence bytecode.

---

## 11. Commit and validation

A scanner returns a `ScanResult`, but the session does not blindly trust it. It turns the result into a `ScanCommit`, normalizes IDs, validates it, and only then appends it to stores.

The validation layer checks things such as:

- asset IDs are present and unique;
- asset IDs do not reuse existing IDs;
- source ranges point at active source bytes and are in bounds;
- source map annotations have valid ranges;
- annotation parent and link IDs are valid;
- diagnostics do not point at missing annotations;
- match facts point at known assets and sources;
- extracted source origins are valid;
- sequence, instrument, and sample data pass their own model checks.

This makes `ScanCommit` the admission gate between “a format scanner tried to parse something” and “the session now owns this data.”

That boundary is healthy. It means individual format modules can stay focused on parsing rather than duplicating session bookkeeping rules.

---

## 12. Session and stores

`Session` is the mutable workspace. It owns:

- `SourceStore`
- `AssetStore`
- `MatchFactStore`
- `ExplicitCollectionStore`
- `SourceMapStore`
- `CollectionStore`
- `DiagnosticStore`
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

### 12.1 Registries are sealed before use

The session seals the format and dialect registries before adding, scanning, removing, or exporting. This prevents the meaning of already-scanned assets from changing halfway through a session because a new format or dialect was added late.

### 12.2 Scanning a source scans its derived sources

When `Session::scanSource` scans a source, it also scans any derived sources created by extractors during that scan. This happens through a queue.

This design lets extractors and normal format modules compose without special cases. A PSF extractor can produce executable bytes. Then the Akao scanner can scan those bytes as a normal source.

### 12.3 Stores have narrow jobs

Each store owns one kind of session data:

| Store | Job |
|---|---|
| `AssetStore` | Owns assets, indexes by ID, tracks source ownership, keeps stable IDs for materialized assets. |
| `MatchFactStore` | Stores match facts and removes them when related sources/assets are removed. |
| `ExplicitCollectionStore` | Stores scanner-known collections and turns them into desired collections. |
| `SourceMapStore` | Stores raw annotations and publishes a combined `SourceMap`. |
| `DiagnosticStore` | Stores diagnostics and removes source-owned diagnostics. |
| `CollectionStore` | Reconciles desired collections by stable key and marks stale collections when assets disappear. |

This is intentionally more structured than a single large root object. It makes ownership and removal rules easier to reason about.

### 12.4 Snapshot creation

`Session::snapshot()` gathers the current store contents into a `SessionSnapshot`. The snapshot is copyable and read-only. It contains:

- sources;
- assets;
- match facts;
- collections;
- source map;
- diagnostics.

It also builds indexes for fast asset and collection lookup by ID.

UI, tests, and export should read snapshots rather than mutable stores. That keeps most consumers from accidentally changing session state.

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

A resolver reads the current snapshot and returns the collections that should exist now. Resolvers are pure functions over the snapshot and source store. They do not receive mutable stores.

The helper `MatchFactIndex` makes resolver code easier by giving typed access to facts and assets. `CollectionAssembly` helps build collections while handling duplicate suppression and common missing-role issues.

### 13.4 Stable collection keys

Every resolved collection has a `CollectionKey`:

```text
resolver id + resolver-specific value
```

The key is the durable identity of a collection. When more sources are loaded, the resolver may return a collection with the same key but more complete membership. `CollectionStore` uses the key to update the existing collection rather than creating a duplicate.

### 13.5 Legacy materialization

The current Akao implementation still materializes assets after collection resolution. This is migration debt, not the target architecture.

Akao sequences and sample collections are scanned separately. Only after the resolver chooses the correct sample collections can the code build a bound instrument set for that specific collection. That happens in `FormatModule::materializeCollection`.

The legacy materializer receives:

- the source store;
- the current snapshot;
- the resolver’s desired collection;
- the ID allocator;
- a callback that returns a stable asset ID for a named materialization slot.

The materializer returns:

- the final desired collection;
- materialized assets;
- diagnostics.

The stable slot mechanism preserves asset IDs during this transition. The replacement is symbolic sample binding stored in durable instrument/sample values, followed by a pure collection binder. The intended invariant is: **collection rebuilding never accesses `SourceStore` and never reparses sequence, articulation, instrument, or sample structures.**

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

`SequenceProgram` is an immutable parsed source program, not a live player. It owns driver profile/configuration and tracks of `SourceCommand` records. Every semantic command has an opcode, named operands, stored decode flow, its source address/range, and an annotation ID.

The stored flow lets walkers and validators inspect control flow without executing format code. Typed operands let execution and analysis use parsed meaning without reopening the source. A format profile belongs to this program value, so one registered executor family can handle every version of the driver.

### 14.2 `SequenceDialect`

A semantic dialect is the small piece of driver-specific behavior needed by the VM. It contains:

- an ID;
- a timebase;
- default behavior;
- an optional program-state factory for genuinely shared driver state;
- a function to create per-track state;
- a function to execute one semantic command.

The semantic executor receives the command, program state, track state, `PerformanceEmitter`, and `VmApi`. It does **not** receive a `TrackProgram`, source bytes, or global registry context. Version data is read once from `SequenceProgram::config` when state is constructed.

The dialect is registered once in `SequenceDialectRegistry`; export looks it up by family ID. Capcom SNES, for example, registers `capcom-snes` once and stores V1/V2/V3 selection on each program.

### 14.3 Semantic decode and execution

The target authoring model is one opcode profile whose entries contain both lifecycle operations. A command's label, broad semantic, source reads, conversion, discovery flow, and playback behavior are adjacent:

```cpp
profile[0x05] = command(
    "Tempo", SequenceSemantic::Tempo,
    [](Decode& d) {
      d.resolved("microseconds_per_quarter", d.rawU16be("raw"), convertTempo);
    },
    [](Args a, Playback& p) {
      p.out.tempo(a.u32());
      return Effects{};
    });
```

`SemanticCommandDecoder` wraps `RecordReader` for checked reads, exact ranges, raw/resolved pairing, and discovery-flow bookkeeping. `SemanticCommandArgs` reads the nearby named operands during playback. Names intentionally serve as identity: a linear lookup across a handful of operands is preferable to a command-kind enum, an operand-ID enum, metadata switches, and parallel decode/execution switches.

The two lifecycle entry points merely select the same profile entry by program profile and opcode. Decode invokes its `decode` function once; playback invokes its `execute` function on every runtime visit. `CommandSourceMap` projects the decoded value into an annotation, so command definitions never call `.field()`, `.derived()`, or `.link()`.

Do not introduce a command class hierarchy, handler-per-opcode files, a binary-schema DSL, typed command variants, or a generic microcode language. The point is to keep the complete source-driver operation visible in one local block while removing repeated parsing and representational ceremony.

`VmCommandCursor` and `SequenceCursorDialect` still support unmigrated formats. They retain command bytes and invoke a reader in decode and render phases. This path is deprecated because execution can reparse bytes and decode-time analysis must masquerade as playback.

### 14.4 Bytecode walkers

The code provides linear and reachable bytecode walkers. They accept a command decoder and build address-indexed tracks:

- `decodeLinearBytecodeTrack`
- `decodeReachableBytecodeBlocks`

A linear track is mostly decoded in byte order. A reachable track follows stored static targets such as jumps and calls. Cursor-prefixed wrappers adapt legacy command readers to these same walkers.

Semantic formats normally call `decodeSemanticLinearTrack`. It wraps the linear walker with the shared track lifecycle: create the track annotation, project each already-decoded command, and finalize the track's source range. Format code supplies only the one-command decoder.

### 14.5 `SequenceVm`

For semantic dialects, `SequenceVm` owns one program state and one runtime per track. It repeatedly executes the active track with the lowest `(tick, stable track order)`. At equal ticks, an earlier track keeps control while it consumes zero-time commands until it waits or ends. This explicit rule matches multi-channel drivers and makes shared state deterministic.

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

Legacy dialects remain track-major until migrated. Their dry-run and complete-sequence-prepass options are compatibility mechanisms; semantic formats should model shared state directly instead.

### 14.6 `PerformanceSequence`

The VM does not output MIDI directly. It outputs a `PerformanceSequence` made of target-neutral events:

- note;
- tempo;
- time signature;
- instrument;
- level;
- expression;
- pan;
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
- pan is -1.0 to 1.0;
- pitch bend is semitones;
- modulation amount is normalized.

Instrument events carry a source-domain identity rather than a pre-encoded bank/program pair. Level and expression events may carry neutral source quantization (the number of distinct source values), not a destination bit width. Legacy cursor dialects still populate their older compatibility fields.

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

It resolves source instrument identities against the collection's instrument sets, then assigns MIDI bank/program addresses. The synth export preparation layer performs the equivalent identity-to-preset lowering for SF2 and DLS.

The renderer can also use modulation usage analysis to scale controller values when the export request asks for observed-range modulation scaling.

---

## 15. Synth architecture

The synth model is shared by SF2, DLS, WAV, and future exporters. It describes instruments and samples in neutral terms.

### 15.1 Instruments and regions

An `Instrument` has a source-domain identity and a set of regions. A region describes when and how a sample should play. Legacy instruments may still carry bank/program compatibility fields until their formats migrate:

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

### 15.2 Samples

A `Sample` records encoded source data and decode settings. The encoded bytes stay in `SourceStore` until export. This keeps scanning cheap and preserves source-backed diagnostics.

Sample codecs currently include PCM, SNES BRR, NDS IMA ADPCM, NDS PSG, PSX ADPCM, and OKI ADPCM.

### 15.3 Decoded samples

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

### 16.1 Export preparation

Export first resolves one collection into typed pointers:

- sequence program;
- instrument sets;
- sample collections;
- miscellaneous assets;
- diagnostics for missing or wrong-type references.

This is done by `resolveCollectionAssets` on the snapshot.

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

- scan validation checks a `ScanCommit` before it enters the session;
- sequence validation checks `SequenceProgram` structure;
- synth validation checks instrument and sample model structure;
- snapshot validation checks whole-session consistency.

The validation result is a `ValidationReport`, which can be converted to diagnostics or can throw on errors.

This is an important part of the value-oriented design. Since values can be created in many places, the system needs clear gates that verify they make sense before other layers rely on them.

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
optional materializer builds collection-dependent assets
```

Akao follows this shape.

### 18.4 Sequence command readers

Command code should resemble a driver interpreter. Capcom SNES demonstrates the semantic form: each opcode-profile entry places source decoding beside playback behavior. The profile replaces separate metadata, operand, flow, and execution switches. NDS, Akao, Akao SNES, and Konami SNES still use normal cursor switches through the legacy adapter.

That is a good match for format developers’ mental model. It avoids requiring every command to be represented as a separate class or visitor.

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

NDS sequence code shows the cursor-based sequence style clearly. `NdsCursorReader` handles opcodes directly. For note opcodes, it records the note, reads velocity and duration, emits a note, and returns either wait or next depending on driver state. For program, pan, volume, tempo, jump, call, return, and end, the code stays close to the source-driver meaning.

NDS also shows that the architecture can handle recovery paths. Normal SSEQ decode uses reachable block decoding. Malformed SDAT recovery has a specialized path, but still produces normal `TrackProgram` and source annotations.

### 19.2 Capcom SNES

The Capcom SNES module shows the “single source, explicit collection” path.

The scanner finds the layout, reserves sequence/instrument/sample IDs, parses instrument/sample information when the instrument table and SPC DIR are detected, always emits the sequence, and creates one collection for the source. It owns asset metadata while `decodeCapcomSnesSequence` returns only the neutral `SequenceProgram`. If synth information is unavailable, the collection still has the sequence and a warning.

Capcom is the first semantic-command vertical slice. Its base opcode profile plus two V1 patches are the single source of command behavior. Each entry owns presentation, source reads, conversion, discovery flow, and playback side-by-side. Decode reads every operand once, stores encoded and resolved values where they differ, and records control flow; the track keeps no command-byte pool. `decodeSemanticLinearTrack` owns walking, command projection, and track annotation finalization, so the format's track function supplies only `decodeCommand`. The same profile entry is selected during rendering by program profile and opcode, and the global scheduler supplies per-track state. The executor cannot access source bytes by construction.

The track state owns duration rate, transpose, octave flags, slur state, modulation, portamento, and previous-note information. Loop and repeat commands return VM flow helpers rather than implementing export loop policy. Driver math is local to the value implementation and contains no dependency on the old parser architecture.

The layout uses the shared masked-pattern matcher, and the synth parser uses the shared SNES sample-directory/BRR reader. Capcom exposes one public header instead of separate module, layout, sequence, synth, and types headers. Its `scan` function performs recognition and layout discovery once; it does not register a duplicate `canScan` probe. One `FormatDefinition` registers both scanner and dialect. Sequence performance and synth instruments use `capcom-snes.instrument` identities; MIDI/SF2/DLS addressing is assigned in export code.

### 19.3 Akao

Akao is the most important architecture stress test in this branch.

The scanner does not try to bind everything immediately. It scans sample collections and sequences separately, then emits facts:

- sample-set IDs when present;
- sequence IDs;
- source offsets;
- articulation coverage from sample collections;
- required articulation IDs from sequences.

The resolver creates one collection per sequence. It chooses sample collections by preferred sample-set ID and by required articulation coverage. It handles PSF-like sources more narrowly, treats missing and zero sample-set IDs as the same anonymous set, sorts attached sample collections by articulation range, and marks collections incomplete when coverage is missing.

The materializer currently re-reads the selected sequence and sample collections, builds an articulation map, and creates a bound instrument set. This violates the parse-once target and should be replaced with durable symbolic articulation/sample bindings plus a pure collection binder.

Akao’s same cursor reader currently runs for analysis and rendering. That flexibility is useful during migration, but the semantic IR should ultimately make analysis a direct inspection of decoded commands.

---

## 20. How to add a new format

A new format usually follows this path:

### Step 1: Register a format definition

Create a `FormatDefinition` containing a module with:

- a name;
- `scan`;
- optional resolver;
- optional materializer.

Add the format's single semantic dialect when it contains bytecode, then pass the definition to `Session::registerFormat`. Do not add separate module and dialect registration entry points.

Put recognition at the start of `scan` and return an empty `ScanResult` for non-matches. Do not add `canScan`; it exists only for unmigrated modules.

### Step 2: Decide how collections are discovered

Use explicit collections when the scanner already knows the grouping.

Use match facts and a resolver when relationships may be discovered across multiple files or depend on incomplete evidence.

Prefer durable symbolic references and a pure binder when an asset depends on collection membership. `materializeCollection` is a legacy adapter for Akao and should not be copied into a new format.

### Step 3: Parse source structure

Use `ByteReader` for random access and `RecordReader` for sequential records. `RecordReader` performs bounds checks, returns ranged values, records fields, and reports truncation. Use focused shared platform readers such as `SnesSampleDirectory` instead of copying stream walkers into a format.

### Step 4: Build assets

Use `ScanResultBuilder` to allocate IDs and attach metadata. A source decoder should return the narrow neutral value it owns—for example, Capcom's sequence decoder returns `SequenceProgram` while the scanner registers it as a sequence asset.

### Step 5: Decode sequences into semantic commands

Define:

- one base opcode profile with sparse version patches;
- one complete definition per command, with adjacent decode and playback lambdas;
- operand names, display rules, roles, and encoded/resolved values directly in the decode lambda;
- a per-program profile in `SequenceProgram::config`;
- a program-state type only when the driver has real shared state;
- a `TrackState` type for driver-local mutable playback state; and
- a track decoder using `decodeSemanticLinearTrack` (or a reachable-block wrapper when the format requires one).

Use `SemanticCommandDecoder` and `SemanticCommandArgs` to keep generic IR construction and type erasure out of format code. The decoder reads bytes once and generic projection creates source annotations. Playback reads only named resolved operands and emits neutral performance events. It should not implement global loop export policy and must never read source bytes.

### Step 6: Build synth data in the neutral model

Build instruments, regions, samples, envelopes, loops, tuning, generators, and modulators. Keep encoded sample bytes in `SourceStore` by storing `SourceRange`s.

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

---

## 21. Component-by-component breakdown

### 21.1 `CoreTypes`

`CoreTypes` provides IDs, source ranges, ranged values, object references, severity, and diagnostics.

The important design point is that these types are small and copyable. They are the shared language between scanners, stores, snapshots, source maps, validators, and exporters.

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

New formats enter through `FormatDefinition` and `Session::registerFormat`, which couples one module to its semantic dialect. The two raw registries remain public only as a migration surface for formats that still register cursor dialect matrices.

### 21.6 `ScanResultBuilder`

`ScanResultBuilder` is the intended scanner authoring API. It creates assets, explicit collections, facts, diagnostics, source annotations, and extracted sources. It tracks reserved handles and validates that referenced handles were committed. Parsers can append directly to its diagnostic collection instead of creating a temporary vector and copying diagnostics back into the result.

This is a major readability win for format code.

### 21.7 `ScanCommit`

`ScanCommit` is a staged scan result tied to one source. It fills in default diagnostic ranges, validates the result, and commits to stores.

This is the line between temporary scanner output and durable session state.

### 21.8 `AssetStore`

`AssetStore` owns assets and tracks which source produced them. It also manages materialized assets and keeps stable IDs for materialization slots.

This is important for collection-dependent assets such as Akao bound instrument sets.

### 21.9 `CollectionStore`

`CollectionStore` reconciles desired collections by stable key. It preserves collection identity across rescans and marks collections stale when referenced assets are removed.

It also validates collection references and reports missing or wrong-type assets.

### 21.10 `MatchFactStore` and resolver helpers

`MatchFactStore` stores facts. Resolver helpers make those facts readable by format-specific collection resolvers.

`MatchFactIndex` is especially useful because it combines fact filtering, payload typing, asset lookup, and optional source lookup.

### 21.11 `SequenceProgram`

`SequenceProgram` records semantic source commands, program profile/configuration, and playback defaults. It is the stable parsed form of a sequence.

Each semantic command stores its opcode, named operands, source provenance, and decode flow. The `AddressIndex` lets the VM resolve runtime control flow by source addresses instead of assuming vector order is execution order. Byte pools exist only for legacy dialects.

### 21.12 `SemanticCommandDecoder`, `RecordReader`, and legacy `VmCommandCursor`

`RecordReader` is the small imperative checked reader for one source record. It owns sequential cursor bounds, exact field ranges, captured field metadata, and truncation diagnostics without introducing a schema language. `SemanticCommandDecoder` adds the narrow command-authoring conveniences for named operands, raw/resolved values, and discovery flow. `CommandSourceMap` performs automatic projection; ordinary records can reuse the reader's captured fields when building their one durable annotation.

`VmCommandCursor` remains the two-phase compatibility surface for unmigrated sequence dialects. It should not be used for new semantic implementations because render-phase cursors can reparse stored command bytes.

### 21.13 `SequenceDialect`

A semantic dialect connects a `SequenceProgram` to executable driver behavior. It packages program/track state factories, a byte-free command executor, timebase, and default behavior.

The implementation uses `std::any` only to erase the concrete program and track runtime-state types. Program-specific version data is not stored in dialect context; it lives on the immutable program. Legacy dialect context remains until those formats migrate.

### 21.14 `SequenceVm`

`SequenceVm` is the shared interpreter for parsed sequences. Semantic dialects run through a global `(tick, stable track order)` scheduler with one program state and per-track runtimes. The VM handles command limits, loops, calls, returns, repeats, diagnostics, and event emission.

Legacy dialects still use track-major execution and optional prepasses. Those paths are adapters to remove as formats migrate.

### 21.15 `PerformanceModel`

`PerformanceModel` is the neutral musical output from the VM. Its events keep source command and source annotation IDs, so output can be traced back to source bytes.

This model is the main bridge to future non-MIDI sequence outputs.

### 21.16 `SynthModel`

`SynthModel` is the neutral instrument/sample layer. It represents instruments, regions, samples, envelopes, loops, tuning, generators, modulators, and decoded PCM.

It is designed for SF2/DLS/WAV export without making those formats leak into scanners.

### 21.17 `Export`

Export resolves a collection, renders the sequence only when needed, decodes samples only when needed, and produces artifacts with diagnostics.

The export layer is careful to share work. For example, if both MIDI and observed-range modulation are requested, the sequence render result can be reused.

### 21.18 `Validation`

Validation checks values at the boundaries. The most important boundary is scan commit. A scanner can be generous and return diagnostics, but invalid durable references should be rejected before entering session stores.

---

## 22. Design strengths

### 22.1 Clear ownership

The architecture makes ownership easy to state:

- `SourceStore` owns bytes.
- session stores own durable values.
- snapshots own read-only copies.
- exporters own temporary output data.
- format scanners own only temporary parse state.

That clarity is a major improvement over a graph of long-lived parser objects.

### 22.2 Better testability

Values are easy to inspect. Tests can assert on assets, collections, source annotations, command fields, performance events, and export artifacts without needing UI state.

The existing tests under `tests/core` and `tests/formats` reflect this direction.

### 22.3 Format code can be local and readable

Complete opcode-profile entries are a strong fit for sequence drivers. Keeping decode and playback lambdas adjacent makes the driver visible without coupling playback to source encoding or scattering one command across several switches.

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

### 23.1 Semantic and legacy sequence paths coexist temporarily

Capcom uses semantic commands and global scheduling, while four format families still use the cursor adapter and track-major execution. Shared code must preserve both until migration is complete. Avoid adding features only to the legacy path; doing so increases the cost of removing it.

### 23.2 `std::any` hides some type checking

`SequenceDialect` type-erases program and track runtime states through `std::any`. A mismatch can become a runtime issue rather than a compile-time issue.

Keep casts in the one state factory/executor family for a format, cover them with focused tests, and never use `std::any` as a program-configuration property bag.

### 23.3 Source map conventions need discipline

The source map is flexible. That is useful, but flexibility can lead to inconsistent annotation names, roles, fields, and detail kinds across formats.

A small style guide for source annotations would help future formats feel consistent in HexView and tests.

### 23.4 Materialization is a migration blocker

The current Akao materializer adds stable slots, stale removal, repeated diagnostics, and source re-reading.

Do not extend this mechanism. Replace it with symbolic bindings and a collection binder that is pure over snapshot values, then remove the second asset lifecycle.

### 23.5 Collection resolution depends on good keys

Stable collection keys are central. Poorly chosen keys can cause duplicates, unexpected updates, or collections that fail to merge when more sources arrive.

New format modules should treat collection-key design as part of the format port, not as an afterthought.

---

## 24. Recommendations

### 24.1 Write a short format-authoring guide

The branch would benefit from a contributor-facing guide with one page each for:

- simple scanner with explicit collection;
- scanner with match facts and resolver;
- semantic sequence decoder/executor with a small opcode profile;
- synth parser using `Instrument`, `Region`, and `Sample`;
- source map annotation conventions.

### 24.2 Keep command decoding and execution boring

The decoder should read operands and store flow once. Playback should consume named resolved operands, update state, emit events, and return runtime flow. Put both operations in the same profile entry; the two lifecycle entry points should only select and invoke that entry.

Keep opcode knowledge in one profile plus sparse patches. Avoid command-kind and operand-ID enums, parallel size/metadata tables, analysis interpreters, and per-opcode handler classes.

### 24.3 Prefer explicit collections when possible

If the source format already tells you what belongs together, emit an explicit collection. Do not force every format through match facts just for consistency.

Use match facts when there is genuine uncertainty or cross-source matching.

### 24.4 Keep export policy out of scanners

Scanners should not decide MIDI channel behavior, loop export behavior, or SF2/DLS details. They should emit neutral sequence and synth values. Export options and shared renderers should handle target-specific policy.

### 24.5 Make source annotations consistent

A future small convention document should define common detail-kind prefixes, field names, and roles for headers, tables, pointers, commands, operands, instruments, regions, and samples.

This will help UI and tests stay stable as more formats are ported.

### 24.6 Replace and then test Akao materialization

Add durable symbolic sample bindings, make collection rebuilding independent of `SourceStore`, and test that repeated rebuilds are pure and diagnostics do not accumulate.

### 24.7 Migration roadmap and invariants

The remaining order is intentional:

1. add neutral control transitions for fades/slides;
2. migrate Konami SNES to semantic commands and delete its modulation shadow interpreter;
3. migrate Akao SNES, replacing its dialect matrix, byte rewriting, tempo scraping, prepass, and tick motion;
4. add symbolic Akao articulation/sample binding and remove materialization source reads;
5. separate NDS canonical decoding from malformed-data repair;
6. migrate the remaining layouts/synth parsers to `RecordReader` and shared platform primitives;
7. remove all `canScan`, cursor-dialect, byte-pool, prepass, and materialization adapters;
8. migrate each format to one `FormatDefinition`, then remove direct access to the two raw registries.

Architectural acceptance criteria:

- every source structure is parsed once;
- semantic VM execution has no byte access;
- collection rebuilding has no source access;
- each driver has one opcode mapping with adjacent decode and playback behavior;
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
3. ScanCommit validates results before storing them.
4. Session stores own the accepted values.
5. Collection resolution groups assets into export units.
6. A pure binder resolves durable symbolic references (legacy Akao still materializes and reparses).
7. SessionSnapshot exposes a read-only view.
8. SequenceVm globally schedules semantic commands into neutral performance events (legacy dialects use an adapter).
9. Exporters turn collections into files.
```

The core idea is not just “use modern C++.” The deeper idea is to make parsed game audio data durable, inspectable, and independent of the parser that found it. That gives VGMTrans a better base for testing, UI explanation, multi-source matching, and future export formats.
