/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/MoriSnes/MoriSnes.h"

#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::mori_snes;

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
  DriverFixture() : data_(kAramSize) {
    write(0x0100, {0x1c, 0xfd, 0xf6, 0x00, 0x12, 0xc4, 0x04, 0xf6, 0x01, 0x12, 0xc4, 0x05,
                   0x8d, 0x00, 0xf7, 0x04, 0x10, 0x05, 0x68, 0xff, 0xd0, 0x14, 0x6f});
    write(0x0200, {0x8f, 0x20, 0xf3, 0x8f, 0x6c, 0xf2});
    write(0x0300, {0x6d, 0xf7, 0x2d, 0xfd, 0xf6, 0x00, 0x10, 0xd5, 0x7e, 0x02, 0xf6, 0x04, 0x10});
    write(0x0400, {0xf5, 0x0a, 0x02, 0xfd, 0xf6, 0x00, 0x11, 0xee, 0x6d, 0xcf, 0x7d, 0x9f, 0xc4, 0xf2});

    word(0x1200, 0xffff);
    word(0x1202, 0x1300);
    data_[0x1300] = 0;
    relative(0x1301, 0x1400);
    data_[0x1303] = 0xff;

    // One prefixed note selects a melodic descriptor, then loops forever.
    write(0x1400, {0x04, 0x03, 0x40, 0xc0});
    relative(0x1404, 0x1500);
    write(0x1406, {0x80, 0xcb});
    relative(0x1408, 0x1406);

    data_[0x1500] = 0;
    data_[0x1501] = 0xde;
    relative(0x1502, 0x1600);
    write(0x1504, {0xc5, 0xf0, 0xda, 0x04});
    const u16 loop = 0x1508;
    write(loop, {0x01, 0xd9, 0x04, 0xdc, 0xfc, 0x01, 0xd9, 0xfc, 0xdc, 0x04, 0xcb});
    relative(loop + 11, loop);

    write(0x1600, {0x00, 0x8f, 0xe0, 0x00, 0x02, 0x00, 0x00});
    for (u32 index = 0; index < 33; ++index) {
      data_[0x1100 + index] = static_cast<u8>(std::lround(std::cos(index * 3.141592653589793 / 64.0) * 128.0));
    }
    word(0x2000, 0x3000);
    word(0x2002, 0x3000);
    write(0x3000, {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  }

  [[nodiscard]] const std::vector<u8>& data() const { return data_; }

private:
  void write(u32 offset, std::initializer_list<u8> values) {
    std::ranges::copy(values, data_.begin() + offset);
  }

  void word(u32 offset, u16 value) {
    data_[offset] = static_cast<u8>(value);
    data_[offset + 1] = static_cast<u8>(value >> 8);
  }

  void relative(u16 operand, u16 destination) {
    word(operand, static_cast<u16>(destination - static_cast<u16>(operand + 2)));
  }

  std::vector<u8> data_;
};

Layout directLayout() {
  return Layout{
      .presetTableAddress = 0x1000,
      .panTableAddress = 0x1100,
  };
}

PerformanceSequence render(std::vector<u8> bytes) {
  bytes.resize(kAramSize);
  for (u32 index = 0; index < 33; ++index) {
    bytes[0x1100 + index] = static_cast<u8>(128 - index * 4);
  }
  const Layout layout = directLayout();
  const SequenceProgramConfig& config = sequenceConfig();
  const ByteReader reader(SourceId{301}, bytes);
  SequenceProgram program{
      .runtime = sequenceRuntime(reader, layout),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {decodeSourceTrack(reader, layout, 0, 0)},
  };
  return SequenceVm(LoopPolicy::PlayOnce).render(program);
}

SessionSnapshot scan(std::vector<u8> bytes) {
  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "MoriSnes fixture.aram"}, std::move(bytes));
  session.scanPendingSources();
  return session.snapshot();
}

