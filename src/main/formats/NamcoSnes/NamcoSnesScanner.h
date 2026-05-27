/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/types.h"
#include "Scanner.h"
#include "byte_pattern.h"

enum NamcoSnesVersion : u8;  // see NamcoSnesFormat.h

class NamcoSnesScanner : public VGMScanner {
 public:
  explicit NamcoSnesScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
  void searchForNamcoSnesFromARAM(RawFile *file);

 private:
  std::map<u8, u8> getInitDspRegMap(RawFile *file);

  static BytePattern ptnReadSongList;
  static BytePattern ptnStartSong;
  static BytePattern ptnLoadInstrTuning;
  static BytePattern ptnDspRegInit;
};
