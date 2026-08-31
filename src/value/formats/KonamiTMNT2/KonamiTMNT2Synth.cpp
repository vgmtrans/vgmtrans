/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiTMNT2/KonamiTMNT2.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace vgmtrans::formats::konami_tmnt2 {

using namespace core;

namespace {

[[nodiscard]] double pitchCents(u16 word) {
  const u16 period = word & 0x0fff;
  if (period >= 4096) {
    return 0.0;
  }
  return 1200.0 * std::log2(112.0 / (4096.0 - period));
}

[[nodiscard]] double panPosition(u8 raw, Version version) {
  static constexpr std::array<double, 8> positions{0.5, 0.0, 0.17, 0.30, 0.5, 0.70, 0.83, 1.0};
  return positions[k053260PanIndex(raw, version)];
}

[[nodiscard]] Ym2151Voice ymVoice(ByteReader reader, u32 offset) {
  Ym2151Voice voice;
  const u8 channel = reader.u8At(offset);
  voice.algorithm = channel & 7;
  voice.feedback = (channel >> 3) & 7;
  voice.leftEnabled = (channel & 0x40) != 0;
  voice.rightEnabled = (channel & 0x80) != 0;
  if (!voice.leftEnabled && !voice.rightEnabled) {
    voice.leftEnabled = true;
    voice.rightEnabled = true;
  }

  // Patch operators are stored M1, C1, M2, C2; the shared model uses
  // native YM2151 register-slot order M1, M2, C1, C2.
  constexpr std::array<u8, 4> nativeSlot{0, 2, 1, 3};
  for (u32 source = 0; source < 4; ++source) {
    const u32 data = offset + 1 + source * 6;
    auto& op = voice.operators[nativeSlot[source]];
    const u8 dtMul = reader.u8At(data);
    const u8 attack = reader.u8At(data + 2);
    const u8 firstDecay = reader.u8At(data + 3);
    const u8 secondDecay = reader.u8At(data + 4);
    const u8 sustainRelease = reader.u8At(data + 5);
    op.detune1 = (dtMul >> 4) & 7;
    op.multiplier = dtMul & 0x0f;
    op.totalLevel = reader.u8At(data + 1) & 0x7f;
    op.keyScale = attack >> 6;
    op.attackRate = attack & 0x1f;
    op.amplitudeModulationEnabled = (firstDecay & 0x80) != 0;
    op.firstDecayRate = firstDecay & 0x1f;
    op.detune2 = secondDecay >> 6;
    op.secondDecayRate = secondDecay & 0x1f;
    op.sustainLevel = sustainRelease >> 4;
    op.releaseRate = sustainRelease & 0x0f;
  }
  return voice;
}

}  // namespace

std::vector<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout) {
  std::vector<ScanSoundBankDraft> result;
  const ByteReader reader = builder.reader();

  if (!layout.ym2151Patches.empty()) {
    auto bank = builder.soundBank(layout.game + " YM2151 Instruments");
    auto& instruments = bank.instruments();
    instruments.include(reader.range(layout.ym2151TableOffset, layout.ym2151Patches.size() * 2));
    instruments.source(SourceRole::Table, "YM2151 Patch Pointer Table",
                       reader.range(layout.ym2151TableOffset, layout.ym2151Patches.size() * 2),
                       "konami-tmnt2-ym2151-table");
    for (u32 index = 0; index < layout.ym2151Patches.size(); ++index) {
      const u32 offset = layout.ym2151Patches[index];
      if (!reader.has(offset, 25)) {
        instruments.warning("KonamiTMNT2 YM2151 patch is truncated", reader.range(offset, 0));
        continue;
      }
      const SourceRange range = reader.range(offset, 25);
      const std::string name = fmt::format("YM2151 Instrument {}", index);
      instruments
          .add(index,
               Instrument{
                   .explicitAddress = InstrumentAddress{.bank = index >> 7, .program = index & 0x7f},
                   .identity = InstrumentIdentity{.domain = std::string(kYm2151Domain), .key = index},
                   .reverb = 0.0,
                   .name = name,
                   .range = range,
                   .synthVoice = Instrument::SynthVoice{ymVoice(reader, offset)},
               })
          .source(name, range, "konami-tmnt2-ym2151-patch");
    }
    result.push_back(bank);
  }

  if (layout.sampleInfos.empty() && layout.sampleInstruments.empty() && layout.drumBanks.empty()) {
    return result;
  }
  auto bank = builder.soundBank(layout.game + " K053260 Instruments");
  auto& instruments = bank.instruments();
  auto& samples = bank.localSamples();
  const double ticksPerSecond = driverTickRate(layout.clkb, layout.defaultTickSkipInterval);

  for (u32 index = 0; index < layout.sampleInfos.size(); ++index) {
    const auto& info = layout.sampleInfos[index];
    if (!info.fitsIn(layout.sound.size)) {
      samples.warning("KonamiTMNT2 sample range is outside the sound ROM", info.range);
      continue;
    }
    const u32 frameScale = info.adpcm ? 2 : 1;
    const u32 loopStart = std::min(info.loopStart, info.length) * frameScale;
    const u32 frames = info.length * frameScale;
    const std::string name = fmt::format("K053260 Sample {}", index);
    samples
        .add(index,
             Sample{
                 .name = name,
                 .codec = info.adpcm ? AudioCodec::KonamiK053260Adpcm : AudioCodec::PcmS8,
                 .encodedData = reader.range(layout.sound.offset + info.start, info.length),
                 .sampleRate = static_cast<u32>(std::lround(kSampleRate)),
                 .bitsPerSample = static_cast<u16>(info.adpcm ? 16 : 8),
                 .reverse = info.reverse,
                 .loop = Loop{.enabled = info.loops, .start = loopStart, .length = frames - loopStart},
             })
        .source(name + " Info", info.range, "konami-tmnt2-sample-info");
  }

  if (!layout.sampleInstruments.empty()) {
    instruments.include(reader.range(layout.k053260TableOffset, layout.sampleInstruments.size() * 2));
    instruments.source(SourceRole::Table, "K053260 Instrument Pointer Table",
                       reader.range(layout.k053260TableOffset, layout.sampleInstruments.size() * 2),
                       "konami-tmnt2-k053260-table");
  }
  for (u32 index = 0; index < layout.sampleInstruments.size(); ++index) {
    const auto& source = layout.sampleInstruments[index];
    const std::string name = fmt::format("K053260 Instrument {}", index);
    auto instrument =
        instruments.add(index, Instrument{
                                   .explicitAddress = InstrumentAddress{.bank = index >> 7, .program = index & 0x7f},
                                   .identity = InstrumentIdentity{.domain = std::string(kK053260Domain), .key = index},
                                   .reverb = 0.0,
                                   .name = name,
                                   .range = source.range,
                               });
    instrument.source(name, source.range, "konami-tmnt2-k053260-instrument").derived("sample", source.sampleIndex);
    const auto sample = samples.find(source.sampleIndex);
    if (source.sampleIndex == kInvalidSampleIndex) {
      continue;
    }
    if (!sample || source.sampleIndex >= layout.sampleInfos.size()) {
      instruments.warning("KonamiTMNT2 instrument refers to missing sample data", source.range);
      continue;
    }
    const auto& info = layout.sampleInfos[source.sampleIndex];
    const double unity = layout.version == Version::Vendetta ? 59.0 - pitchCents(info.pitch) / 100.0 : 59.0;
    instrument
        .region(*sample,
                Region{
                    .range = source.range,
                    .unityKey = unity,
                    .envelope =
                        Envelope{
                            .releaseSeconds =
                                sampledReleaseSeconds(layout.version, source.release, source.volume, ticksPerSecond),
                        },
                    .pan = panPosition(source.pan, layout.version),
                })
        .source("Region", source.range, "konami-tmnt2-k053260-region");
  }

  SourceRange drumRange;
  for (const auto& drumBank : layout.drumBanks) {
    for (const auto& drum : drumBank) {
      if (!drum.valid) {
        continue;
      }
      if (!drumRange.valid()) {
        drumRange = drum.range;
      } else {
        const u64 begin = std::min(drumRange.offset, drum.range.offset);
        const u64 end = std::max(drumRange.endOffset(), drum.range.endOffset());
        drumRange = SourceRange{.source = drum.range.source, .offset = begin, .size = end - begin};
      }
    }
  }
  if (drumRange.valid()) {
    auto drumKit =
        instruments.add(0x100, Instrument{
                                   .explicitAddress = InstrumentAddress{.bank = 2, .program = 0},
                                   .identity = InstrumentIdentity{.domain = std::string(kK053260Domain), .key = 0x100},
                                   .reverb = 0.0,
                                   .name = "K053260 Drum Kit",
                                   .range = drumRange,
                               });
    auto drumSource = drumKit.source("K053260 Drum Kit", drumRange, "konami-tmnt2-drum-kit");
    for (const auto& drumBank : layout.drumBanks) {
      for (const auto& drum : drumBank) {
        if (!drum.valid) {
          continue;
        }
        const u8 key = static_cast<u8>(drum.bank * 16 + drum.slot);
        const auto sample = samples.find(drum.sampleIndex);
        auto region =
            drumKit.region(sample.value_or(SampleRef::none()),
                           Region{
                               .keyRange = KeyRange{.low = key, .high = key},
                               .range = drum.range,
                               .unityKey = key - pitchCents(drum.pitch) / 100.0,
                               .envelope = Envelope{.releaseSeconds = sampledReleaseSeconds(
                                                        layout.version, drum.release, drum.volume, ticksPerSecond)},
                               .pan = panPosition(drum.pan, layout.version),
                           });
        region.source(fmt::format("Drum {}:{}", drum.bank, drum.slot), drum.range, "konami-tmnt2-drum")
            .derived("sample", drum.sampleIndex)
            .parent(drumSource.id());
        if (!sample) {
          instruments.warning("KonamiTMNT2 drum refers to missing sample data", drum.range);
        }
      }
    }
  }
  result.push_back(bank);
  return result;
}

}  // namespace vgmtrans::formats::konami_tmnt2
