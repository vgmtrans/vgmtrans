#include "stdafx.h"
#include "KonamiSnesInstr.h"
#include "Format.h"
#include "SNESDSP.h"
#include "KonamiSnesFormat.h"

// ******************
// KonamiSnesInstrSet
// ******************

// Actually, this engine has 3 instrument tables:
//     - Common samples
//     - Switchable samples
//     - Percussive samples
// KonamiSnesInstrSet tries to load all these samples to merge them into a single DLS.
KonamiSnesInstrSet::KonamiSnesInstrSet(RawFile* file, uint32_t offset, uint32_t bankedInstrOffset, uint8_t firstBankedInstr, uint32_t percInstrOffset, uint32_t spcDirAddr, const std::wstring & name) :
	VGMInstrSet(KonamiSnesFormat::name, file, offset, 0, name),
	bankedInstrOffset(bankedInstrOffset),
	firstBankedInstr(firstBankedInstr),
	percInstrOffset(percInstrOffset),
	spcDirAddr(spcDirAddr)
{
}

KonamiSnesInstrSet::~KonamiSnesInstrSet()
{
}

bool KonamiSnesInstrSet::GetHeaderInfo()
{
	return true;
}

bool KonamiSnesInstrSet::GetInstrPointers()
{
	usedSRCNs.clear();
	for (int instr = 0; instr <= 0xff; instr++)
	{
		uint32_t addrInstrHeader;
		if (instr < firstBankedInstr)
		{
			// common samples
			addrInstrHeader = dwOffset + (7 * instr);
		}
		else
		{
			// switchable samples
			addrInstrHeader = bankedInstrOffset + (7 * instr);
		}
		if (addrInstrHeader + 7 > 0x10000)
		{
			return false;
		}

		if (!KonamiSnesInstr::IsValidHeader(this->rawfile, addrInstrHeader, spcDirAddr))
		{
			break;
		}

		uint8_t srcn = GetByte(addrInstrHeader);
		std::vector<uint8_t>::iterator itrSRCN = find(usedSRCNs.begin(), usedSRCNs.end(), srcn);
		if (itrSRCN == usedSRCNs.end())
		{
			usedSRCNs.push_back(srcn);
		}

		std::wostringstream instrName;
		instrName << L"Instrument " << instr;
		KonamiSnesInstr * newInstr = new KonamiSnesInstr(this, addrInstrHeader, instr >> 7, instr & 0x7f, spcDirAddr, instrName.str());
		aInstrs.push_back(newInstr);
	}
	if (aInstrs.size() == 0)
	{
		return false;
	}

	// TODO: percussive samples

	std::sort(usedSRCNs.begin(), usedSRCNs.end());
	SNESSampColl * newSampColl = new SNESSampColl(KonamiSnesFormat::name, this->rawfile, spcDirAddr, usedSRCNs);
	if (!newSampColl->LoadVGMFile())
	{
		delete newSampColl;
		return false;
	}

	return true;
}

// ***************
// KonamiSnesInstr
// ***************

KonamiSnesInstr::KonamiSnesInstr(VGMInstrSet* instrSet, uint32_t offset, uint32_t theBank, uint32_t theInstrNum, uint32_t spcDirAddr, const std::wstring& name) :
	VGMInstr(instrSet, offset, 7, theBank, theInstrNum, name),
	spcDirAddr(spcDirAddr)
{
}

KonamiSnesInstr::~KonamiSnesInstr()
{
}

bool KonamiSnesInstr::LoadInstr()
{
	uint8_t srcn = GetByte(dwOffset);
	uint32_t offDirEnt = spcDirAddr + (srcn * 4);
	if (offDirEnt + 4 > 0x10000)
	{
		return false;
	}

	uint16_t addrSampStart = GetShort(offDirEnt);

	KonamiSnesRgn * rgn = new KonamiSnesRgn(this, dwOffset);
	rgn->sampOffset = addrSampStart - spcDirAddr;
	aRgns.push_back(rgn);

	return true;
}

bool KonamiSnesInstr::IsValidHeader(RawFile * file, uint32_t addrInstrHeader, uint32_t spcDirAddr)
{
	if (addrInstrHeader + 7 > 0x10000)
	{
		return false;
	}

	uint8_t srcn = file->GetByte(addrInstrHeader);
	int16_t pitch_scale = file->GetShortBE(addrInstrHeader + 1);
	uint8_t adsr1 = file->GetByte(addrInstrHeader + 3);
	uint8_t adsr2 = file->GetByte(addrInstrHeader + 4);

	if (srcn == 0xff) // SRCN:FF is false-positive in 99.999999% of cases
	{
		return false;
	}

	uint32_t addrDIRentry = spcDirAddr + (srcn * 4);
	if (addrDIRentry + 4 > 0x10000)
	{
		return false;
	}

	uint16_t srcAddr = file->GetShort(addrDIRentry);
	uint16_t loopStartAddr = file->GetShort(addrDIRentry + 2);

	if (srcAddr > loopStartAddr || (loopStartAddr - srcAddr) % 9 != 0)
	{
		return false;
	}

	return true;
}

// *************
// KonamiSnesRgn
// *************

KonamiSnesRgn::KonamiSnesRgn(KonamiSnesInstr* instr, uint32_t offset) :
	VGMRgn(instr, offset, 7)
{
	uint8_t srcn = GetByte(offset);
	int8_t key = GetByte(offset + 1);
	int8_t tuning = GetByte(offset + 2);
	uint8_t adsr1 = GetByte(offset + 3);
	uint8_t adsr2 = GetByte(offset + 4);
	uint8_t pan = GetByte(offset + 5);
	uint8_t gain = adsr2;
	bool use_adsr = ((adsr1 & 0x80) != 0);

	double fine_tuning;
	double coarse_tuning;
	fine_tuning = modf(key + (tuning / 256), &coarse_tuning);

	// normalize
	if (fine_tuning >= 0.5)
	{
		coarse_tuning += 1.0;
		fine_tuning -= 1.0;
	}
	else if (fine_tuning <= -0.5)
	{
		coarse_tuning -= 1.0;
		fine_tuning += 1.0;
	}

	AddSampNum(srcn, offset, 1);
	AddUnityKey(95 - (int)(coarse_tuning), offset + 1, 1);
	AddFineTune((int16_t)(fine_tuning * 100.0), offset + 2, 1);
	AddSimpleItem(offset + 3, 1, L"ADSR1");
	AddSimpleItem(offset + 4, 1, use_adsr ? L"ADSR2" : L"GAIN");
	AddSimpleItem(offset + 5, 1, L"Pan");
	AddSimpleItem(offset + 6, 1, L"Volume (Decrease)");
	SNESConvADSR<VGMRgn>(this, adsr1, adsr2, gain);
}

KonamiSnesRgn::~KonamiSnesRgn()
{
}

bool KonamiSnesRgn::LoadRgn()
{
	return true;
}
