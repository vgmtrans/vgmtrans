#include "pch.h"
#include "TamSoftPS1Instr.h"
#include "Format.h"
#include "PSXSPU.h"
#include "TamSoftPS1Format.h"

// ******************
// TamSoftPS1InstrSet
// ******************

TamSoftPS1InstrSet::TamSoftPS1InstrSet(RawFile* file, uint32_t offset, const std::wstring & name) :
	VGMInstrSet(TamSoftPS1Format::name, file, offset, 0, name)
{
}

TamSoftPS1InstrSet::~TamSoftPS1InstrSet()
{
}

bool TamSoftPS1InstrSet::GetHeaderInfo()
{
	if (dwOffset + 0x800 > vgmfile->GetEndOffset()) {
		return false;
	}

	uint32_t sampCollSize = GetWord(0x3fc);
	if (dwOffset + 0x800 + sampCollSize > vgmfile->GetEndOffset()) {
		return false;
	}
	unLength = 0x800 + sampCollSize;

	return true;
}

bool TamSoftPS1InstrSet::GetInstrPointers()
{
	std::vector<SizeOffsetPair> vagLocations;

	for (uint32_t instrNum = 0; instrNum < 256; instrNum++) {
		bool vagLoop;
		uint32_t vagOffset = 0x800 + GetWord(dwOffset + 4 * instrNum);
		if (vagOffset < unLength) {
			SizeOffsetPair vagLocation(vagOffset - 0x800, PSXSamp::GetSampleLength(rawfile, vagOffset, dwOffset + unLength, vagLoop));
			vagLocations.push_back(vagLocation);

			std::wstringstream instrName;
			instrName << L"Instrument " << instrNum;
			TamSoftPS1Instr * newInstr = new TamSoftPS1Instr(this, instrNum, instrName.str());
			aInstrs.push_back(newInstr);
		}
	}

	if (aInstrs.size() == 0)
	{
		return false;
	}

	PSXSampColl * newSampColl = new PSXSampColl(TamSoftPS1Format::name, this, dwOffset + 0x800, unLength - 0x800, vagLocations);
	if (!newSampColl->LoadVGMFile()) {
		delete newSampColl;
		return false;
	}

	return true;
}

// ***************
// TamSoftPS1Instr
// ***************

TamSoftPS1Instr::TamSoftPS1Instr(TamSoftPS1InstrSet * instrSet, uint8_t instrNum, const std::wstring& name) :
	VGMInstr(instrSet, instrSet->dwOffset + 4 * instrNum, 0x400 + 4, 0, instrNum, name)
{
}

TamSoftPS1Instr::~TamSoftPS1Instr()
{
}

bool TamSoftPS1Instr::LoadInstr()
{
	AddSimpleItem(dwOffset, 4, L"Sample Offset");

	TamSoftPS1Rgn * rgn = new TamSoftPS1Rgn(this, dwOffset + 0x400);
	rgn->sampNum = instrNum;
	aRgns.push_back(rgn);
	return true;
}

// *************
// TamSoftPS1Rgn
// *************

//const uint16_t TAMSOFTPS1_PITCH_TABLE[73] = {
//	0x0100, 0x010F, 0x011F, 0x0130, 0x0142, 0x0155, 0x016A, 0x017F,
//	0x0196, 0x01AE, 0x01C8, 0x01E3, 0x0200, 0x021E, 0x023E, 0x0260,
//	0x0285, 0x02AB, 0x02D4, 0x02FF, 0x032C, 0x035D, 0x0390, 0x03C6,
//	0x0400, 0x043C, 0x047D, 0x04C1, 0x050A, 0x0556, 0x05A8, 0x05FE,
//	0x0659, 0x06BA, 0x0720, 0x078D, 0x0800, 0x0879, 0x08FA, 0x0983,
//	0x0A14, 0x0AAD, 0x0B50, 0x0BFC, 0x0CB2, 0x0D74, 0x0E41, 0x0F1A,
//	0x1000, 0x10F3, 0x11F5, 0x1306, 0x1428, 0x155B, 0x16A0, 0x17F9,
//	0x1965, 0x1AE8, 0x1C82, 0x1E34, 0x2000, 0x21E7, 0x23EB, 0x260D,
//	0x2851, 0x2AB7, 0x2D41, 0x2FF2, 0x32CB, 0x35D1, 0x3904, 0x3C68,
//	0x3FFF,
//};

TamSoftPS1Rgn::TamSoftPS1Rgn(TamSoftPS1Instr* instr, uint32_t offset) :
	VGMRgn(instr, offset, 4)
{
	unityKey = TAMSOFTPS1_KEY_OFFSET + 48;
	AddSimpleItem(offset, 2, L"ADSR1");
	AddSimpleItem(offset + 2, 2, L"ADSR2");

	uint16_t adsr1 = GetShort(offset);
	uint16_t adsr2 = GetShort(offset + 2);
	PSXConvADSR<TamSoftPS1Rgn>(this, adsr1, adsr2, false);
}

TamSoftPS1Rgn::~TamSoftPS1Rgn()
{
}

bool TamSoftPS1Rgn::LoadRgn()
{
	return true;
}
