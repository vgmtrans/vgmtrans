#include "stdafx.h"
#include "Vab.h"
#include "Format.h"			//include PS1-specific format header file when it is ready
#include "PSXSpu.h"
#include "PS1Format.h"

Vab::Vab(RawFile* file, ULONG offset)
: VGMInstrSet(PS1Format::name, file, offset)
{
}

Vab::~Vab(void)
{
}


bool Vab::GetHeaderInfo()
{
	UINT nEndOffset = GetEndOffset();
	UINT nMaxLength = nEndOffset - dwOffset;

	if (nMaxLength < 0x20)
	{
		return false;
	}

	name = L"VAB";

	VGMHeader* vabHdr = AddHeader(dwOffset, 0x20, L"VAB Header");
	vabHdr->AddSimpleItem(dwOffset + 0x00, 4, L"ID");
	vabHdr->AddSimpleItem(dwOffset + 0x04, 4, L"Version");
	vabHdr->AddSimpleItem(dwOffset + 0x08, 4, L"VAB ID");
	vabHdr->AddSimpleItem(dwOffset + 0x0c, 4, L"Total Size");
	vabHdr->AddSimpleItem(dwOffset + 0x10, 2, L"Reserved");
	vabHdr->AddSimpleItem(dwOffset + 0x12, 2, L"Number of Programs");
	vabHdr->AddSimpleItem(dwOffset + 0x14, 2, L"Number of Tones");
	vabHdr->AddSimpleItem(dwOffset + 0x16, 2, L"Number of VAGs");
	vabHdr->AddSimpleItem(dwOffset + 0x18, 1, L"Master Volume");
	vabHdr->AddSimpleItem(dwOffset + 0x19, 1, L"Master Pan");
	vabHdr->AddSimpleItem(dwOffset + 0x1a, 1, L"Bank Attributes 1");
	vabHdr->AddSimpleItem(dwOffset + 0x1b, 1, L"Bank Attributes 2");
	vabHdr->AddSimpleItem(dwOffset + 0x1c, 4, L"Reserved");

	GetBytes(dwOffset, 0x20, &hdr);

//	ULONG sampCollOff = (((dwNumInstrs/4)+(dwNumInstrs%4 > 0))* 0x10) + dwTotalRegions * 0x20 + 0x20;
//	sampColl = new WDSampColl(this, sampCollOff, dwSampSectSize);


//	unLength = 0x9000;

//	ULONG sampCollOff = dwOffset+0x20 + 128*0x10 + hdr.ps*16*0x20;
//	sampColl = new VabSampColl(this, sampCollOff, 0, hdr.vs);
	
//	sampColl->Load();

	return true;
}

