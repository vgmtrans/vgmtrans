/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace vgmtrans::core {

class SourceMapBuilder;
struct SampleCollection;

struct SnesBrrStream {
  SourceRange encodedData;
  bool loops = false;
};

[[nodiscard]] std::optional<SnesBrrStream> inspectSnesBrrStream(ByteReader reader, u32 startAddress);

struct SnesSampleDirectoryEntry {
  u8 index = 0;
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

// Shared decoded view used by SNES formats. Collection order follows SRCN;
// canonicalIndex() resolves aliases that point at the same BRR stream.
struct SnesBrrCatalog {
  std::vector<SnesBrrSample> samples;
  SourceRange directoryRange;

  [[nodiscard]] std::optional<u32> index(u8 srcn) const;
  [[nodiscard]] std::optional<u32> firstIndexStartingAt(u32 address) const;
  [[nodiscard]] std::optional<u32> canonicalIndex(u8 srcn) const;
};

[[nodiscard]] std::optional<SnesSampleDirectoryEntry> readSnesSampleDirectoryEntry(ByteReader reader, u32 entryAddress,
                                                                                   bool inspectStream = true);

class SnesSampleDirectory {
public:
  SnesSampleDirectory(ByteReader reader, u32 baseAddress) : reader_(reader), baseAddress_(baseAddress) {}

  [[nodiscard]] std::optional<SnesSampleDirectoryEntry> entry(u8 index, bool inspectStream = true) const;

private:
  ByteReader reader_;
  u32 baseAddress_ = 0;
};

[[nodiscard]] SnesBrrCatalog readSnesBrrCatalog(ByteReader reader, u32 directoryAddress,
                                                std::span<const u8> referencedSrcns);

// Build the neutral samples and their standard DIR/BRR source annotations.
[[nodiscard]] SampleCollection buildSnesBrrSampleCollection(
    ByteReader reader, const SnesBrrCatalog& catalog, AssetId sampleCollectionId, SourceMapBuilder& sourceMap,
    std::string_view directoryEntryKind = "snes-sample-dir-entry");

}  // namespace vgmtrans::core
