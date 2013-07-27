#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMItem.h"
#include "ScaleConversion.h"
#include <math.h>

// All of the ADSR calculations herein (except where inaccurate) are derived from Neill Corlett's work in
// reverse-engineering the Playstation 1/2 SPU unit.

//**************************************************************************************************
// Type Redefinitions

typedef void v0;

#ifdef	__cplusplus
#if defined __BORLANDC__
typedef bool b8;
#else
typedef unsigned char b8;
#endif
#else
typedef	char b8;
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#if defined _MSC_VER || defined __BORLANDC__
typedef unsigned __int64 u64;
#else
typedef	unsigned long long int u64;
#endif

typedef char s8;
typedef short s16;
typedef int s32;
#if defined _MSC_VER || defined __BORLANDC__
typedef __int64 s64;
#else
typedef	long long int s64;
#endif

typedef float f32;
typedef double f64;
typedef long double f80;
//***********************************************************************************************

class DLSArt;

static unsigned long RateTable[160];
static bool bRateTableInitialized = 0;



//VAG format -----------------------------------
typedef struct _VAGHdr							//File Header
{
	u32	id;										//ID - "VAGp"
	u32	ver;                                    //Version - 0x20
	u32	__r1;
	u32	len;                                    //Length of data
	u32	rate;                                   //Sample rate
	u32	__r2[3];
	s8	title[32];
} VAGHdr;


typedef struct _VAGBlk							//Sample Block
{
	struct
	{
		u8	range:4;
		u8	filter:4;
	};

	struct
	{
		b8	end:1;								//End block
		b8	looping:1;							//VAG loops
		b8	loop:1;								//Loop start point
	} flag;

	s8	brr[14];								//Compressed samples
} VAGBlk;


//InitADSR is shamelessly ripped from P.E.Op.S. cause I'm lazy
static void InitADSR(void)                                    // INIT ADSR
{
 unsigned long r,rs,rd;int i;

 memset(RateTable,0,sizeof(unsigned long)*160);        // build the rate table according to Neill's rules

 r=3;rs=1;rd=0;

 for(i=32;i<160;i++)                                   // we start at pos 32 with the real values... everything before is 0
  {
   if(r<0x3FFFFFFF)
    {
     r+=rs;
     rd++;if(rd==5) {rd=1;rs*=2;}
    }
   if(r>0x3FFFFFFF) r=0x3FFFFFFF;

   RateTable[i]=r;
  }
}

inline int RoundToZero(int val)
{
	if (val < 0)
		val = 0;
	return val;
}

template <class T> void PSXConvADSR(T* realADSR, unsigned short ADSR1, unsigned short ADSR2, bool bPS2)
{
	
	U8 Am = (ADSR1 & 0x8000) > 0;	// if 1, then Exponential, else linear
	U8 Ar = (ADSR1 & 0x7F00) >> 8;
	U8 Dr = (ADSR1 & 0x00F0) >> 4;
	U8 Sl = ADSR1 & 0x000F;
	U8 Rm = (ADSR2 & 0x0020) > 0;
	U8 Rr = ADSR2 & 0x001F;

	// The following are unimplemented in conversion (because DLS does not support Sustain Rate)
	U8 Sm = (ADSR2 & 0x8000) > 0;
	U8 Sd = (ADSR2 & 0x4000) > 0;
	U8 Sr = (ADSR2 >> 6) & 0x7F;

	PSXConvADSR(realADSR, Am, Ar, Dr, Sl, Sm, Sd, Sr, Rm, Rr, bPS2);
}


