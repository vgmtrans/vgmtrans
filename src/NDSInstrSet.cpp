#include "stdafx.h"
#include "NDSInstrSet.h"
#include "VGMRgn.h"
#include "math.h"

// INTR_FREQUENCY is the interval in seconds between updates to the vol for articulation.
// In the original software, this is done via a hardware interrupt timer.
// After calculating the number of volume updates that will be made for the articulation values, 
// a calculation found through reverse-engineering the music driver code,  we can multiply the count by
// this frequency to find the duration of the articulation phases with exact accuracy.
#define INTR_FREQUENCY (1.0/192.0)//0.005210332809750868


// ***********
// NDSInstrSet
// ***********

NDSInstrSet::NDSInstrSet(RawFile* file, ULONG offset, ULONG length, wstring name)
: VGMInstrSet(NDSFormat::name, file, offset, length, name)
{
}
/*
NDSInstrSet::~NDSInstrSet(void)
{
}*/

int NDSInstrSet::GetInstrPointers()
{
	VGMHeader* header = AddHeader(dwOffset, 0x38);
	ULONG nInstruments = GetWord(dwOffset + 0x38);
	VGMHeader* instrptrHdr = AddHeader(dwOffset+0x38, nInstruments*4+4, L"Instrument Pointers");

	for (UINT i=0; i<nInstruments; i++)
	{
		ULONG instrPtrOff = dwOffset + 0x3C + i*4;
		ULONG temp = GetWord(instrPtrOff);
		if (temp == 0)
			continue;
		BYTE instrType = temp & 0xFF;
		ULONG pInstr = temp >> 8;
		aInstrs.push_back(new NDSInstr(this, pInstr+dwOffset, 0, 0, i, instrType));

		VGMHeader* hdr = instrptrHdr->AddHeader(instrPtrOff, 4, L"Pointer");
		hdr->AddSimpleItem(instrPtrOff, 1, L"Type");
		hdr->AddSimpleItem(instrPtrOff+1, 3, L"Offset");
	}
	return true;
}

// ********
// NDSInstr
// ********

NDSInstr::NDSInstr(NDSInstrSet* instrSet, ULONG offset, ULONG length, ULONG theBank,
				  ULONG theInstrNum, BYTE theInstrType)
 : 	VGMInstr(instrSet, offset, length, theBank, theInstrNum), instrType(theInstrType)
{
}

int NDSInstr::LoadInstr()
{
	//All of the undefined case values below are used for tone or noise channels
	switch (instrType)					
	{
	case 0x01:		//single region format
		{
			name = L"Single-Region Instrument";
			unLength = 10;
			VGMRgn* rgn = AddRgn(dwOffset, 10, GetShort(dwOffset));
			GetSampCollPtr(rgn, GetShort(dwOffset+2));
			GetArticData(rgn, dwOffset+4);
		}
		break;
	case 0x02:		//used in Mr. Driller "BANK_BGM" for some reason
	case 0x03:
	case 0x04:
	case 0x05:
		break;

	case 0x06:
	case 0x07:
	case 0x08:
	case 0x09:
	case 0x0A:
	case 0x0B:
	case 0x0C:
	case 0x0D:
	case 0x0E:
	case 0x0F:
		break;

	case 0x10:		//drumset
		{
			name = L"Drumset";
			BYTE lowKey = GetByte(dwOffset);
			BYTE highKey = GetByte(dwOffset+1);
			BYTE nRgns = (highKey-lowKey) + 1;
			for (BYTE i=0; i < nRgns; i++)
			{
				VGMRgn* rgn = AddRgn(dwOffset+2+i*12, 12, GetShort(dwOffset+2 +2 + i*12), lowKey+i, lowKey+i);
				GetSampCollPtr(rgn, GetShort(dwOffset+2 + (i*12) + 4));
				GetArticData(rgn, dwOffset+2 +6 + i*12);
			}
			unLength = 2+nRgns*12;
			break;
		}
	

	case 0x11:		//multiple regions format
		{
			name = L"Multi-Region Instrument";
			BYTE keyRanges[8];
			BYTE nRgns = 0;
			for (int i=0; i<8; i++)
			{
				keyRanges[i] = GetByte(dwOffset+i);
				if (keyRanges[i] != 0)
					nRgns++;
				else
					break;
			}

			for (int i=0; i<nRgns; i++)
			{
				VGMRgn* rgn = AddRgn(dwOffset+8+i*12, 12, GetShort(dwOffset+8 + i*12 + 2), 
										(i==0) ? 0 : keyRanges[i-1]+1, keyRanges[i]);
				GetSampCollPtr(rgn, GetShort(dwOffset+8 + (i*12) + 4));
				GetArticData(rgn, dwOffset+8 + i*12 + 6);
			}
			unLength = nRgns*12+8;
			break;
		}
	}
	return true;
}

