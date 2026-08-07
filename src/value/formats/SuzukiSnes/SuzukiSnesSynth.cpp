/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SuzukiSnes/SuzukiSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace vgmtrans::formats::suzuki_snes {

using namespace core;

namespace {

constexpr u32 kProgramCount = 128;
constexpr u8 kSrcnCount = 64;

struct Patch {
  u8 program = 0;
  u8 srcn = 0;
  u8 volume = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  s16 tuning = 0;
  SourceRange programSource;
  SourceRange volumeSource;
  SourceRange adsrSource;
  SourceRange tuningSource;
};

[[nodiscard]] double attenuation(u8 volume) {
  const double gain = volume / 128.0;
  return gain == 0.0 ? 100.0 : std::max(0.0, -20.0 * std::log10(gain));
}

[[nodiscard]] double unityKey(s16 tuning) {
  // The table is a signed little-endian 8.8 semitone offset. The driver adds
  // it to note pitch before looking up the DSP ratio; source note 69 is unity
  // when the table entry is zero.
  return 69.0 - tuning / 256.0;
}

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout) {
  std::vector<Patch> result;
  result.reserve(kProgramCount);
  for (u32 program = 0; program < kProgramCount; ++program) {
    const u32 programAddress = layout.srcnTableAddress + program;
    if (!reader.has(programAddress, 1)) {
      break;
    }
    const u8 srcn = reader.u8At(programAddress);
    if (srcn >= kSrcnCount) {
      continue;
    }

    const u32 volumeAddress = layout.volumeTableAddress + srcn * 2;
    const u32 adsrAddress = layout.adsrTableAddress + srcn * 2;
    const u32 tuningAddress = layout.tuningTableAddress + srcn * 2;
    if (!reader.has(volumeAddress, 1) || !reader.has(adsrAddress, 2) || !reader.has(tuningAddress, 2)) {
      continue;
    }
    const u8 volume = reader.u8At(volumeAddress);
    const u16 adsr = reader.le16(adsrAddress);
    const u16 rawTuning = reader.le16(tuningAddress);
    const auto directory = readSnesSampleDirectoryEntry(reader, layout.spcDirAddress + srcn * 4, true);
    // Zero ADSR and $ffff tuning are unused-slot sentinels in these tables.
    // Skip only the affected program: programs are an indirection table, so a
    // hole in one SRCN must not hide later valid mappings.
    if (volume > 0x7f || adsr == 0 || rawTuning == 0xffff || !directory || !directory->stream ||
        directory->startAddress < layout.spcDirAddress) {
      continue;
    }
    result.push_back(Patch{
        .program = static_cast<u8>(program),
        .srcn = srcn,
        .volume = volume,
        .adsr1 = static_cast<u8>(adsr),
        .adsr2 = static_cast<u8>(adsr >> 8),
        .tuning = static_cast<s16>(rawTuning),
        .programSource = reader.range(programAddress, 1),
        .volumeSource = reader.range(volumeAddress, 1),
        .adsrSource = reader.range(adsrAddress, 2),
        .tuningSource = reader.range(tuningAddress, 2),
    });
  }
  return result;
}

[[nodiscard]] const Patch* patchForProgram(const std::vector<Patch>& patches, u8 program) {
  const auto found = std::ranges::find(patches, program, &Patch::program);
  return found == patches.end() ? nullptr : &*found;
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

void addMelodicInstruments(InstrumentSetBuilder& instruments, const std::vector<Patch>& patches,
                           const SnesBrrSampleRefs& samples) {
  for (const Patch& patch : patches) {
    const auto sample = samples.findSrcn(patch.srcn);
    if (!sample) {
      continue;
    }
    Instrument instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = patch.program},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = patch.program},
        .name = fmt::format("Instrument {} (SRCN {})", patch.program, patch.srcn),
        .range = patch.programSource,
    };
    auto entry = instruments.append(std::move(instrument));
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", patch.program), patch.programSource, "suzuki-snes-instrument").id();
    entry.source("Volume", patch.volumeSource, "suzuki-snes-instrument-volume").parent(root);
    entry.source("ADSR", patch.adsrSource, "suzuki-snes-instrument-adsr").parent(root);
    entry.source("Tuning", patch.tuningSource, "suzuki-snes-instrument-tuning").parent(root);

    entry
        .region(*sample, Region{
                             .range = patch.programSource,
                             .unityKey = unityKey(patch.tuning),
                             .envelope = snesDspEnvelope(patch.adsr1, patch.adsr2, 0),
                             .attenuationDb = attenuation(patch.volume),
                         })
        .source("Region", patch.programSource, "suzuki-snes-region")
        .description(fmt::format("SRCN {}", patch.srcn));
  }
}

void addDrumKit(InstrumentSetBuilder& instruments, const SequenceRecipes& recipes, const std::vector<Patch>& patches,
                const SnesBrrSampleRefs& samples) {
  std::optional<InstrumentSetBuilder::Entry> kit;
  for (const DrumSlot& drum : recipes.drums) {
    if (drum.note > 127 - kDrumKeyBias || drum.volume > 0x7f) {
      instruments.warning("Drum row has an out-of-range key or volume", drum.source);
      continue;
    }
    const Patch* patch = patchForProgram(patches, drum.sourceProgram);
    const auto sample = patch ? samples.findSrcn(patch->srcn) : std::nullopt;
    if (!patch || !sample) {
      instruments.warning("Drum row refers to an unavailable source instrument", drum.source);
      continue;
    }
    if (!kit) {
      kit = instruments.append(Instrument{
          .explicitAddress = InstrumentAddress{.bank = 0x7f, .program = 0},
          .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = kDrumKitKey},
          .name = "Drum Kit",
          .range = drum.source,
      });
    }

    const u8 outputKey = static_cast<u8>(drum.note + kDrumKeyBias);
    const double root = unityKey(patch->tuning) + outputKey - drum.sourceKey;
    (*kit)
        .region(*sample, Region{
                             .keyRange = KeyRange{.low = outputKey, .high = outputKey},
                             .range = drum.source,
                             .unityKey = root,
                             .envelope = snesDspEnvelope(patch->adsr1, patch->adsr2, 0),
                             .pan = drum.pan / 256.0,
                             .attenuationDb = attenuation(drum.volume),
                         })
        .source(fmt::format("Drum {}", drum.note), drum.source, "suzuki-snes-drum-region")
        .description(fmt::format("Program {}, source key {}, SRCN {}", drum.sourceProgram, drum.sourceKey,
                                 patch->srcn));
  }
}

}  // namespace

std::optional<ScanSynthRefs> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                      const SequenceRecipes& recipes, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<Patch> patches = collectPatches(reader, layout);
  if (patches.empty()) {
    return std::nullopt;
  }
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, referencedSrcns(patches));
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto instruments = builder.instrumentSet(fmt::format("{} Instruments", displayName));
  auto sampleCollection = builder.sampleCollection(fmt::format("{} Samples", displayName));
  const SnesBrrSampleRefs samples = addSnesBrrSamples(sampleCollection.builder(), reader, catalog);
  addMelodicInstruments(instruments.builder(), patches, samples);
  addDrumKit(instruments.builder(), recipes, patches, samples);
  if (instruments.builder().empty()) {
    return std::nullopt;
  }
  return ScanSynthRefs{.instruments = instruments.ref(), .samples = sampleCollection.ref()};
}

}  // namespace vgmtrans::formats::suzuki_snes
