#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum MintSnesVersion; // see MintSnesFormat.h

class MintSnesScanner :
	public VGMScanner
{
public:
	MintSnesScanner(void)
	{
		USE_EXTENSION(L"spc");
	}
	virtual ~MintSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForMintSnesFromARAM(RawFile* file);
	void SearchForMintSnesFromROM(RawFile* file);

private:
	static BytePattern ptnLoadSeq;
	static BytePattern ptnSetDIR;
};