void NDSInstr::GetSampCollPtr(VGMRgn* rgn, int waNum)
{
	rgn->sampCollPtr = ((NDSInstrSet*)parInstrSet)->sampCollWAList[waNum];
}

void NDSInstr::GetArticData(VGMRgn* rgn, ULONG offset)
{
	short realDecay;
	short realRelease;
	BYTE realAttack;
	long realSustainLev;
	const BYTE AttackTimeTable[] = { 0x00, 0x01, 0x05, 0x0E, 0x1A, 0x26,
		0x33, 0x3F, 0x49, 0x54, 0x5C, 0x64, 0x6D, 0x74, 0x7B, 0x7F, 0x84,
		0x89, 0x8F};

	const USHORT sustainLevTable[] = { 0xFD2D, 0xFD2E, 0xFD2F, 0xFD75, 0xFDA7, 0xFDCE, 0xFDEE, 0xFE09, 0xFE20, 0xFE34, 0xFE46, 0xFE57, 0xFE66, 0xFE74,
	0xFE81, 0xFE8D, 0xFE98, 0xFEA3, 0xFEAD, 0xFEB6, 0xFEBF, 0xFEC7, 0xFECF, 0xFED7, 0xFEDF, 0xFEE6, 0xFEEC, 0xFEF3,
	0xFEF9, 0xFEFF, 0xFF05, 0xFF0B, 0xFF11, 0xFF16, 0xFF1B, 0xFF20, 0xFF25, 0xFF2A, 0xFF2E, 0xFF33, 0xFF37, 0xFF3C,
	0xFF40, 0xFF44, 0xFF48, 0xFF4C, 0xFF50, 0xFF53, 0xFF57, 0xFF5B, 0xFF5E, 0xFF62, 0xFF65, 0xFF68, 0xFF6B, 0xFF6F,
	0xFF72, 0xFF75, 0xFF78, 0xFF7B, 0xFF7E, 0xFF81, 0xFF83, 0xFF86, 0xFF89, 0xFF8C, 0xFF8E, 0xFF91, 0xFF93, 0xFF96,
	0xFF99, 0xFF9B, 0xFF9D, 0xFFA0, 0xFFA2, 0xFFA5, 0xFFA7, 0xFFA9, 0xFFAB, 0xFFAE, 0xFFB0, 0xFFB2, 0xFFB4, 0xFFB6,
	0xFFB8, 0xFFBA, 0xFFBC, 0xFFBE, 0xFFC0, 0xFFC2, 0xFFC4, 0xFFC6, 0xFFC8, 0xFFCA, 0xFFCC, 0xFFCE, 0xFFCF, 0xFFD1,
	0xFFD3, 0xFFD5, 0xFFD6, 0xFFD8, 0xFFDA, 0xFFDC, 0xFFDD, 0xFFDF, 0xFFE1, 0xFFE2, 0xFFE4, 0xFFE5, 0xFFE7, 0xFFE9,
	0xFFEA, 0xFFEC, 0xFFED, 0xFFEF, 0xFFF0, 0xFFF2, 0xFFF3, 0xFFF5, 0xFFF6, 0xFFF8, 0xFFF9, 0xFFFA, 0xFFFC, 0xFFFD, 
	0xFFFF, 0x0000 };

	rgn->SetUnityKey(GetByte(offset++));
	BYTE AttackTime = GetByte(offset++);
	BYTE DecayTime = GetByte(offset++);
	BYTE SustainLev = GetByte(offset++);
	BYTE ReleaseTime = GetByte(offset++);
	BYTE Pan = GetByte(offset++);

	if (AttackTime >= 0x6D)
		realAttack = AttackTimeTable[0x7F-AttackTime];
	else
		realAttack = 0xFF-AttackTime;

	realDecay = GetFallingRate(DecayTime);
	realRelease = GetFallingRate(ReleaseTime);

	int count = 0; 
	for (long i = 0x16980; i != 0; i = (i*realAttack)>>8)
		count++;
	rgn->attack_time = count * INTR_FREQUENCY;

	if (SustainLev == 0x7F)				
		realSustainLev = 0;
	else
		realSustainLev = (0x10000-sustainLevTable[SustainLev]) << 7;
	if (DecayTime == 0x7F)
		rgn->decay_time = 0.001;
	else
	{
		count = 0x16980 / realDecay;//realSustainLev / realDecay;
		rgn->decay_time = count * INTR_FREQUENCY;
	}
	
	if (realSustainLev == 0)
		rgn->sustain_level = 1.0;
	else
		//rgn->sustain_level = 20 * log10 ((92544.0-realSustainLev) / 92544.0);
		rgn->sustain_level = (double)(0x16980-realSustainLev)/(double)0x16980;

	
	count = 0x16980 / realRelease;		//we express release rate as time from maximum volume, not sustain level
	rgn->release_time = count * INTR_FREQUENCY;
	
	if (Pan == 0)
		rgn->pan = 0;
	else if (Pan == 127)
		rgn->pan = 1.0;
	else if (Pan == 64)
		rgn->pan = 0.5;
	else
		rgn->pan = (double)Pan/(double)127;
}

