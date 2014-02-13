#include "stdafx.h"
#include "QSoundInstr.h"
#include "QSoundFormat.h"
#include "VGMSamp.h"
#include "VGMRgn.h"
#include "ScaleConversion.h"

using namespace std;

// ****************
// QSoundArticTable
// ****************


QSoundArticTable::QSoundArticTable(RawFile* file, std::wstring& name, U32 offset, U32 length)
	: VGMMiscFile(QSoundFormat::name, file, offset, length, name)
{
}

QSoundArticTable::~QSoundArticTable(void)
{
	if (artics)
		delete[] artics;
}

bool QSoundArticTable::LoadMain()
{
	DWORD off = dwOffset;
	U32 test1=1, test2=1;
	//for (int i = 0; (test1 || test2)  && ((test1 != 0xFFFFFFFF) || (test2 != 0xFFFFFFFF)); i++, off += sizeof(qs_samp_info) )
	for (int i = 0; off < dwOffset+unLength; i++, off += sizeof(qs_samp_info))
	{
		test1 = GetWord(off);
		test2 = GetWord(off+4);
		if ( (test1 == 0 && test2 == 0) || (test1 == 0xFFFFFFFF && test2 == 0xFFFFFFFF))
			continue;


		wostringstream name;
		name << L"Articulation " << i;
		VGMContainerItem* containerItem = new VGMContainerItem(this, off, sizeof(qs_samp_info), name.str());
		containerItem->AddSimpleItem(off,   1, L"Attack Rate");
		containerItem->AddSimpleItem(off+1, 1, L"Decay Rate");
		containerItem->AddSimpleItem(off+2, 1, L"Sustain Level");
		containerItem->AddSimpleItem(off+3, 1, L"Sustain Rate");
		containerItem->AddSimpleItem(off+4, 1, L"Release Rate");
		containerItem->AddSimpleItem(off+5, 3, L"Unknown");
		this->AddItem(containerItem);
	}
	//unLength = off - dwOffset;
	int numArtics = unLength/sizeof(qs_artic_info);
	artics = new qs_artic_info[numArtics];
	GetBytes(dwOffset, unLength, artics);

	return true;
}


// *********************
// QSoundSampleInfoTable
// *********************


QSoundSampleInfoTable::QSoundSampleInfoTable(RawFile* file, wstring& name, U32 offset, U32 length)
	: VGMMiscFile(QSoundFormat::name, file, offset, length, name)
{
}

QSoundSampleInfoTable::~QSoundSampleInfoTable(void)
{
	if (infos)
		delete[] infos;
}

bool QSoundSampleInfoTable::LoadMain()
{
	DWORD off = dwOffset;
	U32 test1=1, test2=1;
	if (unLength == 0)
		unLength = 0xFFFFFFFF - dwOffset;
	for (int i = 0; (test1 || test2)  && ((test1 != 0xFFFFFFFF) || (test2 != 0xFFFFFFFF)) && off < dwOffset+unLength; i++, off += sizeof(qs_samp_info) )
	{
		test1 = GetWord(off+8);
		test2 = GetWord(off+12);

		wostringstream name;
		name << L"Sample Info " << i;
		VGMContainerItem* containerItem = new VGMContainerItem(this, off, sizeof(qs_samp_info), name.str());
		containerItem->AddSimpleItem(off,   1, L"Bank");
		containerItem->AddSimpleItem(off+1, 2, L"Offset");
		containerItem->AddSimpleItem(off+3, 2, L"Loop Offset");
		containerItem->AddSimpleItem(off+5, 2, L"End Offset");
		containerItem->AddSimpleItem(off+7, 1, L"Unity Key");
		this->AddItem(containerItem);
	}
	unLength = off - dwOffset;
	this->numSamples = unLength/sizeof(qs_samp_info);
	infos = new qs_samp_info[numSamples];
	GetBytes(dwOffset, unLength, infos);	

	return true;
}

// **************
// QSoundInstrSet
// **************

QSoundInstrSet::QSoundInstrSet(RawFile* file,
							   QSoundVer version,
							   U32 offset,
							   int numInstrBanks,
							   QSoundSampleInfoTable* theSampInfoTable,
							   QSoundArticTable* theArticTable,
                               wstring& name)
: VGMInstrSet(QSoundFormat::name, file, offset, 0, name),
  fmt_version(version),
  num_instr_banks(numInstrBanks),
  sampInfoTable(theSampInfoTable),
  articTable(theArticTable)
{
}