const Collection* firstCollection(const SessionSnapshot& snapshot) {
  return snapshot.collections().empty() ? nullptr : &snapshot.collections().front();
}

const SoundBankAsset* firstSoundBank(const SessionSnapshot& snapshot) {
  const Collection* collection = firstCollection(snapshot);
  return collection == nullptr || collection->members.soundBanks.empty()
             ? nullptr
             : snapshot.asset<SoundBankAsset>(collection->members.soundBanks.front());
}

const Region* firstRegion(const SoundBankAsset* bank) {
  return bank == nullptr || bank->instruments.empty() || bank->instruments.front().regions.empty()
             ? nullptr
             : &bank->instruments.front().regions.front();
}

void eventTimingAndAuditedCommandsRenderPhysically() {
  std::vector<u8> bytes{
      0x04, 0x03, 0x40, 0xc3, 0x80,  // prefix establishes delta/duration/velocity
      0xc2, 0x80,                    // persistent four-tick wait
      0x00, 0xc5, 0xfa,              // explicit zero-delay command
      0xdc, 0x0a,                    // byte-wrap relative volume: $fa + $0a = $04
      0x80,                          // note at the same tick
      0xe6, 0x01,                    // current wait uses old timebase
      0x08, 0x80,                    // later prefix wait is halved
      0xd6, 0x20,
      0xdd, 0x40,
      0xde, 0xe8, 0x11,              // dynamic DSP row at $1200
      0xca, 0x03, 0xc0, 0x40, 0x02,
      0xc9,
      0xc8,
      0xd0,
  };
  bytes.resize(kAramSize);
  bytes[0x1200] = 0;
  bytes[0x1201] = 0x8f;
  bytes[0x1202] = 0xe0;
  bytes[0x1203] = 0;
  const PerformanceSequence performance = render(std::move(bytes));
  const PerformanceTrack& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  const auto levels = events<LevelPerformanceEvent>(track);
  const auto bends = events<PitchBendPerformanceEvent>(track);
  const auto ranges = events<PitchBendRangePerformanceEvent>(track);
  const auto tempos = events<TempoPerformanceEvent>(track);
  const auto reverbs = events<ReverbPerformanceEvent>(track);

  expect(performance.diagnostics.empty() && notes.size() == 2 && notes[0]->header.tick == 0 &&
             notes[1]->header.tick == 4 * 256 && notes[0]->durationTicks == 3 * 256 &&
             !notes[0]->maximumDurationMilliseconds,
         "Mori event prefixes, persistent deltas, zero delay, and tempo-clocked note gates should remain distinct "
         "(notes=" +
             std::to_string(notes.size()) +
             (notes.empty() ? std::string{} : ", first=" + std::to_string(notes.front()->header.tick)) +
             (notes.size() < 2 ? std::string{} : ", second=" + std::to_string(notes[1]->header.tick)) + ")");
  expect(!levels.empty() && std::abs(levels.back()->linearGain - 4.0 / 256.0) < 0.000001,
         "relative volume should use the driver's wrapping eight-bit addition");
  expect(bends.size() == 1 && std::abs(bends.front()->semitones - 2.0) < 0.000001 &&
             bends.front()->normalizedWheelPosition == 0.5 && !ranges.empty() && ranges.back()->cents == 400,
         "the audited eighth-semitone bend range and signed pitch wheel should be preserved");
  expect(tempos.size() == 1 && tempos.back()->microsecondsPerQuarter == 121344000u / 0x80,
         "E6 should halve later countdowns exactly without applying the speed change a second time to tempo "
         "(count=" +
             std::to_string(tempos.size()) +
             (tempos.empty() ? std::string{} : ", last=" + std::to_string(tempos.back()->microsecondsPerQuarter)) +
             ")");
  expect(!reverbs.empty() && reverbs.back()->delayMilliseconds == 48.0 && reverbs.back()->leftGain == -0.5 &&
             reverbs.back()->feedback == 0.5 && reverbs.back()->send == 0.5,
         "CA should retain delay, signed volume, feedback, filter, and wet send");
}

