/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/RareSnes/RareSnes.h"

#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::rare_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeBytes(std::vector<u8>& bytes, u32 offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + offset);
}

void writeLe16(std::vector<u8>& bytes, u32 offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void writeStandardHeader(std::vector<u8>& bytes, u16 address) {
  writeLe16(bytes, address, 0x4000);
  bytes[address + 16] = 0x80;
  bytes[0x4000] = 0;
}

std::vector<u8> laterDriverFixture(Profile profile) {
  std::vector<u8> bytes(kAramSize);
  constexpr u16 dispatch = 0x1000;
  constexpr u16 header = 0x2000;
  writeBytes(bytes, 0x0200,
             {0x8d, 0x00, 0xf7, 0xe5, 0x30, 0x06, 0x4d, 0x1c, 0x5d, 0x1f, static_cast<u8>(dispatch),
              static_cast<u8>(dispatch >> 8)});
  writeBytes(bytes, 0x0220,
             {0xe8, 0x01, 0xd4, 0x00, 0xd5, 0x10, 0x01, 0xf7, 0xe5, 0xd4, 0x00, 0xfc, 0xf7, 0x00, 0xd4, 0x00});
  writeLe16(bytes, 0xe5, header);
  writeStandardHeader(bytes, header);

  const auto handler = [&](u8 opcode, u16 address = 0x3000) {
    writeLe16(bytes, dispatch + static_cast<u32>(opcode) * 2, address);
  };
  if (profile != Profile::KillerInstinct) {
    handler(0x0c);
  }
  if (profile != Profile::WinningRun) {
    handler(0x19);
    handler(0x1a);
    handler(0x1b);
  } else {
    handler(0x20);
    handler(0x22);
  }
  if (profile == Profile::KillerInstinctBeta) {
    handler(0x24);
    handler(0x25);
    handler(0x2d);
  }
  return bytes;
}

u8 defaultTimer(Profile profile) {
  return profile == Profile::DonkeyKongCountry || profile == Profile::BattletoadsDoubleDragon ||
                 profile == Profile::KillerInstinctBeta
             ? 0x3c
             : 0x64;
}

PerformanceSequence render(Profile profile, std::vector<u8> bytes, u32 floor = 0, u32 trackNumber = 0,
                           bool monoOutput = false) {
  const auto& config = sequenceConfig();
  TrackProgram track = decodeSourceTrack(ByteReader(SourceId{90}, bytes), profile, trackNumber, 0, floor);
  SequenceProgram program{
      .runtime = sequenceRuntime(profile, 0x80, defaultTimer(profile), monoOutput),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {std::move(track)},
  };
  const double initialChannelGain = profile == Profile::KillerInstinct ? 0.5 : 127.0 / 128.0;
  program.behavior.initialStereoBalance = StereoBalance{initialChannelGain, initialChannelGain};
  return SequenceVm(LoopPolicy::PlayOnce).render(program);
}

template <class Event>
std::vector<const Event*> events(const PerformanceSequence& sequence) {
  std::vector<const Event*> result;
  for (const PerformanceEvent& event : sequence.tracks.front().events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      result.push_back(typed);
    }
  }
  return result;
}

}  // namespace

