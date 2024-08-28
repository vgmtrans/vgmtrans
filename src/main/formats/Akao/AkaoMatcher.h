/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
*/
#pragma once
#include "Matcher.h"
#include <vector>
#include <unordered_map>

class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class AkaoSeq;
class AkaoInstrSet;
class AkaoSampColl;
class RawFile;

class AkaoMatcher : public Matcher {
 public:
  explicit AkaoMatcher(Format *format)
    : Matcher(format) {}
  ~AkaoMatcher() override = default;

  void onFinishedScan(RawFile* rawfile) override;

 protected:
  bool onNewSeq(VGMSeq* seq) override;
  bool onNewInstrSet(VGMInstrSet* instrSet) override;
  bool onNewSampColl(VGMSampColl* sampColl) override;
  bool onCloseSeq(VGMSeq* seq) override;
  bool onCloseInstrSet(VGMInstrSet* instrSet) override;
  bool onCloseSampColl(VGMSampColl* sampColl) override;

 private:
  std::unordered_map<int, AkaoSeq*> seqs;
  std::unordered_map<int, AkaoInstrSet*> instrSets;
  std::vector<AkaoSampColl*> sampColls;

  bool tryCreateCollection(int id);
};
