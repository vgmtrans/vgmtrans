# Value Collection Matching Design

## Context

The value-oriented engine keeps scanners and collection resolution separate:

- Format scanners emit immutable assets plus lightweight match facts.
- Collection resolvers are pure functions over the current session snapshot.
- Collection materializers may build derived assets from resolved membership.
- `CollectionStore` reconciles resolver output by stable `CollectionKey`.
- If a resolver fails, the session keeps the last resolved collections visible.

This lifecycle is a good fit and should remain the ownership boundary. The weak
point is the resolver authoring surface. Today the only shared resolver helper is
`resolveCollectionMemberFacts()`, which groups explicit `CollectionMemberFact`s
by one common key. That is ideal when a scanner already knows the full
collection, but it is too narrow for formats whose assets only publish partial
relationships.

## Legacy Matcher Behavior

The legacy matcher system is event-driven. `VGMRoot` notifies a format matcher
when each `VGMSeq`, `VGMInstrSet`, or `VGMSampColl` appears or disappears.
Matchers keep mutable caches and create `VGMColl`s when enough files are
present.

Common matchers cover simple cases:

- `GetIdMatcher` pairs sequence/instrument/sample assets by `VGMFile::id()`.
- `FilenameMatcher` pairs by source path.
- `FilegroupMatcher` waits until a scan finishes, then pairs assets by source
  grouping, offset order, and sample-collection viability.

`AkaoMatcher` needs more structure:

- A sequence's file id, when present, names its preferred sample set.
- Sequence-side instrument/drum regions reference articulation ids.
- Sample collections cover articulation-id ranges.
- A collection may need multiple sample collections to cover all used
  articulations.
- PSF files may omit useful sample-set ids, so sample matching must be lenient
  there.
- Sample collections are attached in ascending articulation-id order, even
  though matching prefers recently scanned collections first.

Those rules are not simply "same id means same collection"; they are a small
declarative matching problem over facts.

## Design Goals

The value matcher layer should:

- Keep the session lifecycle unchanged: scan facts, resolve from snapshots,
  materialize derived assets, then reconcile by stable keys.
- Make resolvers readable and testable without mutable root callbacks.
- Use shared typed facts for common relationships and keep fact payloads
  format-owned only when the rule is genuinely format-specific.
- Provide reusable indexing, asset typing, and collection assembly utilities.
- Make incomplete and ambiguous collections explicit in `DesiredCollection`.
- Keep membership resolution separate from collection-dependent asset
  construction.
- Preserve deterministic output independent of scan order, except where a
  format intentionally models "most recent" or source-local behavior.

## New Resolver Authoring Surface

`CollectionResolver` grows from a single helper into a small resolver toolkit:

- `MatchFactIndex` wraps `MatchContext` and provides typed fact iteration.
- `AssetMatchView<T>` exposes typed fact and asset references plus the optional
  source for one fact.
- Shared typed facts cover common matcher vocabulary:
  - `IdMatchFact` for ids in a named domain.
  - `OffsetOrderFact` for deterministic source/order preferences.
  - `SampleCoverageFact` for sample/articulation coverage in a named domain.
  - `SampleRequirementFact` for sample/articulation requirements in a named
    domain.
- `fieldValue()` and numeric field helpers read `FormatSpecificFact` fields
  without ad hoc loops.
- `CollectionAssembly` wraps `DesiredCollection` mutation, status, issues, and
  duplicate suppression.
- `CollectionAssembly::requireSequence()`, `requireInstrumentSet()`, and
  `requireSampleCollection()` apply common missing-role issue rules.

The existing `CollectionMemberFact` helper remains for simple formats. More
complex formats can combine typed facts, narrowly scoped format facts, and shared
collection assembly utilities.

## Materialization Phase

Resolution answers only this question:

> Which scanned sequence, instrument set, sample collections, and misc assets
> belong together?

Some formats also need a second answer:

> Given those selected members, what derived assets should this collection
> expose?

That is the job of `FormatModule::materializeCollection`. The session passes a
`MaterializationContext` containing the immutable sources, the current snapshot,
the resolver's `DesiredCollection`, the session id allocator, and a stable
`assetIdForSlot()` callback. A materializer returns:

- the final `DesiredCollection`;
- zero or more `MaterializedAsset`s keyed by small slot names;
- optional diagnostics.

The slot name is collection-local. The session combines resolver id, collection
key, and slot to keep materialized asset ids stable across rescans. If a
materializer stops returning a slot, the stale materialized asset is removed.

