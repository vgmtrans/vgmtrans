# Asset dependencies and collection preparation

A format describes what each asset needs when it scans that asset. The core
expands those dependencies into a collection, records the selected relationships,
and prepares private copies for playback or export.

`FormatModule` only registers scanning, accepted source formats, and sample-filter
preferences. It has no collection-resolution or binding callback.

## Writing a format

Use a direct reference when the scanner already knows the provider:

```cpp
auto samples = result.samplePool("Samples");
auto bank = result.soundBank("Bank");
bank.useSamples(samples);
sequence.useBank(bank);
```

Local samples and already-resolved `SampleRef`s need no deferred dependency.
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
an intentional ordered group. A request can instead build a `DependencySelection`
with custom diagnostics, a prioritized subset, or per-provider placement data.
For example, Sony PS1 records where a bank's sample table begins inside a pool;
Akao selects enough pools to cover its required playable articulations.

Selection only examines immutable assets. It must not mutate format state,
construct collections, or capture borrowed pointers in its result. Asset IDs and
`AssetPrivateData` placements survive the temporary catalog.

Preparation runs after selection, on a private bank copy:

```cpp
void prepareBank(BankPreparationContext& context, const BankData& layout) {
  for (const auto& input : context.samples<SampleData>()) {
    // input.asset, input.data, and input.placement belong to this bank's inputs.
    // Resolve native sample indexes into context.bank's regions.
  }
}
```

A bank's hook belongs to the bank, so a Konami sequence can use a Sony bank
without knowing how Sony prepares samples. `bankIndex` gives its ordinal among
selected banks of the same format. Sequence preparation runs afterward and may
configure its private runtime from the prepared banks. SegSat also uses this
step for sequence-specific logical bank addressing. Runtime replacement must
keep the same executor family.

## Core policy

- `useBank` and `useBanks` make a sequence an automatic collection root. Its key
  uses the sequence's stable asset ID; adding or removing a matching provider
  updates the existing collection. Missing providers leave an incomplete root.
- Banks and sample pools remain assets without generating synthetic collections.
  `bindSoundBank` resolves and prepares a standalone bank's own dependencies.
- Existing scanner-supplied collections remain supported. Their members seed
  dependency expansion, and they suppress duplicate automatic roots for the
  same sequence.
- Each bank retains its own ordered dependency targets. Flattened collection
  membership deduplicates assets without discarding relationships or placements.
- Automatic candidates cannot cross independent container roots. Standalone
  files remain available to format-specific ID, path, and compatibility rules.
  Exact references supplied by a scanner are authoritative.
- Manual collections override the sequence's bank requests. Bank requests run
  within the manually selected sample pools, in user-selected order. They may
  intentionally cross container boundaries but cannot add an unselected asset.
- Selection failures and cycles become issues on the affected collection.
  Missing or ambiguous providers are inspectable; export may still work for
  requests such as MIDI without a bank. Preparation and sample validation decide
  whether the requested collection can actually be bound.
- Preparation validates membership, recorded relationships, bank identity, sample
  references, and resulting synth data. Failures publish no partially prepared
  collection. Durable assets and previous snapshots remain unchanged.

The resolver rebuilds decisions from the current immutable catalog after session
changes. It does not retain a mutable matching database or search globally for a
combination of providers. Driver-specific choices stay in small format requests.

Standalone full-bank export uses the bank's own recipe. Exporting only instruments
used by a sequence requires one unambiguous collection; exporting a particular
collection also applies its sequence-specific runtime and instrument behavior.

## Regression coverage

Core tests exercise direct and deferred dependencies, shared providers with
different positions, manual candidate ordering and confinement, container scope,
cycles and exceptions, standalone preparation, and copy isolation. Format tests
cover native matching and runtime semantics, including Sony sample positions,
Akao articulation coverage, PSF2 manifests, and multibank addressing.

Migration verification used all 43 headless CTest targets and 49 real files from
13 corpus groups spanning the six migrated formats. All 157 discovered
collections retained the same banks, sample pools, and resolved sample references;
manual selections agreed with automatic resolution. The 157 MIDI exports and 13
each of SoundFont and DLS exports were byte-identical to the previous architecture.
The corpus sample included shared Tamsoft banks, multibank Konami sequences,
FFVIII articulation supplementation, and PSF2 files with unrelated member names.
