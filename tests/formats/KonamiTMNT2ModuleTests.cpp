/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiTMNT2/KonamiTMNT2.h"

#include "value/extractors/MameRomSetExtractor.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/SampleDecoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::konami_tmnt2;
namespace mame = vgmtrans::formats::mame;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void write(std::vector<u8>& bytes, u32 offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + offset);
}

void writeLe16(std::vector<u8>& bytes, u32 offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

template <class T>
const T* firstAsset(const ScanResult& result) {
  for (const auto& asset : result.assets) {
    if (const auto* typed = std::get_if<T>(&asset)) {
      return typed;
    }
  }
  return nullptr;
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
  const auto release = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* envelope = std::get_if<EnvelopePerformanceEvent>(&event);
    return envelope && envelope->update.fields == EnvelopeFields::Release;
  });
  expect(release != performance.tracks[0].events.end(), "TMNT2 DC should emit a release update");
  const auto& releaseValues = std::get<EnvelopePerformanceEvent>(*release).update.values;
  expect(releaseValues && releaseValues->releaseSeconds && std::isinf(*releaseValues->releaseSeconds),
         "TMNT2 DC operand 15 should wrap to native YM2151 release rate zero");
  expect(std::ranges::any_of(performance.tracks[0].events,
                             [](const PerformanceEvent& event) {
                               const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
                               return modulation && modulation->target == ModulationPerformanceTarget::VibratoDepth &&
                                      modulation->pitchDepthSemitones && *modulation->pitchDepthSemitones > 0.0;
                             }),
         "E5 should preserve the driver's software vibrato as physical modulation");
}

void konamiTmnt2SteppedModulationRunsAndRestoresOverTime() {
  std::vector<u8> bytes(0xa0);
  write(bytes, 0x80,
        {
            0xe0,
            0x01,
            0x00,
            0x00,
            0x00,  // initialize
            0xed,
            0x20,  // persistent +1/2-semitone bend
            0xee,
            0x04,
            0x01,
            0x21,  // four ticks; +pitch, -volume every two ticks
            0x11,
            0xff,
        });
  const auto performance =
      SequenceVm(LoopPolicy::PlayOnce).render(decode(std::move(bytes), Version::Tmnt2, TrackChip::Ym2151));
  const auto renderedNotes = notes(performance);
  expect(renderedNotes.size() == 1 && renderedNotes.front()->header.tick == 4,
         "EE should block sequence execution for its full 8-bit step count");
  std::vector<const PitchBendPerformanceEvent*> bends;
  std::vector<const ExpressionPerformanceEvent*> expressions;
  for (const auto& event : performance.tracks.front().events) {
    if (const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event)) {
      bends.push_back(bend);
    } else if (const auto* expression = std::get_if<ExpressionPerformanceEvent>(&event)) {
      expressions.push_back(expression);
    }
  }
  expect(bends.size() == 5 && bends[0]->header.tick == 0 && std::abs(bends[0]->semitones - 0.5) < 0.0001 &&
             bends[0]->layer != kPrimaryPitchBendLayer && bends[1]->header.tick == 1 &&
             bends[1]->layer != bends[0]->layer && std::abs(bends[1]->semitones - 1.0 / 64.0) < 0.0001 &&
             bends[3]->header.tick == 3 && bends.back()->header.tick == 4 &&
             std::abs(bends.back()->semitones) < 0.0001 &&
             std::ranges::none_of(performance.tracks.front().events, [](const PerformanceEvent& event) {
               return std::holds_alternative<PitchBendRangePerformanceEvent>(event);
             }),
         "EE should remain independent of the persistent bend and reset only its own pitch contribution");
  expect(expressions.size() >= 2 && expressions[expressions.size() - 2]->header.tick == 2 &&
             expressions.back()->header.tick == 4,
         "EE should apply its packed volume interval and restore the pre-command level");
}

