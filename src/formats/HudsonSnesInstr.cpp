#include "stdafx.h"
#include "HudsonSnesInstr.h"
#include "Format.h"
#include "SNESDSP.h"
#include "HudsonSnesFormat.h"

// ******************
// HudsonSnesInstrSet
// ******************

HudsonSnesInstrSet::HudsonSnesInstrSet(RawFile* file, HudsonSnesVersion ver, uint32_t offset, uint32_t length, uint32_t spcDirAddr, const std::wstring & name) :
	VGMInstrSet(HudsonSnesFormat::name, file, offset, length, name), version(ver),
	spcDirAddr(spcDirAddr)
{
}

HudsonSnesInstrSet::~HudsonSnesInstrSet()
{
}

bool HudsonSnesInstrSet::GetHeaderInfo()
{
	return true;
}

bool HudsonSnesInstrSet::GetInstrPointers()
{
	usedSRCNs.clear();
	for (uint8_t instrNum = 0; instrNum <= unLength / 4; instrNum++)
	{
		uint32_t ofsInstrEntry = dwOffset + (instrNum * 4);
		if (ofsInstrEntry + 4 > 0x10000) {
			break;
		}
		uint8_t srcn = GetByte(ofsInstrEntry);

		uint32_t addrDIRentry = spcDirAddr + (srcn * 4);
		if (!SNESSampColl::IsValidSampleDir(rawfile, addrDIRentry, true)) {
			continue;
		}

		usedSRCNs.push_back(srcn);

		std::wostringstream instrName;
		instrName << L"Instrument " << srcn;
		HudsonSnesInstr * newInstr = new HudsonSnesInstr(this, version, ofsInstrEntry, instrNum, spcDirAddr, instrName.str());
		aInstrs.push_back(newInstr);
	}
	if (aInstrs.size() == 0)
	{
		return false;
	}

	std::sort(usedSRCNs.begin(), usedSRCNs.end());
	SNESSampColl * newSampColl = new SNESSampColl(HudsonSnesFormat::name, this->rawfile, spcDirAddr, usedSRCNs);
	if (!newSampColl->LoadVGMFile())
	{
		delete newSampColl;
		return false;
	}

	return true;
}

// ***************
// HudsonSnesInstr
// ***************

HudsonSnesInstr::HudsonSnesInstr(VGMInstrSet* instrSet, HudsonSnesVersion ver, uint32_t offset, uint8_t instrNum, uint32_t spcDirAddr, const std::wstring& name) :
	VGMInstr(instrSet, offset, 4, 0, instrNum, name), version(ver),
	spcDirAddr(spcDirAddr)
{
}

HudsonSnesInstr::~HudsonSnesInstr()
{
}

bool HudsonSnesInstr::LoadInstr()
{
	uint8_t srcn = GetByte(dwOffset);

	uint32_t offDirEnt = spcDirAddr + (srcn * 4);
	if (offDirEnt + 4 > 0x10000) {
		return false;
	}

	uint16_t addrSampStart = GetShort(offDirEnt);

	HudsonSnesRgn * rgn = new HudsonSnesRgn(this, version, dwOffset, spcDirAddr);
	rgn->sampOffset = addrSampStart - spcDirAddr;
	aRgns.push_back(rgn);

	return true;
}

// *************
// HudsonSnesRgn
// *************

HudsonSnesRgn::HudsonSnesRgn(HudsonSnesInstr* instr, HudsonSnesVersion ver, uint32_t offset, uint32_t spcDirAddr) :
	VGMRgn(instr, offset, 4),
	version(ver)
{
	uint8_t srcn = GetByte(dwOffset);
	uint8_t adsr1 = GetByte(dwOffset + 1);
	uint8_t adsr2 = GetByte(dwOffset + 2);
	uint8_t gain = GetByte(dwOffset + 3);

	unityKey = 69;
	AddSimpleItem(dwOffset, 1, L"SRCN");
	AddSimpleItem(dwOffset + 1, 1, L"ADSR(1)");
	AddSimpleItem(dwOffset + 2, 1, L"ADSR(2)");
	AddSimpleItem(dwOffset + 3, 1, L"GAIN");
	SNESConvADSR<VGMRgn>(this, adsr1, adsr2, gain);
}

HudsonSnesRgn::~HudsonSnesRgn()
{
}

bool HudsonSnesRgn::LoadRgn()
{
	return true;
}
