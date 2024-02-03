#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMItem.h"
#include "ScaleConversion.h"
#include "Root.h"

// All of the ADSR calculations herein (except where inaccurate) are derived from Neill Corlett's work in
// reverse-engineering the Playstation 1/2 SPU unit.

//**************************************************************************************************
// Type Redefinitions

typedef void v0;

#ifdef    __cplusplus
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
typedef unsigned long long int u64;
#endif

typedef char s8;
typedef short s16;
typedef int s32;
#if defined _MSC_VER || defined __BORLANDC__
typedef __int64 s64;
#else
typedef long long int s64;
#endif

typedef float f32;
typedef double f64;
typedef long double f80;
//***********************************************************************************************

class DLSArt;

static unsigned long RateTable[160];
static bool bRateTableInitialized = 0;


//VAG format -----------------------------------
//File Header
typedef struct _VAGHdr {
  u32 id;                                     //ID - "VAGp"
  u32 ver;                                    //Version - 0x20
  u32 __r1;
  u32 len;                                    //Length of data
  u32 rate;                                   //Sample rate
  u32 __r2[3];
  s8 title[32];
} VAGHdr;


//Sample Block
typedef struct _VAGBlk {
  struct {
    u8 range:4;
    u8 filter:4;
  };

  struct {
    b8 end:1;                                 //End block
    b8 looping:1;                             //VAG loops
    b8 loop:1;                                //Loop start point
  } flag;

  s8 brr[14];                                //Compressed samples
} VAGBlk;


//InitADSR is shamelessly ripped from P.E.Op.S
static void InitADSR(void)
{
  unsigned long r, rs, rd;
  int i;

  // build the rate table according to Neill's rules
  memset(RateTable, 0, sizeof(unsigned long) * 160);

  r = 3;
  rs = 1;
  rd = 0;

  // we start at pos 32 with the real values... everything before is 0
  for (i = 32; i < 160; i++) {
    if (r < 0x3FFFFFFF) {
      r += rs;
      rd++;
      if (rd == 5) {
        rd = 1;
        rs *= 2;
      }
    }
    if (r > 0x3FFFFFFF) r = 0x3FFFFFFF;

    RateTable[i] = r;
  }
}

inline int RoundToZero(int val) {
  if (val < 0)
    val = 0;
  return val;
}

inline constexpr uint16_t ComposePSXADSR1(uint8_t am, uint8_t ar, uint8_t dr, uint8_t sl) {
  return ((am & 1) << 15) | ((ar & 0x7f) << 8) | ((dr & 0xf) << 4) | (sl & 0x0f);
}

inline constexpr uint16_t ComposePSXADSR2(uint8_t sm, uint8_t sd, uint8_t sr, uint8_t rm,
                                         uint8_t rr) {
  return ((sm & 1) << 15) | ((sd & 1) << 14) | ((sr & 0x7f) << 6) | ((rm & 1) << 5) | (rr & 0x1f);
}

template<class T>
void PSXConvADSR(T *realADSR, unsigned short ADSR1, unsigned short ADSR2, bool bPS2) {

  uint8_t Am = (ADSR1 & 0x8000) >> 15;    // if 1, then Exponential, else linear
  uint8_t Ar = (ADSR1 & 0x7F00) >> 8;
  uint8_t Dr = (ADSR1 & 0x00F0) >> 4;
  uint8_t Sl = ADSR1 & 0x000F;
  uint8_t Rm = (ADSR2 & 0x0020) >> 5;
  uint8_t Rr = ADSR2 & 0x001F;

  // The following are unimplemented in conversion (because DLS and SF2 do not support Sustain Rate)
  uint8_t Sm = (ADSR2 & 0x8000) >> 15;
  uint8_t Sd = (ADSR2 & 0x4000) >> 14;
  uint8_t Sr = (ADSR2 >> 6) & 0x7F;

  PSXConvADSR(realADSR, Am, Ar, Dr, Sl, Sm, Sd, Sr, Rm, Rr, bPS2);
}


