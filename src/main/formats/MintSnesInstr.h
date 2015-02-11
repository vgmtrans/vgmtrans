#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "MintSnesFormat.h"

struct MintSnesInstrHint
{
	MintSnesInstrHint() :
		startAddress(0),
		size(0),
		seqAddress(0),
		seqSize(0),
		rgnAddress(0),
		transpose(0),
		pan(0)
	{
	}

	uint16_t startAddress;
	uint16_t size;
	uint16_t seqAddress;
	uint16_t seqSize;
	uint16_t rgnAddress;
	int8_t transpose;
	int8_t pan;
};

struct MintSnesInstrHintDir
{
	MintSnesInstrHintDir() :
		startAddress(0),
		size(0),
		percussion(false)
	{
	}

	uint16_t startAddress;
	uint16_t size;

	bool percussion;
	MintSnesInstrHint instrHint;
	std::vector<MintSnesInstrHint> percHints;
};

// ****************
// MintSnesInstrSet
// ****************

class MintSnesInstrSet :
	public VGMInstrSet
{
public:
	MintSnesInstrSet(RawFile* file, MintSnesVersion ver, uint32_t spcDirAddr, std::vector<uint16_t> instrumentAddresses, std::map<uint16_t, MintSnesInstrHintDir> instrumentHints, const std::wstring & name = L"MintSnesInstrSet");
	virtual ~MintSnesInstrSet(void);

	virtual bool GetHeaderInfo();
	virtual bool GetInstrPointers();

	MintSnesVersion version;

protected:
	uint32_t spcDirAddr;
	std::vector<uint16_t> instrumentAddresses;
	std::map<uint16_t, MintSnesInstrHintDir> instrumentHints;
	std::vector<uint8_t> usedSRCNs;
};

// *************
// MintSnesInstr
// *************

class MintSnesInstr
	: public VGMInstr
{
public:
	MintSnesInstr(VGMInstrSet* instrSet, MintSnesVersion ver, uint8_t instrNum, uint32_t spcDirAddr, const MintSnesInstrHintDir& instrHintDir, const std::wstring& name = L"MintSnesInstr");
	virtual ~MintSnesInstr(void);

	virtual bool LoadInstr();

	MintSnesVersion version;

protected:
	uint32_t spcDirAddr;
	MintSnesInstrHintDir instrHintDir;
};

// ***********
// MintSnesRgn
// ***********

class MintSnesRgn
	: public VGMRgn
{
public:
	MintSnesRgn(MintSnesInstr* instr, MintSnesVersion ver, uint32_t spcDirAddr, const MintSnesInstrHint& instrHint, int8_t percNoteKey = -1);
	virtual ~MintSnesRgn(void);

	virtual bool LoadRgn();

	MintSnesVersion version;
};
