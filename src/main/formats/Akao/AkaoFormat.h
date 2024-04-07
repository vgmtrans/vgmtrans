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
  explicit AkaoColl(std::string name = "Unnamed Collection") : VGMColl(name), origInstrSet(nullptr), numAddedInstrs(0) {}

  bool LoadMain() override;
  void PreSynthFileCreation() override;
  void PostSynthFileCreation() override;

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
