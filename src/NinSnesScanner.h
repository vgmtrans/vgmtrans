#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class NinSnesScanner :
	public VGMScanner
{
public:
	NinSnesScanner(void)
	{
		//USE_EXTENSION(L"spc");
	}
	virtual ~NinSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForNinSnesSeq (RawFile* file);

private:
	static BytePattern ptnBranchForVcmd;
};
