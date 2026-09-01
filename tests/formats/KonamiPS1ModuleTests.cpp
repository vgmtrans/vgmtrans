/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiPS1/KonamiPS1.h"
#include "value/formats/SonyPS1/SonyPS1.h"

#include "value/export/CollectionBinding.h"
#include "value/session/Session.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::konami_ps1;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void le16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void le32(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
  bytes[offset + 2] = static_cast<u8>(value >> 16);
  bytes[offset + 3] = static_cast<u8>(value >> 24);
}

std::vector<u8> kdt1(std::initializer_list<std::vector<u8>> tracks) {
  u32 length = 0x10 + static_cast<u32>(tracks.size()) * 2;
  for (const auto& track : tracks) {
    length += static_cast<u32>(track.size());
  }
  std::vector<u8> bytes(length, 0);
  bytes[0] = 'K';
  bytes[1] = 'D';
  bytes[2] = 'T';
  bytes[3] = '1';
  le32(bytes, 4, length);
  le32(bytes, 8, 480);
  le32(bytes, 12, static_cast<u32>(tracks.size()));
  u32 offset = 0x10 + static_cast<u32>(tracks.size()) * 2;
  u32 index = 0;
  for (const auto& track : tracks) {
    le16(bytes, 0x10 + index++ * 2, static_cast<u16>(track.size()));
    std::ranges::copy(track, bytes.begin() + offset);
    offset += static_cast<u32>(track.size());
  }
  return bytes;
}

std::vector<u8> kdt2(std::vector<u8> track) {
  std::vector<u8> bytes(0x50 + track.size(), 0);
  bytes[0] = 'K';
  bytes[1] = 'D';
  bytes[2] = 'T';
  bytes[3] = '2';
  le32(bytes, 4, static_cast<u32>(bytes.size()));
  le32(bytes, 8, 480);
  le32(bytes, 12, 1);
  le16(bytes, 0x10, static_cast<u16>(track.size()));
  std::ranges::copy(track, bytes.begin() + 0x50);
  return bytes;
}

template <class Event>
std::vector<const Event*> eventsOfType(const PerformanceTrack& track) {
  std::vector<const Event*> result;
  for (const auto& value : track.events) {
    if (const auto* event = std::get_if<Event>(&value)) {
      result.push_back(event);
    }
  }
  return result;
}

void layoutsUseAuditedSizesAndBothTableGenerations() {
  const std::vector<u8> track{0, 0xc7, 0xa2, 0xc9, 3, 0, 0xff, 0xff};
  const auto packed = kdt1({track});
  const auto packedLayout = readKonamiPs1SequenceLayout(ByteReader(SourceId{201}, packed), 0);
  expect(packedLayout && packedLayout->length == packed.size() && packedLayout->tracks.front().events.size() == 3,
         "KDT1's inclusive size and carry-chained event should parse at the exact source boundary");
  expect(packedLayout->tracks.front().events[1].delta == 0 &&
             packedLayout->tracks.front().events[1].kind == EventKind::Program,
         "the data-byte carry bit should suppress only the following delta");

  const auto fixed = kdt2(track);
  const auto fixedLayout = readKonamiPs1SequenceLayout(ByteReader(SourceId{202}, fixed), 0);
  expect(fixedLayout && fixedLayout->version == 2 && fixedLayout->tracks.front().offset == 0x50,
         "the original KDT2 generation should use its 32-slot, 0x50-byte header");
  const auto fixedPerformance =
      SequenceVm(LoopPolicy::PlayOnce)
          .render(parseKonamiPs1Sequence(ByteReader(SourceId{202}, fixed), AssetId{202}, *fixedLayout));
  expect(fixedPerformance.initialTempoMicrosecondsPerQuarter == 500122 &&
             eventsOfType<TempoPerformanceEvent>(fixedPerformance.tracks.front()).front()->microsecondsPerQuarter ==
                 743039,
         "fixed-table KDT2 should use Azure Dreams' 0x1c00 root counter");

  const u32 wrappedSize = (static_cast<u32>(packed.size()) + 3) & ~3u;
  std::vector<u8> wrapped(0x10 + wrappedSize, 0);
  wrapped[0] = 'K';
  wrapped[1] = 'D';
  wrapped[2] = 'T';
  wrapped[3] = '2';
  le32(wrapped, 4, wrappedSize);
  le32(wrapped, 8, 7);
  std::ranges::copy(packed, wrapped.begin() + 0x10);
  const auto found = findKonamiPs1Sequences(ByteReader(SourceId{203}, wrapped));
  expect(found.size() == 1 && found.front().hasKdt2Header && found.front().sequenceId == 7 &&
             found.front().containerLength == wrapped.size(),
         "the later KDT2 wrapper should contribute identity without being mistaken for an old fixed-table sequence");
}

