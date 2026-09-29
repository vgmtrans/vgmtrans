# Asset dependencies and collection preparation

A format describes what each asset needs when it scans that asset. The core
resolves sequences to banks, then banks to sample pools, and prepares private
copies for playback or export. These two supported relationships are represented
by separate `SequenceRecipe` and `BankRecipe` types. There is no general dependency
graph, bank-to-bank traversal, or format-level collection callback.

## Writing a format

Use a direct reference when the scanner already knows the provider:

```cpp
auto samples = result.samplePool("Samples");
auto bank = result.soundBank("Bank");
bank.useSamples(samples);
sequence.useBank(bank);
```

Direct references are stored as values. Local samples and already-resolved
`SampleRef`s need no additional declaration: the core discovers their external
pool dependencies from the bank's regions.

Every published sequence produces a discovered collection, even without bank
requests. The sequence's `AssetId` determines its stable identity; its name is
the default display name. Formats do not supply collection keys or resolver
namespaces. `useBank` and `useBanks` only declare dependencies.

Use `sequence.collectionName(name)` for a naming override and
`sequence.includeMisc(table)` for supplemental inspection assets. A loose or
helper sequence can opt out with `sequence.withoutCollection()` while remaining
available for direct sequence export. Naming, supplemental assets, and bank
requests do not undo that opt-out. User-created collections remain a separate
session operation with explicitly chosen members and independent identities.

For assets that arrive separately, use a small callable request with owned values:

```cpp
struct BankId {
  u16 value;

  DependencySelection operator()(const DependencyContext& context) const {
    auto candidates = context.candidates<SoundBankAsset, BankData>();
    std::erase_if(candidates, [&](const auto& bank) { return bank.data->id != value; });
    return selectOne(candidates);
  }
};

sequence.useBanks(BankId{header.bankId});
bank.data(layout).useSamples(selectSamples).prepare<BankData>(prepareBank);
```

`selectOne` records an unresolved choice when candidates tie. `selectAll` records
an intentional ordered group. Both accept asset views or `DependencyTarget` values
with native placement data. Sony PS1 uses the latter to retain the possible
starting positions of a bank's sample table within one or more pools.

`bestMatches(context.candidates<SoundBankAsset, BankData>(), score)` returns
copies of the highest-scoring candidate views in input order. Negative scores
reject candidates; zero is a valid fallback. These small views borrow the
catalog's assets, data, and sources, so temporary candidate lists are safe and
matching can compose directly with `selectOne` or `selectAll`.

Source locations have separate host and member paths. `SourceFile::logicalPath()`
uses the typed `memberPath` for archive members, otherwise the host `path`, then
the display `name` when no path is available. It normalizes lexically without
filesystem access; transformed sources such as PSF RAM keep their host location.
`sourcePath` and `sourceDirectory` accept nullable source pointers. `sameDirectory`,
`sameStem`, and `sameContainer` expose relationships without assigning affinity
scores. Member-path comparisons are scoped to their immediate container.

Formats retain their own priorities and fallbacks. SonyPS1 combines directory
and stem matches; SonyPS2 also distinguishes shared stems across directories in
one container. SquarePS2 explicitly compares host paths after source and parent
identity. Tamsoft retains its case folding, directory fallback, and global BGM
bank rule. SonyPS2's device prefixes and ISO filename suffixes remain format rules.
PSF2 publishes `memberPath` like other archives; no string attribute is needed.

A selection has a typed `ResolutionStatus`: `Resolved`, `Incomplete`, `Ambiguous`,
or `Failed`. Empty selections are incomplete. `incomplete(message)` records
missing coverage, including when some useful providers were selected.
`ambiguous(alternatives, message)` retains complete alternatives and any explicitly
selected fallback. `DependencySelection::failed(message)` reports a fatal request
failure. Diagnostics explain these outcomes; their code strings do not control
export. Akao's coverage selection and Tamsoft's first-choice fallback retain their
native policies through these operations.

