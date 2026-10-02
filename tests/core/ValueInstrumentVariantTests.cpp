/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../MidiTestSupport.h"
#include "../PerformanceTestSupport.h"
#include "../TestSupport.h"
#include "SynthExportTestSupport.h"

#include "value/export/ResolvedPerformance.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/synth/SynthExportData.h"

#include <algorithm>
#include <array>

using namespace vgmtrans::core;

namespace {

PerformanceEventHeader eventHeader(u64 tick, u64 sequence) {
  return PerformanceEventHeader{
      .track = TrackId{0},
      .tick = tick,
      .sequence = sequence,
  };
}

Instrument testInstrument(u32 key, Envelope first, std::optional<Envelope> second = std::nullopt) {
  Instrument instrument{
      .identity = InstrumentIdentity{.domain = "dynamic-envelope-test", .key = key},
      .name = "Instrument " + std::to_string(key),
      .regions = {Region{.envelope = std::move(first)}},
  };
  if (second) {
    instrument.regions.push_back(Region{.envelope = std::move(*second)});
  }
  return instrument;
}

PerformanceSequence sequenceWithEvents(std::vector<PerformanceEvent> events, u64 endTick = 64) {
  return PerformanceSequence{
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .endTick = endTick,
          .events = std::move(events),
      }},
  };
}

InstrumentHandle selectedHandleForNote(const ResolvedPerformance& prepared, PerformanceNoteId note) {
  for (const auto& event : prepared.performance().tracks.front().events) {
    if (const auto* value = std::get_if<NotePerformanceEvent>(&event); value && value->note == note) {
      return std::get<InstrumentHandle>(prepared.voiceFor(*value).instrument);
    }
  }
  throw std::runtime_error("Test note instrument was not found");
}

InstrumentAddress selectedAddressForNote(const ResolvedPerformance& prepared, PerformanceNoteId note) {
  return prepared.selectionFor(selectedHandleForNote(prepared, note)).address;
}

size_t selectedInstrumentForNote(const ResolvedPerformance& prepared, PerformanceNoteId note) {
  return selectedHandleForNote(prepared, note).instrument;
}

void instrumentSelectionUsesOneResolutionPolicy() {
  Instrument addressMatch = testInstrument(5, {});
  addressMatch.identity->domain = "other";
  addressMatch.name = "Address match";
  Instrument exactMatch = testInstrument(5, {});
  exactMatch.explicitAddress = InstrumentAddress{.program = 7};
  exactMatch.name = "Identity match";
  const SoundBankAsset bank{.instruments = {addressMatch, exactMatch, addressMatch, exactMatch}};

  struct SelectionCase {
    decltype(InstrumentPerformanceEvent::instrument) selection;
    std::optional<InstrumentAddress> noteAddress;
    size_t firstMatch;
  };
  const std::array cases{
      SelectionCase{InstrumentIdentity{.domain = "dynamic-envelope-test", .key = 5}, {}, 1},
      SelectionCase{InstrumentIdentity{.domain = "missing", .key = 5}, {}, 0},
      SelectionCase{InstrumentAddress{.program = 5}, {}, 0},
      SelectionCase{InstrumentAddress{.program = 5}, InstrumentAddress{.program = 7}, 1},
  };
  for (const auto& test : cases) {
    const InstrumentPerformanceEvent selection{.header = eventHeader(0, 0), .instrument = test.selection};
    const auto performance = sequenceWithEvents({
        selection,
        EnvelopePerformanceEvent{
            .header = eventHeader(0, 1),
            .update = EnvelopeUpdate::set(Envelope{.attackSeconds = 0.25}, EnvelopeFields::Attack)},
        NotePerformanceEvent{.header = eventHeader(0, 2),
                             .key = 60,
                             .durationTicks = 4,
                             .instrument = test.noteAddress,
                             .note = PerformanceNoteId{1}},
    });
    const std::array<const SoundBankAsset*, 2> inputs{nullptr, &bank};
    const auto resolved = prepareTestPerformance(performance, inputs, {.onlyUsedInstruments = true});
    const auto selected = selectSynthBanks(resolved);
    const size_t first = test.firstMatch;
    expect(selected.size() == 2 && selected[0].instruments.empty() && selected[1].instruments.size() == 1 &&
               selected[1].instruments[0].instrument == &resolved.soundBanks()[1].instruments[first],
           "all consumers must use the first resolved definition, including numeric fallback and note overrides");

    std::array banks{bank};
    const auto materialized = preparePerformance(performance, {banks.begin(), banks.end()}, InstrumentPreparationOptions{.dynamicEnvelopes = true});
  const auto& preparedBanks = materialized.soundBanks();
    expect(
        std::ranges::any_of(materialized.performance().diagnostics, [](const Diagnostic& diagnostic) {
          return diagnostic.code == "instrument-selection-conflict";
        }) && preparedBanks[0].instruments.size() == 5 &&
            preparedBanks[0].instruments.back().name == bank.instruments[first].name + " [dynamic envelope]" &&
            selectedInstrumentForNote(materialized, PerformanceNoteId{1}) == 4,
        "variant materialization must prefer exact identities, fall back to addresses, and clone only the first match");
  }
}

