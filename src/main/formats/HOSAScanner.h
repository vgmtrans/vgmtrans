/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Scanner.h"

class HOSASeq;
class HOSAInstrSet;
class PSXSampColl;

class HOSAScanner:
    public VGMScanner {
 public:
  HOSAScanner(void);
 public:
  virtual ~HOSAScanner(void);

  virtual void Scan(RawFile *file, void *info = 0);
  HOSASeq *SearchForHOSASeq(RawFile *file);
  HOSAInstrSet *SearchForHOSAInstrSet(RawFile *file, PSXSampColl *sampcoll);
  bool RecursiveRgnCompare(RawFile *file, int i, int sampNum, int numSamples, int numFinds, uint32_t *sampOffsets);
};



