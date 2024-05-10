/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum ChunSnesVersion : uint8_t;  // see ChunSnesFormat.h

class ChunSnesScanner : public VGMScanner {
 public:
  void Scan(RawFile *file, void *info) override;
  static void SearchForChunSnesFromARAM(RawFile *file);
  static void SearchForChunSnesFromROM(RawFile *file);

 private:
  static BytePattern ptnLoadSeqSummerV2;
  static BytePattern ptnLoadSeqWinterV1V2;
  static BytePattern ptnLoadSeqWinterV3;
  static BytePattern ptnSaveSongIndexSummerV2;
  static BytePattern ptnDSPInitTable;
  static BytePattern ptnProgChangeVCmdSummer;
  static BytePattern ptnProgChangeVCmdWinter;
};
