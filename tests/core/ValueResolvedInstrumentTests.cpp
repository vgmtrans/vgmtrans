/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "../TestSupport.h"
#include "../PerformanceTestSupport.h"
#include "SessionSnapshotBuilder.h"
#include "SynthExportTestSupport.h"
#include "value/export/CollectionBinding.h"
#include "value/export/Export.h"
#include "value/export/ResolvedPerformance.h"
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

  PerformanceTrackFixture fixture{12};
  auto& track = fixture.track;
  auto& out = fixture.out;
  out.instrument(InstrumentIdentity{"prototype", 5});
  out.updateEnvelope(Envelope{.attackSeconds = 0.25}, EnvelopeFields::Attack);
  const auto first = out.note(60, 1, 4);
  out.at(2).instrument(InstrumentIdentity{"prototype", 7});
  out.at(2).continueVoice(first, NotePerformanceEvent{.key = 60, .durationTicks = 2});
  out.at(4).restoreEnvelope();
  const auto continuation = out.at(4).continueVoice(first, NotePerformanceEvent{.key = 64, .durationTicks = 4});
  out.at(4).pitchSlide(continuation, 60, 64, 2);
  const auto fresh = out.at(8).note(67, 1, 4);
  const PerformanceSequence sourcePerformance{.timebase = {.ppqn = 48}, .tracks = {track}};
  const auto resolved = preparePerformance(sourcePerformance, {banks.begin(), banks.end()},
      {.dynamicEnvelopes = true, .onlyUsedInstruments = true, .firstBank = 11});
  const auto& preparedBanks = resolved.soundBanks();
  const auto variant = resolved.voiceFor(noteById(resolved.performance(), first)).instrument;
  expect(std::get<InstrumentHandle>(variant) == InstrumentHandle{0, 2} &&
             resolved.voiceFor(noteById(resolved.performance(), continuation)).instrument == variant &&
             resolved.voiceFor(noteById(resolved.performance(), fresh)).instrument == InstrumentSelection{InstrumentHandle{0, 1}} &&
             !preparedBanks[0].instruments[2].explicitAddress,
         "variants must propagate stable handles without overwriting the instrument's source address preference");
  expect(!noteById(sourcePerformance, first).instrument && banks[0].instruments[0].regions[0].envelope.attackSeconds == 1.0,
         "preparation must leave the source performance and original instrument envelope intact");

  expect(resolved.valid() && resolved.bankMapping().at(0) == 11 && resolved.bankMapping().at(9) == 12 &&
             resolved.voiceFor(noteById(resolved.performance(), first)).instrument == variant,
         "completed preparation must retain handles while assigning the requested bank namespace");
  const auto selected = selectSynthBanks(resolved);
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
    const auto midi = renderMidiSequence(resolved, {.pitchTransitions = mode});
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
        const auto wanted = event.tick < 8 ? resolved.selectionFor(noteById(resolved.performance(), first)).address : resolved.selectionFor(InstrumentHandle{0, 1}).address;
        expect(current == wanted && sfPresets.contains({current.bank, current.program}),
               "every MIDI attack and portamento fragment must select the matching serialized synth preset");
      }
    }
    expect(count == (mode == MidiPitchTransitionRendering::PitchBend ? 2u : 3u),
           "only native portamento should add a physical continuation attack");
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
  const auto resolved = preparePerformance(PerformanceSequence{.tracks = {track}}, {bank}, {.onlyUsedInstruments = true});
  expect(resolved.voiceFor(noteById(resolved.performance(), PerformanceNoteId{0})).instrument ==
             InstrumentSelection{InstrumentHandle{0, 0}} &&
             resolved.voiceFor(noteById(resolved.performance(), PerformanceNoteId{1})).instrument ==
             InstrumentSelection{InstrumentHandle{0, 0}} &&
             resolved.voiceFor(noteById(resolved.performance(), PerformanceNoteId{2})).instrument ==
             InstrumentSelection{InstrumentAddress{7, 9}},
         "exact identity, fallback and external presets must resolve consistently");
  expect(std::ranges::count(resolved.performance().diagnostics, std::string("instrument-selection-fallback"),
                            &Diagnostic::code) == 1 &&
             std::ranges::count(resolved.performance().diagnostics, std::string("instrument-selection-conflict"),
                               &Diagnostic::code) == 2,
         "fallback and conflicting definitions must be reported once per source selection");
  expect(selectSynthBanks(resolved)[0].instruments.size() == 1,
         "filtering must use the same single definition as notes, leaving external presets external");
  const auto external = resolved.selectionFor(noteById(resolved.performance(), PerformanceNoteId{2}));
  expect(external.instrument == nullptr && external.address == InstrumentAddress{7, 9},
         "the resolved output view must distinguish external presets from owned instrument definitions");
  const PerformanceSequence leadingTie{.tracks = {{.events = {NotePerformanceEvent{.extendsPrevious = true}}}}};
  expect(preparePerformance(leadingTie, {SoundBankAsset{.instruments = {Instrument{}}}}).usedInstruments() == std::set{InstrumentHandle{0, 0}},
         "a leading tie with no preceding voice should still retain the selected instrument");
  const auto midi = renderMidiSequence(resolved);
  expect(!midi.tracks.empty(), "a missing companion instrument must not prevent standalone MIDI");
}

