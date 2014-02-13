#include "stdafx.h"
#include "SynthFile.h"
#include "VGMInstrSet.h"
#include "VGMSamp.h"
#include "common.h"
#include "Root.h"

using namespace std;

//  **********************************************************************************
//  SynthFile - An intermediate class to lay out all of the the data necessary for Coll conversion
//				to DLS or SF2 formats.  Currently, the structure is identical to DLS.
//  **********************************************************************************

SynthFile::SynthFile(string synth_name)
	: name(synth_name)
{
	
}

SynthFile::~SynthFile(void)
{
	DeleteVect(vInstrs);
	DeleteVect(vWaves);
}

SynthInstr* SynthFile::AddInstr(unsigned long bank, unsigned long instrNum)
{
	stringstream str;
	str << "Instr bnk" << bank << " num" << instrNum;
	vInstrs.insert(vInstrs.end(), new SynthInstr(bank, instrNum, str.str()));
	return vInstrs.back();
}

SynthInstr* SynthFile::AddInstr(unsigned long bank, unsigned long instrNum, string name)
{
	vInstrs.insert(vInstrs.end(), new SynthInstr(bank, instrNum, name));
	return vInstrs.back();
}

void SynthFile::DeleteInstr(unsigned long bank, unsigned long instrNum)
{

}

SynthWave* SynthFile::AddWave(USHORT formatTag, USHORT channels, int samplesPerSec, int aveBytesPerSec,
						  USHORT blockAlign, USHORT bitsPerSample, ULONG waveDataSize, unsigned char* waveData,
						  string name)
{
	vWaves.insert(vWaves.end(), new SynthWave(formatTag, channels, samplesPerSec, aveBytesPerSec, blockAlign, bitsPerSample, waveDataSize, waveData, name));
	return vWaves.back();
}

//  **********
//  SynthInstr
//  **********


SynthInstr::SynthInstr(ULONG bank, ULONG instrument)
: ulBank(bank), ulInstrument(instrument)
{
	stringstream str;
	str << "Instr bnk" << bank << " num" << instrument;
	name = str.str();
	//RiffFile::AlignName(name);
}

SynthInstr::SynthInstr(ULONG bank, ULONG instrument, string instrName)
: ulBank(bank), ulInstrument(instrument), name(instrName)
{
	//RiffFile::AlignName(name);
}

SynthInstr::SynthInstr(ULONG bank, ULONG instrument, string instrName, vector<SynthRgn*> listRgns)
: ulBank(bank), ulInstrument(instrument), name(instrName)
{
	//RiffFile::AlignName(name);
	vRgns = listRgns;
}

SynthInstr::~SynthInstr()
{
	DeleteVect(vRgns);
}

SynthRgn* SynthInstr::AddRgn(void)
{
	vRgns.insert(vRgns.end(), new SynthRgn());
	return vRgns.back();
}

SynthRgn* SynthInstr::AddRgn(SynthRgn rgn)
{
	SynthRgn* newRgn = new SynthRgn();
	*newRgn = rgn;
	vRgns.insert(vRgns.end(), newRgn);
	return vRgns.back();
}

//  ********
//  SynthRgn
//  ********

SynthRgn::~SynthRgn(void)
{
	if (sampinfo)
		delete sampinfo;
	if (art)
		delete art;
}

SynthArt* SynthRgn::AddArt(void)
{
	art = new SynthArt();
	return art;
}

SynthSampInfo* SynthRgn::AddSampInfo(void)
{
	sampinfo = new SynthSampInfo();
	return sampinfo;
}

void SynthRgn::SetRanges(USHORT keyLow, USHORT keyHigh, USHORT velLow, USHORT velHigh)
{
	usKeyLow = keyLow;
	usKeyHigh = keyHigh;
	usVelLow = velLow;
	usVelHigh = velHigh;
}

void SynthRgn::SetWaveLinkInfo(USHORT options, USHORT phaseGroup, ULONG theChannel, ULONG theTableIndex)
{
	fusOptions = options;
	usPhaseGroup = phaseGroup;
	channel = theChannel;
	tableIndex = theTableIndex;
}

//  ********
//  SynthArt
//  ********

SynthArt::~SynthArt()
{
	//DeleteVect(vConnBlocks);
}

void SynthArt::AddADSR(double attack, Transform atk_transform, double decay, double sustain_level,
					   double sustain, double release, Transform rls_transform)
{
	this->attack_time = attack;
	this->attack_transform = atk_transform;
	this->decay_time = decay;
	this->sustain_lev = sustain_level;
	this->sustain_time = sustain;
	this->release_time = release;
	this->release_transform = rls_transform;
}

void SynthArt::AddPan(double thePan)
{
	this->pan = thePan;
}


//  *************
//  SynthSampInfo
//  *************

void SynthSampInfo::SetLoopInfo(Loop& loop, VGMSamp* samp)
{
	const int origFormatBytesPerSamp = samp->bps/8;
	double compressionRatio = samp->GetCompressionRatio();

	// If the sample loops, but the loop length is 0, then assume the length should
	// extend to the end of the sample.
	if (loop.loopStatus && loop.loopLength == 0)
		loop.loopLength = samp->dataLength - loop.loopStart;

	cSampleLoops = loop.loopStatus;
	ulLoopType = loop.loopType; 
	ulLoopStart =  (loop.loopStartMeasure==LM_BYTES) ?			//In DLS, the value is in number of samples
		(ULONG)((loop.loopStart * compressionRatio) / origFormatBytesPerSamp) ://(16/8) :		//if the val is a raw offset of the original format, multiply it by the compression ratio
		loop.loopStart /** origFormatBytesPerSamp * compressionRatio / origFormatBytesPerSamp*/;//(16/8);	 //if the value is in samples, multiply by bytes per sample to get raw offset of original format, then multiply by compression ratio to get raw offset in standard wav, then divide bytes bytesPerSamp of standard wave (2)
	ulLoopLength = (loop.loopLengthMeasure==LM_BYTES) ?			//In DLS, the value is in number of samples
		(ULONG)((loop.loopLength * compressionRatio) / origFormatBytesPerSamp) ://(16/8)  :	//if it's in raw bytes, multiply by compressionRatio to convert to Raw length in new format, and divide by bytesPerSamp of a 16 bit standard wave (16bits/8 bits per byte)
		loop.loopLength /** origFormatBytesPerSamp * compressionRatio) / origFormatBytesPerSamp*/;//(16/8);
}

void SynthSampInfo::SetPitchInfo(USHORT unityNote, short fineTune, double atten)
{
	usUnityNote = unityNote;
	sFineTune = fineTune;
	attenuation = atten;
}


//  *********
//  SynthWave
//  *********

void SynthWave::ConvertTo16bitSigned()
{
	if (wBitsPerSample == 8)
	{
		this->wBitsPerSample = 16;
		this->wBlockAlign = 16 / 8*this->wChannels;
		this->dwAveBytesPerSec *= 2;
		
		S16* newData = new S16[this->dataSize];
		for (unsigned int i = 0; i < this->dataSize; i++)
			newData[i] = ((S16)this->data[i] - 128) << 8;
		delete[] this->data;
		this->data = (U8*)newData;
		this->dataSize *= 2;
	}
}

SynthWave::~SynthWave()
{
	if (sampinfo)
		delete sampinfo;
	if (data)
		delete data;
}

SynthSampInfo* SynthWave::AddSampInfo(void)
{
	sampinfo = new SynthSampInfo();
	return sampinfo;
}