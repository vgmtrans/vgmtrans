/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class TriAcePS1Seq;
class TriAcePS1InstrSet;

class TriAcePS1Scanner : public VGMScanner {
 public:
  TriAcePS1Scanner();
  ~TriAcePS1Scanner();

  virtual void scan(RawFile *file, void *info = 0);
  void searchForSLZSeq(RawFile *file);
  void searchForInstrSet(RawFile *file, std::vector<TriAcePS1InstrSet *> &instrsets);
  TriAcePS1Seq *decompressTriAceSLZFile(RawFile *file, uint32_t cfOff);
};
