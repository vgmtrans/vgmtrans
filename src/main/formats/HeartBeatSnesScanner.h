#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum HeartBeatSnesVersion; // see HeartBeatSnesFormat.h

class HeartBeatSnesScanner :
	public VGMScanner
{
public:
	HeartBeatSnesScanner(void)
	{
		USE_EXTENSION(L"spc");
	}
	virtual ~HeartBeatSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForHeartBeatSnesFromARAM(RawFile* file);
	void SearchForHeartBeatSnesFromROM(RawFile* file);

private:
	static BytePattern ptnReadSongList;
	static BytePattern ptnSetDIR;
	static BytePattern ptnLoadSRCN;
};
