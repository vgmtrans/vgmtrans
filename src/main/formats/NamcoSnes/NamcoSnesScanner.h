/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum NamcoSnesVersion : uint8_t;  // see NamcoSnesFormat.h

class NamcoSnesScanner : public VGMScanner {
 public:
  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForNamcoSnesFromARAM(RawFile *file);
  void SearchForNamcoSnesFromROM(RawFile *file);

 private:
  std::map<uint8_t, uint8_t> GetInitDspRegMap(RawFile *file);

  static BytePattern ptnReadSongList;
  static BytePattern ptnStartSong;
  static BytePattern ptnLoadInstrTuning;
  static BytePattern ptnDspRegInit;
};
