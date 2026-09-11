/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/FalcomSnes/FalcomSnes.h"

#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::falcom_snes;

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

std::vector<const ModulationPerformanceEvent*> modulationEvents(const PerformanceTrack& track,
                                                                ModulationPerformanceTarget target) {
  auto result = events<ModulationPerformanceEvent>(track);
  std::erase_if(result, [=](const ModulationPerformanceEvent* event) { return event->target != target; });
  return result;
}

class DriverFixture {
public:
  explicit DriverFixture(std::initializer_list<u8> commands) : data_(kAramSize) {
    write(0x0100, {0x4b, 0x67, 0xf7, 0xb9, 0xd4, 0x73, 0xc4, 0x00, 0xfc, 0xf7, 0xb9, 0xc4, 0x01, 0x60, 0x84, 0xba,
                   0xd4, 0x7d, 0x09, 0x01, 0x00, 0xf0, 0x03, 0x18, 0x80, 0x67, 0xfc, 0x3d, 0xc8, 0x08, 0xd0, 0xe0});
    write(0x0200, {0xe8, 0x60, 0x8f, 0x5d, 0xf2, 0xc4, 0xf3});
    write(0x0300, {0xcd, 0x00, 0x75, 0x00, 0x30, 0xf0, 0x03, 0x3d, 0x2f, 0xf8, 0x8d, 0x05,
                   0xcf, 0xfd, 0x7d, 0xf8, 0x00, 0xd5, 0x00, 0x00, 0xf6, 0x00, 0x40});

    word(0x00b9, 0x2000);
    word(0x2000, 0x0020);
    write(0x2018, {1, 2, 3, 4, 6, 8, 12});
    write(0x2020, commands);

    // Program 3 is the first live SRCN. Its five-byte patch has a 1.00
    // multiplier and a valid ADSR envelope.
    write(0x3000, {3, 0xff});
    write(0x400f, {0x8f, 0xe0, 0x00, 0x01, 0x00});
    word(0x6000, 0x6200);
    word(0x6002, 0x6200);
    write(0x6200, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});
  }

  [[nodiscard]] const std::vector<u8>& data() const { return data_; }

private:
  void word(u32 offset, u16 value) {
    data_[offset] = static_cast<u8>(value);
    data_[offset + 1] = static_cast<u8>(value >> 8);
  }

  void write(u32 offset, std::initializer_list<u8> values) { std::ranges::copy(values, data_.begin() + offset); }

  std::vector<u8> data_;
};

PerformanceSequence render(std::initializer_list<u8> commands) {
  const DriverFixture fixture(commands);
  const ByteReader reader(SourceId{240}, fixture.data());
  const auto layout = findLayout(reader);
  expect(layout.has_value(), "FalcomSnes test driver should match its audited signatures");
  SequenceParse parsed = decodeSequence(reader, *layout, AssetId{240});
  return SequenceVm(LoopPolicy::PlayOnce).render(parsed.program);
}

void layoutAndScannerBuildTheCompleteYsVCollection() {
  const DriverFixture fixture({0xd8, 3, 0x00, 1, 0xfc, 0, 0});
  const ByteReader reader(SourceId{241}, fixture.data());
  const auto layout = findLayout(reader);
  expect(layout && layout->sequenceHeaderAddress == 0x2000 && layout->trackStarts[0] == 0x2020 &&
             layout->instrumentSrcnMapAddress == 0x3000 && layout->instrumentTableAddress == 0x4000 &&
             layout->spcDirAddress == 0x6000,
         "FalcomSnes signatures should recover the live sequence, dynamic instrument map, patch table, and DIR");

  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "Ys V fixture.aram"}, fixture.data());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const Collection* collection = snapshot.collections().empty() ? nullptr : &snapshot.collections().front();
  expect(snapshot.collections().size() == 1 && collection->members.sequence &&
             collection->members.soundBanks.size() == 1 && collection->members.samplePools.empty(),
         "FalcomSnes scanning should publish a sequence and self-contained sound bank");

  const auto* set = snapshot.asset<SoundBankAsset>(collection->members.soundBanks.front());
  const Instrument* instrument = set != nullptr && set->instruments.size() == 1 ? &set->instruments.front() : nullptr;
  const Region* region =
      instrument != nullptr && instrument->regions.size() == 1 ? &instrument->regions.front() : nullptr;
  constexpr double kPitchTableC6 = 0x10be / 4096.0;
  const double expectedUnity = 96.0 - std::log2(kPitchTableC6) * 12.0;
  expect(instrument != nullptr && instrument->identity && instrument->identity->key == 3 && region != nullptr &&
             std::abs(region->unityKey - expectedUnity) < 0.000001 && region->envelope.attackSeconds,
         "dynamic program IDs should resolve to live SRCNs with the driver's 8.8 root-key and ADSR math");
}

