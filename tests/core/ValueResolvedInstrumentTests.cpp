/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "../TestSupport.h"
#include "SessionSnapshotBuilder.h"
#include "SynthExportTestSupport.h"
#include "value/export/CollectionBinding.h"
#include "value/export/Export.h"
#include "value/export/ResolvedPerformance.h"
#include "value/export/midi/PitchTransitionMidiLowering.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/synth/SynthExportData.h"
#include "value/sequence/SequenceVm.h"

#include <array>
#include <set>
#include <type_traits>

using namespace vgmtrans::core;

namespace {

static_assert(!std::is_default_constructible_v<ResolvedPerformance>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<ResolvedPerformance&>().performance())>>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<ResolvedPerformance&>().soundBanks())>>);

const NotePerformanceEvent& noteById(const PerformanceSequence& performance, PerformanceNoteId id) {
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event); note && note->note == id) return *note;
  }
  throw std::runtime_error("Missing test note");
}

void resolvedVariantsShareAddressesWithBothSynthWriters() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "resolved.pcm"}, {0, 32, 64, 96});
  const SamplePoolAsset pool{
      .metadata = {.id = AssetId{20}},
      .pool = {.samples = {Sample{.codec = AudioCodec::PcmS8, .encodedData = {source, 0, 4}, .sampleRate = 16000}}}};
  const auto makeInstrument = [](u32 program) {
    return Instrument{
        .explicitAddress = InstrumentAddress{9, program},
        .identity = InstrumentIdentity{"prototype", program},
        .pitchBendRangeCents = 400,
        .name = "Program " + std::to_string(program),
        .regions = {Region{.sample = SampleRef::resolved(AssetId{20}, 0),
                           .envelope = Envelope{.attackSeconds = 1.0}}}};
  };
  std::array banks{SoundBankAsset{.metadata = {.id = AssetId{10}},
                                  .instruments = {makeInstrument(5), makeInstrument(7)}}};
  banks[0].instruments.shrink_to_fit();  // Appending the variant must not invalidate its base handle.
  const std::array<const SamplePoolAsset*, 1> pools{&pool};

  PerformanceTrack track{.id = TrackId{0}, .endTick = 12};
  u64 order = 0;
  u32 noteId = 0, automationId = 0;
  PerformanceEmitter out{track, {TrackId{0}, CommandId{1}}, SourceAnnotationId{1}, 0, order, noteId, automationId};
  out.instrument(InstrumentIdentity{"prototype", 5});
  out.updateEnvelope(Envelope{.attackSeconds = 0.25}, EnvelopeFields::Attack);
  const auto first = out.note(60, 1, 4);
  out.at(2).instrument(InstrumentIdentity{"prototype", 7});
  const auto continuation = out.at(4).note(64, 1, 4);
  out.at(4).pitchSlide(continuation, 60, 64, 2).continueFrom(first);
  const auto fresh = out.at(8).note(67, 1, 4);
  const PerformanceSequence sourcePerformance{.timebase = {.ppqn = 48}, .tracks = {track}};
  const auto unadapted = preparePerformance(sourcePerformance, {banks.begin(), banks.end()});
  expect(noteById(unadapted.performance(), first).instrument == InstrumentSelection{InstrumentHandle{0, 0}} &&
             noteById(unadapted.performance(), continuation).instrument == InstrumentSelection{InstrumentHandle{0, 0}} &&
             noteById(unadapted.performance(), fresh).instrument == InstrumentSelection{InstrumentHandle{0, 1}},
         "resolution must retain the attack's instrument through an intervening program change");
  const auto resolved = preparePerformance(sourcePerformance, {banks.begin(), banks.end()}, {.dynamicEnvelopes = true});
  const auto& preparedBanks = resolved.soundBanks();
  const auto variant = *noteById(resolved.performance(), first).instrument;
  expect(std::get<InstrumentHandle>(variant) == InstrumentHandle{0, 2} &&
             noteById(resolved.performance(), continuation).instrument == variant &&
             noteById(resolved.performance(), fresh).instrument == InstrumentSelection{InstrumentHandle{0, 1}} &&
             !preparedBanks[0].instruments[2].explicitAddress,
         "variants must propagate stable handles without allocating an output preset");
  expect(!noteById(sourcePerformance, first).instrument && banks[0].instruments[0].regions[0].envelope.attackSeconds == 1.0,
         "preparation must leave the source performance and original instrument envelope intact");

  const auto layout = planInstrumentAddresses(resolved, true, 11);
  expect(layout.valid && layout.banks.at(0) == 11 && layout.banks.at(9) == 12 &&
             noteById(resolved.performance(), first).instrument == variant,
         "late bank assignment must leave instrument references unchanged");
  const auto selected = selectSynthBanks(resolved, layout, true);
  expect(selected.size() == 1 && selected[0].bank == &preparedBanks[0] &&
             selected[0].instruments.size() == 2 &&
             selected[0].instruments[0].instrument == &preparedBanks[0].instruments[1] &&
             selected[0].instruments[1].instrument == &preparedBanks[0].instruments[2],
         "used-only selection must consume handles and retain the attack variant, not its unused base");
  const SynthExportInput input{.soundBanks = selected, .samplePools = pools,
                               .filterSamplesToReferencedInstruments = true};
  const auto sf2 = buildSoundFont2(input, sources);
  const auto dls = buildDls(input, sources);
  expect(sf2.diagnostics.empty() && dls.diagnostics.empty(), "both synth writers should accept the shared selection");
  std::set<std::pair<u32, u32>> sfPresets;
  const auto phdr = asciiOffset(sf2.bytes, "phdr") + 8;
  for (size_t index = 0; index + 1 < chunkSize(sf2.bytes, "phdr") / 38; ++index) {
    sfPresets.emplace(readLe16(sf2.bytes, phdr + index * 38 + 22), readLe16(sf2.bytes, phdr + index * 38 + 20));
  }
  std::set<std::pair<u32, u32>> expected;
  for (const auto& item : selected[0].instruments) expected.emplace(item.address.bank, item.address.program);
  expect(sfPresets == expected && readLe32(dls.bytes, asciiOffset(dls.bytes, "colh") + 8) == expected.size(),
         "serialized synth preset tables must use the shared late address plan");
  // Inspect DLS instrument headers independently of the production model.
  std::set<std::pair<u32, u32>> dlsPresets;
  for (size_t offset = 0; offset + 20 <= dls.bytes.size(); ++offset) {
    if (std::string_view(reinterpret_cast<const char*>(dls.bytes.data() + offset), 4) == "insh") {
      dlsPresets.emplace(readLe32(dls.bytes, offset + 12) >> 8, readLe32(dls.bytes, offset + 16));
    }
  }
  expect(dlsPresets == expected, "DLS and SF2 must agree on every planned preset");

  for (const auto mode : {MidiPitchTransitionRendering::PitchBend, MidiPitchTransitionRendering::Portamento}) {
    const auto midi = renderMidiSequence(resolved, layout, {.pitchTransitions = mode});
    expect(midi.diagnostics.empty(), "resolved MIDI should not repeat lookup or adaptation diagnostics");
    auto events = midi.tracks[0].events;
    std::ranges::stable_sort(events, {}, [](const MidiEvent& event) { return std::pair{event.tick, event.priority}; });
    InstrumentAddress current;
    size_t count = 0;
    for (const auto& event : events) {
      if (const auto* bank = std::get_if<BankSelect>(&event.payload)) current.bank = bank->bank;
      if (const auto* message = std::get_if<MidiChannelMessage>(&event.payload);
          message && message->kind == MidiChannelMessageKind::ProgramChange) current.program = message->value;
      if (std::holds_alternative<NoteDuration>(event.payload)) {
        ++count;
        const auto wanted = event.tick < 8 ? layout.address(variant) : layout.address(InstrumentHandle{0, 1});
        expect(current == wanted && sfPresets.contains({current.bank, current.program}),
               "every MIDI attack and portamento fragment must select the matching serialized synth preset");
      }
    }
    expect(count >= 2, "the paired-output fixture must contain both performed attacks");
  }
}