void sequenceModelsDriverLfosAdsrReverbAndTempo() {
  const auto bytes = kdt1({{
      0,  0xc7, 38,                 // Enemy Attack's opening tempo
      0,  0x94, 5,                  // vibrato delay
      0,  0x95, 32,                 // vibrato rate
      0,  0x96, 16,                 // vibrato depth
      0,  0x97, 4,                  // vibrato depth ramp
      0,  0x99, 2,                  // tremolo delay
      0,  0x9a, 16,                 // tremolo rate
      0,  0x9b, 32,                 // tremolo depth
      0,  0x9c, 3,                  // tremolo depth ramp
      0,  0xe2, 4,                  // NRPN parameter: linear attack
      0,  0xe3, 0,                  // tone zero
      0,  0x86, 100,                // data entry
      0,  0xe2, 15,   0, 0x86, 5,   // global reverb mode
      0,  0xe2, 16,                 // global reverb depth
      0,  0xe3, 16,                 // global target
      0,  0x86, 64,                 // half-scale SPU depth
      0,  0xe2, 17,   0, 0x86, 32,  // reverb feedback
      0,  0xe2, 18,   0, 0x86, 12,  // reverb delay
      0,  60,   100,                // note on
      20, 0xca,                     // note off current key
      0,  0xff, 0xff,
  }});
  const ByteReader reader(SourceId{204}, bytes);
  const auto layout = readKonamiPs1SequenceLayout(reader, 0);
  expect(layout.has_value(), "audited KonamiPS1 feature fixture should parse");
  const u16 initialAdsr1 = composePsxAdsr1(1, 0x40, 8, 8);
  const u16 initialAdsr2 = composePsxAdsr2(1, 1, 0x30, 1, 0x10);
  Tone tone{
      .keyLow = 0,
      .keyHigh = 127,
      .flags = 4,
      .bendDown = 4,
      .bendUp = 12,
      .adsr1 = initialAdsr1,
      .adsr2 = initialAdsr2,
      .originalAdsr1 = initialAdsr1,
      .originalAdsr2 = initialAdsr2,
  };
  const PerformanceSequence performance =
      SequenceVm(LoopPolicy::PlayOnce).render(parseKonamiPs1Sequence(reader, AssetId{204}, *layout, {tone}));
  expect(performance.diagnostics.empty(), "audited KonamiPS1 feature fixture should render cleanly");
  const auto tempos = eventsOfType<TempoPerformanceEvent>(performance.tracks.front());
  expect(performance.initialTempoMicrosecondsPerQuarter == 511286 && tempos.size() == 1 &&
             tempos.front()->microsecondsPerQuarter == 681714,
         "KDT1 tempo should use Suikoden II's root counter and sequence accumulator");

  const auto modulation = eventsOfType<ModulationPerformanceEvent>(performance.tracks.front());
  const auto vibratoRate = std::ranges::find_if(
      modulation, [](const auto* event) { return event->target == ModulationPerformanceTarget::VibratoRate; });
  const auto tremoloRate = std::ranges::find_if(
      modulation, [](const auto* event) { return event->target == ModulationPerformanceTarget::TremoloRate; });
  expect(vibratoRate != modulation.end() && (*vibratoRate)->context.frequencyHz &&
             std::abs(*(*vibratoRate)->context.frequencyHz - 6.5651052) < 0.000001 &&
             (*vibratoRate)->context.delayMilliseconds &&
             std::abs(*(*vibratoRate)->context.delayMilliseconds - 190.4006522) < 0.001,
         "vibrato should retain the timer-divided phase accumulator and doubled delay counter");
  expect(tremoloRate != modulation.end() && (*tremoloRate)->context.frequencyHz &&
             std::abs(*(*tremoloRate)->context.frequencyHz - 3.2825526) < 0.000001,
         "tremolo should retain its independent timer-divided triangle accumulator");
  expect(std::ranges::count_if(performance.tracks.front().automations,
                               [](const PerformanceAutomation& automation) {
                                 const auto* intent =
                                     std::get_if<ScalarPerformanceAutomationIntent>(&automation.intent);
                                 return intent &&
                                        (intent->target == PerformanceAutomationTarget::VibratoDepth ||
                                         intent->target == PerformanceAutomationTarget::TremoloDepth) &&
                                        intent->restartsOnNote;
                               }) == 2,
         "both driver depth ramps should be represented as note-restarting automation");

  const auto envelopes = eventsOfType<EnvelopePerformanceEvent>(performance.tracks.front());
  const u16 changedAdsr1 = static_cast<u16>((initialAdsr1 & 0x00ff) | (27u << 8));
  expect(envelopes.size() == 1 && envelopes.front()->scope == VoiceEnvelopeScope::FutureAttacks &&
             envelopes.front()->update.values == psxSpuEnvelope(changedAdsr1, initialAdsr2),
         "NRPN 4 should mutate the selected VAB tone and apply it only to later attacks");
  const auto reverbs = eventsOfType<ReverbPerformanceEvent>(performance.tracks.front());
  const ReverbPerformanceEvent* globalReverb = nullptr;
  for (const auto* event : reverbs) {
    if (event->voiceMask) {
      globalReverb = event;
    }
  }
  expect(globalReverb != nullptr && std::abs(globalReverb->send - 0.5) < 0.000001 && globalReverb->leftGain == 0.5 &&
             globalReverb->rightGain == 0.5 && globalReverb->feedback == 0.25 &&
             globalReverb->delayMilliseconds == 12.0 && globalReverb->filterIndex == 5,
         "global SPU reverb mode, depth, feedback, and delay should survive as structured performance data");
  expect(!reverbs.empty() && !reverbs.back()->voiceMask && std::abs(reverbs.back()->send - 0.5) < 0.000001,
         "tone-default reverb routing should use the current global SPU depth for its later attack");
}

