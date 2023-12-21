#pragma once
#include "Format.h"
#include "Root.h"
#include "SonyPS2Scanner.h"
#include "Matcher.h"
#include "VGMColl.h"

typedef struct _VersCk {
  uint32_t Creator;
  uint32_t Type;
  uint32_t chunkSize;
  uint16_t reserved;
  uint8_t versionMajor;
  uint8_t versionMinor;
} VersCk;

// ***********
// SonyPS2Coll
// ***********

class SonyPS2Coll:
    public VGMColl {
 public:
  SonyPS2Coll(std::string name = "Unnamed Collection") : VGMColl(name) { }
  virtual ~SonyPS2Coll() { }
};


// *************
// SonyPS2Format
// *************

BEGIN_FORMAT(SonyPS2)
  USING_SCANNER(SonyPS2Scanner)
  USING_MATCHER_WITH_ARG(FilenameMatcher, true)
  USING_COLL(SonyPS2Coll)
END_FORMAT()


