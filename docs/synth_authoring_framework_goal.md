# Goal: Create an intuitive framework for authoring synth conversions

This document began as an implementation brief. The user subsequently authorized implementation with CapcomSnes, KonamiSnes, and NDS as successive proofs. The other format migrations remain deliberately deferred to later work.

## Implementation status

The first framework slice is implemented:

- the two shared content builders and their short-lived entry proxies;
- dense sample, instrument, and region ownership with source provenance and sample links;
- scan-time factories and explicit consuming commits;
- detached construction and immutable collection-dependent preparation with stable derived asset IDs;
- a builder-backed SNES BRR adapter;
- collection-level sample-reference, identity, and explicit-address validation;
- CapcomSnes, KonamiSnes, and NDS as the first three migrated formats;
- focused lifecycle, annotation, validation, preparation, Capcom, Konami, and NDS tests.

Akao/AkaoSnes remain future migrations. Their cases shaped this API and are covered where possible by synthetic framework tests, but they have not yet been migrated.

The remaining migration sections below preserve the broader roadmap. Requirements specific to those deferred formats are not claims about the completed Capcom-first slice.

### First-slice measurements and verification

The dedicated framework is 1,053 production lines: 802 for the two builders and entry proxies, plus 251 for the exceptional collection-preparation adapter. The thin scan integration adds 82 lines to `ScanResultBuilder`, including comments that explain its less obvious lifetime rules. This is intentionally flat, explicit C++ rather than a smaller-looking hierarchy or template layer. The shared SNES adapter grew by 51 lines while it gained builder population and concrete reference lookup.

CapcomSnes synth conversion fell from 221 to 211 lines. KonamiSnes fell from 412 to 395 lines without adding any generic production code. Together, the first two consumers are only 27 lines shorter. That is not enough raw code reduction to justify more than a thousand lines of framework on its own, and this goal must not claim otherwise.

The more meaningful Konami result is the bookkeeping removed from its construction path. Format code no longer owns the instrument vector, program-to-dense-index map, canonical sample indexes, hand-built `SampleRef` and `ObjectRef` values, region owners, asset ranges, or sample-use links. `getOrAdd` directly expresses several percussion entries becoming one kit, and the builder gives sparse programs and grouped regions durable owners automatically. Konami's unusual transformed-address lookup and sample-zero fallback remain visible in one named format helper instead of leaking into the shared API.

This is a conceptual simplification and it fixed annotation identities that the old manual indexing could not represent correctly, but it is still not the dramatic format-code reduction sought by the goal. Subsequent migrations are go/no-go evidence, not a reason to add more framework layers. If they do not become plainly smaller and easier to author, simplify or remove machinery before declaring the framework justified.

NDS exposed a genuine weakness in the first API draft. That draft made the format carry a collection handle, a copied source-key lookup, and pointers tying them together. `NdsSynth.cpp` grew from 523 to 539 lines, while `Nds.h` and `NdsModule.cpp` gained another 11 lines. The framework was handling dense indexing internally but then making the format preserve part of that bookkeeping across separately discovered assets.

The revised API keeps a committed sample builder's source-key lookup inside `ScanResultBuilder`, provides auto-reserving `samples()` and `instruments()` factories, centralizes lookup-and-warning behavior, and can address a region already present in an ordinary `Instrument` value. NDS now passes only ordinary `ScanSampleCollectionRef` values between its module and synth code. Its SWAV helper also adds the parsed sample and provenance directly instead of returning an NDS-only carrier value. `Nds.h` and `NdsModule.cpp` are back at their original sizes, and `NdsSynth.cpp` is 510 lines, 13 lines below its 523-line baseline. Across CapcomSnes, KonamiSnes, and NDS, the three main synth implementations are 40 lines shorter. Including Konami's one required header line leaves directly migrated format code 39 lines shorter.

The NDS migration nevertheless removes a real correctness hazard. A SWAR source entry is now the builder key, so rejecting source sample 1 does not cause an SBNK reference to source sample 2 to point at the wrong dense sample. An SBNK can resolve each region through any of four independent SWAR lookups, while the builder assigns dense sample, instrument, and region owners. Missing archive slots and rejected samples produce understandable warnings instead of collectionless or out-of-range references.

That correction and the removal of manual ownership/link bookkeeping are worthwhile. NDS is now modest evidence of raw authoring-code reduction as well, but the overall framework still does not amortize its production line count across three formats. Its case rests primarily on reducing synchronized concepts, centralizing invariants, and improving annotation correctness. Akao remains useful evidence because its binding rules are structurally different, but its migration must not add another common layer merely to make the existing framework appear justified.

Verification for this slice includes:

