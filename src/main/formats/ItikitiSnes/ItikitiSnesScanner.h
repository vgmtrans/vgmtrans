/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include "Scanner.h"

class ItikitiSnesScanner: public VGMScanner {
 public:
  void Scan(RawFile *file, void *info) override;
  static void ScanFromApuRam(RawFile *file);
  static void ScanFromRom(RawFile *file);

 private:
  static bool ScanSongHeader(RawFile *file, uint32_t &song_header_offset);
};
