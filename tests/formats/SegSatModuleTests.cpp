/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/base/LevelScale.h"
#include "value/export/Export.h"
#include "value/extractors/PsfExtractor.h"
#include "value/formats/SegSat/SegSat.h"
#include "value/session/Session.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/SampleDecoder.h"

#include "../core/SessionSnapshotBuilder.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::segsat;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void be16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value >> 8);
  bytes[offset + 1] = static_cast<u8>(value);
}

void be32(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>(value >> 24);
  bytes[offset + 1] = static_cast<u8>(value >> 16);
  bytes[offset + 2] = static_cast<u8>(value >> 8);
  bytes[offset + 3] = static_cast<u8>(value);
}

void le32(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
  bytes[offset + 2] = static_cast<u8>(value >> 16);
  bytes[offset + 3] = static_cast<u8>(value >> 24);
}

u8 normalizedSegSatVelocity(u8 velocity, const SegSatVlTable& table, u8 totalLevel) {
  const double gain = segSatLinearGain(SegSatVolumeModel::V1_33, velocity, table, totalLevel, 0, 127, 127);
  return LevelScale::midi7FromLinear(gain / segSatRegionReferenceGain(SegSatVolumeModel::V1_33, table, totalLevel, 0));
}

std::vector<u8> segSatFixture() {
  constexpr u32 sequenceTable = 0x400;
  constexpr u32 sequence = sequenceTable + 6;
  constexpr u32 bank = 0x1000;
  constexpr u32 sample = bank + 0x200;
  std::vector<u8> bytes(0x1300);
  const std::initializer_list<u8> driverVersion{'V', 'e', 'r', '1', '.', '3', '3'};
  std::ranges::copy(driverVersion, bytes.begin() + 0x20);

  // One-entry sequence table. Its first pointer overlaps the detector's
  // six-byte table header, as it does in Saturn sound RAM.
  bytes[sequenceTable + 1] = 1;
  be32(bytes, sequenceTable + 2, 6);
  be16(bytes, sequence, 48);
  be16(bytes, sequence + 2, 1);
  be16(bytes, sequence + 4, 16);
  be16(bytes, sequence + 6, 0);
  be32(bytes, sequence + 8, 0);
  be32(bytes, sequence + 12, 500000);

  size_t command = sequence + 16;
  auto append = [&](std::initializer_list<u8> values) {
    std::ranges::copy(values, bytes.begin() + static_cast<std::ptrdiff_t>(command));
    command += values.size();
  };
  append({0xb0, 32, 5, 0});       // source bank 5
  append({0xc0, 0, 0});           // program 0
  append({0xc3, 0x81, 0});        // flagged channel 3 program event ignored by the driver
  append({0xb0, 7, 64, 0});       // channel volume
  append({0xb0, 10, 0, 0});       // hard left
  append({0x00, 60, 64, 10, 0});  // note, source velocity 64
  append({0xe0, 0xbd, 1});        // the driver ignores a flagged event
  append({0xe0, 0x3d, 1});        // ordinary pitch bend
  append({0x83});

  // The sound-RAM bank map gives the bank its source-domain number. A sole
  // attached bank is remapped to bank zero during collection preparation.
  be32(bytes, 0x500, (5u << 24) | bank);
  be32(bytes, 0x508, 0xff000000);

  // One playable instrument, one empty 0xff-sentinel instrument, one VL
  // table, and one PCM16 region.
  be16(bytes, bank, 0x0c);
  be16(bytes, bank + 2, 0x1e);
  be16(bytes, bank + 4, 0x28);
  be16(bytes, bank + 6, 0x32);
  be16(bytes, bank + 8, 0x36);
  be16(bytes, bank + 10, 0x5a);
  const std::initializer_list<u8> identityVl{2, 127, 127, 2, 127, 127, 2, 127, 127, 2};
  std::ranges::copy(identityVl, bytes.begin() + bank + 0x1e);

  // The high nibble is play mode; bend range 13 (two octaves) is the low nibble.
  bytes[bank + 0x36] = 0x4d;
  bytes[bank + 0x36 + 2] = 0;  // one region
  const u32 region = bank + 0x3a;
  bytes[region] = 0;
  bytes[region + 1] = 127;
  be32(bytes, region + 2, 0x200);
  be16(bytes, region + 6, 0);
  be16(bytes, region + 8, 4);
  // EGHOLD with KRS 1, a D1 target of -39 dB, and an endless D2.
  be16(bytes, region + 10, 0x022f);
  be16(bytes, region + 12, 0x05a0);
  // Pitch and amplitude have separate waveform fields in the same SCSP word.
  // Use sawtooth pitch and triangle amplitude so neither can be read as the other.
  bytes[region + 20] = 0x20;
  bytes[region + 21] = 0x52;
  bytes[region + 24] = 0xe0;
  bytes[region + 25] = 60;
  bytes[region + 29] = 0;
  bytes[bank + 0x5a + 2] = 0xff;  // byte increment wraps to zero regions

  const std::initializer_list<u8> pcm{0x12, 0x34, 0xfe, 0xdc, 0x00, 0x01, 0x80, 0x00};
  std::ranges::copy(pcm, bytes.begin() + sample);
  return bytes;
}

