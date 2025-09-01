/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "common.h"
#include "Scanner.h"
#include "BytePattern.h"
#include <vector>

class SegSatSeq;
class SegSatInstrSet;

enum SegSatDriverVer: uint8_t {
  Unknown,
  V1_28,
  V2_08,
  V2_20,
};

class SegSatScanner : public VGMScanner {
 public:
  explicit SegSatScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
  SegSatDriverVer determineVersion(RawFile* file);
  void handleSsfFile(RawFile* file);
  std::vector<SegSatSeq *> searchForSeqs(RawFile *file, bool useMatcher);
  bool validateBankAt(RawFile *file, u32 base);
  std::vector<SegSatInstrSet *> searchForInstrSets(RawFile *file, SegSatDriverVer& ver, bool useMatcher);

private:
  static BytePattern ptn_v1_28_handle_8_slots_per_irq;
  static BytePattern ptn_v2_08_self_modifying_8_slots_per_irq;
  static BytePattern ptn_v2_20_handle_all_slots_per_irq;
};
