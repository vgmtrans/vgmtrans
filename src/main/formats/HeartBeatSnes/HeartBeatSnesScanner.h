/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "BytePattern.h"
#include "Scanner.h"
enum HeartBeatSnesVersion : uint8_t;  // see HeartBeatSnesFormat.h

class HeartBeatSnesScanner : public VGMScanner {
 public:
  explicit HeartBeatSnesScanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile *file, void *info) override;
  static void searchForHeartBeatSnesFromARAM(RawFile *file);

 private:
  static BytePattern ptnReadSongList;
  static BytePattern ptnSetDIR;
  static BytePattern ptnLoadSRCN;
  static BytePattern ptnSaveSeqHeaderAddress;
};