void cc119RestoresTheBankAdsr() {
  const auto bytes = kdt1({{
      0, 0xe2, 4,    0, 0xe3, 0, 0, 0x86, 100,  // mutate tone-zero attack
      0, 60,   100,  1, 0xca,                   // first attack uses the mutation
      0, 0xf7, 0,                               // CC119 restores the bank tone table
      0, 60,   100,  1, 0xca,                   // second attack inherits the VAB ADSR
      0, 0xff, 0xff,
  }});
  const ByteReader reader(SourceId{207}, bytes);
  const auto layout = readKonamiPs1SequenceLayout(reader, 0);
  expect(layout.has_value(), "bank ADSR reset fixture should parse");
  const u16 adsr1 = composePsxAdsr1(1, 0x40, 8, 8);
  const u16 adsr2 = composePsxAdsr2(1, 1, 0x30, 1, 0x10);
  const auto performance =
      SequenceVm(LoopPolicy::PlayOnce)
          .render(parseKonamiPs1Sequence(
              reader, AssetId{207}, *layout,
              {Tone{.adsr1 = adsr1, .adsr2 = adsr2, .originalAdsr1 = adsr1, .originalAdsr2 = adsr2}}));
  const auto envelopes = eventsOfType<EnvelopePerformanceEvent>(performance.tracks.front());
  expect(envelopes.size() == 3 && envelopes.front()->update.values && !envelopes[1]->update.values &&
             !envelopes[2]->update.values,
         "CC119 should clear both the live tone mutation and the future-attack envelope override");
}