USHORT NDSInstr::GetFallingRate(BYTE DecayTime)
{
	ULONG realDecay;
	if (DecayTime == 0x7F)
		realDecay = 0xFFFF;
	else if (DecayTime == 0x7E)
		realDecay = 0x3C00;
	else if (DecayTime < 0x32)
	{
		realDecay = DecayTime * 2;
		++realDecay;
		realDecay &= 0xFFFF;
	}
	else
	{
		realDecay = 0x1E00;
		DecayTime = 0x7E - DecayTime;
		realDecay /= DecayTime;		//there is a whole subroutine that seems to resolve simply to this.  I have tested all cases
		realDecay &= 0xFFFF;
	}
	return (USHORT)realDecay;
}








// ***********
// NDSWaveArch
// ***********

NDSWaveArch::NDSWaveArch(RawFile* file, ULONG offset, ULONG length, wstring name)
: VGMSampColl(NDSFormat::name, file, offset, length, name)
{
}

NDSWaveArch::~NDSWaveArch()
{
}

bool NDSWaveArch::GetHeaderInfo()
{
	unLength = GetWord(dwOffset+8);
	return true;
}

bool NDSWaveArch::GetSampleInfo()
{
	ULONG nSamples = GetWord(dwOffset + 0x38);
	for (ULONG i=0; i<nSamples; i++)
	{
		ULONG pSample = GetWord(dwOffset + 0x3C + i*4) + dwOffset;
		int nChannels = 1;
		BYTE waveType = GetByte(pSample);
		bool bLoops = (GetByte(pSample+1) != 0);
		USHORT rate = GetShort(pSample+2);
		USHORT bps;
		//BYTE multiplier;
		switch (waveType)
		{
		case NDSSamp::PCM8:
			bps = 8;
			break;
		case NDSSamp::PCM16:
			bps = 16;
			break;
		case NDSSamp::IMA_ADPCM:		// By far, and unfortunately, the most common type
			bps = 16;
			break;
		}
		ULONG loopOff = (GetShort(pSample+6))*4;//*multiplier;		//represents loop point in words, excluding header supposedly
		ULONG nonLoopLength = GetShort(pSample+8)*4;		//if IMA-ADPCM, subtract one for the ADPCM header

		ULONG dataStart, dataLength;
		if (waveType == NDSSamp::IMA_ADPCM)
		{
			dataStart = pSample+0x10;
			dataLength = loopOff+nonLoopLength-4;
		}
		else
		{
			dataStart = pSample+0xC;
			dataLength = loopOff+nonLoopLength;
		}

		wostringstream name;
		name << L"Sample " << (float)samples.size();
		NDSSamp* samp =  new NDSSamp(this, pSample, dataStart+dataLength-pSample, dataStart,
								     dataLength, nChannels, bps, rate, waveType, name.str());
		
		if (waveType == NDSSamp::IMA_ADPCM)
		{
			samp->SetLoopStartMeasure(LM_SAMPLES);
			samp->SetLoopLengthMeasure(LM_SAMPLES);
			loopOff *= 2;			//now it's in samples
			loopOff = loopOff - 8 + 1;		//exclude the header's sample.  not exactly sure why 8.
			nonLoopLength = (dataLength*2 + 1) - loopOff;
			samp->ulUncompressedSize = (nonLoopLength + loopOff)*2;
		}

		samp->SetLoopStatus(bLoops);
		samp->SetLoopOffset(loopOff);
		samp->SetLoopLength(nonLoopLength);
		samples.push_back(samp);
		//AddSamp(pSample, realSampSize+0xC, pSample+0xC, realSampSize, nChannels,
		//		16, rate, loopOff);
	}
	return true;
}