Each resolved relationship owns its status. `Collection::resolutionStatus()`
derives the summary using the precedence `Failed > Ambiguous > Incomplete > Resolved`;
export checks that same summary before preparation. `CollectionIssue` contains only
diagnostic information: changing a message, code, or severity, or removing the
diagnostic, cannot change the outcome. A resolved collection can carry warnings;
an incomplete or ambiguous collection may still have usable selected inputs.
Resolution status describes matching, not whether later preparation or rendering
will succeed.

Selection examines immutable assets. It must not mutate format state, construct
collections, or capture borrowed pointers in its result. Asset IDs and owned
`AssetPrivateData` placements survive the temporary catalog.

## Preparation

A bank's preparation hook receives its selected sample inputs and a private bank
copy:

```cpp
void prepareBank(BankPreparationContext& context, const BankData& layout) {
  for (const auto& input : context.samples<SampleData>()) {
    // Resolve native sample indexes using input.asset, input.data,
    // and input.placement, updating context.bank's regions.
  }
}
```

The hook belongs to the bank, so a Konami sequence can use a Sony bank without
knowing how Sony prepares samples. `bankIndex` gives the bank's ordinal among
selected banks of the same format. A shared sample pool can have different
placements in each bank's inputs.

When a bank needs exactly one pool, `context.sample<SampleData>()` returns that
input directly. It fails if the selection is empty, has multiple inputs, or lacks
the requested data. An optional message describes the format's single-input
requirement.

Sequence preparation runs after the selected banks have prepared their samples.
It receives private, mutable bank contents and their retained format data, and
can configure those contents together with the sequence runtime:

```cpp
std::optional<SequenceRuntime> prepareSequence(SequencePreparationContext& context) {
  RuntimeConfig config;
  for (const auto& bank : context.banks<BankData>(kFormatName)) {
    appendPrograms(config, bank.data);
    // bank.asset is this collection's prepared copy. Configure its instrument
    // identities or preferred output addresses here when the sequence requires it.
  }
  return sequenceRuntime(std::move(config));
}
```

SegSat reserves exact physical bank matches before assigning fallback logical
numbers in selected order. One callback applies those numbers to instrument
identities and playback velocity data, and gives a lone bank output bank zero.
No assignment callback or stored sequence-to-bank placement is involved. A bank
count mismatch is reported during preparation; collection discovery reports
companion-selection problems only.

Bank views preserve selected order and skip other formats. A matching bank with
missing retained data fails preparation; it is not silently skipped. `context.sequence` is always a
reference to the sequence being prepared. Both preparation contexts default
diagnostics to their owner's source range. Supplemental assets remain available
through collection inspection, outside the audio preparation interface.

The core validates and installs the returned runtime; both the original and the
replacement must have an executor from the same family. A hook that sometimes
leaves the runtime unchanged returns `std::optional<SequenceRuntime>`, using
`std::nullopt` for that case.

In either preparation context, `warning(message, range)` records a diagnostic and
continues. `fail(message, range)` stops preparation immediately, including any
calling helpers and later hooks. Typed input validation uses the same failure
path. Formats do not need to propagate failure flags or write `return` after
`fail()`. The core catches the failure at the collection boundary, preserves
earlier warnings, reports the error once, and discards partial changes.

Different sequences can assign different logical addresses to the same durable
bank. Standalone bank preparation runs only the bank hook and keeps its native instrument mapping.

## Stored collection inputs

`Collection::selection` retains the original choices. For an automatic collection,
these are the sequence and supplemental inspection assets. Manual collections also
retain the user's ordered bank and sample choices, including unused sample pools.

`Collection::inputs.banks` holds the resolved banks in order. Each `CollectionBank`
contains its bank ID and `samples`: chosen pool uses, their placements, status,
and alternatives. Bank-selection status and alternatives belong to `CollectionInputs`.
IDs refer to assets in the same immutable session snapshot as the collection.

