/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
*/

#pragma once

#include "Matcher.h"
#include <list>

class VGMSeq;
class VGMInstrSet;
class VGMSampColl;

// ****************
// FilegroupMatcher
// ****************

// FilegroupMatcher handles formats where the only method of associating sequences, instrument sets,
// and sample collections is that they are loaded together within the same RawFile. When loading is
// complete, FilegroupMatcher processes the VGMFiles in the order they were added and groups them
// into collections: thus, it assumes association by order. FilegroupMatcher does not retain
// state between loads, so it will never create a collection that spans multiple RawFiles, with the
// exception that FilegroupMatcher processes a psf file and all of its psflib dependencies together
// as a single load.

class FilegroupMatcher : public Matcher {
public:
  explicit FilegroupMatcher(Format *format);

  void onFinishedScan(RawFile *rawfile) override;

protected:
  bool onNewSeq(VGMSeq *seq) override;
  bool onNewInstrSet(VGMInstrSet *instrset) override;
  bool onNewSampColl(VGMSampColl *sampcoll) override;

  bool onCloseSeq(VGMSeq *seq) override;
  bool onCloseInstrSet(VGMInstrSet *instrset) override;
  bool onCloseSampColl(VGMSampColl *sampcoll) override;

  virtual void lookForMatch();

protected:
  std::list<VGMSeq *> seqs;
  std::list<VGMInstrSet *> instrsets;
  std::list<VGMSampColl *> sampcolls;
};