void rareSnesLayoutsDifferentiateDriverFamilies() {
  for (const Profile expected :
       {Profile::KillerInstinct, Profile::WinningRun, Profile::KillerInstinctBeta, Profile::DonkeyKongCountry2}) {
    const std::vector<u8> bytes = laterDriverFixture(expected);
    const auto layout = findLayout(ByteReader(SourceId{96}, bytes));
    expect(layout && layout->profile == expected,
           std::string("later Rare driver should classify as ") + profileName(expected));
  }

  std::vector<u8> battletoads(kAramSize);
  constexpr u16 earlierDispatch = 0x1200;
  writeBytes(battletoads, 0x0200,
             {0x8d, 0x00, 0xf7, 0x04, 0x68, 0x00, 0x30, 0x06, 0x4d, 0x1c, 0x5d, 0x1f, static_cast<u8>(earlierDispatch),
              static_cast<u8>(earlierDispatch >> 8)});
  writeStandardHeader(battletoads, 0x0f00);
  const auto battletoadsLayout = findLayout(ByteReader(SourceId{97}, battletoads));
  expect(battletoadsLayout && battletoadsLayout->profile == Profile::BattletoadsDoubleDragon,
         "earlier direct-SRCN Rare driver should classify as Battletoads & Double Dragon");
  expect(battletoadsLayout->monoOutput, "Battletoads & Double Dragon should expose its always-mono mixer");

  std::vector<u8> dkc = battletoads;
  constexpr u16 dkcHeader = 0x2200;
  writeBytes(dkc, 0x0300, {0x4d, 0xf7, 0x00, 0x5d, 0xf5, 0x00, 0x30, 0xce});
  writeBytes(dkc, 0x0320,
             {0xe8, 0x01, 0xd4, 0x00, 0xd5, 0x10, 0x01, 0xf6, static_cast<u8>(dkcHeader),
              static_cast<u8>(dkcHeader >> 8), 0xd4, 0x00, 0xf6, 0x00, 0x00, 0xd4, 0x00});
  writeStandardHeader(dkc, dkcHeader);
  const auto dkcLayout = findLayout(ByteReader(SourceId{98}, dkc));
  expect(dkcLayout && dkcLayout->profile == Profile::DonkeyKongCountry,
         "earlier table-SRCN Rare driver should classify as Donkey Kong Country");

  std::vector<u8> battlemaniacs(kAramSize);
  writeBytes(battlemaniacs, 0x0200, {0xf6, 0x00, 0x0f, 0xc4, 0x01});
  writeBytes(battlemaniacs, 0x0220, {0xf6, 0xa0, 0x0f, 0xd4, 0x8b, 0xf6, 0xa1, 0x0f, 0xd4, 0x91, 0xfc});
  battlemaniacs[0x0f00] = 0x80;
  for (u32 track = 0; track < 6; ++track) {
    const u16 start = static_cast<u16>(0x4000 + track);
    writeLe16(battlemaniacs, 0x0fa0 + track * 2, start);
    battlemaniacs[0x8b + track] = static_cast<u8>(start);
    battlemaniacs[0x91 + track] = static_cast<u8>(start >> 8);
  }
  const auto battlemaniacsLayout = findLayout(ByteReader(SourceId{99}, battlemaniacs));
  expect(battlemaniacsLayout && battlemaniacsLayout->profile == Profile::Battlemaniacs &&
             battlemaniacsLayout->battlemaniacsSong == 0,
         "six-track Rare branch should infer Battlemaniacs song zero from runtime cursors");
}