QSoundInstrSet::~QSoundInstrSet(void)
{
}


bool QSoundInstrSet::GetHeaderInfo()
{
	return true;
}

bool QSoundInstrSet::GetInstrPointers()
{
	// Load the instr_info tables.

	if (fmt_version <= VER_115)
	{
		// In these versions, there are no instr_info_table pointers, it's hard-coded into the assembly code.
		// There are two possible instr_info banks stored next to each other with 256 entries in each,
		// unlike higher versions, where the number of instr_infos in each bank is variable and less than 0x7F.
		
		//dwOffset is the offset to the instr_info_table
		
		for (UINT bank = 0; bank < num_instr_banks; bank++)
			for (UINT i=0; i<256; i++)
			{
				wostringstream name;
				name << L"Instrument " << bank*256 + i;
				aInstrs.push_back(new QSoundInstr(this, dwOffset+i*8+(bank*256*8), 8, (bank*2)+(i/128), i%128, name.str()));
			}
	}
	else
	{
		U8 instr_info_length = sizeof(qs_prog_info_ver_130);
		if (fmt_version < VER_130)
			instr_info_length = sizeof(qs_prog_info_ver_103);		//1.16 (Xmen vs SF) is like this

		vector<U16> instr_table_ptrs;
		for (unsigned int i=0; i<num_instr_banks; i++)
			instr_table_ptrs.push_back(GetShort(dwOffset+i*2));	//get the instr table ptrs
		int totalInstrs = 0;
		for (UINT i=0; i<instr_table_ptrs.size(); i++)
		{
			U16 endOffset;
			// The following is actually incorrect.  There is a max of 256 instruments per bank
			if (i+1 < instr_table_ptrs.size())	
				endOffset = instr_table_ptrs[i+1];
			else
				endOffset = instr_table_ptrs[i] + instr_info_length * 256;//4*0x7F;
			
			int k=0;
			for (int j = instr_table_ptrs[i]; j < endOffset; j+=instr_info_length, k++)
			{
				if (GetShort(j) == 0 && GetByte(j+2) == 0 && i != 0)
					break;
				wostringstream name;
				name << L"Instrument " << totalInstrs + k;
				aInstrs.push_back(new QSoundInstr(this, j, instr_info_length, (i*2) + (k / 128), (k % 128), name.str()));
			}
			totalInstrs += k;
		}
	}
	return true;
}


// ***********
// QSoundInstr
// ***********

QSoundInstr::QSoundInstr(VGMInstrSet* instrSet, ULONG offset, ULONG length, ULONG theBank, ULONG theInstrNum, wstring& name)
 : 	VGMInstr(instrSet, offset, length, theBank, theInstrNum, name)
{
}

QSoundInstr::~QSoundInstr(void)
{
}