void resolutionChoosesAndDiagnosesOneDefinition() {
  const Instrument first{.explicitAddress = InstrumentAddress{3, 5},
                          .identity = InstrumentIdentity{"native", 5}, .name = "first"};
  const SoundBankAsset bank{.instruments = {first, first}};
  PerformanceTrack track{.id = TrackId{0}};
  track.events = {
      InstrumentPerformanceEvent{.instrument = InstrumentIdentity{"native", 5}},
      NotePerformanceEvent{.note = PerformanceNoteId{0}},
      InstrumentPerformanceEvent{.header = {.tick = 1}, .instrument = InstrumentIdentity{"missing", 389}},
      NotePerformanceEvent{.header = {.tick = 1}, .note = PerformanceNoteId{1}},
      NotePerformanceEvent{.header = {.tick = 2}, .instrument = InstrumentAddress{7, 9}, .note = PerformanceNoteId{2}},
  };
  const auto resolved = preparePerformance(PerformanceSequence{.tracks = {track}}, {bank});
  expect(noteById(resolved.performance(), PerformanceNoteId{0}).instrument ==
             InstrumentSelection{InstrumentHandle{0, 0}} &&
             noteById(resolved.performance(), PerformanceNoteId{1}).instrument ==
             InstrumentSelection{InstrumentHandle{0, 0}} &&
             noteById(resolved.performance(), PerformanceNoteId{2}).instrument ==
             InstrumentSelection{InstrumentAddress{7, 9}},
         "exact identity, fallback and external presets must resolve consistently");
  expect(std::ranges::count(resolved.performance().diagnostics, std::string("instrument-selection-fallback"),
                            &Diagnostic::code) == 1 &&
             std::ranges::count(resolved.performance().diagnostics, std::string("instrument-selection-conflict"),
                               &Diagnostic::code) == 2,
         "fallback and conflicting definitions must be reported once per source selection");
  const auto layout = planInstrumentAddresses(resolved, true);
  expect(selectSynthBanks(resolved, layout, true)[0].instruments.size() == 1,
         "filtering must use the same single definition as notes, leaving external presets external");
  const auto midi = renderMidiSequence(resolved, layout);
  expect(!midi.tracks.empty(), "a missing companion instrument must not prevent standalone MIDI");
}

