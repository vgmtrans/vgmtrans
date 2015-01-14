#pragma once
#include "Scanner.h"

class AkaoSnesScanner :
	public VGMScanner
{
public:
	AkaoSnesScanner(void)
	{
		//USE_EXTENSION(L"spc");
	}
	virtual ~AkaoSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForAkaoSnesSeq (RawFile* file);
};