- the full value-core test executable and parity self-test;
- focused builder, source-provenance, collection-preparation, collection-validation, session replacement/removal, and sparse Capcom table tests;
- all 55 collections in `Breath of Fire 2.rsn`: summary parity, SF2 parity, DLS parity, sequence-event-simulation sanity, and direct export smoke producing 2,789 artifacts;
- all 21 collections in `Axelay.rsn`: sequence-event-simulation sanity and direct export smoke producing 585 artifacts (21 MIDI, 21 SF2, 21 DLS, and 522 WAV files);
- Axelay summary parity matches through the same first 12 collections as the pre-migration baseline, then reaches the unchanged legacy/value sample-offset difference in “Set Up”; direct synth comparison reaches the unchanged Axelay modulation-range difference. The first mismatches and values are identical before and after the Konami migration;
- all 40 collections in the Castlevania: Dawn of Sorrow ROM: summary, SF2, and DLS parity, plus direct export smoke producing 991 artifacts (40 MIDI, 40 SF2, 40 DLS, and 871 WAV files). MIDI reaches the identical pre-existing `SDL_BGM_ARR1_` controller mismatch at normalized event 634 before and after the synth migration;
- a malformed NDS fixture with two SWARs proves sparse source sample lookup, an absent middle sample, two non-adjacent archive slots, sparse SBNK programs, warnings, and dense sample/instrument/region source owners;
- the configured Mega Man X corpus: summary, SF2/DLS, and export checks pass. Its two MIDI CTests still stop at the pre-existing “Stage Select 2” value-side tempo-event difference, which is sequence behavior outside this synth change.

## Objective

Create a value-oriented synth-authoring framework that does for samples, instruments, and regions what `CompilerCursor` does for sequence commands: keep source data and its meaning close together while shared machinery handles repetitive, error-prone bookkeeping.

The primary measure of success is the format author's experience. Normal synth conversion should require little more than:

1. read and validate source data;
2. construct ordinary `Sample`, `Instrument`, and `Region` values;
3. add them to the appropriate asset builder;
4. attach detailed source records where the format has useful names, fields, or structure;
5. hand the completed builders to the existing result or collection-preparation owner.

The framework must materially reduce both required code and the number of concepts a format author must keep synchronized. It must also support exceptional formats and collection-dependent construction without forcing their complexity into every normal format.

The completed scan result must also retain everything a future HexView and TreeView need to explain synth data. UI code must not have to reconstruct source provenance from format rules or infer object identity from byte ranges.

Add short, plain-language comments before non-obvious framework invariants and unusual format rules. Comments should explain why the behavior exists and avoid internal jargon.

Collection-dependent behavior formerly implemented through legacy `useColl()` is an important requirement, but it is not the organizing purpose of the framework.

Do not implement or migrate SuzukiSnes as part of this goal. SuzukiSnes is a design case and future consumer.

## Architectural decision

The author-facing synth framework has two independent asset-content builders:

- `SampleCollectionBuilder` builds the contents of one sample-collection asset.
- `InstrumentSetBuilder` builds the contents of one instrument-set asset.

There is no top-level `SynthBuilder` object that owns both. A source component may use “SynthBuilder” in a filename if useful, but it must not become another author-facing owner, lifecycle, or durable model. Collections, sequence matching, collection preparation, and late binding remain outside the two builders.

The builders consume the existing neutral value types. Do not create a fluent setter for every field of `Sample`, `Instrument`, or `Region`. Doing so would duplicate `SynthModel`, enlarge the API whenever the model changes, and force authors to learn two representations of the same data.

The builders may return small, short-lived entry proxies so an author can attach source information or add regions to the instrument just created. These proxies are normally held with `auto`; they are not durable model objects and are not independent top-level builders.

## Why this is the right analogy to `CompilerCursor`

The desired consistency is conceptual, not literal.

`CompilerCursor` keeps one command's reads, semantic meaning, state changes, and playback effects in one imperative block. Synth authoring should likewise keep a source entry, the musical value derived from it, and its provenance in one local block.

Synth construction differs in one important way: instruments and samples often come from separate tables, are joined later, or are shared by several assets. Combining `RecordReader` with the synth builders would therefore make complex formats harder rather than easier. `RecordReader` remains the source-reading API; the synth builders remain asset-construction APIs.

The equivalent local handoff is:

```cpp
RecordReader record(reader, address, end, &result.diagnostics());
const auto srcn = record.u8("srcn", SourceValueDisplay::Hex);
const auto adsr1 = record.u8("adsr1", SourceValueDisplay::Hex);

// Validation and format-specific conversion remain ordinary C++ here.

auto instrument = instruments.add(programKey, Instrument{
    .identity = sourceIdentity,
    .explicitAddress = exportAddress,
    .name = name,
});
instrument.source(name, record.range(), "format-instrument")
    .fields(record.fields());
```

No synth parsing language, table schema, or second representation of the model is needed.

## Desired common authoring surface

A normal format should read approximately like this:

```cpp
auto samples = result.samples();
auto instruments = result.instruments();

for (const auto& info : sampleInfos) {
  samples.add(info.sourceIndex, Sample{
      .name = info.name,
      .codec = AudioCodec::SnesBrr,
      .encodedData = info.data,
      .sampleRate = 32000,
      .loop = info.loop,
  }).source(info.label, info.range, info.kind)
      .fields(info.sourceFields);
}

for (const auto& info : instrumentInfos) {
  const auto sample = samples.find(info.srcn);
  if (!sample) {
    instruments.warning("Instrument sample was not found", info.range);
    continue;
  }

  const auto pitch = instrumentPitch(info);
  auto instrument = instruments.add(
      programKey(info),
      Instrument{
          .identity = instrumentIdentity(info),
          .explicitAddress = instrumentAddress(info),
          .name = instrumentName(info),
          .modulation = instrumentModulation(version),
      });

  instrument.source(instrumentName(info), info.range, instrumentKind(info))
      .fields(info.sourceFields);

  instrument.region(*sample, Region{
      .keyRange = regionKeys(info),
      .tuning = pitch.aggregate,
      .rootKey = pitch.rootKey,
      .fineTuneCents = pitch.fineTuneCents,
      .envelope = instrumentEnvelope(info),
      .attenuationDb = instrumentAttenuation(info),
  }).source("Region", info.range, regionKind(info));
}

result.sampleCollection(sampleName, std::move(samples));
result.instrumentSet(instrumentName, std::move(instruments));
```

