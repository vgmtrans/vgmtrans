/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ChunSnes/ChunSnes.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"

#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace vgmtrans;
using namespace vgmtrans::core;
using namespace vgmtrans::formats::chun_snes;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

void runChunSnesModuleTests() {
  std::vector<u8> aram(kAramSize);
  aram[0x200] = 120;
  aram[0x201] = 1;
  aram[0x202] = 0x10;
  const std::vector<u8> commands{
      0xf0, 0x03,        // program
      0xef, 0x8f, 0xe5,  // dynamic ADSR
      0xdd, 0x04,        // gated release rate
      0xe2, 0x00,        // pitch script
      0xf9,              // top-level pattern end is a no-op
      0xfc,              // echo on
      0xfb, 0x01, 0x06,  // slide the following note upward
      0xed, 0x46,        // channel master volume
      0xf6, 0xfe,        // channel volume
      0xe6, 0x78, 0x60,  // first volume fade
      0x51, 0x60,        // C, length 96
      0xe6, 0x18, 0x48,  // second stage, down to near-silence
      0x51, 0x48,        // C, length 72
      0xfd,              // echo off
      0xff,              // end
  };
  std::ranges::copy(commands, aram.begin() + 0x210);

  aram[0x300] = 0x02;
  aram[0x301] = 0x03;
  const std::vector<u8> pitchScript{
      0x1e, 0x16,  // end and loop offsets
      0x00, 0x03, 0x01, 0x03, 0x00, 0x03, 0xff, 0x03, 0x00, 0x03, 0x02, 0x03, 0x00, 0x03,
      0xfc, 0x03, 0x00, 0x03, 0x08, 0x03, 0x00, 0x03, 0xf4, 0x03, 0x00, 0x03, 0x0c, 0x03,
  };
  std::ranges::copy(pitchScript, aram.begin() + 0x302);

  const ByteReader reader(SourceId{1}, aram);
  const Layout layout{
      .version = Version::Winter,
      .sequenceHeaderAddress = 0x200,
      .pitchEnvelopeTableAddress = 0x300,
      .echo = EchoState{.left = 32, .right = 24, .feedback = -16, .delay = 2},
  };
  const SequenceParse parsed = decodeSequence(reader, layout, AssetId{1});
  expect(parsed.program.tracks.size() == 1, "ChunSnes should decode the active track");
  const TrackProgram& track = parsed.program.tracks.front();
  expect(track.commands.size() == 15 && track.commands[6].opcode == 0xfb && track.commands[10].opcode == 0x51,
         "top-level pattern end should preserve the following commands");

  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(parsed.program, sequenceDialect());
  expect(performance.diagnostics.empty(), "ChunSnes compiled playback should be source-free and diagnostic-free");
  expect(performance.tracks.size() == 1 && performance.tracks.front().endTick == 168,
         "ChunSnes note length and track end should follow the 48 PPQN driver timeline");

  bool envelope = false;
  bool negativePitchPeak = false;
  bool positivePitchPeak = false;
  bool reverb = false;
  bool silentFadeEndpoint = false;
  for (const PerformanceEvent& event : performance.tracks.front().events) {
    envelope |= std::holds_alternative<EnvelopePerformanceEvent>(event);
    if (const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event)) {
      negativePitchPeak |= bend->header.tick == 34 && bend->semitones == -0.09375;
      positivePitchPeak |= bend->header.tick == 40 && bend->semitones == 0.09375;
    }
    reverb |= std::holds_alternative<ReverbPerformanceEvent>(event);
    if (const auto* level = std::get_if<LevelPerformanceEvent>(&event)) {
      silentFadeEndpoint |= level->header.tick == 168 && level->linearGain == 7.0 / 256.0;
    }
  }
  const auto slide = std::ranges::find_if(performance.tracks.front().automations, [](const auto& automation) {
    return std::holds_alternative<PitchTransitionIntent>(automation.intent);
  });
  expect(envelope, "dynamic ADSR and release commands should emit active-voice envelope changes");
  expect(negativePitchPeak && positivePitchPeak, "looping pitch scripts should reach their signed pitch peaks");
  expect(reverb, "echo commands should retain the DSP echo parameters");
  expect(silentFadeEndpoint, "two-stage volume fades should end at the driver's near-silent mixer level");
  expect(slide != performance.tracks.front().automations.end(), "pre-note slides should bind to the following note");
  const auto& transition = std::get<PitchTransitionIntent>(slide->intent);
  expect(transition.startKey == 24.0 && transition.targetKey == 25.0 && transition.timing.timelineTicks == 6,
         "pitch slides should retain their direction, distance, and duration");

  const MidiSequence midi = renderMidiSequence(performance);
  const bool upwardSlide = std::ranges::any_of(midi.tracks.front().events, [](const MidiEvent& event) {
    const auto* bend = std::get_if<PitchBend>(&event);
    return bend != nullptr && bend->tick <= 6 && bend->value > 1024;
  });
  expect(upwardSlide, "upward slides should lower to positive pitch bends");

  std::vector<u8> synchronizedAram(kAramSize);
  synchronizedAram[0x400] = 120;
  synchronizedAram[0x401] = 3;
  synchronizedAram[0x402] = 0x10;
  synchronizedAram[0x404] = 0x20;
  synchronizedAram[0x406] = 0x30;
  const std::vector<u8> leader{0xb5, 0x51, 0x60, 0xff};
  const std::vector<u8> follower{0xf2, 0x01, 0xff};
  std::ranges::copy(leader, synchronizedAram.begin() + 0x410);
  std::ranges::copy(follower, synchronizedAram.begin() + 0x420);
  std::ranges::copy(follower, synchronizedAram.begin() + 0x430);

  const SequenceParse synchronized = decodeSequence(ByteReader(SourceId{2}, synchronizedAram),
                                                    Layout{
                                                        .version = Version::Winter,
                                                        .sequenceHeaderAddress = 0x400,
                                                    },
                                                    AssetId{2});
  const PerformanceSequence synchronizedPerformance =
      SequenceVm(LoopPolicy::PlayOnce).render(synchronized.program, sequenceDialect());
  expect(synchronizedPerformance.tracks.size() == 3, "duration-copy regression should render all three tracks");
  for (const PerformanceTrack& synchronizedTrack : synchronizedPerformance.tracks) {
    const auto note = std::ranges::find_if(synchronizedTrack.events, [](const PerformanceEvent& event) {
      return std::holds_alternative<NotePerformanceEvent>(event);
    });
    expect(note != synchronizedTrack.events.end() && std::get<NotePerformanceEvent>(*note).durationTicks == 96,
           "chained duration-copy tracks should use the preceding track's state at the same tick");
  }
}
