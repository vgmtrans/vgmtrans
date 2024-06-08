/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class HOSASeq;
class HOSAInstrSet;
class PSXSampColl;

class HOSAScanner : public VGMScanner {
 public:
  HOSAScanner();
  ~HOSAScanner() override;

  void scan(RawFile *file, void *info) override;
  static HOSASeq* searchForHOSASeq(RawFile *file);
  static HOSAInstrSet* searchForHOSAInstrSet(RawFile *file, const PSXSampColl *sampcoll);
  static bool recursiveRgnCompare(RawFile *file, int i, int sampNum, int numSamples, int numFinds, uint32_t *sampOffsets);
};
