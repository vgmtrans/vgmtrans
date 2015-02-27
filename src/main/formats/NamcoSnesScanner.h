#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum NamcoSnesVersion : uint8_t; // see NamcoSnesFormat.h

class NamcoSnesScanner :
	public VGMScanner
{
public:
	NamcoSnesScanner(void)
	{
		USE_EXTENSION(L"spc");
	}
	virtual ~NamcoSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForNamcoSnesFromARAM(RawFile* file);
	void SearchForNamcoSnesFromROM(RawFile* file);

private:
	static BytePattern ptnReadSongList;
	static BytePattern ptnStartSong;
};
