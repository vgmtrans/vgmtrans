/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/synth/SynthBuilder.h"

#include <array>
#include <bitset>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::core {

class SourceMapBuilder;
struct SamplePool;

struct SnesBrrStream {
  SourceRange encodedData;
  bool loops = false;
};

[[nodiscard]] std::optional<SnesBrrStream> inspectSnesBrrStream(ByteReader reader, u32 startAddress);

struct SnesSampleDirectoryEntry {
  SourceRange entryRange;
  u16 startAddress = 0;
  u16 loopAddress = 0;
  std::optional<SnesBrrStream> stream;

  [[nodiscard]] bool loopAddressIsBlockAligned() const noexcept {
    return loopAddress >= startAddress && ((loopAddress - startAddress) % 9) == 0;
  }
};

// A fully validated BRR sample referenced through an SPC sample directory.
struct SnesBrrSample {
  u8 srcn = 0;
  SourceRange directoryEntry;
  u16 startAddress = 0;
  u16 loopAddress = 0;
  SnesBrrStream stream;
};

[[nodiscard]] std::optional<SnesSampleDirectoryEntry> readSnesSampleDirectoryEntry(ByteReader reader, u32 entryAddress,
                                                                                   bool inspectStream = true);

// Accept sample-number ranges or instrument records with an SRCN projection.
// Byte-sized membership determines the validated catalog's unique SRCN order.
template <std::ranges::input_range Entries, class Srcn = std::identity>
[[nodiscard]] std::vector<SnesBrrSample> readSnesBrrCatalog(ByteReader reader, u32 directoryAddress, Entries&& entries,
                                                            Srcn srcn = {}) {
  std::bitset<256> referenced;
  for (const auto& entry : entries) {
    const u8 number = std::invoke(srcn, entry);
    referenced.set(number);
  }
  if (referenced.none()) {
    return {};
  }
  std::vector<SnesBrrSample> catalog;
  for (u32 number = 0; number < referenced.size(); ++number) {
    if (!referenced.test(number)) {
      continue;
    }
    const auto entry = readSnesSampleDirectoryEntry(reader, directoryAddress + number * 4);
    if (!entry) {
      continue;
    }
    catalog.push_back(SnesBrrSample{
        .srcn = static_cast<u8>(number),
        .directoryEntry = entry->entryRange,
        .startAddress = entry->startAddress,
        .loopAddress = entry->loopAddress,
        .stream = *entry->stream,
    });
  }
  return catalog;
}

[[nodiscard]] inline std::vector<SnesBrrSample> readSnesBrrCatalog(ByteReader reader, u32 directoryAddress,
                                                                 std::vector<u8> srcns) {
  return readSnesBrrCatalog<std::vector<u8>&>(reader, directoryAddress, srcns);
}

// Concrete references created while adding an SNES catalog. SRCNs with
// equivalent BRR data and loop behavior resolve to the first matching sample.
class SnesBrrSampleRefs {
public:
  // Returns an empty reference for an absent or invalid catalog entry.
  [[nodiscard]] SampleRef findSrcn(u8 srcn) const;

private:
  friend SnesBrrSampleRefs addSnesBrrSamples(SamplePoolBuilder&, ByteReader, std::span<const SnesBrrSample>,
                                             std::span<const u8>, std::string_view);

  // SRCNs are byte-sized source keys, independent of dense sample indexes.
  std::array<SampleRef, 256> samples_{};
};

// Populate the generic sample builder from validated samples, with the standard
// DIR/payload source structure used by SNES formats.
[[nodiscard]] SnesBrrSampleRefs addSnesBrrSamples(SamplePoolBuilder& samples, ByteReader reader,
                                                  std::span<const SnesBrrSample> catalog,
                                                  std::string_view directoryEntryKind = "snes-sample-dir-entry");
// A non-empty usedSrcns span labels every other catalog entry as unused.
[[nodiscard]] SnesBrrSampleRefs addSnesBrrSamples(SamplePoolBuilder& samples, ByteReader reader,
                                                  std::span<const SnesBrrSample> catalog, std::span<const u8> usedSrcns,
                                                  std::string_view directoryEntryKind = "snes-sample-dir-entry");

}  // namespace vgmtrans::core