void sourceVoiceScriptChangesFutureReleaseBehavior() {
  std::vector<u8> bytes(kAramSize);
  bytes[0] = 0xc0;
  const u16 descriptorRelative = 0x0200 - 3;
  bytes[1] = static_cast<u8>(descriptorRelative);
  bytes[2] = static_cast<u8>(descriptorRelative >> 8);
  bytes[3] = 4;
  bytes[4] = 3;
  bytes[5] = 0x40;
  bytes[6] = 0x80;
  bytes[7] = 0xde;
  const u16 scriptRelative = 0x0220 - 10;
  bytes[8] = static_cast<u8>(scriptRelative);
  bytes[9] = static_cast<u8>(scriptRelative >> 8);
  bytes[10] = 0x80;
  bytes[11] = 0xd0;

  const auto script = [&](u16 address, u16 row) {
    bytes[address] = 0xde;
    const u16 rowRelative = static_cast<u16>(row - static_cast<u16>(address + 3));
    bytes[address + 1] = static_cast<u8>(rowRelative);
    bytes[address + 2] = static_cast<u8>(rowRelative >> 8);
    bytes[address + 3] = 0xda;
    bytes[address + 4] = 1;
    bytes[address + 5] = 0xcb;
    bytes[address + 6] = 0xfc;
    bytes[address + 7] = 0xff;
  };
  bytes[0x0200] = 0;
  script(0x0201, 0x0300);
  script(0x0220, 0x0310);
  bytes[0x0304] = 2;
  bytes[0x0314] = 5;

  const PerformanceSequence performance = render(std::move(bytes));
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const auto instruments = events<InstrumentPerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 2 && notes[0]->durationTicks == 5 * 256 &&
             notes[1]->durationTicks == 8 * 256 && instruments.size() == 2 &&
             instruments.back()->sourceInstrument &&
             instruments.back()->sourceInstrument->key == (kDirectInstrumentFlag | 0x0220),
         "source DE should select a directly bound voice script for future attacks, including that script's release "
         "delay and instrument row");
}

void zeroDurationNotesReuseTheDriverVoice() {
  std::vector<u8> bytes{
      0x04, 0x00, 0x40, 0x80,  // attack with the hardware gate disabled
      0x80,                    // same pitch: retain the existing voice
      0x04, 0x03, 0x80,        // same pitch: close it with a three-timer gate
      0xd0,
  };
  const PerformanceSequence performance = render(std::move(bytes));
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->header.tick == 0 &&
             notes.front()->durationTicks == 11 * 256 && !notes.front()->maximumDurationMilliseconds,
         "duration zero should reuse a matching hardware voice, and a later tempo-clocked note should close that "
         "attack");
}

void fixedClockModeUsesTimerDurations() {
  std::vector<u8> bytes{
      0xd5, 0x04,
      0x04, 0x03, 0x40, 0x80,
      0xd0,
  };
  const PerformanceSequence performance = render(std::move(bytes));
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->header.tick == 0 &&
             notes.front()->durationTicks == std::numeric_limits<u32>::max() &&
             notes.front()->maximumDurationMilliseconds &&
             std::abs(*notes.front()->maximumDurationMilliseconds - 3 * 9.875) < 0.000001,
         "D5 mode bit 2 should move source waits and voice gates onto the fixed Timer 0 clock");
}