void rareSnesProfilesDecodeTheirDistinctOpcodeTails() {
  const std::vector<u8> battlemaniacsBytes{
      0x03, 0x02, 0x8f, 0xe0, 0x7f, 0x40, 0x20, 0x00, 0x04, 0x00, 0x08, 0x08, 0x01, 0x02, 0x0a, 0x00, 0x00, 0x04,
      0x01, 0x02, 0x21, 0x04, 0x20, 0x10, 0x08, 0x22, 0x7f, 0,    0,    0,    0,    0,    0,    0,    0x2a, 0x40,
      0x20, 0x8f, 0xe0, 0x04, 0x2e, 0x32, 0xff, 0xff, 0x01, 0x04, 0x33, 0x00, 0x06, 0x08, 0x81, 0x00,
  };
  const PerformanceSequence battlemaniacs = render(Profile::Battlemaniacs, battlemaniacsBytes);
  const auto battlemaniacsNotes = events<NotePerformanceEvent>(battlemaniacs);
  expect(battlemaniacs.diagnostics.empty() && battlemaniacsNotes.size() == 1,
         "Battlemaniacs embedded instruments, echo, presets, and fades should render");

  const PerformanceSequence battlemaniacsMasterFade = render(Profile::Battlemaniacs, {
                                                                                         0x0f,
                                                                                         0x40,
                                                                                         0x20,
                                                                                         0x32,
                                                                                         0xf0,
                                                                                         0x08,
                                                                                         0x01,
                                                                                         0x02,
                                                                                         0x80,
                                                                                         4,
                                                                                         0x00,
                                                                                     });
  expect(battlemaniacsMasterFade.tracks.front().automations.size() == 1,
         "Battlemaniacs master fade should produce one structured automation");
  const auto* masterFade = std::get_if<ScalarPerformanceAutomationIntent>(
      &battlemaniacsMasterFade.tracks.front().automations.front().intent);
  expect(masterFade != nullptr && masterFade->target == PerformanceAutomationTarget::MasterLevel &&
             masterFade->durationTicks == 4 && masterFade->targetValue &&
             std::abs(*masterFade->targetValue - 0.375) < 0.000001,
         "Battlemaniacs master fade should apply independent signed DSP steps from the current L/R master levels");

  const PerformanceSequence battlemaniacsPercussion =
      render(Profile::Battlemaniacs,
             {
                 0x03, 0x02, 0x03, 0x8f, 0xe0, 0x7f, 0x40, 0x20, 0x00, 0x04, 37,   0x0c, 0x02, 0x30, 0x18, 0x0d, 0x02,
                 0x8e, 0xe0, 0x03, 0x15, 0x02, 0x7f, 0x03, 0x16, 0x7f, 0x33, 0x02, 0x02, 0x06, 0x08, 0xa8, 0x00,
             },
             0, 5);
  const auto percussionNotes = events<NotePerformanceEvent>(battlemaniacsPercussion);
  const auto percussionBalances = events<StereoBalancePerformanceEvent>(battlemaniacsPercussion);
  const auto percussionReverb = events<ReverbPerformanceEvent>(battlemaniacsPercussion);
  expect(battlemaniacsPercussion.diagnostics.empty() && percussionNotes.size() == 1 &&
             percussionNotes.front()->key == 72.0 && !percussionBalances.empty() &&
             std::abs(percussionBalances.back()->leftGain - 0.375) < 0.000001 &&
             std::abs(percussionBalances.back()->rightGain - 0.1875) < 0.000001 && !percussionReverb.empty() &&
             percussionReverb.back()->voiceMask == 0x20,
         "Battlemaniacs percussion commands should update the selected patch and project its EON flag");

  const PerformanceSequence battletoads =
      render(Profile::BattletoadsDoubleDragon,
             {
                 0x01, 0x03, 0x02, 0x60, 0x20, 0x10, 0x8f, 0xe0, 0x1c, 0x60, 0x20, 0x8f, 0xe0, 0x21,
                 0x26, 0,    1,    4,    2,    0x28, 0x04, 0x50, 0x30, 0x2a, 0x3c, 0x81, 8,    0x00,
             });
  expect(battletoads.diagnostics.empty() && events<NotePerformanceEvent>(battletoads).size() == 1,
         "Battletoads/Double Dragon direct SRCN and preset tail should render");
  const std::vector<u8> battletoadsNopBytes{
      0x15, 1, 2, 3, 4, 0x81, 1, 0x00,
  };
  const TrackProgram battletoadsNop =
      decodeSourceTrack(ByteReader(SourceId{93}, battletoadsNopBytes), Profile::BattletoadsDoubleDragon, 0, 0);
  expect(battletoadsNop.commands.size() == 3 && battletoadsNop.commands.front().range.size == 5,
         "Battletoads/Double Dragon opcode 15 should consume its four reserved bytes");

  const PerformanceSequence dkc = render(
      Profile::DonkeyKongCountry,
      {
          0x01, 0x03, 0x02, 0x60, 0x20, 0x1c, 0x60, 0x20, 0x8f, 0xe0, 0x21, 0x2f, 8, 2, 2, 1, 0x30, 0x81, 8, 0x00,
      });
  expect(dkc.diagnostics.empty() && events<NotePerformanceEvent>(dkc).size() == 1,
         "DKC volume/ADSR presets and tremolo should render");

  const PerformanceSequence kiBeta =
      render(Profile::KillerInstinctBeta,
             {
                 0x01, 0x03, 0x24, 0x25, 0x11, 1, 2, 0, 0x10, 0x60, 0x2f, 8, 2, 2, 1, 0x30, 0x81, 8, 0x00,
             });
  expect(kiBeta.diagnostics.empty() && !kiBeta.tracks.front().automations.empty(),
         "KI beta all-LFO-off and bounded stereo motion should remain distinct from release KI");

  const std::vector<u8> winningBytes{
      0x01, 0x03, 0x20, 50, 0x21, 0x40, 0x22, 0xaa, 0xbb, 0x24, 0x25, 0x11, 1, 2, 0, 0x10, 0x60, 0x81, 8, 0x00,
  };
  TrackProgram winningTrack = decodeSourceTrack(ByteReader(SourceId{91}, winningBytes), Profile::WinningRun, 0, 0);
  const auto nop = std::ranges::find(winningTrack.commands, u8{0x22}, &SourceCommand::opcode);
  expect(nop != winningTrack.commands.end() && nop->range.size == 3,
         "Winning Run opcode 22 is a two-operand NOP, not legacy's three-operand unknown");
  const PerformanceSequence winning = render(Profile::WinningRun, winningBytes);
  expect(winning.diagnostics.empty() && events<MasterLevelPerformanceEvent>(winning).size() == 1,
         "Winning Run percent master volume should render as a linear master gain");

  const PerformanceSequence ki = render(Profile::KillerInstinct, {
                                                                     0x1e,
                                                                     0x40,
                                                                     0x20,
                                                                     0x21,
                                                                     0x22,
                                                                     3,
                                                                     2,
                                                                     16,
                                                                     0x23,
                                                                     8,
                                                                     0x81,
                                                                     8,
                                                                     0x00,
                                                                 });
  expect(ki.diagnostics.empty() && events<NotePerformanceEvent>(ki).size() == 1,
         "Killer Instinct ADSR reset and short voice parameters should render");

  const PerformanceSequence dkc2 =
      render(Profile::DonkeyKongCountry2, {
                                              0x1e, 0x60, 0x20, 0x30, 0x10, 0x20, 0x22, 3,    2, 16,   0x60,
                                              0x20, 0x8f, 0xe0, 0x24, 50,   0x31, 0x32, 0x81, 8, 0x00,
                                          });
  expect(dkc2.diagnostics.empty() && events<MasterLevelPerformanceEvent>(dkc2).size() == 1,
         "DKC2 voice parameters, two volume presets, and echo-off alias should render");
}

