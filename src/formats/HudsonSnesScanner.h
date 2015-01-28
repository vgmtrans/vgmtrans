#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum HudsonSnesVersion; // see HudsonSnesFormat.h

class HudsonSnesScanner :
	public VGMScanner
{
public:
	HudsonSnesScanner(void)
	{
		USE_EXTENSION(L"spc");
	}
	virtual ~HudsonSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForHudsonSnesFromARAM(RawFile* file);
	void SearchForHudsonSnesFromROM(RawFile* file);

private:
	static BytePattern ptnNoteLenTable;
	static BytePattern ptnGetSeqTableAddrV0;
	static BytePattern ptnGetSeqTableAddrV1V2;
};
