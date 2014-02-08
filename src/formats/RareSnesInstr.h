#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"

// ****************
// RareSnesInstrSet
// ****************

class RareSnesInstrSet :
	public VGMInstrSet
{
public:
	RareSnesInstrSet(RawFile* file, ULONG offset, U32 spcDirAddr, const std::wstring & name = L"RareSnesInstrSet");
	virtual ~RareSnesInstrSet(void);

	virtual bool GetHeaderInfo();
	virtual bool GetInstrPointers();

	const std::vector<BYTE>& GetAvailableInstruments();

protected:
	U32 spcDirAddr;
	BYTE maxSRCNValue;
	std::vector<BYTE> availInstruments;

	void ScanAvailableInstruments();
};

// *************
// RareSnesInstr
// *************

class RareSnesInstr
	: public VGMInstr
{
public:
	RareSnesInstr(VGMInstrSet* instrSet, ULONG offset, ULONG theBank, ULONG theInstrNum, U32 spcDirAddr, const wstring& name = L"RareSnesInstr");
	virtual ~RareSnesInstr(void);

	virtual bool LoadInstr();

protected:
	U32 spcDirAddr;
};

// ***********
// RareSnesRgn
// ***********

class RareSnesRgn
	: public VGMRgn
{
public:
	RareSnesRgn(RareSnesInstr* instr, ULONG offset);
	virtual ~RareSnesRgn(void);

	virtual bool LoadRgn();
};
