#include "pch.h"
#include "WD.h"
#include "SquarePS2Format.h"
#include "PSXSPU.h"
#include "ScaleConversion.h"

using namespace std;

// **********
// WDInstrSet
// **********

WDInstrSet::WDInstrSet(RawFile* file, uint32_t offset)
: VGMInstrSet(SquarePS2Format::name, file, offset)
{
}

WDInstrSet::~WDInstrSet(void)
{
}


bool WDInstrSet::GetHeaderInfo()
{
	VGMHeader* header = AddHeader(dwOffset, 0x10, L"Header");
	header->AddSimpleItem(dwOffset + 0x2, 2, L"ID");
	header->AddSimpleItem(dwOffset + 0x4, 4, L"Sample Section Size");
	header->AddSimpleItem(dwOffset + 0x8, 4, L"Number of Instruments");
	header->AddSimpleItem(dwOffset + 0xC, 4, L"Number of Regions");

	id =					GetShort(0x2+dwOffset);
	dwSampSectSize =		GetWord(0x4+dwOffset);
	dwNumInstrs =			GetWord(0x8+dwOffset);
	dwTotalRegions =		GetWord(0xC+dwOffset);

	if (dwSampSectSize < 0x40)	//Some songs in the Bouncer have bizarre values here
		dwSampSectSize = 0;

	wostringstream	theName;
	theName << L"WD " << id;
	name = theName.str();

	uint32_t sampCollOff = dwOffset + GetWord(dwOffset + 0x20) + (dwTotalRegions * 0x20);
	//uint32_t sampCollOff = (((dwNumInstrs/4)+(dwNumInstrs%4 > 0))* 0x10) + dwTotalRegions * 0x20 + 0x20 + dwOffset;
	
	sampColl = new PSXSampColl(SquarePS2Format::name, this, sampCollOff, dwSampSectSize);
	unLength = sampCollOff+dwSampSectSize - dwOffset;

	return true;
}

bool WDInstrSet::GetInstrPointers()
{

	uint32_t j = 0x20+dwOffset;

	//check for bouncer WDs with 0xFFFFFFFF as the last instr pointer.  If it's there ignore it
	for (uint32_t i=0; i<dwNumInstrs; i++)
	{
		if (GetWord(j + i*4) == 0xFFFFFFFF)
			dwNumInstrs = i;
	}

	for (uint32_t i=0; i<dwNumInstrs; i++)
	{
		

		uint32_t instrLength;
		if (i != dwNumInstrs-1)	//while not the last instr
			instrLength = GetWord(j+((i+1)*4)) - GetWord(j+(i*4));
		else
			instrLength = sampColl->dwOffset - (GetWord(j+(i*4)) + dwOffset);
		wostringstream	name;
		name << L"Instrument " << i;
		WDInstr* newWDInstr = new WDInstr(this, dwOffset+GetWord(j+(i*4)), instrLength, 0, i, name.str());//strStr);
		aInstrs.push_back(newWDInstr);
		//newWDInstr->dwRelOffset = GetWord(j+(i*4));
	}
	return true;
}


// *******
// WDInstr
// *******


WDInstr::WDInstr(VGMInstrSet* instrSet, uint32_t offset, uint32_t length, uint32_t theBank, uint32_t theInstrNum, const wstring name)
 : 	VGMInstr(instrSet, offset, length, theBank, theInstrNum, name)
{
}

WDInstr::~WDInstr(void)
{
}


