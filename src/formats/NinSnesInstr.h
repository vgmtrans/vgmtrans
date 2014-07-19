#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"

// ****************
// NinSnesInstrSet
// ****************

class NinSnesInstrSet :
	public VGMInstrSet
{
public:
	NinSnesInstrSet(RawFile* file, uint32_t offset, uint32_t spcDirAddr, const std::wstring & name = L"NinSnesInstrSet");
	virtual ~NinSnesInstrSet(void);

	virtual bool GetHeaderInfo();
	virtual bool GetInstrPointers();

protected:
	uint32_t spcDirAddr;
	std::vector<uint8_t> usedSRCNs;
};

// *************
// NinSnesInstr
// *************

class NinSnesInstr
	: public VGMInstr
{
public:
	NinSnesInstr(VGMInstrSet* instrSet, uint32_t offset, uint32_t theBank, uint32_t theInstrNum, uint32_t spcDirAddr, const std::wstring& name = L"NinSnesInstr");
	virtual ~NinSnesInstr(void);

	virtual bool LoadInstr();

	static bool IsValidHeader(RawFile * file, uint32_t addrInstrHeader, uint32_t spcDirAddr);

protected:
	uint32_t spcDirAddr;
};

// ***********
// NinSnesRgn
// ***********

class NinSnesRgn
	: public VGMRgn
{
public:
	NinSnesRgn(NinSnesInstr* instr, uint32_t offset);
	virtual ~NinSnesRgn(void);

	virtual bool LoadRgn();
};
