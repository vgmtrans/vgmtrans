/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ChunSnes/ChunSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace vgmtrans::formats::chun_snes {

using namespace core;

namespace {

struct Patch {
  u8 program = 0;
  u8 global = 0;
  u8 srcn = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
  u16 pitchScale = 0;
  SourceRange mappingSource;
  SourceRange sampleInfoSource;
};

[[nodiscard]] double unityKey(const Layout& layout, u16 scale) {
  const double referenceKey = layout.version == Version::Summer ? 95.0 : 119.0;
  const double ratio = (scale / 256.0) * (layout.pitchReference / 8192.0);
  return ratio > 0.0 ? referenceKey - 12.0 * std::log2(ratio) : referenceKey;
}

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout) {
  const u32 mapping = layout.soundBankAddress + (layout.version == Version::Summer ? 1 : 2);
  const u32 count = reader.u8At(layout.soundBankAddress);
  std::vector<Patch> result;
  result.reserve(count);
  for (u32 program = 0; program < count && reader.has(mapping + program, 1); ++program) {
    const u8 global = reader.u8At(mapping + program);
    if (!reader.has(layout.srcnTableAddress + global, 1)) {
      continue;
    }
    const u8 srcn = reader.u8At(layout.srcnTableAddress + global);
    if (srcn == 0xff) {
      continue;
    }
    const u32 info = layout.sampleInfoTableAddress + srcn * 8;
    if (!reader.has(info, 8)) {
      continue;
    }
    const u16 scale = reader.be16(info + 5);
    if (scale == 0) {
      continue;
    }
    result.push_back(Patch{
        .program = static_cast<u8>(program),
        .global = global,
        .srcn = srcn,
        .adsr1 = reader.u8At(info + 2),
        .adsr2 = reader.u8At(info + 3),
        .gain = reader.u8At(info + 4),
        .pitchScale = scale,
        .mappingSource = reader.range(mapping + program, 1),
        .sampleInfoSource = reader.range(info, 8),
    });
  }
  return result;
}

[[nodiscard]] std::vector<u8> referencedSrcns(const std::vector<Patch>& patches) {
  std::vector<u8> result;
  result.reserve(patches.size());
  for (const Patch& patch : patches) {
    result.push_back(patch.srcn);
  }
  std::ranges::sort(result);
  result.erase(std::ranges::unique(result).begin(), result.end());
  return result;
}

}  // namespace

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<Patch> patches = collectPatches(reader, layout);
  if (patches.empty()) {
    return std::nullopt;
  }
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, referencedSrcns(patches));
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto instruments = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& samplePool = instruments.samples();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(samplePool, reader, catalog);
  for (const Patch& patch : patches) {
    const auto sample = samples.findSrcn(patch.srcn);
    if (!sample) {
      continue;
    }
    Instrument instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = patch.program},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = patch.program},
        .name = fmt::format("Instrument {} (global {}, SRCN {})", patch.program, patch.global, patch.srcn),
        .range = patch.mappingSource,
    };
    auto entry = instruments.builder().append(std::move(instrument));
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", patch.program), patch.mappingSource, "chun-snes-instrument").id();
    entry.source("Sample parameters", patch.sampleInfoSource, "chun-snes-sample-info").parent(root);

    Envelope envelope = snesDspEnvelope(patch.adsr1, patch.adsr2, patch.gain);
    envelope.releaseSeconds = snesDspAdsrSustainSeconds(defaultReleaseRate(layout.version));
    entry
        .region(*sample,
                Region{
                    .range = patch.sampleInfoSource,
                    .unityKey = unityKey(layout, patch.pitchScale),
                    .envelope = envelope,
                })
        .source("Region", patch.sampleInfoSource, "chun-snes-region")
        .description(fmt::format("SRCN {}, pitch scale {:#06x}", patch.srcn, patch.pitchScale));
  }

  if (instruments.builder().empty()) {
    return std::nullopt;
  }
  return instruments;
}

}  // namespace vgmtrans::formats::chun_snes
