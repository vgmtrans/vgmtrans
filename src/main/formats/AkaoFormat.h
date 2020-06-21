#pragma once
#include "Format.h"
#include "AkaoScanner.h"
#include "VGMColl.h"
#include "Matcher.h"

class AkaoInstrSet;

//const THIS_FORMAT FMT_AKAO;

// ********
// AkaoColl
// ********

class AkaoColl:
    public VGMColl {
 public:
  AkaoColl(std::wstring name = L"Unnamed Collection") : VGMColl(name) { }
  virtual ~AkaoColl() { }

  virtual bool LoadMain();
  virtual void PreSynthFileCreation();
  virtual void PostSynthFileCreation();

 public:
  AkaoInstrSet *origInstrSet;
  uint32_t numAddedInstrs;
};

// ***********
// AkaoMatcher
// ***********

//class AkaoMatcher : public SimpleMatcher
//{
//public:
//	AkaoMatcher(Format* format);
//	~AkaoMatcher(void) {}
//};


// **********
// AkaoFormat
// **********

//class AkaoFormat : public Format
//{
//public:
//	AkaoFormat(void) {}
//	virtual ~AkaoFormat(void) {}
BEGIN_FORMAT(Akao)
  USING_SCANNER(AkaoScanner)
  //USING_MATCHER_WITH_ARG(SimpleMatcher, true)
  USING_MATCHER_WITH_ARG(GetIdMatcher, true)
  USING_COLL(AkaoColl)
END_FORMAT()
//};