// *******
// NDSSamp
// *******

NDSSamp::NDSSamp(VGMSampColl* sampColl, ULONG offset, ULONG length, ULONG dataOffset,
				 ULONG dataLen, BYTE nChannels, USHORT theBPS,
				 ULONG theRate, BYTE theWaveType, wstring name)
: VGMSamp(sampColl, offset, length, dataOffset, dataLen, nChannels, theBPS,
		  theRate, name), waveType(theWaveType)
{
}


double NDSSamp::GetCompressionRatio()
{
	if (waveType == IMA_ADPCM)
		return 4.0;
	else
		return 1.0;
}

void NDSSamp::ConvertToStdWave(BYTE* buf)
{
	if (waveType == IMA_ADPCM)
		ConvertImaAdpcm(buf);
	else if (waveType == PCM8)
	{
		GetBytes(dataOff, dataLength, buf);
		for(unsigned int i = 0; i < dataLength; i++)		//convert every byte from signed to unsigned value
			buf[i] ^= 0x80;			//For whatever reason, the WAV standard has PCM8 unsigned and PCM16 signed
	}
	else
		GetBytes(dataOff, dataLength, buf);
}

// From nocash's site: The NDS data consist of a 32bit header, followed by 4bit values (so each byte contains two
// values, the first value in the lower 4bits, the second in upper 4 bits). The 32bit header contains initial values:
//
//  Bit0-15   Initial PCM16 Value (Pcm16bit = -7FFFh..+7FFF) (not -8000h)
//  Bit16-22  Initial Table Index Value (Index = 0..88)
//  Bit23-31  Not used (zero)


// As far as I can tell, the NDS IMA-ADPCM format has one difference from standard IMA-ADPCM:
// it clamps min (and max?) sample values differently (see below).  I really don't know how much of a difference
// it makes, but this implementation is, to my knowledge, the proper way of doing things for NDS.
void NDSSamp::ConvertImaAdpcm(BYTE *buf)
{
	ULONG destOff = 0;
	UINT sampHeader = GetWord(dataOff-4);
	int decompSample = sampHeader & 0xFFFF;
	int stepIndex = (sampHeader >> 16) & 0x7F;
	//int decompSample = GetShort(dataOff);
	//int stepIndex = GetShort(dataOff+2);
	ULONG curOffset = dataOff;
	((SHORT*)buf)[destOff++] = (SHORT)decompSample;
	
	
	BYTE compByte;
	while (curOffset < dataOff+dataLength)
	{
		compByte = GetByte(curOffset++);
		process_nibble(compByte, stepIndex, decompSample);
		((SHORT*)buf)[destOff++] = (SHORT)decompSample;
		process_nibble((compByte & 0xF0) >> 4, stepIndex, decompSample);
		((SHORT*)buf)[destOff++] = (SHORT)decompSample;
	}
}