void rareSnesSignedStereoVolumesPreserveDriverRelativeLevels() {
  const PerformanceSequence performance = render(Profile::DonkeyKongCountry, {0x02, 0x40, 0x20, 0x81, 4, 0x00});
  const auto balances = events<StereoBalancePerformanceEvent>(performance);
  expect(performance.diagnostics.empty() && balances.size() == 2,
         "Rare stereo volume should follow the initial balance with one exact source channel-gain event");
  expect(std::abs(balances.back()->leftGain - 0.5) < 0.000001 && std::abs(balances.back()->rightGain - 0.25) < 0.000001,
         "Rare signed DSP volumes should be normalized independently, without legacy sqrt pan attenuation");

  const PerformanceSequence inverted = render(Profile::DonkeyKongCountry, {0x02, 0x80, 0x7f, 0x81, 4, 0x00});
  const auto invertedBalance = events<StereoBalancePerformanceEvent>(inverted);
  expect(invertedBalance.size() == 2 && std::abs(invertedBalance.back()->leftGain - 1.0) < 0.000001 &&
             std::abs(invertedBalance.back()->rightGain - (127.0 / 128.0)) < 0.000001,
         "DSP phase inversion should retain its magnitude instead of overflowing signed -128");

  const PerformanceSequence dkc2Mono =
      render(Profile::DonkeyKongCountry2, {0x02, 0xc0, 0x20, 0x81, 4, 0x00}, 0, 0, true);
  const auto dkc2MonoBalance = events<StereoBalancePerformanceEvent>(dkc2Mono);
  expect(dkc2MonoBalance.size() == 2 && std::abs(dkc2MonoBalance.back()->leftGain - 0.375) < 0.000001 &&
             std::abs(dkc2MonoBalance.back()->rightGain - 0.375) < 0.000001,
         "later Rare mono mode should average absolute signed channel magnitudes exactly as the driver");

  const auto& config = sequenceConfig();
  const std::vector<u8> firstTrackBytes{
      0x1c, 0x40, 0x20, 0x8f, 0xe0, 0x80, 1, 0x21, 0x81, 1, 0x00,
  };
  const std::vector<u8> secondTrackBytes{
      0x1c, 0x10, 0x08, 0x8e, 0xe0, 0x81, 2, 0x00,
  };
  SequenceProgram presetProgram{
      .runtime = sequenceRuntime(Profile::DonkeyKongCountry, 0x80, 0x3c),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks =
          {
              decodeSourceTrack(ByteReader(SourceId{94}, firstTrackBytes), Profile::DonkeyKongCountry, 0, 0),
              decodeSourceTrack(ByteReader(SourceId{95}, secondTrackBytes), Profile::DonkeyKongCountry, 1, 0),
          },
  };
  presetProgram.behavior.initialStereoBalance = StereoBalance{127.0 / 128.0, 127.0 / 128.0};
  const PerformanceSequence localPresets = SequenceVm(LoopPolicy::PlayOnce).render(presetProgram);
  const auto firstTrackBalances = events<StereoBalancePerformanceEvent>(localPresets);
  expect(localPresets.diagnostics.empty() && firstTrackBalances.size() == 2 &&
             std::abs(firstTrackBalances.back()->leftGain - 0.5) < 0.000001 &&
             std::abs(firstTrackBalances.back()->rightGain - 0.25) < 0.000001,
         "Rare volume/envelope presets should remain local to the voice that saved them");
}