The final two calls are small convenience overloads on `ScanResultBuilder`. Each builder already knows its reserved asset ID and accumulated range. Moving the builder into the result is an explicit commit and ownership boundary; it is not destructor-driven finalization.

Detached or collection-preparation code can instead call `std::move(builder).finish()` to obtain the ordinary completed value.

For an instrument intentionally assembled from several source entries, use `getOrAdd` instead of `add`:

```cpp
auto drumKit = instruments.getOrAdd(
    drumKitKey,
    Instrument{
        .explicitAddress = InstrumentAddress{.bank = 127, .program = 0},
        .name = "Drum Kit",
    });

drumKit.source(entryName, entryRange, "percussion-entry")
    .fields(entryFields);
drumKit.region(sample, Region{
    .keyRange = KeyRange{.low = key, .high = key},
    .rootKey = rootKey,
    .envelope = envelope,
}).source("Region", entryRange, "percussion-region");
```

`add` is the normal operation and diagnoses a duplicate builder key. `getOrAdd` is an explicit statement that several source entries are meant to contribute to one instrument.

`getOrAdd` uses `initialValue` only when the key is first encountered. Later calls return the existing instrument and do not merge or reinterpret the new aggregate. Format code remains responsible for keeping shared instrument-level properties consistent across contributing entries.

## Exact responsibility boundary

### `SampleCollectionBuilder`

`SampleCollectionBuilder` owns only mechanical work for one sample collection:

- dense sample storage;
- stable dense indexes once a sample is added;
- mapping temporary source keys, such as SRCNs or SWAR entry indexes, to concrete `SampleRef` values;
- explicit aliases where several source keys refer to one stored sample;
- correct sample source-map owners;
- sample-collection range accumulation;
- duplicate-key and invalid-alias diagnostics;
- final movement into an ordinary `SampleCollection`.

Its conceptual surface should remain close to:

```cpp
class SampleCollectionBuilder {
public:
  class Entry;

  Entry add(u64 sourceKey, Sample sample);
  Entry alias(u64 aliasKey, u64 existingKey);
  [[nodiscard]] std::optional<SampleRef> find(u64 sourceKey) const;

  // Optional snapshot for a detached owner that cannot retain lookup state
  // when it finishes the sample builder.
  [[nodiscard]] SampleRefLookup refs() const;

  SampleCollectionBuilder& include(SourceRange range);
  [[nodiscard]] SourceRange range() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] size_t size() const;

  void warning(std::string message, SourceRange range = {});
  void error(std::string message, SourceRange range = {});

  [[nodiscard]] SampleCollection finish() &&;
};
```

The exact integer width and minor names may change during a compiling prototype, but the concepts should not broaden.

`find()` returns the existing durable `SampleRef` type, not a new author-facing `SampleHandle`. The builder has already translated the source key to a dense index and knows the collection asset ID. `InstrumentSetBuilder` can derive the corresponding sample `ObjectRef` when it creates source links.

`refs()` is an opt-in immutable lookup snapshot, not another construction layer. Most formats should keep the sample builder alive and call `find()` directly. When a scan commits the sample builder first, `ScanResultBuilder` retains the lookup automatically and later code calls `sampleByKey()` or `sampleByKeyOrWarning()` with the ordinary sample-collection handle. A detached owner may still retain `refs()` explicitly when no scan result owns the lifecycle.

The lookup type should be a small copyable value normally held with `auto`. It exposes lookup only; it cannot add, remove, annotate, commit, or otherwise continue construction.

Do not automatically deduplicate samples merely because their encoded ranges or bytes match. Whether two source entries are aliases is format meaning and should remain explicit or live in a shared platform adapter that understands the source format.

Samples must never be removed or reordered after being added. That invariant keeps every returned `SampleRef` stable.

### `InstrumentSetBuilder`

`InstrumentSetBuilder` owns only mechanical work for one instrument set:

- dense instrument storage;
- mapping temporary grouping keys to dense instrument indexes;
- unique `add` behavior;
- intentional `getOrAdd` behavior for drum kits and other multi-entry instruments;
- appending regions to the selected instrument;
- assigning the region's concrete `SampleRef`;
- correct dense instrument source-map owners;
- region and instrument `UsesSample` links;
- instrument-set and object range accumulation;
- duplicate grouping-key diagnostics;
- final movement into an ordinary instrument vector.

Its conceptual surface should remain close to:

```cpp
class InstrumentSetBuilder {
public:
  class Entry;
  class RegionEntry;

  Entry append(Instrument instrument);
  Entry add(u64 groupingKey, Instrument instrument);
  Entry getOrAdd(u64 groupingKey, Instrument initialValue);
  [[nodiscard]] std::optional<Entry> find(u64 groupingKey);

  InstrumentSetBuilder& include(SourceRange range);
  [[nodiscard]] SourceRange range() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] size_t size() const;

  void warning(std::string message, SourceRange range = {});
  void error(std::string message, SourceRange range = {});

  [[nodiscard]] std::vector<Instrument> finish() &&;
};
```

