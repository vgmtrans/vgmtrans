/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "Scanner.h"
#include "BytePattern.h"

class KonamiTMNT2Seq;
class MAMEGame;

enum KonamiTMNT2FormatVer: uint8_t {
  VERSION_UNDEFINED,
  TMNT2,
  SSRIDERS
};

class KonamiTMNT2Scanner : public VGMScanner {
 public:
  explicit KonamiTMNT2Scanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile *file, void *info) override;
private:
  static BytePattern ptn_tmnt2_LoadSeqTable;
  static BytePattern ptn_simp_LoadSeqTable;
  static BytePattern ptn_moomesa_LoadSeqTable;
  static BytePattern ptn_tmnt2_LoadInstrTable;
};
