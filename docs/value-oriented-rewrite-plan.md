# Value-Oriented Rewrite Plan

This document describes how to evolve the value-oriented code in `src/value` into
the main VGMTrans architecture. The immediate target is a complete, parity-tested
CapcomSnes implementation that can export MIDI, SF2, DLS, and WAV through the new
pipeline. The design should also leave a clean path to future tracker exports
without requiring format modules to know which export format the user selected.

## Design Goals

The rewrite is not just a mechanical conversion from classes to structs. The goal
is to make VGMTrans easier to extend while preserving the precision of the legacy
model.

The core goals are:

- Keep parsed data as plain value types with explicit ownership.
- Keep format modules small, readable, and source-oriented.
- Keep conversion behavior accurate enough for byte-level or normalized parity
  testing against the legacy implementation.
- Keep export decisions out of format modules.
- Make intermediate models honest about their abstraction level.
- Preserve source ranges and diagnostics throughout the pipeline.
- Avoid clever framework code that makes each format harder to read.

The non-goals are:

- Do not add tracker export yet.
- Do not introduce a control-flow graph as part of this phase.
- Do not build a generic music theory model that hides format-specific behavior.
- Do not force every format to implement a large visitor or inheritance hierarchy.

## Naming

Use names that state the model's abstraction level.

- `CommandSequence`: parsed source-semantic sequence commands.
- `MidiSequence`: lowered MIDI-oriented performance events.
- `MidiSequenceBuilder`: lowers `CommandSequence` to `MidiSequence`.
- `MidiSequenceProfile`: format-specific sequence interpretation used during MIDI
  lowering.
- `TrackerSequence`: future tracker/pattern-oriented lowered representation.
- `InstrumentSetAsset`: parsed instrument data owned by a project.
- `InstrumentSet` or `SynthSet`: future resolved/export-stage synth view if the
  current `InstrumentSetAsset` needs a distinct lowered counterpart.

Avoid `EventSequence`, `PerformanceTimeline`, `PatternProject`, and `SynthBank`.
Avoid `Set` prefixes in command names. Prefer `VolumeCommand`,
`VibratoCommand`, `TremoloCommand`, `ProgramCommand`, and similar names.

## Target Architecture

The value pipeline should be:

```text
SourceStore
  -> FormatModule scan
  -> Project
      -> Collection
      -> SequenceAsset(CommandSequence)
      -> InstrumentSetAsset
      -> SampleCollectionAsset
  -> ExportService
      -> CommandSequence + MidiSequenceProfile -> MidiSequence -> MidiExporter
      -> InstrumentSetAsset + SampleCollectionAsset -> Synth export plan -> SF2/DLS
      -> SampleCollectionAsset -> WAV
      -> future CommandSequence/TrackerSequence path -> tracker exporter
```

The important boundary is that `FormatModule` produces parsed assets, not export
artifacts. Export services decide how to lower those assets for the selected
output.

## Core Layer

`src/value/core` should contain shared value types and small services only.

The core layer should own:

- IDs, source ranges, byte readers, source storage, and diagnostics.
- `Project`, `Collection`, `Asset`, and asset metadata.
- `CommandSequence` and command value types.
- `InstrumentSetAsset`, regions, modulators, generators, samples, and loops.
- Lowered model types such as `MidiSequence`.
- Registries and orchestration services such as `FormatRegistry`,
  `MidiSequenceProfileRegistry`, `ScanService`, and `Session`.

The current `Model.h` is useful for proving the shape of the model, but it will
become too large. Split it once the naming and behavior are stable:

- `ProjectModel.h`: assets, collections, metadata, and lookup helpers.
- `SequenceModel.h`: `CommandSequence`, `CommandTrack`, and command types.
- `SynthModel.h`: instruments, regions, modulators, samples, and decoded samples.
- `MidiModel.h`: `MidiSequence`, `MidiTrack`, and MIDI event types.
- `Model.h`: compatibility include while the rest of the app migrates.

Do this split mechanically after tests are strong. Do not split first and then
debug behavior through a pile of moved code.

## Scan Layer

`FormatModule` should stay minimal:

- `canScan()` checks whether the module recognizes a source.
- `scan()` parses source bytes into value assets and collections.
- `scan()` reports diagnostics and extracted child sources.
- `scan()` does not call MIDI, SF2, DLS, WAV, or tracker exporters.