For example, a user can select pools A and B while one bank uses A twice at
different offsets. The selection retains `[A, B]`; the bank's sample targets retain
`[A at offset 4, A at offset 12]`. Preparation visits those targets directly.
`members()` derives the flat inspection/export view, retaining the user's order
and adding automatic uses once each. There is no stored owner/role list or second
resolved member list to reconcile.

## Core policy

- Adding or removing providers updates the same sequence collection. Missing
  providers leave an incomplete root. Renaming or reordering discovery results
  preserves collection IDs; removing a sequence removes its discovered collection.
- Scanners publish shared physical sequences once. SegSat deduplicates aliases
  across song tables before publication, preserving the first entry's name.
- Banks and sample pools remain assets without generating synthetic collections.
  `bindSoundBank` resolves and prepares a standalone bank's own dependencies.
- Supplemental inspection references have their own recorded outcome. Missing or
  wrong-type references are omitted from membership and make the collection
  incomplete, preserving audio preparation despite their error diagnostics.
- Multiple requests combine into one ordered selection, preserving unresolved
  outcomes and alternatives. Bank inputs deduplicate assets; a bank's sample uses
  retain distinct placements within the same pool. `members()` deduplicates uses
  for display, without changing the stored relationships.
- Preparation reads each bank's sample inputs directly. It does not join owner IDs
  or roles to a separate member list. Resolved provider IDs and manual restrictions
  are validated during resolution; snapshot lookups still reject missing or
  wrong-type assets before callbacks run.
- Automatic candidates cannot cross independent container roots. Standalone
  files remain available to native ID, path, and compatibility rules. Exact
  references supplied by a scanner are authoritative.
- Manual collections replace the sequence's bank requests with the chosen banks.
  Bank sample requests run within the selected pools in user order. Manual
  selections may cross container boundaries but cannot add unselected providers.
- Wrong-type providers and selector exceptions produce failed selections and
  block preparation. Missing or ambiguous requests remain
  inspectable; preparation and sample validation determine whether the selected
  assets can be used. MIDI can still be useful without a bank.
- Preparation validates bank identity, sample references,
  and resulting synth data. Failures publish no partially prepared collection.
  Durable assets and previous snapshots remain unchanged.

The resolver rebuilds automatic collections from the current immutable catalog
after session changes. User collections retain their manual resolution; they are
not automatically rematched when new files arrive. The resolver retains no mutable
matching database and does not search globally for a combination of providers.
Driver-specific choices stay in format requests.

Standalone full-bank export uses the bank's own recipe. Exporting only instruments
used by a sequence requires one unambiguous collection; exporting a particular
collection also applies its sequence-specific runtime and instrument behavior.

## Regression coverage

Core tests exercise automatic publication, persistent opt-outs, identity across
renaming and reordering, direct and deferred requests, typed failure status independent
of diagnostic codes, ambiguous positions within one pool, manual ordering and
confinement, container scope, provider type validation, standalone preparation,
and copy isolation. Preparation tests cover immediate failure across nested
helpers, single-input validation, and returned-runtime validation and retention.
Shared-bank tests cover sequence-specific configuration, overlapping requests,
failure after editing private bank contents, and manually substituted banks.
They also cover unused manual sample choices and repeated placements without
duplicating automatic membership. Format tests cover
native matching, sample positions, Akao coverage, PSF2 manifests, and SegSat shared
sequence entries, logical addressing, and velocity behavior.

The collection-input revision passes all 43 headless CTest targets. Current
verification is recorded in [the implementation notes](../../../docs/value-core-review/resolved-instrument-prototype.html#verification).

Historical corpus verification of the earlier dependency implementation covered 56 files across 17 groups, including the original six formats
and additional SegSat, NDS, MP2k, and Namco SNES archives. All 430 collections
retained the same banks, pools, and resolved sample references; manual selections
agreed with automatic resolution. All 464 generated artifacts were byte-identical:
430 MIDI files and 17 each of SoundFont and DLS. The sampled MP2k group reports
pre-existing sample-reference errors in both builds, with matching artifacts and
exit status; the other 16 groups export without a nonzero exit status.