void rareSnesPhysicalLfosAndPitchEnvelopesUseTimerClock() {
  const PerformanceSequence performance =
      render(Profile::DonkeyKongCountry, {
                                             0x0f, 8, 2, 4, 3, 0x2f, 8, 2, 4, 3, 0x08, 0, 1, 4, 4, 0, 0x81, 16, 0x00,
                                         });
  const auto modulation = events<ModulationPerformanceEvent>(performance);
  const bool vibratoDepth = std::ranges::any_of(modulation, [](const ModulationPerformanceEvent* event) {
    return event->target == ModulationPerformanceTarget::VibratoDepth && event->pitchDepthSemitones &&
           *event->pitchDepthSemitones > 0.0 && event->delayMilliseconds;
  });
  const bool vibratoRate = std::ranges::any_of(modulation, [](const ModulationPerformanceEvent* event) {
    return event->target == ModulationPerformanceTarget::VibratoRate && event->frequencyHz && *event->frequencyHz > 0.0;
  });
  const bool tremoloDepth = std::ranges::any_of(modulation, [](const ModulationPerformanceEvent* event) {
    return event->target == ModulationPerformanceTarget::TremoloDepth && event->volumeDepthLinearGain &&
           *event->volumeDepthLinearGain > 0.0;
  });
  const bool tremoloRate = std::ranges::any_of(modulation, [](const ModulationPerformanceEvent* event) {
    return event->target == ModulationPerformanceTarget::TremoloRate && event->frequencyHz && *event->frequencyHz > 0.0;
  });
  expect(performance.diagnostics.empty() && vibratoDepth && vibratoRate && tremoloDepth && tremoloRate,
         "Rare vibrato and tremolo should retain physical triangle depth, rate, and delay");
  const auto activeRate = std::ranges::find_if(modulation, [](const ModulationPerformanceEvent* event) {
    return event->target == ModulationPerformanceTarget::VibratoRate && event->frequencyHz.value_or(0.0) > 0.0;
  });
  expect(activeRate != modulation.end() &&
             std::abs((*activeRate)->frequencyHz.value() - (1.0 / (8 * 2 * 2 * 0.0075))) < 0.000001,
         "Rare triangle LFO rate should include both halves of its driver period");
  expect(std::ranges::any_of(performance.tracks.front().automations,
                             [](const PerformanceAutomation& automation) {
                               const auto* pitch = pitchTransitionIntent(automation);
                               return pitch != nullptr &&
                                      std::holds_alternative<FixedDurationPitchSlideTiming>(pitch->timing.physical);
                             }),
         "Rare pitch envelopes should remain fixed-duration note pitch transitions");

  const PerformanceSequence delayedNote =
      render(Profile::DonkeyKongCountry, {0x0f, 8, 2, 4, 3, 0x80, 32, 0x81, 16, 0x00});
  const MidiSequence simulated =
      renderMidiSequence(delayedNote, {}, ModulationConversionPolicy::SequenceEventSimulation);
  expect(std::ranges::none_of(simulated.tracks.front().events,
                              [](const MidiEvent& event) {
                                const auto* bend = std::get_if<PitchBend>(&event);
                                return bend != nullptr && bend->tick < 32 && bend->value != 0;
                              }),
         "Rare vibrato configuration should not generate pitch bends during rests before the first note");
}