void layoutSeparatesCollisionsAndRejectsOverflow() {
  const SoundBankAsset bank{.instruments = {
      Instrument{.explicitAddress = InstrumentAddress{0, 5}, .identity = InstrumentIdentity{"a", 1}},
      Instrument{.explicitAddress = InstrumentAddress{0, 5}, .identity = InstrumentIdentity{"b", 1}}}};
  const auto resolved = preparePerformance({.diagnostics = {{.code = "source-warning"}}}, {bank});
  expect(resolved.valid() && resolved.selectionFor(InstrumentHandle{0, 0}).address != resolved.selectionFor(InstrumentHandle{0, 1}).address,
         "distinct definitions must receive distinct output slots even when their preferred addresses collide");
  const auto implicitZero = preparePerformance({}, {SoundBankAsset{
      .instruments = {Instrument{}, Instrument{.explicitAddress = InstrumentAddress{0, 0}}}}});
  expect(implicitZero.selectionFor(InstrumentHandle{0, 0}).address == InstrumentAddress{0, 0} &&
             implicitZero.selectionFor(InstrumentHandle{0, 1}).address == InstrumentAddress{0, 1},
         "an original instrument's implicit zero address is a preference, not an unassigned generated variant");
  const auto failed = preparePerformance({.diagnostics = {{.code = "source-warning"}}}, {bank}, {.firstBank = 128});
  const auto failedMidi = renderMidiSequence(failed);
  expect(!failed.valid() && failed.performance().diagnostics.size() == 2 &&
             selectSynthBanks(failed).empty() && failedMidi.tracks.empty() && failedMidi.diagnostics.size() == 2 &&
             failedMidi.diagnostics.front().code == "source-warning",
         "bank exhaustion must reject a partial plan while preserving earlier preparation diagnostics");
}

