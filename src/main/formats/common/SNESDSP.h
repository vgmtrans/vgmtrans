#pragma once

#include "base/Types.h"

#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMItem.h"
#include "ScaleConversion.h"

class VGMInstrSet;

typedef struct _BRRBlk                //Sample Block
{
  struct {
    bool end:1;                       //End block
    bool loop:1;                      //Loop start point
    u8 filter:2;
    u8 range:4;
  } flag;

  u8 brr[8];                     //Compressed samples
} BRRBlk;

// *************
// SNES Envelope
// *************

static unsigned const SDSP_COUNTER_RATES [32] =
{
  0x7800, // never fires
        2048, 1536,
  1280, 1024,  768,
   640,  512,  384,
   320,  256,  192,
   160,  128,   96,
    80,   64,   48,
    40,   32,   24,
    20,   16,   12,
    10,    8,    6,
     5,    4,    3,
           2,
           1
};

// Emulate GAIN envelope while (increase: env < env_to, or decrease: env > env_to)
// return elapsed time in sample count, and final env value if requested.
u32 emulateSDSPGAIN (u8 gain, s16 env_from, s16 env_to, s16 *env_after_ptr, double *sf2_envelope_time_ptr);
void convertSNESADSR(u8 adsr1,
                     u8 adsr2,
                     u8 gain,
                     u16 env_from,
                     double *ptr_attack_time,
                     double *ptr_decay_time,
                     double *ptr_sustain_level,
                     double *ptr_sustain_time,
                     double *ptr_release_time);

template<class T>
void snesConvADSR(T *rgn, u8 adsr1, u8 adsr2, u8 gain) {
  bool adsr_enabled = (adsr1 & 0x80) != 0;

  if (adsr_enabled) {
    // ADSR mode
    // u8 ar = adsr1 & 0x0f;
    // u8 dr = (adsr1 & 0x70) >> 4;
    u8 sl = (adsr2 & 0xe0) >> 5;
    // u8 sr = adsr2 & 0x1f;

    convertSNESADSR(adsr1,
                    adsr2,
                    gain,
                    0x7ff,
                    &rgn->attack_time,
                    &rgn->decay_time,
                    &rgn->sustain_level,
                    &rgn->sustain_time,
                    &rgn->release_time);

    // Merge decay and sustain into a single envelope, since DLS does not have sustain rate.
    if (sl == 7) {
      // no decay, use sustain as decay
      rgn->decay_time = rgn->sustain_time;
      if (rgn->sustain_time != -1) {
        rgn->sustain_level = 0;
      }
    }
    else if (rgn->sustain_time != -1) {
      double decibelAtSustainStart = ampToDb(rgn->sustain_level);
      double decayTimeRate = decibelAtSustainStart / 100.0;
      rgn->decay_time = (rgn->decay_time * decayTimeRate) + (rgn->sustain_time * (1.0 - decayTimeRate));

      // sustain finishes at zero volume
      rgn->sustain_level = 0;
    }
  }
  else {
    // TODO: GAIN mode
  }
}

// ************
// SNESSampColl
// ************

class SNESSampColl
    : public VGMSampColl {
 public:
  SNESSampColl(const std::string& format, RawFile* rawfile, u32 offset, u32 maxNumSamps = 256);
  SNESSampColl(const std::string& format, VGMInstrSet* instrset, u32 offset, u32 maxNumSamps = 256);
  SNESSampColl(const std::string& format, RawFile* rawfile, u32 offset, const std::vector<u8>& targetSRCNs, std::string name = "SNESSampColl");
  SNESSampColl(const std::string& format, VGMInstrSet* instrset, u32 offset, const std::vector<u8>& targetSRCNs, std::string name = "SNESSampColl");
  ~SNESSampColl() override;

  bool parseSampleInfo() override;

  static bool isValidSampleDir(const RawFile *file, u32 spcDirEntAddr, bool validateSample);

 protected:
  VGMHeader *spcDirHeader{nullptr};
  u32 spcDirAddr;
  std::vector<u8> targetSRCNs;

  void setDefaultTargets(u32 maxNumSamps);
};

// ********
// SNESSamp
// ********

class SNESSamp
    : public VGMSamp {
 public:
  SNESSamp(VGMSampColl *sampColl, u32 offset, u32 length, u32 dataOffset,
           u32 dataLen, u32 loopOffset, std::string name = "BRR");
  ~SNESSamp() override;

  static u32 getSampleLength(const RawFile *file, u32 offset, bool &loop);

  double compressionRatio() const override;

 private:
  std::vector<u8> decodeToNativePcm() override;
  static void decompBRRBlk(s16 *pSmp, const BRRBlk *pVBlk, s32 *prev1, s32 *prev2);

 private:
  u32 brrLoopOffset;
};
