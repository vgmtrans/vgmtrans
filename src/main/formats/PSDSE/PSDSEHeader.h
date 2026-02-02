#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "RawFile.h"
#include "LogManager.h"

struct SWDLHeader {
  uint32_t offset = 0;
  uint16_t version = 0;
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

    uint32_t sig = file->readWordBE(readOffset);
    if (sig != 0x7377646C)  // "swdl"
      return false;

    version = file->readShort(readOffset + 0x0C);

    char fname[17];
    file->readBytes(readOffset + 0x20, 16, fname);
    fname[16] = '\0';
    intName = std::string(fname);
    if (intName.empty()) {
      intName = "SWDL Audio";
    }

    uint32_t pcmdlen = file->readWord(readOffset + 0x40);
    hasExternalPcmd = (pcmdlen == 0xAAAA0000);

    if (version == 0x0402) {
      nbwavislots = file->readByte(readOffset + 0x46);
      nbprgislots = file->readByte(readOffset + 0x47);
    } else if (version == 0x0415) {
      nbwavislots = file->readShort(readOffset + 0x46);
      nbprgislots = file->readShort(readOffset + 0x48);
    } else {
      return false;
    }

    uint32_t currentOffset = readOffset + 0x50;
    while (currentOffset < file->size()) {
      if (currentOffset + 0x10 > file->size())
        break;

      uint32_t chunkSig = file->readWord(currentOffset);
      uint32_t chunkSize = file->readWord(currentOffset + 0x0C);

      if (chunkSig == 0x69677270) {  // "prgi"
        prgiOffset = currentOffset;
        prgiSize = chunkSize;
      } else if (chunkSig == 0x69766177) {  // "wavi"
        waviOffset = currentOffset;
        waviSize = chunkSize;
      } else if (chunkSig == 0x646d6370) {  // "pcmd"
        pcmdOffset = currentOffset;
        pcmdSize = chunkSize;
      } else if (chunkSig == 0x7072676b) {  // "kgrp"
        kgrpOffset = currentOffset;
        kgrpSize = chunkSize;
      } else if (chunkSig == 0x20646f65) {  // "eod "
        break;
      }

      if (chunkSize == 0 && chunkSig != 0)
        break;

      currentOffset += chunkSize + 0x10;
      if (currentOffset % 16 != 0) {
        currentOffset += (16 - (currentOffset % 16));
      }
    }
    return true;
  }
};
