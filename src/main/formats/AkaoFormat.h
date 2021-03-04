#pragma once
#include "Format.h"
#include "AkaoScanner.h"
#include "VGMColl.h"
#include "Matcher.h"

class AkaoInstrSet;

// ********
// AkaoColl
// ********

class AkaoColl:
    public VGMColl {
 public:
  AkaoColl(std::wstring name = L"Unnamed Collection") : VGMColl(name) { }

  virtual bool LoadMain();
  virtual void PreSynthFileCreation();
  virtual void PostSynthFileCreation();

 public:
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
