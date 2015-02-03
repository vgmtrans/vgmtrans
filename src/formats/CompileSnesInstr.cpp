#include "stdafx.h"
#include "CompileSnesInstr.h"
#include "Format.h"
#include "SNESDSP.h"
#include "CompileSnesFormat.h"

// *******************
// CompileSnesInstrSet
// *******************

CompileSnesInstrSet::CompileSnesInstrSet(RawFile* file, CompileSnesVersion ver, uint32_t offset, uint32_t spcDirAddr, const std::wstring & name) :
	VGMInstrSet(CompileSnesFormat::name, file, offset, 0, name), version(ver),
	spcDirAddr(spcDirAddr)
{
}

CompileSnesInstrSet::~CompileSnesInstrSet()
{
}

bool CompileSnesInstrSet::GetHeaderInfo()
{
	return true;
}

bool CompileSnesInstrSet::GetInstrPointers()
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

		uint32_t ofsInstrEntry = dwOffset + (srcn * 2);
		if (ofsInstrEntry + 2 > 0x10000) {
			break;
		}

		uint8_t pitchTableIndex = GetByte(ofsInstrEntry + 1);
		if (pitchTableIndex >= 0x80) {
			// apparently it's too large
			continue;
		}

		usedSRCNs.push_back(srcn);

		std::wostringstream instrName;
		instrName << L"Instrument " << srcn;
		CompileSnesInstr * newInstr = new CompileSnesInstr(this, version, ofsInstrEntry, srcn, spcDirAddr, instrName.str());
		aInstrs.push_back(newInstr);
	}
	if (aInstrs.size() == 0)
	{
		return false;
	}

	std::sort(usedSRCNs.begin(), usedSRCNs.end());
	SNESSampColl * newSampColl = new SNESSampColl(CompileSnesFormat::name, this->rawfile, spcDirAddr, usedSRCNs);
	if (!newSampColl->LoadVGMFile())
	{
		delete newSampColl;
		return false;
	}

	return true;
}

// ****************
// CompileSnesInstr
// ****************

CompileSnesInstr::CompileSnesInstr(VGMInstrSet* instrSet, CompileSnesVersion ver, uint32_t offset, uint8_t srcn, uint32_t spcDirAddr, const std::wstring& name) :
	VGMInstr(instrSet, offset, 2, 0, srcn, name), version(ver),
	spcDirAddr(spcDirAddr)
{
}

CompileSnesInstr::~CompileSnesInstr()
{
}

bool CompileSnesInstr::LoadInstr()
{
	uint32_t offDirEnt = spcDirAddr + (instrNum * 4);
	if (offDirEnt + 4 > 0x10000) {
		return false;
	}

	uint16_t addrSampStart = GetShort(offDirEnt);

	CompileSnesRgn * rgn = new CompileSnesRgn(this, version, dwOffset, instrNum, spcDirAddr);
	rgn->sampOffset = addrSampStart - spcDirAddr;
	aRgns.push_back(rgn);

	return true;
}

// **************
// CompileSnesRgn
// **************

CompileSnesRgn::CompileSnesRgn(CompileSnesInstr* instr, CompileSnesVersion ver, uint32_t offset, uint8_t srcn, uint32_t spcDirAddr) :
	VGMRgn(instr, offset, 2),
	version(ver)
{

	int8_t transpose = GetByte(dwOffset);
	uint8_t pitchTableIndex = GetByte(dwOffset + 1);

	AddUnityKey(72 + transpose, dwOffset, 1);
	AddSimpleItem(dwOffset + 1, 1, L"Pitch Table Index");

	uint8_t adsr1 = 0xff;
	uint8_t adsr2 = 0xe0;
	SNESConvADSR<VGMRgn>(this, adsr1, adsr2, 0);
}

CompileSnesRgn::~CompileSnesRgn()
{
}

bool CompileSnesRgn::LoadRgn()
{
	return true;
}
