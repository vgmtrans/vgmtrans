/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HeartBeatPS1/HeartBeatPS1.h"

#include "value/base/RecordReader.h"
#include "value/synth/PsxAdpcm.h"
#include "value/synth/PsxSpu.h"
#include "value/synth/SynthMath.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <utility>

namespace vgmtrans::formats::heartbeat_ps1 {

using namespace core;

namespace {

constexpr u32 kAttributeHeaderSize = 8;
constexpr u32 kProgramSize = 0x24;
constexpr u32 kToneSize = 0x14;

struct ParsedProgram {
  u8 number = 0;
  std::array<u16, 16> toneIndices{};
  u8 volume = 127;
  u8 pan = 64;
  SourceRecord source;
};

[[nodiscard]] double driverLevel(u8 value) {
  return value == 127 ? 1.0 : std::min<u8>(value, 127) / 128.0;
}

[[nodiscard]] double driverPan(u8 masterPan, u8 programPan, u8 tonePan) {
  // The driver multiplies all four 0..127 pan factors, with 0x1000 as
  // center. A sequence-channel default of 64 supplies the fourth factor.
  const u32 hardware = static_cast<u32>(masterPan) * programPan * tonePan / 64;
  return std::clamp(hardware / 8192.0, 0.0, 1.0);
}

[[nodiscard]] std::vector<ParsedProgram> readPrograms(ByteReader reader, const HeartBeatPs1BankLayout& layout) {
  std::vector<ParsedProgram> programs;
  programs.reserve(layout.programCount);
  const u32 begin = layout.attributeOffset + kAttributeHeaderSize;
  for (u32 index = 0; index < layout.programCount; ++index) {
    const u32 offset = begin + index * kProgramSize;
    RecordReader record(reader, offset, offset + kProgramSize);
    ParsedProgram program{.number = static_cast<u8>(index)};
    for (u32 tone = 0; tone < program.toneIndices.size(); ++tone) {
      program.toneIndices[tone] = *record.u16leAt(tone * 2, fmt::format("tone_{}", tone), SourceValueDisplay::Hex);
    }
    program.volume = *record.u8At(0x20, "volume");
    program.pan = *record.u8At(0x21, "pan");
    static_cast<void>(record.u8At(0x22, "unknown_22", SourceValueDisplay::Hex));
    static_cast<void>(record.u8At(0x23, "unknown_23", SourceValueDisplay::Hex));
    program.source = std::move(record).finish();
    programs.push_back(std::move(program));
  }
  return programs;
}

[[nodiscard]] std::vector<HeartBeatPs1Tone> readTones(ByteReader reader, const HeartBeatPs1BankLayout& layout) {
  std::vector<HeartBeatPs1Tone> tones;
  tones.reserve(layout.toneCount);
  const u32 begin =
      layout.attributeOffset + kAttributeHeaderSize + static_cast<u32>(layout.programCount) * kProgramSize;
  for (u32 index = 0; index < layout.toneCount; ++index) {
    const u32 offset = begin + index * kToneSize;
    RecordReader record(reader, offset, offset + kToneSize);
    HeartBeatPs1Tone tone{
        .sampleOffset = *record.u32leAt(0, "sample_offset", SourceValueDisplay::Address),
        .adsr1 = *record.u16leAt(4, "adsr1", SourceValueDisplay::Hex),
        .adsr2 = *record.u16leAt(6, "adsr2", SourceValueDisplay::Hex),
    };
    static_cast<void>(record.u8At(8, "unknown_08", SourceValueDisplay::Hex));
    tone.volume = *record.u8At(9, "volume");
    tone.pan = *record.u8At(10, "pan");
    const u8 root = *record.u8At(11, "root_key", SourceValueDisplay::MidiNote);
    const u8 fine = *record.u8At(12, "fine_tune");
    tone.unityKey = root - (fine & 0x7f) / 128.0;
    tone.bendDownSemitones = *record.u8At(13, "pitch_bend_down");
    tone.bendUpSemitones = *record.u8At(14, "pitch_bend_up");
    tone.keys.low = *record.u8At(15, "key_low", SourceValueDisplay::MidiNote);
    tone.keys.high = *record.u8At(16, "key_high", SourceValueDisplay::MidiNote);
    tone.flags = *record.u8At(17, "flags", SourceValueDisplay::Hex);
    static_cast<void>(record.u8At(18, "priority"));
    static_cast<void>(record.u8At(19, "reserved", SourceValueDisplay::Hex));
    record.derived("unity_key", tone.unityKey);
    tone.source = std::move(record).finish();
    tones.push_back(std::move(tone));
  }
  return tones;
}

}  // namespace

std::optional<HeartBeatPs1ScannedBank> addHeartBeatPs1Bank(ScanResultBuilder& result,
                                                           const HeartBeatPs1BankLayout& layout) {
  const ByteReader reader = result.reader();
  const auto programs = readPrograms(reader, layout);
  const auto tones = readTones(reader, layout);

  std::set<u32> sampleOffsets;
  for (const auto& tone : tones) {
    if (tone.sampleOffset < layout.sampleSize && tone.keys.low <= tone.keys.high) {
      sampleOffsets.insert(tone.sampleOffset);
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

  const std::string bankName = fmt::format("HeartBeatPS1 Bank {}", layout.bank);
  auto bank = result.soundBank(bankName);
  auto& instruments = bank.instruments();
  auto& samples = bank.localSamples();
  const u32 totalSize = layout.sampleSize + layout.attributeSize;
  const SourceRange bankRange = reader.range(layout.sampleOffset, totalSize);
  instruments.include(bankRange);
  samples.include(bankRange);

  instruments
      .source(SourceRole::Header, "Wave Bank Header", reader.range(layout.attributeOffset, kAttributeHeaderSize),
              "heartbeat-ps1-bank-header")
      .field("slot", reader.range(layout.attributeOffset, 1), reader.u8At(layout.attributeOffset))
      .field("program_count", reader.range(layout.attributeOffset + 1, 1), layout.programCount)
      .field("tone_count", reader.range(layout.attributeOffset + 2, 2), layout.toneCount)
      .field("master_volume", reader.range(layout.attributeOffset + 4, 1), layout.masterVolume)
      .field("master_pan", reader.range(layout.attributeOffset + 5, 1), layout.masterPan)
      .field("flags", reader.range(layout.attributeOffset + 6, 1), reader.u8At(layout.attributeOffset + 6),
             SourceValueDisplay::Hex);
  const SourceAnnotationId programRoot = instruments
                                             .source(SourceRole::Table, "Program Table",
                                                     reader.range(layout.attributeOffset + kAttributeHeaderSize,
                                                                  static_cast<u32>(layout.programCount) * kProgramSize),
                                                     "heartbeat-ps1-program-table")
                                             .id();
  const SourceAnnotationId toneRoot = instruments
                                          .source(SourceRole::Table, "Tone Table",
                                                  reader.range(layout.attributeOffset + kAttributeHeaderSize +
                                                                   static_cast<u32>(layout.programCount) * kProgramSize,
                                                               static_cast<u32>(layout.toneCount) * kToneSize),
                                                  "heartbeat-ps1-tone-table")
                                          .id();
  const SourceAnnotationId sampleRoot =
      samples
          .source(SourceRole::SamplePool, "SPU Sample Data", reader.range(layout.sampleOffset, layout.sampleSize),
                  "heartbeat-ps1-sample-data")
          .id();

  std::map<u32, SampleRef> sampleRefs;
  for (const auto& [offset, stream] : streams) {
    const u32 index = static_cast<u32>(sampleRefs.size());
    auto entry = samples.add(offset, Sample{
                                         .name = fmt::format("Sample {}", index),
                                         .codec = AudioCodec::PsxAdpcm,
                                         .encodedData = stream.encodedData,
                                         .sampleRate = kPs1SpuSampleRate,
                                         .channels = 1,
                                         .bitsPerSample = 16,
                                         .loop = stream.loop,
                                     });
    entry.source(fmt::format("Sample {}", index), stream.encodedData, "psx-adpcm-sample").parent(sampleRoot);
    sampleRefs.emplace(offset, entry.ref());
  }

  std::vector<HeartBeatPs1InstrumentInfo> runtimeInstruments;
  for (const auto& sourceProgram : programs) {
    HeartBeatPs1InstrumentInfo runtime{.bank = layout.bank, .program = sourceProgram.number};
    for (const u16 toneIndex : sourceProgram.toneIndices) {
      if (toneIndex != 0xffff && toneIndex < tones.size()) {
        runtime.tones.push_back(tones[toneIndex]);
      }
    }
    if (runtime.tones.empty()) {
      continue;
    }

    const u32 exportBank = static_cast<u32>(layout.bank) * 2 + sourceProgram.number / 128;
    u8 bendRange = 0;
    for (const auto& tone : runtime.tones) {
      bendRange = std::max({bendRange, tone.bendDownSemitones, tone.bendUpSemitones});
    }
    auto instrument = instruments.append(Instrument{
        .explicitAddress =
            InstrumentAddress{.bank = exportBank, .program = static_cast<u32>(sourceProgram.number & 0x7f)},
        .identity = heartBeatPs1InstrumentIdentity(layout.bank, sourceProgram.number),
        .pitchBendRangeCents = static_cast<u16>(bendRange * 100),
        .reverb =
            std::ranges::any_of(runtime.tones, [](const auto& tone) { return (tone.flags & 4) != 0; }) ? 1.0 : 0.0,
        .name = fmt::format("Instrument {}", sourceProgram.number),
        .range = sourceProgram.source.range,
    });
    instrument.source(instrument.value().name, sourceProgram.source, "heartbeat-ps1-program").parent(programRoot);

    for (const u16 toneIndex : sourceProgram.toneIndices) {
      if (toneIndex == 0xffff || toneIndex >= tones.size()) {
        continue;
      }
      const HeartBeatPs1Tone& tone = tones[toneIndex];
      const auto sample = sampleRefs.find(tone.sampleOffset);
      if (sample == sampleRefs.end() || tone.keys.low > tone.keys.high) {
        continue;
      }
      const double gain =
          driverLevel(layout.masterVolume) * driverLevel(sourceProgram.volume) * driverLevel(tone.volume);
      instrument
          .region(sample->second,
                  Region{
                      .keyRange = tone.keys,
                      .range = tone.source.range,
                      .unityKey = tone.unityKey,
                      .envelope = psxSpuEnvelope(tone.adsr1, tone.adsr2),
                      .loop = streams.at(tone.sampleOffset).loop,
                      .pan = driverPan(layout.masterPan, sourceProgram.pan, tone.pan),
                      .attenuationDb = linearAmplitudeToAttenuationDb(gain),
                  })
          .source(fmt::format("Tone {}", toneIndex), tone.source, "heartbeat-ps1-tone")
          .parent(toneRoot);
    }
    runtimeInstruments.push_back(std::move(runtime));
  }
  if (runtimeInstruments.empty()) {
    return std::nullopt;
  }
  return HeartBeatPs1ScannedBank{.bank = bank, .instruments = std::move(runtimeInstruments)};
}

}  // namespace vgmtrans::formats::heartbeat_ps1
