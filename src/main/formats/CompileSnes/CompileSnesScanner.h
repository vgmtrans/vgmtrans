/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/types.h"
#include "Scanner.h"
#include "byte_pattern.h"

enum CompileSnesVersion : u8;  // see CompileSnesFormat.h

class CompileSnesScanner : public VGMScanner {
 public:
  explicit CompileSnesScanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile *file, void *info) override;
  static void searchForCompileSnesFromARAM(RawFile *file);

 private:
  static BytePattern ptnSetSongListAddress;
};