void rareSnesPitchEnvelopeInvertsOnlyItsInitialSteps() {
  // Gang-Plank Galleon uses this envelope at $12e2 and $1608. The driver
  // subtracts the delta for the first step, then adds it for the remaining 22.
  const PerformanceSequence performance =
      render(Profile::DonkeyKongCountry, {0x08, 1, 1, 0x17, 0x17, 1, 0xa3, 0x40, 0x0a, 0x00});
  expect(performance.diagnostics.empty() && performance.tracks.front().automations.size() == 1,
         "Gang-Plank Galleon's pitch envelope should render as one transition");

  const auto* pitch = pitchTransitionIntent(performance.tracks.front().automations.front());
  const auto* curve = pitch == nullptr ? nullptr : std::get_if<SampledAutomationCurve>(&pitch->curve);
  expect(pitch != nullptr && pitch->targetKey > pitch->startKey && curve != nullptr && curve->samples.size() > 3 &&
             curve->samples[1].value <= pitch->startKey && curve->samples.back().value == pitch->targetKey,
         "the initial inverted step should precede the remaining upward driver steps");

  const MidiSequence pitchBend =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  expect(std::ranges::count_if(pitchBend.tracks.front().events,
                               [](const MidiEvent& event) {
                                 const auto* bend = std::get_if<PitchBend>(&event);
                                 return bend != nullptr && bend->value > 0;
                               }) > 3,
         "pitch-bend rendering should step upward instead of popping to Gang-Plank Galleon's target");

  const MidiSequence portamento =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento});
  expect(std::ranges::any_of(portamento.tracks.front().events,
                             [](const MidiEvent& event) {
                               const auto* note = std::get_if<NoteDuration>(&event);
                               return note != nullptr && note->key > 70;
                             }),
         "portamento rendering should glide Gang-Plank Galleon's A3 event upward");
}