Format modules should be allowed to use local helper types that reflect the
driver, such as CapcomSnes layout and table records. Those helpers should remain
inside the format directory unless another format demonstrably shares them.

The scan result should be complete enough that export can be repeated without
rescanning source bytes, except when a sample decoder intentionally reads sample
payloads from `SourceStore`.

## Sequence Model

`CommandSequence` is the source-semantic sequence model. It should not pretend to
be a universal tracker or MIDI representation. It should preserve commands in
terms close to the original driver while still using shared command names where
the meaning is common.

Good command types are:

- `NoteCommand`
- `RestCommand`
- `DurationCommand`
- `ProgramCommand`
- `VolumeCommand`
- `PanCommand`
- `TempoCommand`
- `TransposeCommand`
- `GlobalTransposeCommand`
- `TuningCommand`
- `PortamentoCommand`
- `VibratoCommand`
- `VibratoOffCommand`
- `TremoloCommand`
- `TremoloOffCommand`
- `ReverbCommand`
- `EnvelopeCommand`
- `JumpCommand`
- `RepeatCommand`
- `RepeatBreakCommand`
- `LoopBoundaryCommand`
- `EndCommand`
- `DriverSpecificCommand`
- `UnknownCommand`

The shared command model should grow when at least one of these is true:

- Multiple formats can use the concept directly.
- A lowerer needs to preserve the concept for more than one export target.
- Keeping it as `DriverSpecificCommand` would make a format profile unreadable.

Otherwise, keep the command local and explicit. A small amount of
format-specific data is better than a vague universal command whose meaning is
not stable.

## MIDI Lowering

`MidiSequence` is not the canonical sequence model. It is the MIDI-targeted
lowered model.

`MidiSequenceBuilder` should own generic sequence traversal:

- Track iteration.
- Tick advancement.
- Basic repeat and jump playback.
- Play-once loop policy.
- Ending tracks.
- Diagnostics for impossible traversal.

`MidiSequenceProfile` should own format-specific interpretation:

- Raw duration to ticks.
- Note length, slur, gate, and articulation behavior.
- Volume, pan, tempo, tuning, reverb, portamento, vibrato, and tremolo mapping.
- Driver-specific commands that are meaningful for MIDI export.

This split keeps per-format code small. A format author writes parsing code and a
profile that explains the driver's semantics. They should not write a full MIDI
exporter.

The profile API should remain narrow. If it starts gaining many unrelated fields,
prefer adding a source-level command or a small analysis pass over making
`MidiTrackState` a dumping ground for one driver's private state.

## Modulation

Vibrato and tremolo should stay explicit in the source command model rather than
being hidden behind a generic LFO command.

The source model should represent modulation as driver-level intent:

- Target: pitch, volume, pan, or unknown.
- Raw depth.
- Raw rate.
- Optional delay.
- Optional fade.
- Optional waveform/type.
- Optional enable/disable state.
- Source range.

The exact command names can be concrete (`VibratoCommand`, `TremoloCommand`) or a
single `ModulationCommand` if the second format proves the shared shape is
strong. For CapcomSnes, concrete commands will probably be clearer.

Export should be policy-driven. Add a modulation export policy to `ExportRequest`
when the shell/UI is ready:

- Prefer synth modulators.
- Prefer explicit MIDI pitch bend for vibrato.
- Prefer MIDI volume or expression events for tremolo.
- Auto, where the exporter chooses the best supported representation.

The policy is an export concern. It should not change how CapcomSnes parses
sequence commands.

## Modulation Analysis

The legacy `useColl()` behavior should become a pure analysis pass.

Use the value-layer `ModulationUsage` result to carry source-command observations:

- Per collection.
- Per sequence/profile.
- Per referenced instrument set where needed.
- Actual observed raw vibrato depth range.
- Actual observed raw tremolo depth range.
- Actual observed raw modulation rate range.
- Diagnostics for unsupported or ambiguous modulation.

