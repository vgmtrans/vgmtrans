#include "stdafx.h"
#include "RareSnesInstr.h"
#include "Format.h"
#include "SNESDSP.h"
#include "RareSnesFormat.h"

// ****************
// RareSnesInstrSet
// ****************

RareSnesInstrSet::RareSnesInstrSet(RawFile* file, ULONG offset, U32 spcDirAddr, const std::wstring & name) :
	VGMInstrSet(RareSnesFormat::name, file, offset, 0, name),
	spcDirAddr(spcDirAddr),
	maxSRCNValue(255)
{
	Initialize();
}

RareSnesInstrSet::RareSnesInstrSet(RawFile* file, ULONG offset, U32 spcDirAddr,
		const std::map<BYTE, double> & instrUnityKeyHints, const std::map<BYTE, USHORT> & instrADSRHints,
		const std::wstring & name) :
	VGMInstrSet(RareSnesFormat::name, file, offset, 0, name),
	spcDirAddr(spcDirAddr),
	maxSRCNValue(255),
	instrUnityKeyHints(instrUnityKeyHints),
	instrADSRHints(instrADSRHints)
{
	Initialize();
}

RareSnesInstrSet::~RareSnesInstrSet()
{
}

void RareSnesInstrSet::Initialize()
{
	for (UINT srcn = 0; srcn < 256; srcn++)
	{
		UINT offDirEnt = spcDirAddr + (srcn * 4);
		if (offDirEnt + 4 > 0x10000)
		{
			maxSRCNValue = srcn - 1;
			break;
		}

		if (GetShort(offDirEnt) == 0)
		{
			maxSRCNValue = srcn - 1;
			break;
		}
	}

	unLength = 0x100;
	if (dwOffset + unLength > GetRawFile()->size())
	{
		unLength = GetRawFile()->size() - dwOffset;
	}
	ScanAvailableInstruments();
}

void RareSnesInstrSet::ScanAvailableInstruments()
{
	availInstruments.clear();

	bool firstZero = true;
	for (UINT inst = 0; inst < unLength; inst++)
	{
		BYTE srcn = GetByte(dwOffset + inst);

		if (srcn == 0 && !firstZero)
		{
			continue;
		}
		if (srcn == 0)
		{
			firstZero = false;
		}

		UINT offDirEnt = spcDirAddr + (srcn * 4);
		if (offDirEnt + 4 > 0x10000)
		{
			continue;
		}
		if (srcn > maxSRCNValue)
		{
			continue;
		}

		uint16_t addrSampStart = GetShort(offDirEnt);
		uint16_t addrSampLoop = GetShort(offDirEnt + 2);
		// valid loop?
		if (addrSampStart > addrSampLoop)
		{
			continue;
		}
		// not in DIR table
		if (addrSampStart < spcDirAddr + (128 * 4))
		{
			continue;
		}
		// address 0 is probably legit, but it should not be used
		if (addrSampStart == 0)
		{
			continue;
		}
		// Rare engine does not break the following rule... perhaps
		if (addrSampStart < spcDirAddr)
		{
			continue;
		}

		availInstruments.push_back(inst);
	}
}

bool RareSnesInstrSet::GetHeaderInfo()
{
	return true;
}

bool RareSnesInstrSet::GetInstrPointers()
{
	for (std::vector<BYTE>::iterator itr = availInstruments.begin(); itr != availInstruments.end(); ++itr)
	{
		BYTE inst = (*itr);
		BYTE srcn = GetByte(dwOffset + inst);

		double transpose = 0;
		std::map<BYTE, double>::iterator itrKey;
		itrKey = this->instrUnityKeyHints.find(inst);
		if (itrKey != instrUnityKeyHints.end())
		{
			transpose = itrKey->second;
		}

		USHORT adsr = 0x8FE0;
		std::map<BYTE, USHORT>::iterator itrADSR;
		itrADSR = this->instrADSRHints.find(inst);
		if (itrADSR != instrADSRHints.end())
		{
			adsr = itrADSR->second;
		}

		RareSnesInstr * newInstr = new RareSnesInstr(this, dwOffset + inst, inst >> 7, inst & 0x7f, spcDirAddr, transpose, adsr);
		aInstrs.push_back(newInstr);
	}
	return aInstrs.size() != 0;
}

const std::vector<BYTE>& RareSnesInstrSet::GetAvailableInstruments()
{
	return availInstruments;
}

// *************
// RareSnesInstr
// *************

RareSnesInstr::RareSnesInstr(VGMInstrSet* instrSet, ULONG offset, ULONG theBank, ULONG theInstrNum, U32 spcDirAddr, double transpose, U16 adsr, const wstring& name) :
	VGMInstr(instrSet, offset, 1, theBank, theInstrNum, name),
	spcDirAddr(spcDirAddr),
	transpose(transpose),
	adsr(adsr)
{
}

RareSnesInstr::~RareSnesInstr()
{
}

bool RareSnesInstr::LoadInstr()
{
	BYTE srcn = GetByte(dwOffset);
	UINT offDirEnt = spcDirAddr + (srcn * 4);
	if (offDirEnt + 4 > 0x10000)
	{
		return false;
	}

	uint16_t addrSampStart = GetShort(offDirEnt);

	RareSnesRgn * rgn = new RareSnesRgn(this, dwOffset, transpose, adsr);
	rgn->sampOffset = addrSampStart - spcDirAddr;
	aRgns.push_back(rgn);
	return true;
}

// ***********
// RareSnesRgn
// ***********

RareSnesRgn::RareSnesRgn(RareSnesInstr* instr, ULONG offset, double transpose, U16 adsr) :
	VGMRgn(instr, offset, 1),
	transpose(transpose),
	adsr(adsr)
{
	int relKey = (int) (transpose + 0.5);
	short fineTune = (short) ((transpose - relKey) * 100.0 + 0.5);

	// NOTE_PITCH_TABLE[73] == 0x1000
	// 0x80 + (73 - 36) = 0xA5
	SetUnityKey(36 + 36 - relKey);
	SetFineTune(fineTune);
}

RareSnesRgn::~RareSnesRgn()
{
}

bool RareSnesRgn::LoadRgn()
{
	
	return true;
}
