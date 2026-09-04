/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SquarePS2/SquarePS2.h"

#include "value/base/RecordReader.h"
#include "value/synth/PsxAdpcm.h"
#include "value/synth/PsxSpu.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace vgmtrans::formats::square_ps2 {

using namespace core;

namespace {

constexpr u32 kRegionSize = 0x20;

struct ParsedRegion {
  u32 flags = 0;
  u32 sampleOffset = 0;
  u32 loopOffset = 0;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
  s16 tuning = 0;
  u8 keyHigh = 127;
  u8 velocityHigh = 127;
  u8 level = 127;
  u8 pan = 0;
  u8 routing = 0;
  SourceRecord source;
};

[[nodiscard]] ParsedRegion readRegion(ByteReader reader, u32 offset) {
  RecordReader record(reader, offset, offset + kRegionSize);
  ParsedRegion region;
  region.flags = *record.u32leAt(0, "flags", SourceValueDisplay::Hex);
  region.sampleOffset = *record.u32leAt(4, "sample_offset", SourceValueDisplay::Address);
  region.loopOffset = *record.u32leAt(8, "loop_offset", SourceValueDisplay::Address);
  region.adsr1 = *record.u16leAt(0x0c, "adsr1", SourceValueDisplay::Hex);
  region.adsr2 = *record.u16leAt(0x0e, "adsr2", SourceValueDisplay::Hex);
  region.tuning = static_cast<s16>(*record.u16leAt(0x12, "pitch_correction"));
  region.keyHigh = *record.u8At(0x14, "key_high", SourceValueDisplay::MidiNote);
  region.velocityHigh = *record.u8At(0x15, "velocity_high");
  region.level = *record.u8At(0x16, "level");
  region.pan = *record.u8At(0x17, "pan", SourceValueDisplay::Hex);
  region.routing = *record.u8At(0x18, "routing", SourceValueDisplay::Hex);
  (void)record.rangeAt(0x10, 2, "pitch_fraction");
  (void)record.rangeAt(0x19, 7, "reserved");
  region.source = std::move(record).finish();
  return region;
}

[[nodiscard]] double attenuation(u8 raw) {
  const double gain = (raw & 0x7f) == 0 ? 0.0 : ((raw & 0x7f) + 1) / 128.0;
  return gain == 0.0 ? 96.0 : -20.0 * std::log10(gain);
}

[[nodiscard]] double regionPan(u8 raw) {
  if ((raw & 0x80) == 0) {
    return 0.5;
  }
  const u8 position = raw & 0x7f;
  return position == 0 ? 0.5 : position / 127.0;
}

}  // namespace

