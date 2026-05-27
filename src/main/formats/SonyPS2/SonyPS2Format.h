#pragma once

#include "base/Types.h"
#include "Format.h"
#include "SonyPS2Scanner.h"
#include "FilenameMatcher.h"
#include "VGMColl.h"

typedef struct _VersCk {
  u32 Creator;
  u32 Type;
  u32 chunkSize;
  u16 reserved;
  u8 versionMajor;
  u8 versionMinor;
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


