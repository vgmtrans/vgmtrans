/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "formats/PS1/Vab.h"
#include "HeartBeatPS1Seq.h"
#include "Scanner.h"
class HeartBeatPS1Scanner : public VGMScanner {
 public:
  explicit HeartBeatPS1Scanner(Format* format) : VGMScanner(format) {}
  ~HeartBeatPS1Scanner() override = default;;

  void scan(RawFile *file, void *info) override;
  static std::vector<VGMFile *> searchForHeartBeatPS1VGMFile(RawFile *file);
};