void sequenceModelsIndependentChannelAndRandomPitch() {
  const auto bytes = kdt1({{
      0, 0x83, 0,   // channel pitch mode: CC1 depth, CC2 period
      0, 0x82, 16,  // 16 driver ticks per cycle before integer quantization
      0, 0x81, 32,  // half-semitone channel-wide triangle
      0, 0x82, 8,   // denominator is latched on the next CC1
      0, 0x83, 64,  // alternate mode: CC2 changes depth, not the latched rate
      0, 0x82, 24,  // three-eighth-semitone depth
      0, 0x8d, 64,  // random update accumulator rate: 30 Hz
      0, 0x8e, 8,   // random depth: two semitones
      0, 60,   100, 0x81, 0x70, 0xca, 0, 0xff, 0xff,
  }});
  const ByteReader reader(SourceId{206}, bytes);
  const auto layout = readKonamiPs1SequenceLayout(reader, 0);
  expect(layout.has_value(), "audited layered-pitch fixture should parse");
  const auto performance =
      SequenceVm(LoopPolicy::PlayOnce).render(parseKonamiPs1Sequence(reader, AssetId{206}, *layout));
  const auto modulation = eventsOfType<ModulationPerformanceEvent>(performance.tracks.front());
  const auto find = [&](ModulationPerformanceTarget target, PitchBendLayerId layer) {
    return std::ranges::find_if(
        modulation, [=](const auto* event) { return event->target == target && event->pitchLayer == layer; });
  };
  const auto channelRate = find(ModulationPerformanceTarget::VibratoRate, PitchBendLayerId{1});
  const auto channelDepth = find(ModulationPerformanceTarget::VibratoDepth, PitchBendLayerId{1});
  const auto randomRate = find(ModulationPerformanceTarget::VibratoRate, PitchBendLayerId{2});
  const auto randomDepth = find(ModulationPerformanceTarget::VibratoDepth, PitchBendLayerId{2});
  expect(channelRate != modulation.end() && (*channelRate)->context.frequencyHz &&
             std::abs(*(*channelRate)->context.frequencyHz - 3.2825526) < 0.000001 && (*channelRate)->context.shape &&
             (*channelRate)->context.shape->waveform == LfoWaveform::Triangle &&
             !(*channelRate)->context.restartsOnNote,
         "CC1-3 should retain the channel-wide integer-stepped triangle without restarting it at note-on");
  expect(channelDepth != modulation.end() && (*channelDepth)->pitchDepthSemitones == 0.5,
         "the channel pitch accumulator should use 1/64-semitone depth units");
  expect(std::ranges::count_if(modulation,
                               [](const auto* event) {
                                 return event->target == ModulationPerformanceTarget::VibratoRate &&
                                        event->pitchLayer == PitchBendLayerId{1};
                               }) == 1 &&
             std::ranges::any_of(modulation,
                                 [](const auto* event) {
                                   return event->target == ModulationPerformanceTarget::VibratoDepth &&
                                          event->pitchLayer == PitchBendLayerId{1} &&
                                          event->pitchDepthSemitones == 0.375;
                                 }),
         "CC2/CC3 should preserve the driver's asymmetric rate latch while alternate-mode CC2 updates depth");
  expect(randomRate != modulation.end() && (*randomRate)->context.frequencyHz &&
             std::abs(*(*randomRate)->context.frequencyHz - 26.2604208) < 0.000001 && (*randomRate)->context.shape &&
             (*randomRate)->context.shape->waveform == LfoWaveform::Noise && (*randomRate)->context.restartsOnNote,
         "CC13 should become the later driver's note-restarted sample-and-hold update rate");
  expect(randomDepth != modulation.end() && (*randomDepth)->pitchDepthSemitones == 2.0,
         "CC14 should retain the driver's signed-byte depth scale in semitones");
}

