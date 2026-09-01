/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NamcoSnes/NamcoSnes.h"

#include "value/sequence/SequenceVm.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::namco_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Event>
std::vector<const Event*> events(const PerformanceTrack& track) {
  std::vector<const Event*> result;
  for (const PerformanceEvent& event : track.events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      result.push_back(typed);
    }
  }
  return result;
}

class DriverFixture {
public:
  explicit DriverFixture(Version version) : data_(kAramSize), version_(version) {
    block_ = version == Version::YuuYuuHakushoTokubetsuHen ? 0xc000
             : version == Version::BlueCrystalRod          ? 0xf000
                                                           : 0xdc10;
    write(0x0100, {0xe5, lo(block_), hi(block_), 0xc4, 0x42, 0xe5, lo(block_ + 1), hi(block_ + 1), 0xc4,
                   0x43, 0xf5, 0x40, 0x03, 0x1c, 0xfd, 0xf7, 0x42});
    write(0x0200, {0xf5, 0x00, 0x02, 0x1c, 0x60, 0x88, 0xe0, 0xc4, 0x3c, 0xe8, 0x11, 0x88, 0x00, 0xc4,
                   0x3d});
    write(0x0300, {0x0c, 0x60, 0x1c, 0x60, 0x0d, 0x00, 0x2c, 0x00, 0x2d, 0x00, 0x3c, 0x00,
                   0x3d, 0x00, 0x4d, 0x00, 0x5d, 0x11, 0x6d, 0xac, 0x7d, 0x01, 0x6c, 0x20});
    word(block_, 0xdd00);
    word(block_ + 2, 0xde00);
    word(block_ + 4, 0xdf00);
    word(block_ + 6, 0xe000);

    word(0xdd00, 0xdd20);
    word(0xdd02, 0xdd23);
    write(0xdd20, {0xff, 0xe0, 0x1f, 0xf2, 0xe0, 0x0a});
    word(0xde00, 0xde40);
    word(0xde02, 0xde20);
    write(0xde20, {0x64, 0x68, 0x64, 0x60, 0xf4, 0x00});
    write(0xde40, {0x64, 0xf0});
    write(0xdf00, {4, 1, 0xc0, 0x88, 0x30});
    bigWord(0x11e0 + 3 * 2, 0x0700);
    bigWord(0x11e0 + 4 * 2, 0x0700);
    selectSong(1, 3, 0x2000);
  }

  DriverFixture& sequence(std::initializer_list<u8> bytes, u16 address = 0x2000) {
    write(address, bytes);
    selectSong(1, 3, address);
    return *this;
  }

  DriverFixture& percussionSource(u8 index, u8 sourceKey) {
    data_[0xdf00 + index * 5u + 4u] = sourceKey;
    return *this;
  }

  [[nodiscard]] const std::vector<u8>& data() const { return data_; }
  [[nodiscard]] u16 block() const { return block_; }

private:
  static u8 lo(u16 value) { return static_cast<u8>(value); }
  static u8 hi(u16 value) { return static_cast<u8>(value >> 8); }

  void selectSong(u8 song, u8 group, u16 address) {
    data_[0x49 + group * 2u] = static_cast<u8>(0x80 | song);
    if (version_ == Version::YuuYuuHakushoTokubetsuHen) {
      const u16 list = static_cast<u16>(0xc100 + group * 0x20);
      word(block_ + 8 + group * 2u, list);
      word(list + song * 2u, address);
    } else {
      const u32 row = block_ + 8 + song * 3u;
      data_[row] = group;
      word(row + 1, address);
    }
  }

  void word(u32 offset, u16 value) {
    data_[offset] = lo(value);
    data_[offset + 1] = hi(value);
  }
  void bigWord(u32 offset, u16 value) {
    data_[offset] = hi(value);
    data_[offset + 1] = lo(value);
  }
  void write(u32 offset, std::initializer_list<u8> values) {
    std::ranges::copy(values, data_.begin() + offset);
  }

  std::vector<u8> data_;
  Version version_;
  u16 block_ = 0;
};