void dynamicEnvelopeMaterializationIsIncrementalAndDeduplicated() {
  const Envelope firstBase{
      .attackSeconds = 1.0,
      .decaySeconds = 3.0,
      .releaseSeconds = 5.0,
      .sustainAmplitude = 0.6,
  };
  const Envelope secondBase{
      .attackSeconds = 2.0,
      .decaySeconds = 4.0,
      .releaseSeconds = 7.0,
      .sustainAmplitude = 0.4,
  };
  std::vector<SoundBankAsset> sets{SoundBankAsset{
      .instruments = {testInstrument(5, firstBase, secondBase)},
  }};
  // Native responses must be evaluated before attack-time overrides, and must
  // not survive to overwrite the variant during synth export.
  sets[0].instruments[0].regions[0].envelope = {};
  sets[0].instruments[0].regions[0].response.evaluate = [firstBase](Region& region, u8, u8) {
    region.envelope = firstBase;
  };
  sets.push_back(sets.front());
  sets.back().instruments[0].identity->key = 99;

  auto performance = sequenceWithEvents({
      InstrumentPerformanceEvent{
          .header = eventHeader(0, 0),
          .instrument = InstrumentIdentity{.domain = "dynamic-envelope-test", .key = 5},
      },
      EnvelopePerformanceEvent{
          .header = eventHeader(0, 1),
          .update = EnvelopeUpdate::set(Envelope{.attackSeconds = 0.25}, EnvelopeFields::Attack),
      },
      NotePerformanceEvent{
          .header = eventHeader(0, 2),
          .key = 60,
          .durationTicks = 4,
          .note = PerformanceNoteId{1},
      },
      NotePerformanceEvent{
          .header = eventHeader(8, 3),
          .key = 62,
          .durationTicks = 4,
          .note = PerformanceNoteId{2},
      },
      EnvelopePerformanceEvent{
          .header = eventHeader(16, 4),
          .update = EnvelopeUpdate::set(Envelope{.releaseSeconds = 2.0}, EnvelopeFields::Release),
      },
      NotePerformanceEvent{
          .header = eventHeader(16, 5),
          .key = 64,
          .durationTicks = 4,
          .note = PerformanceNoteId{3},
      },
      EnvelopePerformanceEvent{
          .header = eventHeader(24, 6),
          .update = EnvelopeUpdate::restore(EnvelopeFields::Attack),
      },
      NotePerformanceEvent{
          .header = eventHeader(24, 7),
          .key = 65,
          .durationTicks = 4,
          .note = PerformanceNoteId{4},
      },
      EnvelopePerformanceEvent{
          .header = eventHeader(32, 8),
          .update = EnvelopeUpdate::set(Envelope{}, EnvelopeFields::Release),
      },
      NotePerformanceEvent{
          .header = eventHeader(32, 9),
          .key = 67,
          .durationTicks = 4,
          .note = PerformanceNoteId{5},
      },
      EnvelopePerformanceEvent{
          .header = eventHeader(40, 10),
          .update = EnvelopeUpdate::restore(),
      },
      NotePerformanceEvent{
          .header = eventHeader(40, 11),
          .key = 69,
          .durationTicks = 4,
          .note = PerformanceNoteId{6},
      },
  });

  const auto materialized = preparePerformance(performance, {sets.begin(), sets.end()}, InstrumentPreparationOptions{.dynamicEnvelopes = true});
  const auto& preparedBanks = materialized.soundBanks();
  expect(materialized.performance().diagnostics.empty(), "valid future-note envelope updates should not warn");
  expect(preparedBanks[0].instruments.size() == 5, "only four distinct effective envelopes should create variants");
  expect(preparedBanks[1].instruments.size() == 1 && preparedBanks[1].instruments[0].regions[0].response.evaluate,
         "banks that need no variants should retain their native responses");

  const size_t first = selectedInstrumentForNote(materialized, PerformanceNoteId{1});
  const size_t duplicate = selectedInstrumentForNote(materialized, PerformanceNoteId{2});
  const size_t combined = selectedInstrumentForNote(materialized, PerformanceNoteId{3});
  const size_t releaseOnly = selectedInstrumentForNote(materialized, PerformanceNoteId{4});
  const size_t cleared = selectedInstrumentForNote(materialized, PerformanceNoteId{5});
  const size_t restored = selectedInstrumentForNote(materialized, PerformanceNoteId{6});
  expect(first == duplicate, "repeated notes under one envelope state should share a materialized variant");
  expect(combined != first && releaseOnly != combined,
         "incremental changes should materialize only their distinct effective states");
  expect(restored == 0, "restoring inheritance should select the original instrument");
  const auto& clearedVariant = preparedBanks[0].instruments[cleared];
  expect(!clearedVariant.regions[0].envelope.releaseSeconds && !clearedVariant.regions[1].envelope.releaseSeconds,
         "setting a field with an absent value should explicitly clear that field");

  const auto& attackVariant = preparedBanks[0].instruments[first];
  expect(!attackVariant.regions[0].response.evaluate && !preparedBanks[0].instruments[0].regions[0].response.evaluate,
         "materialized regions should have no pending native response");
  expect(attackVariant.regions[0].envelope.attackSeconds == 0.25 &&
             attackVariant.regions[1].envelope.attackSeconds == 0.25,
         "a partial attack update should apply to every region");
  expect(attackVariant.regions[0].envelope.releaseSeconds == 5.0 &&
             attackVariant.regions[1].envelope.releaseSeconds == 7.0,
         "untouched fields should continue to inherit each region's own envelope");
}

