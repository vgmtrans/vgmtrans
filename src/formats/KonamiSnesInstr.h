#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"

// ******************
// KonamiSnesInstrSet
// ******************

class KonamiSnesInstrSet :
	public VGMInstrSet
{
public:
	KonamiSnesInstrSet(RawFile* file, uint32_t offset, uint32_t spcDirAddr, const std::wstring & name = L"KonamiSnesInstrSet");
	virtual ~KonamiSnesInstrSet(void);

	virtual bool GetHeaderInfo();
	virtual bool GetInstrPointers();

protected:
	uint32_t spcDirAddr;
	std::vector<uint8_t> usedSRCNs;
};

// ***************
// KonamiSnesInstr
// ***************

class KonamiSnesInstr
	: public VGMInstr
{
public:
	KonamiSnesInstr(VGMInstrSet* instrSet, uint32_t offset, uint32_t theBank, uint32_t theInstrNum, uint32_t spcDirAddr, const std::wstring& name = L"KonamiSnesInstr");
	virtual ~KonamiSnesInstr(void);

	virtual bool LoadInstr();

	static bool IsValidHeader(RawFile * file, uint32_t addrInstrHeader, uint32_t spcDirAddr);

protected:
	uint32_t spcDirAddr;
};

// *************
// KonamiSnesRgn
// *************

class KonamiSnesRgn
	: public VGMRgn
{
public:
	KonamiSnesRgn(KonamiSnesInstr* instr, uint32_t offset);
	virtual ~KonamiSnesRgn(void);

	virtual bool LoadRgn();
};
