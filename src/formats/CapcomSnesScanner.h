#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class CapcomSnesScanner :
	public VGMScanner
{
public:
	CapcomSnesScanner(void)
	{
		USE_EXTENSION(L"spc");
	}
	virtual ~CapcomSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForCapcomSnesFromARAM (RawFile* file);
	void SearchForCapcomSnesFromROM (RawFile* file);
	bool IsValidBGMHeader (RawFile* file, UINT addrSongHeader);

private:
	static BytePattern ptnReadSongList;
	static BytePattern ptnReadBGMAddress;
};