template <class T> void PSXConvADSR(T* realADSR,
									U8 Am, U8 Ar, U8 Dr, U8 Sl,
									U8 Sm, U8 Sd, U8 Sr, U8 Rm, U8 Rr, bool bPS2)
{
	// Make sure all the ADSR values are within the valid ranges
	if (((Am & 0xFE) != 0) ||
		((Ar & 0x80) != 0) ||
		((Dr & 0xF0) != 0) ||
		((Sl & 0xF0) != 0) ||
		((Rm & 0xFE) != 0) ||
		((Rr & 0xE0) != 0) ||
		((Sm & 0xFE) != 0) ||
		((Sd & 0xFE) != 0) ||
		((Sr & 0x80) != 0))
	{
		pRoot->AddLogItem(new LogItem(L"PSX ADSR Out Of Range.", LOG_LEVEL_ERR, L"PSXConvADSR"));
		return;
	}

	// PS1 games use 44k, PS2 uses 48k
	double sampleRate = bPS2 ? 48000 : 44100;
	

	int rateIncTable[8] = { 0, 4, 6, 8, 9, 10, 11, 12 };
	long envelope_level;
	//long sustain_envelope_level;
	double samples;
	unsigned long rate;
	unsigned long remainder;
	double timeInSecs;
	//double theRate;
	int l;

	if (!bRateTableInitialized)
	{
		InitADSR();
		bRateTableInitialized = true;
	}

	//to get the dls 32 bit time cents, take log base 2 of number of seconds * 1200 * 65536 (dls1v11a.pdf p25).

//	if (RateTable[(Ar^0x7F)-0x10 + 32] == 0)
//		realADSR->attack_time = 0;
//	else
//	{
	if ((Ar^0x7F) < 0x10)
		Ar = 0;
		if (Am == 0)	//if linear Ar Mode
		{
			rate = RateTable[RoundToZero( (Ar^0x7F)-0x10 ) + 32];
			samples = ceil(0x7FFFFFFF / (double)rate);	
		}
		else if (Am == 1)
		{
			rate = RateTable[RoundToZero( (Ar^0x7F)-0x10 ) + 32];
			samples = 0x60000000 / rate;
			remainder = 0x60000000 % rate;
			rate = RateTable[RoundToZero( (Ar^0x7F)-0x18 ) + 32];
			samples += ceil(max(0, 0x1FFFFFFF-remainder) / (double)rate);
		}
		timeInSecs = samples / sampleRate;
		realADSR->attack_time = timeInSecs;
//	}


	//Decay Time

	envelope_level = 0x7FFFFFFF;
	
	bool bSustainLevFound = false;
	ULONG realSustainLevel;
	for (l=0; envelope_level > 0; l++)		//DLS decay rate value is to -96db (silence) not the sustain level
	{
		if (4*(Dr^0x1F) < 0x18)
			Dr = 0;
		switch ((envelope_level>>28)&0x7)
		{
		case 0: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+0 ) +  32]; break;
		case 1: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+4 ) +  32]; break;
		case 2: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+6 ) +  32]; break;
		case 3: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+8 ) +  32]; break;
		case 4: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+9 ) +  32]; break;
		case 5: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+10) + 32]; break;
		case 6: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+11) + 32]; break;
		case 7: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+12) + 32]; break;
		}
		if (!bSustainLevFound && ((envelope_level>>27)&0xF) <= Sl)
		{
			realSustainLevel = envelope_level;
			bSustainLevFound = true;
		}
	}
	samples = l;
	timeInSecs = samples / sampleRate;
	realADSR->decay_time = timeInSecs;

	// Sustain Rate

	envelope_level = 0x7FFFFFFF;
	if (Sd == 0)		// increasing... we won't even bother
	{
		realADSR->sustain_time = -1;
	}
	else
	{
		if (Sr == 0x7F)
			realADSR->sustain_time = -1;		// this is actually infinite
		else
		{
			if (Sm == 0)	// linear
			{
				rate = RateTable[RoundToZero( (Sr^0x7F)-0x0F ) + 32];
				samples = ceil(0x7FFFFFFF / (double)rate);	
			}
			else
			{
				for (l=0; envelope_level > 0; l++)		//DLS decay rate value is to -96db (silence) not the sustain level
				{
					switch ((envelope_level>>28)&0x7)
					{
					case 0: envelope_level -= RateTable[RoundToZero( (Sr^0x7F)-0x1B+0 ) +  32]; break;
					case 1: envelope_level -= RateTable[RoundToZero( (Sr^0x7F)-0x1B+4 ) +  32]; break;
					case 2: envelope_level -= RateTable[RoundToZero( (Sr^0x7F)-0x1B+6 ) +  32]; break;
					case 3: envelope_level -= RateTable[RoundToZero( (Sr^0x7F)-0x1B+8 ) +  32]; break;
					case 4: envelope_level -= RateTable[RoundToZero( (Sr^0x7F)-0x1B+9 ) +  32]; break;
					case 5: envelope_level -= RateTable[RoundToZero( (Sr^0x7F)-0x1B+10) + 32]; break;
					case 6: envelope_level -= RateTable[RoundToZero( (Sr^0x7F)-0x1B+11) + 32]; break;
					case 7: envelope_level -= RateTable[RoundToZero( (Sr^0x7F)-0x1B+12) + 32]; break;
					}
				}
				samples = l;

			}
			timeInSecs = samples / sampleRate;
			realADSR->sustain_time = /*Sm ? timeInSecs : */LinAmpDecayTimeToLinDBDecayTime(timeInSecs);
		}
	}

	//Sustain Level
	//realADSR->sustain_level = (double)envelope_level/(double)0x7FFFFFFF;//(long)ceil((double)envelope_level * 0.030517578139210854);	//in DLS, sustain level is measured as a percentage
	if (Sl == 0)
		realSustainLevel = 0x07FFFFFF;
	realADSR->sustain_level = realSustainLevel/(double)0x7FFFFFFF;


	// If decay is going unused, and there's a sustain rate with sustain level close to max...
	//  we'll put the sustain_rate in place of the decay rate.
	if ((realADSR->decay_time < 2 || (Dr == 0x0F && Sl >= 0x0C)) && Sr < 0x7E && Sd == 1)
	{
		realADSR->sustain_level = 0;
		realADSR->decay_time = realADSR->sustain_time;
		//realADSR->decay_time = 0.5;
	}

	//Release Time

	//sustain_envelope_level = envelope_level;
	
	envelope_level = 0x7FFFFFFF;	//We do this because we measure release time from max volume to 0, not from sustain level to 0

	if (Rm == 0)	//if linear Rr Mode
	{
		rate = RateTable[RoundToZero( (4*(Rr^0x1F))-0x0C ) + 32];

		if (rate != 0)
			samples = ceil((double)envelope_level / (double)rate);
		else
			samples = 0;
	}
	else if (Rm == 1)
	{
		if ((Rr^0x1F)*4 < 0x18)
			Rr = 0;
		for (l=0; envelope_level > 0; l++)
		{
			switch ((envelope_level>>28)&0x7)
			{
			case 0: envelope_level -= RateTable[RoundToZero( (4*(Rr^0x1F))-0x18+0 ) +  32]; break;
			case 1: envelope_level -= RateTable[RoundToZero( (4*(Rr^0x1F))-0x18+4 ) +  32]; break;
			case 2: envelope_level -= RateTable[RoundToZero( (4*(Rr^0x1F))-0x18+6 ) +  32]; break;
			case 3: envelope_level -= RateTable[RoundToZero( (4*(Rr^0x1F))-0x18+8 ) +  32]; break;
			case 4: envelope_level -= RateTable[RoundToZero( (4*(Rr^0x1F))-0x18+9 ) +  32]; break;
			case 5: envelope_level -= RateTable[RoundToZero( (4*(Rr^0x1F))-0x18+10) + 32]; break;
			case 6: envelope_level -= RateTable[RoundToZero( (4*(Rr^0x1F))-0x18+11) + 32]; break;
			case 7: envelope_level -= RateTable[RoundToZero( (4*(Rr^0x1F))-0x18+12) + 32]; break;
			}
		}
		samples = l;
	}
	timeInSecs = samples / sampleRate;

	//theRate = timeInSecs / sustain_envelope_level;
	//timeInSecs = 0x7FFFFFFF * theRate;	//the release time value is more like a rate.  It is the time from max value to 0, not from sustain level.
	//if (Rm == 0) // if it's linear
	//	timeInSecs *=  LINEAR_RELEASE_COMPENSATION;

	realADSR->release_time = /*Rm ? timeInSecs : */LinAmpDecayTimeToLinDBDecayTime(timeInSecs);

	// We need to compensate the decay and release times to represent them as the time from full vol to -100db 
	// where the drop in db is a fixed amount per time unit (SoundFont2 spec for vol envelopes, pg44.)
	//  We assume the psx envelope is using a linear scale wherein envelope_level / 2 == half loudness.  
	//  For a linear release mode (Rm == 0), the time to reach half volume is simply half the time to reach 0.  
	// Half perceived loudness is -10db. Therefore, time_to_half_vol * 10 == full_time * 5 == the correct SF2 time
	//realADSR->decay_time = LinAmpDecayTimeToLinDBDecayTime(realADSR->decay_time);	
	//realADSR->sustain_time = LinAmpDecayTimeToLinDBDecayTime(realADSR->sustain_time);
	//realADSR->release_time = LinAmpDecayTimeToLinDBDecayTime(realADSR->release_time);



	//Calculations are done, so now add the articulation data
	//artic->AddADSR(attack_time, Am, decay_time, sustain_lev, release_time, 0);
	return;
}


