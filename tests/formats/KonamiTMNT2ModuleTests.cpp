/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiTMNT2/KonamiTMNT2.h"

#include "value/sequence/SequenceVm.h"
#include "value/synth/SampleDecoder.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::konami_tmnt2;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void write(std::vector<u8>& bytes, u32 offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + offset);
}

SequenceProgram decode(std::vector<u8> bytes, Version version, TrackChip chip,
                       std::vector<SampleInstrument> instruments = {}) {
  constexpr u32 kTrack = 0x80;
  const SourceId source{501};
  Layout layout{
      .version = version,
      .game = "fixture",
      .program = SourceRange{.source = source, .offset = 0, .size = bytes.size()},
      .sound = SourceRange{.source = source, .offset = bytes.size(), .size = 0},
      .clkb = 0xf2,
      .sampleInstruments = std::move(instruments),
  };
  SequenceLayout sequence{
      .index = 0,
      .trackTable = SourceRange{.source = source, .offset = 0x20, .size = 2},
      .tracks = {{.number = 0,
                  .chip = chip,
                  .offset = kTrack,
                  .pointer = SourceRange{.source = source, .offset = 0x20, .size = 2}}},
      .name = "fixture",
  };
  std::vector<Diagnostic> diagnostics;
  auto program = decodeSequence(ByteReader(source, bytes), layout, sequence, AssetId{1}, nullptr, &diagnostics);
  expect(diagnostics.empty(), "audited KonamiTMNT2 fixture should decode without diagnostics");
  return program;
}

const SourceCommand* commandAt(const SequenceProgram& program, u32 address) {
  const auto& commands = program.tracks.front().commands;
  const auto found =
      std::ranges::find_if(commands, [=](const SourceCommand& command) { return command.address.value == address; });
  return found == commands.end() ? nullptr : &*found;
}

std::vector<const NotePerformanceEvent*> notes(const PerformanceSequence& performance) {
  std::vector<const NotePerformanceEvent*> result;
  for (const auto& event : performance.tracks.front().events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      result.push_back(note);
    }
  }
  return result;
}

}  // namespace

void konamiTmnt2ContextualFlowAndDynamicYmReleaseRender() {
  std::vector<u8> bytes(0xc0);
  write(bytes, 0x80,
        {
            0xe0,
            0x01,
            0x00,
            0x00,
            0x00,  // initialize
            0xfa,  // loop start
            0x11,  // note
            0xfa,
            0x02,  // loop end
            0xfc,
            0xa0,
            0x00,  // call
            0x21,  // note after return
            0xff,  // end
        });
  write(bytes, 0xa0,
        {
            0xe5,
            0x01,
            0x24,  // table vibrato
            0xdc,
            0x0f,  // TMNT2's one-byte operator release override
            0xe6,
            0xe7,
            0xe8,  // music-parser no-ops
            0xe9,
            0x00,  // disable native YM LFO
            0xfc,  // return
        });

  const SequenceProgram program = decode(std::move(bytes), Version::Tmnt2, TrackChip::Ym2151);
  expect(program.tracks.size() == 1 && program.tracks[0].commands.size() == 14,
         "TMNT2 call targets and loop bodies should be discovered exactly once");
  const auto* loopEnd = commandAt(program, 0x87);
  const auto* call = commandAt(program, 0x89);
  const auto* return_ = commandAt(program, 0xaa);
  expect(loopEnd && loopEnd->range.size == 2 && call && call->range.size == 3 && return_ && return_->range.size == 1 &&
             call->flow.defaultTransition.kind == CommandTransitionKind::Call &&
             return_->flow.defaultTransition.kind == CommandTransitionKind::Return,
         "FA and FC must retain their context-sensitive loop/call widths");

  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program);
  const auto renderedNotes = notes(performance);
  expect(performance.diagnostics.empty() && renderedNotes.size() == 3,
         "TMNT2 contextual flow should terminate after two loop plays and a returned note");
  expect(std::ranges::any_of(performance.tracks[0].events,
                             [](const PerformanceEvent& event) {
                               const auto* envelope = std::get_if<EnvelopePerformanceEvent>(&event);
                               return envelope && envelope->update.fields == EnvelopeFields::Release &&
                                      envelope->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks;
                             }),
         "DC should emit a sticky dynamic YM2151 release override");
  expect(std::ranges::any_of(performance.tracks[0].events,
                             [](const PerformanceEvent& event) {
                               const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
                               return modulation && modulation->target == ModulationPerformanceTarget::VibratoDepth &&
                                      modulation->pitchDepthSemitones && *modulation->pitchDepthSemitones > 0.0;
                             }),
         "E5 should preserve the driver's software vibrato as physical modulation");
}

