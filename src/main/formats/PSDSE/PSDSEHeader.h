#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "base/Binary.h"
#include "RawFile.h"
#include "LogManager.h"

namespace PSDSE {

enum class FileKind { Unknown, Sequence, Bank };

struct MagicInfo {
  FileKind kind = FileKind::Unknown;
  Endianness endianness = Endianness::Little;
};

inline MagicInfo magicInfo(uint32_t magic) {
  const uint8_t c0 = static_cast<uint8_t>(magic >> 24) | 0x20;
  const uint8_t c1 = static_cast<uint8_t>(magic >> 16) | 0x20;
  const uint8_t c2 = static_cast<uint8_t>(magic >> 8) | 0x20;
  const uint8_t c3 = static_cast<uint8_t>(magic) | 0x20;

  FileKind kind = FileKind::Unknown;
  if (c0 == 's' && c1 == 'm' && c2 == 'd') {
    kind = FileKind::Sequence;
  } else if (c0 == 's' && c1 == 'w' && c2 == 'd') {
    kind = FileKind::Bank;
  }

  if (kind == FileKind::Unknown || (c3 != 'l' && c3 != 'b')) {
    return {};
  }
  return {kind, c3 == 'b' ? Endianness::Big : Endianness::Little};
}

inline uint16_t readU16(const RawFile *file, uint32_t offset, Endianness endianness) {
  return endianness == Endianness::Big ? file->readShortBE(offset) : file->readShort(offset);
}

inline uint32_t readU32(const RawFile *file, uint32_t offset, Endianness endianness) {
  return endianness == Endianness::Big ? file->readWordBE(offset) : file->readWord(offset);
}

}  // namespace PSDSE

struct SWDLHeader {
  uint32_t offset = 0;
  uint32_t fileLength = 0;
  uint16_t version = 0;
  Endianness endianness = Endianness::Little;
  std::string intName;
  uint16_t nbwavislots = 0;
  uint16_t nbprgislots = 0;
  bool hasExternalPcmd = false;

  uint32_t waviOffset = 0;
  uint32_t waviSize = 0;
  uint32_t pcmdOffset = 0;
  uint32_t pcmdSize = 0;
  uint32_t prgiOffset = 0;
  uint32_t prgiSize = 0;
  uint32_t kgrpOffset = 0;
  uint32_t kgrpSize = 0;

  bool read(const RawFile* file, uint32_t readOffset) {
    this->offset = readOffset;
    if (readOffset + 0x50 > file->size())
      return false;

    const PSDSE::MagicInfo magic = PSDSE::magicInfo(file->readWordBE(readOffset));
    if (magic.kind != PSDSE::FileKind::Bank)
      return false;
    endianness = magic.endianness;

    fileLength = PSDSE::readU32(file, readOffset + 0x08, endianness);
    version = PSDSE::readU16(file, readOffset + 0x0C, endianness);
    if (fileLength < 0x50 || fileLength > file->size() - readOffset)
      return false;

    char fname[17];
    file->readBytes(readOffset + 0x20, 16, fname);
    fname[16] = '\0';
    intName = std::string(fname);
    if (intName.empty()) {
      intName = "SWDL Audio";
    }

    uint32_t pcmdlen = PSDSE::readU32(file, readOffset + 0x40, endianness);
    hasExternalPcmd = (pcmdlen == 0xAAAA0000);

    if (version == 0x0402) {
      nbwavislots = file->readByte(readOffset + 0x46);
      nbprgislots = file->readByte(readOffset + 0x47);
    } else if (version == 0x0415) {
      nbwavislots = PSDSE::readU16(file, readOffset + 0x46, endianness);
      nbprgislots = PSDSE::readU16(file, readOffset + 0x48, endianness);
    } else {
      return false;
    }

    uint32_t currentOffset = readOffset + 0x50;
    const uint32_t fileEnd = readOffset + fileLength;
    while (currentOffset < fileEnd) {
      if (currentOffset + 0x10 > fileEnd)
        break;

      uint32_t chunkSig = file->readWordBE(currentOffset);
      uint32_t chunkSize = PSDSE::readU32(file, currentOffset + 0x0C, endianness);

      if (chunkSig == 0x70726769) {  // "prgi"
        prgiOffset = currentOffset;
        prgiSize = chunkSize;
      } else if (chunkSig == 0x77617669) {  // "wavi"
        waviOffset = currentOffset;
        waviSize = chunkSize;
      } else if (chunkSig == 0x70636d64) {  // "pcmd"
        pcmdOffset = currentOffset;
        pcmdSize = chunkSize;
      } else if (chunkSig == 0x6b677270) {  // "kgrp"
        kgrpOffset = currentOffset;
        kgrpSize = chunkSize;
      } else if (chunkSig == 0x656f6420) {  // "eod "
        break;
      }

      if (chunkSize == 0 && chunkSig != 0)
        break;

      currentOffset += chunkSize + 0x10;
      if (currentOffset % 16 != 0) {
        currentOffset += (16 - (currentOffset % 16));
      }
      if (currentOffset > fileEnd)
        break;
    }
    return true;
  }
};