void writeOneInstrumentBank(std::vector<u8>& bytes, u32 bank, u32 sampleRelative, u16 sampleFrames,
                            u8 totalLevel) {
  be16(bytes, bank, 0x0a);
  be16(bytes, bank + 2, 0x1c);
  be16(bytes, bank + 4, 0x26);
  be16(bytes, bank + 6, 0x30);
  be16(bytes, bank + 8, 0x34);
  const std::initializer_list<u8> identityVl{2, 127, 127, 2, 127, 127, 2, 127, 127, 2};
  std::ranges::copy(identityVl, bytes.begin() + bank + 0x1c);

  bytes[bank + 0x34 + 2] = 0;
  const u32 region = bank + 0x38;
  bytes[region] = 0;
  bytes[region + 1] = 127;
  be32(bytes, region + 2, sampleRelative);
  be16(bytes, region + 6, 0);
  be16(bytes, region + 8, sampleFrames);
  bytes[region + 15] = totalLevel;
  bytes[region + 24] = 0xe0;
  bytes[region + 25] = 60;
}

std::vector<u8> overlappingBankFixture() {
  constexpr u32 sequenceTable = 0x400;
  constexpr u32 sequence = sequenceTable + 6;
  constexpr u32 obscuringBank = 0x1000;
  constexpr u32 mappedBank = 0x1200;
  std::vector<u8> bytes(0x1500);

  bytes[sequenceTable + 1] = 1;
  be32(bytes, sequenceTable + 2, 6);
  be16(bytes, sequence, 48);
  be16(bytes, sequence + 2, 0);
  be16(bytes, sequence + 4, 8);
  be16(bytes, sequence + 6, 0);
  size_t command = sequence + 8;
  const std::initializer_list<u8> commands{0x00, 60, 64, 10, 0, 0x83};
  std::ranges::copy(commands, bytes.begin() + static_cast<std::ptrdiff_t>(command));

  // The first bank's sample extends from 0x1100 through 0x1300, containing the
  // second bank header. Real Saturn sound RAM can be arranged this way.
  writeOneInstrumentBank(bytes, obscuringBank, 0x100, 0x100, 0);
  writeOneInstrumentBank(bytes, mappedBank, 0x100, 4, 128);
  be32(bytes, 0x500, (16u << 24) | obscuringBank);
  be32(bytes, 0x508, mappedBank);  // implicit driver bank zero
  be32(bytes, 0x510, 0xff000000);
  return bytes;
}

std::vector<u8> multiBankVelocityFixture() {
  constexpr u32 sequenceTable = 0x400;
  constexpr u32 sequence = sequenceTable + 6;
  constexpr u32 bank5 = 0x1000;
  constexpr u32 bank6 = 0x1200;
  std::vector<u8> bytes(0x1500);

  bytes[sequenceTable + 1] = 1;
  be32(bytes, sequenceTable + 2, 6);
  be16(bytes, sequence, 48);
  be16(bytes, sequence + 2, 0);
  be16(bytes, sequence + 4, 8);
  be16(bytes, sequence + 6, 0);
  size_t command = sequence + 8;
  const std::initializer_list<u8> commands{
      0xb0, 32, 6, 0, 0xc0, 0, 0, 0x00, 60, 64, 10, 0,
      0xb0, 32, 5, 0,             0x00, 60, 64, 10, 0, 0x83,
  };
  std::ranges::copy(commands, bytes.begin() + static_cast<std::ptrdiff_t>(command));

  writeOneInstrumentBank(bytes, bank5, 0x100, 4, 0);
  writeOneInstrumentBank(bytes, bank6, 0x100, 4, 128);
  be32(bytes, 0x500, (5u << 24) | bank5);
  be32(bytes, 0x508, (6u << 24) | bank6);
  be32(bytes, 0x510, 0xff000000);
  return bytes;
}

