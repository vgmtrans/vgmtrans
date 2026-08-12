/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CompileSnes/CompileSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vgmtrans::formats::compile_snes {

using namespace core;

Envelope driverEnvelope(u8 adsr1, u8 adsr2, u8 gain) {
  return snesDspEnvelope(adsr1, adsr2, gain);
}

namespace {

struct Patch {
  u8 srcn;
  s8 transpose;
  u8 pitchTable;
  double unityKey;
  SourceRange source;
};

[[nodiscard]] u16 pitchTableAddress(ByteReader reader, const Layout& layout, u8 index) {
  if (layout.early() || index == 0 || !reader.has(layout.pitchTableListAddress + index * 2u, 2)) {
    return layout.regularPitchTableAddress;
  }
  const u16 address = reader.le16(layout.pitchTableListAddress + index * 2u);
  return reader.has(address, 242) ? address : layout.regularPitchTableAddress;
}

[[nodiscard]] double unityKey(ByteReader reader, u16 table, s8 transpose) {
  u8 nearest = 1;
  u16 nearestPitch = 0;
  unsigned distance = std::numeric_limits<unsigned>::max();
  for (u32 key = 1; key <= 120 && reader.has(table + key * 2u, 2); ++key) {
    const u16 pitch = reader.le16(table + key * 2u);
    const unsigned candidate = static_cast<unsigned>(std::abs(static_cast<int>(pitch) - 0x1000));
    if (candidate < distance) {
      nearest = static_cast<u8>(key);
      nearestPitch = pitch;
      distance = candidate;
    }
  }
  const double correction = nearestPitch == 0 ? 0.0 : 12.0 * std::log2(nearestPitch / 4096.0);
  return nearest - transpose - correction;
}

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout,
                                                const ReferencedData& references) {
  std::vector<Patch> result;
  for (const u8 srcn : references.programs) {
    if (srcn >= 0x40) {
      continue;
    }
    const u32 address = layout.tuningTableAddress + srcn * (layout.early() ? 1u : 2u);
    if (!reader.has(address, layout.early() ? 1 : 2) ||
        !readSnesSampleDirectoryEntry(reader, layout.spcDirAddress + srcn * 4u, true)) {
      continue;
    }
    const s8 transpose = static_cast<s8>(reader.u8At(address));
    const u8 pitchTable = layout.early() ? 0 : reader.u8At(address + 1);
    const u16 table = pitchTableAddress(reader, layout, pitchTable);
    result.push_back(Patch{
        .srcn = srcn,
        .transpose = transpose,
        .pitchTable = pitchTable,
        .unityKey = unityKey(reader, table, transpose),
        .source = reader.range(address, layout.early() ? 1 : 2),
    });
  }
  return result;
}

[[nodiscard]] std::vector<u8> programs(const std::vector<Patch>& patches) {
  std::vector<u8> result;
  result.reserve(patches.size());
  for (const Patch& patch : patches) {
    result.push_back(patch.srcn);
  }
  return result;
}

}  // namespace

std::optional<ScanSynthRefs> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                      const ReferencedData& references, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<Patch> patches = collectPatches(reader, layout, references);
  if (patches.empty()) {
    return std::nullopt;
  }
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, programs(patches));
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto instruments = builder.instrumentSet(fmt::format("{} Instruments", displayName));
  auto sampleCollection = builder.sampleCollection(fmt::format("{} Samples", displayName));
  const SnesBrrSampleRefs samples = addSnesBrrSamples(sampleCollection.builder(), reader, catalog);
  for (const Patch& patch : patches) {
    const auto sample = samples.findSrcn(patch.srcn);
    if (!sample) {
      continue;
    }
    auto entry = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = patch.srcn},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = patch.srcn},
        .name = fmt::format("Instrument {}", patch.srcn),
        .range = patch.source,
    });
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", patch.srcn), patch.source, "compile-snes-instrument").id();
    entry.source("Tuning", patch.source, "compile-snes-tuning")
        .parent(root)
        .description(fmt::format("transpose {}, pitch table {}", patch.transpose, patch.pitchTable));
    entry
        .region(*sample,
                Region{
                    .range = patch.source,
                    .unityKey = patch.unityKey,
                    // Compile owns ADSR in track state; this is merely a safe
                    // fallback for consumers that ignore dynamic overrides.
                    .envelope = driverEnvelope(0x8f, 0xe0),
                })
        .source("Region", patch.source, "compile-snes-region")
        .description(fmt::format("SRCN {}, unity key {:.3f}", patch.srcn, patch.unityKey));
  }
  return ScanSynthRefs{.instruments = instruments.ref(), .samples = sampleCollection.ref()};
}

}  // namespace vgmtrans::formats::compile_snes
