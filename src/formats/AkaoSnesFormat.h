#pragma once
#include "Format.h"
#include "Root.h"
#include "AkaoSnesScanner.h"
//#include "Matcher.h"
//#include "VGMColl.h"


// *************
// SquarePS2Coll
// *************

/*class SquarePS2Coll :
	public VGMColl
{
public:
	SquarePS2Coll(wstring name = L"Unnamed Collection") : VGMColl(name) {}
	virtual ~SquarePS2Coll() {}
};*/

// ***************
// SquarePS2Format
// ***************

BEGIN_FORMAT(AkaoSnes)
	USING_SCANNER(AkaoSnesScanner)
	//USING_MATCHER(SimpleMatcher)
	//USING_COLL(SquarePS2Coll)
END_FORMAT()