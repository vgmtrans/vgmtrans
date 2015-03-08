#include "pch.h"
#include "NinSnesInstr.h"
#include "Format.h"
#include "SNESDSP.h"
#include "NinSnesFormat.h"

// ****************
// NinSnesInstrSet
// ****************

NinSnesInstrSet::NinSnesInstrSet(RawFile* file, NinSnesVersion ver, uint32_t offset, uint32_t spcDirAddr, const std::wstring & name) :
		VGMInstrSet(NinSnesFormat::name, file, offset, 0, name), version(ver),
		spcDirAddr(spcDirAddr),
		konamiTuningTableAddress(0),
		konamiTuningTableSize(0)
{
}

NinSnesInstrSet::~NinSnesInstrSet()
{
}

bool NinSnesInstrSet::GetHeaderInfo()
{
	return true;
}

bool NinSnesInstrSet::GetInstrPointers()
{
	uint8_t instr_max = 0x7f;
	if (version == NINSNES_HUMAN) {
		instr_max = 0x200 / NinSnesInstr::ExpectedSize(version);
	}

	usedSRCNs.clear();
	for (int instr = 0; instr <= instr_max; instr++)
	{
		uint32_t instrItemSize = NinSnesInstr::ExpectedSize(version);
		uint32_t addrInstrHeader = dwOffset + (instrItemSize * instr);
		if (addrInstrHeader + instrItemSize > 0x10000)
		{
			return false;
		}

		// skip blank slot (Kirby Super Star)
		if (version != NINSNES_EARLIER) {
			if (GetByte(addrInstrHeader) == 0xff && GetByte(addrInstrHeader + 1) == 0xff && GetByte(addrInstrHeader + 2) == 0xff &&
					GetByte(addrInstrHeader + 3) == 0xff && GetByte(addrInstrHeader + 4) == 0xff && GetByte(addrInstrHeader + 5) == 0xff)
			{
				continue;
			}
		}

		uint8_t srcn = GetByte(addrInstrHeader);

		uint32_t offDirEnt = spcDirAddr + (srcn * 4);
		if (offDirEnt + 4 > 0x10000) {
			break;
		}

		uint16_t addrSampStart = GetShort(offDirEnt);
		uint16_t addrLoopStart = GetShort(offDirEnt + 2);

		if (addrSampStart == 0x0000 && addrLoopStart == 0x0000 ||
				addrSampStart == 0xffff && addrLoopStart == 0xffff) {
			// example: Lemmings - Stage Clear (00 00 00 00)
			// example: Yoshi's Island - Bowser (ff ff ff ff)
			continue;
		}
		if (!NinSnesInstr::IsValidHeader(this->rawfile, version, addrInstrHeader, spcDirAddr, false)) {
			break;
		}
		if (!NinSnesInstr::IsValidHeader(this->rawfile, version, addrInstrHeader, spcDirAddr, true)) {
			continue;
		}

		if (version == NINSNES_EARLIER || version == NINSNES_STANDARD) {
			if (addrSampStart < offDirEnt + 4) {
				continue;
			}
		}

		std::vector<uint8_t>::iterator itrSRCN = find(usedSRCNs.begin(), usedSRCNs.end(), srcn);
		if (itrSRCN == usedSRCNs.end())
		{
			usedSRCNs.push_back(srcn);
		}

		std::wostringstream instrName;
		instrName << L"Instrument " << instr;
		NinSnesInstr * newInstr = new NinSnesInstr(this, version, addrInstrHeader, instr >> 7, instr & 0x7f, spcDirAddr, instrName.str());
		newInstr->konamiTuningTableAddress = konamiTuningTableAddress;
		newInstr->konamiTuningTableSize = konamiTuningTableSize;
		aInstrs.push_back(newInstr);
	}
	if (aInstrs.size() == 0)
	{
		return false;
	}

	std::sort(usedSRCNs.begin(), usedSRCNs.end());
	SNESSampColl * newSampColl = new SNESSampColl(NinSnesFormat::name, this->rawfile, spcDirAddr, usedSRCNs);
	if (!newSampColl->LoadVGMFile())
	{
		delete newSampColl;
		return false;
	}

	return true;
}

// *************
// NinSnesInstr
// *************

NinSnesInstr::NinSnesInstr(VGMInstrSet* instrSet, NinSnesVersion ver, uint32_t offset, uint32_t theBank, uint32_t theInstrNum, uint32_t spcDirAddr, const std::wstring& name) :
		VGMInstr(instrSet, offset, NinSnesInstr::ExpectedSize(ver), theBank, theInstrNum, name), version(ver),
		spcDirAddr(spcDirAddr),
		konamiTuningTableAddress(0),
		konamiTuningTableSize(0)
{
}

NinSnesInstr::~NinSnesInstr()
{
}