std::vector<u8> velocityBankSource(u8 totalLevel) {
  constexpr u32 bank = 0x100;
  std::vector<u8> bytes(0x300);
  writeOneInstrumentBank(bytes, bank, 0x100, 4, totalLevel);
  return bytes;
}

const SequenceProgramAsset& sequenceAsset(const SessionSnapshot& snapshot, const Collection& collection) {
  expect(collection.members.sequence.has_value(), "SegSat fixture collection should reference a sequence");
  const auto* sequence = snapshot.asset<SequenceProgramAsset>(*collection.members.sequence);
  expect(sequence != nullptr, "SegSat fixture sequence asset should exist");
  return *sequence;
}

const NotePerformanceEvent* firstNote(const PerformanceSequence& performance) {
  for (const auto& track : performance.tracks) {
    for (const auto& event : track.events) {
      if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
        return note;
      }
    }
  }
  return nullptr;
}

const LevelPerformanceEvent* firstLevel(const PerformanceSequence& performance) {
  for (const auto& track : performance.tracks) {
    for (const auto& event : track.events) {
      if (const auto* level = std::get_if<LevelPerformanceEvent>(&event)) {
        return level;
      }
    }
  }
  return nullptr;
}

}  // namespace

void segSatVlCurveMatchesMm8Saturation() {
  expect(segSatRegionCount(0x7f) == 128 && segSatRegionCount(0x80) == 0 && segSatRegionCount(0xff) == 0,
         "MM8 should reject signed-negative region-count bytes, including the 0xff empty-program sentinel");

  const SegSatVlTable identity{
      .rate0 = 2,
      .point0 = 127,
      .level0 = 127,
      .rate1 = 2,
      .point1 = 127,
      .level1 = 127,
      .rate2 = 2,
      .point2 = 127,
      .level2 = 127,
      .rate3 = 2,
  };
  expect(segSatMidiVelocity(64, identity, 0, 0) == 8,
         "MM8 VL conversion should include the region total-level attenuation path");
  const auto expectedGain = [](u8 attenuation) { return std::pow(10.0, -(attenuation * 0.37529) / 20.0); };
  expect(std::abs(segSatLinearGain(SegSatVolumeModel::V1_28, 64, identity, 0, 0, 64, 127) - expectedGain(192)) <
                 0.000000001 &&
             std::abs(segSatLinearGain(SegSatVolumeModel::V1_33, 64, identity, 0, 0, 64, 127) - expectedGain(189)) <
                 0.000000001 &&
             std::abs(segSatLinearGain(SegSatVolumeModel::V2_20, 64, identity, 0, 0, 64, 127) - expectedGain(193)) <
                 0.000000001 &&
             std::abs(segSatLinearGain(SegSatVolumeModel::V3_1, 64, identity, 0, 0, 64, 127) - expectedGain(189)) <
                 0.000000001,
         "each known driver should reproduce its integer note-level arithmetic");

  SegSatVlTable positiveOverflow = identity;
  positiveOverflow.rate0 = 0x11;
  expect(segSatMidiVelocity(80, positiveOverflow, 0, 0) == 127,
         "MM8 byte arithmetic should saturate 0x80..0xbf to positive full scale");

  SegSatVlTable negativeUnderflow = identity;
  negativeUnderflow.rate0 = 6;
  expect(segSatMidiVelocity(1, negativeUnderflow, 0, 0) == 1,
         "MM8 byte arithmetic should saturate 0xc0..0xff to zero before SCSP attenuation conversion");
}