void noteTimingLegatoAndMixerStateMatchTheDriver() {
  const PerformanceSequence performance =
      render({0xd2, 0xd8, 3, 0xde, 0x7f, 0xe7, 0x40, 0xdd, 0x80, 0x08, 8, 0x00, 8, 0xfc, 0, 0});
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 2 && notes[0]->header.tick == 3 &&
             std::abs(notes[0]->key - 48.0) < 0.000001 && notes[0]->durationTicks == 8 && notes[1]->header.tick == 11 &&
             notes[1]->durationTicks == 4 && notes[1]->extendsPrevious,
         "octave, first-program startup delay, slur ties, and quantized key-off timing should match the SPC700");

  const auto levels = events<LevelPerformanceEvent>(performance.tracks.front());
  const auto balances = events<StereoBalancePerformanceEvent>(performance.tracks.front());
  expect(!levels.empty() && std::abs(levels.back()->linearGain - 1.0) < 0.000001 && !balances.empty() &&
             std::abs(balances.back()->leftGain - 63.0 / 127.0) < 0.000001 &&
             std::abs(balances.back()->rightGain - 63.0 / 127.0) < 0.000001,
         "deferred volume and pan writes should be applied with Ys V's exact linear DSP balance table on attack");

  const PerformanceSequence adsrFirst = render({0xf2, 0x8f, 0xe0, 0x00, 1, 0xfc, 0, 0});
  const auto adsrFirstNotes = events<NotePerformanceEvent>(adsrFirst.tracks.front());
  expect(adsrFirst.diagnostics.empty() && adsrFirstNotes.size() == 1 && adsrFirstNotes.front()->header.tick == 3,
         "F2 should pass through the same one-shot startup latch as the D8 instrument command");
}

void modulationDynamicAdsrAndEchoRemainPhysical() {
  const PerformanceSequence performance = render({
      0xd8, 3,    0xd9, 1,    65,   64, 0xea, 127, 8,    2, 0xf0, 0, 128,  2, 0xf2, 0x8f,
      0xe0, 0xf9, 32,   0xe0, 0xf7, 9,  0xc0, 2,   0xf6, 1, 0x00, 8, 0xfc, 0, 0,
  });
  const PerformanceTrack& track = performance.tracks.front();
  const auto vibratoDepth = modulationEvents(track, ModulationPerformanceTarget::VibratoDepth);
  const auto vibratoRate = modulationEvents(track, ModulationPerformanceTarget::VibratoRate);
  const auto panDepth = modulationEvents(track, ModulationPerformanceTarget::PanDepth);
  const auto panRate = modulationEvents(track, ModulationPerformanceTarget::PanRate);
  expect(performance.diagnostics.empty() && vibratoDepth.size() == 1 && vibratoRate.size() == 1 &&
             vibratoDepth[0]->pitchDepthSemitones && std::abs(*vibratoDepth[0]->pitchDepthSemitones - 1.0) < 0.000001 &&
             vibratoDepth[0]->context.delayTicks == 2 && vibratoDepth[0]->context.shape &&
             vibratoDepth[0]->context.shape->samples.size() == 256 &&
             vibratoRate[0]->context.cyclesPerTick == 0.25 && panDepth.size() == 1 &&
             panDepth[0]->panDepth == 1.0 && panDepth[0]->context.shape &&
             panDepth[0]->context.shape->samples.size() == 32 && panRate[0]->context.cyclesPerTick == 0.03125,
         "vibrato and pan LFO events should retain the driver's integer waveforms, delay, depth, and tick rates");

  const auto envelopes = events<EnvelopePerformanceEvent>(track);
  const auto dynamic = std::ranges::find_if(
      envelopes, [](const EnvelopePerformanceEvent* event) { return event->update.values.has_value(); });
  expect(dynamic != envelopes.end() && (*dynamic)->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks &&
             (*dynamic)->update.values->releaseSeconds == snesDspEnvelope(0x8f, 0xe0, 0).releaseSeconds,
         "F2 should immediately replace the active voice ADSR and retain it for future attacks");

  const auto bends = events<PitchBendPerformanceEvent>(track);
  expect(bends.size() >= 2 && std::abs(bends[0]->semitones - 0.5) < 0.000001 &&
             std::abs(bends[1]->semitones - 1.0) < 0.000001,
         "the repeating pitch envelope should apply its unsigned 8.8 step at the signed interval");

  const auto reverbs = events<ReverbPerformanceEvent>(track);
  const ReverbPerformanceEvent* echo = reverbs.empty() ? nullptr : reverbs.back();
  expect(echo != nullptr && echo->voiceMask == 1 && echo->delayMilliseconds == 112.0 && echo->feedback == -0.5 &&
             echo->filterIndex == 2 && echo->leftGain == 0.25 && echo->rightGain == -0.25 &&
             std::abs(echo->send - 32.0 / 127.0) < 0.000001,
         "echo should preserve EON, clamped EDL, signed EFB/EVOL, wet send, and FIR preset identity");
}

void loopsAndMutableFirPresetsFollowDriverMemory() {
  const PerformanceSequence loop = render({
      0xed,
      2,
      0,
      0x00,
      1,
      0xee,
      5,
      0,
      0x00,
      1,
      0xef,
      0xf5,
      0xff,
      0xfc,
      0,
      0,
  });
  const auto loopNotes = events<NotePerformanceEvent>(loop.tracks.front());
  expect(loop.diagnostics.empty() && loopNotes.size() == 3,
         "repeat counters and the final-iteration break should follow the driver's writable counter cell");

  const PerformanceSequence fir = render({
      0xfa, 2, 1, 2, 3, 4, 5, 6, 7, 8, 0xf7, 1, 0, 2, 0xf4, 0x1f, 0xf5, 1, 0xfc, 0, 0,
  });
  const auto reverbs = events<ReverbPerformanceEvent>(fir.tracks.front());
  expect(fir.diagnostics.empty() && !reverbs.empty() && !reverbs.back()->filterIndex,
         "an overwritten FIR preset must not be mislabeled as one of the driver's immutable built-in filters");
}

}  // namespace

void runFalcomSnesModuleTests() {
  layoutAndScannerBuildTheCompleteYsVCollection();
  noteTimingLegatoAndMixerStateMatchTheDriver();
  modulationDynamicAdsrAndEchoRemainPhysical();
  loopsAndMutableFirPresetsFollowDriverMemory();
}
