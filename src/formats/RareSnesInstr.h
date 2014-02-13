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
	RareSnesInstrSet(RawFile* file, uint32_t offset, uint32_t spcDirAddr, const std::wstring & name = L"RareSnesInstrSet");
	RareSnesInstrSet(RawFile* file, uint32_t offset, uint32_t spcDirAddr, const std::map<uint8_t, double> & instrUnityKeyHints, const std::map<uint8_t, uint16_t> & instrADSRHints, const std::wstring & name = L"RareSnesInstrSet");
	virtual ~RareSnesInstrSet(void);

	virtual void Initialize();
	virtual bool GetHeaderInfo();
	virtual bool GetInstrPointers();

	const std::vector<uint8_t>& GetAvailableInstruments();

protected:
	uint32_t spcDirAddr;
	uint8_t maxSRCNValue;
	std::vector<uint8_t> availInstruments;
	std::map<uint8_t, double> instrUnityKeyHints;
	std::map<uint8_t, uint16_t> instrADSRHints;

	void ScanAvailableInstruments();
};

// *************
// RareSnesInstr
// *************

class RareSnesInstr
	: public VGMInstr
{
public:
	RareSnesInstr(VGMInstrSet* instrSet, uint32_t offset, uint32_t theBank, uint32_t theInstrNum, uint32_t spcDirAddr, double transpose = 0, uint16_t adsr = 0x8FE0, const std::wstring& name = L"RareSnesInstr");
	virtual ~RareSnesInstr(void);

	virtual bool LoadInstr();

protected:
	uint32_t spcDirAddr;
	double transpose;
	uint16_t adsr;
};

// ***********
// RareSnesRgn
// ***********

class RareSnesRgn
	: public VGMRgn
{
public:
	RareSnesRgn(RareSnesInstr* instr, uint32_t offset, double transpose = 0, uint16_t adsr = 0x8FE0);
	virtual ~RareSnesRgn(void);

	virtual bool LoadRgn();

protected:
	double transpose;
	uint16_t adsr;
};