void layoutSeparatesCollisionsAndRejectsOverflow() {
  const SoundBankAsset bank{.instruments = {
      Instrument{.explicitAddress = InstrumentAddress{0, 5}, .identity = InstrumentIdentity{"a", 1}},
      Instrument{.explicitAddress = InstrumentAddress{0, 5}, .identity = InstrumentIdentity{"b", 1}}}};
  const auto resolved = preparePerformance({.diagnostics = {{.code = "source-warning"}}}, {bank});
  const auto layout = planInstrumentAddresses(resolved);
  expect(layout.valid && layout.address(InstrumentHandle{0, 0}) != layout.address(InstrumentHandle{0, 1}),
         "distinct definitions must receive distinct output slots even when their preferred addresses collide");
  const auto implicitZero = preparePerformance({}, {SoundBankAsset{
      .instruments = {Instrument{}, Instrument{.explicitAddress = InstrumentAddress{0, 0}}}}});
  const auto zeroPlan = planInstrumentAddresses(implicitZero);
  expect(zeroPlan.address(InstrumentHandle{0, 0}) == InstrumentAddress{0, 0} &&
             zeroPlan.address(InstrumentHandle{0, 1}) == InstrumentAddress{0, 1},
         "an original instrument's implicit zero address is a preference, not an unassigned generated variant");
  const auto failed = planInstrumentAddresses(resolved, false, 128);
  const auto failedMidi = renderMidiSequence(resolved, failed);
  expect(!failed.valid && failed.diagnostics.size() == 1 &&
             failedMidi.tracks.empty() && failedMidi.diagnostics.size() == 2 &&
             failedMidi.diagnostics.front().code == "source-warning",
         "bank exhaustion must reject a partial plan while preserving earlier preparation diagnostics");
}