Materializers should not perform matching. They should consume the resolver's
selected members and immutable source bytes, then build collection-dependent
assets. This keeps scan order out of derived asset contents.

## Akao Fact Vocabulary

Akao scanners emit generic facts in Akao domains:

- `IdMatchFact("akao.sequence-id")` on sequences.
- `IdMatchFact("akao.sample-set")` on sequences and sample collections when a
  sample-set id exists.
- `OffsetOrderFact` on sequences and sample collections.
- `SampleCoverageFact("akao.articulation")` on sample collections, with the
  covered articulation range.
- `SampleRequirementFact("akao.articulation")` on sequences, with the
  non-zero articulation ids required by sequence-side instrument tables.

The resolver then:

1. Indexes sequences by `seq_id`, samples by coverage, and required
   articulation ids by sequence asset.
2. Creates one desired collection per sequence.
3. Prefers the sequence's named sample set when present.
4. Adds recently scanned sample collections that cover remaining required
   articulations.
5. Includes the preferred sample set even when no region references it, matching
   FF8-style key-split behavior. Missing or zero sample-set ids are treated as
   the same anonymous sample set to preserve PSF rip behavior.
6. Sorts attached sample collections by articulation range before publishing.
7. Marks collections incomplete when required roles are missing or required
   articulations remain uncovered.

The collection key is `Akao:seq:<id>:source:<source>:offset:<offset>`. It is
stable for rescans, allows different PSF tracks with the same sequence id to
coexist, and avoids the legacy matcher's destructive "consume sequence after
match" behavior.

## Akao Format Module Shape

Akao should stay split by the format concepts rather than by export target:

- `AkaoModule` owns scan orchestration: it finds sequence and sample headers,
  reserves assets, and emits only scanned assets plus facts needed by the
  resolver. It does not bind instruments to whatever samples happened to be
  available in the same scan.
- `AkaoResolver` owns collection assembly. It chooses sample collections by
  preferred sample-set id plus sequence-required articulation coverage, and
  reports missing coverage as collection issues.
- `AkaoResolver` also owns collection materialization. After resolution, it
  re-reads the resolved sequence and selected sample collections from source
  bytes, builds an articulation map, and emits a stable bound instrument-set
  asset for the collection.
- `AkaoVersion` contains source-name and header heuristics.
- `AkaoBytecode` contains opcode sizes, branch targets, and track analysis.
- `AkaoSequenceDecoder` converts bytecode into the shared sequence VM commands.
- `AkaoSynth` parses articulation tables and PSX ADPCM sample data.
- `AkaoInstrumentSet` turns sequence-side instrument/drum tables plus resolved
  articulations into value-model instruments and regions.

This keeps parity quirks local to the format concept that owns them. For example,
version 3 drum tables inherit ADSR override bytes from the start of the drum
table, so that rule belongs in `AkaoInstrumentSet`, not in SF2/DLS exporters.
Akao absolute tuning preserves legacy pitch-bend quantization in
`AkaoSequenceDecoder`; the shared MIDI renderer keeps its normal rounding
behavior for other formats.

## Synth Loop Ownership

Akao exposed a value-model gap: some loop points belong to articulations/regions,
not globally to a decoded sample. The synth model therefore allows `Region::loop`
to override `Sample::loop`. Exporters compute an effective loop as
`region.loop.value_or(sample.loop)`.

For Akao specifically:

- PSX ADPCM loop status is still stored on `Sample::loop`.
- Articulation loop points are copied to `Region::loop` only when legacy PSX
  sample-loop priority would use region data.
- Non-playing loop offsets may remain on `Sample::loop` with `enabled=false`,
  matching legacy SF2 sample headers while keeping `sampleModes` off.

This avoids Akao-specific branches in the exporters and gives future formats a
clear place to represent region-local loop metadata.

## PSF1 Extraction

The value extractor must support PSF1 in addition to 2SF/NCSF. PSF1 executable
payloads use the load address at decompressed offset `0x18`, and executable data
starts at offset `0x800`. Library overlay order remains `_lib`, current file,
then `_lib2` and higher, matching legacy behavior.

## Non-Goals

This design does not move user-created collections into the resolver layer and
does not add mutable matcher callbacks to the value engine. It also does not
make every format use matcher facts; scanner-known collections should continue
using explicit collections or `CollectionMemberFact` when that is the clearest
expression. Materialization is also not a general cache for expensive parse
results; it exists for assets whose value depends on the resolved collection
membership.
