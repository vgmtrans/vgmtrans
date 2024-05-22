/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "HeartBeatPS1Seq.h"
#include "formats/PS1/Vab.h"

class HeartBeatPS1Scanner : public VGMScanner {
 public:
  HeartBeatPS1Scanner();
  ~HeartBeatPS1Scanner() override;

  void Scan(RawFile *file, void *info) override;
  static std::vector<VGMFile *> SearchForHeartBeatPS1VGMFile(RawFile *file);
};