void loopUsesCc99AndDataEntryCount() {
  const auto bytes = kdt1({{
      0,
      0xe3,
      20,  // save loop state
      0,
      0x86,
      2,  // two jumps, three total plays
      0,
      60,
      100,
      1,
      0xca,
      0,
      0xe3,
      30,  // restore loop state
      0,
      0xff,
      0xff,
  }});
  const ByteReader reader(SourceId{205}, bytes);
  const auto layout = readKonamiPs1SequenceLayout(reader, 0);
  expect(layout && std::ranges::any_of(layout->tracks.front().events,
                                       [](const EventLayout& event) {
                                         return event.loopDestination.has_value() && event.loopCount == 2;
                                       }),
         "CC99 20 / CC6 / CC99 30 should recover the driver's loop frame");
  const auto performance =
      SequenceVm(LoopPolicy::PlayOnce).render(parseKonamiPs1Sequence(reader, AssetId{205}, *layout));
  expect(eventsOfType<NotePerformanceEvent>(performance.tracks.front()).size() == 3,
         "a loop count of two should jump twice and play its body three times");
}

void channelResetHardStopsSustainedVoices() {
  const auto bytes = kdt1({{
      0,
      60,
      100,  // note on
      1,
      0xc0,
      127,  // sustain on
      1,
      0xca,  // release current note into sustain
      1,
      0xfb,
      0,  // CC123 invokes the driver's hard group reset
      5,
      0xff,
      0xff,
  }});
  const ByteReader reader(SourceId{208}, bytes);
  const auto layout = readKonamiPs1SequenceLayout(reader, 0);
  expect(layout.has_value(), "channel-reset fixture should parse");
  const auto performance =
      SequenceVm(LoopPolicy::PlayOnce).render(parseKonamiPs1Sequence(reader, AssetId{208}, *layout));
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks.front());
  expect(notes.size() == 1 && notes.front()->durationTicks == 3,
         "CC120, CC121, and CC123 should hard-stop voices even when sustain has retained their release");
}

std::vector<u8> sourceWithVab() {
  auto bytes = kdt1({{0, 0xc9, 0, 0, 60, 100, 1, 0xca, 0, 0xff, 0xff}});
  constexpr u32 bankOffset = 0x100;
  constexpr u32 headerSize = 0xc20;
  bytes.resize(bankOffset + headerSize + 0x10, 0);
  bytes[bankOffset] = 'p';
  bytes[bankOffset + 1] = 'B';
  bytes[bankOffset + 2] = 'A';
  bytes[bankOffset + 3] = 'V';
  le32(bytes, bankOffset + 4, 5);
  le32(bytes, bankOffset + 0x0c, headerSize + 0x10);
  le16(bytes, bankOffset + 0x12, 1);
  le16(bytes, bankOffset + 0x14, 1);
  le16(bytes, bankOffset + 0x16, 1);
  bytes[bankOffset + 0x18] = 127;
  bytes[bankOffset + 0x19] = 64;
  const u32 program = bankOffset + 0x20;
  bytes[program] = 1;
  bytes[program + 1] = 127;
  bytes[program + 4] = 64;
  const u32 tone = bankOffset + 0x820;
  bytes[tone + 2] = 127;
  bytes[tone + 3] = 64;
  bytes[tone + 4] = 60;
  bytes[tone + 6] = 0;
  bytes[tone + 7] = 127;
  bytes[tone + 0x0c] = 2;
  bytes[tone + 0x0d] = 2;
  le16(bytes, tone + 0x10, composePsxAdsr1(1, 0x40, 8, 8));
  le16(bytes, tone + 0x12, composePsxAdsr2(1, 1, 0x30, 1, 0x10));
  le16(bytes, tone + 0x16, 1);
  le16(bytes, bankOffset + 0xa22, 2);
  bytes[bankOffset + headerSize + 1] = 1;
  return bytes;
}

