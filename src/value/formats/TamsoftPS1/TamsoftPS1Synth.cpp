/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TamsoftPS1/TamsoftPS1.h"

#include "value/synth/PsxAdpcm.h"
#include "value/synth/PsxSpu.h"

#include <fmt/format.h>

#include <iterator>
#include <map>
#include <set>
#include <string>

namespace vgmtrans::formats::tamsoft_ps1 {

using namespace core;

namespace {

[[nodiscard]] u32 decodedAdsr(u32 stored, Generation generation) {
  // HG2 stores complemented SPU fields. load_tvbf reconstructs both register
  // words with this exact mask before chgtone writes them to the SPU2.
  return generation == Generation::Ps2 ? (~stored & 0xdffffff0) | (stored & 0x0f) : stored;
}

}  // namespace

bool addBank(ScanResultBuilder& result, const BankLayout& layout, std::string_view name) {
  const ByteReader reader = result.reader();
  std::set<u32> offsets;
  for (u32 program = 1; program < kProgramCount; ++program) {
    const u32 offset = reader.le32(program * 4);
    if (offset != 0 && offset < layout.sampleSize) {
      offsets.insert(offset);
    }
  }

  std::map<u32, PsxAdpcmStream> streams;
  for (auto current = offsets.begin(); current != offsets.end(); ++current) {
    const auto next = std::next(current);
    const u32 boundary = kBankHeaderSize + (next == offsets.end() ? layout.sampleSize : *next);
    if (const auto stream = inspectPsxAdpcmStream(reader, kBankHeaderSize + *current, boundary)) {
      streams.emplace(*current, *stream);
    }
  }
  if (streams.empty()) {
    return false;
  }

  auto bank = result.soundBank(std::string(name), reader.range(0, reader.size()));
  auto& instruments = bank.instruments();
  auto& samples = bank.localSamples();
  instruments.include(reader.range(0, kBankHeaderSize));
  samples.include(reader.range(kBankHeaderSize, layout.sampleSize));
  const SourceAnnotationId pointerRoot =
      instruments.source(SourceRole::Table, "Sample Pointer Table", reader.range(0, kProgramTableSize),
                         "tamsoft-ps1-sample-pointers")
          .id();
  const SourceAnnotationId adsrRoot =
      instruments.source(SourceRole::Table, "ADSR Table",
                         reader.range(kProgramTableSize, kProgramTableSize), "tamsoft-ps1-adsr-table")
          .id();
  const SourceAnnotationId sampleRoot =
      samples.source(SourceRole::SamplePool,
                     layout.generation == Generation::Ps2 ? "SPU2 ADPCM Sample Data" : "SPU ADPCM Sample Data",
                     reader.range(kBankHeaderSize, layout.sampleSize), "tamsoft-ps1-sample-data")
          .id();

  std::map<u32, SampleRef> sampleRefs;
  const u32 sampleRate = layout.generation == Generation::Ps2 ? kPs2SpuSampleRate : kPs1SpuSampleRate;
  for (const auto& [offset, stream] : streams) {
    const u32 index = static_cast<u32>(sampleRefs.size());
    auto sample = samples.add(offset, Sample{
                                          .name = fmt::format("Sample {}", index),
                                          .codec = AudioCodec::PsxAdpcm,
                                          .encodedData = stream.encodedData,
                                          .sampleRate = sampleRate,
                                          .channels = 1,
                                          .bitsPerSample = 16,
                                          .loop = stream.loop,
                                      });
    sample.source(sample.value().name, stream.encodedData, "psx-adpcm-sample").parent(sampleRoot);
    sampleRefs.emplace(offset, sample.ref());
  }

  const PsxSpuGeneration spuGeneration =
      layout.generation == Generation::Ps2 ? PsxSpuGeneration::Ps2 : PsxSpuGeneration::Ps1;
  for (u32 program = 1; program < kProgramCount; ++program) {
    const u32 sampleOffset = reader.le32(program * 4);
    const auto sample = sampleRefs.find(sampleOffset);
    if (sample == sampleRefs.end()) {
      continue;
    }
    const u32 storedAdsr = reader.le32(kProgramTableSize + program * 4);
    const u32 adsr = decodedAdsr(storedAdsr, layout.generation);
    const SourceRange pointerRange = reader.range(program * 4, 4);
    const SourceRange adsrRange = reader.range(kProgramTableSize + program * 4, 4);
    auto instrument = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = program / 128, .program = program % 128},
        .identity = instrumentIdentity(static_cast<u8>(program)),
        .reverb = 0.0,
        .name = fmt::format("Instrument {}", program),
        .range = pointerRange,
    });
    instrument.source(instrument.value().name, pointerRange, "tamsoft-ps1-instrument").parent(pointerRoot);
    instrument
        .region(sample->second,
                Region{
                    .keyRange = KeyRange{.low = 0, .high = 127},
                    .range = adsrRange,
                    .unityKey = 48.0,
                    .envelope = psxSpuEnvelope(static_cast<u16>(adsr), static_cast<u16>(adsr >> 16), spuGeneration),
                    .loop = streams.at(sampleOffset).loop,
                })
        .source("ADSR", adsrRange, "tamsoft-ps1-region")
        .parent(adsrRoot)
        .field("stored_adsr1", reader.range(adsrRange.offset, 2), static_cast<u16>(storedAdsr),
               SourceValueDisplay::Hex)
        .field("stored_adsr2", reader.range(adsrRange.offset + 2, 2), static_cast<u16>(storedAdsr >> 16),
               SourceValueDisplay::Hex)
        .derived("adsr1", static_cast<u16>(adsr), SourceValueDisplay::Hex)
        .derived("adsr2", static_cast<u16>(adsr >> 16), SourceValueDisplay::Hex);
  }
  bank.data(BankData{.stem = std::string(name), .generation = layout.generation});
  return !instruments.empty();
}

}  // namespace vgmtrans::formats::tamsoft_ps1