Layout manualLayout(const DriverFixture& fixture) {
  return Layout{
      .version = Version::WagyanParadise,
      .sequenceAddress = 0x2000,
      .sequenceReferenceAddress = static_cast<u16>(fixture.block() + 11),
      .sequenceReferenceSize = 3,
      .dataPointerBlockAddress = fixture.block(),
      .tuningTableAddress = 0x11e0,
      .spcDirAddress = 0x1100,
      .mono = false,
  };
}

PerformanceSequence render(const DriverFixture& fixture) {
  const ByteReader reader(SourceId{221}, fixture.data());
  SequenceParse parsed = decodeSequence(RetainedSource::copyOf(reader), manualLayout(fixture), AssetId{221});
  return SequenceVm(LoopPolicy::PlayOnce).render(parsed.program);
}

void layoutsCoverAllAuditedDriverRelocations() {
  for (const Version version : {Version::WagyanParadise, Version::YuuYuuHakushoTokubetsuHen,
                                Version::BlueCrystalRod}) {
    DriverFixture fixture(version);
    fixture.sequence({0x03});
    const auto layout = findLayout(ByteReader(SourceId{222}, fixture.data()));
    expect(layout && layout->version == version && layout->sequenceAddress == 0x2000 &&
               layout->dataPointerBlockAddress == fixture.block() && layout->tuningTableAddress == 0x11e0 &&
               layout->spcDirAddress == 0x1100,
           "NamcoSnes layout detection should follow every audited relocation and live song slot");
  }
}

void interleavedRuntimePreservesDynamicDriverFeatures() {
  DriverFixture fixture(Version::WagyanParadise);
  fixture.percussionSource(0, 0x7f).sequence({
      0x00, 4,    0x04, 1,    0x01, 0xc0, 0x20, 0xc0, 3,    4,    0x21, 0xc0, 0xa0, 0x80,
      0x22, 0xc0, 0xf4, 0x4f, 0x24, 0x80, 1,    0x28, 0x80, 0x80, 0x29, 0x80, 0,
      0x2a, 0xc0, 1,    0,    0x0b, 0x40, 2,    0x05, 0xc0, 0x0d, 1,    0x0a, 3,
      0x11, 0x80, 0x12, 2,    0x13, 0x40, 0x09, 0xc0, 0x30, 0x31, 0x23, 0x80, 2,
      0x27, 0x80, 0x40, 0x09, 0x80, 0x34, 0x09, 0x80, 0x80, 0x09, 0x80, 0x55,
      0x09, 0x80, 0x54, 0x14, 0x12, 0x03,
  });
  const PerformanceSequence performance = render(fixture);
  expect(performance.diagnostics.empty() && performance.tracks.size() == kTrackCount,
         "the physical stream should render as eight source voices without diagnostics");

  const auto voice0Notes = events<NotePerformanceEvent>(performance.tracks[0]);
  const auto voice1Notes = events<NotePerformanceEvent>(performance.tracks[1]);
  expect(voice0Notes.size() == 4 && voice1Notes.size() == 1 && voice1Notes.front()->header.tick == 2,
         "masked notes, percussion, noise, rests, and delayed note-on should retain driver timing");
  expect(voice0Notes[1]->durationTicks == 3 && voice0Notes[2]->durationTicks == 3,
         "gate values should latch on attack and release after value-plus-one driver ticks");

  const auto envelopes = events<EnvelopePerformanceEvent>(performance.tracks[0]);
  expect(!envelopes.empty() && envelopes.front()->update.values &&
             std::abs(*envelopes.front()->update.values->releaseSeconds -
                      snesDspGainEnvelopeSeconds(0xaa, 0x7ff, 0)) < 0.000001,
         "sequence-selected ADSR should retain its separate GAIN release rate");
  const auto pitchTable = events<PitchBendPerformanceEvent>(performance.tracks[0]);
  expect(!pitchTable.empty() &&
             std::ranges::all_of(pitchTable, [](const PitchBendPerformanceEvent* event) {
               return event->layer != kPrimaryPitchBendLayer;
             }) &&
             !performance.tracks[0].automations.empty(),
         "pitch tables should remain independent of the track's musical slides");
  expect(events<PitchBendRangePerformanceEvent>(performance.tracks[0]).empty(),
         "physical pitch-table values should not emit exporter-specific bend ranges");

  const auto instruments = events<InstrumentPerformanceEvent>(performance.tracks[0]);
  expect(std::ranges::any_of(instruments, [](const InstrumentPerformanceEvent* event) {
           return event->bank == 127 && event->program == 0;
         }) &&
             std::ranges::any_of(instruments, [](const InstrumentPerformanceEvent* event) {
               return event->sourceInstrument && event->sourceInstrument->key == kNoiseInstrumentKey;
             }),
         "percussion and DSP noise should remain distinct from melodic SRCN instruments");

  const auto reverb = events<ReverbPerformanceEvent>(performance.tracks[0]);
  expect(reverb.size() == 6 && reverb.back()->leftGain == 0.5 && reverb.back()->rightGain == 0.5 &&
             reverb.back()->delayMilliseconds == 48.0 && reverb.back()->feedback == -1.0 &&
             reverb.back()->filterIndex == 2 && reverb.back()->voiceMask == 3,
         "echo commands should preserve EON, EDL, signed EFB, FIR identity, and the one-byte mono EVOL write");
}