SF2/DLS modulation scaling should use `MidiModulationUsage`, because synth
modulators respond to MIDI controller values after sequence lowering rather than
raw driver bytes. The analysis should run before synth export when observed
controller scaling is requested. It should not mutate the parsed collection or
instrument set. The synth exporter receives the parsed `InstrumentSetAsset`,
decoded samples, and `MidiModulationUsage`; `ModulationScalingPolicy` selects
whether synth export keeps legacy full-format ranges or uses observed sequence
ranges. The remaining work is to apply that policy when choosing controller
scaling and modulator amounts.

This solves the controller-resolution problem: if CapcomSnes only uses a narrow
vibrato depth range in a song, the exported modulator can map the observed range
to a 7-bit controller range instead of wasting resolution on the full theoretical
driver range.

## Instrument And Synth Model

The current `InstrumentSetAsset`, `Instrument`, `Region`, `SampleCollectionAsset`,
and `Sample` shape is a good foundation. It is less complicated than sequences,
but it still needs a clean parsed-versus-resolved split.

Parsed assets should contain:

- Instrument program and bank identity.
- Regions and key/velocity ranges.
- Source sample references.
- Tuning.
- Envelope data in source-friendly and normalized units where available.
- Pan, attenuation, reverb send, generators, and modulators.
- Source ranges for diagnostics and UI inspection.

Export-stage synth preparation should contain:

- Decoded samples.
- Resolved region sample indexes.
- Export-format constraints.
- SF2/DLS-specific generator and modulator conversion.
- Diagnostics for missing samples, unsupported codecs, invalid loops, and
  unsupported modulation.

If this grows beyond helpers such as `SynthExportData`, introduce
`ResolvedInstrumentSet` or `SynthSet`. Do not call it a bank.

## Tracker Roadmap

Tracker export should not be implemented in this phase, but the current design
should avoid blocking it.

The future tracker path should be:

```text
CommandSequence
  -> TrackerSequenceBuilder
  -> TrackerSequence
  -> Furnace/IT/XM exporter
```

`TrackerSequence` should be a lowered pattern/order/effect representation, not a
replacement for `CommandSequence`. It can preserve tracker-native concepts that
MIDI cannot express well, such as note cut, note delay, effect memory, pattern
break, order jumps, and per-row effect columns.

For now, only prepare for this by:

- Keeping `CommandSequence` source-semantic instead of MIDI-semantic.
- Keeping `MidiSequence` explicitly MIDI-named.
- Avoiding MIDI controller types in parsed command data unless the source format
  actually uses MIDI-like controllers.
- Keeping export policy in `ExportRequest`, not in format modules.
- Capturing loop and repeat commands instead of flattening them during scan.

A control-flow graph can be revisited later if tracker export needs deeper
structure-preserving loop analysis. It is not necessary for the current
CapcomSnes parity work.

## CapcomSnes Implementation Plan

CapcomSnes should remain the first full migration because it exercises sequence,
instrument, sample, and modulation behavior.

The format directory should stay split by concern:

- `CapcomSnesValueLayout`: source discovery and driver version/layout detection.
- `CapcomSnesValueSequence`: command parsing into `CommandSequence`.
- `CapcomSnesValueSynth`: instrument and sample parsing.
- `CapcomSnesProfile`: MIDI lowering behavior.
- `CapcomSnesModule`: scan orchestration and collection assembly.

The implementation must cover all higher-level behavior needed for legacy parity:

- Track headers and addresses.
- Notes, rests, slurs, durations, dotted/triplet/octave state.
- Tempo.
- Program/instrument selection.
- Volume and master volume.
- Pan.
- Reverb.
- Tuning and transpose.
- Portamento.
- Vibrato and tremolo.
- Repeats, repeat breaks, jumps, loops, and end commands.
- Instrument tables.
- ADSR, gain, pitch scale, tuning, pan/attenuation if represented by the driver.
- BRR sample references, loop points, and decoded sample export.
- SF2 and DLS generator/modulator behavior needed for parity.

The acceptable standard is not "exports something." The standard is normalized
or byte-level equivalence against the legacy model, with documented exceptions
only where the new value model intentionally fixes a legacy bug.

## Testing Plan

Testing has to prove both behavior and architecture.

Core tests should cover:

- Value lookup helpers.
- Source range propagation.
- Command naming and display helpers.
- `MidiSequenceBuilder` loop policies.
- Repeat, repeat-break, jump, and end behavior.
- Monophonic/slur note extension.
- Diagnostics when traversal cannot continue.

CapcomSnes tests should cover:

- Layout detection by engine version.
- Sequence parsing command-by-command.
- Instrument table parsing.
- Sample table parsing and BRR source ranges.
- Profile lowering for timing, volume, pan, tuning, portamento, vibrato,
  tremolo, repeat breaks, and loops.
- Export through `ExportService`.

Parity tests should compare the new pipeline against the legacy pipeline:

- Collection summary parity.
- MIDI parity using normalized MIDI events.
- SF2 parity using normalized presets, instruments, regions, samples,
  generators, modulators, and loop metadata.
- DLS parity using normalized instruments, regions, waves, articulations, and
  loops.
- Export smoke tests for every collection in selected RSN fixtures.

The branch should not be considered ready until:

- Focused unit tests pass.
- The parity harness passes for the selected fixture set.
- A broader SPC directory smoke/parity run passes or has documented, reviewed
  exceptions.

## Migration Phases

### Phase 1: Stabilize Names And Boundaries

- Rename `EventSequence` to `MidiSequence`.
- Rename `EventTrack` to `MidiTrack`.
- Rename `EventSequenceBuilder` to `MidiSequenceBuilder`.
- Rename `SequencerProfile` to `MidiSequenceProfile`.
- Rename `sequencerProfile` fields to `midiSequenceProfile`.
- Keep `CommandSequence` as the parsed sequence model.
- Add this plan as the roadmap for the rest of the work.

### Phase 2: Finish CapcomSnes Parity

- Audit every legacy CapcomSnes command and every value parser command.
- Add missing source-level commands instead of hiding behavior in
  `DriverSpecificCommand`.
- Complete MIDI lowering profile behavior.
- Complete instrument and sample conversion parity.
- Keep tests close to the behavior being migrated.

### Phase 3: Add Modulation Policy And Analysis

- Continue refining explicit vibrato/tremolo/rate source commands where formats
  need more detail.
- Expand `ModulationUsage` and `MidiModulationUsage` as additional formats expose
  modulation details.
- Use the threaded `MidiModulationUsage` and `ModulationScalingPolicy` data for
  SF2/DLS controller scaling.
- Add export policy for synth modulators versus explicit MIDI events.
- Keep default behavior matching legacy output.

### Phase 4: Refine Synth Export Preparation

- Keep parsed `InstrumentSetAsset` source-oriented.
- Move export-specific resolution into a dedicated synth export preparation
  layer.
- Introduce `ResolvedInstrumentSet` or `SynthSet` only if helper functions stop
  being enough.
- Keep SF2 and DLS exporters sharing resolved data where their behavior matches.

### Phase 5: Prepare For Tracker Export Without Implementing It

- Keep parsed commands free of accidental MIDI assumptions.
- Add comments/tests where a command must preserve loop or modulation semantics
  for future tracker lowering.
- Do not add `TrackerSequence` until there is a concrete first exporter target.

### Phase 6: App Integration

- Keep the new pipeline accessible through `vgmtrans-shell` until parity is
  credible.
- Once CapcomSnes parity is strong, add UI integration behind a narrow selection
  path.
- Keep legacy and value pipelines side by side until enough formats have migrated
  or a compatibility shim can cover remaining UI behavior.

## Branch Strategy

Continue on the current value-oriented branch while it contains the CapcomSnes
POC and parity harness. Starting over from `master` would throw away useful test
infrastructure and working parse/export code.

Create a fresh branch from `master` only if the current branch becomes impossible
to review because of unrelated churn. If that happens, cherry-pick the valuable
pieces in this order:

1. Core value model and source infrastructure.
2. Export service and MIDI/SF2/DLS/WAV exporters.
3. CapcomSnes parser/profile/synth implementation.
4. Parity harness and focused tests.
5. Documentation and shell entry points.

## Review Checklist

Before merging the rewrite, verify:

- Format modules do not branch on export kind.
- Exporters do not parse source bytes except through sample decoding.
- `CommandSequence` is not MIDI-specific.
- `MidiSequence` is explicitly MIDI-specific.
- Instrument conversion has a parsed stage and an export-preparation stage.
- Vibrato and tremolo behavior is represented before it is lowered.
- Modulation controller scaling uses observed sequence ranges where required.
- CapcomSnes parity passes against legacy output.
- Any parity exceptions are intentional and documented.