bool NinSnesInstr::LoadInstr()
{
	uint8_t srcn = GetByte(dwOffset);
	uint32_t offDirEnt = spcDirAddr + (srcn * 4);
	if (offDirEnt + 4 > 0x10000)
	{
		return false;
	}

	uint16_t addrSampStart = GetShort(offDirEnt);

	NinSnesRgn * rgn = new NinSnesRgn(this, version, dwOffset, konamiTuningTableAddress, konamiTuningTableSize);
	rgn->sampOffset = addrSampStart - spcDirAddr;
	aRgns.push_back(rgn);

	return true;
}

bool NinSnesInstr::IsValidHeader(RawFile * file, NinSnesVersion version, uint32_t addrInstrHeader, uint32_t spcDirAddr, bool validateSample)
{
	size_t instrItemSize = NinSnesInstr::ExpectedSize(version);

	if (addrInstrHeader + instrItemSize > 0x10000)
	{
		return false;
	}

	bool hasUniqueByte = false;
	for (size_t offset = 0; offset <= instrItemSize; offset++) {
		uint8_t theByte = file->GetByte(addrInstrHeader + offset);
		if (theByte != 0x00 && theByte != 0xff) {
			hasUniqueByte = true;
			break;
		}
	}
	if (!hasUniqueByte) {
		return false;
	}

	uint8_t srcn = file->GetByte(addrInstrHeader);
	uint8_t adsr1 = file->GetByte(addrInstrHeader + 1);
	uint8_t adsr2 = file->GetByte(addrInstrHeader + 2);
	uint8_t gain = file->GetByte(addrInstrHeader + 3);

	if (srcn >= 0x80 || (adsr1 == 0 && gain == 0))
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

uint32_t NinSnesInstr::ExpectedSize(NinSnesVersion version)
{
	if (version == NINSNES_EARLIER) {
		return 5;
	}
	else {
		return 6;
	}
}

// ***********
// NinSnesRgn
// ***********

NinSnesRgn::NinSnesRgn(NinSnesInstr* instr, NinSnesVersion ver, uint32_t offset, uint16_t konamiTuningTableAddress, uint8_t konamiTuningTableSize) :
		VGMRgn(instr, offset, NinSnesInstr::ExpectedSize(ver)), version(ver)
{
	uint8_t srcn = GetByte(offset);
	uint8_t adsr1 = GetByte(offset + 1);
	uint8_t adsr2 = GetByte(offset + 2);
	uint8_t gain = GetByte(offset + 3);
	int16_t pitch_scale;
	if (version == NINSNES_EARLIER) {
		pitch_scale = (int8_t)GetByte(offset + 4) * 256;
	}
	else {
		pitch_scale = GetShortBE(offset + 4);
	}

	const double pitch_fixer = 4286.0 / 4096.0;
	double fine_tuning;
	double coarse_tuning;
	fine_tuning = modf((log(pitch_scale * pitch_fixer / 256.0) / log(2.0)) * 12.0, &coarse_tuning);

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
	AddSimpleItem(offset + 1, 1, L"ADSR1");
	AddSimpleItem(offset + 2, 1, L"ADSR2");
	AddSimpleItem(offset + 3, 1, L"GAIN");
	if (version == NINSNES_EARLIER) {
		AddUnityKey(96 - (int)(coarse_tuning), offset + 4, 1);
		AddFineTune((int16_t)(fine_tuning * 100.0), offset + 4, 1);
	}
	else if (version == NINSNES_KONAMI && konamiTuningTableAddress != 0) {
		uint16_t addrTuningTableCoarse = konamiTuningTableAddress;
		uint16_t addrTuningTableFine = konamiTuningTableAddress + konamiTuningTableSize;

		int8_t coarse_tuning;
		uint8_t fine_tuning;
		if (srcn < konamiTuningTableSize) {
			coarse_tuning = GetByte(addrTuningTableCoarse + srcn);
			fine_tuning = GetByte(addrTuningTableFine + srcn);
		}
		else {
			coarse_tuning = 0;
			fine_tuning = 0;
		}

		double fine_tune_real = fine_tuning / 256.0;
		fine_tune_real += log(4045.0 / 4096.0) / log(2) * 12; // -21.691 cents

		unityKey = 71 - coarse_tuning;
		fineTune = (int16_t)(fine_tune_real * 100.0);

		AddSimpleItem(offset + 4, 2, L"Tuning (Unused)");
	}
	else {
		AddUnityKey(96 - (int)(coarse_tuning), offset + 4, 1);
		AddFineTune((int16_t)(fine_tuning * 100.0), offset + 5, 1);
	}
	SNESConvADSR<VGMRgn>(this, adsr1, adsr2, gain);
}

NinSnesRgn::~NinSnesRgn()
{
}

bool NinSnesRgn::LoadRgn()
{
	return true;
}