void percussionPitchMappingIsAppliedExactlyOnce() {
  DriverFixture fixture(Version::WagyanParadise);
  fixture.sequence({0x00, 1, 0x01, 0x80, 0x25, 0x80, 0xfe, 0x26, 0x80, 0x80, 0x09, 0x80, 0x80, 0x03});
  const PerformanceSequence performance = render(fixture);
  const auto notes = events<NotePerformanceEvent>(performance.tracks[0]);
  const auto tunings = events<TuningPerformanceEvent>(performance.tracks[0]);

  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->key == 0 && tunings.size() == 1 &&
             std::abs(tunings.front()->cents + 150.0) < 0.000001,
         "percussion regions should own table key mapping while the sequence applies only transpose and fine tuning");
}

void pitchTableIndexUsesSpcAccumulatorWrapping() {
  DriverFixture fixture(Version::WagyanParadise);
  fixture.sequence({0x00, 1, 0x01, 0x80, 0x24, 0x80, 0x80, 0x09, 0x80, 0x30, 0x03});
  const auto bends = events<PitchBendPerformanceEvent>(render(fixture).tracks[0]);

  expect(bends.size() == 1 && bends.front()->semitones == 0.0,
         "pitch table index $80 should alias index zero like the driver's eight-bit ASL A");
}

void attacksFollowThePhysicalVoiceLifecycle() {
  DriverFixture fixture(Version::WagyanParadise);
  fixture.sequence({0x00, 4, 0x01, 0x80, 0x0b, 0x80, 2,    0x09, 0x80, 0x30,
                    0x09, 0x80, 0x31, 0x0c, 0x80, 0x09, 0x80, 0x31, 0x0c, 0x00,
                    0x09, 0x80, 0x54, 0x03});
  const PerformanceSequence performance = render(fixture);
  const auto notes = events<NotePerformanceEvent>(performance.tracks[0]);
  const auto instruments = events<InstrumentPerformanceEvent>(performance.tracks[0]);

  expect(performance.diagnostics.empty() && instruments.size() == 1 && notes.size() == 3 &&
             notes[0]->header.tick == 2 &&
             notes[0]->durationTicks == 4 && notes[1]->header.tick == 6 && notes[1]->durationTicks == 8 &&
             notes[2]->header.tick == 10 && notes[2]->extendsPrevious && notes[2]->note == notes[1]->note,
         "attacks should manage voice lifetime without redundantly reselecting an unchanged instrument");
}

