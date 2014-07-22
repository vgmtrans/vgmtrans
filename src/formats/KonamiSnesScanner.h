#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum KonamiSnesVersion; // see KonamiSnesFormat.h

class KonamiSnesScanner :
	public VGMScanner
{
public:
	KonamiSnesScanner(void)
	{
		USE_EXTENSION(L"spc");
	}
	virtual ~KonamiSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForKonamiSnesFromARAM (RawFile* file);
	void SearchForKonamiSnesFromROM (RawFile* file);

private:
	//int GetLengthOfSongList (RawFile* file, uint16_t addrSongList);
	//uint16_t GetCurrentPlayAddressFromARAM (RawFile* file, KonamiSnesVersion version, uint8_t channel);
	//int8_t GuessCurrentSongFromARAM (RawFile* file, KonamiSnesVersion version, uint16_t addrSongList);
	//bool IsValidBGMHeader (RawFile* file, UINT addrSongHeader);
	//std::map<uint8_t, uint8_t> GetInitDspRegMap (RawFile* file);

	static BytePattern ptnSetSongHeaderAddress;
	static BytePattern ptnJumpToVcmd;
	//static BytePattern ptnDspRegInit;
	//static BytePattern ptnDspRegInitOldVer;
	//static BytePattern ptnLoadInstrTableAddress;
};
