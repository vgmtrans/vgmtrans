#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "AkaoSnesFormat.h"

// ****************
// AkaoSnesInstrSet
// ****************

class AkaoSnesInstrSet :
	public VGMInstrSet
{
public:
	AkaoSnesInstrSet(RawFile* file, AkaoSnesVersion ver, uint32_t spcDirAddr, uint16_t addrTuningTable, uint16_t addrADSRTable, const std::wstring & name = L"AkaoSnesInstrSet");
	virtual ~AkaoSnesInstrSet(void);

	virtual bool GetHeaderInfo();
	virtual bool GetInstrPointers();

	AkaoSnesVersion version;

protected:
	uint32_t spcDirAddr;
	uint16_t addrTuningTable;
	uint16_t addrADSRTable;
	std::vector<uint8_t> usedSRCNs;
};

// *************
// AkaoSnesInstr
// *************

class AkaoSnesInstr
	: public VGMInstr
{
public:
	AkaoSnesInstr(VGMInstrSet* instrSet, AkaoSnesVersion ver, uint8_t srcn, uint32_t spcDirAddr, uint16_t addrTuningTable, uint16_t addrADSRTable, const std::wstring& name = L"AkaoSnesInstr");
	virtual ~AkaoSnesInstr(void);

	virtual bool LoadInstr();

	AkaoSnesVersion version;

protected:
	uint32_t spcDirAddr;
	uint16_t addrTuningTable;
	uint16_t addrADSRTable;
};

// ***********
// AkaoSnesRgn
// ***********

class AkaoSnesRgn
	: public VGMRgn
{
public:
	AkaoSnesRgn(AkaoSnesInstr* instr, AkaoSnesVersion ver, uint8_t srcn, uint32_t spcDirAddr, uint16_t addrTuningTable, uint16_t addrADSRTable);
	virtual ~AkaoSnesRgn(void);

	virtual bool LoadRgn();

	AkaoSnesVersion version;
};
