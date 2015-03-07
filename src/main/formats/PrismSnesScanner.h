#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class PrismSnesScanner :
	public VGMScanner
{
public:
	PrismSnesScanner(void)
	{
		USE_EXTENSION(L"spc");
	}
	virtual ~PrismSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForPrismSnesFromARAM (RawFile* file);
	void SearchForPrismSnesFromROM (RawFile* file);

private:
	static BytePattern ptnLoadSeq;
	static BytePattern ptnExecVCmd;
	static BytePattern ptnSetDSPd;
	static BytePattern ptnLoadInstr;
	static BytePattern ptnLoadInstrTuning;
};
