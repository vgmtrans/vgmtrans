#pragma once

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
    uint8_t filter:2;
    uint8_t range:4;
  } flag;

  uint8_t brr[8];                     //Compressed samples
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
uint32_t emulateSDSPGAIN (uint8_t gain, int16_t env_from, int16_t env_to, int16_t *env_after_ptr, double *sf2_envelope_time_ptr);
void convertSNESADSR(uint8_t adsr1,
                     uint8_t adsr2,
                     uint8_t gain,
                     uint16_t env_from,
                     double *ptr_attack_time,
                     double *ptr_decay_time,
                     double *ptr_sustain_level,
                     double *ptr_sustain_time,
                     double *ptr_release_time);

template<class T>
void snesConvADSR(T *rgn, uint8_t adsr1, uint8_t adsr2, uint8_t gain) {
  bool adsr_enabled = (adsr1 & 0x80) != 0;

  if (adsr_enabled) {
    // ADSR mode
    // uint8_t ar = adsr1 & 0x0f;
    // uint8_t dr = (adsr1 & 0x70) >> 4;
    uint8_t sl = (adsr2 & 0xe0) >> 5;
    // uint8_t sr = adsr2 & 0x1f;

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
  SNESSampColl(const std::string& format, RawFile* rawfile, uint32_t offset, uint32_t maxNumSamps = 256);
  SNESSampColl(const std::string& format, VGMInstrSet* instrset, uint32_t offset, uint32_t maxNumSamps = 256);
  SNESSampColl(const std::string& format, RawFile* rawfile, uint32_t offset, const std::vector<uint8_t>& targetSRCNs, std::string name = "SNESSampColl");
  SNESSampColl(const std::string& format, VGMInstrSet* instrset, uint32_t offset, const std::vector<uint8_t>& targetSRCNs, std::string name = "SNESSampColl");
  ~SNESSampColl() override;

  bool parseSampleInfo() override;

  static bool isValidSampleDir(const RawFile *file, uint32_t spcDirEntAddr, bool validateSample);

 protected:
  VGMHeader *spcDirHeader{nullptr};
  uint32_t spcDirAddr;
  std::vector<uint8_t> targetSRCNs;

  void setDefaultTargets(uint32_t maxNumSamps);
};

// ********
// SNESSamp
// ********

class SNESSamp
    : public VGMSamp {
 public:
  SNESSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset,
           uint32_t dataLen, uint32_t loopOffset, std::string name = "BRR");
  ~SNESSamp() override;

  static uint32_t getSampleLength(const RawFile *file, uint32_t offset, bool &loop);

  double compressionRatio() const override;

 private:
  std::vector<uint8_t> decode() override;
  static void decompBRRBlk(int16_t *pSmp, const BRRBlk *pVBlk, int32_t *prev1, int32_t *prev2);

 private:
  uint32_t brrLoopOffset;
};