void scannerBuildsScriptedSynthModulation() {
  DriverFixture fixture;
  const ByteReader reader(SourceId{302}, fixture.data());
  const auto layout = findLayout(reader);
  expect(layout && layout->songListAddress == 0x1200 && layout->songHeaderAddress == 0x1300 &&
             layout->spcDirAddress == 0x2000 && layout->presetTableAddress == 0x1000 &&
             layout->panTableAddress == 0x1100 && layout->tracks.size() == 1 &&
             layout->tracks.front().startAddress == 0x1400,
         "MoriSnes signatures should recover the live song, relative tracks, presets, pan law, and DIR");

  const SessionSnapshot snapshot = scan(fixture.data());
  const Collection* collection = firstCollection(snapshot);
  const auto* bank = firstSoundBank(snapshot);
  const Instrument* instrument = bank == nullptr || bank->instruments.empty() ? nullptr : &bank->instruments.front();
  const Region* region = firstRegion(bank);

  expect(snapshot.diagnostics().empty() && collection != nullptr && collection->members.sequence && bank != nullptr &&
             instrument != nullptr && instrument->identity && instrument->identity->key == 0x1500 && region != nullptr,
         "scanner output should bind the descriptor-address identity to a BRR-backed instrument");
  expect(region->modulation.vibrato && region->modulation.tremolo &&
             region->modulation.vibrato->depthMode == ModulationDepthMode::Fixed &&
             region->modulation.tremolo->depthMode == ModulationDepthMode::Fixed &&
             region->modulation.vibrato->delaySeconds &&
             std::abs(region->modulation.vibrato->delaySeconds->minimum - 4 * 0.009875) < 0.000001 &&
             std::abs(region->modulation.vibrato->rateHertz.minimum - 1.0 / (2 * 0.009875)) < 0.000001,
         "instrument mini-scripts should become fixed-clock vibrato and tremolo with their physical delay and rate "
         "(v=" +
             std::to_string(region->modulation.vibrato.has_value()) +
             ", t=" + std::to_string(region->modulation.tremolo.has_value()) +
             (region->modulation.vibrato
                  ? ", delay=" + std::to_string(region->modulation.vibrato->delaySeconds->minimum) +
                        ", rate=" + std::to_string(region->modulation.vibrato->rateHertz.minimum)
                  : std::string{}) +
             ")");
}

void loopingVoicePreludeRemainsSeparateFromItsVibratoCycle() {
  DriverFixture fixture;
  std::vector<u8> bytes = fixture.data();
  const std::vector<u8> script{
      0xde, 0xfc, 0x00,              // DSP row $1600
      0xc5, 0xd2, 0xd8, 0xfe, 0xda,  // attack at -2 semitones
      0x01,
      0xce, 0x05, 0xd9, 0x67, 0x01, 0xdc, 0xfe, 0xcf,  // five-step rise to +3/256
      0xce, 0x0a, 0xdc, 0xfa, 0x01, 0xcf,              // one-shot volume fade
      0x0a,
      0xce, 0x06, 0xd9, 0xf7, 0x01, 0xcf,
      0xce, 0x0c, 0xd9, 0x09, 0x01, 0xcf,
      0xce, 0x06, 0xd9, 0xf7, 0x01, 0xcf,
      0xcb, 0xeb, 0xff,  // repeat the 24-tick +/-54/256 cycle
  };
  std::ranges::copy(script, bytes.begin() + 0x1501);

  const SessionSnapshot snapshot = scan(std::move(bytes));
  const Collection* collection = firstCollection(snapshot);
  const auto* bank = firstSoundBank(snapshot);
  const auto* sequence = collection == nullptr || !collection->members.sequence
                             ? nullptr
                             : snapshot.asset<SequenceProgramAsset>(*collection->members.sequence);
  const Region* region = firstRegion(bank);
  expect(snapshot.diagnostics().empty() && sequence != nullptr && region != nullptr &&
             region->modulation.vibrato && region->modulation.vibrato->delaySeconds &&
             std::abs(region->modulation.vibrato->maxDepthCents - 54 * (100.0 / 256.0)) < 0.000001 &&
             std::abs(region->modulation.vibrato->delaySeconds->minimum - 26 * 0.009875) < 0.000001 &&
             std::abs(region->modulation.vibrato->rateHertz.minimum - 1.0 / (24 * 0.009875)) < 0.000001,
         "a looping script's vibrato should use only its steady cycle, not its one-shot attack ramp" +
             (region && region->modulation.vibrato
                  ? " (depth=" + std::to_string(region->modulation.vibrato->maxDepthCents) +
                        ", delay=" +
                        std::to_string(region->modulation.vibrato->delaySeconds
                                           ? region->modulation.vibrato->delaySeconds->minimum
                                           : -1.0) +
                        ", rate=" + std::to_string(region->modulation.vibrato->rateHertz.minimum) + ")"
                  : std::string{" (missing vibrato)"}));

  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence->program);
  const PerformanceTrack& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  const auto expressions = events<ExpressionPerformanceEvent>(track);
  const auto* pitch = track.automations.empty()
                          ? nullptr
                          : std::get_if<PitchTransitionIntent>(&track.automations.front().intent);
  const auto faded = std::ranges::find_if(expressions, [](const ExpressionPerformanceEvent* expression) {
    return expression->header.tick == 15 * 0x20;
  });
  expect(performance.diagnostics.empty() && !notes.empty() && pitch != nullptr &&
             pitch->timing.timelineTicks == 5 * 0x20 &&
             std::holds_alternative<FixedDurationPitchSlideTiming>(pitch->timing.physical) &&
             std::abs(pitch->targetKey - pitch->startKey - 515.0 / 256.0) < 0.000001 &&
             faded != expressions.end() && std::abs((*faded)->linearGain - 140.0 / 210.0) < 0.000001,
         "the pre-cycle pitch rise and volume fade should remain per-note fixed-clock automation" +
             (pitch ? " (ticks=" + std::to_string(pitch->timing.timelineTicks) +
                          ", delta=" + std::to_string(pitch->targetKey - pitch->startKey) + ")"
                    : std::string{" (missing pitch)"}));
}