void dynamicEnvelopeInstrumentSelectionControlsOverrideCarry() {
  std::vector<SoundBankAsset> sets{SoundBankAsset{
      .instruments =
          {
              testInstrument(0, Envelope{.attackSeconds = 1.0}),
              testInstrument(1, Envelope{.attackSeconds = 2.0}),
          },
  }};
  auto performance = sequenceWithEvents({
      InstrumentPerformanceEvent{
          .header = eventHeader(0, 0),
          .instrument = InstrumentIdentity{.domain = "dynamic-envelope-test", .key = 0},
      },
      EnvelopePerformanceEvent{
          .header = eventHeader(0, 1),
          .update = EnvelopeUpdate::set(Envelope{.attackSeconds = 0.1}, EnvelopeFields::Attack),
      },
      NotePerformanceEvent{
          .header = eventHeader(0, 2),
          .key = 60,
          .durationTicks = 4,
          .note = PerformanceNoteId{1},
      },
      InstrumentPerformanceEvent{
          .header = eventHeader(8, 3),
          .instrument = InstrumentIdentity{.domain = "dynamic-envelope-test", .key = 1},
      },
      NotePerformanceEvent{
          .header = eventHeader(8, 4),
          .key = 62,
          .durationTicks = 4,
          .note = PerformanceNoteId{2},
      },
      EnvelopePerformanceEvent{
          .header = eventHeader(16, 5),
          .update = EnvelopeUpdate::set(Envelope{.attackSeconds = 0.2}, EnvelopeFields::Attack),
      },
      InstrumentPerformanceEvent{
          .header = eventHeader(16, 6),
          .instrument = InstrumentIdentity{.domain = "dynamic-envelope-test", .key = 0},
          .envelopeMode = InstrumentEnvelopeMode::PreserveDynamicOverride,
      },
      NotePerformanceEvent{
          .header = eventHeader(16, 7),
          .key = 64,
          .durationTicks = 4,
          .note = PerformanceNoteId{3},
      },
  });

  const auto materialized = preparePerformance(performance, {sets.begin(), sets.end()}, InstrumentPreparationOptions{.dynamicEnvelopes = true});
  const auto& preparedBanks = materialized.soundBanks();
  const size_t first = selectedInstrumentForNote(materialized, PerformanceNoteId{1});
  const size_t preserved = selectedInstrumentForNote(materialized, PerformanceNoteId{3});
  expect(first >= 2, "a dynamic override should materialize a variant before an instrument change");
  expect(selectedInstrumentForNote(materialized, PerformanceNoteId{2}) == 1,
         "ordinary selection must resolve to the new instrument's native envelope, not a variant");
  expect(preserved >= 2 && preparedBanks[0].instruments[preserved].regions[0].envelope.attackSeconds == 0.2,
         "an explicit preserve transition should carry the dynamic override to the selected instrument");
}

