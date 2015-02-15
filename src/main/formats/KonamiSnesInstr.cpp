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
KonamiSnesInstrSet::KonamiSnesInstrSet(RawFile* file, KonamiSnesVersion ver, uint32_t offset, uint32_t bankedInstrOffset, uint8_t firstBankedInstr, uint32_t percInstrOffset, uint32_t spcDirAddr, const std::wstring & name) :
	VGMInstrSet(KonamiSnesFormat::name, file, offset, 0, name), version(ver),
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
		uint32_t instrItemSize = KonamiSnesInstr::ExpectedSize(version);

		uint32_t addrInstrHeader;
		if (instr < firstBankedInstr) {
			// common samples
			addrInstrHeader = dwOffset + (instrItemSize * instr);
		}
		else {
			// switchable samples
			addrInstrHeader = bankedInstrOffset + (instrItemSize * (instr - firstBankedInstr));
		}
		if (addrInstrHeader + instrItemSize > 0x10000) {
			return false;
		}

		if (!KonamiSnesInstr::IsValidHeader(this->rawfile, version, addrInstrHeader, spcDirAddr, false)) {
			if (instr < firstBankedInstr) {
				continue;
			}
			else {
				break;
			}
		}
		if (!KonamiSnesInstr::IsValidHeader(this->rawfile, version, addrInstrHeader, spcDirAddr, true)) {
			continue;
		}

		uint8_t srcn = GetByte(addrInstrHeader);

		uint32_t offDirEnt = spcDirAddr + (srcn * 4);
		uint16_t addrSampStart = GetShort(offDirEnt);
		if (addrSampStart < offDirEnt + 4) {
			continue;
		}

		std::vector<uint8_t>::iterator itrSRCN = find(usedSRCNs.begin(), usedSRCNs.end(), srcn);
		if (itrSRCN == usedSRCNs.end())
		{
			usedSRCNs.push_back(srcn);
		}

		std::wostringstream instrName;
		instrName << L"Instrument " << instr;
		KonamiSnesInstr * newInstr = new KonamiSnesInstr(this, version, addrInstrHeader, instr >> 7, instr & 0x7f, spcDirAddr, false, instrName.str());
		aInstrs.push_back(newInstr);
	}
	if (aInstrs.size() == 0)
	{
		return false;
	}

	// percussive samples
	KonamiSnesInstr * newInstr = new KonamiSnesInstr(this, version, percInstrOffset, 127, 0, spcDirAddr, true, L"Percussions");
	aInstrs.push_back(newInstr);

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

KonamiSnesInstr::KonamiSnesInstr(VGMInstrSet* instrSet, KonamiSnesVersion ver, uint32_t offset, uint32_t theBank, uint32_t theInstrNum, uint32_t spcDirAddr, bool percussion, const std::wstring& name) :
	VGMInstr(instrSet, offset, KonamiSnesInstr::ExpectedSize(ver), theBank, theInstrNum, name),
	spcDirAddr(spcDirAddr),
	percussion(percussion)
{
}

KonamiSnesInstr::~KonamiSnesInstr()
{
}

bool KonamiSnesInstr::LoadInstr()
{
	// TODO: percussive samples
	if (percussion) {
		return true;
	}

	uint8_t srcn = GetByte(dwOffset);
	uint32_t offDirEnt = spcDirAddr + (srcn * 4);
	if (offDirEnt + 4 > 0x10000)
	{
		return false;
	}

	uint16_t addrSampStart = GetShort(offDirEnt);

	KonamiSnesRgn * rgn = new KonamiSnesRgn(this, version, dwOffset, percussion);
	rgn->sampOffset = addrSampStart - spcDirAddr;
	aRgns.push_back(rgn);

	return true;
}

bool KonamiSnesInstr::IsValidHeader(RawFile * file, KonamiSnesVersion version, uint32_t addrInstrHeader, uint32_t spcDirAddr, bool validateSample)
{
	size_t instrItemSize = KonamiSnesInstr::ExpectedSize(version);

	if (addrInstrHeader + instrItemSize > 0x10000)
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
	if (!SNESSampColl::IsValidSampleDir(file, addrDIRentry, validateSample)) {
		return false;
	}

	uint16_t srcAddr = file->GetShort(addrDIRentry);
	uint16_t loopStartAddr = file->GetShort(addrDIRentry + 2);
	if (srcAddr > loopStartAddr || (loopStartAddr - srcAddr) % 9 != 0) {
		return false;
	}

	return true;
}

uint32_t KonamiSnesInstr::ExpectedSize(KonamiSnesVersion version)
{
	if (version == KONAMISNES_V1 || version == KONAMISNES_V2) {
		return 8;
	}
	else {
		return 7;
	}
}

// *************
// KonamiSnesRgn
// *************

KonamiSnesRgn::KonamiSnesRgn(KonamiSnesInstr* instr, KonamiSnesVersion ver, uint32_t offset, bool percussion) :
	VGMRgn(instr, offset, KonamiSnesInstr::ExpectedSize(ver))
{
	// TODO: percussive samples

	uint8_t srcn = GetByte(offset);
	int8_t key = GetByte(offset + 1);
	int8_t tuning = GetByte(offset + 2);
	uint8_t adsr1 = GetByte(offset + 3);
	uint8_t adsr2 = GetByte(offset + 4);
	uint8_t pan = GetByte(offset + 5);
	int8_t vol = GetByte(offset + 6);

	uint8_t gain = adsr2;
	bool use_adsr = ((adsr1 & 0x80) != 0);

	double fine_tuning;
	double coarse_tuning;
	const double pitch_fixer = log(4096.0 / 4286.0) / log(2); // from pitch table ($10be vs $1000)
	fine_tuning = modf((key + (tuning / 256.0)) + pitch_fixer, &coarse_tuning);

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
	AddUnityKey(71 - (int)(coarse_tuning), offset + 1, 1);
	AddFineTune((int16_t)(fine_tuning * 100.0), offset + 2, 1);
	AddSimpleItem(offset + 3, 1, L"ADSR1");
	AddSimpleItem(offset + 4, 1, use_adsr ? L"ADSR2" : L"GAIN");
	AddSimpleItem(offset + 5, 1, L"Pan");
	// volume is *decreased* by final volume value
	// so it is impossible to convert it in 100% accuracy
	// the following value 48.0 is chosen as a "average channel volume level"
	AddVolume(max(1.0 - (vol / 48.0), 0.0), offset + 6);
	SNESConvADSR<VGMRgn>(this, adsr1, adsr2, gain);
}

KonamiSnesRgn::~KonamiSnesRgn()
{
}

bool KonamiSnesRgn::LoadRgn()
{
	return true;
}
