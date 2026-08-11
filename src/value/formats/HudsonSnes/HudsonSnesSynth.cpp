/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HudsonSnes/HudsonSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace vgmtrans::formats::hudson_snes {

using namespace core;

Envelope driverEnvelope(u8 adsr1, u8 adsr2, u8 gain) {
  // Ordinary expiration uses native KOF. The shared conversion compensates
  // its short linear-amplitude fade for dB-linear SoundFont/DLS envelopes.
  return snesDspEnvelope(adsr1, adsr2, gain);
}

double driverPseudoReleaseSeconds(u8 gain) {
  const u8 mode = gain >> 5;
  if (mode == 4) {
    return snesDspGainPhysicalSeconds(gain, 0x7ff, 0);
  }
  if (mode == 5) {
    return snesDspGainEnvelopeSeconds(gain, 0x7ff, 0);
  }
  // Direct and increasing GAIN modes do not fade the voice at gate-off.
  return std::numeric_limits<double>::infinity();
}

namespace {

struct Patch {
  InstrumentRow row;
  double unityKey = 72.0;
  SourceRange tuningSource;
};

[[nodiscard]] double tuningUnityKey(ByteReader reader, u32 address) {
  const u16 rawScale = static_cast<u16>((reader.u8At(address) << 8) | reader.u8At(address + 1));
  if (rawScale == 0) {
    return 72.0;
  }
  constexpr double pitchTableCorrection = 4286.0 / 4096.0;
  const double multiplier = rawScale / 256.0;
  const double semitones = std::log2(multiplier * pitchTableCorrection) * 12.0 +
                           static_cast<s8>(reader.u8At(address + 2)) +
                           static_cast<s8>(reader.u8At(address + 3)) / 256.0;
  return 72.0 - semitones;
}

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout,
                                                const SequenceRecipes& recipes) {
  std::vector<Patch> patches;
  for (const InstrumentRow& row : recipes.instruments) {
    const u32 tuning = layout.tuningTableAddress + row.srcn * 4u;
    const auto directory = readSnesSampleDirectoryEntry(reader, layout.spcDirAddress + row.srcn * 4u, true);
    if (!reader.has(tuning, 4) || !directory || !directory->stream || !directory->loopAddressIsBlockAligned()) {
      continue;
    }
    patches.push_back(Patch{
        .row = row,
        .unityKey = tuningUnityKey(reader, tuning),
        .tuningSource = reader.range(tuning, 4),
    });
  }
  return patches;
}

[[nodiscard]] const Patch* patchForProgram(const std::vector<Patch>& patches, u8 program) {
  const auto found = std::ranges::find(patches, program, [](const Patch& patch) { return patch.row.program; });
  return found == patches.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<u8> referencedSrcns(const std::vector<Patch>& patches) {
  std::vector<u8> result;
  for (const Patch& patch : patches) {
    result.push_back(patch.row.srcn);
  }
  std::ranges::sort(result);
  result.erase(std::ranges::unique(result).begin(), result.end());
  return result;
}

void addMelodic(InstrumentSetBuilder& instruments, const std::vector<Patch>& patches,
                const SnesBrrSampleRefs& samples) {
  for (const Patch& patch : patches) {
    const auto sample = samples.findSrcn(patch.row.srcn);
    if (!sample) {
      continue;
    }
    auto entry = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = patch.row.program},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = patch.row.program},
        .name = fmt::format("Instrument {} (SRCN {})", patch.row.program, patch.row.srcn),
        .range = patch.row.source,
    });
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", patch.row.program), patch.row.source, "hudson-snes-instrument").id();
    entry.source("Sample tuning", patch.tuningSource, "hudson-snes-tuning").parent(root);
    entry
        .region(*sample,
                Region{
                    .range = patch.row.source,
                    .unityKey = patch.unityKey,
                    .envelope = driverEnvelope(patch.row.adsr1, patch.row.adsr2, patch.row.gain),
                })
        .source("Region", patch.row.source, "hudson-snes-region")
        .description(fmt::format("SRCN {}", patch.row.srcn));
  }
}

void addDrums(InstrumentSetBuilder& instruments, const SequenceRecipes& recipes, const std::vector<Patch>& patches,
              const SnesBrrSampleRefs& samples) {
  std::optional<InstrumentSetBuilder::Entry> kit;
  for (const DrumSlot& drum : recipes.drums) {
    if (drum.note > 127 - kDrumKeyBias) {
      continue;
    }
    const Patch* patch = patchForProgram(patches, drum.sourceProgram);
    const auto sample = patch ? samples.findSrcn(patch->row.srcn) : std::nullopt;
    if (!patch || !sample) {
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
    const u8 key = static_cast<u8>(drum.note + kDrumKeyBias);
    (*kit)
        .region(*sample,
                Region{
                    .keyRange = KeyRange{.low = key, .high = key},
                    .range = drum.source,
                    .unityKey = patch->unityKey + key - drum.sourceKey,
                    .envelope = driverEnvelope(patch->row.adsr1, patch->row.adsr2, patch->row.gain),
                })
        .source(fmt::format("Drum {}", drum.note), drum.source, "hudson-snes-drum-region")
        .description(
            fmt::format("Program {}, source key {}, SRCN {}", drum.sourceProgram, drum.sourceKey, patch->row.srcn));
  }
}

}  // namespace

std::optional<ScanSynthRefs> addSynth(ScanResultBuilder& builder, const Layout& layout, const SequenceRecipes& recipes,
                                      std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<Patch> patches = collectPatches(reader, layout, recipes);
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
  addMelodic(instruments.builder(), patches, samples);
  addDrums(instruments.builder(), recipes, patches, samples);
  if (instruments.builder().empty()) {
    return std::nullopt;
  }
  return ScanSynthRefs{.instruments = instruments.ref(), .samples = sampleCollection.ref()};
}

}  // namespace vgmtrans::formats::hudson_snes
