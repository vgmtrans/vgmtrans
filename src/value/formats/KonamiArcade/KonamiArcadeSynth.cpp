/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiArcade/KonamiArcade.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace vgmtrans::formats::konami_arcade {

using namespace core;

namespace {

constexpr double kMelodicReleaseSeconds = 0.5;
constexpr double kDrumReleaseSeconds = 0.7;
constexpr u8 kDriverInstrumentAttenuation = 16;

[[nodiscard]] double attenuationDb(u32 value) {
  // K054539's table is 36 dB per 64 source steps.
  return 36.0 * value / 64.0;
}

[[nodiscard]] double drumPitch(const KonamiArcadeLayout& layout, const KonamiArcadeDrum& drum) {
  if (layout.version == KonamiArcadeVersion::Gx) {
    return drum.unityKey + drum.pitch / 256.0;
  }
  return drum.unityKey + (drum.pitch >> 2) / 16.0;
}

[[nodiscard]] std::optional<u32> sampleByteLength(ByteReader reader, SourceRange sound,
                                                  const KonamiArcadeSampleInfo& info) {
  if (info.type == KonamiSampleType::Unknown) {
    return std::nullopt;
  }
  if (info.startOffset > sound.size) {
    return std::nullopt;
  }

  const u16 marker = info.type == KonamiSampleType::Pcm16   ? 0x8000
                     : info.type == KonamiSampleType::Adpcm ? 0x8888
                                                            : 0x8080;
  const u32 step = info.type == KonamiSampleType::Pcm16 ? 2 : 1;
  const u32 soundBegin = static_cast<u32>(sound.offset);
  const u32 soundEnd = static_cast<u32>(sound.endOffset());
  const u32 start = soundBegin + info.startOffset;

  if (info.reverse) {
    if (start < soundBegin + 2) {
      return std::nullopt;
    }
    u32 offset = start - 2;
    while (true) {
      if (reader.has(offset, 2) && reader.le16(offset) == marker) {
        return start - offset;
      }
      if (offset < soundBegin + step) {
        break;
      }
      offset -= step;
    }
    return start - soundBegin;
  }

  for (u32 offset = start; offset + 2 <= soundEnd; offset += step) {
    if (reader.le16(offset) == marker) {
      return offset - start;
    }
    if (offset > soundEnd - 2 - std::min<u32>(step, soundEnd - 2)) {
      break;
    }
  }
  return soundEnd - start;
}

[[nodiscard]] u32 decodedFrames(KonamiSampleType type, u32 bytes) {
  switch (type) {
    case KonamiSampleType::Pcm8:
      return bytes;
    case KonamiSampleType::Pcm16:
      return bytes / 2;
    case KonamiSampleType::Adpcm:
      return bytes * 2;
    case KonamiSampleType::Unknown:
      return 0;
  }
  return 0;
}

[[nodiscard]] u32 decodedLoopStart(const KonamiArcadeSampleInfo& info, u32 byteLength) {
  const s64 relative = info.reverse ? static_cast<s64>(info.startOffset) - info.loopOffset
                                    : static_cast<s64>(info.loopOffset) - info.startOffset;
  // A loop address on the wrong side of playback direction cannot be reached.
  // Preserve the legacy driver's unsigned saturation behavior without allowing
  // the invalid address to wrap into the sample.
  const u32 byteOffset = relative < 0 ? byteLength : std::min<u64>(static_cast<u64>(relative), byteLength);
  return decodedFrames(info.type, byteOffset);
}

[[nodiscard]] AudioCodec codec(KonamiSampleType type) {
  switch (type) {
    case KonamiSampleType::Pcm8:
      return AudioCodec::PcmS8;
    case KonamiSampleType::Pcm16:
      return AudioCodec::PcmS16;
    case KonamiSampleType::Adpcm:
      return AudioCodec::KonamiK054539Adpcm;
    case KonamiSampleType::Unknown:
      return AudioCodec::Unknown;
  }
  return AudioCodec::Unknown;
}

}  // namespace

bool addKonamiArcadeSynth(ScanResultBuilder& builder, ScanInstrumentSetRef instrumentSet,
                          ScanSampleCollectionRef sampleCollection, const KonamiArcadeLayout& layout) {
  const ByteReader reader = builder.reader();
  auto samples = builder.samples(sampleCollection);
  samples.include(layout.sound);

  for (u32 index = 0; index < layout.sampleInfos.size(); ++index) {
    const auto& info = layout.sampleInfos[index];
    if (info.type == KonamiSampleType::Unknown) {
      samples.warning("KonamiArcade sample uses an unsupported codec", info.range);
      continue;
    }
    const auto byteLength = sampleByteLength(reader, layout.sound, info);
    if (!byteLength) {
      samples.warning("KonamiArcade sample start is outside the sound ROM region", info.range);
      continue;
    }

    const u32 relativeStart = info.reverse ? info.startOffset - *byteLength : info.startOffset;
    if (relativeStart > layout.sound.size || *byteLength > layout.sound.size - relativeStart) {
      samples.warning("KonamiArcade sample data range is outside the sound ROM region", info.range);
      continue;
    }
    const SourceRange encoded = reader.range(layout.sound.offset + relativeStart, *byteLength);
    const u32 frameCount = decodedFrames(info.type, *byteLength);
    const u32 loopStart = decodedLoopStart(info, *byteLength);
    const std::string name = fmt::format("Sample {}", index);
    samples
        .add(index,
             Sample{
                 .name = name,
                 .codec = codec(info.type),
                 .encodedData = encoded,
                 .sampleRate = kKonamiArcadeSampleRate,
                 .bitsPerSample = static_cast<u16>(info.type == KonamiSampleType::Pcm8 ? 8 : 16),
                 .reverse = info.reverse,
                 .loop =
                     Loop{
                         .enabled = info.loops,
                         .start = info.loops ? loopStart : 0,
                         .length = info.loops && frameCount >= loopStart ? frameCount - loopStart : 0,
                     },
                 // The 68000 GX driver adds a fixed 0x10 to every sample-info
                 // attenuation byte. The older Z80 driver uses it directly.
                 .attenuationDb =
                     attenuationDb(static_cast<u32>(info.attenuation) +
                                   (layout.version == KonamiArcadeVersion::Gx ? kDriverInstrumentAttenuation : 0)),
             })
        .source(name + " Info", info.range, "konami-arcade-sample-info");
  }
  if (samples.empty()) {
    return false;
  }

  auto instruments = builder.instruments(instrumentSet);
  const u32 melodicCount = std::min<u32>(layout.melodicSampleCount, static_cast<u32>(layout.sampleInfos.size()));
  if (melodicCount != 0) {
    const SourceRange tableRange{
        .source = layout.code.source,
        .offset = layout.sampleInfos.front().range.offset,
        .size = layout.sampleInfos[melodicCount - 1].range.endOffset() - layout.sampleInfos.front().range.offset,
    };
    instruments.include(tableRange);
    instruments.source(SourceRole::Table, "Instrument Sample Info Table", tableRange, "konami-arcade-instrument-table");
  }

  for (u32 index = 0; index < melodicCount; ++index) {
    const SourceRange range = layout.sampleInfos[index].range;
    const u32 bank = index >> 7;
    const u32 program = index & 0x7f;
    const std::string name = fmt::format("Instrument {} Bank {}", program, bank);
    auto instrument = instruments.add(
        index, Instrument{
                   .explicitAddress = InstrumentAddress{.bank = bank, .program = program},
                   .identity = InstrumentIdentity{.domain = std::string(kKonamiArcadeInstrumentDomain), .key = index},
                   .name = name,
                   .range = range,
               });
    instrument.source(name, range, "konami-arcade-instrument").derived("sample", index);
    const auto sample = samples.find(index);
    if (!sample) {
      instruments.warning("KonamiArcade instrument refers to a sample with unsupported or missing data", range);
      continue;
    }
    instrument
        .region(*sample,
                Region{
                    .range = range,
                    .unityKey = 66.0,
                    .envelope = Envelope{.releaseSeconds = kMelodicReleaseSeconds},
                })
        .source("Region", range, "konami-arcade-region");
  }

  if (layout.drumTableOffset) {
    const u64 tableSize =
        layout.drumCount != 0
            ? layout.drums[layout.drumCount - 1].range.endOffset() - layout.drums.front().range.offset
            : std::min<u64>(layout.drums.size() * 8, layout.code.endOffset() - *layout.drumTableOffset);
    const SourceRange drumTable{
        .source = layout.code.source,
        .offset = layout.drumCount != 0 ? layout.drums.front().range.offset : *layout.drumTableOffset,
        .size = tableSize,
    };
    instruments.include(drumTable);
    instruments.source(SourceRole::Table, "Drum Kit Table", drumTable, "konami-arcade-drum-table");
    auto drumKit = instruments.add(
        0x100, Instrument{
                   .explicitAddress = InstrumentAddress{.bank = 2, .program = 0},
                   .identity = InstrumentIdentity{.domain = std::string(kKonamiArcadeInstrumentDomain), .key = 0x100},
                   .name = "Drum Kit",
                   .range = drumTable,
               });
    auto drumAnnotation = drumKit.source("Drum Kit", drumTable, "konami-arcade-drum-kit");
    for (u32 index = 0; index < layout.drumCount; ++index) {
      const auto& drum = layout.drums[index];
      const u32 sourceSample = melodicCount + drum.sample;
      const auto sample = samples.find(sourceSample);
      const SampleRef sampleRef = sample.value_or(SampleRef{.index = invalidIdValue});
      const u8 key = static_cast<u8>(index + 24);
      const double driverPitch = drumPitch(layout, drum);
      auto region = drumKit.region(sampleRef, Region{
                                                  .keyRange = KeyRange{.low = key, .high = key},
                                                  .range = drum.range,
                                                  .unityKey = static_cast<double>(key) + 0x2a - driverPitch,
                                                  .envelope = Envelope{.releaseSeconds = kDrumReleaseSeconds},
                                                  .attenuationDb = attenuationDb(drum.attenuation),
                                              });
      auto annotation = region.source(fmt::format("Drum {}", index), drum.range, "konami-arcade-drum");
      annotation.derived("sample", sourceSample);
      if (!sample) {
        instruments.warning("KonamiArcade drum refers to an unsupported or missing sample", drum.range);
      }
      annotation.parent(drumAnnotation.id());
    }
  }

  if (instruments.empty()) {
    return false;
  }
  builder.instrumentSet(layout.game + " Instruments", std::move(instruments));
  builder.sampleCollection(layout.game + " Samples", std::move(samples));
  return true;
}

}  // namespace vgmtrans::formats::konami_arcade
