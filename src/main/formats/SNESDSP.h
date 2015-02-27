#pragma once

#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMItem.h"
#include "ScaleConversion.h"

typedef struct _BRRBlk							//Sample Block
{
    struct
    {
        bool	end:1;							//End block
        bool	loop:1;							//Loop start point
        uint8_t	filter:2;
        uint8_t	range:4;
    } flag;

    uint8_t	brr[8];								//Compressed samples
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
uint32_t EmulateSDSPGAIN(uint8_t gain, int16_t env_from, int16_t env_to, int16_t * env_after_ptr, double * sf2_envelope_time_ptr);

// See Anomie's S-DSP document for technical details
// http://www.romhacking.net/documents/191/
//
// Also, see ScaleConversion.h to know linear-logarithmic conversion approach
template <class T> void SNESConvADSR(T* rgn, uint8_t adsr1, uint8_t adsr2, uint8_t gain)
{
    bool adsr_enabled = (adsr1 & 0x80) != 0;

    int16_t env;
    int16_t env_after;
    uint32_t samples;

    if (adsr_enabled) {
        // ADSR mode

        uint8_t ar = adsr1 & 0x0f;
        uint8_t dr = (adsr1 & 0x70) >> 4;
        uint8_t sl = (adsr2 & 0xe0) >> 5;
        uint8_t sr = adsr2 & 0x1f;

        // attack
        if (ar < 15) {
            rgn->attack_time = SDSP_COUNTER_RATES[ar * 2 + 1] * 64 / 32000.0;
        }
        else {
            rgn->attack_time = 2 / 32000.0;
        }
        env = 0x7FF;

        // decay
        int16_t env_sustain_start = env;
        if (sl == 7) {
            // no decay
            rgn->decay_time = 0;
        }
        else {
            uint8_t dr_rate = 0x10 | (dr << 1);
            EmulateSDSPGAIN(0xa0 | dr_rate, env, (sl << 8) | 0xff, &env_after, &rgn->decay_time); // exponential decrease
            env_sustain_start = env_after;
            env = env_after;
        }

        // sustain
        rgn->sustain_level = (sl + 1) / 8.0;
        if (sr == 0) {
            rgn->sustain_time = -1; // infinite
        }
        else {
            EmulateSDSPGAIN(0xa0 | sr, env, 0, &env_after, &rgn->sustain_time); // exponential decrease
        }

        // release
        // decrease envelope by 8 for every sample
        samples = (env_sustain_start + 7) / 8;
        rgn->release_time = LinAmpDecayTimeToLinDBDecayTime(samples / 32000.0, 0x7ff);

        // Merge decay and sustain into a single envelope, since DLS does not have sustain rate.
        if (sl == 7) {
            // no decay, use sustain as decay
            rgn->decay_time = rgn->sustain_time;
            if (rgn->sustain_time != -1) {
                rgn->sustain_level = 0;
            }
        }
        else if (rgn->sustain_time != -1) {
            double decibelAtSustainStart = ConvertPercentAmplitudeToAttenDB(rgn->sustain_level);
            double decayTimeRate = decibelAtSustainStart / -100.0;
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
        : public VGMSampColl
{
public:
    SNESSampColl(const std::string& format, RawFile* rawfile, uint32_t offset, uint32_t maxNumSamps = 256);
    SNESSampColl(const std::string& format, VGMInstrSet* instrset, uint32_t offset, uint32_t maxNumSamps = 256);
    SNESSampColl(const std::string& format, RawFile* rawfile, uint32_t offset, const std::vector<uint8_t>& targetSRCNs, std::wstring name = L"SNESSampColl");
    SNESSampColl(const std::string& format, VGMInstrSet* instrset, uint32_t offset, const std::vector<uint8_t>& targetSRCNs, std::wstring name = L"SNESSampColl");
    virtual ~SNESSampColl();

    virtual bool GetSampleInfo();		//retrieve sample info, including pointers to data, # channels, rate, etc.

    static bool IsValidSampleDir(RawFile * file, uint32_t spcDirEntAddr, bool validateSample);

protected:
    VGMHeader* spcDirHeader;
    std::vector<uint8_t> targetSRCNs;
    uint32_t spcDirAddr;

    void SetDefaultTargets(uint32_t maxNumSamps);
};

// ********
// SNESSamp
// ********

class SNESSamp
        : public VGMSamp
{
public:
    SNESSamp(VGMSampColl* sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset,
            uint32_t dataLen, uint32_t loopOffset, std::wstring name = L"BRR");
    virtual ~SNESSamp(void);

    static uint32_t GetSampleLength(RawFile * file, uint32_t offset, bool& loop);

    virtual double GetCompressionRatio();	// ratio of space conserved.  should generally be > 1
    // used to calculate both uncompressed sample size and loopOff after conversion
    virtual void ConvertToStdWave(uint8_t* buf);

private:
    void DecompBRRBlk(int16_t *pSmp, BRRBlk *pVBlk, int32_t *prev1, int32_t *prev2);

private:
    uint32_t brrLoopOffset;
};