void dynamicEnvelopeActiveVoiceLimitationIsExplicit() {
  std::vector<SoundBankAsset> sets{SoundBankAsset{
      .instruments = {testInstrument(0, Envelope{.attackSeconds = 1.0})},
  }};
  auto performance = sequenceWithEvents({
      NotePerformanceEvent{
          .header = eventHeader(0, 0),
          .key = 60,
          .durationTicks = 20,
          .note = PerformanceNoteId{1},
      },
      EnvelopePerformanceEvent{
          .header = eventHeader(5, 1),
          .update = EnvelopeUpdate::set(Envelope{.attackSeconds = 0.1}, EnvelopeFields::Attack),
          .scope = VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks,
      },
      NotePerformanceEvent{
          .header = eventHeader(20, 2),
          .key = 62,
          .durationTicks = 4,
          .note = PerformanceNoteId{2},
      },
  });

  const auto materialized = preparePerformance(performance, {sets.begin(), sets.end()}, InstrumentPreparationOptions{.dynamicEnvelopes = true});
  const auto& preparedBanks = materialized.soundBanks();
  expect(std::ranges::any_of(
             materialized.performance().diagnostics,
             [](const Diagnostic& diagnostic) { return diagnostic.code == "dynamic-envelope-active-voice"; }),
         "an active-voice envelope command should report the static-variant limitation");
  const size_t later = selectedInstrumentForNote(materialized, PerformanceNoteId{2});
  expect(later == 1, "a combined active/future command should still affect the next fresh attack");
}

