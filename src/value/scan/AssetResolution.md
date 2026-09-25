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

## Bank assignments and preparation

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

Some sequences also assign logical meanings to their selected banks. Register
`sequence.assignBanks(assignBanks)` for that case. It runs after automatic or
manual selection and can attach native placement values to the sequence-to-bank
relationships. It cannot change membership or mutate bank assets:

```cpp
void assignBanks(BankAssignmentContext& context) {
  const auto& sequence = context.data<SequenceData>();
  for (auto& bank : context.banks<BankData>()) {
    bank.placement = AssetPrivateData::make(logicalUse(sequence, bank.data));
  }
}
```

SegSat uses this step to reserve exact physical bank matches before assigning
fallback logical roles. Each bank applies its assignment through
`BankPreparationContext::placement`. Sequence preparation runs afterward with
read-only prepared banks. Like `samples<Data>()`, `banks<Data>(format)` presents
each input's `asset`, retained `data`, and `placement` together:

```cpp
SequenceRuntime prepareSequence(SequencePreparationContext& context) {
  RuntimeConfig config;
  for (const auto& bank : context.banks<BankData>(kFormatName)) {
    appendPrograms(config, bank.data);
  }
  return sequenceRuntime(std::move(config));
}
```

Bank views preserve selected order and skip other formats. A matching bank with
missing retained data fails preparation; it is not silently skipped. A placement
is empty when no sequence assignment exists. `context.sequence` is always a
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
bank. Standalone bank preparation has no sequence assignment.

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
- Each owner has one ordered input list for its dependency role. Multiple requests
  combine into that list, preserving unresolved outcomes and alternatives. Bank
  inputs deduplicate assets; sample inputs preserve distinct placements within
  the same pool. Flattened collection membership deduplicates both.
- Bank assignment writes directly to the recorded inputs. Preparation borrows
  those same lists from the collection; it does not reconstruct relationships.
- Automatic candidates cannot cross independent container roots. Standalone
  files remain available to native ID, path, and compatibility rules. Exact
  references supplied by a scanner are authoritative.
- Manual collections replace the sequence's bank requests with the chosen banks.
  Bank sample requests run within the selected pools in user order. Manual
  selections may cross container boundaries but cannot add unselected providers.
  Bank assignments apply after this override.
- Wrong-type providers, selector exceptions, and assignment exceptions produce
  failed dependencies and block preparation. Missing or ambiguous requests remain
  inspectable; preparation and sample validation determine whether the selected
  assets can be used. MIDI can still be useful without a bank.
- Preparation validates recorded relationships, bank identity, sample references,
  and resulting synth data. Failures publish no partially prepared collection.
  Durable assets and previous snapshots remain unchanged.

The resolver rebuilds decisions from the current immutable catalog after session
changes. It retains no mutable matching database and does not search globally for
a combination of providers. Driver-specific choices stay in format requests.

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
Shared-bank tests cover different logical assignments across
sequences, overlapping requests, assignment failures, and assignments to a manually
substituted bank. Format tests cover
native matching, sample positions, Akao coverage, PSF2 manifests, and SegSat shared
sequence entries, logical addressing, and velocity behavior.

The refined implementation passes all 43 headless CTest targets. Corpus
verification covers 56 files across 17 groups, including the original six formats
and additional SegSat, NDS, MP2k, and Namco SNES archives. All 430 collections
retain the same banks, pools, and resolved sample references; manual selections
agree with automatic resolution. All 464 generated artifacts are byte-identical:
430 MIDI files and 17 each of SoundFont and DLS. The sampled MP2k group reports
pre-existing sample-reference errors in both builds, with matching artifacts and
exit status; the other 16 groups export without a nonzero exit status.
