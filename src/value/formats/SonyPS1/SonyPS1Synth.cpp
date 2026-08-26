/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS1/SonyPS1.h"

#include "value/synth/PsxAdpcm.h"
#include "value/synth/PsxSpu.h"
#include "value/synth/SynthMath.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::sony_ps1 {

using namespace core;

namespace {

constexpr u32 kVabHeaderSize = 0x20;
constexpr u32 kProgramSize = 0x10;
constexpr u32 kToneSize = 0x20;
constexpr u32 kTonesPerProgram = 16;

struct PanGains {
  double left = 1.0;
  double right = 1.0;
};

[[nodiscard]] PanGains psxPan(u8 raw) {
  const u8 pan = std::min<u8>(raw, 127);
  if (pan < 64) {
    return PanGains{.left = 1.0, .right = pan / 64.0};
  }
  return PanGains{.left = (127 - pan) / 63.0, .right = 1.0};
}

[[nodiscard]] std::pair<double, double> regionPan(u8 programPan, u8 tonePan) {
  const PanGains program = psxPan(programPan);
  const PanGains tone = psxPan(tonePan);
  const double left = std::pow(program.left * tone.left, 2.0);
  const double right = std::pow(program.right * tone.right, 2.0);
  const double loudest = std::max(left, right);
  const double position = left + right == 0.0 ? 0.5 : right / (left + right);
  return {position, loudest};
}

[[nodiscard]] std::vector<PsxAdpcmStream> inspectRawBody(ByteReader reader) {
  const auto bodies = findSonyPs1SampleBodies(reader);
  if (bodies.empty()) {
    return {};
  }
  const auto body = std::ranges::max_element(bodies, {}, &SonyPs1SampleBodyLayout::length);
  std::vector<PsxAdpcmStream> streams;
  streams.reserve(body->samples.size());
  for (const auto& sample : body->samples) {
    streams.push_back(sample.stream);
  }
  return streams;
}

}  // namespace

