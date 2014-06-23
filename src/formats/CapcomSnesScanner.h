#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum CapcomSnesVersion; // see CapcomSnesFormat.h

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

private:
	int GetLengthOfSongList (RawFile* file, uint16_t addrSongList);
	uint16_t GetCurrentPlayAddressFromARAM (RawFile* file, CapcomSnesVersion version, uint8_t channel);
	int8_t GuessCurrentSongFromARAM (RawFile* file, CapcomSnesVersion version, uint16_t addrSongList);
	bool IsValidBGMHeader (RawFile* file, UINT addrSongHeader);
	uint16_t GetDIRAddress (RawFile* file);

	static BytePattern ptnReadSongList;
	static BytePattern ptnReadBGMAddress;
	static BytePattern ptnDspRegInit;
	static BytePattern ptnDspRegInitOldVer;
};
