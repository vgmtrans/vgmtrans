/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "PS1Seq.h"
#include "formats/PS1/Vab.h"

class PS1SeqScanner : public VGMScanner {
 public:
  PS1SeqScanner();
  virtual ~PS1SeqScanner();

  virtual void Scan(RawFile *file, void *info = 0);
  std::vector<PS1Seq *> SearchForPS1Seq(RawFile *file);
  std::vector<Vab *> SearchForVab(RawFile *file);
};
