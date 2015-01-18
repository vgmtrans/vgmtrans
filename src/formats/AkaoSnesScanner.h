#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum AkaoSnesVersion; // see AkaoSnesFormat.h

class AkaoSnesScanner :
	public VGMScanner
{
public:
	AkaoSnesScanner(void)
	{
		USE_EXTENSION(L"spc");
	}
	virtual ~AkaoSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForAkaoSnesFromARAM(RawFile* file);
	void SearchForAkaoSnesFromROM(RawFile* file);
};