void preparedOwnershipSurvivesMovesAndLowering() {
  const auto makePrepared = [] {
    SoundBankAsset bank{.instruments = {Instrument{.explicitAddress = InstrumentAddress{0, 5}, .name = "Owned"}}};
    PerformanceSequence performance{.tracks = {PerformanceTrack{.events = {
        InstrumentPerformanceEvent{.instrument = InstrumentAddress{0, 5}},
        NotePerformanceEvent{.key = 60, .durationTicks = 4, .note = PerformanceNoteId{0}},
    }}}};
    return preparePerformance(std::move(performance), {std::move(bank)});
  };
  auto original = makePrepared();
  const auto* instrument = original.instrument(InstrumentHandle{0, 0});
  auto lowered = lowerMidiPerformanceAutomation(original, {}, PerformanceTempoMap{original.performance()});
  expect(lowered.instrument(InstrumentHandle{0, 0}) == instrument,
         "lowering must share immutable banks rather than copying them or borrowing the input's lifetime");
  original = preparePerformance({});
  std::vector<ResolvedPerformance> moved;
  moved.push_back(std::move(lowered));
  for (size_t index = 0; index < 8; ++index) moved.push_back(makePrepared());
  expect(moved[0].instrument(InstrumentHandle{0, 0})->name == "Owned" &&
             !renderMidiSequence(moved[0], planInstrumentAddresses(moved[0])).tracks.empty(),
         "prepared handles must remain usable after source destruction, lowering, moves and container growth");
}

void laterSourceSelectionsCannotFindGeneratedVariants() {
  const SoundBankAsset bank{.instruments = {Instrument{
      .identity = InstrumentIdentity{"native", 5}, .regions = {Region{.envelope = {.attackSeconds = 1.0}}}}}};
  PerformanceSequence source{.tracks = {PerformanceTrack{.events = {
      InstrumentPerformanceEvent{.instrument = InstrumentIdentity{"native", 5}},
      EnvelopePerformanceEvent{.update = EnvelopeUpdate::set(Envelope{.attackSeconds = 0.25}, EnvelopeFields::Attack)},
      NotePerformanceEvent{.note = PerformanceNoteId{0}},
      InstrumentPerformanceEvent{.header = {.tick = 1}, .instrument = InstrumentIdentity{"missing", 0}},
      NotePerformanceEvent{.header = {.tick = 1}, .note = PerformanceNoteId{1}},
  }}}};
  const auto prepared = preparePerformance(source, {bank}, {.dynamicEnvelopes = true});
  expect(prepared.soundBanks()[0].instruments.size() == 2 &&
             noteById(prepared.performance(), PerformanceNoteId{0}).instrument ==
                 InstrumentSelection{InstrumentHandle{0, 1}} &&
             noteById(prepared.performance(), PerformanceNoteId{1}).instrument ==
                 InstrumentSelection{InstrumentAddress{0, 0}},
         "numeric fallback must search original definitions, not an earlier generated variant without an address");
  const auto plan = planInstrumentAddresses(prepared);
  expect(plan.address(InstrumentHandle{0, 1}) != InstrumentAddress{0, 0},
         "the external preset must reserve its address before any generated variant receives an output slot");
}