void addSonyPs1Bank(ScanResultBuilder& result, const SonyPs1BankLayout& layout, u16 bank) {
  const ByteReader reader = result.reader();
  const std::string name = fmt::format("Sony PS1 VAB {}", bank);
  auto bankDraft = result.soundBank(name);
  bankDraft.data(SonyPs1SampleSize{.bytes = layout.expectedSampleBytes});
  auto& instruments = bankDraft.instruments();
  auto& samples = bankDraft.localSamples();

  if (layout.hasSampleBody) {
    std::vector<std::pair<u32, PsxAdpcmStream>> streams;
    u32 sampleOffset = layout.sampleDataOffset;
    for (u32 sample = 0; sample < layout.sampleSizes.size(); ++sample) {
      const u32 size = layout.sampleSizes[sample];
      if (size != 0) {
        if (auto stream = inspectPsxAdpcmStream(reader, sampleOffset, sampleOffset + size)) {
          streams.emplace_back(sample, *stream);
        }
      }
      sampleOffset += size;
    }

    if (!streams.empty()) {
      const SourceRange body = reader.range(layout.sampleDataOffset, layout.expectedSampleBytes);
      const SourceAnnotationId root =
          samples.source(SourceRole::SamplePool, "VAB Sample Body", body, "sony-ps1-vab-body").id();
      for (const auto& [index, stream] : streams) {
        auto entry = samples.add(index, Sample{
                                            .name = fmt::format("VAG {}", index + 1),
                                            .codec = AudioCodec::PsxAdpcm,
                                            .encodedData = stream.encodedData,
                                            .sampleRate = kPs1SpuSampleRate,
                                            .channels = 1,
                                            .bitsPerSample = 16,
                                            .loop = stream.loop,
                                        });
        entry.source(fmt::format("VAG {}", index + 1), stream.encodedData, "sony-ps1-vag").parent(root);
      }
    }
  }

  instruments.include(reader.range(layout.offset, layout.headerSize));
  instruments
      .source(SourceRole::Header, "VAB Header", reader.range(layout.offset, kVabHeaderSize), "sony-ps1-vab-header")
      .field("version", reader.range(layout.offset + 0x04, 4), layout.version)
      .field("vab_id", reader.range(layout.offset + 0x08, 4), layout.id)
      .field("declared_file_size", reader.range(layout.offset + 0x0c, 4), layout.declaredFileSize)
      .field("program_count", reader.range(layout.offset + 0x12, 2), layout.programCount)
      .field("tone_count", reader.range(layout.offset + 0x14, 2), layout.toneCount)
      .field("vag_count", reader.range(layout.offset + 0x16, 2), layout.sampleCount)
      .field("master_volume", reader.range(layout.offset + 0x18, 1), layout.masterVolume)
      .field("master_pan", reader.range(layout.offset + 0x19, 1), layout.masterPan)
      .derived("program_slots", layout.programSlots)
      .derived("sample_size_shift", layout.sampleSizeShift);

  const u32 programTable = layout.offset + kVabHeaderSize;
  const u32 toneTable = programTable + layout.programSlots * kProgramSize;
  const SourceAnnotationId programRoot =
      instruments
          .source(SourceRole::Table, "Program Attribute Table",
                  reader.range(programTable, layout.programSlots * kProgramSize), "sony-ps1-program-table")
          .id();
  const SourceAnnotationId toneRoot =
      instruments
          .source(SourceRole::Table, "Tone Attribute Table",
                  reader.range(toneTable, static_cast<u32>(layout.programCount) * kTonesPerProgram * kToneSize),
                  "sony-ps1-tone-table")
          .id();

  u32 effectiveProgram = 0;
  for (u32 program = 0; program < layout.programSlots && effectiveProgram < layout.programCount; ++program) {
    const u32 programOffset = programTable + program * kProgramSize;
    const u8 toneCount = reader.u8At(programOffset);
    if (toneCount == 0) {
      continue;
    }
    const u8 programVolume = reader.u8At(programOffset + 1);
    const u8 programPan = reader.u8At(programOffset + 4);
    const u32 programTones = toneTable + effectiveProgram * kTonesPerProgram * kToneSize;
    ++effectiveProgram;

    auto instrument = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = bank, .program = program},
        .identity = sonyPs1InstrumentIdentity(bank, static_cast<u8>(program)),
        .name = fmt::format("Program {}", program),
        .range = reader.range(programOffset, kProgramSize),
    });
    instrument.source(fmt::format("Program {}", program), reader.range(programOffset, kProgramSize), "sony-ps1-program")
        .fieldsAsChildren()
        .field("tone_count", reader.range(programOffset, 1), toneCount)
        .field("volume", reader.range(programOffset + 1, 1), programVolume)
        .field("priority", reader.range(programOffset + 2, 1), reader.u8At(programOffset + 2))
        .field("mode", reader.range(programOffset + 3, 1), reader.u8At(programOffset + 3), SourceValueDisplay::Hex)
        .field("pan", reader.range(programOffset + 4, 1), programPan)
        .parent(programRoot);

    for (u32 tone = 0; tone < std::min<u32>(toneCount, kTonesPerProgram); ++tone) {
      const u32 toneOffset = programTones + tone * kToneSize;
      const u8 toneVolume = reader.u8At(toneOffset + 2);
      const u8 tonePan = reader.u8At(toneOffset + 3);
      const u8 center = reader.u8At(toneOffset + 4);
      const u8 shift = std::min<u8>(reader.u8At(toneOffset + 5), 127);
      const u8 keyLow = reader.u8At(toneOffset + 6);
      const u8 keyHigh = reader.u8At(toneOffset + 7);
      const u16 adsr1 = reader.le16(toneOffset + 0x10);
      const u16 adsr2 = reader.le16(toneOffset + 0x12);
      const s16 vag = static_cast<s16>(reader.le16(toneOffset + 0x16));
      if (keyLow > keyHigh || vag <= 0 || vag > layout.sampleCount) {
        continue;
      }
      const u32 sampleIndex = static_cast<u32>(vag - 1);
      SampleRef sample = SampleRef::unbound(sampleIndex);
      if (layout.hasSampleBody) {
        const auto found = samples.find(sampleIndex);
        if (!found) {
          continue;
        }
        sample = *found;
      }

      const auto [pan, panGain] = regionPan(programPan, tonePan);
      const double amplitude = (std::min<u8>(layout.masterVolume, 127) / 127.0) *
                               (std::min<u8>(programVolume, 127) / 127.0) * (std::min<u8>(toneVolume, 127) / 127.0);
      Region region{
          .keyRange = KeyRange{.low = keyLow, .high = keyHigh},
          .sample = sample,
          .range = reader.range(toneOffset, kToneSize),
          // libsnd adds shift/128 to the played note before comparing it with
          // center, so the equivalent source-sample unity key is lower.
          .unityKey = center - shift / 128.0,
          .envelope = psxSpuEnvelope(adsr1, adsr2),
          .pan = pan,
          .attenuationDb = linearAmplitudeToAttenuationDb(amplitude * amplitude * panGain),
      };
      instrument.region(sample, std::move(region))
          .source(fmt::format("Tone {}", tone), reader.range(toneOffset, kToneSize), "sony-ps1-tone")
          .fieldsAsChildren()
          .field("volume", reader.range(toneOffset + 2, 1), toneVolume)
          .field("pan", reader.range(toneOffset + 3, 1), tonePan)
          .field("center", reader.range(toneOffset + 4, 1), center)
          .field("shift", reader.range(toneOffset + 5, 1), shift)
          .field("key_low", reader.range(toneOffset + 6, 1), keyLow, SourceValueDisplay::MidiNote)
          .field("key_high", reader.range(toneOffset + 7, 1), keyHigh, SourceValueDisplay::MidiNote)
          .field("adsr1", reader.range(toneOffset + 0x10, 2), adsr1, SourceValueDisplay::Hex)
          .field("adsr2", reader.range(toneOffset + 0x12, 2), adsr2, SourceValueDisplay::Hex)
          .field("vag", reader.range(toneOffset + 0x16, 2), vag)
          .parent(toneRoot);
    }
  }
}

bool addSonyPs1RawSampleBody(ScanResultBuilder& result) {
  const ByteReader reader = result.reader();
  const auto streams = inspectRawBody(reader);
  if (streams.empty()) {
    return false;
  }

  auto pool = result.samplePool(result.sourceDisplayName() + " VAG Samples");
  pool.data(SonyPs1SampleSize{.bytes = static_cast<u32>(reader.size())});
  auto& samples = pool.samples();
  const SourceRange body = reader.range(0, reader.size());
  const SourceAnnotationId root =
      samples.source(SourceRole::SamplePool, "VAB Sample Body", body, "sony-ps1-vab-body").id();
  samples.include(body);
  for (u32 index = 0; index < streams.size(); ++index) {
    const auto& stream = streams[index];
    auto entry = samples.add(index, Sample{
                                        .name = fmt::format("VAG {}", index + 1),
                                        .codec = AudioCodec::PsxAdpcm,
                                        .encodedData = stream.encodedData,
                                        .sampleRate = kPs1SpuSampleRate,
                                        .channels = 1,
                                        .bitsPerSample = 16,
                                        .loop = stream.loop,
                                    });
    entry.source(fmt::format("VAG {}", index + 1), stream.encodedData, "sony-ps1-vag").parent(root);
  }
  return true;
}

}  // namespace vgmtrans::formats::sony_ps1
