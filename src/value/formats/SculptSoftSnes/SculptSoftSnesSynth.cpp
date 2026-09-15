/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SculptSoftSnes/SculptSoftSnes.h"
#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vgmtrans::formats::sculpt_soft_snes {

using namespace core;

namespace {

[[nodiscard]] std::optional<u16> tableEntry(ByteReader reader, const Layout& layout, u8 table, u8 index) {
  const u16 base = reader.le16(layout.tables + table);
  u32 end = kAramSize;
  for (u8 offset : {2, 4, 6, 8, 10, 12, 14, 16, 26}) {
    const u16 next = reader.le16(layout.tables + offset);
    if (next > base) {
      end = std::min(end, u32(next));
    }
  }
  const u32 slot = base + 2u * index;
  if (base < 0x200 || slot + 2 > end || !reader.has(slot, 2)) {
    return std::nullopt;
  }
  const u16 address = reader.le16(slot);
  return address >= 0x200 && address != 0xffff ? std::optional{address} : std::nullopt;
}

[[nodiscard]] std::optional<Curve> readCurve(ByteReader reader, u16 address, u8 width) {
  if (!reader.has(address, 8) || reader.u8At(address + 6) != width) {
    return std::nullopt;
  }
  const u8 count = reader.u8At(address + 5);
  const u8 loopStart = reader.u8At(address);
  const u8 loopEnd = reader.u8At(address + 1);
  // Some authored curves put release/loop points beyond the nominal end.
  // The SPC follows those indexes literally, so retain the reachable tail.
  const u32 storedCount = loopEnd == 0xff ? count : std::max({u32(count), u32(loopStart) + 1, u32(loopEnd) + 1});
  const u32 size = 8u + storedCount * width;
  if (count == 0 || !reader.has(address, size)) {
    return std::nullopt;
  }
  Curve curve{
      .range = reader.range(address, size),
      .loopStart = loopStart,
      .loopEnd = loopEnd,
      .releaseLead = reader.u8At(address + 2),
      .speed = reader.u8At(address + 3),
      .interpolate = reader.u8At(address + 4) != 0,
      .alternate = reader.u8At(address + 7) != 0,
      .pointCount = count,
  };
  for (u32 i = 0; i < storedCount; ++i) {
    const u32 at = address + 8 + i * width;
    curve.points.push_back(width == 1 ? reader.u8At(at) : reader.le16(at));
  }
  return curve;
}

}  // namespace