The grouping key is temporary builder state only. The builder must not infer or assign:

- `InstrumentIdentity`;
- MIDI bank or program;
- `explicitAddress`;
- percussion status;
- any other durable musical field.

Those decisions remain visible in the `Instrument` aggregate. A source instrument identity, an export address, and a key used to merge percussion entries are related in some formats but are not the same concept.

`append` supports instruments that do not need temporary lookup and makes it possible to seed a collection-derived builder from copied value objects without inventing a fake source key.

When an `Instrument` passed to `append`, `add`, or `getOrAdd` already contains regions, the builder must accept them, register their sample references for validation and source links, and preserve the ordinary value. Region-by-region construction is a convenience, not a required alternate model.

Do not add removal or automatic pruning. A format must validate before adding an object. This keeps dense indexes and source owners stable and prevents hidden policy such as silently dropping regionless instruments.

### Short-lived entry proxies

The nested entry types are ergonomic references to objects already owned by a builder. They should provide only the operations that need builder context:

```cpp
class SampleCollectionBuilder::Entry {
public:
  [[nodiscard]] SampleRef ref() const;
  [[nodiscard]] Sample& value();
  [[nodiscard]] AnnotationBuilder source(
      std::string_view label,
      SourceRange range,
      std::string_view kind = {});
};

class InstrumentSetBuilder::Entry {
public:
  [[nodiscard]] Instrument& value();
  [[nodiscard]] AnnotationBuilder source(
      std::string_view label,
      SourceRange range,
      std::string_view kind = {});
  [[nodiscard]] RegionEntry region(SampleRef sample, Region region);
  [[nodiscard]] RegionEntry regionAt(u32 regionIndex);
};

class InstrumentSetBuilder::RegionEntry {
public:
  [[nodiscard]] Region& value();
  [[nodiscard]] AnnotationBuilder source(
      std::string_view label,
      SourceRange range,
      std::string_view kind = {});
};
```

`value()` is the narrow escape hatch for an unusual format-specific adjustment. It is not the normal route and should not cause every exceptional operation to become a new shared method.

`regionAt()` exposes a region already present in the ordinary `Instrument` aggregate. It exists so a parser that naturally constructs a complete instrument can attach an exact region source record without moving the regions out, clearing the vector, and adding the same values again. Generic fallback annotations remain sufficient when the region's durable range already describes all of its source bytes.

Entry proxies are valid only while their owning builder remains in place and unfinished. Format code should not store them long-term. `SampleRef`, `ScanSampleCollectionRef`, and the optional detached `SampleRefLookup` are the stable cross-step results.

### Source provenance

`source()` should return the existing `AnnotationBuilder`. Do not introduce a second source-annotation DSL. The builder supplies the repetitive generic projection:

- sample sources use `SourceRole::Sample` and the correct dense sample owner;
- instrument sources use `SourceRole::Instrument` and the correct dense instrument owner;
- region sources use `SourceRole::Region` and the correct dense region owner;
- region sources link to their exact sample;
- instrument sources link to the union of samples used by that instrument;
- source ranges contribute to object and asset range accumulation.

Add `ObjectKind::Region` and `ObjectRefs::region(instrumentSetAsset, instrumentIndex, regionIndex)` to the existing
compact object-reference vocabulary. The two indexes are the region's position in the durable instrument-set value,
not a source program number, grouping key, or table index. This is not a new region model or an author-facing handle;
it is the stable join key used by source annotations, diagnostics, and future UI code. Append-only builder behavior keeps
the reference valid.

`SourceAnnotation::parent` describes how source records nest in a source outline. It must not be used as a substitute
for region identity. A region may have several annotations, and an instrument assembled from split tables may have
several possible source parents. Every annotation for one region therefore receives the same region owner even when
those annotations have different ranges or parents.

The format continues to supply labels, local kinds, fields, descriptions, table parents, pointers, and unusual links. Since `source()` returns `AnnotationBuilder`, existing calls such as `.fields(...)`, `.derived(...)`, `.parent(...)`, and `.link(...)` remain available.

Calling `InstrumentSetBuilder::Entry::source()` establishes the default parent for subsequently added region sources on that entry. This matches the ordinary source-entry-then-region flow, including drum kits. Formats with split tuning/ADSR tables or other unusual provenance can override the parent explicitly.

Source behavior must be order-independent for sample links. If an instrument source is added after regions, it still receives links to those regions' samples. If a region is added after an instrument source, the same links are added without duplication.

One durable object may have several source records. This is required for:

- a drum kit assembled from many percussion entries;
- Akao SNES tuning and ADSR stored in separate tables;
- aliases that give one sample several source-directory entries.

If an object's durable `range` is invalid, source records should supply a conservative covering range when they share a source. Exact disjoint ranges remain in the source map. An explicitly supplied valid model range is never silently replaced.

`include(range)` supplies an explicit asset-range input when the meaningful container or table range is known. Once at least one range has been included, object source records no longer widen the asset metadata range; this prevents an SNES BRR payload from stretching a sample collection whose meaningful root is the sample directory. Multiple included ranges may form a covering range only when they share a source. If no range is included, source-record accumulation is the fallback. The builder must never manufacture one `SourceRange` that appears to span bytes from different sources.

Detached construction must work when no live source map is available. The source calls should remain safe and preserve range accumulation, while annotation creation becomes optional. Do not require a separate detached authoring vocabulary.