void rareSnesCallsConditionalBranchesAndLongDurationsExecuteSourceFree() {
  const PerformanceSequence repeated = render(Profile::DonkeyKongCountry, {
                                                                              0x04,
                                                                              2,
                                                                              0x06,
                                                                              0x00,
                                                                              0x00,
                                                                              0xff,
                                                                              0x81,
                                                                              2,
                                                                              0x05,
                                                                          });
  const size_t repeatedNotes = events<NotePerformanceEvent>(repeated).size();
  expect(repeated.diagnostics.empty() && repeatedNotes == 2 && repeated.tracks.front().endTick == 4,
         "Rare repeat calls should replay their subroutine and return to the encoded continuation (notes=" +
             std::to_string(repeatedNotes) + ", tick=" + std::to_string(repeated.tracks.front().endTick) +
             ", diagnostics=" + std::to_string(repeated.diagnostics.size()) + ")");

  const std::vector<u8> reusedPatternBytes{
      0x04, 1, 13, 0,  // call the same pattern from two source sites
      0x04, 1, 13, 0, 0x81, 2, 0x00, 0x00, 0x00, 0x82, 2, 0x05,
  };
  const PerformanceSequence reusedPattern = render(Profile::DonkeyKongCountry, reusedPatternBytes);
  const size_t reusedPatternNotes = events<NotePerformanceEvent>(reusedPattern).size();
  expect(reusedPattern.diagnostics.empty() && reusedPatternNotes == 3 && reusedPattern.tracks.front().endTick == 6,
         "Rare calls to a reused pattern should remain finite and continue after each return (notes=" +
             std::to_string(reusedPatternNotes) + ", tick=" + std::to_string(reusedPattern.tracks.front().endTick) +
             ", diagnostics=" + std::to_string(reusedPattern.diagnostics.size()) + ")");

  const PerformanceSequence longDuration = render(Profile::DonkeyKongCountry, {
                                                                                  0x2b,
                                                                                  0x06,
                                                                                  0x00,
                                                                                  0x04,
                                                                                  0x81,
                                                                                  0x07,
                                                                                  0x80,
                                                                                  0x00,
                                                                                  0x02,
                                                                                  0x00,
                                                                              });
  expect(longDuration.diagnostics.empty() && longDuration.tracks.front().endTick == 6,
         "Rare 16-bit default and inline durations should preserve their stateful widths");

  const PerformanceSequence zeroDefault = render(Profile::DonkeyKongCountry, {0x06, 0x00, 0x81, 3, 0x00});
  const auto zeroDefaultNotes = events<NotePerformanceEvent>(zeroDefault);
  expect(
      zeroDefault.diagnostics.empty() && zeroDefaultNotes.size() == 1 && zeroDefaultNotes.front()->durationTicks == 3,
      "Rare default duration zero should keep consuming inline note durations");

  std::vector<u8> conditional(24, 0);
  conditional[0] = 0x2e;
  conditional[1] = 1;
  conditional[2] = 0x2d;
  conditional[3] = 12;
  conditional[4] = 0;
  conditional[5] = 16;
  conditional[6] = 0;
  conditional[12] = 0x80;
  conditional[13] = 1;
  conditional[14] = 0;
  conditional[16] = 0x81;
  conditional[17] = 3;
  conditional[18] = 0;
  const PerformanceSequence branched = render(Profile::DonkeyKongCountry, conditional, 8);
  const auto branchNotes = events<NotePerformanceEvent>(branched);
  expect(branched.diagnostics.empty() && branchNotes.size() == 1 && branchNotes.front()->durationTicks == 3,
         "DKC conditional jump should select the indexed destination at runtime (notes=" +
             std::to_string(branchNotes.size()) + ", tick=" + std::to_string(branched.tracks.front().endTick) +
             ", diagnostics=" + std::to_string(branched.diagnostics.size()) + ")");
}
