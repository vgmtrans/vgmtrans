/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "SPCFile.h"

#include "base/Types.h"
#include "LogManager.h"
#include "RawFile.h"
#include "VGMTag.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {

u32 parseOptionalID666Number(std::string value, std::string_view fieldName) {
  auto isWhitespace = [](unsigned char c) { return std::isspace(c) != 0; };
  value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isWhitespace));
  value.erase(std::find_if_not(value.rbegin(), value.rend(), isWhitespace).base(), value.end());

  if (value.empty()) {
    return 0;
  }

  const bool isDecimal = std::ranges::all_of(value, [](unsigned char c) { return std::isdigit(c) != 0; });
  if (!isDecimal) {
    L_WARN("Ignored malformed {} in ID666 tag: {}", fieldName, value);
    return 0;
  }

  try {
    return static_cast<u32>(std::stoul(value));
  } catch (const std::exception& e) {
    L_WARN("Failed to parse {} from ID666 tag: {}", fieldName, e.what());
    return 0;
  }
}

}  // namespace

VGMTag SPCFile::tagFromSPCFile(const SPCFile& spc) {
  const auto& id666 = spc.id666Tag();
  const auto& dsp = spc.dsp();

  auto tag = VGMTag(id666.songTitle, id666.artist, id666.gameTitle, id666.comments);
  tag.binaries["dsp"] = std::vector(dsp.begin(), dsp.end());
  return tag;
}

SPCFile::SPCFile(const RawFile& file) {
  if (file.size() < 0x10180) {
    throw std::length_error("SPC file is smaller than required length");
  }

  if (!std::equal(file.begin(), file.begin() + 27, "SNES-SPC700 Sound File Data") || file.readShort(0x21) != 0x1a1a) {
    throw std::runtime_error("Invalid SPC signature");
  }

  m_hasID666Tag = file.readByte(0x23) == 0x1a;
  m_versionMinor = file.readByte(0x24);
  file.readBytes(0x100, m_ram.size(), m_ram.data());
  file.readBytes(0x10100, m_dspRegisters.size(), m_dspRegisters.data());
  file.readBytes(0x101C0, m_extraRam.size(), m_extraRam.data());

  if (m_hasID666Tag) {
    loadID666Tag(file);
  }
  if (file.size() >= 0x10208) {
    loadExtendedID666Tag(file, 0x10200);
  }
}

void SPCFile::loadID666Tag(const RawFile& file) {
  m_id666Tag.songTitle = file.readNullTerminatedString(0x2E, 32);
  m_id666Tag.gameTitle = file.readNullTerminatedString(0x4E, 32);
  m_id666Tag.nameOfDumper = file.readNullTerminatedString(0x6E, 16);
  m_id666Tag.comments = file.readNullTerminatedString(0x7E, 32);
  m_id666Tag.dateDumped = file.readNullTerminatedString(0x9E, 11);
  m_id666Tag.secondsToPlay = parseOptionalID666Number(file.readNullTerminatedString(0xA9, 3), "seconds to play");
  m_id666Tag.fadeLength = parseOptionalID666Number(file.readNullTerminatedString(0xAC, 5), "fade length");
  m_id666Tag.artist = file.readNullTerminatedString(0xB1, 32);
  m_id666Tag.defaultChannelDisables = file.readByte(0xD1);
  m_id666Tag.emulatorUsed = file.readByte(0xD2);
}

void SPCFile::loadExtendedID666Tag(const RawFile& file, size_t offset) {
  if (!std::equal(file.begin() + offset, file.begin() + offset + 4, "xid6")) {
    return;  // No extended ID666 chunk found
  }

  size_t chunkSize = file.readWord(offset + 4);
  size_t chunkEnd = offset + 8 + chunkSize;

  if (file.size() < chunkEnd) {
    throw std::length_error("Extended ID666 chunk size exceeds file size");
  }

  size_t currentOffset = offset + 8;
  while (currentOffset < chunkEnd) {
    u8 id = file.readByte(currentOffset);
    u8 type = file.readByte(currentOffset + 1);
    u16 headerData = file.readShort(currentOffset + 2);

    switch (type) {
      case 0:
        // Data stored in last two bytes of 4 byte header
        currentOffset += 4;
        break;

      case 1: {
        // Data is stored after the header. The two header data bytes are length.
        size_t dataLength = headerData;
        switch (id) {
          case 0x01:
            m_id666Tag.songTitle = file.readNullTerminatedString(currentOffset + 4, dataLength);
            break;
          case 0x02:
            m_id666Tag.gameTitle = file.readNullTerminatedString(currentOffset + 4, dataLength);
            break;
          case 0x03:
            m_id666Tag.artist = file.readNullTerminatedString(currentOffset + 4, dataLength);
            break;
          case 0x04:
            m_id666Tag.nameOfDumper = file.readNullTerminatedString(currentOffset + 4, dataLength);
            break;
          case 0x05:
            m_id666Tag.dateDumped = std::to_string(file.readWord(currentOffset + 4));
            break;
          case 0x06:
            m_id666Tag.emulatorUsed = file.readByte(currentOffset + 4);
            break;
          case 0x07:
            m_id666Tag.comments = file.readNullTerminatedString(currentOffset + 4, dataLength);
            break;
          default:
            L_INFO("Ignored id6 tag id: {} in SPC", id);
            break;
        }
        currentOffset += 4 + ((dataLength + 3) & ~3);  // Align to 32-bit boundary
        break;
      }

      case 4:
        // Integer. Data stored after header as int32.
        currentOffset += 8;
        break;

      default:
        L_WARN("Unknown xid type: {:d} in SPC", type);
        currentOffset += 4;
        break;
    }
  }
}