When a live source-map sink is present, the builders guarantee at least basic annotation coverage for every
source-backed object:

- an explicitly supplied sample source record is preferred; otherwise a sample with valid `encodedData` receives a
  generic sample annotation for that payload;
- an explicitly supplied instrument source record is preferred; otherwise an instrument with a valid `range` receives
  a generic instrument annotation;
- an explicitly supplied region source record is preferred; otherwise a region with a valid `range` receives a generic
  region annotation;
- objects with no valid source range are treated as genuinely derived and do not receive fabricated range-less
  annotations.

The fallback is intentionally modest: it supplies the generic role, label, range, owner, and automatic sample links.
It does not invent field names, table structure, format-specific kinds, or descriptions. Calling `source()` remains the
normal path for a parsed source record, and suppresses the fallback for that object. This makes accidental omission
degrade to a useful basic view instead of making the object invisible, while keeping format meaning explicit.

### Future HexView and TreeView data contract

The UI consumes two related but distinct structures from `SessionSnapshot`:

- durable synth values provide the logical hierarchy: instrument-set asset -> instrument -> region, and
  sample-collection asset -> sample;
- `SourceMap` provides the source hierarchy and presentation: byte ranges, parents, roles, labels, descriptions,
  format kinds, outline policy, fields, and links.

`ObjectRef` joins those structures. It lets a logical tree node find all of its source records with `ownedBy()`, and it
lets a selected byte annotation navigate back to the corresponding instrument, region, or sample. Multiple annotations
with the same owner are several explanations of one logical object, not duplicate logical objects. This distinction is
essential for Akao split tables, percussion kits assembled from many entries, and samples represented by both directory
records and encoded payloads.

The retained data must support both useful projections without committing the framework to a particular widget:

- a logical asset view built from `InstrumentSetAsset` and `SampleCollectionAsset`, with owned source records shown
  beneath or alongside each object;
- a source outline built from annotation `parent`, `role`, and `outline`, including format-specific tables, pointers,
  data blocks, and entries that do not correspond one-to-one with musical objects.

For HexView, every source-backed annotation has an exact primary range. `SourceField` retains the exact range, decoded
value, and display hint for each encoded field; a derived field has no range and is display-only. A future view can find
annotations at or intersecting a byte range, then inspect their fields for the most precise explanation. It can also
highlight every disjoint record owned by a selected logical object. These are projection queries over retained data;
they do not require format code to create UI objects during scanning.

For TreeView, `label`, `description`, `localKind`, `detailKind`, `role`, and `SourceOutlinePolicy` provide presentation
metadata without becoming the musical model. Ordinary scalar fields stay lightweight `SourceField` values and can be
rendered as virtual children. A source structure that needs its own nesting, selection identity, description, links, or
outline policy should remain a full `SourceAnnotation`. Do not enlarge `SourceField` into another annotation hierarchy.

The source map may add indexes for owner or field-range lookup if UI profiling later justifies them. That is an internal
query optimization and must not change the authoring surface or duplicate retained data.

### Source-table vocabulary

Use **table entry** for one item in a binary table and **record** for the bounded `RecordReader` used to parse its fields. Avoid **row**: source bytes are not inherently two-dimensional, and the term is easy to confuse with a musical region.

Keep **region** exclusively for a playable key or velocity zone.

The value-oriented source-map vocabulary now uses `SourceMapBuilder::entry()` and `SourceRole::TableEntry`. Touched local `RecordReader` variables use `record`.

## What the builders must not own

Neither builder should:

- reserve or choose which assets belong to a collection;
- own a sequence or combined synth;
- read bytes or discover tables;
- decide whether a source entry is valid;
- interpret hardware pitch, envelope, pan, attenuation, or modulation;
- infer instrument identity or export addressing from a grouping key;
- decide whether two samples are aliases without format/platform knowledge;
- resolve Akao sample-set matching;
- implement collection-dependent orchestration;
- perform target-specific SF2, DLS, MIDI, or WAV lowering;
- observe sequence performance to choose modulation scaling;
- hide commit in a destructor.

That boundary is the main defense against framework bloat.

## Integration with existing construction owners

### Scan-time construction

`ScanResultBuilder` provides two small auto-reserving factories for the normal case:

```cpp
auto samples = result.samples();
auto instruments = result.instruments();
```

The factories reserve an asset ID and inject it together with the source map and diagnostic sink. Overloads accepting an existing handle remain available when another object had to refer to the asset before construction began. The content builders themselves do not allocate IDs.

`ScanResultBuilder` should also provide consuming commit overloads:

```cpp
result.sampleCollection(displayName, std::move(samples));
result.instrumentSet(displayName, std::move(instruments));
```

These overloads finish the builder and then use its asset ID and final accumulated range to call the existing asset-commit machinery. Finishing first matters because an unusual format may have supplied a late durable range through `value()`. An optional explicit asset range may be supported, but `builder.include(range)` should cover the normal case.

A sample-builder commit also retains its source-key lookup inside `ScanResultBuilder` for the remainder of the scan. Later instrument parsing can therefore resolve a sparse source key with an ordinary `ScanSampleCollectionRef`; the format does not need a wrapper that couples the handle to copied builder state. `sampleByKeyOrWarning()` combines this lookup with the common missing-reference diagnostic.

This is an adapter at the existing assembly boundary, not a third synth builder.

