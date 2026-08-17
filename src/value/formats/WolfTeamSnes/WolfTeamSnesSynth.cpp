/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/WolfTeamSnes/WolfTeamSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::formats::wolf_team_snes {

using namespace core;

namespace {

struct Patch {
  u8 program = 0;
  u8 srcn = 0;
  u8 patchIndex = 0;
  u8 pitch = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
  u8 volume = 0;
  u8 tuning = 0;
  SourceRange patchSource;
  SourceRange mapSource;
  SourceRange volumeSource;
};

[[nodiscard]] s16 signedByte(u8 value) {
  return value < 0x80 ? value : static_cast<s16>(value) - 0x100;
}

[[nodiscard]] double attenuation(double gain) {
  return gain <= 0.0 ? 100.0 : std::max(0.0, -20.0 * std::log10(gain));
}

[[nodiscard]] double lateUnityKey(const Patch& patch) {
  const double fine = signedByte(static_cast<u8>(patch.tuning - 0x40)) / 64.0;
  const double pitchTableOffset = 12.0 * std::log2(4286.0 / 4096.0);
  return 72.0 - (signedByte(patch.pitch) + fine + pitchTableOffset);
}

[[nodiscard]] double segmentedUnityKey(const Patch& patch, const Layout& layout) {
  if (layout.middleSegmented()) {
    return 60.0;
  }
  const s16 coarse = signedByte(static_cast<u8>(patch.pitch + layout.instruments.globalPitchBase));
  const double tableBase = 12.0 * std::log2(0x10be / 4096.0);
  return 60.0 - coarse - tableBase;
}

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout) {
  std::vector<Patch> result;
  result.reserve(layout.instruments.count);
  for (u32 program = 0; program < layout.instruments.count; ++program) {
    u8 patchIndex = static_cast<u8>(program);
    SourceRange mapSource;
    if (layout.instruments.patchMapAddress) {
      const u32 map = *layout.instruments.patchMapAddress + program;
      if (!reader.has(map, 1)) {
        continue;
      }
      patchIndex = reader.u8At(map);
      mapSource = reader.range(map, 1);
    }
    const u32 patch = layout.instruments.patchTableAddress + patchIndex * layout.instruments.entrySize;
    if (!reader.has(patch, layout.instruments.entrySize)) {
      continue;
    }
    Patch info{
        .program = static_cast<u8>(program),
        .srcn = static_cast<u8>(program),
        .patchIndex = patchIndex,
        .pitch = reader.u8At(patch),
        .adsr1 = reader.u8At(patch + 1),
        .adsr2 = reader.u8At(patch + 2),
        .gain = layout.segmented() ? reader.u8At(patch + 3) : u8{0xb8},
        .tuning = layout.segmented() ? u8{0x40} : reader.u8At(patch + 3),
        .patchSource = reader.range(patch, layout.instruments.entrySize),
        .mapSource = mapSource,
    };
    if (layout.instruments.volumeTableAddress) {
      const u32 volume = *layout.instruments.volumeTableAddress + program;
      if (!reader.has(volume, 1)) {
        continue;
      }
      info.volume = reader.u8At(volume);
      info.volumeSource = reader.range(volume, 1);
    }
    result.push_back(info);
  }
  return result;
}

[[nodiscard]] std::vector<u8> referencedSrcns(const std::vector<Patch>& patches) {
  std::vector<u8> result;
  result.reserve(patches.size());
  for (const Patch& patch : patches) {
    result.push_back(patch.srcn);
  }
  return result;
}

void addInstruments(InstrumentSetBuilder& instruments, const std::vector<Patch>& patches,
                    const SnesBrrSampleRefs& samples, const Layout& layout) {
  for (const Patch& patch : patches) {
    const auto sample = samples.findSrcn(patch.srcn);
    if (!sample) {
      continue;
    }
    Instrument instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = patch.program},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = patch.program},
        .name = fmt::format("Instrument {} (SRCN {})", patch.program, patch.srcn),
        .range = patch.mapSource.valid() ? patch.mapSource : patch.patchSource,
    };
    auto entry = instruments.append(std::move(instrument));
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", patch.program), patch.patchSource, "wolf-team-snes-instrument").id();
    if (patch.mapSource.valid()) {
      entry.source("SRCN Patch Map", patch.mapSource, "wolf-team-snes-patch-map")
          .description(fmt::format("Patch {}", patch.patchIndex))
          .parent(root);
    }
    entry
        .source("Pitch", SourceRange{.source = patch.patchSource.source, .offset = patch.patchSource.offset, .size = 1},
                "wolf-team-snes-instrument-pitch")
        .parent(root);
    entry
        .source(layout.segmented() ? "ADSR/Gain" : "ADSR",
                SourceRange{.source = patch.patchSource.source,
                            .offset = patch.patchSource.offset + 1,
                            .size = layout.segmented() ? 3u : 2u},
                "wolf-team-snes-instrument-envelope")
        .parent(root);
    if (!layout.segmented()) {
      entry
          .source("Fine Tuning",
                  SourceRange{.source = patch.patchSource.source, .offset = patch.patchSource.offset + 3, .size = 1},
                  "wolf-team-snes-instrument-tuning")
          .parent(root);
    }
    if (patch.volumeSource.valid()) {
      entry.source("Sample Volume", patch.volumeSource, "wolf-team-snes-instrument-volume").parent(root);
    }

    const double unityKey = layout.segmented() ? segmentedUnityKey(patch, layout) : lateUnityKey(patch);
    const double gain = layout.segmented() ? 1.0 : patch.volume / 16.0;
    entry
        .region(*sample,
                Region{
                    .range = patch.patchSource,
                    .unityKey = unityKey,
                    .envelope = snesDspEnvelope(patch.adsr1, patch.adsr2, patch.gain),
                    .attenuationDb = attenuation(gain),
                })
        .source("Region", patch.patchSource, "wolf-team-snes-region")
        .description(fmt::format("SRCN {}", patch.srcn));
  }
}

}  // namespace

std::optional<ScanSoundBankRef> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                         std::string_view displayName) {
  if (!layout.instruments.confirmed) {
    return std::nullopt;
  }
  const ByteReader reader = builder.reader();
  const std::vector<Patch> patches = collectPatches(reader, layout);
  if (patches.empty()) {
    return std::nullopt;
  }
  SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.instruments.sampleDirAddress, referencedSrcns(patches));
  const u32 minimumSampleStart =
      layout.variant == Variant::Arcus
          ? layout.instruments.sampleDirAddress + 0x100
          : (layout.middleSegmented()
                 ? layout.instruments.patchTableAddress + layout.instruments.count * layout.instruments.entrySize
                 : 1);
  std::erase_if(catalog.samples, [&](const SnesBrrSample& sample) {
    if (sample.startAddress < minimumSampleStart) {
      return true;
    }
    if (layout.segmented()) {
      return false;
    }
    const u64 sampleEnd = sample.stream.encodedData.endOffset();
    return sampleEnd > layout.instruments.sampleDirAddress || sample.loopAddress == 0xffff ||
           sample.loopAddress < sample.startAddress || sample.loopAddress >= layout.instruments.sampleDirAddress ||
           sample.loopAddress > sampleEnd || (sample.stream.loops && sample.loopAddress >= sampleEnd);
  });
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto instruments = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& samplePool = instruments.samples();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(samplePool, reader, catalog, "wolf-team-snes-sample-dir-entry");
  addInstruments(instruments.builder(), patches, samples, layout);
  if (instruments.builder().empty()) {
    return std::nullopt;
  }
  return instruments.ref();
}

}  // namespace vgmtrans::formats::wolf_team_snes
