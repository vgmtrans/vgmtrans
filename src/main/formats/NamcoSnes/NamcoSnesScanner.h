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
  explicit NamcoSnesScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
  void searchForNamcoSnesFromARAM(RawFile *file);

 private:
  std::map<uint8_t, uint8_t> getInitDspRegMap(RawFile *file);

  static BytePattern ptnReadSongListBCR;
  static BytePattern ptnReadSongList;
  static BytePattern ptnStartSong;
  static BytePattern ptnLoadInstrTuning;
  static BytePattern ptnDspRegInit;
};