void dynamicEnvelopeMidiUsesLoweredPerformanceAndReturnsToBankZero(MidiPitchTransitionRendering rendering) {
  std::vector<Instrument> instruments;
  instruments.reserve(128);
  for (u32 program = 0; program < 128; ++program) {
    instruments.push_back(testInstrument(program, Envelope{.attackSeconds = 1.0}));
  }
  std::vector<SoundBankAsset> sets{SoundBankAsset{
      .instruments = std::move(instruments),
  }};

  auto performance = sequenceWithEvents({
      InstrumentPerformanceEvent{
          .header = eventHeader(0, 0),
          .instrument = InstrumentIdentity{.domain = "dynamic-envelope-test", .key = 0},
      },
      EnvelopePerformanceEvent{
          .header = eventHeader(0, 1),
          .update = EnvelopeUpdate::set(Envelope{.attackSeconds = 0.1}, EnvelopeFields::Attack),
      },
      NotePerformanceEvent{
          .header = eventHeader(0, 2),
          .key = 60,
          .durationTicks = 4,
          .note = PerformanceNoteId{1},
          .voice = PerformanceVoiceId{1},
      },
      NotePerformanceEvent{
          .header = eventHeader(4, 3),
          .key = 60,
          .durationTicks = 4,
          .extendsPrevious = true,
          .note = PerformanceNoteId{1},
          .voice = PerformanceVoiceId{1},
      },
      EnvelopePerformanceEvent{
          .header = eventHeader(6, 4),
          .update = EnvelopeUpdate::restore(),
      },
      NotePerformanceEvent{
          .header = eventHeader(8, 5),
          .key = 62,
          .durationTicks = 2,
          .note = PerformanceNoteId{2},
          .voice = PerformanceVoiceId{1},
      },
      NotePerformanceEvent{
          .header = eventHeader(10, 7),
          .key = 64,
          .durationTicks = 4,
          .note = PerformanceNoteId{3},
          .voice = PerformanceVoiceId{3},
      },
  });
  performance.tracks[0].automations.push_back(PerformanceAutomation{
      .header = eventHeader(8, 6),
      .intent =
          PitchTransitionIntent{
              .note = PerformanceNoteId{2}, .startKey = 60, .targetKey = 62},
      .realization = {.startTick = 8, .endTick = 8},
  });

  const auto materialized = preparePerformance(performance, {sets.begin(), sets.end()}, InstrumentPreparationOptions{.dynamicEnvelopes = true});
  const auto& preparedBanks = materialized.soundBanks();
  expect(preparedBanks[0].instruments.size() == 129 &&
             materialized.selectionFor(InstrumentHandle{0, 128}).address == InstrumentAddress{.bank = 1, .program = 0},
         "the allocator should move to the next free bank after bank zero is occupied");

  const MidiSequence midi = renderMidiSequence(materialized, {.pitchTransitions = rendering});
  std::vector<std::pair<u64, u16>> banks;
  for (const auto& event : midi.tracks[0].events) {
    if (const auto* bank = std::get_if<BankSelect>(&event.payload)) {
      banks.emplace_back(event.tick, bank->bank);
    }
  }
  expect(std::ranges::find(banks, std::pair<u64, u16>{0, 1}) != banks.end(),
         "the first fresh note should select the generated logical bank");
  expect(std::ranges::find(banks, std::pair<u64, u16>{10, 0}) != banks.end(),
         "restoring the base envelope should explicitly return MIDI to bank zero");
  expect(std::ranges::any_of(midi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* program = midiChannelMessage(event, MidiChannelMessageKind::ProgramChange);
                               return program != nullptr && event.tick == 10 && program->value == 0;
                             }),
         "a bank change should reselect the program even when its number is unchanged");
  expect(std::ranges::none_of(banks, [](const auto& bank) { return bank.first == 4 || bank.first == 8; }),
         "same-pitch and key-changing ties should keep their attack-time instrument");
}