bool QSoundInstr::LoadInstr()
{
	VGMRgn* rgn;

	if (GetFormatVer() < VER_103)
	{
		rgn = new VGMRgn(this, dwOffset, unLength, L"Instrument Info ver < 1.03");
		rgn->AddSimpleItem(this->dwOffset,     1, L"Sample Info Index");
		rgn->AddSimpleItem(this->dwOffset + 1, 1, L"Unknown / Ignored");
		rgn->AddSimpleItem(this->dwOffset + 2, 1, L"Attack Rate");
		rgn->AddSimpleItem(this->dwOffset + 3, 1, L"Decay Rate");
		rgn->AddSimpleItem(this->dwOffset + 4, 1, L"Sustain Level");
		rgn->AddSimpleItem(this->dwOffset + 5, 1, L"Sustain Rate");
		rgn->AddSimpleItem(this->dwOffset + 6, 1, L"Release Rate");
		rgn->AddSimpleItem(this->dwOffset + 7, 1, L"Unknown");

		qs_prog_info_ver_101 progInfo;
		GetBytes(dwOffset, sizeof(qs_prog_info_ver_101), &progInfo);
		rgn->sampNum = progInfo.sample_index;
		this->attack_rate = progInfo.attack_rate;
		this->decay_rate = progInfo.decay_rate;
		this->sustain_level = progInfo.sustain_level;
		this->sustain_rate = progInfo.sustain_rate;
		this->release_rate = progInfo.release_rate;
	}
	else if (GetFormatVer() < VER_130)
	{
		rgn = new VGMRgn(this, dwOffset, unLength, L"Instrument Info ver < 1.30");
		rgn->AddSimpleItem(this->dwOffset,     2, L"Sample Info Index");
		rgn->AddSimpleItem(this->dwOffset + 2, 1, L"Unknown");
		rgn->AddSimpleItem(this->dwOffset + 3, 1, L"Attack Rate");
		rgn->AddSimpleItem(this->dwOffset + 4, 1, L"Decay Rate");
		rgn->AddSimpleItem(this->dwOffset + 5, 1, L"Sustain Level");
		rgn->AddSimpleItem(this->dwOffset + 6, 1, L"Sustain Rate");
		rgn->AddSimpleItem(this->dwOffset + 7, 1, L"Release Rate");

		qs_prog_info_ver_103 progInfo;
		GetBytes(dwOffset, sizeof(qs_prog_info_ver_103), &progInfo);
		rgn->sampNum = progInfo.sample_index;
		this->attack_rate = progInfo.attack_rate;
		this->decay_rate = progInfo.decay_rate;
		this->sustain_level = progInfo.sustain_level;
		this->sustain_rate = progInfo.sustain_rate;
		this->release_rate = progInfo.release_rate;
	}
	else
	{
		rgn = new VGMRgn(this, dwOffset, unLength, L"Instrument Info ver >= 1.30");
		qs_prog_info_ver_130 progInfo;
		GetBytes(dwOffset, sizeof(qs_prog_info_ver_130), &progInfo);

		rgn->AddSimpleItem(this->dwOffset,     2, L"Sample Info Index");
		rgn->AddSimpleItem(this->dwOffset + 2, 1, L"Unknown");
		rgn->AddSimpleItem(this->dwOffset + 3, 1, L"Articulation Index");

		QSoundArticTable* articTable = ((QSoundInstrSet*)this->parInstrSet)->articTable;
		qs_artic_info* artic = &articTable->artics[progInfo.artic_index];
		rgn->sampNum =			progInfo.sample_index;
		this->attack_rate =		artic->attack_rate;
		this->decay_rate =		artic->decay_rate;
		this->sustain_level =	artic->sustain_level;
		this->sustain_rate =	artic->sustain_rate;
		this->release_rate =	artic->release_rate;

	}

	// Fix for Dungeons and Dragons Shadow Over Mystara
	if (rgn->sampNum >= 0x8000)
		rgn->sampNum -= 0x8000;

	// if the sample doesn't exist, set it to the first sample
	if (rgn->sampNum >= ((QSoundInstrSet*)parInstrSet)->sampInfoTable->numSamples)
		rgn->sampNum = 0;

	WORD Ar = attack_rate_table[this->attack_rate];
	WORD Dr = decay_rate_table[this->decay_rate];
	WORD Sl = linear_table[this->sustain_level];
	WORD Sr = decay_rate_table[this->sustain_rate];
	WORD Rr = decay_rate_table[this->release_rate];

	long ticks = 0;

	// The rate values are all measured from max to min, as the SF2 and DLS specs call for.
	//  In the actual code, the envelope level starts at different points
	// Attack rate 134A in sfa2
	ticks = Ar ? 0xFFFF / Ar : 0;
	rgn->attack_time =  (Ar == 0xFFFF) ? 0 : ticks * QSOUND_TICK_FREQ;

	// Decay rate 1365 in sfa2
	//  Let's check if we should substitute the sustain rate for the decay rate.
	//  Why?  SF2 and DLS do not have a sustain rate.  If the decay rate here is practically
	//  unnoticeable because the sustain level is extremely high - meaning decay time is
	//  is super short - then let's set the sustain level to 0 and substitute the sustain 
	//  rate into the decay rate,  achieving the same effect as sustain rate.

	if (this->sustain_level >= 0x7E && this->sustain_rate > 0 && this->decay_rate > 1)
	{
		//for a better approximation, we count the ticks to get from original Dr to original Sl
		ticks = (long)ceil((0xFFFF-Sl) / (double)Dr);
		ticks += (long)ceil(Sl / (double)Sr);
		//double ticksToHalfVol = ((65535.0 - 20724.9866) / 65535.0)
		rgn->decay_time = ticks * QSOUND_TICK_FREQ;
		Sl = 0;
	}
	else
	{
		ticks = Dr ? (0xFFFF / Dr) : 0;
		rgn->decay_time = (Dr == 0xFFFF) ? 0 :ticks * QSOUND_TICK_FREQ;
	}
	rgn->decay_time = LinAmpDecayTimeToLinDBDecayTime(rgn->decay_time);

	// Sustain level
	//    if the Decay rate is 0, then the sustain level is effectively max
	//    some articulations have Dr = 0, Sl = 0 (ex: mmatrix).  Dr 0 means the decay phase never ends
	//    and Sl value is irrelevant because the envelope stays at max, but we can't set an infinite decay phase in sf2 or dls
	if (Dr <= 1)	
		rgn->sustain_level = 1.0;
	else
		rgn->sustain_level = Sl / (double)0xFFFF;

	// Sustain rate 138D in sfa2
	ticks = Sr ? 0xFFFF / Sr : 0;
	rgn->sustain_time = (Sr == 0xFFFF) ? 0 : ticks * QSOUND_TICK_FREQ;
	rgn->sustain_time = LinAmpDecayTimeToLinDBDecayTime(rgn->sustain_time);

	ticks = Rr ? 0xFFFF / Rr : 0;
	rgn->release_time = (Rr == 0xFFFF) ? 0 : ticks * QSOUND_TICK_FREQ;
	rgn->release_time = LinAmpDecayTimeToLinDBDecayTime(rgn->release_time);

	if (rgn->sampNum == 0xFFFF || rgn->sampNum >= ((QSoundInstrSet*)parInstrSet)->sampInfoTable->numSamples)
		rgn->sampNum = 0;
	rgn->SetUnityKey( ((QSoundInstrSet*)parInstrSet)->sampInfoTable->infos[rgn->sampNum].unity_key);
	aRgns.push_back(rgn);
	return true;
}
 