void collectionExportsRequireACompanionForVariants() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "variant.pcm"}, std::vector<u8>(64));
  const SourceRange range{source, 0, 64};
  const auto play = [](const SourceCommand&, std::any&, std::any&, PerformanceEmitter& out, VmApi&) {
    out.instrument(InstrumentIdentity{"prototype", 5});
    out.updateEnvelope(Envelope{.attackSeconds = 0.25}, EnvelopeFields::Attack);
    out.note(60, 1, 4);
    return Effects::wait(4);
  };
  test::SessionSnapshotBuilder builder;
  builder.sources = sources.sourceFiles();
  builder.assets = {
      SequenceProgramAsset{
          .metadata = {.id = AssetId{0}, .name = "Song", .range = range},
          .program = {.runtime = {.execute = play},
                      .tracks = {TrackProgram{
                          .startAddress = Address{0},
                          .commands = {SourceCommand{.address = Address{0}, .range = {source, 0, 1},
                                                     .flow = CommandFlow::end(Address{1})}}}}}},
      SoundBankAsset{
          .metadata = {.id = AssetId{1}, .name = "Bank", .range = range},
          .instruments = {Instrument{
              .identity = InstrumentIdentity{"prototype", 5},
              .regions = {Region{.sample = SampleRef::resolved(AssetId{1}, 0),
                                 .envelope = {.attackSeconds = 1.0}}}}},
          .localSamples = {.samples = {Sample{.codec = AudioCodec::PcmS16, .encodedData = range,
                                               .sampleRate = 22050}}}}};
  builder.collections = {{.id = CollectionId{0}, .name = "Song",
                           .members = {.sequence = AssetId{0}, .soundBanks = {AssetId{1}}}}};
  const auto snapshot = builder.finish();
  const auto direct = exportSequenceMidi(snapshot, sources, AssetId{0}, {});
  const auto ignored = exportCollection(snapshot, sources, CollectionId{0},
      {.kinds = {ExportKind::Midi}, .dynamicEnvelopes = DynamicEnvelopePolicy::Ignore});
  const auto paired = exportCollection(snapshot, sources, CollectionId{0},
      {.kinds = {ExportKind::Midi, ExportKind::SoundFont2, ExportKind::Dls}});
  const auto reversed = exportCollection(snapshot, sources, CollectionId{0},
      {.kinds = {ExportKind::Dls, ExportKind::SoundFont2, ExportKind::Midi}});
  const auto playback = prepareCollectionPlayback(snapshot, sources, CollectionId{0}, {});
  const auto withWav = exportCollection(snapshot, sources, CollectionId{0},
      {.kinds = {ExportKind::Midi, ExportKind::SoundFont2, ExportKind::Wav}});
  const auto wavOnly = exportCollection(snapshot, sources, CollectionId{0}, {.kinds = {ExportKind::Wav}});
  expect(withWav.size() == 3 && wavOnly.size() == 1 && !wavOnly[0].bytes.empty() &&
             withWav[2].bytes == wavOnly[0].bytes,
         "transferring bank ownership into preparation must preserve local samples for a paired WAV export");
  expect(!direct.bytes.empty() && direct.bytes == ignored[0].bytes && direct.bytes != paired[0].bytes &&
             std::ranges::count(direct.diagnostics, std::string("dynamic-envelope-no-companion"),
                                &Diagnostic::code) == 1,
         "MIDI without a companion must retain source presets and report the unrepresented envelope changes");
  expect(paired[0].bytes == reversed[2].bytes && paired[1].bytes == reversed[1].bytes &&
             paired[2].bytes == reversed[0].bytes && paired[0].bytes == playback.midi &&
             paired[1].bytes == playback.soundFont && paired[0].diagnostics.empty() &&
             paired[1].diagnostics.empty() && paired[2].diagnostics.empty(),
         "playback and paired exports must consume the same prepared instruments regardless of output order");
  expect(chunkSize(paired[1].bytes, "phdr") == 3 * 38 &&
             readLe32(paired[2].bytes, asciiOffset(paired[2].bytes, "colh") + 8) == 2,
         "both companion formats must carry the original preset and its generated variant");
  expect(snapshot.asset<SoundBankAsset>(AssetId{1})->instruments.size() == 1,
         "repeated exports must not add variants to the snapshot's source bank");
}

