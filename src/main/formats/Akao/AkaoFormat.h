#pragma once
#include "Format.h"
#include "AkaoScanner.h"
#include "VGMColl.h"
#include "Matcher.h"

class AkaoInstrSet;

// ********
// AkaoColl
// ********

class AkaoColl final :
    public VGMColl {
 public:
  explicit AkaoColl(std::string name = "Unnamed Collection") : VGMColl(std::move(name)), origInstrSet(nullptr), numAddedInstrs(0) {}

  bool loadMain() override;
  void preSynthFileCreation() override;
  void postSynthFileCreation() override;

  AkaoInstrSet *origInstrSet;
  uint32_t numAddedInstrs;
};

// **********
// AkaoFormat
// **********

BEGIN_FORMAT(Akao)
  USING_SCANNER(AkaoScanner)
  USING_MATCHER(FilegroupMatcher)
  USING_COLL(AkaoColl)
END_FORMAT()
