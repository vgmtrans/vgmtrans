#pragma once
#include "VGMInstrSet.h"
#include "VGMItem.h"
#include <math.h>

// All of the ADSR calculations herein (except where inaccurate) are derived from Neill Corlett's work in
// reverse-engineering the Playstation 1/2 SPU unit.  Rock on, Neill.

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


/*typedef struct _ADSR
{
	double attack_time;			//in seconds
	USHORT attack_transform;
	double decay_time;			//in seconds
	double sustain_level;		//as a percentage
	USHORT release_transform;
	double release_time;		//in seconds
} ADSR;*/

#define LINEAR_RELEASE_COMPENSATION 1//5.4000000000//5.200000000


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


template <class T> void PSXConvADSR(T* realADSR, unsigned short ADSR1, unsigned short ADSR2)
{
	unsigned char ArMode;
	unsigned char Ar;
	unsigned char Dr;
	unsigned char Sl;
	unsigned char Rr;
	unsigned char RrMode;
	int rateIncTable[8] = { 0, 4, 6, 8, 9, 10, 11, 12 };
	long envelope_level;
	long sustain_envelope_level;
	double samples;
	unsigned long rate;
	unsigned long remainder;
	double timeInSecs;
	double theRate;
	int l;

	if (!bRateTableInitialized)
	{
		InitADSR();
		bRateTableInitialized = true;
	}

	ArMode = (ADSR1 & 0x8000) > 0;	// if 1, then Exponential, else linear

	//to get the dls 32 bit time cents, take log base 2 of number of seconds * 1200 * 65536 (dls1v11a.pdf p25).
	Ar = (ADSR1 & 0x7F00) >> 8;
	if (RateTable[(Ar^0x7F)-0x10 + 32] == 0)		//cause we don't want to take the log of 0
		realADSR->attack_time = 0;
	else
	{
		if (ArMode == 0)	//if linear Ar Mode
		{
			rate = RateTable[(Ar^0x7F)-0x10 + 32];
			samples = 0x7FFFFFFF / rate;
		}
		else if (ArMode == 1)
		{
			rate = RateTable[(Ar^0x7F)-0x10 + 32];
			samples = 0x60000000 / rate;
			remainder = 0x60000000 % rate;
			rate = RateTable[(Ar^0x7F)-0x18 + 32];
			samples += (0x1FFFFFFF+remainder) / rate;
		}
		timeInSecs = (samples/48000);
		if (ArMode == 0)		//if it's linear
			timeInSecs *=  LINEAR_RELEASE_COMPENSATION;
		realADSR->attack_time = timeInSecs;
	}


	//Decay Time
	Dr = (ADSR1 & 0x7F00) >> 8;
	Sl = ADSR1 & 0x000F;
	envelope_level = 0x7FFFFFFF;
	
	if (((ADSR1 & 0x00F0) >> 4) == 0)		//cause we don't want to take the log of 0
		realADSR->decay_time = (long)ceil((log(0.001)/log((double)2)) * 1200 * 65536);	//dls seems to want .001 instead of 0 anyways
	else
	{
		l=0;
		rate = RateTable[(4*(Dr^0x1F))-0x18+0 + 32];
		if (rate != 0)
		{
			for (l=0; envelope_level>>27 > Sl; l++)
			{
				envelope_level-=rate;
				if ( ((envelope_level>>28) & 0x7) != 7)
					rate = RateTable[(4*(Dr^0x1F))-0x18+ rateIncTable[(int)(ceil(((double)envelope_level / 268435455.875) - 1))] + 32];
			}
		}

		samples = l;
		timeInSecs = (samples/48000);
		if (timeInSecs == 0)
			timeInSecs = 0.001;			//it just prefers it this way for some reason
		else
		{
			theRate = timeInSecs / (0x7FFFFFFF - envelope_level);
			timeInSecs = 0x7FFFFFFF * theRate;	// decay time is actually time to 0 vol, not the sustain level, in the dls spec.
		}
		realADSR->decay_time = timeInSecs;
	}

	//Sustain Level
	realADSR->sustain_level = (double)envelope_level/(double)0x7FFFFFFF;//(long)ceil((double)envelope_level * 0.030517578139210854);	//in DLS, sustain level is measured as a percentage
	

	//Release Time
	Rr = ADSR2 & 0x001F;
	RrMode = ((ADSR2 & 0x0020) > 0);

	sustain_envelope_level = envelope_level;
	
	if (RrMode == 0)	//if linear Rr Mode
	{
		rate = RateTable[(4*(Rr^0x1F))-0x0C + 32];
		if (rate != 0)
			samples = ((double)envelope_level / (double)rate);
		else
			samples = 0;
	}
	else if (RrMode == 1)
	{
		rate = RateTable[(4*(Rr^0x1F))-0x18+0 + 32];
		if (rate != 0)
		{
			for (l=0; (envelope_level > 0); l++)
			{
				envelope_level-=rate;
				if (((envelope_level>>28) & 0x7) != 7)
					rate = RateTable[(4*(Rr^0x1F))-0x18+ rateIncTable[(int)(ceil((double)envelope_level / 268435455.875) - 1)] + 32];
				//if (((double)envelope_level / 268435455.875) == 0)
				//	printf("FUCK!");
				//if (rate == 0)
				//	printf("doublefuck");
			}
			samples = l;
		}
		else
			samples = 0;
	}
	timeInSecs = samples/48000;
	if (timeInSecs == 0)
		timeInSecs = 0.001;
	else
	{
		theRate = timeInSecs / sustain_envelope_level;
		timeInSecs = 0x7FFFFFFF * theRate;	//the release time value is more like a rate.  It is the time from max value to 0, not from sustain level.
		if (RrMode == 0) // if it's linear
			timeInSecs *=  LINEAR_RELEASE_COMPENSATION;
	}
	realADSR->release_time = timeInSecs;

	//Calculations are done, so now add the articulation data
	//artic->AddADSR(attack_time, ArMode, decay_time, sustain_lev, release_time, 0);
	return;
}


class PSXSamp
	: public VGMSamp
{
public:
	//PSXSamp(VGMInstrSet* parentSet, ULONG offset, DWORD compressedSize = 0);
	PSXSamp(VGMSampColl* sampColl, ULONG offset, ULONG length, ULONG dataOffset,
				 ULONG dataLen, BYTE nChannels, USHORT theBPS,
				 ULONG theRate, wstring name);
	virtual ~PSXSamp(void);

	virtual double GetCompressionRatio();	// ratio of space conserved.  should generally be > 1
											// used to calculate both uncompressed sample size and loopOff after conversion
	virtual void ConvertToStdWave(BYTE* buf);

	//void SetCompressedSize(DWORD compSize);
	//int UncompSample(SHORT* uncompBuf);
	
private:
	void DecompVAGBlk(s16 *pSmp, VAGBlk *pVBlk, f32 *prev1, f32 *prev2);
	

public:
	VGMInstrSet* parentInstrSet;

	//int num;	//each sample has a number, figured by its order within the sample_section
	DWORD dwCompSize;
	DWORD dwUncompSize;
	BOOL bLoops;
};