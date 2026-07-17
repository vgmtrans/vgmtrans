/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"

#include <optional>

namespace vgmtrans::core {

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

class SnesSampleDirectory {
public:
  SnesSampleDirectory(ByteReader reader, u32 baseAddress) : reader_(reader), baseAddress_(baseAddress) {}

  [[nodiscard]] std::optional<SnesSampleDirectoryEntry> entry(u8 index, bool inspectStream = true) const;

private:
  ByteReader reader_;
  u32 baseAddress_ = 0;
};

}  // namespace vgmtrans::core