void segSatCollectionPreparationSuppliesVlTablesToSequence() {
  Session session;
  session.registerFormat(segSatDefinition());
  const SourceId source = session.addSource(SourceFile{.name = "segsat-fixture.bin"}, segSatFixture());
  session.scanPendingSources();

  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1, "SegSat fixture should produce one explicit collection");
  const Collection& collection = snapshot.collections().front();
  const auto& sequence = sequenceAsset(snapshot, collection);
  const auto sequenceCommands = snapshot.sourceMap().withRole(source, SourceRole::Command);
  expect(snapshot.sourceMap().withRole(source, SourceRole::SequenceTrack).empty() && !sequenceCommands.empty() &&
             std::ranges::all_of(sequenceCommands, [&](SourceAnnotationId id) {
               const SourceAnnotation& command = snapshot.sourceMap().get(id);
               return !command.parent && snapshot.sourceMap().assetOwner(id) == sequence.metadata.id;
             }),
         "SegSat source events should be sequence-owned roots rather than children of synthetic tracks");
  const auto* dialect = session.formats().findDialect(sequence.program.dialect.value);
  expect(dialect != nullptr, "SegSat fixture dialect should be registered");

  const PerformanceSequence unprepared = SequenceVm(LoopPolicy::PlayOnce).render(sequence.program, *dialect);
  expect(std::ranges::any_of(unprepared.sourceSpans,
                             [](const SourcePlaybackSpan& span) { return span.channel == 3; }),
         "SegSat playback spans should retain each event's source channel");
  const auto* sourceNote = firstNote(unprepared);
  expect(sourceNote != nullptr && LevelScale::midi7FromLinear(sourceNote->linearVelocity) == 64,
         "a durable SegSat sequence should retain its source velocity when no collection context is present");

  const CollectionPlayback playback =
      session.preparePlayback(collection.id, PlaybackRequest{.sequence = {.sequenceLoops = 0}});
  const auto* preparedNote = firstNote(playback.performance);
  const auto* preparedLevel = firstLevel(playback.performance);
  const double expectedNoteGain = std::pow(10.0, -(189 * 0.37529) / 20.0);
  expect(playback.playable() && preparedNote != nullptr && preparedLevel != nullptr &&
             LevelScale::midi7FromLinear(preparedNote->linearVelocity) == 33 &&
             std::abs(preparedNote->linearVelocity * preparedLevel->linearGain - expectedNoteGain) < 0.000000001,
         "collection preparation should combine the VL curve and channel volume exactly at note-on");

  const auto balance = std::ranges::find_if(playback.performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<StereoBalancePerformanceEvent>(event);
  });
  const auto* stereo = balance != playback.performance.tracks[0].events.end()
                           ? std::get_if<StereoBalancePerformanceEvent>(&*balance)
                           : nullptr;
  expect(stereo != nullptr && stereo->leftGain == 1.0 && stereo->rightGain == 0.0,
         "SegSat sequence pan should use the SCSP's stepped left and right gains");

  const auto instrument = std::ranges::find_if(
      playback.performance.tracks[0].events,
      [](const PerformanceEvent& event) { return std::holds_alternative<InstrumentPerformanceEvent>(event); });
  const auto* selection = instrument != playback.performance.tracks[0].events.end()
                              ? std::get_if<InstrumentPerformanceEvent>(&*instrument)
                              : nullptr;
  expect(selection != nullptr && selection->sourceInstrument == segSatInstrumentIdentity(5, 0),
         "SegSat program selection should retain its source identity for collection-time address resolution");

  const auto bend = std::ranges::find_if(playback.performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<PitchBendPerformanceEvent>(event);
  });
  const auto* pitch =
      bend != playback.performance.tracks[0].events.end() ? std::get_if<PitchBendPerformanceEvent>(&*bend) : nullptr;
  const auto pitchCount =
      std::ranges::count_if(playback.performance.tracks[0].events, [](const PerformanceEvent& event) {
        return std::holds_alternative<PitchBendPerformanceEvent>(event);
      });
  expect(pitchCount == 1 && pitch != nullptr && std::abs(pitch->semitones - (-0.09375)) < 0.000001 &&
             pitch->normalizedWheelPosition && std::abs(*pitch->normalizedWheelPosition - (-0.046875)) < 0.000001,
         "SegSat should retain its raw pitch-wheel position for collection-aware lowering");

  expect(collection.members.sampleCollections.size() == 1, "SegSat fixture should attach its parsed sample collection");
  const auto* samples = snapshot.asset<SampleCollectionAsset>(collection.members.sampleCollections.front());
  expect(samples != nullptr && samples->samples.samples.size() == 1,
         "SegSat fixture should expose its one unique sample");
  expect(collection.members.instrumentSets.size() == 1, "SegSat fixture should attach its instrument set");
  const auto* instruments = snapshot.asset<InstrumentSetAsset>(collection.members.instrumentSets.front());
  expect(instruments != nullptr && instruments->instruments.size() == 2 &&
             instruments->instruments.front().pitchBendRangeCents == 2400 &&
             instruments->instruments.back().regions.empty(),
         "SegSat instruments should decode bend range and preserve an empty 0xff-sentinel program");
  const auto& parsedRegion = instruments->instruments.front().regions.front();
  expect(parsedRegion.modulation.vibrato && parsedRegion.modulation.vibrato->depthMode == ModulationDepthMode::Fixed &&
             parsedRegion.modulation.vibrato->waveform == LfoWaveform::SawtoothUp &&
             parsedRegion.modulation.vibrato->maxDepthCents == 13.5 &&
             parsedRegion.modulation.vibrato->rateHertz.minimum == 0.68 && parsedRegion.modulation.tremolo &&
             parsedRegion.modulation.tremolo->waveform == LfoWaveform::Triangle &&
             parsedRegion.modulation.tremolo->maxDepthDb == 0.4,
         "SegSat regions should preserve both SCSP LFO waveforms and depths");
  const auto regionSources = snapshot.sourceMap().ownedBy(ObjectRefs::region(instruments->metadata.id, 0, 0));
  const auto* regionSource = regionSources.size() == 1 ? snapshot.sourceMap().find(regionSources.front()) : nullptr;
  expect(regionSource != nullptr, "SegSat region should have one source annotation");
  const auto totalLevel = std::ranges::find_if(regionSource->fields, [](const SourceField& field) {
    return field.name == "total_level" && field.range.valid();
  });
  const auto sourceFields =
      std::ranges::count_if(regionSource->fields, [](const SourceField& field) { return field.range.valid(); });
  expect(regionSource->fieldsAsChildren && sourceFields == 21 && totalLevel != regionSource->fields.end() &&
             totalLevel->range == SourceRange{.source = source, .offset = 0x1049, .size = 1},
         "SegSat region fields should appear as individually selectable TreeView children");
  const double expectedHold = 0.047 * 640.0 / 1023.0;
  const double expectedDecay = 7.4;
  const double expectedSustain = std::pow(10.0, -39.0 / 20.0);
  expect(parsedRegion.envelope.attackSeconds == 0.0 && parsedRegion.envelope.holdSeconds &&
             std::abs(*parsedRegion.envelope.holdSeconds - expectedHold) < 0.000001 &&
             parsedRegion.envelope.decaySeconds &&
             std::abs(*parsedRegion.envelope.decaySeconds - expectedDecay) < 0.000001 &&
             parsedRegion.envelope.secondDecaySeconds && std::isinf(*parsedRegion.envelope.secondDecaySeconds) &&
             parsedRegion.envelope.releaseSeconds &&
             std::abs(*parsedRegion.envelope.releaseSeconds - 118.2) < 0.000001 &&
             parsedRegion.envelope.sustainAmplitude &&
             std::abs(*parsedRegion.envelope.sustainAmplitude - expectedSustain) < 0.000001,
         "SegSat regions should retain SCSP full-scale decay rates, levels, KRS, holds, and endless D2");
  const auto decoded = decodeSample(samples->samples.samples.front(), session.sources().bytes(source));
  expect(decoded && decoded->pcm.size() == 4 && decoded->pcm[0] == 0x1234 && decoded->pcm[1] == -292,
         "SegSat PCM16 samples should decode in the SCSP's big-endian byte order");
}

