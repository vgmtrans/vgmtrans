#pragma once
#include "Format.h"
#include "AkaoScanner.h"
#include "VGMColl.h"
#include "AkaoMatcher.h"

class AkaoInstrSet;
class AkaoArt;

// ********
// AkaoColl
// ********

class AkaoColl final :
    public VGMColl {
 public:
  explicit AkaoColl(std::string name = "Unnamed Collection") : VGMColl(std::move(name)), origInstrSet(nullptr), numInstrsToAdd(0) {}

  bool loadMain() override;
  void preSynthFileCreation() override;
  void postSynthFileCreation() override;

  AkaoInstrSet *origInstrSet;
  uint32_t numInstrsToAdd;

private:
  std::tuple<std::map<int, AkaoArt*>, std::map<int, int>, std::map<int, AkaoSampColl*>> mapSampleCollections();
};

// **********
// AkaoFormat
// **********

BEGIN_FORMAT(Akao)
  USING_SCANNER(AkaoScanner)
  USING_MATCHER(AkaoMatcher)
  USING_COLL(AkaoColl)
END_FORMAT()
