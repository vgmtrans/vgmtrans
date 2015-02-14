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
	static BytePattern ptnSetSongHeaderAddress;
	static BytePattern ptnReadSongListAXE;
	static BytePattern ptnReadSongListCNTR3;
	static BytePattern ptnJumpToVcmd;
	static BytePattern ptnJumpToVcmdCNTR3;
	static BytePattern ptnBranchForVcmd6xMDR2;
	static BytePattern ptnBranchForVcmd6xCNTR3;
	static BytePattern ptnSetDIR;
	static BytePattern ptnSetDIRCNTR3;
	static BytePattern ptnLoadInstr;
	static BytePattern ptnLoadInstrCNTR3;
	static BytePattern ptnLoadPercInstr;
};