void everyTriggerLatchesLiveVoiceControls() {
  DriverFixture fixture(Version::WagyanParadise);
  fixture.sequence({0x00, 4,    0x20, 0x80, 4,    0x01, 0x80, 0x09, 0x80, 0x30, 0x25, 0x80, 12,
                    0x21, 0x80, 0x40, 0x23, 0x80, 2,    0x0c, 0x80, 0x09, 0x80, 0x31,
                    0x21, 0x80, 0xa0, 0x09, 0x80, 0x54, 0x03});
  const PerformanceSequence performance = render(fixture);
  const auto notes = events<NotePerformanceEvent>(performance.tracks[0]);
  const auto levels = events<LevelPerformanceEvent>(performance.tracks[0]);
  const auto instruments = events<InstrumentPerformanceEvent>(performance.tracks[0]);

  expect(performance.diagnostics.empty() && notes.size() == 2 && notes[1]->key == 0x31 + 12,
         "a slur trigger should latch pitch controls");
  expect(notes[1]->durationTicks == 3, "a slur trigger should latch gate controls");
  expect(levels.back()->header.tick == 8 && std::abs(levels.back()->linearGain - 0xa0 / 256.0) < 0.000001,
         "a rest trigger should latch mix controls");
  expect(instruments.size() == 2 && instruments.back()->sourceInstrument &&
             instruments.back()->sourceInstrument->key == 4,
         "voice activation should preserve the driver's persistent SRCN control");
}

void releaseTailsRetainDriverPitch() {
  DriverFixture fixture(Version::WagyanParadise);
  fixture.sequence({0x00, 4,    0x01, 0x80, 0x27, 0x80, 0x80, 0x09, 0x80, 0x30,
                    0x09, 0x80, 0x54, 0x09, 0x80, 0x3c, 0x03});
  const PerformanceSequence performance = render(fixture);
  const auto* slide = performance.tracks[0].automations.empty()
                          ? nullptr
                          : pitchTransitionIntent(performance.tracks[0].automations.front());
  const auto* curve = slide == nullptr ? nullptr : std::get_if<SampledAutomationCurve>(&slide->curve);

  expect(slide != nullptr && slide->preferredRendering == PitchTransitionRenderingHint::PitchBend &&
             slide->timing.timelineTicks == 10 && slide->startKey == 51.25 && slide->targetKey == 60.0 &&
             curve != nullptr && curve->samples.size() == 11 &&
             curve->samples[1] == AutomationSample{.tickOffset = 1, .value = 53.5} &&
             curve->samples[4] == AutomationSample{.tickOffset = 4, .value = 57.5},
         "release tails should retain the driver's immediate, distance-scaled fixed-point portamento curve");
}

void bothRepeatCountersFollowTheSharedIncrementRules() {
  DriverFixture fixture(Version::WagyanParadise);
  fixture.sequence({0x00, 1,    0x04, 1,    0x01, 0x80, 0x09, 0x80, 0x30, 0x06, 3,    0x06,
                    0x20, 0x03});
  const PerformanceSequence repeat = render(fixture);
  expect(repeat.diagnostics.empty() && events<NotePerformanceEvent>(repeat.tracks[0]).size() == 3,
         "repeat-until should increment before comparison and preserve its finite driver count");

  fixture.sequence({0x00, 1,    0x04, 1,    0x01, 0x80, 0x09, 0x80, 0x30, 0x07, 2,    0x10,
                    0x20, 0x08, 0x06, 0x20, 0x03});
  const PerformanceSequence repeatBreak = render(fixture);
  expect(repeatBreak.diagnostics.empty() && events<NotePerformanceEvent>(repeatBreak.tracks[0]).size() == 2,
         "repeat-break should share its slot counter, branch on equality, and reset it when taken");
}

}  // namespace

void runNamcoSnesModuleTests() {
  layoutsCoverAllAuditedDriverRelocations();
  interleavedRuntimePreservesDynamicDriverFeatures();
  percussionPitchMappingIsAppliedExactlyOnce();
  pitchTableIndexUsesSpcAccumulatorWrapping();
  attacksFollowThePhysicalVoiceLifecycle();
  everyTriggerLatchesLiveVoiceControls();
  releaseTailsRetainDriverPitch();
  bothRepeatCountersFollowTheSharedIncrementRules();
}