void konamiTmnt2NativeLfoUsesHardwareRateAndChannelRamp() {
  std::vector<u8> bytes(0xa0);
  write(bytes, 0x80,
        {
            0xe0,
            0x01,
            0x00,
            0x00,
            0x00,  // initialize
            0xe9,
            0xff,
            0x7f,
            0x7f,
            0x12,
            0x00,  // maximum PM/AM; two-tick ramp; zero delay clamps to one
            0x18,
            0xff,  // 24-tick note
        });
  const auto performance =
      SequenceVm(LoopPolicy::PlayOnce).render(decode(std::move(bytes), Version::BellsWhistles, TrackChip::Ym2151));
  const auto renderedNotes = notes(performance);
  expect(renderedNotes.size() == 1 && !renderedNotes.front()->restartsVibratoLfoPhase.value_or(true) &&
             !renderedNotes.front()->restartsTremoloLfoPhase.value_or(true),
         "the native YM2151 LFO should remain free-running across note attacks");

  std::vector<const ModulationPerformanceEvent*> pitchDepths;
  std::vector<const ModulationPerformanceEvent*> amplitudeDepths;
  const ModulationPerformanceEvent* rate = nullptr;
  for (const auto& event : performance.tracks.front().events) {
    const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
    if (modulation == nullptr) {
      continue;
    }
    if (modulation->target == ModulationPerformanceTarget::VibratoDepth && modulation->pitchDepthSemitones &&
        *modulation->pitchDepthSemitones > 0.0) {
      pitchDepths.push_back(modulation);
    } else if (modulation->target == ModulationPerformanceTarget::TremoloDepth && modulation->volumeDepthDecibels &&
               *modulation->volumeDepthDecibels > 0.0) {
      amplitudeDepths.push_back(modulation);
    } else if (modulation->target == ModulationPerformanceTarget::VibratoRate) {
      rate = modulation;
    }
  }
  expect(rate && rate->context.frequencyHz && *rate->context.frequencyHz > 52.0 && *rate->context.frequencyHz < 54.0,
         "E9 should convert the YM2151 register-18 4.4 rate to its physical frequency");
  expect(pitchDepths.size() == 7 && pitchDepths.front()->header.tick == 3 && pitchDepths.back()->header.tick == 15 &&
             std::abs(*pitchDepths.back()->pitchDepthSemitones - 7.0) < 0.0001,
         "E9 should honor the per-note delay and seven-step PMS ramp");
  expect(amplitudeDepths.size() == 6 && amplitudeDepths.front()->header.tick == 5 &&
             amplitudeDepths.back()->header.tick == 15 &&
             std::abs(*amplitudeDepths.back()->volumeDepthDecibels - 95.6) < 0.0001,
         "E9 should ramp AMS at half the PMS sensitivity");

  std::vector<u8> sunset(0xa0);
  write(sunset, 0x80,
        {
            0xe0,
            0x01,
            0x00,
            0x00,
            0x00,  // initialize
            0xe5,
            0x01,
            0x00,  // Sunset Riders promotes depth zero to one
            0x11,
            0xff,
        });
  const auto sunsetPerformance =
      SequenceVm(LoopPolicy::PlayOnce).render(decode(std::move(sunset), Version::SunsetRiders, TrackChip::Ym2151));
  expect(std::ranges::any_of(sunsetPerformance.tracks.front().events,
                             [](const PerformanceEvent& event) {
                               const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
                               return modulation && modulation->target == ModulationPerformanceTarget::VibratoDepth &&
                                      modulation->pitchDepthSemitones &&
                                      std::abs(*modulation->pitchDepthSemitones - 0.125) < 0.0001;
                             }),
         "Sunset Riders E5 should promote a zero depth nibble to the driver's minimum depth");
}

