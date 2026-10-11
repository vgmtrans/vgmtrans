/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/OceanSnes/OceanSnes.h"
#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>
#include <algorithm>
#include <bitset>
#include <cmath>
#include <limits>

namespace vgmtrans::formats::ocean_snes {
using namespace core;

namespace {

// There is no encoded instrument count. The pointer table ends at its first
// forward record. Do not turn following instrument bytes into extra programs
// (notably AFV's Game Over selects one such invalid program).
u32 pointerTableEnd(ByteReader reader, u16 table, u32 limit) {
  u32 end = std::min({limit, u32(table) + 256, kAramSize});
  for (u32 at = table; at + 2 <= end; at += 2) {
    const u16 pointer = reader.le16(at);
    if (pointer > table && pointer < end) {
      end = pointer;
    }
  }
  return end;
}

}  // namespace

DriverData readDriverData(ByteReader reader, const Layout& layout, std::vector<Diagnostic>& diagnostics) {
  DriverData data;
  const auto warn = [&](std::string message, SourceRange range) {
    diagnostics.push_back({.severity = Severity::Warning, .message = std::move(message), .range = range});
  };
  const u32 instrumentEnd =
      pointerTableEnd(reader, layout.instruments, layout.samples > layout.instruments ? layout.samples : kAramSize);
  const u32 sampleEnd = pointerTableEnd(reader, layout.samples, kAramSize);
  std::bitset<128> referencedSamples;
  for (u32 i = 0; layout.instruments + i * 2 + 2 <= instrumentEnd; ++i) {
    const u32 address = reader.le16(layout.instruments + i * 2);
    if (address < 0x100) {
      data.retainedInstruments.set(i);
      continue;
    }
    if (address < instrumentEnd || !reader.has(address, 3)) {
      continue;
    }
    const u8 count = reader.u8At(address);
    const u32 size = 3u + count * 23u;
    if (count == 0 || !reader.has(address, size) || (layout.samples > address && address + size > layout.samples)) {
      warn(fmt::format("OceanSnes instrument {} has an invalid region array", i), reader.range(address, 3));
      continue;
    }
    InstrumentData instrument{.range = reader.range(address, size),
                              .volume = reader.u8At(address + 1),
                              .transpose = reader.u8At(address + 2)};
    for (u32 j = 0; j < count; ++j) {
      const u32 r = address + 3 + j * 23;
      InstrumentRegion region{.range = reader.range(r, 23),
                              .low = reader.u8At(r),
                              .high = reader.u8At(r + 1),
                              .sample = reader.u8At(r + 2),
                              .attack = reader.u8At(r + 3),
                              .decay = reader.u8At(r + 4),
                              .sustain = reader.u8At(r + 5),
                              .release = reader.u8At(r + 6),
                              .sustainTicks = reader.u8At(r + 7),
                              .echo = reader.u8At(r + 8),
                              .volume = reader.u8At(r + 9),
                              .transpose = reader.u8At(r + 10),
                              .tuning = reader.le16(r + 11),
                              .pan = reader.u8At(r + 13),
                              .vibratoDelay = reader.le16(r + 14),
                              .vibratoDepth = reader.u8At(r + 16),
                              .vibratoRate = reader.u8At(r + 17),
                              .bendScale = reader.u8At(r + 18),
                              .lfo = reader.u8At(r + 19),
                              .pitchModulation = reader.u8At(r + 20),
                              .alternateBank = reader.u8At(r + 21),
                              .surround = reader.u8At(r + 22)};
      if (region.low > region.high || region.high < 0x80 || region.low > 0xbd || region.sample >= 128) {
        warn(fmt::format("OceanSnes instrument {} has an invalid key range or sample", i), region.range);
        continue;
      }
      referencedSamples.set(region.sample);
      instrument.regions.push_back(region);
    }
    data.instruments[i] = std::move(instrument);
  }
  for (u32 i = 0; i < 128; ++i) {
    if (!referencedSamples.test(i)) {
      continue;
    }
    const u32 slot = layout.samples + i * 2;
    if (slot + 2 > sampleEnd) {
      warn(fmt::format("OceanSnes sample {} is outside the sample pointer table", i), reader.range(layout.samples, 2));
      continue;
    }
    const u32 header = reader.le16(slot);
    if (header < sampleEnd || !reader.has(header, 13)) {
      warn(fmt::format("OceanSnes sample {} has an invalid header", i), reader.range(slot, 2));
      continue;
    }
    const auto stream = inspectSnesBrrStream(reader, header + 4);
    if (!stream) {
      warn(fmt::format("OceanSnes sample {} has no BRR end block", i), reader.range(header, 4));
      continue;
    }
    SampleData sample{.header = reader.range(header, 4), .brr = stream->encodedData, .tuning = reader.le16(header)};
    const u32 loopBytes = reader.le16(header + 2);
    if (stream->loops) {
      if (loopBytes % 9 == 0 && loopBytes < sample.brr.size) {
        sample.loop = {.enabled = true,
                       .start = (loopBytes / 9) * 16,
                       .length = static_cast<u32>((sample.brr.size - loopBytes) / 9) * 16};
      } else {
        warn(fmt::format("OceanSnes sample {} continues outside its first BRR segment; exporting the first segment", i),
             sample.header);
      }
    }
    data.samples[i] = sample;
  }
  for (auto& instrument : data.instruments) {
    if (!instrument) {
      continue;
    }
    for (auto& region : instrument->regions) {
      if (const auto& sample = data.samples[region.sample]) {
        const u16 tuning = static_cast<u16>(sample->tuning + region.tuning);
        // Encoded $81 is displayed as C1 (MIDI 24). Anchor tuning at table
        // index 48; the sequence retains the table's integer pitch rounding.
        const double pitch = reader.le16(layout.pitchTable + 96) * tuning / 256.0;
        region.unityKey = 72.0 - 12.0 * std::log2(std::max(1.0, pitch) / 4096.0) -
                          static_cast<s8>(instrument->transpose) - static_cast<s8>(region.transpose);
      }
    }
  }
  return data;
}

Envelope regionEnvelope(const InstrumentRegion& region) {
  const s16 threshold = static_cast<s16>(std::clamp<int>(region.sustain * 16 - 1, 0, 0x7ff));
  return Envelope{.attackSeconds = snesDspGainPhysicalSeconds(region.attack | 0xc0, 0, 0x7ff),
                  .holdSeconds = 0.0,
                  .decaySeconds = region.sustain == 0x7f
                                      ? std::numeric_limits<double>::infinity()
                                      : snesDspGainEnvelopeSeconds(region.decay | 0xa0, 0x7ff, threshold),
                  .releaseSeconds = snesDspGainEnvelopeSeconds((region.release & 31) | 0xa0, 0x7ff, 0),
                  .sustainAmplitude = region.sustain == 0x7f ? 1.0 : threshold / 2047.0};
}

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout, const DriverData& data,
                                           std::string_view name) {
  if (std::ranges::none_of(data.samples, [](const auto& sample) { return sample.has_value(); })) {
    return std::nullopt;
  }
  const auto reader = builder.reader();
  auto bank = builder.soundBank(fmt::format("{} Instruments", name));
  auto& samples = bank.localSamples();
  for (u32 i = 0; i < data.samples.size(); ++i) {
    if (!data.samples[i]) {
      continue;
    }
    const auto& sample = *data.samples[i];
    auto entry = samples.add(i, Sample{.name = fmt::format("Sample {}", i),
                                       .codec = AudioCodec::SnesBrr,
                                       .encodedData = sample.brr,
                                       .sampleRate = 32000,
                                       .loop = sample.loop});
    auto header = entry.source("Sample Header", sample.header, "ocean-snes-sample").fieldsAsChildren();
    header.field("Tuning", reader.range(sample.header.offset, 2), sample.tuning, SourceValueDisplay::Hex)
        .field("Loop Offset", reader.range(sample.header.offset + 2, 2), reader.le16(sample.header.offset + 2),
               SourceValueDisplay::Hex);
    entry.source("BRR Data", sample.brr, "snes-brr-payload").role(SourceRole::Payload).parent(header.id());
  }
  constexpr std::array<std::string_view, 23> fields{"Lowest Note",
                                                    "Highest Note",
                                                    "Sample",
                                                    "GAIN Attack",
                                                    "GAIN Decay",
                                                    "Sustain Threshold",
                                                    "GAIN Release",
                                                    "Sustain Ticks",
                                                    "Echo",
                                                    "Volume",
                                                    "Transpose",
                                                    "Tuning Low",
                                                    "Tuning High",
                                                    "Pan",
                                                    "Vibrato Delay Low",
                                                    "Vibrato Delay High",
                                                    "Vibrato Depth",
                                                    "Vibrato Rate",
                                                    "Bend Sensitivity",
                                                    "Global LFO Target",
                                                    "Pitch Modulation",
                                                    "Alternate SFX Bank",
                                                    "Surround"};
  for (u32 i = 0; i < data.instruments.size(); ++i) {
    if (!data.instruments[i]) {
      continue;
    }
    const auto& instrument = *data.instruments[i];
    auto entry = bank.instruments().append(
        Instrument{.identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = i},
                   .reverb = 0.0,
                   .name = fmt::format("Instrument {}", i),
                   .range = instrument.range});
    auto header = entry.source("Instrument", instrument.range, "ocean-snes-instrument").fieldsAsChildren();
    header.field("Region Count", reader.range(instrument.range.offset, 1), reader.u8At(instrument.range.offset))
        .field(layout.version == Version::Waterworld ? "Volume Cap" : "Volume",
               reader.range(instrument.range.offset + 1, 1), instrument.volume)
        .field("Transpose", reader.range(instrument.range.offset + 2, 1), static_cast<s8>(instrument.transpose));
    std::bitset<128> covered;
    for (const auto& region : instrument.regions) {
      auto annotation =
          entry.source("Region", region.range, "ocean-snes-region").parent(header.id()).fieldsAsChildren();
      for (u32 byte = 0; byte < fields.size(); ++byte) {
        annotation.field(fields[byte], reader.range(region.range.offset + byte, 1),
                         reader.u8At(region.range.offset + byte), SourceValueDisplay::Hex);
      }
      const auto sample = samples.find(region.sample);
      const int low = std::max<int>(region.low, 0x80) - 0x81 + 24;
      const int high = std::min<int>(region.high, 0xbd) - 0x81 + 24;
      // Region matching is ordered, so overlapping source zones must not layer.
      for (int key = low; key <= high;) {
        if (covered.test(key)) {
          ++key;
          continue;
        }
        const int first = key;
        while (key <= high && !covered.test(key)) {
          covered.set(key++);
        }
        if (!sample.empty()) {
          entry
              .region(Region{.keyRange = {static_cast<u8>(first), static_cast<u8>(key - 1)},
                             .sample = sample,
                             .range = region.range,
                             .unityKey = region.unityKey,
                             .envelope = regionEnvelope(region)})
              .source("Playable Region", region.range, "ocean-snes-playable-region")
              .parent(annotation.id());
        }
      }
    }
  }
  return bank;
}

}  // namespace vgmtrans::formats::ocean_snes
