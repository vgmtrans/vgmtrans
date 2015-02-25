#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class IMaxSnesScanner :
	public VGMScanner
{
public:
	IMaxSnesScanner(void)
	{
		USE_EXTENSION(L"spc");
	}
	virtual ~IMaxSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForIMaxSnesFromARAM (RawFile* file);
	void SearchForIMaxSnesFromROM (RawFile* file);

private:
	static BytePattern ptnLoadSeq;
};