### Detached construction

The same two content builders must be constructible with:

- a target asset ID;
- an optional source-map sink;
- a diagnostic sink;
- any small internal services required to build references.

`finish()` returns ordinary model content. No temporary handles, lookup maps, or builder state may enter `SessionSnapshot`, `SynthModel`, or exporters.

Use a small internal services object if it materially simplifies implementation, but keep it out of ordinary format code. Do not use inheritance, `std::any`, a string-keyed property bag, or a generic dependency container.

## Shared platform adapters

Platform helpers should populate the generic builders rather than return parallel neutral-model structures that formats must reconcile manually.

For SNES, the intended shape is:

```cpp
const auto brr = addSnesBrrSamples(samples, catalog);
const auto sample = brr.findSrcn(info.srcn);
```

The adapter may own BRR construction, SRCN aliases, directory/payload source annotations, and platform-specific lookups such as canonical stream or start address. It returns concrete `SampleRef` values backed by `SampleCollectionBuilder`; it does not create another sample model.

NDS should normally use `SampleCollectionBuilder` directly:

```cpp
samples.add(swarIndex, parseNdsWave(...));
```

If SWAR entry 3 is invalid, entry 4 must still map to its correct dense sample through `find(4)` before commit or `ScanResultBuilder::sampleByKey()` afterward.

Add a platform adapter only when it removes repeated, genuinely platform-specific work from at least two formats.

## Collection-dependent construction: the `useColl()` replacement

Build collection-specific structural changes at the earliest point where all required dependencies and matching decisions are known. If that point is after collection resolution, materialize the derived values once before exporters consume them. Never implement the change by temporarily mutating a shared instrument set during each export.

Do not defer construction merely because legacy code used `useColl()`. When a scanner already has the sequence data, instrument tables, sample tables, and an unambiguous collection relationship, it should build the sequence-specific instrument set during the normal scan with the same two builders. Use collection preparation only when matching or binding information genuinely becomes available after independently scanned assets are resolved. This keeps simple one-source formats on the simple path.

Provide one optional `CollectionPreparation` assembly helper over the existing collection materialization stage. It is the collection-specific counterpart to `ScanResultBuilder`, not a synth builder and not part of the static-format common path.

Its responsibilities are:

- expose the resolved collection and its base assets read-only;
- allocate stable derived asset IDs by named slot;
- create the same `SampleCollectionBuilder` and `InstrumentSetBuilder` instances for derived assets;
- collect derived ordinary assets and diagnostics;
- explicitly keep, append, replace, or remove collection asset references;
- finish one deterministic prepared collection result.

The intended exceptional flow is approximately:

```cpp
CollectionPreparation prepared(context);

const auto* base = context.snapshot.asset<InstrumentSetAsset>(baseInstrumentSetId);
if (base == nullptr) {
  return prepared.incomplete("Base instrument set was not found");
}

auto instruments = prepared.instruments("effective-instruments");
for (const auto& instrument : base->instruments) {
  instruments.append(instrument);  // ordinary value copy
}

// Apply a decoded, sequence-owned drum-kit or override recipe here using the
// same add/getOrAdd/find/region vocabulary as scan-time construction.

prepared.replaceInstrumentSet("Effective Instruments", std::move(instruments));
return std::move(prepared).finish();
```

The exact helper names may follow existing `MaterializationContext` conventions, but the semantic boundaries are fixed:

- base assets are immutable;
- derived assets are ordinary values with stable IDs;
- preparation is deterministic and independent of export target;
- static formats define no callback and pay no authoring cost;
- multiple base and derived instrument sets and sample collections are supported;
- no `useColl()`, `unuseColl()`, temporary-object sink, mutation, or reset lifecycle reappears under new names.

Formats may either build a derived set from scratch or copy ordinary base values into an `InstrumentSetBuilder`. Use `append` for untouched copied instruments; use `add(formatKey, instrument)` when later overrides need `find(formatKey)` and `value()`. Do not create a generic patch language, recipe schema, or overlay hierarchy merely to avoid copying small value objects.

Sequence-owned preparation data should be decoded into durable values when the surrounding format architecture already has an appropriate place for it. The synth-builder framework must not solve this by adding opaque payloads, a universal recipe tree, or format-specific alternatives to the core asset variant.

Akao's current parse-twice materialization debt is a separate problem. This framework must let its binder feed resolved `SampleRef`s into `InstrumentSetBuilder`, but it must not broaden the common synth API solely to redesign Akao recipe persistence in the same change.

## Classify legacy `useColl()` behavior instead of forcing it through one hook

Legacy `useColl()` served several unrelated purposes. They should not all become synth preparation.

### Structural instrument changes

These use the same `InstrumentSetBuilder`, either during scanning when all dependencies are already known or during collection preparation when matching happens later:

- SuzukiSnes sequence-defined drum kits;
- NinSnes sequence-defined drum kits and instrument overrides;
- Akao binding of sequence instrument recipes to selected sample collections.

### Performance-dependent modulation precision

These do not change synth structure:

- KonamiSnes observed vibrato depth/rate range;
- NinSnes observed modulation range.

Keep them in the generic source-free performance flow:

1. sequence playback emits physical modulation meaning;
2. `PerformanceSequence` analysis measures observed ranges;
3. MIDI/SF2/DLS lowering applies the requested precision policy;
4. instrument assets remain immutable descriptions of driver capability.

