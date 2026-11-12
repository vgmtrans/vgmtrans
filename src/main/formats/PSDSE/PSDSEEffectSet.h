#pragma once

#include "PSDSEHeader.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace PSDSE {

struct EffectSequence {
  uint32_t offset = 0;
  uint32_t length = 0;
  uint16_t version = 0;
  uint16_t number = 0;
  Endianness endianness = Endianness::Little;
  std::string sourceName;
};

struct EffectSetHeader {
  std::vector<EffectSequence> effects;

  bool read(const RawFile* file, uint32_t offset) {
    effects.clear();
    if (offset > file->size() || file->size() - offset < 0x40) {
      return false;
    }
    const uint32_t magic = file->readWordBE(offset) | 0x20202020;
    if (magic != 0x7365646c && magic != 0x73656462) {
      return false;
    }
    const auto endian = magicInfo(magic).endianness;
    const uint32_t length = readU32(file, offset + 8, endian);
    const uint16_t version = readU16(file, offset + 0x0c, endian);
    if (length < 0x40 || length > file->size() - offset ||
        length > std::numeric_limits<uint32_t>::max() - offset || (version != 0x0402 && version != 0x0415)) {
      return false;
    }

    // [Line Attack Heroes]: SsdGetEffectInformation uses the signed count at +0x30 as an exclusive bound.
    // With +0x35 clear, its halfword table begins at +0x40 and points relative to the SEDB header.
    const uint16_t slots = readU16(file, offset + 0x30, endian);
    if (slots > 0x7fff) {
      return false;
    }
    uint32_t table = offset + 0x40;
    uint32_t base = offset;
    uint32_t end = offset + length;
    if (file->readByte(offset + 0x35) != 0) {
      // [Pokemon Mystery Dungeon: Explorers of Sky]: The seq chunk contains both the table and its targets;
      // offsets are relative to the chunk payload, not to the SEDL header. BNKL and MCRL are separate chunks.
      // [Line Attack Heroes]: SsdAddEffectData selects this same layout when +0x35 is nonzero.
      bool found = false;
      while (table <= end && end - table >= 0x10) {
        const uint32_t tag = file->readWordBE(table);
        const uint32_t size = readU32(file, table + 0x0c, endian);
        const uint32_t alignment = file->readByte(table + 8);
        if (size > end - table - 0x10) {
          return false;
        }
        if (tag == 0x73657120) {
          table += 0x10;
          base = table;
          end = table + size;
          found = true;
          break;
        }
        if (tag == 0x656f6420 || alignment == 0 || (alignment & (alignment - 1)) != 0) {
          return false;
        }
        const uint64_t step = (static_cast<uint64_t>(size) + 0x10 + alignment - 1) &
                              ~static_cast<uint64_t>(alignment - 1);
        if (step > end - table) {
          return false;
        }
        table += static_cast<uint32_t>(step);
      }
      if (!found) {
        return false;
      }
    }
    if (static_cast<uint32_t>(slots) * 2 > end - table) {
      return false;
    }
    const uint32_t firstRecord = table + static_cast<uint32_t>(slots) * 2;
    const uint32_t infoSize = version == 0x0402 ? 0x10 : 0x30;
    std::vector<uint32_t> offsets(slots);
    std::vector<uint32_t> boundaries{end};
    for (uint32_t slot = 0; slot < slots; ++slot) {
      const uint16_t relative = readU16(file, table + slot * 2, endian);
      if (relative == 0) {
        continue;
      }
      const uint64_t record = static_cast<uint64_t>(base) + relative;
      if (record < firstRecord || record > end || infoSize > end - record) {
        return false;
      }
      offsets[slot] = static_cast<uint32_t>(record);
      boundaries.push_back(offsets[slot]);
    }
    std::ranges::sort(boundaries);
    const auto sourceName = file->readNullTerminatedString(offset + 0x20, 16);
    for (uint16_t slot = 0; slot < slots; ++slot) {
      if (offsets[slot] == 0) {
        continue;
      }
      const uint32_t recordEnd = *std::upper_bound(boundaries.begin(), boundaries.end(), offsets[slot]);
      if (recordEnd - offsets[slot] < infoSize) {
        effects.clear();
        return false;
      }
      // [Disaster: Day of Crisis]: RAY_CF_M00.SED has nonzero pointers to empty effects consisting only of
      // seqinfo and EOC. These have no tracks to export and do not need separate sequence objects.
      const uint32_t trackCountOffset = version == 0x0402 ? 4 : 6;
      if (file->readByte(offsets[slot] + trackCountOffset) == 0 &&
          recordEnd - offsets[slot] >= infoSize + 4 &&
          file->readWordBE(offsets[slot] + infoSize) == 0x656f6320) {
        continue;
      }
      effects.push_back({offsets[slot], recordEnd - offsets[slot], version, slot, endian, sourceName});
    }
    return true;
  }
};

}  // namespace PSDSE