std::optional<ScanSoundBankDraft> addWd(ScanResultBuilder& result, const WdLayout& layout) {
  const ByteReader reader = result.reader();
  std::vector<std::vector<ParsedRegion>> programs(layout.instrumentCount);
  std::set<u32> sampleOffsets;
  std::vector<u32> programOffsets(layout.instrumentCount);
  for (u32 program = 0; program < layout.instrumentCount; ++program) {
    programOffsets[program] = reader.le32(layout.instrumentTableOffset + program * 4);
  }
  for (u32 program = 0; program < layout.instrumentCount; ++program) {
    const u32 relative = programOffsets[program];
    if (relative == 0 || relative == std::numeric_limits<u32>::max()) {
      continue;
    }
    // Selection flags describe key/velocity thresholds; pointer spans own the records.
    u32 relativeEnd = layout.sampleOffset - layout.offset;
    for (const u32 candidate : programOffsets) {
      if (candidate > relative) {
        relativeEnd = std::min(relativeEnd, candidate);
      }
    }
    u32 regionOffset = layout.offset + relative;
    const u32 regionEnd = layout.offset + relativeEnd;
    while (regionOffset + kRegionSize <= regionEnd) {
      auto region = readRegion(reader, regionOffset);
      const u32 flags = region.flags;
      const u32 sampleOffset = region.sampleOffset & ~u32{0x0f};
      if (sampleOffset < layout.sampleSize) {
        region.sampleOffset = sampleOffset;
        sampleOffsets.insert(sampleOffset);
        programs[program].push_back(std::move(region));
      }
      regionOffset += kRegionSize;
      if ((flags & 1) != 0 && regionOffset + kRegionSize <= regionEnd) {
        auto stereo = readRegion(reader, regionOffset);
        stereo.sampleOffset &= ~u32{0x0f};
        if (stereo.sampleOffset < layout.sampleSize) {
          sampleOffsets.insert(stereo.sampleOffset);
          programs[program].push_back(std::move(stereo));
        }
        regionOffset += kRegionSize;
      }
    }
  }
  if (sampleOffsets.empty()) {
    return std::nullopt;
  }

  const u32 sampleEnd = layout.sampleOffset + layout.sampleSize;
  std::map<u32, PsxAdpcmStream> streams;
  for (auto current = sampleOffsets.begin(); current != sampleOffsets.end(); ++current) {
    const u32 boundary =
        std::next(current) == sampleOffsets.end() ? sampleEnd : layout.sampleOffset + *std::next(current);
    if (const auto stream = inspectPsxAdpcmStream(reader, layout.sampleOffset + *current, boundary)) {
      streams.emplace(*current, *stream);
    }
  }
  if (streams.empty()) {
    return std::nullopt;
  }

  auto bank = result.soundBank(fmt::format("SquarePS2 WD {}", layout.bankId));
  auto& instruments = bank.instruments();
  auto& samples = bank.localSamples();
  const SourceRange bankRange = reader.range(layout.offset, layout.length);
  instruments.include(bankRange);
  samples.include(bankRange);
  instruments.source(SourceRole::Header, "WD Header", reader.range(layout.offset, 0x20), "square-ps2-wd-header")
      .fieldsAsChildren()
      .field("bank_id", reader.range(layout.offset + 2, 2), layout.bankId)
      .field("sample_size", reader.range(layout.offset + 4, 4), reader.le32(layout.offset + 4))
      .field("instrument_count", reader.range(layout.offset + 8, 4), layout.instrumentCount)
      .field("region_count", reader.range(layout.offset + 0x0c, 4), layout.regionCount);

  std::map<u32, SampleRef> refs;
  for (const auto& [offset, stream] : streams) {
    const u32 index = static_cast<u32>(refs.size());
    auto entry = samples.add(offset, Sample{
                                         .name = fmt::format("Sample {}", index),
                                         .codec = AudioCodec::PsxAdpcm,
                                         .encodedData = stream.encodedData,
                                         .sampleRate = kPs2SpuSampleRate,
                                         .channels = 1,
                                         .bitsPerSample = 16,
                                         .loop = stream.loop,
                                     });
    entry.source(fmt::format("Sample {}", index), stream.encodedData, "psx-adpcm-sample");
    refs.emplace(offset, entry.ref());
  }

  SoundBankData data{.bankId = layout.bankId};
  for (u32 program = 0; program < programs.size(); ++program) {
    const auto& regions = programs[program];
    if (regions.empty()) {
      continue;
    }
    const u32 programOffset = regions.front().source.range.offset;
    auto instrument = instruments.append(Instrument{
        // The WD ID selects a loaded driver bank; it is not a MIDI/SF2 bank number.
        .explicitAddress = InstrumentAddress{.bank = 0, .program = program},
        .identity = instrumentIdentity(layout.bankId, static_cast<u8>(program)),
        .reverb =
            std::ranges::any_of(regions, [](const ParsedRegion& region) { return region.routing != 0; }) ? 1.0 : 0.0,
        .name = fmt::format("Instrument {}", program),
        .range = reader.range(programOffset, static_cast<u32>(regions.size()) * kRegionSize),
    });
    instrument.source(instrument.value().name, instrument.value().range, "square-ps2-instrument");
    data.envelopes.push_back(EnvelopeDefaults{
        .bank = layout.bankId,
        .program = static_cast<u8>(program),
        .adsr1 = regions.front().adsr1,
        .adsr2 = regions.front().adsr2,
    });

    u8 keyLow = 0;
    u8 velocityLow = 0;
    u8 previousKeyHigh = 0xff;
    for (size_t index = 0; index < regions.size();) {
      const auto& primary = regions[index];
      if (previousKeyHigh != primary.keyHigh) {
        keyLow = previousKeyHigh == 0xff ? 0 : static_cast<u8>(std::min<unsigned>(previousKeyHigh + 1, 127));
        velocityLow = 0;
        previousKeyHigh = primary.keyHigh;
      }
      const KeyRange keys{.low = keyLow, .high = std::min<u8>(primary.keyHigh, 127)};
      const VelocityRange velocities{.low = velocityLow, .high = std::min<u8>(primary.velocityHigh, 127)};
      const size_t layers = (primary.flags & 1) != 0 && index + 1 < regions.size() ? 2 : 1;
      for (size_t layer = 0; layer < layers; ++layer) {
        const auto& parsedRegion = regions[index + layer];
        const auto sample = refs.find(parsedRegion.sampleOffset);
        if (sample == refs.end()) {
          continue;
        }
        Region region{
            .keyRange = keys,
            .velocityRange = velocities,
            .range = parsedRegion.source.range,
            .unityKey = 60.0 - parsedRegion.tuning / 256.0,
            .envelope = psxSpuEnvelope(parsedRegion.adsr1, parsedRegion.adsr2, PsxSpuGeneration::Ps2),
            .pan = layers == 2 ? static_cast<double>(layer) : regionPan(parsedRegion.pan),
            .attenuationDb = attenuation(parsedRegion.level),
        };
        // The driver adds this WD field to the sample address; it is not pool-relative.
        if (const auto stream = streams.find(parsedRegion.sampleOffset);
            stream != streams.end() && stream->second.loop.enabled &&
            parsedRegion.loopOffset < stream->second.encodedData.size) {
          const u32 frames = psxAdpcmDecodedFrames(static_cast<u32>(stream->second.encodedData.size));
          const u32 loopStart = psxAdpcmDecodedOffset(parsedRegion.loopOffset);
          if (loopStart < frames) {
            region.loop = Loop{.enabled = true, .start = loopStart, .length = frames - loopStart};
          }
        }
        instrument.region(sample->second, std::move(region)).source("Region", parsedRegion.source, "square-ps2-region");
      }
      velocityLow = primary.velocityHigh >= 127 ? 0 : static_cast<u8>(primary.velocityHigh + 1);
      index += layers;
    }
  }
  bank.data(std::move(data));
  return bank;
}

}  // namespace vgmtrans::formats::square_ps2