void preAttackFinePitchRemainsRelativeToTheSourceNote() {
  DriverFixture fixture;
  std::vector<u8> bytes = fixture.data();

  // Shopping District instrument $1400 applies D9 $88 before KON. The
  // hardware borrows into the inherited source note rather than clamping the
  // isolated script offset to zero.
  const std::vector<u8> script{
      0xde, 0xfc, 0x00,  // DSP row $1600
      0xd9, 0x88,        // -120/256 semitone, relative to the source note
      0xda,
      0xd0,
  };
  std::ranges::copy(script, bytes.begin() + 0x1501);
  bytes[0x1605] = 14;

  const SessionSnapshot snapshot = scan(std::move(bytes));
  const Region* region = firstRegion(firstSoundBank(snapshot));
  const double pitchTableCorrection = 12.0 * std::log2(4286.0 / 4096.0);
  const double expectedUnityKey = 72.0 - (14.0 - 120.0 / 256.0 + pitchTableCorrection);

  expect(snapshot.diagnostics().empty() && region != nullptr &&
             std::abs(region->unityKey - expectedUnityKey) < 0.000001,
         "pre-attack fine pitch should borrow into the inherited source note instead of tuning the region sharp" +
             (region ? " (unity=" + std::to_string(region->unityKey) + ")" : std::string{" (missing region)"}));
}

void physicalVoiceScriptsCanBoundNotes() {
  std::vector<u8> bytes(kAramSize);
  bytes[0] = 4;
  bytes[1] = 0;
  bytes[2] = 0x40;
  bytes[3] = 0xc0;
  const u16 descriptorRelative = 0x0200 - 6;
  bytes[4] = static_cast<u8>(descriptorRelative);
  bytes[5] = static_cast<u8>(descriptorRelative >> 8);
  bytes[6] = 0x80;
  bytes[7] = 0xd0;

  bytes[0x0200] = 0;
  bytes[0x0201] = 0xde;
  const u16 rowRelative = 0x0300 - 0x0204;
  bytes[0x0202] = static_cast<u8>(rowRelative);
  bytes[0x0203] = static_cast<u8>(rowRelative >> 8);
  bytes[0x0204] = 0xda;
  bytes[0x0205] = 5;
  bytes[0x0206] = 0xdb;
  bytes[0x0207] = 0xd0;
  bytes[0x0301] = 0x8f;
  bytes[0x0302] = 0xe0;
  bytes[0x0304] = 3;

  const PerformanceSequence performance = render(std::move(bytes));
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(notes.size() == 1 && notes.front()->maximumDurationMilliseconds &&
             std::abs(*notes.front()->maximumDurationMilliseconds - 5 * 0.009875 * 1000.0) < 0.000001,
         "a voice script's fixed-clock DB should bound a duration-zero note independently of sequence tempo");
}

