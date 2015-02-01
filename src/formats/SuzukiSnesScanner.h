#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum SuzukiSnesVersion; // see SuzukiSnesFormat.h

class SuzukiSnesScanner :
	public VGMScanner
{
public:
	SuzukiSnesScanner(void)
	{
		USE_EXTENSION(L"spc");
	}
	virtual ~SuzukiSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForSuzukiSnesFromARAM(RawFile* file);
	void SearchForSuzukiSnesFromROM(RawFile* file);

private:
	static BytePattern ptnLoadSongSD3;
	static BytePattern ptnLoadSongBL;
	static BytePattern ptnExecVCmdBL;
	static BytePattern ptnLoadDIR;
	static BytePattern ptnLoadInstr;
};
