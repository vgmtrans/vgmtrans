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

class AkaoColl final : public VGMColl {
public:
  explicit AkaoColl(std::string name = "Unnamed Collection")
      : VGMColl(std::move(name)) {}

  bool loadMain() override;
  void preSynthFileCreation() override;
  void postSynthFileCreation() override;

  AkaoInstrSet *origInstrSet = nullptr;
  uint32_t numInstrsToAdd = 0;

private:
  std::tuple<std::unordered_map<int, AkaoArt *>, std::unordered_map<int, int>,
             std::unordered_map<int, AkaoSampColl *>>
  mapSampleCollections();
};

// **********
// AkaoFormat
// **********

BEGIN_FORMAT(Akao)
  USING_SCANNER(AkaoScanner)
  USING_MATCHER(AkaoMatcher)
  USING_COLL(AkaoColl)
END_FORMAT()