void preparedOwnershipSurvivesCopiesAndMoves() {
  const auto makePrepared = [](u32 firstBank) {
    SoundBankAsset bank{.instruments = {
        Instrument{.explicitAddress = InstrumentAddress{0, 0}, .pitchBendRangeCents = 700, .name = "Initial"},
        Instrument{.explicitAddress = InstrumentAddress{0, 5}, .name = "Owned"},
        Instrument{.explicitAddress = InstrumentAddress{0, 7}, .name = "Program only"}}};
    PerformanceSequence performance{.tracks = {PerformanceTrack{.events = {
        InstrumentPerformanceEvent{.instrument = InstrumentAddress{0, 5}},
        NotePerformanceEvent{.key = 60, .durationTicks = 4, .note = PerformanceNoteId{0}},
        InstrumentPerformanceEvent{.header = {.tick = 4}, .instrument = InstrumentAddress{0, 7}},
    }}}};
    return preparePerformance(std::move(performance), {std::move(bank)},
                              {.onlyUsedInstruments = true, .firstBank = firstBank});
  };
  auto original = makePrepared(23);
  const auto* instrument = original.selectionFor(InstrumentHandle{0, 1}).instrument;
  expect(!original.instrumentAddresses().contains(InstrumentHandle{0, 0}) &&
             original.initialInstrument()->pitchBendRangeCents == 700 &&
             original.selectionFor(InstrumentHandle{0, 2}).address == InstrumentAddress{23, 7} &&
             selectSynthBanks(original)[0].instruments.size() == 1,
         "initial pitch context needs no preset slot; a program-only selection needs a MIDI slot but no synth entry");
  std::vector<ResolvedPerformance> moved{original};
  auto copy = original;
  moved.push_back(std::move(copy));
  expect(moved.back().selectionFor(InstrumentHandle{0, 1}).instrument == instrument,
         "copies and moves must share the immutable bank allocation");
  original = makePrepared(61);
  for (size_t index = 0; index < 8; ++index) moved.push_back(original);
  for (size_t index = 0; index < 2; ++index) {
    const auto& retained = moved[index];
    const auto selection = retained.selectionFor(noteById(retained.performance(), PerformanceNoteId{0}));
    expect(selection.instrument->name == "Owned" && selection.address == InstrumentAddress{23, 5} &&
               retained.initialInstrument()->name == "Initial" && retained.bankMapping().at(0) == 23 &&
               retained.nextBank() == 24 && original.selectionFor(InstrumentHandle{0, 1}).address.bank == 61 &&
               !renderMidiSequence(retained).tracks.empty(),
           "prepared copies must retain their own banks and addresses after replacement, moves and growth");
  }
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
             prepared.voiceFor(noteById(prepared.performance(), PerformanceNoteId{0})).instrument ==
                 InstrumentSelection{InstrumentHandle{0, 1}} &&
             prepared.voiceFor(noteById(prepared.performance(), PerformanceNoteId{1})).instrument ==
                 InstrumentSelection{InstrumentAddress{0, 0}},
         "numeric fallback must search original definitions, not an earlier generated variant without an address");
  expect(prepared.selectionFor(InstrumentHandle{0, 1}).address != InstrumentAddress{0, 0},
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
                           .selection = {.sequence = AssetId{0}, .soundBanks = {AssetId{1}}},
      .inputs = {.banks = {{.bank = AssetId{1}}}}}};
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
      builder.collections = {{.id = CollectionId{0}, .name = "Collection", .selection = {
          .sequence = scenario == Scenario::NoSequence ? std::nullopt : std::optional{AssetId{0}},
          .soundBanks = {AssetId{1}}, .samplePools = {AssetId{2}}},
      .inputs = {.banks = {{.bank = AssetId{1}}}}}};
      const auto snapshot = builder.finish();
      auto binding = bindCollection(snapshot, CollectionId{0});
      expect(binding.collection.has_value(), "the lifetime fixture should bind successfully");
      const PreparedCollection original{std::move(*binding.collection), {
          .sequence = scenario == Scenario::Skipped || scenario == Scenario::NoSequence
              ? std::nullopt : std::optional{SequenceRenderOptions{}},
          .instruments = {.dynamicEnvelopes = true},
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
                 performance.selectionFor(noteById(performance.performance(), PerformanceNoteId{0})).instrument != nullptr,
             "resolved events must share the final banks while source events remain available for inspection");
      expect(!renderMidiSequence(performance).tracks.empty(),
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
  }}}}, banks, {.onlyUsedInstruments = true});
  // The input owns the selection returned by the builder; no temporary view is retained.
  SynthExportInput input{.soundBanks = selectSynthBanks(resolved), .samplePools = pools};
  auto prepared = prepareSynthData(input, sources);
  expect(prepared.instruments.size() == 2 && prepared.instruments[0].regions.size() == 4 &&
             prepared.instruments[1].regions.size() == 16 && prepared.diagnostics.size() == 1 &&
             prepared.instruments[0].regions[0].region.attenuationDb == 82 &&
             prepared.instruments[1].regions[0].region.attenuationDb == 80,
         "selected instruments must use their own bank's sampling budget, including its unselected regions");
  expect(prepared.instruments[0].address == resolved.selectionFor(InstrumentHandle{0, 0}).address &&
             prepared.instruments[1].address == resolved.selectionFor(InstrumentHandle{1, 0}).address &&
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

  const auto silent = preparePerformance({}, banks, {.onlyUsedInstruments = true});
  const auto empty = prepareSynthData({.soundBanks = selectSynthBanks(silent),
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
  builder.collections = {{.id = CollectionId{0}, .selection = {
      .sequence = AssetId{0}, .soundBanks = {AssetId{10}, AssetId{20}, AssetId{30}}, .samplePools = {AssetId{40}}},
      .inputs = {.banks = {{.bank = AssetId{10}}, {.bank = AssetId{20}}, {.bank = AssetId{30}}}}}};
  const auto snapshot = builder.finish();
  for (const auto format : {SynthExportFormat::SoundFont2, SynthExportFormat::Dls}) {
    const auto artifact = exportSoundBank(snapshot, sources, AssetId{20}, format, {.exportOnlyUsedInstruments = true});
    expect(!artifact.bytes.empty() && artifact.diagnostics.empty() && containsAscii(artifact.bytes, "Selected B") &&
               !containsAscii(artifact.bytes, "Selected A") && !containsAscii(artifact.bytes, "Unused A"),
           "exporting one used bank must exclude other banks, their instruments, and their sampling diagnostics");
  }
}

void soundingVoicesOwnSelectionsAndDeadlines() {
  PerformanceTrackFixture fixture{40};
  auto& track = fixture.track;
  auto& out = fixture.out;
  out.instrument(0, 5);
  const auto attack = out.note(NotePerformanceEvent{.key = 60, .durationTicks = 4,
                                                  .maximumDurationMilliseconds = 1000.0});
  out.at(2).instrument(0, 7);
  const auto other = out.at(2).note(NotePerformanceEvent{.key = 72, .durationTicks = 38, .lane = PerformanceLaneId{1}});
  // An explicit tie must find its voice even with an unrelated note interleaved.
  out.at(4).note(NotePerformanceEvent{.key = 60, .durationTicks = 4, .extendsPrevious = true, .note = attack});
  const auto continued = out.at(8).continueVoice(attack, NotePerformanceEvent{.key = 64, .durationTicks = 32});
  out.at(8).tempo(1000000);
  const SoundBankAsset bank{.instruments = {
      Instrument{.explicitAddress = InstrumentAddress{0, 5}}, Instrument{.explicitAddress = InstrumentAddress{0, 7}}}};
  const auto prepared = preparePerformance({.timebase = {.ppqn = 10}, .tracks = {track, track}}, {bank});
  const auto& first = noteById(prepared.performance(), attack);
  const auto& last = noteById(prepared.performance(), continued);
  const auto& overlap = noteById(prepared.performance(), other);
  const auto& voice = prepared.voiceFor(first);
  expect(&voice == &prepared.voiceFor(last) && &voice != &prepared.voiceFor(overlap) &&
             voice.endLimit == 14 &&
             !prepared.voiceFor(overlap).endLimit &&
             prepared.selectionFor(last).address.program == 5 && prepared.selectionFor(overlap).address.program == 7,
         "one voice must own the attack instrument and tempo-aware deadline across interleaved segments");
  const auto secondTrackNotes = eventsOfType<NotePerformanceEvent>(prepared.performance().tracks[1]);
  expect(secondTrackNotes.front()->voice != first.voice,
         "track-local source note IDs must become distinct prepared voices across tracks");
}

void canceledOrDelayedPitchMotionDoesNotJoinAttacks() {
  for (const bool delayed : {false, true}) {
    PerformanceTrackFixture fixture{};
    auto& track = fixture.track;
    auto& out = fixture.out;
    out.instrument(0, 5);
    const auto first = out.note(60, 1.0, 4);
    out.at(4).instrument(0, 7);
    const auto second = out.at(4).note(64, 1.0, 4);
    auto slide = out.at(delayed ? 5 : 4).pitchSlide(second, 60, 64, 2);
    if (!delayed) slide.stop(out.at(4));
    const auto prepared = preparePerformance({.tracks = {track}});
    expect(noteById(prepared.performance(), first).voice != noteById(prepared.performance(), second).voice &&
               prepared.selectionFor(noteById(prepared.performance(), second)).address.program == 7,
           "pitch motion must never join independently emitted attacks");
  }
}

void pitchEditsPreserveDeclaredVoiceOwnership() {
  for (const bool middleContinues : {false, true}) {
    for (const bool followsMiddle : {false, true}) {
      PerformanceTrackFixture fixture{};
      auto& track = fixture.track;
      auto& out = fixture.out;
      out.instrument(0, 5);
      const auto first = out.note(NotePerformanceEvent{.key = 60, .durationTicks = 16,
                                                     .maximumDurationMilliseconds = 500.0});
      out.at(4).instrument(0, 7);
      NotePerformanceEvent event{.key = 64, .durationTicks = 12};
      const auto middle = middleContinues ? out.at(4).continueVoice(first, event) : out.at(4).note(event);
      auto slide = out.at(4).pitchSlide(middle, 60, 64, 4);
      const auto last = out.at(8).continueVoice(followsMiddle ? middle : first,
                                               NotePerformanceEvent{.key = 67, .durationTicks = 8});

      // Lookahead can cancel pitch motion after later continuations exist.
      // Voice ownership was declared by the note operation and cannot change here.
      slide.stop(out.at(4));
      slide.clear();
      const auto prepared = preparePerformance({.timebase = {.ppqn = 10}, .tracks = {track}});
      const auto& attack = noteById(prepared.performance(), first);
      const auto& second = noteById(prepared.performance(), middle);
      const auto& continued = noteById(prepared.performance(), last);
      expect((attack.voice == second.voice) == middleContinues &&
                 prepared.selectionFor(second).address.program == (middleContinues ? 5u : 7u),
             "canceling pitch motion must preserve the source's attack or continuation decision");
      const bool retainsFirst = middleContinues || !followsMiddle;
      expect(continued.voice == (followsMiddle ? second.voice : attack.voice) &&
                 prepared.selectionFor(continued).address.program == (retainsFirst ? 5u : 7u) &&
                 prepared.voiceFor(continued).endLimit == (retainsFirst ? std::optional<u64>{10} : std::nullopt),
             "pitch edits must not relabel later branches or change their instrument and deadline");
    }
  }
}

}  // namespace

void runResolvedInstrumentTests() {
  pitchEditsPreserveDeclaredVoiceOwnership();
  soundingVoicesOwnSelectionsAndDeadlines();
  canceledOrDelayedPitchMotionDoesNotJoinAttacks();
  resolvedVariantsShareAddressesWithBothSynthWriters();
  resolutionChoosesAndDiagnosesOneDefinition();
  layoutSeparatesCollisionsAndRejectsOverflow();
  preparedOwnershipSurvivesCopiesAndMoves();
  laterSourceSelectionsCannotFindGeneratedVariants();
  collectionExportsRequireACompanionForVariants();
  completedCollectionsRetainInputsAcrossRenderingOutcomes();
  synthSelectionsPreserveBankSamplingAndSampleOwners();
}
