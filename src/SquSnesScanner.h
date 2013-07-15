#pragma once
#include "Scanner.h"

class SquSnesScanner :
	public VGMScanner
{
public:
	SquSnesScanner(void)
	{
		//USE_EXTENSION(L"spc");
	}
	virtual ~SquSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForSquSnesSeq (RawFile* file);
};
