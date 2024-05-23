/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum HeartBeatSnesVersion : uint8_t;  // see HeartBeatSnesFormat.h

class HeartBeatSnesScanner : public VGMScanner {
 public:
  void Scan(RawFile *file, void *info) override;
  static void SearchForHeartBeatSnesFromARAM(RawFile *file);

 private:
  static BytePattern ptnReadSongList;
  static BytePattern ptnSetDIR;
  static BytePattern ptnLoadSRCN;
  static BytePattern ptnSaveSeqHeaderAddress;
};
