/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SuzukiPS1/SuzukiPS1.h"

#include "value/base/RecordReader.h"
#include "value/synth/PsxAdpcm.h"
#include "value/synth/PsxSpu.h"

#include <fmt/format.h>

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace vgmtrans::formats::suzuki_ps1 {

using namespace core;

namespace {

constexpr u32 kBankHeaderSize = 0x30;
constexpr u32 kInstrumentSize = 0x10;

struct ParsedInstrument {
  u8 program = 0;
  u32 sampleOffset = 0;
  u32 loopOffset = 0;
  double unityKey = 60.0;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
  SourceRecord source;
};

[[nodiscard]] ParsedInstrument readInstrument(ByteReader reader, const SuzukiPs1BankLayout& layout, u8 program) {
  const u32 offset = layout.offset + kBankHeaderSize + static_cast<u32>(program) * kInstrumentSize;
  RecordReader record(reader, offset, offset + kInstrumentSize);
  const u32 unit = layout.kind == SuzukiPs1BankKind::Wds ? 8 : 1;
  const u32 sampleOffset = *record.u32leAt(0, "sample_offset") * unit;
  const u32 loopOffset = *record.u16leAt(4, "loop_offset") * unit;
  const u8 fine = *record.u8At(6, "fine_tune");
  const s8 semitone = *record.s8At(7, "semitone_tune");

  u16 adsr1 = 0;
  u16 adsr2 = 0;
  if (layout.kind == SuzukiPs1BankKind::Dwds) {
    const u8 attackRate = *record.u8At(8, "attack_rate");
    const u8 decayRate = *record.u8At(9, "decay_rate");
    const u8 sustainRate = *record.u8At(10, "sustain_rate");
    const u8 releaseRate = *record.u8At(11, "release_rate");
    const u8 sustainLevel = *record.u8At(12, "sustain_level");
    const u8 attackMode = *record.u8At(13, "attack_mode", SourceValueDisplay::Hex);
    const u8 sustainMode = *record.u8At(14, "sustain_mode", SourceValueDisplay::Hex);
    const u8 releaseMode = *record.u8At(15, "release_mode", SourceValueDisplay::Hex);
    adsr1 = composePsxAdsr1((attackMode & 4) >> 2, attackRate, decayRate, sustainLevel);
    adsr2 = composePsxAdsr2((sustainMode & 4) >> 2, (sustainMode & 2) >> 1, sustainRate, (releaseMode & 4) >> 2,
                            releaseRate);
  } else {
    const u32 rates = *record.u32leAt(8, "adsr_rates", SourceValueDisplay::Hex);
    const u16 modes = *record.u16leAt(12, "adsr_modes", SourceValueDisplay::Hex);
    (void)record.rangeAt(14, 2, "reserved");
    // WDS packs the attack, sustain, and release modes into adjacent
    // three-bit fields. Name them before selecting their native SPU bits.
    const u8 attackMode = modes & 0x07;
    const u8 sustainMode = (modes >> 4) & 0x07;
    const u8 releaseMode = (modes >> 8) & 0x07;
    adsr1 = composePsxAdsr1((attackMode >> 2) & 1, rates & 0x7f, (rates >> 8) & 0x0f, (rates >> 12) & 0x0f);
    adsr2 = composePsxAdsr2((sustainMode >> 2) & 1, (sustainMode >> 1) & 1, (rates >> 16) & 0x7f,
                            (releaseMode >> 2) & 1, (rates >> 24) & 0x1f);
  }
  record.derived("adsr1", adsr1, SourceValueDisplay::Hex);
  record.derived("adsr2", adsr2, SourceValueDisplay::Hex);
  return ParsedInstrument{
      .program = program,
      .sampleOffset = sampleOffset,
      .loopOffset = loopOffset,
      // The driver subtracts both pitch fields from middle C. Keeping the
      // fractional root avoids a lossy coarse/fine split during export.
      .unityKey = 60.0 - semitone - fine / 256.0,
      .adsr1 = adsr1,
      .adsr2 = adsr2,
      .source = std::move(record).finish(),
  };
}

}  // namespace

std::optional<SuzukiPs1ScannedBank> addSuzukiPs1Bank(ScanResultBuilder& result, const SuzukiPs1BankLayout& layout) {
  const ByteReader reader = result.reader();
  std::vector<ParsedInstrument> parsed;
  parsed.reserve(layout.highestProgram + 1);
  std::set<u32> sampleOffsets;
  for (u32 program = 0; program <= layout.highestProgram; ++program) {
    auto instrument = readInstrument(reader, layout, static_cast<u8>(program));
    if (instrument.sampleOffset < layout.sampleSize) {
      sampleOffsets.insert(instrument.sampleOffset);
    }
    parsed.push_back(std::move(instrument));
  }
  if (sampleOffsets.empty()) {
    return std::nullopt;
  }

  const u32 sampleSection = layout.offset + layout.headerSize;
  const u32 sampleEnd = sampleSection + layout.sampleSize;
  std::map<u32, PsxAdpcmStream> sampleStreams;
  for (auto current = sampleOffsets.begin(); current != sampleOffsets.end(); ++current) {
    const u32 boundary = std::next(current) == sampleOffsets.end() ? sampleEnd : sampleSection + *std::next(current);
    if (const auto stream = inspectPsxAdpcmStream(reader, sampleSection + *current, boundary)) {
      sampleStreams.emplace(*current, *stream);
    }
  }
  // Draft creation is the publication decision, so establish that both halves
  // of the synth are viable before creating either one.
  if (sampleStreams.empty()) {
    return std::nullopt;
  }

  const std::string bankName = fmt::format("SuzukiPS1 WDS {}", layout.bank);
  auto instruments = result.soundBank(bankName);
  auto& samples = instruments.samples();
  const SourceRange bankRange = reader.range(layout.offset, layout.length);
  const SourceAnnotationId sampleRoot =
      samples
          .source(SourceRole::SamplePool, "WDS Sample Data", reader.range(sampleSection, layout.sampleSize),
                  "suzuki-ps1-sample-data")
          .id();
  samples.include(bankRange);
  std::map<u32, SampleRef> sampleRefs;
  for (const auto& [offset, stream] : sampleStreams) {
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

  const SourceAnnotationId instrumentRoot =
      instruments
          .source(SourceRole::Table, "WDS Instrument Table",
                  reader.range(layout.offset + kBankHeaderSize,
                               static_cast<u32>(layout.highestProgram + 1) * kInstrumentSize),
                  "suzuki-ps1-instrument-table")
          .id();
  instruments.include(bankRange);
  instruments
      .source(SourceRole::Header, "WDS Header", reader.range(layout.offset, kBankHeaderSize), "suzuki-ps1-wds-header")
      .field("header_size", reader.range(layout.offset + 0x10, 4), layout.headerSize)
      .field("sample_size", reader.range(layout.offset + 0x14, 4), layout.sampleSize)
      .field("highest_program", reader.range(layout.offset + 0x1c, 4), layout.highestProgram)
      .field("bank", reader.range(layout.offset + 0x20, 4), layout.bank);
  std::vector<SuzukiPs1EnvelopeRegisters> envelopes;
  for (const ParsedInstrument& source : parsed) {
    envelopes.push_back(SuzukiPs1EnvelopeRegisters{
        .bank = layout.bank,
        .program = source.program,
        .adsr1 = source.adsr1,
        .adsr2 = source.adsr2,
    });
    const auto sample = sampleRefs.find(source.sampleOffset);
    if (sample == sampleRefs.end()) {
      continue;
    }

    const u32 exportBank = static_cast<u32>(layout.bank) * 2 + source.program / 128;
    auto instrument = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = exportBank, .program = static_cast<u32>(source.program & 0x7f)},
        .identity = suzukiPs1InstrumentIdentity(layout.bank, source.program),
        .name = fmt::format("Instrument {}", source.program),
        .range = source.source.range,
    });
    instrument.source(fmt::format("Instrument {}", source.program), source.source, "suzuki-ps1-instrument")
        .parent(instrumentRoot);

    Region region{
        .range = source.source.range,
        .unityKey = source.unityKey,
        .envelope = psxSpuEnvelope(source.adsr1, source.adsr2),
    };
    if (source.loopOffset != 0) {
      const auto stream = sampleStreams.find(source.sampleOffset);
      if (stream != sampleStreams.end() && source.loopOffset < stream->second.encodedData.size) {
        const u32 frames = psxAdpcmDecodedFrames(static_cast<u32>(stream->second.encodedData.size));
        const u32 loopStart = psxAdpcmDecodedOffset(source.loopOffset);
        region.loop = Loop{
            .enabled = stream->second.loop.enabled,
            .start = loopStart,
            .length = loopStart < frames ? frames - loopStart : 0,
        };
      }
    }
    instrument.region(sample->second, std::move(region)).source("Region", source.source.range, "suzuki-ps1-region");
  }

  return SuzukiPs1ScannedBank{
      .bank = instruments.ref(),
      .envelopes = std::move(envelopes),
  };
}

}  // namespace vgmtrans::formats::suzuki_ps1
