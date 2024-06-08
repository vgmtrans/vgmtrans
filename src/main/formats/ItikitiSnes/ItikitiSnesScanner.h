/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include "common.h"
#include "Scanner.h"

class ItikitiSnesScanner: public VGMScanner {
 public:
  void scan(RawFile *file, void *info) override;
  static void scanFromApuRam(RawFile *file);
  static void scanFromRom(RawFile *file);

 private:
  static bool scanSongHeader(RawFile *file, uint32_t &song_header_offset);
};