void variantAddressesRespectExportProjectionsAndExhaustion() {
  std::vector<SoundBankAsset> sets{SoundBankAsset{
      .instruments = {testInstrument(0, Envelope{.attackSeconds = 1.0})},
  }};
  std::vector<PerformanceEvent> events;
  for (u32 address = 0; address < 128 * 128; ++address) {
    if (address == 1 || address == 128 || address == 256 || address == 384 || address == 511 || address == 16383) {
      continue;
    }
    events.push_back(
        InstrumentPerformanceEvent{.instrument = InstrumentAddress{.bank = address / 128, .program = address % 128}});
  }
  // Reserve holes through the MIDI/DLS bank projection, SF2's packed bank,
  // and the exporters' clamped program respectively.
  events.push_back(InstrumentPerformanceEvent{.instrument = InstrumentAddress{.bank = 129, .program = 0}});
  events.push_back(NotePerformanceEvent{.instrument = InstrumentAddress{512, 0}});
  events.push_back(InstrumentPerformanceEvent{.instrument = InstrumentAddress{.bank = 3, .program = 255}});
  events.push_back(InstrumentPerformanceEvent{
      .instrument = InstrumentIdentity{.domain = "dynamic-envelope-test", .key = 0},
  });
  for (u32 note = 1; note <= 4; ++note) {
    events.push_back(EnvelopePerformanceEvent{
        .header = eventHeader(note * 4, events.size()),
        .update = EnvelopeUpdate::set(Envelope{.attackSeconds = note + 1.0}, EnvelopeFields::Attack),
    });
    events.push_back(NotePerformanceEvent{
        .header = eventHeader(note * 4, events.size()),
        .key = 60,
        .durationTicks = 1,
        .note = PerformanceNoteId{note},
    });
  }
  const auto resolved = preparePerformance(sequenceWithEvents(std::move(events)), std::move(sets),
                                            {.dynamicEnvelopes = true});
  expect(resolved.soundBanks()[0].instruments.size() == 5 && !resolved.soundBanks()[0].instruments.back().explicitAddress,
         "variant preparation must be independent of available output addresses");
  expect(!resolved.valid() && resolved.performance().diagnostics.size() == 1 &&
             resolved.performance().diagnostics[0].code == "instrument-addresses-exhausted" &&
             renderMidiSequence(resolved).tracks.empty(),
         "address exhaustion must reject the shared output plan instead of silently dropping one envelope variant");

}

void signedStereoMaterializationUsesAttackTimeVariants() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "signed-stereo.pcm"}, std::vector<u8>{0x00, 0x80, 0xe8, 0x03});
  Instrument instrument = testInstrument(0, {});
  instrument.regions[0].sample = SampleRef::resolved(AssetId{10}, 0);
  instrument.regions[0].pan = 0.0;
  instrument.regions[0].response.evaluate = [](Region& region, u8, u8) { region.pan = 0.5; };
  std::vector<SoundBankAsset> sets{SoundBankAsset{
      .metadata = AssetMetadata{.id = AssetId{10}},
      .instruments = {std::move(instrument)},
      .localSamples = SamplePool{.samples = {Sample{
                                     .codec = AudioCodec::PcmS16,
                                     .encodedData = SourceRange{.source = source, .size = 4},
                                     .sampleRate = 32000,
                                 }}},
  }};
  auto performance = sequenceWithEvents({
      StereoBalancePerformanceEvent{
          .header = eventHeader(0, 0),
          .leftGain = -1.0,
          .rightGain = 1.0,
      },
      ChannelPanPerformanceEvent{
          .header = eventHeader(0, 1),
          .position = 0.25,
      },
      EnvelopePerformanceEvent{
          .header = eventHeader(0, 2),
          .update = EnvelopeUpdate::set(Envelope{.attackSeconds = 0.25}, EnvelopeFields::Attack),
      },
      NotePerformanceEvent{
          .header = eventHeader(0, 3),
          .key = 60,
          .durationTicks = 10,
          .note = PerformanceNoteId{1},
      },
      ChannelPanPerformanceEvent{
          .header = {.sourceCommand = {TrackId{0}, CommandId{4}}, .track = TrackId{0}, .tick = 4, .sequence = 4},
          .position = 0.75,
      },
      StereoBalancePerformanceEvent{
          .header = {.sourceCommand = {TrackId{1}, CommandId{4}}, .track = TrackId{0}, .tick = 5, .sequence = 5},
          .leftGain = 1.0,
          .rightGain = 1.0,
      },
  });

  const auto materialized = preparePerformance(
      performance, {sets.begin(), sets.end()}, InstrumentPreparationOptions{.dynamicEnvelopes = true, .signedStereo = true, .onlyUsedInstruments = true});
  const auto& preparedBanks = materialized.soundBanks();
  expect(preparedBanks[0].instruments.size() == 2, "attack-time state should create one combined instrument variant");
  expect(std::ranges::none_of(materialized.performance().tracks[0].events,
                              [](const PerformanceEvent& event) {
                                return std::holds_alternative<StereoBalancePerformanceEvent>(event) ||
                                       std::holds_alternative<ChannelPanPerformanceEvent>(event);
                              }),
         "materialized stereo state should not also be emitted as channel-wide MIDI pan");
  expect(std::ranges::count_if(
             materialized.performance().diagnostics,
             [](const Diagnostic& diagnostic) { return diagnostic.code == "signed-stereo-active-voice"; }) == 2,
         "phase and pan commands from different source tracks should each report the attack-time limitation");

  const size_t inverted = selectedInstrumentForNote(materialized, PerformanceNoteId{1});
  const auto& invertedRegions = preparedBanks[0].instruments[inverted].regions;
  expect(invertedRegions.size() == 2 && invertedRegions[0].pan == 0.0 && invertedRegions[1].pan == 1.0 &&
             invertedRegions[0].invertSamplePhase && !invertedRegions[1].invertSamplePhase &&
             invertedRegions[0].envelope.attackSeconds == 0.25 &&
             invertedRegions[0].attenuationDb < invertedRegions[1].attenuationDb,
         "the combined variant should retain its envelope and bake signed pan into two hard-panned layers");

  const auto selectedInstruments = selectSynthBanks(materialized);
  const auto prepared = prepareSynthData(
      SynthExportInput{
          .soundBanks = selectedInstruments,
          .filterSamplesToReferencedInstruments = true,
      },
      sources);
  const auto phaseInverted = std::ranges::find_if(
      prepared.samples, [](const DecodedSynthSample& sample) { return sample.name.ends_with(" [inverted]"); });
  expect(prepared.samples.size() == 2 && phaseInverted != prepared.samples.end() &&
             phaseInverted->decoded.pcm == std::vector<s16>{32767, -1000},
         "shared synth preparation should create one saturated phase-inverted PCM copy");
}

