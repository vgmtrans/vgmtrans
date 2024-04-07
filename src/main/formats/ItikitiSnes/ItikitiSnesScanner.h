#pragma once
#include "Scanner.h"

class ItikitiSnesScanner: public VGMScanner {
 public:
  ItikitiSnesScanner() {
    USE_EXTENSION("spc");
  }

  void Scan(RawFile *file, void *info) override;
  static void ScanFromApuRam(RawFile *file);
  static void ScanFromRom(RawFile *file);

 private:
  static bool ScanSongHeader(RawFile *file, uint32_t &song_header_offset);
};