template<class T>
void PSXConvADSR(T *realADSR,
                 uint8_t Am, uint8_t Ar, uint8_t Dr, uint8_t Sl,
                 uint8_t Sm, uint8_t Sd, uint8_t Sr, uint8_t Rm, uint8_t Rr, bool bPS2) {
  // Make sure all the ADSR values are within the valid ranges
  if (((Am & ~0x01) != 0) ||
      ((Ar & ~0x7F) != 0) ||
      ((Dr & ~0x0F) != 0) ||
      ((Sl & ~0x0F) != 0) ||
      ((Rm & ~0x01) != 0) ||
      ((Rr & ~0x1F) != 0) ||
      ((Sm & ~0x01) != 0) ||
      ((Sd & ~0x01) != 0) ||
      ((Sr & ~0x7F) != 0)) {
    pRoot->AddLogItem(new LogItem("PSX ADSR Out Of Range.", LOG_LEVEL_ERR, "PSXConvADSR"));
    return;
  }

  // PS1 games use 44k, PS2 uses 48k
  double sampleRate = bPS2 ? 48000 : 44100;


  int rateIncTable[8] = {0, 4, 6, 8, 9, 10, 11, 12};
  long envelope_level;
  double samples;
  unsigned long rate;
  unsigned long remainder;
  double timeInSecs;
  int l;

  if (!bRateTableInitialized) {
    InitADSR();
    bRateTableInitialized = true;
  }

  //to get the dls 32 bit time cents, take log base 2 of number of seconds * 1200 * 65536 (dls1v11a.pdf p25).

//	if (RateTable[(Ar^0x7F)-0x10 + 32] == 0)
//		realADSR->attack_time = 0;
//	else
//	{
  if ((Ar ^ 0x7F) < 0x10)
    Ar = 0;
  //if linear Ar Mode
  if (Am == 0) {
    rate = RateTable[RoundToZero((Ar ^ 0x7F) - 0x10) + 32];
    samples = ceil(0x7FFFFFFF / (double) rate);
  }
  else if (Am == 1) {
    rate = RateTable[RoundToZero((Ar ^ 0x7F) - 0x10) + 32];
    samples = 0x60000000 / rate;
    remainder = 0x60000000 % rate;
    rate = RateTable[RoundToZero((Ar ^ 0x7F) - 0x18) + 32];
    samples += ceil(fmax(0, 0x1FFFFFFF - remainder) / (double) rate);
  }
  timeInSecs = samples / sampleRate;
  realADSR->attack_time = timeInSecs;
//	}


  //Decay Time

  envelope_level = 0x7FFFFFFF;

  bool bSustainLevFound = false;
  uint32_t realSustainLevel;
  //DLS decay rate value is to -96db (silence) not the sustain level
  for (l = 0; envelope_level > 0; l++) {
    if (4 * (Dr ^ 0x1F) < 0x18)
      Dr = 0;
    switch ((envelope_level>>28)&0x7) {
      case 0: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+0 ) +  32]; break;
      case 1: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+4 ) +  32]; break;
      case 2: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+6 ) +  32]; break;
      case 3: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+8 ) +  32]; break;
      case 4: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+9 ) +  32]; break;
      case 5: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+10) + 32]; break;
      case 6: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+11) + 32]; break;
      case 7: envelope_level -= RateTable[RoundToZero( (4*(Dr^0x1F))-0x18+12) + 32]; break;
    }
    if (!bSustainLevFound && ((envelope_level >> 27) & 0xF) <= Sl) {
      realSustainLevel = envelope_level;
      bSustainLevFound = true;
    }
  }
  samples = l;
  timeInSecs = samples / sampleRate;
  realADSR->decay_time = timeInSecs;

  // Sustain Rate

  envelope_level = 0x7FFFFFFF;
  // increasing... we won't even bother
  if (Sd == 0) {
    realADSR->sustain_time = -1;
  }
  else {
    if (Sr == 0x7F)
      realADSR->sustain_time = -1;        // this is actually infinite
    else {
      // linear
      if (Sm == 0) {
        rate = RateTable[RoundToZero((Sr ^ 0x7F) - 0x0F) + 32];
        samples = ceil(0x7FFFFFFF / (double) rate);
      }
      else {
        l = 0;
        //DLS decay rate value is to -96db (silence) not the sustain level
        while (envelope_level > 0) {
          long envelope_level_diff;
          long envelope_level_target;

          switch ((envelope_level >> 28) & 0x7) {
            case 0: envelope_level_target = 0x00000000; envelope_level_diff = RateTable[RoundToZero( (Sr^0x7F)-0x1B+0 ) +  32]; break;
            case 1: envelope_level_target = 0x0fffffff; envelope_level_diff = RateTable[RoundToZero( (Sr^0x7F)-0x1B+4 ) +  32]; break;
            case 2: envelope_level_target = 0x1fffffff; envelope_level_diff = RateTable[RoundToZero( (Sr^0x7F)-0x1B+6 ) +  32]; break;
            case 3: envelope_level_target = 0x2fffffff; envelope_level_diff = RateTable[RoundToZero( (Sr^0x7F)-0x1B+8 ) +  32]; break;
            case 4: envelope_level_target = 0x3fffffff; envelope_level_diff = RateTable[RoundToZero( (Sr^0x7F)-0x1B+9 ) +  32]; break;
            case 5: envelope_level_target = 0x4fffffff; envelope_level_diff = RateTable[RoundToZero( (Sr^0x7F)-0x1B+10) + 32]; break;
            case 6: envelope_level_target = 0x5fffffff; envelope_level_diff = RateTable[RoundToZero((Sr ^ 0x7F) - 0x1B + 11) + 32]; break;
            case 7: envelope_level_target = 0x6fffffff; envelope_level_diff = RateTable[RoundToZero((Sr ^ 0x7F) - 0x1B + 12) + 32]; break;
          }

          long steps = (envelope_level - envelope_level_target + (envelope_level_diff - 1)) / envelope_level_diff;
          envelope_level -= (envelope_level_diff * steps);
          l += steps;
        }
        samples = l;

      }
      timeInSecs = samples / sampleRate;
      realADSR->sustain_time = /*Sm ? timeInSecs : */LinAmpDecayTimeToLinDBDecayTime(timeInSecs, 0x800);
    }
  }

  //Sustain Level
  //realADSR->sustain_level = (double)envelope_level/(double)0x7FFFFFFF;//(long)ceil((double)envelope_level * 0.030517578139210854);	//in DLS, sustain level is measured as a percentage
  if (Sl == 0)
    realSustainLevel = 0x07FFFFFF;
  realADSR->sustain_level = realSustainLevel / (double) 0x7FFFFFFF;


  // If decay is going unused, and there's a sustain rate with sustain level close to max...
  //  we'll put the sustain_rate in place of the decay rate.
  if ((realADSR->decay_time < 2 || (Dr == 0x0F && Sl >= 0x0C)) && Sr < 0x7E && Sd == 1) {
    realADSR->sustain_level = 0;
    realADSR->decay_time = realADSR->sustain_time;
    //realADSR->decay_time = 0.5;
  }

  //Release Time

  //sustain_envelope_level = envelope_level;

  //We do this because we measure release time from max volume to 0, not from sustain level to 0
  envelope_level = 0x7FFFFFFF;

  //if linear Rr Mode
  if (Rm == 0) {
    rate = RateTable[RoundToZero((4 * (Rr ^ 0x1F)) - 0x0C) + 32];

    if (rate != 0)
      samples = ceil((double) envelope_level / (double) rate);
    else
      samples = 0;
  }
  else if (Rm == 1) {
    if ((Rr ^ 0x1F) * 4 < 0x18)
      Rr = 0;
    for (l = 0; envelope_level > 0; l++) {
      switch ((envelope_level >> 28) & 0x7) {
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

  realADSR->release_time = /*Rm ? timeInSecs : */LinAmpDecayTimeToLinDBDecayTime(timeInSecs, 0x800);

  // We need to compensate the decay and release times to represent them as the time from full vol to -100db
  // where the drop in db is a fixed amount per time unit (SoundFont2 spec for vol envelopes, pg44.)
  //  We assume the psx envelope is using a linear scale wherein envelope_level / 2 == half loudness.
  //  For a linear release mode (Rm == 0), the time to reach half volume is simply half the time to reach 0.
  // Half perceived loudness is -10db. Therefore, time_to_half_vol * 10 == full_time * 5 == the correct SF2 time
  //realADSR->decay_time = LinAmpDecayTimeToLinDBDecayTime(realADSR->decay_time, 0x800);
  //realADSR->sustain_time = LinAmpDecayTimeToLinDBDecayTime(realADSR->sustain_time, 0x800);
  //realADSR->release_time = LinAmpDecayTimeToLinDBDecayTime(realADSR->release_time, 0x800);



  //Calculations are done, so now add the articulation data
  //artic->AddADSR(attack_time, Am, decay_time, sustain_lev, release_time, 0);
  return;
}


class PSXSampColl
    : public VGMSampColl {
 public:
  PSXSampColl(const std::string &format, RawFile *rawfile, uint32_t offset, uint32_t length = 0);
  PSXSampColl(const std::string &format, VGMInstrSet *instrset, uint32_t offset, uint32_t length = 0);
  PSXSampColl(const std::string &format,
              VGMInstrSet *instrset,
              uint32_t offset,
              uint32_t length,
              const std::vector<SizeOffsetPair> &vagLocations);

  virtual bool GetSampleInfo();        //retrieve sample info, including pointers to data, # channels, rate, etc.
  static PSXSampColl *SearchForPSXADPCM(RawFile *file, const std::string &format);
  static const std::vector<PSXSampColl *> SearchForPSXADPCMs(RawFile *file, const std::string &format);

 protected:
  std::vector<SizeOffsetPair> vagLocations;
};


class PSXSamp
    : public VGMSamp {
 public:
  PSXSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset,
          uint32_t dataLen, uint8_t nChannels, uint16_t theBPS,
          uint32_t theRate, std::string name, bool bSetLoopOnConversion = true);
  virtual ~PSXSamp(void);

  // ratio of space conserved.  should generally be > 1
  // used to calculate both uncompressed sample size and loopOff after conversion
  virtual double GetCompressionRatio();
  virtual void ConvertToStdWave(uint8_t *buf);
  void SetLoopOnConversion(bool bDoIt) { bSetLoopOnConversion = bDoIt; }

  static uint32_t GetSampleLength(RawFile *file, uint32_t offset, uint32_t endOffset, bool &loop);

 private:
  void DecompVAGBlk(s16 *pSmp, VAGBlk *pVBlk, f32 *prev1, f32 *prev2);


 public:
  bool bSetLoopOnConversion;
  uint32_t dwCompSize;
  uint32_t dwUncompSize;
  bool bLoops;
};
