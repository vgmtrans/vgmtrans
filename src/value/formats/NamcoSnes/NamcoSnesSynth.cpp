/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NamcoSnes/NamcoSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <string>
#include <vector>

namespace vgmtrans::formats::namco_snes {

using namespace core;

namespace {

// Timer 0's $86 target runs at 8 kHz: one driver tick is 16.75 ms (not the
// 1.675 ms stated in the original disassembly comment).
constexpr double kDriverTickSeconds = 134.0 / 8000.0;
constexpr u32 kDrumBank = 127;
constexpr u32 kDrumProgram = 0;
constexpr u32 kNoiseBank = 126;
constexpr u32 kNoiseProgram = 0;

[[nodiscard]] std::optional<u16> tableEntry(ByteReader reader, u16 table, u8 index) {
  const u32 pointer = table + index * 2u;
  if (!reader.has(pointer, 2)) {
    return std::nullopt;
  }
  const u16 address = reader.le16(pointer);
  return reader.has(address, 1) ? std::optional<u16>{address} : std::nullopt;
}

[[nodiscard]] double customStageTicks(u8 from, u8 to, u8 step) {
  if (step == 0 || from == to) {
    return 0.0;
  }
  return std::ceil(std::abs(static_cast<int>(to) - from) * 256.0 / (step * step));
}

[[nodiscard]] double releaseSeconds(ByteReader reader, u16 script, u32 releaseOffset) {
  if (!reader.has(script + releaseOffset, 1)) {
    return 0.0;
  }
  const u8 gain = static_cast<u8>(0xa0 | (reader.u8At(script + releaseOffset) & 0x1f));
  return snesDspGainEnvelopeSeconds(gain, 0x7ff, 0);
}

[[nodiscard]] double unityKey(Version version, u16 pitchScale) {
  // The engine multiplies the per-SRCN 8.8 scale by its C pitch-table entry.
  // Blue Crystal Rod ships the slightly flatter $0F6F table; the other two
  // audited drivers use $0FC0.
  const double pitchTableCorrection = (version == Version::BlueCrystalRod ? 0x0f6f : 0x0fc0) / 4096.0;
  return 71.0 - std::log2((pitchScale / 256.0) * pitchTableCorrection) * 12.0;
}

struct Drum {
  u8 index;
  u8 srcn;
  u8 envelope;
  u8 volume;
  u8 balance;
  u8 sourceKey;

