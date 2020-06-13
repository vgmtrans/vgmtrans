#pragma once
#include "Format.h"
#include "AkaoScanner.h"
#include "VGMColl.h"
#include "Matcher.h"

enum class AkaoPs1Version : uint8_t {
  // Final Fantasy 7
  VERSION_1_0 = 1,

  // Headered AKAO samples, 0xFC opcode is used for specifying articulation zones
  // SaGa Frontier
  VERSION_1_1,

  // 0xFC xx meta event introduced:
  // Front Mission 2
  // Chocobo's Mysterious Dungeon
  VERSION_1_2,

  // Header size increased to 0x20 bytes:
  // Parasite Eve
  VERSION_2,

  // Header size increased to 0x40 bytes:
  VERSION_3
};

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
  virtual bool PreDLSMainCreation();
  virtual bool PostDLSMainCreation();

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
