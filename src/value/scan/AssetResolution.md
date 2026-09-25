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

`useBank` and `useBanks` opt the sequence into an automatic collection. Use
`sequence.collection()` to publish a sequence even when it has no banks, and
`sequence.includeMisc(table)` to attach supplemental inspection assets. Collection
publication is an explicit optional descriptor on the sequence, independent of
its dependency requests. The default name comes from the sequence; its stable
identity comes from its asset ID. `collection(key, name)` can override either.
All discovered collections use these declarations. User-created collections
remain a separate session operation with explicitly chosen members.

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

A selection has a typed `ResolutionStatus`: `Resolved`, `Incomplete`, `Ambiguous`,
or `Failed`. Empty selections are incomplete. `incomplete(message)` records
missing coverage, including when some useful providers were selected.
`ambiguous(alternatives, message)` retains complete alternatives and any explicitly
selected fallback. `DependencySelection::failed(message)` reports a fatal request
failure. Diagnostics explain these outcomes; their code strings do not control
export. Akao's coverage selection and Tamsoft's first-choice fallback retain their
native policies through these operations.

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
read-only banks; it can read the same assignment with `bankPlacement<T>(id)` and
configure its private runtime. Runtime replacement must retain the executor family.
Different sequences can assign different logical addresses to the same durable
bank. Standalone bank preparation has no sequence assignment.

## Core policy

- Adding or removing providers updates the same sequence collection. Missing
  providers leave an incomplete root.
- Banks and sample pools remain assets without generating synthetic collections.
  `bindSoundBank` resolves and prepares a standalone bank's own dependencies.
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

Core tests exercise direct and deferred requests, typed failure status independent
of diagnostic codes, ambiguous positions within one pool, manual ordering and
confinement, container scope, provider type validation, standalone preparation,
and copy isolation. Shared-bank tests cover different logical assignments across
sequences, overlapping requests, assignment failures, and assignments to a manually
substituted bank. Format tests cover
native matching, sample positions, Akao coverage, PSF2 manifests, and SegSat logical
addressing and velocity behavior.

The refined implementation passes all 43 headless CTest targets. Corpus
verification covers 56 files across 17 groups, including the original six formats
and additional SegSat, NDS, MP2k, and Namco SNES archives. All 430 collections
retain the same banks, pools, and resolved sample references; manual selections
agree with automatic resolution. All 464 generated artifacts are byte-identical:
430 MIDI files and 17 each of SoundFont and DLS. The sampled MP2k group reports
pre-existing sample-reference errors in both builds, with matching artifacts and
exit status; the other 16 groups export without a nonzero exit status.
