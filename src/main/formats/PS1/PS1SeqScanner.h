/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "formats/PS1/Vab.h"
#include "PS1Seq.h"
#include "Scanner.h"

#include <vector>

class PS1SeqScanner : public VGMScanner {
 public:
  explicit PS1SeqScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
  std::vector<PS1Seq *> searchForPS1Seq(RawFile *file);
  std::vector<Vab *> searchForVab(RawFile *file);
};
