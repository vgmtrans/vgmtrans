/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
*/

#pragma once

#include "SimpleMatcher.h"

class FilenameMatcher : public SimpleMatcher<std::string> {
public:
  explicit FilenameMatcher(Format *format, bool bRequiresSampColl = false)
      : SimpleMatcher(format, bRequiresSampColl) {}

  bool seqId(VGMSeq *seq, std::string &id) override {
    RawFile *rawfile = seq->rawFile();
    id = rawfile->path().string();
    return !id.empty();
  }

  bool instrSetId(VGMInstrSet *instrset, std::string &id) override {
    RawFile *rawfile = instrset->rawFile();
    id = rawfile->path().string();
    return !id.empty();
  }

  bool sampCollId(VGMSampColl *sampcoll, std::string &id) override {
    RawFile *rawfile = sampcoll->rawFile();
    id = rawfile->path().string();
    return !id.empty();
  }
};
