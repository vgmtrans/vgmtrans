#pragma once
#include "Scanner.h"

class SuzukiSnesScanner :
	public VGMScanner
{
public:
	SuzukiSnesScanner(void)
	{
		//USE_EXTENSION(L"spc");
	}

	virtual ~SuzukiSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForSuzukiSnesSeq (RawFile* file);
};