bool Vab::GetInstrPointers()
{
	UINT nEndOffset = GetEndOffset();
	UINT nMaxLength = nEndOffset - dwOffset;

	ULONG offProgs = dwOffset + 0x20;
	ULONG offToneAttrs = offProgs + (16 * 128);

	USHORT numPrograms = GetShort(dwOffset + 0x12);
	USHORT numVAGs = GetShort(dwOffset + 0x16);

	ULONG offVAGOffsets = offToneAttrs + (32 * 16 * numPrograms);

	VGMHeader* progsHdr = AddHeader(offProgs, 16 * 128, L"Program Table");
	VGMHeader* toneAttrsHdr = AddHeader(offToneAttrs, 32 * 16, L"Tone Attributes Table");

	if (numPrograms > 128)
	{
		return false;
	}
	if (numVAGs > 255)
	{
		return false;
	}

	for (ULONG i = 0; i < numPrograms; i++)
	{
		ULONG offCurrProg = offProgs + (i * 16);
		ULONG offCurrToneAttrs = offToneAttrs + (i * 32 * 16);

		if (nEndOffset < offCurrToneAttrs + (32 * 16))
		{
			break;
		}

		BYTE numTones = GetByte(offCurrProg);
		if (numTones != 0 && numTones <= 32)
		{
			VabInstr* newInstr = new VabInstr(this, offCurrToneAttrs, 0x20 * 16, 0, i);
			aInstrs.push_back(newInstr);
			GetBytes(offCurrProg, 0x10, &newInstr->attr);

			VGMHeader* hdr = progsHdr->AddHeader(offCurrProg, 0x10, L"Program");
			hdr->AddSimpleItem(offCurrProg + 0x00, 1, L"Number of Tones");
			hdr->AddSimpleItem(offCurrProg + 0x01, 1, L"Volume");
			hdr->AddSimpleItem(offCurrProg + 0x02, 1, L"Priority");
			hdr->AddSimpleItem(offCurrProg + 0x03, 1, L"Mode");
			hdr->AddSimpleItem(offCurrProg + 0x04, 1, L"Pan");
			hdr->AddSimpleItem(offCurrProg + 0x05, 1, L"Reserved");
			hdr->AddSimpleItem(offCurrProg + 0x06, 2, L"Attribute");
			hdr->AddSimpleItem(offCurrProg + 0x08, 4, L"Reserved");
			hdr->AddSimpleItem(offCurrProg + 0x0c, 4, L"Reserved");

			newInstr->masterVol = GetByte(offCurrProg + 0x01);

			toneAttrsHdr->unLength = offCurrToneAttrs + (32 * 16) - offToneAttrs;
		}
	}

	if ((offVAGOffsets + 2 * 256) <= nEndOffset)
	{
		wchar_t name[256];
		std::vector<SizeOffsetPair> vagLocations;
		ULONG totalVAGSize = 0;
		VGMHeader* vagOffsetHdr = AddHeader(offVAGOffsets, 2 * 256, L"VAG Pointer Table");

		ULONG vagStartOffset = GetShort(offVAGOffsets) * 8;
		vagOffsetHdr->AddSimpleItem(offVAGOffsets, 2, L"VAG Size /8 #0");
		totalVAGSize = vagStartOffset;

		for (ULONG i = 0; i < numVAGs; i++)
		{
			ULONG vagOffset;
			ULONG vagSize;

			if (i == 0)
			{
				vagOffset = vagStartOffset;
				vagSize = GetShort(offVAGOffsets + (i + 1) * 2) * 8;
			}
			else
			{
				vagOffset = vagStartOffset + vagLocations[i - 1].offset + vagLocations[i - 1].size;
				vagSize = GetShort(offVAGOffsets + (i + 1) * 2) * 8;
			}

			wsprintf(name,  L"VAG Size /8 #%u", i + 1);
			vagOffsetHdr->AddSimpleItem(offVAGOffsets + (i + 1) * 2, 2, name);

			if (vagOffset + vagSize <= nEndOffset)
			{
				vagLocations.push_back(SizeOffsetPair(vagOffset, vagSize));
				totalVAGSize += vagSize;
			}
			else
			{
				wchar_t log[512];
				wsprintf(log,  L"VAG #%u pointer (offset=0x%08X, size=%u) is invalid.", i + 1, vagOffset, vagSize);
				pRoot->AddLogItem(new LogItem(log, LOG_LEVEL_WARN, L"Vab"));
			}
		}
		unLength = (offVAGOffsets + 2 * 256) - dwOffset;

		// single VAB file?
		ULONG offVAGs = offVAGOffsets + 2 * 256;
		if (GetStartOffset() == 0 && vagLocations.size() != 0)
		{
			// load samples as well
			PSXSampColl* newSampColl = new PSXSampColl(format, this, offVAGs, totalVAGSize, vagLocations);
			if (newSampColl->LoadVGMFile())
			{
				pRoot->AddVGMFile(newSampColl);
				//this->sampColl = newSampColl;
			}
			else
			{
				delete newSampColl;
			}
		}
	}

	return true;
}






// ********
// VabInstr
// ********