class PSXSampColl
	: public VGMSampColl
{
public:
	PSXSampColl(const string& format, RawFile* rawfile, U32 offset, U32 length = 0);
	PSXSampColl(const string& format, VGMInstrSet* instrset, U32 offset, U32 length = 0);

	virtual bool GetSampleInfo();		//retrieve sample info, including pointers to data, # channels, rate, etc.
	static PSXSampColl* SearchForPSXADPCM (RawFile* file, const string& format);
};



class PSXSamp
	: public VGMSamp
{
public:
	//PSXSamp(VGMInstrSet* parentSet, ULONG offset, DWORD compressedSize = 0);
	PSXSamp(VGMSampColl* sampColl, ULONG offset, ULONG length, ULONG dataOffset,
				 ULONG dataLen, BYTE nChannels, USHORT theBPS,
				 ULONG theRate, wstring name, bool bSetLoopOnConversion = true);
	virtual ~PSXSamp(void);

	virtual double GetCompressionRatio();	// ratio of space conserved.  should generally be > 1
											// used to calculate both uncompressed sample size and loopOff after conversion
	virtual void ConvertToStdWave(BYTE* buf);
	void SetLoopOnConversion(bool bDoIt) { bSetLoopOnConversion = bDoIt; }

	//void SetCompressedSize(DWORD compSize);
	//int UncompSample(SHORT* uncompBuf);
	
private:
	void DecompVAGBlk(s16 *pSmp, VAGBlk *pVBlk, f32 *prev1, f32 *prev2);
	

public:
	VGMInstrSet* parentInstrSet;

	bool bSetLoopOnConversion;
	//bool bUseADPCMLoopInfoOnConversion;
	DWORD dwCompSize;
	DWORD dwUncompSize;
	BOOL bLoops;
};