void konamiTmnt2AliasesMiscSamplesAndTrackLabels() {
  constexpr u32 codeSize = 0x800;
  constexpr u32 soundSize = 0x100;
  std::vector<u8> bytes(codeSize + soundSize);

  write(bytes, 0x40, {0x21, 0x00, 0x02, 0xe6, 0x7f, 0x07, 0x5f, 0x19});
  write(bytes, 0x50, {0x13, 0x1a, 0x21, 0x00, 0x03, 0x07, 0x4f, 0x09, 0x4e});
  write(bytes, 0x60, {0x4f, 0x06, 0x00, 0xdd, 0x7e, 0x00, 0x07, 0x5f, 0x50, 0x21, 0x00, 0x04, 0x19});
  write(bytes, 0x80, {0x13, 0x1a, 0xd9, 0xcb, 0x7f, 0xca, 0x00, 0x00, 0x21, 0x00, 0x05, 0xe6, 0x7f});

  writeLe16(bytes, 0x200, 0x240);
  writeLe16(bytes, 0x202, 0x260);
  writeLe16(bytes, 0x204, 0x240);  // alias: valid in every non-Vendetta table
  bytes[0x240] = 1;
  writeLe16(bytes, 0x241, 0x600);
  bytes[0x260] = 1;
  writeLe16(bytes, 0x261, 0x610);
  write(bytes, 0x600, {0xe0, 0x01, 0x00, 0x00, 0x00, 0x11, 0xff});
  write(bytes, 0x610, {0xe0, 0x01, 0x00, 0x00, 0x00, 0x21, 0xff});

  writeLe16(bytes, 0x300, 0x350);
  writeLe16(bytes, 0x302, 0x360);
  write(bytes, 0x350, {0x08, 0x10, 0x00, 0x20, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x00});
  write(bytes, 0x360, {0x09, 0x10, 0x00, 0x20, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x00});
  for (u32 index = 0; index < 0x10; ++index) {
    bytes[codeSize + 0x20 + index] = static_cast<u8>(index);
  }

  const SourceFile source{
      .id = SourceId{502},
      .name = "tmnt2 fixture",
      .title = "tmnt2 fixture",
      .size = bytes.size(),
      .attributes =
          {
              {std::string(mame::kMameGameAttribute), "tmnt2"},
              {std::string(mame::kMameFormatAttribute), std::string(kFormatName)},
          },
      .segments =
          {
              SourceSegment{.name = "soundcpu", .offset = 0, .size = codeSize},
              SourceSegment{.name = "sound", .offset = codeSize, .size = soundSize},
          },
  };
  const ByteReader reader(source.id, bytes);
  std::vector<Diagnostic> diagnostics;
  const auto layout = findLayout(source, reader, &diagnostics);
  expect(layout && diagnostics.empty() && layout->sequences.size() == 2 && layout->sequencePointers.size() == 3,
         "non-Vendetta duplicate sequence pointers should remain aliases, not terminate the table");
  expect(layout->sampleInfos.size() == 2 && layout->sampleInstruments.size() == 2 &&
             layout->sampleInstruments[0].sampleIndex != layout->sampleInstruments[1].sampleIndex,
         "looping and one-shot instruments over the same bytes must retain distinct sample identities");

  ScanIdAllocator ids;
  const ScanResult result = module().scan(ScanInput{.source = source, .reader = reader, .ids = ids});
  const auto* misc = firstAsset<MiscAsset>(result);
  const auto* bank = firstAsset<SoundBankAsset>(result);
  expect(result.diagnostics.empty() && result.explicitCollections.size() == 2 && misc != nullptr &&
             misc->metadata.name == "Sequence Table" && misc->metadata.range.offset == 0x200 &&
             misc->metadata.range.endOffset() == 0x273 && misc->payload.size() == 0x73,
         "the complete pointer/track-table span should be published once as a shared Sequence Table misc asset");
  expect(std::ranges::all_of(
             result.explicitCollections,
             [](const ExplicitCollection& collection) { return collection.members.miscAssets.size() == 1; }),
         "every aliased sequence collection should attach the shared Sequence Table misc asset");
  const auto* sequence = firstAsset<SequenceProgramAsset>(result);
  expect(sequence && sequence->program.tracks.size() == 1 && sequence->program.tracks[0].name == "FM Track 0",
         "sequence programs should preserve the driver's FM/Sampled track names");
  expect(bank && bank->localSamples.samples.size() == 2 && bank->localSamples.samples[0].reverse &&
             bank->localSamples.samples[0].encodedData.offset == codeSize + 0x20 &&
             !bank->localSamples.samples[0].loop.enabled && bank->localSamples.samples[1].loop.enabled,
         "reverse samples should slice from their stored low ROM address and retain independent loop modes");
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
  expect(std::abs(sampledReleaseSeconds(Version::Tmnt2, 0xfe, 0x30, 100.0) - 0.45) < 0.0001,
         "sample release should apply its first attenuation step immediately");
  expect(std::abs(sampledReleaseSeconds(Version::Tmnt2, 0x01, 0x05, 100.0) - 2.56) < 0.0001,
         "a zero release-delay nibble should wrap through the 8-bit 256-tick countdown");
  expect(std::isinf(sampledReleaseSeconds(Version::Tmnt2, 0x10, 0x7f, 100.0)),
         "a zero attenuation rate should remain an infinite release");

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