VabInstr::VabInstr(VGMInstrSet* instrSet, ULONG offset, ULONG length, ULONG theBank, ULONG theInstrNum, const wstring& name)
 : 	VGMInstr(instrSet, offset, length, theBank, theInstrNum, name),
	masterVol(127)
{
}

VabInstr::~VabInstr(void)
{
}



bool VabInstr::LoadInstr()
{
	char numRgns = attr.tones;
	for (int i = 0; i < numRgns; i++)
	{
		VabRgn* rgn = new VabRgn(this, dwOffset+i*0x20);
		if (!rgn->LoadRgn())
		{
			delete rgn;
			return false;
		}
		aRgns.push_back(rgn);
	}
	return TRUE;
}





// ******
// VabRgn
// ******

VabRgn::VabRgn(VabInstr* instr, ULONG offset)
: VGMRgn(instr, offset)
{
}
 


bool VabRgn::LoadRgn()
{
	VabInstr* instr = (VabInstr*) parInstr;
	unLength = 0x20;
	GetBytes(dwOffset, 0x20, &attr);

	AddGeneralItem(dwOffset, 1, L"Priority");
	AddGeneralItem(dwOffset+1, 1, L"Mode (use reverb?)");
	//AddGeneralItem(dwOffset+2, 1, L"Volume");
	AddVolume( (GetByte(dwOffset+2) * instr->masterVol)  / (127.0 * 127.0), dwOffset+2, 1);
	AddPan(GetByte(dwOffset+3), dwOffset+3);
	AddUnityKey(GetByte(dwOffset+4), dwOffset+4);
	AddGeneralItem(dwOffset+5, 1, L"Pitch Tune");
	AddKeyLow(GetByte(dwOffset+6), dwOffset+6);
	AddKeyHigh(GetByte(dwOffset+7), dwOffset+7);
	AddGeneralItem(dwOffset+8, 1, L"Vibrato Width");
	AddGeneralItem(dwOffset+9, 1, L"Vibrato Time");
	AddGeneralItem(dwOffset+10, 1, L"Portamento Width");
	AddGeneralItem(dwOffset+11, 1, L"Portamento Holding Time");
	AddGeneralItem(dwOffset+12, 1, L"Pitch Bend Min");
	AddGeneralItem(dwOffset+13, 1, L"Pitch Bend Max");
	AddGeneralItem(dwOffset+14, 1, L"Reserved");
	AddGeneralItem(dwOffset+15, 1, L"Reserved");
	AddGeneralItem(dwOffset+16, 2, L"ADSR1");
	AddGeneralItem(dwOffset+18, 2, L"ADSR2");
	AddGeneralItem(dwOffset+20, 2, L"Parent Program");
	AddSampNum(GetShort(dwOffset+22)-1, dwOffset+22, 2);
	AddGeneralItem(dwOffset+24, 2, L"Reserved");
	AddGeneralItem(dwOffset+26, 2, L"Reserved");
	AddGeneralItem(dwOffset+28, 2, L"Reserved");
	AddGeneralItem(dwOffset+30, 2, L"Reserved");
	ADSR1 = attr.adsr1;
	ADSR2 = attr.adsr2;
	if ((int)sampNum < 0)
		sampNum = 0;

	if (keyLow > keyHigh)
		return false;

	signed char ft = (signed char)GetByte(dwOffset+5);
	
	double cents = (double)ft;//((double)ft/(double)127) * 100.0;
	
	//ineTune
	//short ft = art->fineTune;
	//		if (ft < 0)
	//			ft += 0x8000;
	//		double freq_multiplier = (double) (((ft * 32)  + 0x100000) / (double)0x100000);  //this gives us the pitch multiplier value ex. 1.05946
	//		double cents = log(freq_multiplier)/log((double)2)*1200;
	//		if (art->fineTune < 0)
	//			cents -= 1200;
	//		rgn->fineTune = cents;
	SetFineTune((short)cents);

	PSXConvADSR<VabRgn>(this, ADSR1, ADSR2, false);
	return true;
}