void physicalVoiceScriptsCanRetriggerNotes() {
  std::vector<u8> bytes(kAramSize);
  bytes[0] = 4;
  bytes[1] = 20;
  bytes[2] = 0x40;
  bytes[3] = 0xc0;
  const u16 descriptorRelative = 0x0200 - 6;
  bytes[4] = static_cast<u8>(descriptorRelative);
  bytes[5] = static_cast<u8>(descriptorRelative >> 8);
  bytes[6] = 0x80;
  bytes[7] = 0xd0;

  bytes[0x0200] = 0;
  bytes[0x0201] = 0xde;
  const u16 rowRelative = 0x0300 - 0x0204;
  bytes[0x0202] = static_cast<u8>(rowRelative);
  bytes[0x0203] = static_cast<u8>(rowRelative >> 8);
  bytes[0x0204] = 0xc5;
  bytes[0x0205] = 0xf0;
  bytes[0x0206] = 0xda;
  bytes[0x0207] = 2;
  bytes[0x0208] = 0xdb;
  bytes[0x0209] = 1;
  bytes[0x020a] = 0xdc;
  bytes[0x020b] = 0xf0;
  bytes[0x020c] = 0xd8;
  bytes[0x020d] = 1;
  bytes[0x020e] = 0xda;
  bytes[0x020f] = 3;
  bytes[0x0210] = 0xdb;
  bytes[0x0211] = 0xd0;
  bytes[0x0301] = 0x8f;
  bytes[0x0302] = 0xe0;

  const PerformanceSequence performance = render(std::move(bytes));
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 2 && notes[0]->header.tick == 0 &&
             notes[0]->maximumDurationMilliseconds &&
             std::abs(*notes[0]->maximumDurationMilliseconds - 2 * 9.875) < 0.000001 &&
             notes[1]->header.tick == 3 * 0x20 && notes[1]->maximumDurationMilliseconds &&
             std::abs(*notes[1]->maximumDurationMilliseconds - 3 * 9.875) < 0.000001 &&
             std::abs(notes[1]->key - notes[0]->key - 1.0) < 0.000001 &&
             std::abs(notes[1]->linearVelocity - 0.5 * 224.0 / 240.0) < 0.000001,
         "hardware voice scripts should preserve repeated KON/KOFF envelope attacks, pitch, volume, and fixed-clock "
         "timing");
}

