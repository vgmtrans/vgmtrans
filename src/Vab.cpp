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
	name = L"VAB";

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
	ULONG j = 0x20 + dwOffset;

	VGMHeader* instrptrHdr = AddHeader(j, 128, L"Instrument Pointers");

	for (int i = 0; i < 128; i++)
	{
		BYTE numTones = GetByte(j);
		if (numTones != 0 && numTones <= 32)
		{
			VabInstr* newInstr = new VabInstr(this, dwOffset+0x20+128*0x10 + aInstrs.size()*0x20*16 , 0x20*16, 0, i);
			aInstrs.push_back(newInstr);
			GetBytes(j, 0x10, &newInstr->attr);

			VGMHeader* hdr = instrptrHdr->AddHeader(j, 10, L"Pointer");
			hdr->AddSimpleItem(j, 1, L"Number of Tones");
			hdr->AddSimpleItem(j+1, 1, L"Master Volume");
			hdr->AddSimpleItem(j+2, 1, L"Mode");
			hdr->AddSimpleItem(j+3, 1, L"Master Pan");
			hdr->AddSimpleItem(j+4, 1, L"Reserved");
			hdr->AddSimpleItem(j+5, 2, L"Attribute");
			hdr->AddSimpleItem(j+7, 2, L"Reserved");
			hdr->AddSimpleItem(j+9, 2, L"Reserved");

			newInstr->masterVol = GetByte(j+1);
		}
		j += 0x10;
	}

	instrptrHdr->unLength = j - instrptrHdr->dwOffset;

	return true;
}






// ********
// VabInstr
// ********

VabInstr::VabInstr(VGMInstrSet* instrSet, ULONG offset, ULONG length, ULONG theBank, ULONG theInstrNum)
 : 	VGMInstr(instrSet, offset, length, theBank, theInstrNum),
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
			return false;
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
	fineTune = (short)cents;

	PSXConvADSR<VabRgn>(this, ADSR1, ADSR2, false);
	return true;
}