void segSatRuntimeMapSelectsBankInsideAnotherSampleSpan() {
  Session session;
  session.registerFormat(segSatDefinition());
  session.addSource(SourceFile{.name = "segsat-overlap.bin"}, overlappingBankFixture());
  session.scanPendingSources();

  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1, "overlapping SegSat fixture should produce one collection");
  const Collection& collection = snapshot.collections().front();
  expect(collection.members.instrumentSets.size() == 1,
         "an implicit bank-zero sequence should attach only its runtime-mapped instrument bank");
  const auto* instruments = snapshot.asset<InstrumentSetAsset>(collection.members.instrumentSets.front());
  expect(instruments != nullptr && instruments->metadata.range.offset == 0x1200,
         "runtime bank zero should remain discoverable inside an earlier bank's sample span");

  const CollectionPlayback playback =
      session.preparePlayback(collection.id, PlaybackRequest{.sequence = {.sequenceLoops = 0}});
  const auto* note = firstNote(playback.performance);
  const SegSatVlTable identity{
      .rate0 = 2,
      .point0 = 127,
      .level0 = 127,
      .rate1 = 2,
      .point1 = 127,
      .level1 = 127,
      .rate2 = 2,
      .point2 = 127,
      .level2 = 127,
      .rate3 = 2,
  };
  const double referenceGain = segSatRegionReferenceGain(SegSatVolumeModel::V1_33, identity, 128, 0);
  const double noteGain = segSatLinearGain(SegSatVolumeModel::V1_33, 64, identity, 128, 0, 127, 127);
  expect(note != nullptr && std::abs(note->linearVelocity - noteGain / referenceGain) < 0.000000001 &&
             std::abs(instruments->instruments.front().regions.front().attenuationDb +
                      20.0 * std::log10(referenceGain)) < 0.000001,
         "implicit bank-zero playback should split the region's fixed level from its changing note level");
}