  [[nodiscard]] bool noise() const { return sourceKey > kRest && sourceKey < 0x80; }
  [[nodiscard]] u8 noiseRate() const { return sourceKey & 0x1f; }
};

[[nodiscard]] std::vector<Drum> collectDrums(ByteReader reader, const Layout& layout,
                                             const std::set<u8>& referenced) {
  std::vector<Drum> drums;
  const u16 table = layout.percussionTable(reader);
  for (const u8 index : referenced) {
    const u32 row = table + index * 5u;
    if (!reader.has(row, 5)) {
      continue;
    }
    const u8 sourceKey = reader.u8At(row + 4);
    if (sourceKey == kRest || sourceKey >= 0x80) {
      continue;
    }
    drums.push_back(Drum{
        .index = index,
        .srcn = reader.u8At(row),
        .envelope = reader.u8At(row + 1),
        .volume = reader.u8At(row + 2),
        .balance = reader.u8At(row + 3),
        .sourceKey = sourceKey,
    });
  }
  return drums;
}

[[nodiscard]] std::vector<u8> referencedSamples(const std::set<u8>& melodic, const std::vector<Drum>& drums) {
  std::set<u8> unique = melodic;
  for (const Drum& drum : drums) {
    if (!drum.noise()) {
      unique.insert(drum.srcn);
    }
  }
  return {unique.begin(), unique.end()};
}

[[nodiscard]] std::optional<u16> tuning(ByteReader reader, const Layout& layout, u8 srcn) {
  const u32 address = layout.tuningTableAddress + srcn * 2u;
  if (!reader.has(address, 2)) {
    return std::nullopt;
  }
  const u16 scale = reader.be16(address);
  return scale == 0 || scale == 0xffff ? std::nullopt : std::optional<u16>{scale};
}

}  // namespace

Envelope driverAmplitudeEnvelope(ByteReader reader, const Layout& layout, u8 index) {
  const auto script = tableEntry(reader, layout.amplitudeEnvelopePointerTable(reader), index);
  if (!script) {
    return snesDspEnvelope(0, 0, 0x7f);
  }

  const u8 first = reader.u8At(*script);
  if (index == 0) {
    Envelope envelope = snesDspEnvelope(0, 0, 0x7f);
    envelope.releaseSeconds = releaseSeconds(reader, *script, 2);
    return envelope;
  }
  if ((first & 0x80) != 0 && reader.has(*script, 3)) {
    Envelope envelope = snesDspEnvelope(first, reader.u8At(*script + 1), 0);
    envelope.releaseSeconds = releaseSeconds(reader, *script, 2);
    return envelope;
  }

  // Software GAIN scripts are step/target pairs. F0 holds, F2 introduces the
  // release rate, and F4 loops. Reduce their piecewise curve to the portable
  // attack/decay/sustain model while retaining the driver's physical timing.
  u8 current = 0;
  u8 peak = 0;
  u8 sustain = 0;
  double attackTicks = 0.0;
  double decayTicks = 0.0;
  bool falling = false;
  double release = std::numeric_limits<double>::infinity();
  for (u32 offset = 0; offset < 64 && reader.has(*script + offset, 1);) {
    const u8 step = reader.u8At(*script + offset);
    if (step >= 0xf0) {
      if ((step & 0x0f) == 2) {
        release = releaseSeconds(reader, *script, offset + 1);
      }
      break;
    }
    if (!reader.has(*script + offset + 1, 1)) {
      break;
    }
    const u8 target = std::min<u8>(reader.u8At(*script + offset + 1), 0x7f);
    const double ticks = customStageTicks(current, target, step);
    if (!falling && target >= current) {
      attackTicks += ticks;
      peak = std::max(peak, target);
    } else {
      falling = true;
      decayTicks += ticks;
    }
    current = target;
    sustain = target;
    offset += 2;
  }
  return Envelope{
      .attackSeconds = attackTicks * kDriverTickSeconds,
      .holdSeconds = 0.0,
      .decaySeconds = decayTicks * kDriverTickSeconds,
      .releaseSeconds = release,
      .sustainAmplitude = peak == 0 ? 0.0 : static_cast<double>(sustain) / peak,
  };
}

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const std::set<u8>& srcns, const std::set<u8>& percussion,
                                           const std::set<u8>& noiseRates,
                                           std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<Drum> drums = collectDrums(reader, layout, percussion);
  std::set<u8> melodic = srcns;
  melodic.insert(0);
  const std::vector<u8> usedSrcns = referencedSamples(melodic, drums);
  // The paired tables use one four-byte DIR and one two-byte tuning entry per SRCN.
  const u32 srcnCount = (layout.tuningTableAddress - layout.spcDirAddress) / 4u;
  std::vector<u8> allSrcns(srcnCount);
  std::iota(allSrcns.begin(), allSrcns.end(), 0);
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, allSrcns);
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  const SourceAnnotationId tuningTable =
      instruments
          .source(SourceRole::Table, "Tuning Table",
                  reader.range(layout.tuningTableAddress, srcnCount * 2u), "namco-snes-tuning-table")
          .id();
  for (u32 srcn = 0; srcn < srcnCount; ++srcn) {
    const u32 address = layout.tuningTableAddress + srcn * 2u;
    const bool used = std::ranges::binary_search(usedSrcns, static_cast<u8>(srcn));
    instruments
        .source(SourceRole::TableEntry, fmt::format("Tuning {}{}", srcn, used ? "" : " (unused)"),
                reader.range(address, 2), "namco-snes-tuning")
        .parent(tuningTable)
        .field("pitch_scale", reader.range(address, 2), reader.be16(address), SourceValueDisplay::Hex)
        .description("Big-endian 8.8 pitch scale for the corresponding SRCN");
  }
  const SnesBrrSampleRefs samples = addSnesBrrSamples(bank.localSamples(), reader, catalog, usedSrcns);
  const Envelope neutral = snesDspEnvelope(0, 0, 0x7f);

  std::set<u8> referencedNoise = noiseRates;
  for (const Drum& drum : drums) {
    if (drum.noise()) {
      referencedNoise.insert(drum.noiseRate());
    }
  }
  std::array<SampleRef, 32> noiseSamples;
  for (const u8 rate : referencedNoise) {
    noiseSamples[rate] = bank.localSamples()
                             .add(kNoiseInstrumentKey + rate,
                                  Sample{.name = fmt::format("DSP Noise {}", rate),
                                         .codec = AudioCodec::SnesDspNoise,
                                         .encodedData = reader.range(0, 0),
                                         .sampleRate = kSnesDspSampleRate,
                                         .loop = Loop{.enabled = true, .length = kSnesDspNoiseSampleCount},
                                         .codecParameter = rate})
                             .ref();
  }

  for (const u8 srcn : melodic) {
    const auto sample = samples.findSrcn(srcn);
    const auto scale = tuning(reader, layout, srcn);
    if (!sample || !scale) {
      continue;
    }
    auto instrument = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = static_cast<u32>(srcn >> 7),
                                             .program = static_cast<u32>(srcn & 0x7f)},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = srcn},
        .name = fmt::format("Instrument {}", static_cast<unsigned>(srcn)),
    });
    instrument.region(*sample, Region{.unityKey = unityKey(layout.version, *scale),
                                      .envelope = neutral});
  }

  if (!noiseRates.empty()) {
    auto noise = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = kNoiseBank, .program = kNoiseProgram},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = kNoiseInstrumentKey},
        .name = "DSP Noise",
    });
    for (const u8 rate : noiseRates) {
      const u8 key = static_cast<u8>(kNoiseOutputKeyBase + rate);
      noise.region(noiseSamples[rate], Region{.keyRange = KeyRange{.low = key, .high = key},
                                              .unityKey = static_cast<double>(key),
                                              .envelope = neutral});
    }
  }

  if (!drums.empty()) {
    auto kit = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = kDrumBank, .program = kDrumProgram},
        .name = "Percussion",
    });
    for (const Drum& drum : drums) {
      if (drum.noise()) {
        kit.region(noiseSamples[drum.noiseRate()],
                   Region{.keyRange = KeyRange{.low = drum.index, .high = drum.index},
                          .unityKey = static_cast<double>(drum.index),
                          .envelope = neutral});
        continue;
      }
      const auto sample = samples.findSrcn(drum.srcn);
      const auto scale = tuning(reader, layout, drum.srcn);
      if (!sample || !scale) {
        continue;
      }
      Region region{
          .keyRange = KeyRange{.low = drum.index, .high = drum.index},
          .unityKey = unityKey(layout.version, *scale) + drum.index - drum.sourceKey,
          .envelope = neutral,
      };
      kit.region(*sample, std::move(region));
    }
  }
  return bank;
}

}  // namespace vgmtrans::formats::namco_snes
