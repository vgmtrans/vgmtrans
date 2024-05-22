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

  void Scan(RawFile *file, void *info) override;
  static HOSASeq *SearchForHOSASeq(RawFile *file);
  static HOSAInstrSet *SearchForHOSAInstrSet(RawFile *file, const PSXSampColl *sampcoll);
  static bool RecursiveRgnCompare(RawFile *file, int i, int sampNum, int numSamples, int numFinds, uint32_t *sampOffsets);
};
