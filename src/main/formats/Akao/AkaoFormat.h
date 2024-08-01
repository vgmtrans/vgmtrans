#pragma once
#include "Format.h"
#include "AkaoScanner.h"
#include "VGMColl.h"
#include "AkaoMatcher.h"

class AkaoInstrSet;

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
};

// **********
// AkaoFormat
// **********

BEGIN_FORMAT(Akao)
  USING_SCANNER(AkaoScanner)
  USING_MATCHER(AkaoMatcher)
  USING_COLL(AkaoColl)
END_FORMAT()