// **************
// QSoundSampColl
// **************

QSoundSampColl::QSoundSampColl(RawFile* file, QSoundInstrSet* theinstrset, QSoundSampleInfoTable* sampinfotable, ULONG offset, ULONG length, wstring& name)
: VGMSampColl(QSoundFormat::name, file, offset, length, name), 
  instrset(theinstrset),
  sampInfoTable(sampinfotable)
{

}


bool QSoundSampColl::GetHeaderInfo()
{
	unLength = this->rawfile->size();
	//unLength = GetWord(dwOffset+8);
	return true;
}

bool QSoundSampColl::GetSampleInfo()
{
	//QSoundInstrSet* instrset = (QSoundInstrSet*)instrset;
	UINT numSamples = instrset->sampInfoTable->numSamples;
	for (UINT i=0; i<numSamples; i++)
	{
		wostringstream name;
		name << L"Sample " << i;

		qs_samp_info* sampInfo = &sampInfoTable->infos[i];
		UINT sampOffset = (sampInfo->bank<<16) + (sampInfo->start_addr_hi<<8) + sampInfo->start_addr_lo;
		int sampLength;
		if (sampInfo->end_addr_hi == 0 && sampInfo->end_addr_lo == 0)
			sampLength = ((sampInfo->bank+1)<<16) - sampOffset;
		else
			sampLength = (sampInfo->bank<<16) + (sampInfo->end_addr_hi<<8) + sampInfo->end_addr_lo - sampOffset;
		if (sampLength < 0)
			sampLength = -sampLength;
		//UINT loopOffset = (sampOffset+sampLength)-((sampInfo->bank<<16) + (sampInfo->loop_offset_hi<<8) + sampInfo->loop_offset_lo);
		UINT loopOffset = ((sampInfo->bank<<16) + (sampInfo->loop_offset_hi<<8) + sampInfo->loop_offset_lo) - sampOffset;
		if (loopOffset > (UINT)sampLength)
			loopOffset = sampLength;
		if (sampLength == 0 || sampOffset > unLength)
			break;
		VGMSamp* newSamp = AddSamp(sampOffset, sampLength, sampOffset, sampLength, 1, 8, 24000, name.str());
		newSamp->SetWaveType(WT_PCM8);
		if ( sampLength - loopOffset < 40)
			newSamp->SetLoopStatus(false);
		else
		{
			newSamp->SetLoopStatus(true);
			newSamp->SetLoopOffset(loopOffset);
			newSamp->SetLoopLength(sampLength-loopOffset);
			newSamp->unityKey = sampInfo->unity_key;
		}
	}
	return true;
}
