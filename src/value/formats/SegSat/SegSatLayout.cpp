/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SegSat/SegSat.h"

#include <algorithm>
#include <array>
#include <limits>
#include <set>

namespace vgmtrans::formats::segsat {

using namespace core;

namespace {

[[nodiscard]] bool has(ByteReader reader, u64 offset, u64 size) {
  return offset <= reader.size() && size <= reader.size() - offset;
}

[[nodiscard]] bool validBank(ByteReader reader, u32 base, SegSatBankLayout& layout) {
  if (!has(reader, base, 10)) {
    return false;
  }

  const u16 mixes = reader.be16(base);
  const u16 velocity = reader.be16(base + 2);
  const u16 pegs = reader.be16(base + 4);
  const u16 plfo = reader.be16(base + 6);
  const u16 firstInstrument = reader.be16(base + 8);
  if ((mixes | velocity | pegs | plfo) & 1u || mixes < 10 || mixes >= 0x1000 || velocity >= 0x1000 || pegs >= 0x1000 ||
      !(mixes < velocity && velocity < pegs && pegs < plfo && plfo < firstInstrument)) {
    return false;
  }

  const u32 mixerBytes = velocity - mixes;
  const u32 velocityBytes = pegs - velocity;
  const u32 pegBytes = plfo - pegs;
  const u32 plfoBytes = firstInstrument - plfo;
  if (mixerBytes % 0x12 != 0 || velocityBytes % 0x0a != 0 || pegBytes % 0x0a != 0 || plfoBytes % 4 != 0 ||
      mixerBytes > 20 * 0x12 || (velocityBytes > 20 * 0x0a && velocityBytes != 100 * 0x0a) || pegBytes > 30 * 0x0a ||
      plfoBytes > 20 * 4) {
    return false;
  }

  const u16 instrumentCount = static_cast<u16>((mixes - 8) / 2);
  if (instrumentCount == 0 || instrumentCount > 256 || !has(reader, base + 8, instrumentCount * 2u)) {
    return false;
  }

  u32 previous = reader.be16(base + 8) - 4u;
  u32 instrumentEnd = base + firstInstrument;
  for (u32 index = 0; index < instrumentCount; ++index) {
    const u16 relative = reader.be16(base + 8 + index * 2);
    if (relative <= previous || ((relative - previous) & 0x1f) != 4 || !has(reader, base + relative, 4)) {
      return false;
    }
    const u32 regionCount = segSatRegionCount(reader.u8At(base + relative + 2));
    const u64 end = static_cast<u64>(base) + relative + 4 + regionCount * 0x20;
    if (end > reader.size()) {
      return false;
    }
    instrumentEnd = std::max(instrumentEnd, static_cast<u32>(end));
    previous = relative;
  }

  layout = SegSatBankLayout{
      .offset = base,
      .instrumentDataEnd = instrumentEnd,
      .mixerTables = mixes,
      .velocityTables = velocity,
      .pegTables = pegs,
      .plfoTables = plfo,
      .firstInstrument = firstInstrument,
      .instrumentCount = instrumentCount,
  };
  return true;
}

[[nodiscard]] std::vector<u8> referencedBanks(ByteReader reader, u32 start, u32 end) {
  std::set<u8> banks;
  u32 offset = start;
  for (u32 commands = 0; offset < end && commands < 262144; ++commands) {
    const u8 status = reader.u8At(offset);
    u32 size = 1;
    if (status <= 0x7f) {
      size = 5;
    } else if ((status & 0xf0) == 0xb0) {
      size = 4;
      if (has(reader, offset, size) && reader.u8At(offset + 1) == 32) {
        banks.insert(reader.u8At(offset + 2) & 0x7f);
      }
    } else if ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0 || (status & 0xf0) == 0xe0) {
      size = 3;
    } else if (status == 0x81) {
      size = 4;
    } else if (status == 0x82) {
      size = 2;
    } else if (status == 0x83) {
      break;
    }
    if (!has(reader, offset, size)) {
      break;
    }
    offset += size;
  }
  return {banks.begin(), banks.end()};
}

template <size_t Size>
[[nodiscard]] bool matches(ByteReader reader, u32 offset, const std::array<u8, Size>& pattern,
                           const std::array<bool, Size>& exact) {
  if (!has(reader, offset, Size)) {
    return false;
  }
  for (size_t index = 0; index < Size; ++index) {
    if (exact[index] && reader.u8At(offset + index) != pattern[index]) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::vector<SegSatBankLayout> findSegSatBanks(ByteReader reader) {
  std::vector<SegSatBankLayout> banks;
  if (reader.size() < 10 || reader.size() > std::numeric_limits<u32>::max()) {
    return banks;
  }

  const u32 end = static_cast<u32>(reader.size());
  for (u32 base = 0; base + 10 <= end;) {
    SegSatBankLayout layout;
    if (!validBank(reader, base, layout)) {
      ++base;
      continue;
    }
    banks.push_back(layout);
    // The pointer/table area cannot contain another bank header. Samples can
    // overlap later address ranges, so do not skip to the bank's sample end.
    base += std::max<u32>(layout.firstInstrument, 1);
  }

  // SSF drivers publish loaded bank number/address pairs in sound RAM. Keeping
  // this association on the discovered layout lets each sequence collection
  // select only the banks it actually references.
  for (u32 entry = 0x500; entry + 8 <= std::min<u32>(end, 0x600); entry += 8) {
    const u32 numberAndPointer = reader.be32(entry);
    const u8 bankNumber = static_cast<u8>(numberAndPointer >> 24);
    const u32 pointer = numberAndPointer & 0x00ffffff;
    if (bankNumber == 0xff || pointer >= end + 8u) {
      break;
    }
    const auto found = std::ranges::find(banks, pointer, &SegSatBankLayout::offset);
    if (found != banks.end() && !found->sourceBank) {
      found->sourceBank = bankNumber;
    }
  }
  // Raw dumps and some minimal SSFs omit the runtime map. Bank zero is the
  // driver default, but prefer an explicit map entry even for a sole bank.
  if (banks.size() == 1 && !banks.front().sourceBank) {
    banks.front().sourceBank = 0;
  }
  return banks;
}

std::vector<SegSatSequenceLayout> findSegSatSequences(ByteReader reader) {
  std::vector<SegSatSequenceLayout> sequences;
  u32 tableIndex = 0;
  if (reader.size() < 0x20 || reader.size() > std::numeric_limits<u32>::max()) {
    return sequences;
  }

  const u32 fileEnd = static_cast<u32>(reader.size());
  for (u32 table = 0; table + 0x20 < fileEnd; ++table) {
    const u32 firstWord = reader.be32(table);
    const u8 sequenceCount = static_cast<u8>(firstWord >> 16);
    if ((firstWord & 0xff00ff00u) != 0 || sequenceCount == 0) {
      continue;
    }
    const u32 tableSize = 2 + static_cast<u32>(sequenceCount) * 4;
    if (!has(reader, table, tableSize + 16) || reader.be32(table + 2) != tableSize) {
      continue;
    }

    std::vector<u32> pointers;
    pointers.reserve(sequenceCount);
    u32 previous = 0;
    bool valid = true;
    for (u32 index = 0; index < sequenceCount; ++index) {
      const u32 pointer = reader.be32(table + 2 + index * 4);
      if (pointer <= previous || !has(reader, table + pointer, 16)) {
        valid = false;
        break;
      }
      pointers.push_back(pointer);
      previous = pointer;
    }
    if (!valid) {
      continue;
    }

    size_t accepted = 0;
    for (u32 index = 0; index < sequenceCount; ++index) {
      const u32 offset = table + pointers[index];
      const u16 tempoCount = reader.be16(offset + 2);
      const u16 normal = reader.be16(offset + 4);
      const u16 tempoLoop = reader.be16(offset + 6);
      if (normal != 8u + static_cast<u32>(tempoCount) * 8 || tempoLoop >= normal || !has(reader, offset, normal + 1)) {
        valid = false;
        break;
      }
      const u32 sequenceEnd =
          index + 1 < pointers.size() ? table + pointers[index + 1] : static_cast<u32>(reader.size());
      sequences.push_back(SegSatSequenceLayout{
          .offset = offset,
          .end = sequenceEnd,
          .tableIndex = tableIndex,
          .sequenceIndex = index,
          .ppqn = reader.be16(offset),
          .tempoEventCount = tempoCount,
          .normalTrack = normal,
          .tempoLoop = tempoLoop,
          .referencedBanks = referencedBanks(reader, offset + normal, sequenceEnd),
      });
      ++accepted;
    }
    if (!valid) {
      sequences.resize(sequences.size() - accepted);
      continue;
    }

    // Valid sequence tables cannot overlap; skip their pointer table to avoid
    // recognizing its entries as a second table.
    ++tableIndex;
    table += tableSize - 1;
  }
  return sequences;
}

SegSatDriverVersion determineSegSatDriverVersion(ByteReader reader) {
  constexpr std::array<u8, 16> v128{0x78, 0x00, 0x3a, 0x3c, 0x01, 0x00, 0x10, 0x2e,
                                    0x18, 0x27, 0x02, 0x40, 0x00, 0x03, 0xd0, 0x40};
  constexpr std::array<bool, 16> v128Exact{true,  true,  true, true, true, true, true, true,
                                           false, false, true, true, true, true, true, true};
  constexpr std::array<u8, 18> v208{0x38, 0x3c, 0x00, 0x00, 0x06, 0x44, 0x01, 0x00, 0x02,
                                    0x44, 0x03, 0x00, 0x33, 0xc4, 0x00, 0x00, 0x14, 0x14};
  constexpr std::array<bool, 18> v208Exact{true, true, false, false, true, true, true, true,  true,
                                           true, true, true,  true,  true, true, true, false, false};
  constexpr std::array<u8, 14> v220{0x49, 0xee, 0x10, 0x00, 0x48, 0xe7, 0x00, 0x0c, 0x7e, 0x1f, 0x10, 0x2c, 0x00, 0x34};
  constexpr std::array<bool, 14> v220Exact{true, true, true, true, true, true, true,
                                           true, true, true, true, true, true, true};

  const u32 end = static_cast<u32>(std::min<u64>(reader.size(), std::numeric_limits<u32>::max()));
  for (u32 offset = 0; offset < end; ++offset) {
    if (matches(reader, offset, v128, v128Exact)) {
      return SegSatDriverVersion::V1_28;
    }
  }
  for (u32 offset = 0; offset < end; ++offset) {
    if (matches(reader, offset, v208, v208Exact) && has(reader, offset + 16, 2) &&
        reader.be16(offset + 16) == static_cast<u16>(offset + 2)) {
      return SegSatDriverVersion::V2_08;
    }
  }
  for (u32 offset = 0; offset < end; ++offset) {
    if (matches(reader, offset, v220, v220Exact)) {
      return SegSatDriverVersion::V2_20;
    }
  }
  return SegSatDriverVersion::V2_08;
}

}  // namespace vgmtrans::formats::segsat