void variantLaneStateSurvivesInstrumentChanges() {
  std::array banks{SoundBankAsset{.instruments = {testInstrument(0, Envelope{.attackSeconds = 1.0})}}};
  std::vector<PerformanceEvent> events{
      StereoBalancePerformanceEvent{.header = eventHeader(0, 0), .leftGain = -1.0},
  };
  for (u32 lane = 0; lane < 2; ++lane) {
    events.emplace_back(ChannelPanPerformanceEvent{
        .header = eventHeader(0, events.size()), .position = double(lane), .lane = PerformanceLaneId{lane}});
    events.emplace_back(EnvelopePerformanceEvent{
        .header = eventHeader(0, events.size()),
        .update = EnvelopeUpdate::set(Envelope{.attackSeconds = 0.25 + 0.5 * lane}, EnvelopeFields::Attack),
        .lane = PerformanceLaneId{lane},
    });
    events.emplace_back(NotePerformanceEvent{.header = eventHeader(0, events.size()),
                                             .key = 60,
                                             .durationTicks = lane == 0 ? 20u : 2u,
                                             .note = PerformanceNoteId{lane + 1},
                                             .lane = PerformanceLaneId{lane}});
  }
  events.emplace_back(InstrumentPerformanceEvent{.header = eventHeader(4, events.size())});
  events.emplace_back(StereoBalancePerformanceEvent{.header = eventHeader(4, events.size())});
  events.emplace_back(ChannelPanPerformanceEvent{
      .header = eventHeader(5, events.size()), .position = 0.5, .lane = PerformanceLaneId{1}});
  events.emplace_back(EnvelopePerformanceEvent{
      .header = eventHeader(5, events.size()),
      .update = EnvelopeUpdate::set(Envelope{.attackSeconds = 0.1}, EnvelopeFields::Attack),
      .scope = VoiceEnvelopeScope::ActiveVoices,
  });
  for (u32 lane = 0; lane < 2; ++lane) {
    events.emplace_back(NotePerformanceEvent{.header = eventHeader(20, events.size()),
                                             .key = 60,
                                             .durationTicks = 4,
                                             .note = PerformanceNoteId{lane + 3},
                                             .lane = PerformanceLaneId{lane}});
  }

  const auto result = preparePerformance(sequenceWithEvents(std::move(events)), {banks.begin(), banks.end()}, {.dynamicEnvelopes = true, .signedStereo = true});
  const auto& preparedBanks = result.soundBanks();
  for (u32 lane = 0; lane < 2; ++lane) {
    const auto& before = preparedBanks[0].instruments[selectedInstrumentForNote(result, PerformanceNoteId{lane + 1})];
    const auto& after = preparedBanks[0].instruments[selectedInstrumentForNote(result, PerformanceNoteId{lane + 3})];
    expect(before.regions.size() == 1 && before.regions[0].pan == double(lane) &&
               before.regions[0].envelope.attackSeconds == 0.25 + 0.5 * lane,
           "simultaneous lanes must retain independent pan and envelope overrides");
    expect(after.regions.size() == lane + 1 && after.regions[0].pan == 0.0 &&
               after.regions[0].envelope.attackSeconds == 1.0,
           "instrument changes must reset every envelope override while retaining lane pan");
  }
  expect(result.performance().diagnostics.size() == 2 && result.performance().diagnostics[0].code == "signed-stereo-active-voice" &&
             result.performance().diagnostics[1].code == "dynamic-envelope-active-voice",
         "instrument changes must retain sounding voices, and inactive lanes must not report active-voice warnings");
}