// I'm copying nocash's IMA-ADPCM conversion method verbatim.  Big thanks to him. 
// Info is at http://nocash.emubase.de/gbatek.htm#dssound and the algorithm is described as follows:
//
//The NDS data consist of a 32bit header, followed by 4bit values (so each byte contains two values,
//the first value in the lower 4bits, the second in upper 4 bits). The 32bit header contains initial values:
//
//  Bit0-15   Initial PCM16 Value (Pcm16bit = -7FFFh..+7FFF) (not -8000h)
//  Bit16-22  Initial Table Index Value (Index = 0..88)
//  Bit23-31  Not used (zero)
//
//In theory, the 4bit values are decoded into PCM16 values, as such:
//
//  Diff = ((Data4bit AND 7)*2+1)*AdpcmTable[Index]/8      ;see rounding-error
//  IF (Data4bit AND 8)=0 THEN Pcm16bit = Max(Pcm16bit+Diff,+7FFFh)
//  IF (Data4bit AND 8)=8 THEN Pcm16bit = Min(Pcm16bit-Diff,-7FFFh)
//  Index = MinMax (Index+IndexTable[Data4bit AND 7],0,88)
//
//In practice, the first line works like so (with rounding-error):
//
//  Diff = AdpcmTable[Index]/8
//  IF (data4bit AND 1) THEN Diff = Diff + AdpcmTable[Index]/4
//  IF (data4bit AND 2) THEN Diff = Diff + AdpcmTable[Index]/2
//  IF (data4bit AND 4) THEN Diff = Diff + AdpcmTable[Index]/1
//
//And, a note on the second/third lines (with clipping-error):
//
//  Max(+7FFFh) leaves -8000h unclipped (can happen if initial PCM16 was -8000h)
//  Min(-7FFFh) clips -8000h to -7FFFh (possibly unlike windows .WAV files?)

#define IMAMax(samp) (samp > 0x7FFF) ? ((short)0x7FFF) : samp
#define IMAMin(samp) (samp < -0x7FFF) ? ((short)-0x7FFF) : samp
#define IMAIndexMinMax(index,min,max) (index > max) ? max : ((index < min) ? min : index)

void NDSSamp::process_nibble(unsigned char data4bit, int& Index, int& Pcm16bit)
{
	//int Diff = ((Data4bit & 7)*2+1)*AdpcmTable[Index]/8;
	int Diff = AdpcmTable[Index]/8;
	if (data4bit & 1) Diff = Diff + AdpcmTable[Index]/4;
	if (data4bit & 2) Diff = Diff + AdpcmTable[Index]/2;
	if (data4bit & 4) Diff = Diff + AdpcmTable[Index]/1;

	if ((data4bit & 8)==0) Pcm16bit = IMAMax(Pcm16bit+Diff);
	if ((data4bit & 8)==8) Pcm16bit = IMAMin(Pcm16bit-Diff);
	Index = IMAIndexMinMax (Index+IMA_IndexTable[data4bit & 7],0,88);
}



void NDSSamp::clamp_step_index(int& stepIndex)
{
    if (stepIndex < 0 ) stepIndex = 0;
    if (stepIndex > 88) stepIndex = 88;
}

void NDSSamp::clamp_sample(int& decompSample)
{
    if (decompSample < -32768) decompSample = -32768;
    if (decompSample >  32767) decompSample =  32767;
}
//
//void NDSSamp::process_nibble(unsigned char code, int& stepIndex, int& decompSample)
//{
//    unsigned step;
//    int diff;
//    
//    code &= 0x0F;
//    
//    step = ADPCMTable[stepIndex];
//    diff = step >> 3;
//    if (code & 1) diff += step >> 2;
//    if (code & 2) diff += step >> 1;
//    if (code & 4) diff += step;
//    if (code & 8)	decompSample -= diff;
//    else 		decompSample += diff;
//    clamp_sample(decompSample);
//    stepIndex += IMA_IndexTable[code];
//    clamp_step_index(stepIndex);
//}
