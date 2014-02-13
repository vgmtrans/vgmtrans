#include "stdafx.h"
#include "TriAcePS1InstrSet.h"
#include "TriAcePS1Format.h"
#include "VGMRgn.h"
#include "PSXSPU.h"
#include "ScaleConversion.h"
#include "Vab.h"

// *****************
// TriAcePS1InstrSet
// *****************

TriAcePS1InstrSet::TriAcePS1InstrSet(RawFile* file, uint32_t offset)
: VGMInstrSet(TriAcePS1Format::name, file, offset, 0, L"TriAce InstrSet")
{
}

TriAcePS1InstrSet::~TriAcePS1InstrSet(void)
{
}


//==============================================================
//		ヘッダー情報の取得
//--------------------------------------------------------------
//	Memo:
//		VGMInstrSet::Load()関数から呼ばれる
//==============================================================
bool TriAcePS1InstrSet::GetHeaderInfo()
{
	VGMHeader* header = AddHeader(dwOffset, sizeof(TriAcePS1InstrSet::_InstrHeader));	//1,Sep.2009 revise
	header->AddSimpleItem(dwOffset, 4, L"InstrSet Size");
	header->AddSimpleItem(dwOffset+4, 2, L"Instr Section Size");
	//header->AddSimpleItem(dwOffset+6, 1, L"Number of Instruments");

	//-----------------------
	//1,Sep.2009 revise		to do こうしたい。
	//-----------------------
	//	GetBytes(dwOffset, sizeof(TriAcePS1InstrSet::_InstrHeader), &InstrHeader);
	//		↑　↑　↑
	unLength =				GetWord(dwOffset);			// to do delete
	instrSectionSize =		GetShort(dwOffset+4);		// to do delete
	//-----------------------


	//sampColl = new TriAcePS1SampColl(this, dwOffset+instrSectionSize, unLength-instrSectionSize);
	sampColl = new PSXSampColl(TriAcePS1Format::name, this, dwOffset+instrSectionSize, unLength-instrSectionSize);


	return true;
}

//==============================================================
//		各音色の情報取得
//--------------------------------------------------------------
//	Memo:
//		VGMInstrSet::Load()関数から呼ばれる
//==============================================================
bool TriAcePS1InstrSet::GetInstrPointers()
{

	uint32_t firstWord = GetWord(dwOffset+sizeof(TriAcePS1InstrSet::_InstrHeader));		//1,Sep.2009 revise

	//0xFFFFFFFFになるまで繰り返す。
	for (uint32_t i = dwOffset+sizeof(TriAcePS1InstrSet::_InstrHeader);					//1,Sep.2009 revise
	  ((firstWord != 0xFFFFFFFF) && (i < dwOffset+unLength));
	  i+=sizeof(TriAcePS1Instr::InstrInfo), firstWord = GetWord(i))
	{
		TriAcePS1Instr* newInstr = new TriAcePS1Instr(this, i, 0, 0, 0);
		aInstrs.push_back(newInstr);
		GetBytes(i, sizeof(TriAcePS1Instr::InstrInfo), &newInstr->instrinfo);
		newInstr->AddSimpleItem(i+0,sizeof(short),L"Instrument Number");			//1,Sep.2009 revise
		newInstr->AddSimpleItem(i+2,sizeof(short),L"ADSR1");						//1,Sep.2009 revise
		newInstr->AddSimpleItem(i+4,sizeof(short),L"ADSR2");						//1,Sep.2009 revise
		newInstr->AddUnknownItem(i+6,sizeof(char));									//1,Sep.2009 revise
		newInstr->AddSimpleItem(i+7,sizeof(char),L"Number of Rgns");				//1,Sep.2009 revise
		i += sizeof(TriAcePS1Instr::RgnInfo) * (newInstr->instrinfo.numRgns);
	}
	return true;
}


// **************
// TriAcePS1Instr
// **************