bool WDInstr::LoadInstr()
{
	wostringstream	strStr;
	uint32_t j=0;
	long startAddress = 0;
	bool notSampleStart = false;

	//Read region data 
	

	bool bSecondToLastRgn = 0;
	bool bLastRgn = 0;

	unsigned int k = 0;
	while (k*0x20 < unLength)
	{
		if (bSecondToLastRgn)
			bLastRgn = true;

		WDRgn* rgn = new WDRgn(this, k*0x20  + dwOffset);
		aRgns.push_back(rgn);

		rgn->AddSimpleItem(k*0x20 + dwOffset, 1, L"Stereo Region Flag");
		rgn->AddSimpleItem(k*0x20 + 1 + dwOffset, 1, L"First/Last Region Flags");
		rgn->AddSimpleItem(k*0x20 + 2 + dwOffset, 2, L"Unknown Flag");
		rgn->AddSimpleItem(k*0x20 + 0x4 + dwOffset, 4, L"Sample Offset");
		rgn->AddSimpleItem(k*0x20 + 0x8 + dwOffset, 4, L"Loop Start");
		rgn->AddSimpleItem(k*0x20 + 0xC + dwOffset, 2, L"ADSR1");
		rgn->AddSimpleItem(k*0x20 + 0xE + dwOffset, 2, L"ADSR2");
		rgn->AddSimpleItem(k*0x20 + 0x12 + dwOffset, 1, L"Finetune");
		rgn->AddSimpleItem(k*0x20 + 0x13 + dwOffset, 1, L"UnityKey");
		rgn->AddSimpleItem(k*0x20 + 0x14 + dwOffset, 1, L"Key High");
		rgn->AddSimpleItem(k*0x20 + 0x15 + dwOffset, 1, L"Velocity High");
		rgn->AddSimpleItem(k*0x20 + 0x16 + dwOffset, 1, L"Attenuation");
		rgn->AddSimpleItem(k*0x20 + 0x17 + dwOffset, 1, L"Pan");

		rgn->bStereoRegion =  GetByte(k*0x20 + dwOffset) & 0x1;
		rgn->bUnknownFlag2 =  GetByte(k*0x20 + 2 + dwOffset) & 0x1;
		rgn->bFirstRegion =  GetByte(k*0x20 + 1 + dwOffset) & 0x1;
		rgn->bLastRegion =  (GetByte(k*0x20 + 1 + dwOffset) & 0x2) >> 1;
		//rgn->sample_offset =  GetWord(k*0x20 + 0x4 + dwOffset);
		rgn->sampOffset = GetWord(k*0x20 + 0x4 + dwOffset) & 0xFFFFFFF0;	//The & is there because FFX points to 0x----C offsets for some very odd reason
		rgn->loop.loopStart =  GetWord(k*0x20 + 0x8 + dwOffset);
		rgn->ADSR1 =  GetShort(k*0x20 + 0xC + dwOffset);
		rgn->ADSR2 =  GetShort(k*0x20 + 0xE + dwOffset);
		rgn->fineTune =  GetByte(k*0x20 + 0x12 + dwOffset);
		rgn->unityKey =  0x3A - GetByte(k*0x20 + 0x13 + dwOffset);
		rgn->keyHigh =  GetByte(k*0x20 + 0x14 + dwOffset);
		rgn->velHigh = Convert7bitPercentVolValToStdMidiVal( GetByte(k*0x20 + 0x15 + dwOffset) );

		uint8_t vol = GetByte(k*0x20 + 0x16 + dwOffset);
		rgn->SetVolume((double)vol / 127.0);

		rgn->pan =  (double)GetByte(k*0x20 + 0x17 + dwOffset);		//need to convert

		if (rgn->pan == 255)
			rgn->pan = 1.0;
		else if (rgn->pan == 128)
			rgn->pan = 0;
		else if (rgn->pan == 192)
			rgn->pan = 0.5;
		else if (rgn->pan > 127)
			rgn->pan = (double)(rgn->pan-128)/(double)127;
		else
			rgn->pan = 0.5;

		rgn->fineTune = (short)ceil(((finetune_table[rgn->fineTune] - 0x10000) * 0.025766555011594949755217727389848) - 50);
		PSXConvADSR<WDRgn>(rgn, rgn->ADSR1, rgn->ADSR2, true);

		if (rgn->bLastRegion)
		{
			if (rgn->bStereoRegion)
				bSecondToLastRgn = true;
			else
				bLastRgn = true;
		}
		k++;
	}

	//First, do key and velocity ranges
	uint8_t prevKeyHigh = 0;
	uint8_t prevVelHigh = 0;
	for (uint32_t k=0; k<aRgns.size(); k++)
	{
		// Key Ranges
		if (((WDRgn*)aRgns[k])->bFirstRegion) //&& !instrument[i].region[k].bLastRegion) //used in ffx2 0049 YRP battle 1.  check out first instrument, flags are weird
			aRgns[k]->keyLow = 0;
		else if (k > 0)
		{
			if (aRgns[k]->keyHigh == aRgns[k-1]->keyHigh)
				aRgns[k]->keyLow = aRgns[k-1]->keyLow;//hDLSFile.aInstrs.back()->aRgns.back()->usKeyLow;
			else
				aRgns[k]->keyLow = aRgns[k-1]->keyHigh+1;
		}
		else
			aRgns[k]->keyLow = 0;

		if (((WDRgn*)aRgns[k])->bLastRegion)
			aRgns[k]->keyHigh = 0x7F;


		// Velocity ranges
		if (aRgns[k]->velHigh == prevVelHigh)
			aRgns[k]->velLow = aRgns[k-1]->velLow;
		else
			aRgns[k]->velLow = prevVelHigh + 1;
		prevVelHigh = aRgns[k]->velHigh;

		if (k == 0) //if it's the first region of the instrument
			aRgns[k]->velLow = 0;
		else if (aRgns[k]->velHigh == aRgns[k-1]->velHigh)
		{
			aRgns[k]->velLow = aRgns[k-1]->velLow;
			aRgns[k]->velHigh = 0x7F;		//FFX 0022, aka sight of spira, was giving me problems, hence this
			aRgns[k-1]->velHigh = 0x7F; //hDLSFile.aInstrs.back()->aRgns.back()->usVelHigh = 0x7F;
		}
		else if (aRgns[k-1]->velHigh == 0x7F)
			aRgns[k]->velLow = 0;
		else
			aRgns[k]->velLow = aRgns[k-1]->velHigh+1;
	}
	return true;
}


// *****
// WDRgn
// *****

WDRgn::WDRgn(WDInstr* instr, uint32_t offset)
: VGMRgn(instr, offset, 0x20)
{
}
 