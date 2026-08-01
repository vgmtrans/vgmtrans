/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "value/export/DynamicEnvelope.h"

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
  std::vector<InstrumentSetAsset> sets{InstrumentSetAsset{
      .instruments = {testInstrument(5, firstBase, secondBase)},
  }};

  auto performance = sequenceWithEvents({
      InstrumentPerformanceEvent{
          .header = eventHeader(0, 0),
          .sourceInstrument = InstrumentIdentity{.domain = "dynamic-envelope-test", .key = 5},
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

  const auto materialized = materializeDynamicEnvelopes(performance, sets);
  expect(materialized.diagnostics.empty(), "valid future-note envelope updates should not warn");
  expect(materialized.variantCount == 4 && sets[0].instruments.size() == 5,
         "only four distinct effective envelopes should create variants");

  const auto* first = materialized.instruments.selectionFor(TrackId{0}, PerformanceNoteId{1});
  const auto* duplicate = materialized.instruments.selectionFor(TrackId{0}, PerformanceNoteId{2});
  const auto* combined = materialized.instruments.selectionFor(TrackId{0}, PerformanceNoteId{3});
  const auto* releaseOnly = materialized.instruments.selectionFor(TrackId{0}, PerformanceNoteId{4});
  const auto* cleared = materialized.instruments.selectionFor(TrackId{0}, PerformanceNoteId{5});
  const auto* restored = materialized.instruments.selectionFor(TrackId{0}, PerformanceNoteId{6});
  expect(first != nullptr && duplicate != nullptr && combined != nullptr && releaseOnly != nullptr &&
             cleared != nullptr && restored != nullptr,
      "every fresh note should receive a concrete prepared-instrument assignment");
  expect(first->instrument == duplicate->instrument,
         "repeated notes under one envelope state should share a materialized variant");
  expect(combined->instrument != first->instrument && releaseOnly->instrument != combined->instrument,
         "incremental changes should materialize only their distinct effective states");
  expect(restored->instrument == PreparedInstrumentRef{.set = 0, .instrument = 0},
         "restoring inheritance should select the original instrument");
  const auto& clearedVariant = sets[0].instruments[cleared->instrument.instrument];
  expect(!clearedVariant.regions[0].envelope.releaseSeconds &&
             !clearedVariant.regions[1].envelope.releaseSeconds,
         "setting a field with an absent value should explicitly clear that field");

  const auto& attackVariant = sets[0].instruments[first->instrument.instrument];
  expect(attackVariant.regions[0].envelope.attackSeconds == 0.25 &&
             attackVariant.regions[1].envelope.attackSeconds == 0.25,
         "a partial attack update should apply to every region");
  expect(attackVariant.regions[0].envelope.releaseSeconds == 5.0 &&
             attackVariant.regions[1].envelope.releaseSeconds == 7.0,
         "untouched fields should continue to inherit each region's own envelope");
}

void dynamicEnvelopeInstrumentSelectionControlsOverrideCarry() {
  std::vector<InstrumentSetAsset> sets{InstrumentSetAsset{
      .instruments = {
          testInstrument(0, Envelope{.attackSeconds = 1.0}),
          testInstrument(1, Envelope{.attackSeconds = 2.0}),
      },
  }};
  auto performance = sequenceWithEvents({
      InstrumentPerformanceEvent{
          .header = eventHeader(0, 0),
          .sourceInstrument = InstrumentIdentity{.domain = "dynamic-envelope-test", .key = 0},
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
          .sourceInstrument = InstrumentIdentity{.domain = "dynamic-envelope-test", .key = 1},
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
          .sourceInstrument = InstrumentIdentity{.domain = "dynamic-envelope-test", .key = 0},
          .envelopeMode = InstrumentEnvelopeMode::PreserveDynamicOverride,
      },
      NotePerformanceEvent{
          .header = eventHeader(16, 7),
          .key = 64,
          .durationTicks = 4,
          .note = PerformanceNoteId{3},
      },
  });

  const auto materialized = materializeDynamicEnvelopes(performance, sets);
  const auto* first = materialized.instruments.selectionFor(TrackId{0}, PerformanceNoteId{1});
  const auto* reset = materialized.instruments.selectionFor(TrackId{0}, PerformanceNoteId{2});
  const auto* preserved = materialized.instruments.selectionFor(TrackId{0}, PerformanceNoteId{3});
  expect(first != nullptr && first->instrument.instrument >= 2,
         "a dynamic override should materialize a variant before an instrument change");
  expect(reset != nullptr && reset->instrument == PreparedInstrumentRef{.set = 0, .instrument = 1},
         "ordinary instrument selection should use the new instrument's native envelope");
  expect(preserved != nullptr && preserved->instrument.instrument >= 2 &&
             sets[0].instruments[preserved->instrument.instrument].regions[0].envelope.attackSeconds == 0.2,
         "an explicit preserve transition should carry the dynamic override to the selected instrument");
}

void dynamicEnvelopeActiveVoiceLimitationIsExplicit() {
  std::vector<InstrumentSetAsset> sets{InstrumentSetAsset{
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

  const auto materialized = materializeDynamicEnvelopes(performance, sets);
  expect(std::ranges::any_of(
             materialized.diagnostics,
             [](const Diagnostic& diagnostic) { return diagnostic.code == "dynamic-envelope-active-voice"; }),
         "an active-voice envelope command should report the static-variant limitation");
  const auto* later = materialized.instruments.selectionFor(TrackId{0}, PerformanceNoteId{2});
  expect(later != nullptr && later->instrument.instrument == 1,
         "a combined active/future command should still affect the next fresh attack");
}

void dynamicEnvelopeMidiUsesTheSharedPlanAndReturnsToBankZero() {
  std::vector<Instrument> instruments;
  instruments.reserve(128);
  for (u32 program = 0; program < 128; ++program) {
    instruments.push_back(testInstrument(program, Envelope{.attackSeconds = 1.0}));
  }
  std::vector<InstrumentSetAsset> sets{InstrumentSetAsset{
      .instruments = std::move(instruments),
  }};

  auto performance = sequenceWithEvents({
      InstrumentPerformanceEvent{
          .header = eventHeader(0, 0),
          .sourceInstrument = InstrumentIdentity{.domain = "dynamic-envelope-test", .key = 0},
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
      NotePerformanceEvent{
          .header = eventHeader(4, 3),
          .key = 60,
          .durationTicks = 4,
          .extendsPrevious = true,
          .note = PerformanceNoteId{1},
      },
      EnvelopePerformanceEvent{
          .header = eventHeader(10, 4),
          .update = EnvelopeUpdate::restore(),
      },
      NotePerformanceEvent{
          .header = eventHeader(10, 5),
          .key = 62,
          .durationTicks = 4,
          .note = PerformanceNoteId{2},
      },
  });

  const auto materialized = materializeDynamicEnvelopes(performance, sets);
  expect(materialized.variantCount == 1 &&
             sets[0].instruments.back().explicitAddress == std::optional{InstrumentAddress{.bank = 1, .program = 0}},
         "the allocator should move to the next free bank after bank zero is occupied");

  std::vector<const InstrumentSetAsset*> views{&sets[0]};
  const MidiSequence midi = renderMidiSequence(performance, {}, ModulationConversionPolicy::SynthModulators, views,
                                               nullptr, &materialized.instruments);
  std::vector<std::pair<u64, u16>> banks;
  for (const auto& event : midi.tracks[0].events) {
    if (const auto* bank = std::get_if<BankSelect>(&event)) {
      banks.emplace_back(bank->tick, bank->bank);
    }
  }
  expect(std::ranges::find(banks, std::pair<u64, u16>{0, 128}) != banks.end(),
         "the first fresh note should select the generated logical bank");
  expect(std::ranges::find(banks, std::pair<u64, u16>{10, 0}) != banks.end(),
         "restoring the base envelope should explicitly return MIDI to bank zero");
  expect(std::ranges::none_of(banks, [](const auto& bank) { return bank.first == 4; }),
         "a tied extension should not reselect its instrument");
}

void dynamicEnvelopeSynthFilteringUsesExactPreparedInstruments() {
  std::vector<InstrumentSetAsset> sets{InstrumentSetAsset{
      .instruments = {testInstrument(0, Envelope{.attackSeconds = 1.0})},
  }};
  auto performance = sequenceWithEvents({
      EnvelopePerformanceEvent{
          .header = eventHeader(0, 0),
          .update = EnvelopeUpdate::set(Envelope{.attackSeconds = 0.1}, EnvelopeFields::Attack),
      },
      NotePerformanceEvent{
          .header = eventHeader(0, 1),
          .key = 60,
          .durationTicks = 4,
          .note = PerformanceNoteId{1},
      },
  });
  const auto materialized = materializeDynamicEnvelopes(performance, sets);
  const auto* selected = materialized.instruments.selectionFor(TrackId{0}, PerformanceNoteId{1});
  expect(selected != nullptr && selected->instrument.instrument == 1,
         "the dynamic note should select its generated prepared instrument");

  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "dynamic-envelope.pcm"}, std::vector<u8>{0});
  const SampleCollectionAsset samples{
      .metadata = AssetMetadata{.id = AssetId{10}},
      .samples = SampleCollection{.samples = {Sample{
                                      .codec = AudioCodec::PcmS8,
                                      .encodedData = SourceRange{.source = source, .offset = 0, .size = 1},
                                      .sampleRate = 32000,
                                  }}},
  };
  std::vector<const InstrumentSetAsset*> instrumentViews{&sets[0]};
  std::vector<const SampleCollectionAsset*> sampleViews{&samples};
  const auto prepared = prepareSynthData(
      SynthExportInput{
          .instrumentSets = instrumentViews,
          .sampleCollections = sampleViews,
          .sequenceUsage = &performance,
          .instrumentPlan = &materialized.instruments,
      },
      sources);
  expect(prepared.instruments.size() == 1 && prepared.instruments[0].address == selected->address,
         "used-only synth export should retain the exact generated variant selected by the shared plan");
}

}  // namespace

void runValueDynamicEnvelopeTests() {
  dynamicEnvelopeMaterializationIsIncrementalAndDeduplicated();
  dynamicEnvelopeInstrumentSelectionControlsOverrideCarry();
  dynamicEnvelopeActiveVoiceLimitationIsExplicit();
  dynamicEnvelopeMidiUsesTheSharedPlanAndReturnsToBankZero();
  dynamicEnvelopeSynthFilteringUsesExactPreparedInstruments();
}
