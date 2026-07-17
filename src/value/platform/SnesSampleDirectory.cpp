/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/platform/SnesSampleDirectory.h"

namespace vgmtrans::core {

std::optional<SnesBrrStream> inspectSnesBrrStream(ByteReader reader, u32 startAddress) {
  u32 offset = startAddress;
  while (reader.has(offset, 9)) {
    const u8 header = reader.u8At(offset);
    offset += 9;
    if ((header & 1) != 0) {
      return SnesBrrStream{
          .encodedData = reader.range(startAddress, offset - startAddress),
          .loops = (header & 2) != 0,
      };
    }
  }
  return std::nullopt;
}

std::optional<SnesSampleDirectoryEntry> SnesSampleDirectory::entry(u8 index, bool inspectStream) const {
  const u32 entryAddress = baseAddress_ + static_cast<u32>(index) * 4;
  if (!reader_.has(entryAddress, 4)) {
    return std::nullopt;
  }

  SnesSampleDirectoryEntry result{
      .index = index,
      .entryRange = reader_.range(entryAddress, 4),
      .startAddress = reader_.le16(entryAddress),
      .loopAddress = reader_.le16(entryAddress + 2),
  };
  if (result.loopAddress < result.startAddress || !reader_.has(result.startAddress, 9)) {
    return std::nullopt;
  }
  if (!inspectStream) {
    return result;
  }

  result.stream = inspectSnesBrrStream(reader_, result.startAddress);
  if (!result.stream) {
    return std::nullopt;
  }
  if (result.stream->loops && result.loopAddress >= result.stream->encodedData.endOffset()) {
    return std::nullopt;
  }
  return result;
}

}  // namespace vgmtrans::core