Do not feed observed performance back through `InstrumentSetBuilder` or collection preparation.

### Sequence playback requiring read-only instrument information

SegSat and KonamiTMNT2 use legacy `useColl()` to make instrument data available while rendering a sequence. That is a sequence-runtime resource problem, not synth construction.

If a value-format port proves this is still required, design a separate narrow, typed, read-only resource seam for `SequenceVm` or the relevant playback layer. Prefer neutral instrument/region access. Do not add a generic context bag to the synth builders and do not make them own sequence playback.

## Format stress tests

### CapcomSnes: simple proof

CapcomSnes should demonstrate the ordinary path:

- one sample collection and one instrument set;
- source sample keys mapped to dense indexes;
- one region per instrument;
- source identity distinct from dense vector position;
- standard SNES sample adapter;
- automatic owners and sample links.

The final construction code should retain validation, pitch/envelope formulas, and format-specific names while removing manual sample maps, `SampleRef`, `ObjectRef`, vector-index coordination, range calculation, and routine links.

### KonamiSnes: grouping and platform quirks

KonamiSnes should prove:

- sparse melodic programs;
- entries from several disjoint instrument tables;
- `getOrAdd` merging percussion entries into one kit;
- repeated sample use;
- format-specific sample-selection fallback by BRR location;
- physical modulation specifications;
- precise source fields and parents.

The unusual sample-selection rule remains visible in format code or the SNES platform adapter. The builder must not hide it as generic lookup policy.

### NDS: many-to-many and sparse source keys

NDS should prove:

- several independent sample-collection builders for SWAR assets;
- one SBNK referencing up to four SWAR assets plus PSG/noise samples;
- concrete `SampleRef`s crossing collection boundaries;
- sparse invalid SWAR entries without index corruption;
- single-region, drum, and key-split instruments;
- absent referenced archives;
- source pointers and nested structures with explicit parentage.

NDS must not require a combined synth builder or a format-specific sample wrapper. Its module passes ordinary sample-collection handles; the scan builder owns the temporary sparse-key lookups needed across separate asset lifetimes.

### Akao and AkaoSnes: exceptional audit

AkaoSnes should prove that one durable instrument may receive source records from separate tuning and ADSR tables and that percussion entries can share an instrument.

Audit Akao PS1 against:

- several selected sample collections;
- articulation-to-sample binding;
- synthetic articulation instruments;
- collection-specific instrument materialization;
- detached `InstrumentSetBuilder` use with already-resolved `SampleRef`s.

Implement bounded general improvements exposed by the audit. Do not turn Akao's format-specific articulation catalog or binder into mandatory concepts for ordinary formats. If a full migration broadens this goal substantially, document the remaining work and prove the builder boundary with focused binder tests.

### SuzukiSnes and NinSnes: design-only preparation proof

Do not port SuzukiSnes in this goal.

Use focused framework fixtures to prove the operations its future port requires:

- read an immutable base set;
- consume a decoded sequence drum recipe;
- build a collection-specific drum kit referencing the correct base samples;
- produce two different effective sets from the same base assets for two sequences;
- leave the base set unchanged.

Use a second fixture for NinSnes-style copied instruments, explicit overrides, and drum regions cloned from base regions.

## Validation and diagnostics

Keep validation at the layer that has enough information to make the decision.

### Builder-local validation

The builders should diagnose:

- duplicate sample source keys;
- aliases to missing samples;
- duplicate instrument grouping keys passed to `add`;
- invalid concrete sample references where validity can be established locally;
- unfinished or internally inconsistent construction.

The first accepted mapping remains deterministic after an error. Do not silently append an unreachable duplicate or renumber later objects.

### Asset validation

Existing synth validation should continue to validate completed `Sample`, `Instrument`, `Region`, envelope, pan, attenuation, and identity values. Builders should not duplicate every durable-model invariant.

### Collection validation

Collection validation—not either content builder—must check across attached assets for:

- sample references to absent sample collections;
- sample indexes outside their referenced collections;
- duplicate `InstrumentIdentity` values across several instrument sets;
- conflicting explicit bank/program addresses across sets;
- ambiguous first-match behavior.

Duplicate identities should normally be errors. Some explicit address collisions may begin as warnings when intentional layering is still possible, but collection behavior must never depend silently on incidental vector order.

## Complexity budget and rejected designs

Reject any design that introduces:

- one builder that owns a combined synth;
- `InstrumentBuilder` and `RegionBuilder` as additional top-level author concepts;
- fluent setters mirroring every `SynthModel` field;
- a synth parser language or declarative table schema;
- a generic recipe or patch language for collection preparation;
- separate scan-time and preparation-time synth vocabularies;
- hidden destructor commit;
- automatic format policy based on a builder key;
- target-specific behavior in format code;
- mandatory preparation callbacks for static formats;
- a large inheritance hierarchy;
- `std::any`, a string-keyed context, or a generic property bag;
- a chain of drafts, prepared synths, overlays, and adapters that merely forward calls.

Before adding any convenience method, require either:

- repeated mechanical code in at least two migrated formats; or
- a concrete need from one of the explicit exceptional cases.

Internal machinery is justified only when it substantially reduces author-facing concepts or code. Report both framework size and the net reduction in migrated format code.

## Implementation sequence

Do not start these steps until the user separately authorizes execution of this goal.