std::vector<u8> sourceWithRuntimeLinkedVab() {
  constexpr u32 bankOffset = 0x100;
  constexpr u32 headerSize = 0xc20;
  constexpr u32 recordOffset = 0x2000;
  constexpr u32 bodyOffset = 0x4000;
  constexpr u32 sampleSize = 0x60;
  auto bytes = sourceWithVab();
  bytes.resize(bodyOffset + sampleSize * 2, 0);
  std::fill(bytes.begin() + bankOffset + headerSize, bytes.begin() + bankOffset + headerSize + 0x10, 0);
  le32(bytes, bankOffset + 0x0c, headerSize + sampleSize * 2);
  le16(bytes, bankOffset + 0x16, 2);
  le16(bytes, bankOffset + 0xa22, sampleSize >> 3);
  le16(bytes, bankOffset + 0xa24, sampleSize >> 3);
  le32(bytes, recordOffset, 0x80000000 | bankOffset);
  le32(bytes, recordOffset + 0x0c, 0x80000000 | bodyOffset);
  for (u32 sample = 0; sample < 2; ++sample) {
    const u32 start = bodyOffset + sample * sampleSize;
    for (u32 frame = 1; frame < sampleSize / kPsxAdpcmBlockBytes; ++frame) {
      const u32 block = start + frame * kPsxAdpcmBlockBytes;
      bytes[block] = 0x11;
      bytes[block + 1] = frame + 1 == sampleSize / kPsxAdpcmBlockBytes ? 1 : 0;
      for (u32 byte = 2; byte < kPsxAdpcmBlockBytes; ++byte) {
        bytes[block + byte] = static_cast<u8>(sample * 71 + frame * 17 + byte);
      }
    }
  }
  return bytes;
}

void modulePairsKdtWithSonyVab() {
  Session session;
  session.registerFormat(konamiPs1Module());
  session.registerFormat(vgmtrans::formats::sony_ps1::sonyPs1Module());
  session.addSource(SourceFile{.name = "KonamiPS1 fixture.bin"}, sourceWithVab());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const auto collection = std::ranges::find_if(
      snapshot.collections(), [](const Collection& candidate) { return candidate.members.sequence.has_value(); });
  expect(collection != snapshot.collections().end() && collection->members.soundBanks.size() == 1,
         "Konami KDT collections should bind to Sony VAB banks from the same source");
  const auto* sequence = snapshot.asset<SequenceProgramAsset>(*collection->members.sequence);
  expect(sequence && sequence->metadata.format == kKonamiPs1FormatName,
         "the resolved sequence should remain owned by the KonamiPS1 module");
}

void moduleFollowsAuditedRuntimeVabLinks() {
  auto inlineBytes = sourceWithRuntimeLinkedVab();
  constexpr u32 inlineBody = 0x100 + 0xc20;
  std::ranges::copy(inlineBytes.begin() + 0x4000, inlineBytes.begin() + 0x40c0, inlineBytes.begin() + inlineBody);
  inlineBytes.resize(inlineBody + 0xc0);
  const auto inlineBanks = vgmtrans::formats::sony_ps1::findSonyPs1Banks(ByteReader(SourceId{209}, inlineBytes));
  expect(inlineBanks.size() == 1 && inlineBanks.front().hasSampleBody &&
             inlineBanks.front().sampleDataOffset == inlineBody,
         "declared VAB sample slices should validate directly even when short samples defeat footprint heuristics");

  Session session;
  session.registerFormat(konamiPs1Module());
  session.registerFormat(vgmtrans::formats::sony_ps1::sonyPs1Module());
  session.addSource(SourceFile{.name = "KonamiPS1 split VAB fixture.bin"}, sourceWithRuntimeLinkedVab());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const auto collection = std::ranges::find_if(
      snapshot.collections(), [](const Collection& candidate) { return candidate.members.sequence.has_value(); });
  expect(collection != snapshot.collections().end(), "a runtime-linked VAB fixture should resolve a KDT collection");
  const auto bound = bindCollection(snapshot, collection->id);
  expect(bound.collection && bound.collection->soundBanks().size() == 1 &&
             bound.collection->soundBanks().front().localSamples.samples.size() == 2,
         "the audited VAB header/+0x0c sample pointer pair should make short split samples directly playable");
}

}  // namespace

void runKonamiPs1ModuleTests() {
  layoutsUseAuditedSizesAndBothTableGenerations();
  sequenceModelsDriverLfosAdsrReverbAndTempo();
  sequenceModelsIndependentChannelAndRandomPitch();
  cc119RestoresTheBankAdsr();
  loopUsesCc99AndDataEntryCount();
  channelResetHardStopsSustainedVoices();
  modulePairsKdtWithSonyVab();
  moduleFollowsAuditedRuntimeVabLinks();
}