TriAcePS1Instr::TriAcePS1Instr(VGMInstrSet* instrSet, uint32_t offset, uint32_t length, uint32_t theBank, uint32_t theInstrNum)
 : 	VGMInstr(instrSet, offset, length, theBank, theInstrNum),
	rgns(NULL)
{
}

//==============================================================
//		Make the Object "WdsRgn" (Attribute table)
//--------------------------------------------------------------
bool TriAcePS1Instr::LoadInstr()
{
	unLength = sizeof(InstrInfo) + sizeof(RgnInfo) * instrinfo.numRgns;

//	TriAcePS1InstrSet*	_parInstrSet	=	(TriAcePS1InstrSet*)parInstrSet;

	// Get the rgn data
	rgns = new RgnInfo[instrinfo.numRgns];
	GetBytes((dwOffset+sizeof(InstrInfo)),(sizeof(RgnInfo) * instrinfo.numRgns), rgns);

	// Set the program num and bank num, adjusting for values > 0x7F
	this->instrNum	=   instrinfo.progNum & 0x7F;										//1,Sep.2009 revise
	this->bank		= ((instrinfo.progNum + (instrinfo.bankNum << 8)) & 0xFF80) >> 7;	//1,Sep.2009 revise
//	this->instrNum = instrinfo.progNum;
//	if (this->instrNum > 0x7F)
//		instrNum -= 0x80;
//	this->bank = (instrinfo.bankNum*2);
//	if (instrinfo.progNum > 0x7F)
//		this->bank++;

	


	//ATLTRACE("LOADED INSTR   ProgNum: %X    BankNum: %X\n", instrinfo.progNum, instrinfo.bankNum);

	for (int i=0; i<instrinfo.numRgns; i++)
	{
		RgnInfo* rgninfo = &rgns[i];
		VGMRgn* rgn = new VGMRgn(this, dwOffset + sizeof(InstrInfo) + sizeof(RgnInfo) * i, sizeof(RgnInfo));		//1,Sep.2009 revise
		rgn->AddKeyLow (rgninfo->note_range_low, rgn->dwOffset);
		rgn->AddKeyHigh(rgninfo->note_range_high, rgn->dwOffset+1);
		rgn->AddVelLow (rgninfo->vel_range_low, rgn->dwOffset+2);
		rgn->AddVelHigh(rgninfo->vel_range_high, rgn->dwOffset+3);
		rgn->AddSimpleItem(rgn->dwOffset+4, 4, L"Sample Offset");
		rgn->sampOffset = rgninfo->sampOffset; //+ ((VGMInstrSet*)this->vgmfile)->sampColl->dwOffset;
		rgn->AddSimpleItem(rgn->dwOffset+8, 4, L"Sample Loop Point");
		//rgn->loop.loopStatus = (rgninfo->loopOffset != rgninfo->sampOffset) && (rgninfo->loopOffset != 0);
		rgn->loop.loopStart = rgninfo->loopOffset;
		rgn->AddSimpleItem(rgn->dwOffset+12, 1, L"Attenuation");
		rgn->AddUnityKey((int8_t)0x3B - rgninfo->pitchTuneSemitones, rgn->dwOffset+13);  //You would think it would be 0x3C (middle c)
		rgn->AddSimpleItem(rgn->dwOffset+14, 1, L"Pitch Fine Tune");
		rgn->fineTune =  (short)((double)rgninfo->pitchTuneFine / 1.27);
		rgn->sampCollPtr = ((VGMInstrSet*)this->vgmfile)->sampColl;
		rgn->AddSimpleItem(rgn->dwOffset+15, 5, L"Unknown values");
		

		PSXConvADSR(rgn, instrinfo.ADSR1, instrinfo.ADSR2, false);
		// Attenuation is not using a linear scale. Will need to look at the original code.  For now, it's ignored.
		//  also, be aware the same scale may be employed for vol and expression events (haven't investigated).
		//long dlsAtten = -(ConvertPercentVolToAttenDB(rgninfo->attenuation/((double)255)) * DLS_DECIBEL_UNIT);
		//rgn->SetAttenuation(dlsAtten);
		aRgns.push_back(rgn);
	}
	return true;
}
