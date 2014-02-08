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
	RareSnesInstrSet(RawFile* file, ULONG offset, U32 spcDirAddr, const std::map<BYTE, double> & instrUnityKeyHints, const std::map<BYTE, USHORT> & instrADSRHints, const std::wstring & name = L"RareSnesInstrSet");
	virtual ~RareSnesInstrSet(void);

	virtual void Initialize();
	virtual bool GetHeaderInfo();
	virtual bool GetInstrPointers();

	const std::vector<BYTE>& GetAvailableInstruments();

protected:
	U32 spcDirAddr;
	BYTE maxSRCNValue;
	std::vector<BYTE> availInstruments;
	std::map<BYTE, double> instrUnityKeyHints;
	std::map<BYTE, USHORT> instrADSRHints;

	void ScanAvailableInstruments();
};

// *************
// RareSnesInstr
// *************

class RareSnesInstr
	: public VGMInstr
{
public:
	RareSnesInstr(VGMInstrSet* instrSet, ULONG offset, ULONG theBank, ULONG theInstrNum, U32 spcDirAddr, double transpose = 0, U16 adsr = 0x8FE0, const wstring& name = L"RareSnesInstr");
	virtual ~RareSnesInstr(void);

	virtual bool LoadInstr();

protected:
	U32 spcDirAddr;
	double transpose;
	U16 adsr;
};

// ***********
// RareSnesRgn
// ***********

class RareSnesRgn
	: public VGMRgn
{
public:
	RareSnesRgn(RareSnesInstr* instr, ULONG offset, double transpose = 0, U16 adsr = 0x8FE0);
	virtual ~RareSnesRgn(void);

	virtual bool LoadRgn();

protected:
	double transpose;
	U16 adsr;
};
