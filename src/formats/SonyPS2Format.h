#pragma once
#include "Format.h"
#include "Root.h"
#include "SonyPS2Scanner.h"
#include "Matcher.h"
#include "VGMColl.h"

typedef struct _VersCk
{
	U32 Creator;
	U32 Type;
	U32 chunkSize;
	U16 reserved;
	U8 versionMajor;
	U8 versionMinor;
} VersCk;

// ***********
// SonyPS2Coll
// ***********

class SonyPS2Coll :
	public VGMColl
{
public:
	SonyPS2Coll(std::wstring name = L"Unnamed Collection") : VGMColl(name) {}
	virtual ~SonyPS2Coll() {}
};


// *************
// SonyPS2Format
// *************

BEGIN_FORMAT(SonyPS2)
	USING_SCANNER(SonyPS2Scanner)
	USING_MATCHER_WITH_ARG(FilenameMatcher, true)
	USING_COLL(SonyPS2Coll)
END_FORMAT()