void liveSongSelectionAndHardwareSoundEffectsAreRecovered() {
  DriverFixture fixture;
  std::vector<u8> pending = fixture.data();
  pending[0x1204] = 0x20;
  pending[0x1205] = 0x13;
  pending[0x1320] = 0;
  const u16 trackRelative = static_cast<u16>(0x1400 - 0x1323);
  pending[0x1321] = static_cast<u8>(trackRelative);
  pending[0x1322] = static_cast<u8>(trackRelative >> 8);
  pending[0x1323] = 0xff;
  pending[0xf4] = 2;
  const auto pendingLayout = findLayout(ByteReader(SourceId{303}, pending));
  expect(pendingLayout && pendingLayout->songIndex == 2 && pendingLayout->songHeaderAddress == 0x1320,
         "a pending positive CPU command should select its real song slot instead of stale slot one");

  std::vector<u8> sfx = fixture.data();
  std::fill_n(sfx.begin() + 0x0212, kTrackCount, u8{0xff});
  sfx[0x38] = 0x15;
  sfx[0x3a] = 0x7f;
  sfx[0x3b] = 0x10;
  sfx[0x122a] = 0x00;
  sfx[0x122b] = 0x17;
  sfx[0x1700] = 0xe8;
  sfx[0x1701] = 0;
  sfx[0x1702] = 0xfc;
  sfx[0x1703] = 0;
  sfx[0x1704] = 0xff;
  sfx[0x1800] = 0xde;
  sfx[0x1801] = 0xfd;
  sfx[0x1802] = 0;
  sfx[0x1803] = 0xc5;
  sfx[0x1804] = 0xf0;
  sfx[0x1805] = 0xd7;
  sfx[0x1806] = 0x2e;
  sfx[0x1807] = 0xda;
  sfx[0x1808] = 5;
  sfx[0x1809] = 0xd9;
  sfx[0x180a] = 0x40;
  sfx[0x180b] = 0xdc;
  sfx[0x180c] = 0xf0;
  sfx[0x180d] = 2;
  sfx[0x180e] = 0xdb;
  sfx[0x180f] = 0xd0;
  sfx[0x1900] = 0;
  sfx[0x1901] = 0x8f;
  sfx[0x1902] = 0xe0;

  const ByteReader reader(SourceId{304}, sfx);
  const auto sfxLayout = findLayout(reader);
  expect(sfxLayout && sfxLayout->songIndex == 0x15 && sfxLayout->tracks.empty() &&
             sfxLayout->sfxVoices.size() == 1 && sfxLayout->sfxVoices.front().scriptAddress == 0x1800,
         "the last hardware-only command should recover its direct voice script after source tracks end");
  SequenceParse parsed = decodeSequence(reader, *sfxLayout, AssetId{400});
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(parsed.program);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const auto instruments = events<InstrumentPerformanceEvent>(performance.tracks.front());
  const auto expressions = events<ExpressionPerformanceEvent>(performance.tracks.front());
  const auto* pitch = performance.tracks.front().automations.empty()
                          ? nullptr
                          : std::get_if<PitchTransitionIntent>(&performance.tracks.front().automations.front().intent);
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->maximumDurationMilliseconds &&
             std::abs(*notes.front()->maximumDurationMilliseconds - 7 * 9.875) < 0.000001 &&
             instruments.size() == 1 && instruments.front()->sourceInstrument &&
             instruments.front()->sourceInstrument->key == (kDirectInstrumentFlag | 0x1800) && pitch != nullptr &&
             pitch->timing.timelineTicks == 5 * 0x20 &&
             std::holds_alternative<FixedDurationPitchSlideTiming>(pitch->timing.physical) &&
             std::abs(pitch->targetKey - 60.25) < 0.000001 && expressions.size() == 2 &&
             expressions.front()->header.tick == 0 && expressions.front()->linearGain == 1.0 &&
             expressions.back()->header.tick == 5 * 0x20 &&
             std::abs(expressions.back()->linearGain - 224.0 / 240.0) < 0.000001,
         "a hardware-only sound effect should render its fixed-clock voice, direct scripted instrument, and finite "
         "pitch/volume script");
}

}  // namespace

void runMoriSnesModuleTests() {
  eventTimingAndAuditedCommandsRenderPhysically();
  zeroDurationNotesReuseTheDriverVoice();
  fixedClockModeUsesTimerDurations();
  sourceVoiceScriptChangesFutureReleaseBehavior();
  scannerBuildsScriptedSynthModulation();
  loopingVoicePreludeRemainsSeparateFromItsVibratoCycle();
  preAttackFinePitchRemainsRelativeToTheSourceNote();
  physicalVoiceScriptsCanBoundNotes();
  physicalVoiceScriptsCanRetriggerNotes();
  liveSongSelectionAndHardwareSoundEffectsAreRecovered();
}