void segSatMultiBankPlaybackUsesTheActiveBanksVlTable() {
  Session session;
  session.registerFormat(segSatDefinition());
  session.addSource(SourceFile{.name = "segsat-multi-bank.bin"}, multiBankVelocityFixture());
  session.scanPendingSources();

  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1 && snapshot.collections().front().members.instrumentSets.size() == 2,
         "a two-bank SegSat sequence should attach both runtime-mapped instrument sets");
  const CollectionPlayback playback =
      session.preparePlayback(snapshot.collections().front().id, PlaybackRequest{.sequence = {.sequenceLoops = 0}});

  std::vector<std::pair<u8, const NotePerformanceEvent*>> notes;
  u8 selectedBank = 0;
  for (const auto& event : playback.performance.tracks.front().events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.emplace_back(selectedBank, note);
    } else if (const auto* instrument = std::get_if<InstrumentPerformanceEvent>(&event);
               instrument != nullptr && instrument->sourceInstrument) {
      if (const auto address = decodeSegSatInstrumentIdentity(*instrument->sourceInstrument)) {
        selectedBank = address->sourceBank;
      }
    }
  }

  const SegSatVlTable identity{
      .rate0 = 2,
      .point0 = 127,
      .level0 = 127,
      .rate1 = 2,
      .point1 = 127,
      .level1 = 127,
      .rate2 = 2,
      .point2 = 127,
      .level2 = 127,
      .rate3 = 2,
  };
  expect(
      notes.size() == 2 && notes[0].first == 6 && notes[1].first == 5 &&
          LevelScale::midi7FromLinear(notes[0].second->linearVelocity) == normalizedSegSatVelocity(64, identity, 128) &&
          LevelScale::midi7FromLinear(notes[1].second->linearVelocity) == normalizedSegSatVelocity(64, identity, 0),
      "source bank aliases should remain paired with sorted collection banks regardless of command order");
}

