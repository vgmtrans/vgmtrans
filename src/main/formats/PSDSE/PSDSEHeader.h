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
  } else if (c0 == 's' && c1 == 'e' && c2 == 'd') {
    // [Mimi de Unou o Kitaeru: DS Chou-nouryoku]: Sound-effect event sequences use SEDL with the SMDL header, trk
    // chunks, and event bytecode, but omit the song chunk.
    kind = FileKind::Sequence;
  } else if (c0 == 's' && c1 == 'w' && c2 == 'd') {
    kind = FileKind::Bank;
  }

  if (kind == FileKind::Unknown || (c3 != 'l' && c3 != 'b')) {
    return {};
  }
  return {kind, c3 == 'b' ? Endianness::Big : Endianness::Little};
}

inline uint16_t readU16(const RawFile* file, uint32_t offset, Endianness endianness) {
  return endianness == Endianness::Big ? file->readShortBE(offset) : file->readShort(offset);
}

inline uint32_t readU32(const RawFile* file, uint32_t offset, Endianness endianness) {
  return endianness == Endianness::Big ? file->readWordBE(offset) : file->readWord(offset);
}

}  // namespace PSDSE

struct SWDLHeader {
  uint32_t offset = 0;
  uint32_t fileLength = 0;
  uint16_t version = 0;
  uint16_t id = 0;
  Endianness endianness = Endianness::Little;
  std::string intName;
  uint16_t nbwavislots = 0;
  uint16_t nbprgislots = 0;
  uint8_t nbkeygroups = 0;
  uint8_t sampleStorageKind = 0;
  uint16_t mainBankId = 0xffff;
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
    if (readOffset + 0x50 > file->size()) {
      return false;
    }

    const PSDSE::MagicInfo magic = PSDSE::magicInfo(file->readWordBE(readOffset));
    if (magic.kind != PSDSE::FileKind::Bank) {
      return false;
    }
    endianness = magic.endianness;

    fileLength = PSDSE::readU32(file, readOffset + 0x08, endianness);
    version = PSDSE::readU16(file, readOffset + 0x0C, endianness);
    id = PSDSE::readU16(file, readOffset + 0x0E, endianness);
    if (fileLength < 0x50 || fileLength > file->size() - readOffset) {
      return false;
    }

    char fname[17];
    file->readBytes(readOffset + 0x20, 16, fname);
    fname[16] = '\0';
    intName = std::string(fname);
    if (intName.empty()) {
      intName = "SWDL Audio";
    }

    const uint32_t pcmdlen = PSDSE::readU32(file, readOffset + 0x40, endianness);
    // [Professor Layton and the Diabolical Box]: External PCMD banks store 0xaaaa0101 in this field.
    // [Pokemon Mystery Dungeon: Explorers of Sky]: External PCMD banks store 0xaaaa0000 in this field.
    // The shared high halfword is the external-PCMD sentinel; the low halfword is not part of the byte count.
    hasExternalPcmd = (pcmdlen & 0xffff0000) == 0xaaaa0000;

    if (version == 0x0402) {
      nbwavislots = file->readByte(readOffset + 0x46);
      nbprgislots = file->readByte(readOffset + 0x47);
      nbkeygroups = file->readByte(readOffset + 0x48);
    } else if (version == 0x0415) {
      nbwavislots = PSDSE::readU16(file, readOffset + 0x46, endianness);
      nbprgislots = PSDSE::readU16(file, readOffset + 0x48, endianness);
      // [Pokemon Mystery Dungeon: Explorers of Sky]: DseSwd_LoadBank reads these counts as byte fields. Storage kind
      // 2 interprets +0x40 as the main waveform bank ID and requests external waveform data from the caller.
      nbkeygroups = file->readByte(readOffset + 0x4A);
      sampleStorageKind = file->readByte(readOffset + 0x4B);
      if (sampleStorageKind == 2) {
        mainBankId = PSDSE::readU16(file, readOffset + 0x40, endianness);
      }
    } else {
      return false;
    }
    hasExternalPcmd = hasExternalPcmd || sampleStorageKind == 2;

    uint32_t currentOffset = readOffset + 0x50;
    const uint32_t fileEnd = readOffset + fileLength;
    while (currentOffset < fileEnd) {
      if (currentOffset + 0x10 > fileEnd) {
        break;
      }

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

      if (chunkSize == 0 && chunkSig != 0) {
        break;
      }

      currentOffset += chunkSize + 0x10;
      // [Ni no Kuni: Shikkoku no Madoushi]: Each SWDL follows its SEDL in an NPD package. DSE chunk alignment is
      // relative to the SWDL header rather than the outer package; absolute alignment skips a following prgi chunk
      // when the embedded bank is not 16-byte aligned.
      const uint32_t relativeOffset = currentOffset - readOffset;
      if (relativeOffset % 16 != 0) {
        currentOffset += (16 - (relativeOffset % 16));
      }
      if (currentOffset > fileEnd) {
        break;
      }
    }
    return true;
  }
};