DriverData readDriverData(ByteReader reader, const Layout& layout) {
  DriverData data;
  const u16 basePitch = reader.u8At(layout.pitchTable) | (reader.u8At(layout.pitchTable + 240) << 8);
  data.pitchBaseKey = 72.0 + 12.0 * std::log2(std::max(1u, unsigned(basePitch)) / 4096.0);
  for (u32 i = 0; i < 32; ++i) {
    data.deltas[i] =
        static_cast<s16>(reader.u8At(layout.deltaTable + i) | (reader.u8At(layout.deltaTable + 32 + i) << 8));
  }
  for (u32 i = 0; i < 256; ++i) {
    for (u8 lane = 0; lane < 4; ++lane) {
      if (const auto address = tableEntry(reader, layout, 4 + lane * 2, static_cast<u8>(i))) {
        data.curves[lane][i] = readCurve(reader, *address, lane == 1 ? 2 : 1);
      }
    }
    if (const auto address = tableEntry(reader, layout, 12, static_cast<u8>(i)); address && reader.has(*address, 12)) {
      std::array<u8, 12> echo{};
      for (u32 byte = 0; byte < echo.size(); ++byte) {
        echo[byte] = reader.u8At(*address + byte);
      }
      data.echoes[i] = echo;
    }
    const auto address = tableEntry(reader, layout, 2, static_cast<u8>(i));
    if (!address || !reader.has(*address, 6)) {
      continue;
    }
    Patch patch{.range = reader.range(*address, 6),
                .flags = reader.u8At(*address),
                .gain = reader.u8At(*address + 1),
                .sample = reader.u8At(*address + 2),
                .pitch = reader.u8At(*address + 3),
                .pan = reader.u8At(*address + 4),
                .echo = reader.u8At(*address + 5)};
    data.patches[i] = patch;
  }
  // Sample curves can have a higher table index than the referencing patch.
  for (const auto& patch : data.patches) {
    if (!patch) {
      continue;
    }
    if ((patch->flags & 2) == 0) {
      data.samples.insert(patch->sample);
    } else if (const auto& curve = data.curves[3][patch->sample]) {
      for (const u16 sample : curve->points) {
        data.samples.insert(static_cast<u8>(sample));
      }
    }
  }
  for (const u8 sample : data.samples) {
    const u32 slot = layout.directory + sample * 4u;
    if (reader.has(slot, 4)) {
      const u16 start = reader.le16(slot);
      if (start >= 3 && start != 0xffff) {
        data.sampleTuning[sample] = static_cast<s16>(reader.le16(start - 3));
        data.sampleFlags[sample] = reader.u8At(start - 1);
      }
    }
  }
  std::erase_if(data.samples, [&](u8 sample) {
    const u32 slot = layout.directory + sample * 4u;
    return !reader.has(slot, 4) || reader.le16(slot) < 0x203 || reader.le16(slot) == 0xffff;
  });
  return data;
}

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout, const DriverData& data,
                                           const std::set<u8>& programs, std::string_view name) {
  const auto reader = builder.reader();
  std::set<u8> referenced;
  for (const u8 program : programs) {
    if (const auto& patch = data.patches[program]) {
      if ((patch->flags & 2) == 0) {
        referenced.insert(patch->sample);
      } else if (const auto& curve = data.curves[3][patch->sample]) {
        for (const u16 sample : curve->points) {
          referenced.insert(static_cast<u8>(sample));
        }
      }
    }
  }
  std::vector<u8> srcns;
  for (const u8 sample : referenced) {
    if (data.samples.contains(sample)) {
      srcns.push_back(sample);
    }
  }
  const auto catalog = readSnesBrrCatalog(reader, layout.directory, srcns, [](u8 sample) { return sample; });
  const bool hasNoise = std::ranges::any_of(srcns, [&](u8 sample) { return (data.sampleFlags[sample] & 0x80) != 0; });
  if (catalog.empty() && !hasNoise) {
    return std::nullopt;
  }
  auto bank = builder.soundBank(fmt::format("{} Instruments", name));
  const auto samples = addSnesBrrSamples(bank.localSamples(), reader, catalog);
  for (const u8 srcn : srcns) {
    auto sample = samples.findSrcn(srcn);
    if ((data.sampleFlags[srcn] & 0x80) != 0) {
      sample = bank.localSamples()
                   .add(0x100u + srcn, Sample{.name = fmt::format("Noise {}", data.sampleFlags[srcn] & 31),
                                              .codec = AudioCodec::SnesDspNoise,
                                              .encodedData = reader.range(0, 0),
                                              .sampleRate = kSnesDspSampleRate,
                                              .loop = Loop{.enabled = true, .length = kSnesDspNoiseSampleCount},
                                              .codecParameter = u64(data.sampleFlags[srcn] & 31)})
                   .ref();
    }
    if (!sample) {
      continue;
    }
    const u16 start = reader.le16(layout.directory + srcn * 4u);
    const auto range = reader.range(start - 3, 3);
    auto instrument = bank.instruments().append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = srcn},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = srcn},
        .name = fmt::format("Sample {}", srcn),
        .range = range,
    });
    instrument.source("Sample parameters", range, "sculpt-soft-snes-sample")
        .description(fmt::format("Pitch offset {}/20 semitones; flags ${:02X}", data.sampleTuning[srcn],
                                 data.sampleFlags[srcn]));
    // The sequence performs the sample tuning and software GAIN envelopes.
    instrument.region(*sample, Region{.unityKey = 72.0,
                                      .envelope = Envelope{.attackSeconds = 0.0,
                                                           .decaySeconds = std::numeric_limits<double>::infinity(),
                                                           .releaseSeconds = 0.0,
                                                           .sustainAmplitude = 1.0}});
  }
  constexpr std::array<std::string_view, 4> laneNames{"GAIN", "Pitch", "Pan", "Sample"};
  const std::array<u8, 4> flags{1, 4, 8, 2};
  for (const u8 program : programs) {
    const auto& patch = data.patches[program];
    if (!patch) {
      continue;
    }
    auto root = bank.instruments().source(SourceRole::TableEntry, fmt::format("Patch {}", program), patch->range,
                                          "sculpt-soft-snes-patch");
    root.description(fmt::format("Flags ${:02X}, gain {}, sample {}, pitch {}, pan {}, echo {}", patch->flags,
                                 patch->gain, patch->sample, patch->pitch, patch->pan, patch->echo));
    const std::array<u8, 4> indices{patch->gain, patch->pitch, patch->pan, patch->sample};
    for (u8 lane = 0; lane < 4; ++lane) {
      const auto& curve = data.curves[lane][indices[lane]];
      if ((patch->flags & flags[lane]) != 0 && curve) {
        bank.instruments()
            .source(SourceRole::Table, laneNames[lane], curve->range, "sculpt-soft-snes-curve")
            .parent(root.id())
            .description(fmt::format("{} points, interval {}, interpolation {}, loop {}–{}, release lead {}",
                                     curve->pointCount, curve->speed, curve->interpolate, curve->loopStart,
                                     curve->loopEnd, curve->releaseLead));
      }
    }
    if ((patch->flags & 0x10) != 0 && data.echoes[patch->echo]) {
      if (const auto address = tableEntry(reader, layout, 12, patch->echo)) {
        bank.instruments()
            .source(SourceRole::TableEntry, "Echo / FIR", reader.range(*address, 12), "sculpt-soft-snes-echo")
            .parent(root.id());
      }
    }
  }
  return bank;
}

}  // namespace vgmtrans::formats::sculpt_soft_snes