void completedCollectionsRetainInputsAcrossRenderingOutcomes() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "lifetime.pcm"}, {0, 32, 64, 96});
  enum class Scenario { NoSequence, Skipped, Failed, Rendered };
  for (const auto scenario : {Scenario::NoSequence, Scenario::Skipped, Scenario::Failed, Scenario::Rendered}) {
    size_t executions = 0;
    auto prepared = [&] {
      test::SessionSnapshotBuilder builder;
      builder.sources = sources.sourceFiles();
      builder.assets = {
          SequenceProgramAsset{
              .metadata = {.id = AssetId{0}, .name = "Song"},
              .program = {
                  .runtime = {.createProgramState = [&, scenario](const SequenceProgram&) -> std::any {
                    ++executions;
                    if (scenario == Scenario::Failed) throw std::runtime_error("lifecycle failure");
                    return {};
                  }, .execute = [](const SourceCommand&, std::any&, std::any&, PerformanceEmitter& out, VmApi&) {
                    out.instrument(0, 5);
                    out.updateEnvelope(Envelope{.attackSeconds = 0.25}, EnvelopeFields::Attack);
                    out.note(60, 1, 4);
                    return Effects::wait(4);
                  }},
                  .tracks = {TrackProgram{.startAddress = Address{0}, .commands = {
                      SourceCommand{.address = Address{0}, .flow = CommandFlow::end(Address{1})}}}}}},
          SoundBankAsset{
              .metadata = {.id = AssetId{1}, .name = "Bank"},
              .instruments = {Instrument{.explicitAddress = InstrumentAddress{0, 5}, .regions = {
                  Region{.sample = SampleRef::resolved(AssetId{2}, 0), .envelope = {.attackSeconds = 1.0}}}}}},
          SamplePoolAsset{
              .metadata = {.id = AssetId{2}, .name = "Samples"},
              .pool = {.samples = {Sample{.codec = AudioCodec::PcmS8, .encodedData = {source, 0, 4},
                                          .sampleRate = 16000}}}},
      };
      builder.collections = {{.id = CollectionId{0}, .name = "Collection", .members = {
          .sequence = scenario == Scenario::NoSequence ? std::nullopt : std::optional{AssetId{0}},
          .soundBanks = {AssetId{1}}, .samplePools = {AssetId{2}}}}};
      const auto snapshot = builder.finish();
      auto binding = bindCollection(snapshot, CollectionId{0});
      expect(binding.collection.has_value(), "the lifetime fixture should bind successfully");
      const PreparedCollection original{std::move(*binding.collection), {
          .sequence = scenario == Scenario::Skipped || scenario == Scenario::NoSequence
              ? std::nullopt : std::optional{SequenceRenderOptions{}},
          .variants = {.dynamicEnvelopes = true},
      }};
      expect(snapshot.asset<SoundBankAsset>(AssetId{1})->instruments.size() == 1,
             "preparation must leave the snapshot's bank unchanged");
      std::vector<PreparedCollection> copies;
      copies.push_back(original);
      copies.push_back(original);
      return std::move(copies.back());
    }();  // The binding, snapshot, original result, and other copies are gone.

    const bool rendered = scenario == Scenario::Rendered;
    expect(executions == (scenario == Scenario::Failed || rendered ? 1 : 0),
           "construction must execute the sequence only when requested, once even on failure");
    expect(bool(prepared.performance()) == rendered && bool(prepared.rendering.performance) == rendered &&
               prepared.rendering.diagnostics.empty() == (scenario != Scenario::Failed),
           "the completed result must distinguish a usable performance from skipped or failed rendering");
    expect(prepared.id == CollectionId{0} && prepared.baseName == "Collection" &&
               prepared.sequenceId.has_value() == (scenario != Scenario::NoSequence) &&
               prepared.samplePools[0]->metadata.name == "Samples" && prepared.soundBanks().size() == 1 &&
               prepared.soundBanks()[0].instruments.size() == (rendered ? 2 : 1),
           "moves and copies must retain bank data, metadata, and external sample owners in every outcome");
    const auto banks = prepared.soundBankView();
    const auto synth = buildSoundFont2({.soundBanks = selectSynthBanks(banks), .samplePools = prepared.samplePools}, sources);
    expect(!synth.bytes.empty() && synth.diagnostics.empty(),
           "retained inputs must remain usable for synth export after their original owners are destroyed");
    if (rendered) {
      const auto& performance = *prepared.performance();
      expect(&prepared.soundBanks() == &performance.soundBanks() &&
                 !noteById(*prepared.rendering.performance, PerformanceNoteId{0}).instrument &&
                 noteById(performance.performance(), PerformanceNoteId{0}).instrument.has_value(),
             "resolved events must share the final banks while source events remain available for inspection");
      expect(!renderMidiSequence(performance, planInstrumentAddresses(performance)).tracks.empty(),
             "the retained resolved performance must still render after owner moves and destruction");
    }
  }
}