1. Record baseline line counts and representative summary, source-map, MIDI, SF2, DLS, and WAV outputs for CapcomSnes, KonamiSnes, NDS, and relevant Akao cases.
2. Write compiling API fixtures for the common example, percussion `getOrAdd`, NDS cross-collection lookup, detached construction, and collection preparation. Use them to settle minor C++ signatures before implementation spreads.
3. Implement `SampleCollectionBuilder`, `InstrumentSetBuilder`, their nested entry proxies, stable key maps, range accumulation, diagnostics, source projection, and explicit finish.
4. Add the small `ScanResultBuilder` factories and consuming commit overloads.
5. Rename table-row source vocabulary to table-entry vocabulary where the migration touches it.
6. Adapt the SNES BRR helper to populate `SampleCollectionBuilder` and return concrete lookup results.
7. Migrate CapcomSnes and measure the real reduction. Simplify the API before adding more helpers if the result is not plainly better.
8. Migrate KonamiSnes, including percussion grouping, disjoint source tables, and its explicit sample-selection quirk.
9. Migrate NDS and prove multiple independent sample collections, sparse source keys, and scan-owned lookup lifetime separation.
10. Audit/migrate AkaoSnes and exercise detached construction through the Akao binder without redesigning Akao recipe persistence.
11. Add the small `CollectionPreparation` assembly helper and synthetic SuzukiSnes/NinSnes-style fixtures. Do not implement SuzukiSnes.
12. Extend collection-level validation for cross-set identities, addresses, and sample references.
13. Run all parity checks, report intentional corrections, and compare format code plus total framework size against the baseline.

## Required tests

Add focused tests for:

- stable dense sample indexes;
- source-key lookup and explicit aliases;
- duplicate-key diagnostics without later index corruption;
- scan-owned source-key lookup after a sample builder is committed;
- optional detached `SampleRefLookup` lifetime after its builder is finished;
- unique `add` versus intentional `getOrAdd`;
- sparse instrument grouping keys;
- pre-populated instruments and regions;
- direct region construction with concrete sample references from several collections;
- automatic region and instrument sample links regardless of call order;
- multiple source records on one sample or instrument;
- multiple source records on one region;
- source owner indexes for samples, instruments, and regions after sparse input and grouped construction;
- generic annotation fallback for source-backed objects without explicit `source()` calls;
- no fabricated annotation for genuinely derived objects without a valid source range;
- logical-object-to-source and source-to-logical-object lookup for instruments, regions, and samples;
- field ranges, decoded values, display hints, outline policy, and disjoint owned ranges sufficient for future HexView
  and TreeView projections;
- range accumulation across disjoint entries;
- `value()` escape-hatch behavior;
- detached construction with and without source-map output;
- scan-result consuming commit;
- immutable collection preparation from base values;
- two sequences deriving different instrument sets from one base;
- multiple derived instrument/sample sets;
- collection-level identity, address, and sample-reference conflicts;
- composition with generic observed modulation analysis;
- malformed and partial input diagnostics.

Run all value-core and affected format tests. Verify representative summary, source-map, MIDI, sequence-event-simulation, SF2, DLS, and WAV parity. Document every intentional output difference as a correction.

## Deliverables

- The two shared asset-content builders and nested entry proxies.
- Small scan-time and collection-preparation assembly adapters.
- A builder-backed SNES BRR adapter.
- Migrated CapcomSnes, KonamiSnes, and NDS synth construction.
- A bounded Akao/AkaoSnes audit and detached-builder proof.
- Collection-level validation for multi-set conflicts and sample references.
- Table-entry terminology replacing ambiguous row terminology in touched code.
- API documentation and before/after format examples.
- Concise plain-language comments for non-obvious lifecycle, indexing, range, and provenance behavior.
- Baseline/parity results and net line/complexity measurements.
- A future migration guide for SuzukiSnes, NinSnes, and other legacy `useColl()` formats.

## Acceptance criteria

The goal is complete when:

- a normal format author needs only `RecordReader`, ordinary synth values, and the two content builders;
- normal code reads locally as source data becoming `Sample`, `Instrument`, and `Region` values;
- no author manually synchronizes source keys, dense indexes, `SampleRef`s, `ObjectRef`s, vectors, and source links;
- `Sample`, `Instrument`, and `Region` remain the one durable representation of synth content;
- builder grouping keys never silently become identities or export addresses;
- multiple instrument sets and sample collections require no special synth-builder mode;
- NDS sparse/multi-collection references remain correct;
- Konami percussion grouping and sample quirks remain visible and correct;
- Akao can use the detached builder after format-specific binding without bloating the common path;
- collection-dependent structural changes produce immutable derived values through the same builder vocabulary;
- static formats have no preparation lifecycle;
- observed modulation scaling remains generic performance analysis;
- sequence playback resource needs remain separate from synth construction;
- SuzukiSnes has not been implemented, but its future drum-kit case is proven by focused immutable-preparation tests;
- source-map detail and diagnostics are preserved or improved;
- every source-backed synth object can be found from its logical object reference and every owned synth annotation can
  be resolved back to its logical object without interpreting a format-specific key;
- the retained asset and source-map data can drive both a logical synth tree and a source-oriented outline, including
  exact HexView highlighting for encoded fields and all disjoint records of one object;
- migrated format construction is materially shorter and easier to understand;
- total machinery is justified by a net reduction in code and mental load;
- all relevant tests and parity checks pass.