void signedStereoMaterializationLeavesOrdinaryTracksAlone() {
  std::vector<SoundBankAsset> sets{SoundBankAsset{
      .instruments = {testInstrument(0, {})},
  }};
  sets[0].instruments[0].regions[0].response = {
      .keyDependent = true, .velocityDependent = true, .evaluate = [](Region&, u8, u8) {}};
  const auto performance = sequenceWithEvents({
      ChannelPanPerformanceEvent{
          .header = eventHeader(0, 0),
          .position = 0.25,
      },
      NotePerformanceEvent{
          .header = eventHeader(0, 1),
          .key = 60,
          .durationTicks = 4,
          .note = PerformanceNoteId{1},
      },
  });

  const auto materialized = preparePerformance(
      performance, {sets.begin(), sets.end()}, InstrumentPreparationOptions{.dynamicEnvelopes = true, .signedStereo = true});
  const auto& preparedBanks = materialized.soundBanks();
  expect(materialized.performance().diagnostics.empty() && preparedBanks[0].instruments[0].regions.size() == 1 &&
             preparedBanks[0].instruments[0].regions[0].response.evaluate,
         "ordinary notes should not sample native responses or warn about synth table budgets");
  expect(preparedBanks[0].instruments.size() == 1 &&
             std::holds_alternative<ChannelPanPerformanceEvent>(materialized.performance().tracks[0].events[0]) &&
             selectedAddressForNote(materialized, PerformanceNoteId{1}) == InstrumentAddress{},
         "ordinary panned tracks resolve their original instrument without creating variants");
}

}  // namespace

void runValueInstrumentVariantTests() {
  instrumentSelectionUsesOneResolutionPolicy();
  dynamicEnvelopeMaterializationIsIncrementalAndDeduplicated();
  dynamicEnvelopeInstrumentSelectionControlsOverrideCarry();
  dynamicEnvelopeActiveVoiceLimitationIsExplicit();
  for (const auto rendering : {MidiPitchTransitionRendering::PitchBend, MidiPitchTransitionRendering::Portamento}) {
    dynamicEnvelopeMidiUsesLoweredPerformanceAndReturnsToBankZero(rendering);
  }
  variantAddressesRespectExportProjectionsAndExhaustion();
  signedStereoMaterializationUsesAttackTimeVariants();
  variantLaneStateSurvivesInstrumentChanges();
  signedStereoMaterializationLeavesOrdinaryTracksAlone();
}
