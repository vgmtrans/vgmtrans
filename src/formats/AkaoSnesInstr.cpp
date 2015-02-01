#include "stdafx.h"
#include "AkaoSnesInstr.h"
#include "Format.h"
#include "SNESDSP.h"
#include "AkaoSnesFormat.h"

// ****************
// AkaoSnesInstrSet
// ****************

AkaoSnesInstrSet::AkaoSnesInstrSet(RawFile* file, AkaoSnesVersion ver, uint32_t spcDirAddr, uint16_t addrTuningTable, uint16_t addrADSRTable, const std::wstring & name) :
	VGMInstrSet(AkaoSnesFormat::name, file, min(addrTuningTable, addrADSRTable), 0, name), version(ver),
	spcDirAddr(spcDirAddr),
	addrTuningTable(addrTuningTable),
	addrADSRTable(addrADSRTable)
{
}

AkaoSnesInstrSet::~AkaoSnesInstrSet()
{
}

bool AkaoSnesInstrSet::GetHeaderInfo()
{
	return true;
}

bool AkaoSnesInstrSet::GetInstrPointers()
{
	usedSRCNs.clear();
	for (uint8_t srcn = 0; srcn <= 0x3f; srcn++)
	{
		uint32_t addrDIRentry = spcDirAddr + (srcn * 4);
		if (!SNESSampColl::IsValidSampleDir(rawfile, addrDIRentry, true)) {
			continue;
		}

		uint16_t addrSampStart = GetShort(addrDIRentry);
		if (addrSampStart < spcDirAddr) {
			continue;
		}

		uint32_t ofsTuningEntry;
		if (version == AKAOSNES_V2) {
			ofsTuningEntry = addrTuningTable + srcn;
		}
		else {
			ofsTuningEntry = addrTuningTable + srcn * 2;
		}

		if (ofsTuningEntry + 2 > 0x10000) {
			break;
		}

		if (GetShort(ofsTuningEntry) == 0xffff) {
			continue;
		}

		uint32_t ofsADSREntry = addrADSRTable + srcn * 2;
		if (addrADSRTable + 2 > 0x10000) {
			break;
		}

		if (GetShort(ofsADSREntry) == 0x0000) {
			break;
		}

		usedSRCNs.push_back(srcn);

		std::wostringstream instrName;
		instrName << L"Instrument " << srcn;
		AkaoSnesInstr * newInstr = new AkaoSnesInstr(this, version, srcn, spcDirAddr, addrTuningTable, addrADSRTable, instrName.str());
		aInstrs.push_back(newInstr);
	}
	if (aInstrs.size() == 0)
	{
		return false;
	}

	std::sort(usedSRCNs.begin(), usedSRCNs.end());
	SNESSampColl * newSampColl = new SNESSampColl(AkaoSnesFormat::name, this->rawfile, spcDirAddr, usedSRCNs);
	if (!newSampColl->LoadVGMFile())
	{
		delete newSampColl;
		return false;
	}

	return true;
}

// *************
// AkaoSnesInstr
// *************

AkaoSnesInstr::AkaoSnesInstr(VGMInstrSet* instrSet, AkaoSnesVersion ver, uint8_t srcn, uint32_t spcDirAddr, uint16_t addrTuningTable, uint16_t addrADSRTable, const std::wstring& name) :
	VGMInstr(instrSet, min(addrTuningTable, addrADSRTable), 0, 0, srcn, name), version(ver),
	spcDirAddr(spcDirAddr),
	addrTuningTable(addrTuningTable),
	addrADSRTable(addrADSRTable)
{
}

AkaoSnesInstr::~AkaoSnesInstr()
{
}

bool AkaoSnesInstr::LoadInstr()
{
	uint32_t offDirEnt = spcDirAddr + (instrNum * 4);
	if (offDirEnt + 4 > 0x10000)
	{
		return false;
	}

	uint16_t addrSampStart = GetShort(offDirEnt);

	AkaoSnesRgn * rgn = new AkaoSnesRgn(this, version, instrNum, spcDirAddr, addrTuningTable, addrADSRTable);
	rgn->sampOffset = addrSampStart - spcDirAddr;
	aRgns.push_back(rgn);

	SetGuessedLength();
	return true;
}

// ***********
// AkaoSnesRgn
// ***********

AkaoSnesRgn::AkaoSnesRgn(AkaoSnesInstr* instr, AkaoSnesVersion ver, uint8_t srcn, uint32_t spcDirAddr, uint16_t addrTuningTable, uint16_t addrADSRTable) :
	VGMRgn(instr, min(addrTuningTable, addrADSRTable), 0),
	version(ver)
{
	uint8_t adsr1 = GetByte(addrADSRTable + srcn * 2);
	uint8_t adsr2 = GetByte(addrADSRTable + srcn * 2 + 1);

	uint8_t tuning1;
	uint8_t tuning2;
	if (version == AKAOSNES_V2) {
		tuning1 = GetByte(addrTuningTable + srcn);
		tuning2 = 0;
	}
	else {
		tuning1 = GetByte(addrTuningTable + srcn * 2);
		tuning2 = GetByte(addrTuningTable + srcn * 2 + 1);
	}

	double pitch_scale;
	if (tuning1 <= 0x7f) {
		pitch_scale = 1.0 + (tuning1 / 256.0);
	}
	else {
		pitch_scale = tuning1 / 256.0;
	}
	pitch_scale += tuning2 / 65536.0;

	double fine_tuning;
	double coarse_tuning;
	fine_tuning = modf((log(pitch_scale) / log(2.0)) * 12.0, &coarse_tuning);

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

	sampNum = srcn;
	AddSimpleItem(addrADSRTable + srcn * 2, 1, L"ADSR1");
	AddSimpleItem(addrADSRTable + srcn * 2 + 1, 1, L"ADSR2");
	unityKey = 69 - (int)(coarse_tuning);
	fineTune = (int16_t)(fine_tuning * 100.0);
	if (version == AKAOSNES_V2) {
		AddSimpleItem(addrTuningTable + srcn, 1, L"Tuning");
	}
	else {
		AddSimpleItem(addrTuningTable + srcn * 2, 2, L"Tuning");
	}
	SNESConvADSR<VGMRgn>(this, adsr1, adsr2, 0);

	SetGuessedLength();
}

AkaoSnesRgn::~AkaoSnesRgn()
{
}

bool AkaoSnesRgn::LoadRgn()
{
	return true;
}