void segSatCollectionPreparationReadsVelocityBanksFromSeparateSources() {
  SourceStore sources;
  const SourceId bank5Source = sources.add(SourceFile{.name = "bank-5.bin"}, velocityBankSource(0));
  const SourceId bank6Source = sources.add(SourceFile{.name = "bank-6.bin"}, velocityBankSource(128));

  const std::vector<u8> sequenceBytes = multiBankVelocityFixture();
  const ByteReader sequenceReader(SourceId{99}, sequenceBytes);
  const auto sequenceLayouts = findSegSatSequences(sequenceReader);
  expect(sequenceLayouts.size() == 1, "multi-source fixture should contain one sequence");

  const SequenceProgramAsset sequence{
      .metadata = AssetMetadata{.id = AssetId{0}, .format = "SegSat", .name = "Sequence"},
      .program = parseSegSatSequenceProgram(sequenceReader, AssetId{0}, sequenceLayouts.front()),
  };
  const InstrumentSetAsset bank5{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "SegSat",
              .name = "Bank 5",
              .range = SourceRange{.source = bank5Source, .offset = 0x100, .size = 0x58},
          },
      .instruments = {Instrument{
          .explicitAddress = InstrumentAddress{.bank = 5, .program = 0},
          .identity = segSatInstrumentIdentity(5, 0),
      }},
  };
  const InstrumentSetAsset bank6{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "SegSat",
              .name = "Bank 6",
              .range = SourceRange{.source = bank6Source, .offset = 0x100, .size = 0x58},
          },
      .instruments = {Instrument{
          .explicitAddress = InstrumentAddress{.bank = 6, .program = 0},
          .identity = segSatInstrumentIdentity(6, 0),
      }},
  };
  const Collection collection{
      .id = CollectionId{0},
      .name = "Multi-source SegSat",
      .members =
          {
              .sequence = sequence.metadata.id,
              .instrumentSets = {bank5.metadata.id, bank6.metadata.id},
          },
  };
  test::SessionSnapshotBuilder builder;
  builder.sources = sources.sourceFiles();
  builder.assets = {sequence, bank5, bank6};
  builder.collections = {collection};
  const SessionSnapshot snapshot = builder.finish();

  const FormatDefinition format = segSatDefinition();
  const PreparedCollectionAssets prepared = format.module.prepareCollection(CollectionPrepareContext{
      .sources = sources,
      .snapshot = snapshot,
      .collection = snapshot.collections().front(),
  });
  expect(prepared.diagnostics.empty() && prepared.finalizePerformance,
         "SegSat preparation should read attached banks from separate sources");

  PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence.program, segSatSequenceDialect());
  prepared.finalizePerformance(performance);
  std::vector<const NotePerformanceEvent*> notes;
  for (const auto& event : performance.tracks.front().events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    }
  }
  const SegSatVlTable identity{
      .rate0 = 2,
      .point0 = 127,
      .level0 = 127,
      .rate1 = 2,
      .point1 = 127,
      .level1 = 127,
      .rate2 = 2,
      .point2 = 127,
      .level2 = 127,
      .rate3 = 2,
  };
  expect(notes.size() == 2 &&
             LevelScale::midi7FromLinear(notes[0]->linearVelocity) == normalizedSegSatVelocity(64, identity, 128) &&
             LevelScale::midi7FromLinear(notes[1]->linearVelocity) == normalizedSegSatVelocity(64, identity, 0),
         "each note should use the VL table from its selected bank source");
}

void segSatSsfExtractorUsesFourByteMiniHeader() {
  std::vector<u8> executable(7);
  le32(executable, 0, 0x200);
  executable[4] = 0x12;
  executable[5] = 0x34;
  executable[6] = 0x56;

  uLongf compressedSize = compressBound(executable.size());
  std::vector<u8> compressed(compressedSize);
  expect(compress2(compressed.data(), &compressedSize, executable.data(), executable.size(), Z_BEST_SPEED) == Z_OK,
         "SSF fixture compression should succeed");
  compressed.resize(compressedSize);

  std::vector<u8> ssf(16 + compressed.size());
  ssf[0] = 'P';
  ssf[1] = 'S';
  ssf[2] = 'F';
  ssf[3] = 0x11;
  le32(ssf, 8, static_cast<u32>(compressed.size()));
  const u32 checksum =
      static_cast<u32>(crc32(crc32(0L, Z_NULL, 0), compressed.data(), static_cast<uInt>(compressed.size())));
  le32(ssf, 12, checksum);
  std::ranges::copy(compressed, ssf.begin() + 16);

  const auto extractor = vgmtrans::formats::psf::psfExtractor();
  ExtractionInput input{
      .source = SourceFile{.id = SourceId{9}, .name = "fixture.ssf", .size = ssf.size()},
      .reader = ByteReader(SourceId{9}, ssf),
  };
  const ExtractionResult result = extractor.extract(input);
  expect(result.diagnostics.empty() && result.sources.size() == 1 &&
             result.sources.front().bytes == std::vector<u8>({0x12, 0x34, 0x56}),
         "SSF extraction should overlay payload bytes immediately after its four-byte load address");
}
