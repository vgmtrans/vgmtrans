/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
*/

#pragma once

#include "base/Types.h"
#include "SimpleMatcher.h"

class GetIdMatcher : public SimpleMatcher<u32> {
public:
  explicit GetIdMatcher(Format *format, bool bRequiresSampColl = false)
      : SimpleMatcher(format, bRequiresSampColl) {}

  bool seqId(VGMSeq *seq, u32 &id) override {
    id = seq->id();
    return (id != -1u);
  }

  bool instrSetId(VGMInstrSet *instrset, u32 &id) override {
    id = instrset->id();
    return (id != -1u);
  }

  bool sampCollId(VGMSampColl *sampcoll, u32 &id) override {
    id = sampcoll->id();
    return (id != -1u);
  }
};