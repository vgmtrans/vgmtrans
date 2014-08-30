#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum KonamiSnesVersion : uint8_t; // see KonamiSnesFormat.h

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
	static BytePattern ptnSetSongHeaderAddress;
	static BytePattern ptnJumpToVcmd;
	static BytePattern ptnSetDIR;
	static BytePattern ptnLoadInstr;
	static BytePattern ptnLoadPercInstr;
};
