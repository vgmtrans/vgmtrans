# Value Collection Matching Design

## Context

The value-oriented engine keeps scanners and collection resolution separate:

- Format scanners emit immutable assets plus lightweight match facts.
- Collection resolvers are pure functions over the current session snapshot.
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

- A sequence's `seq_id` matches an instrument set id.
- A sequence's file id, when present, names its preferred sample set.
- Instrument regions reference articulation ids.
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
  reconcile by stable keys.
- Make resolvers readable and testable without mutable root callbacks.
- Keep fact payloads format-owned when the rule is format-specific.
- Provide reusable indexing, asset typing, and collection assembly utilities.
- Make incomplete and ambiguous collections explicit in `DesiredCollection`.
- Preserve deterministic output independent of scan order, except where a
  format intentionally models "most recent" or source-local behavior.

## New Resolver Authoring Surface

`CollectionResolver` grows from a single helper into a small resolver toolkit:

- `MatchFactIndex` wraps `MatchContext` and provides typed fact iteration.
- `AssetMatchView<T>` exposes typed fact and asset references plus the optional
  source for one fact.
- `fieldValue()` and numeric field helpers read `FormatSpecificFact` fields
  without ad hoc loops.
- `CollectionAssembly` wraps `DesiredCollection` mutation, status, issues, and
  duplicate suppression.
- `CollectionAssembly::requireSequence()`, `requireInstrumentSet()`, and
  `requireSampleCollection()` apply common missing-role issue rules.

The existing `CollectionMemberFact` helper remains for simple formats. More
complex formats can emit format-specific facts and still build collections with
shared utilities.

## Akao Fact Vocabulary

Akao scanners emit these format-specific facts:

- `akao.sequence`: sequence id, preferred sample-set id, version, and source
  sequence offset.
- `akao.instrument-set`: instrument-set id.
- `akao.sample-collection`: sample-set id, first articulation id, articulation
  count, and scan ordinal.
- `akao.required-articulation`: one fact per non-zero articulation id referenced
  by an instrument set.

The resolver then:

1. Indexes sequences by `seq_id`, instrument sets by id, samples by coverage,
   and required articulation ids by instrument set asset.
2. Creates one desired collection per sequence that has a matching instrument
   set.
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

## PSF1 Extraction

The value extractor must support PSF1 in addition to 2SF/NCSF. PSF1 executable
payloads use the load address at decompressed offset `0x18`, and executable data
starts at offset `0x800`. Library overlay order remains `_lib`, current file,
then `_lib2` and higher, matching legacy behavior.

## Non-Goals

This design does not move user-created collections into the resolver layer and
does not add mutable matcher callbacks to the value engine. It also does not
make every format use `FormatSpecificFact`; scanner-known collections should
continue using explicit collections or `CollectionMemberFact` when that is the
clearest expression.