void synthSelectionsPreserveBankSamplingAndSampleOwners() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "banks.pcm"}, {10, 20, 30, 40});
  const auto sample = [&](u32 offset) {
    return Sample{.codec = AudioCodec::PcmS8, .encodedData = {source, offset, 1}, .sampleRate = 16000};
  };
  const auto response = [](AssetId owner) {
    return Region{.keyRange = {60, 63}, .velocityRange = {20, 23}, .sample = SampleRef::resolved(owner, 0),
                  .response = {.keyDependent = true, .velocityDependent = true,
                               .evaluate = [](Region& region, u8 key, u8 velocity) {
                                 region.attenuationDb = key + velocity;
                               }}};
  };
  auto unusedResponse = response(AssetId{10});
  unusedResponse.keyRange = {};
  unusedResponse.velocityRange = {};
  const std::vector<SoundBankAsset> banks{
      {.metadata = {.id = AssetId{10}},
       .instruments = {
           Instrument{.explicitAddress = InstrumentAddress{0, 5}, .identity = InstrumentIdentity{"A", 1},
                      .name = "Selected A", .regions = {response(AssetId{10})}},
           Instrument{.explicitAddress = InstrumentAddress{0, 7}, .name = "Unused A", .regions = {unusedResponse}}},
       .localSamples = {.samples = {sample(0)}}},
      {.metadata = {.id = AssetId{20}},
       .instruments = {
           Instrument{.explicitAddress = InstrumentAddress{0, 5}, .identity = InstrumentIdentity{"B", 1},
                      .name = "Selected B", .regions = {response(AssetId{40})}}},
       .localSamples = {.samples = {sample(1)}}},
      {.metadata = {.id = AssetId{30}}, .localSamples = {.samples = {sample(2)}}},
  };
  const SamplePoolAsset pool{.metadata = {.id = AssetId{40}}, .pool = {.samples = {sample(3)}}};
  const std::array pools{&pool};
  const auto resolved = preparePerformance({.tracks = {PerformanceTrack{.events = {
      NotePerformanceEvent{.instrument = InstrumentIdentity{"A", 1}, .note = PerformanceNoteId{0}},
      NotePerformanceEvent{.header = {.tick = 1}, .instrument = InstrumentIdentity{"B", 1},
                           .note = PerformanceNoteId{1}},
  }}}}, banks);
  const auto layout = planInstrumentAddresses(resolved, true);
  // The input owns the selection returned by the builder; no temporary view is retained.
  SynthExportInput input{.soundBanks = selectSynthBanks(resolved, layout, true), .samplePools = pools};
  auto prepared = prepareSynthData(input, sources);
  expect(prepared.instruments.size() == 2 && prepared.instruments[0].regions.size() == 4 &&
             prepared.instruments[1].regions.size() == 16 && prepared.diagnostics.size() == 1 &&
             prepared.instruments[0].regions[0].region.attenuationDb == 82 &&
             prepared.instruments[1].regions[0].region.attenuationDb == 80,
         "selected instruments must use their own bank's sampling budget, including its unselected regions");
  expect(prepared.instruments[0].address == layout.address(InstrumentHandle{0, 0}) &&
             prepared.instruments[1].address == layout.address(InstrumentHandle{1, 0}) &&
             prepared.instruments[0].address != prepared.instruments[1].address,
         "synth selection must preserve planned collision addresses instead of reusing source preferences");
  expect(prepared.samples.size() == 4 && prepared.samples[0].decoded.pcm == std::vector<s16>{2560} &&
             prepared.samples[1].decoded.pcm == std::vector<s16>{5120} &&
             prepared.samples[2].decoded.pcm == std::vector<s16>{7680} &&
             prepared.samples[3].decoded.pcm == std::vector<s16>{10240} &&
             prepared.instruments[1].regions[0].sampleIndex == 3,
         "unfiltered samples must preserve bank/pool order, including banks with no selected instruments");
  input.filterSamplesToReferencedInstruments = true;
  prepared = prepareSynthData(input, sources);
  expect(prepared.samples.size() == 2 && prepared.instruments[0].regions[0].sampleIndex == 0 &&
             prepared.instruments[1].regions[0].sampleIndex == 1 &&
             prepared.samples[1].decoded.pcm == std::vector<s16>{10240},
         "used-only sample filtering must follow the selected entries' local and external sample references");

  const auto silent = preparePerformance({}, banks);
  const auto empty = prepareSynthData({.soundBanks = selectSynthBanks(silent, planInstrumentAddresses(silent, true), true),
                                       .samplePools = pools, .filterSamplesToReferencedInstruments = true}, sources);
  expect(empty.instruments.empty() && empty.samples.empty(),
         "a sequence using no instruments must not fall back to exporting its entire bank");

  test::SessionSnapshotBuilder builder;
  for (const auto& bank : banks) builder.assets.emplace_back(bank);
  builder.assets.emplace_back(pool);
  builder.assets.emplace_back(SequenceProgramAsset{
      .metadata = {.id = AssetId{0}},
      .program = {.runtime = {.execute = [](const SourceCommand&, std::any&, std::any&, PerformanceEmitter& out, VmApi&) {
                    out.instrument(InstrumentIdentity{"A", 1});
                    out.note(60, 1, 1);
                    out.instrument(InstrumentIdentity{"B", 1});
                    out.note(60, 1, 1);
                    return Effects::wait(1);
                  }},
                  .tracks = {TrackProgram{.startAddress = Address{0}, .commands = {
                      SourceCommand{.address = Address{0}, .flow = CommandFlow::end(Address{1})}}}}}});
  builder.collections = {{.id = CollectionId{0}, .members = {
      .sequence = AssetId{0}, .soundBanks = {AssetId{10}, AssetId{20}, AssetId{30}}, .samplePools = {AssetId{40}}}}};
  const auto snapshot = builder.finish();
  for (const auto format : {SynthExportFormat::SoundFont2, SynthExportFormat::Dls}) {
    const auto artifact = exportSoundBank(snapshot, sources, AssetId{20}, format, {.exportOnlyUsedInstruments = true});
    expect(!artifact.bytes.empty() && artifact.diagnostics.empty() && containsAscii(artifact.bytes, "Selected B") &&
               !containsAscii(artifact.bytes, "Selected A") && !containsAscii(artifact.bytes, "Unused A"),
           "exporting one used bank must exclude other banks, their instruments, and their sampling diagnostics");
  }
}

}  // namespace

void runResolvedInstrumentTests() {
  resolvedVariantsShareAddressesWithBothSynthWriters();
  resolutionChoosesAndDiagnosesOneDefinition();
  layoutSeparatesCollisionsAndRejectsOverflow();
  preparedOwnershipSurvivesMovesAndLowering();
  laterSourceSelectionsCannotFindGeneratedVariants();
  collectionExportsRequireACompanionForVariants();
  completedCollectionsRetainInputsAcrossRenderingOutcomes();
  synthSelectionsPreserveBankSamplingAndSampleOwners();
}