void konamiTmnt2DialectWidthsMatchTheMusicParsers() {
  std::vector<u8> sunset(0xa0);
  write(sunset, 0x80,
        {
            0xe0, 0x01, 0x00, 0x00, 0x00, 0xdc, 0x12, 0x34,  // four operator release nibbles
            0xe5, 0x01, 0x23,                                // software vibrato
            0xe6, 0xfe,                                      // signed timer-skip adjustment
            0xe7, 0xe8,                                      // no-ops
            0xe9, 0x00,                                      // one-byte native-LFO disable payload
            0xef, 0x80, 0x01,                                // master attenuation pair
            0x11, 0xff,
        });
  const auto sunsetProgram = decode(std::move(sunset), Version::SunsetRiders, TrackChip::Ym2151);
  expect(commandAt(sunsetProgram, 0x85)->range.size == 3 && commandAt(sunsetProgram, 0x88)->range.size == 3 &&
             commandAt(sunsetProgram, 0x8b)->range.size == 2 && commandAt(sunsetProgram, 0x8d)->range.size == 1 &&
             commandAt(sunsetProgram, 0x8e)->range.size == 1 && commandAt(sunsetProgram, 0x8f)->range.size == 2 &&
             commandAt(sunsetProgram, 0x91)->range.size == 3,
         "Sunset Riders DC/E5/E6/E9/EF widths should match the normal music jump table");
  expect(SequenceVm(LoopPolicy::PlayOnce).render(sunsetProgram).diagnostics.empty(),
         "Sunset Riders' audited modulation and tempo commands should render");

  std::vector<u8> bells(0xa0);
  write(bells, 0x80,
        {
            0xe0,
            0x01,
            0x00,
            0x00,
            0x00,
            0xdc,
            0x12,
            0x34,
            0xe5,
            0x01,
            0x23,
            0xe6,
            0xe7,
            0xe8,  // all three are zero-operand no-ops here
            0xe9,
            0x00,
            0x11,
            0xff,
        });
  const auto bellsProgram = decode(std::move(bells), Version::BellsWhistles, TrackChip::Ym2151);
  expect(commandAt(bellsProgram, 0x8b)->range.size == 1 && commandAt(bellsProgram, 0x8c)->range.size == 1 &&
             commandAt(bellsProgram, 0x8d)->range.size == 1 && commandAt(bellsProgram, 0x8e)->range.size == 2,
         "Bells & Whistles E6-E8 must not consume the following command bytes");
}

void konamiTmnt2SampleReleaseTremoloAndAdpcmAreDistinct() {
  std::vector<u8> bytes(0xa0);
  write(bytes, 0x80,
        {
            0xe0,
            0x01,
            0x00,
            0x00,
            0x00,
            0xdc,
            0x21,  // two-tick release interval, one attenuation step
            0xe7,
            0x20,  // K053260 bytecode volume envelope selector
            0x11,
            0xff,
        });
  const auto program = decode(std::move(bytes), Version::Tmnt2, TrackChip::K053260, {SampleInstrument{.volume = 0x7f}});
  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(program);
  expect(performance.diagnostics.empty() &&
             std::ranges::any_of(performance.tracks[0].events,
                                 [](const PerformanceEvent& event) {
                                   const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
                                   return modulation &&
                                          modulation->target == ModulationPerformanceTarget::TremoloDepth &&
                                          modulation->volumeDepthLinearGain;
                                 }) &&
             std::ranges::any_of(performance.tracks[0].events,
                                 [](const PerformanceEvent& event) {
                                   const auto* envelope = std::get_if<EnvelopePerformanceEvent>(&event);
                                   return envelope && envelope->update.fields == EnvelopeFields::Release;
                                 }),
         "sample tracks should retain both K053260 volume envelopes and dynamic release");

  const std::array<u8, 2> encoded{0x08, 0x21};
  Sample sample{
      .codec = AudioCodec::KonamiK053260Adpcm,
      .encodedData = SourceRange{.source = SourceId{9}, .offset = 0, .size = encoded.size()},
      .sampleRate = static_cast<u32>(kSampleRate),
  };
  const auto forward = decodeSample(sample, encoded);
  expect(forward && forward->pcm == std::vector<s16>({-32768, -32768, -32512, -32000}),
         "K053260 PPCM must decode low nibbles first and treat nibble 8 as -32768");
  sample.reverse = true;
  const auto reverse = decodeSample(sample, encoded);
  expect(reverse && reverse->pcm == std::vector<s16>({256, 768, -32000, -32000}),
         "reversed K053260 PPCM should reverse bytes without reversing nibble order");
}
