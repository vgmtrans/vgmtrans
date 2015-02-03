#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class NinSnesScanner :
	public VGMScanner
{
public:
	NinSnesScanner(void)
	{
		USE_EXTENSION(L"spc");
	}
	virtual ~NinSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForNinSnesFromARAM (RawFile* file);
	void SearchForNinSnesFromROM (RawFile* file);

private:
	static BytePattern ptnBranchForVcmd;
	static BytePattern ptnJumpToVcmd;
	static BytePattern ptnJumpToVcmdSMW;
	static BytePattern ptnReadVcmdLengthSMW;
	static BytePattern ptnIncSectionPtr;
	static BytePattern ptnLoadInstrTableAddress;
	static BytePattern ptnLoadInstrTableAddressSMW;
	static BytePattern ptnSetDIR;
	static BytePattern ptnSetDIRYI;
	static BytePattern ptnSetDIRSMW;

	// PATTERNS FOR DERIVED VERSIONS
	static BytePattern ptnIncSectionPtrGD3;
	static BytePattern ptnJumpToVcmdCTOW;
	static BytePattern ptnDispatchNoteLEM;
};
