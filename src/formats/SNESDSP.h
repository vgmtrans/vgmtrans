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

// Simulate GAIN envelope while (increase: env < env_to, or decrease: env > env_to)
// return elapsed time in sample count, and final env value if requested.
uint32_t GetSNESGAINEnvLength(uint8_t gain, int16_t env, int16_t env_to, int16_t * env_after);

// See Anomie's S-DSP document for technical details
// http://www.romhacking.net/documents/191/
//
// Also, see ScaleConversion.h to know linear-logarithmic conversion approach
template <class T> void SNESConvADSR(T* rgn, uint8_t adsr1, uint8_t adsr2, uint8_t gain)
{
	bool adsr_enabled = (adsr1 & 0x80) != 0;

	int16_t env;
	int16_t env_sim;
	uint32_t decay_samples;
	uint32_t decay_full_samples;
	uint32_t sustain_samples;
	uint32_t sustain_full_samples;
	uint32_t samples;
	double timeInSec;

	if (adsr_enabled)
	{
		// ADSR mode

		uint8_t ar = adsr1 & 0x0f;
		uint8_t dr = (adsr1 & 0x70) >> 4;
		uint8_t sl = (adsr2 & 0xe0) >> 5;
		uint8_t sr = adsr2 & 0x1f;

		uint8_t dr_rate = 0x10 | (dr << 1);

		// attack time
		if (ar < 15)
		{
			timeInSec = SDSP_COUNTER_RATES[ar * 2 + 1] * 64 / 32000.0;
		}
		else
		{
			timeInSec = 2 / 32000.0;
		}
		rgn->attack_time = timeInSec;
		env = 0x7FF;

		if (sl == 7)
		{
			// no decay
			rgn->decay_time = 0;
			rgn->sustain_level = 1.0;
			decay_samples = 0;
			decay_full_samples = 0;
		}
		else
		{
			// decay time
			decay_full_samples = GetSNESGAINEnvLength(0xa0 | dr_rate, env, 0, &env_sim); // exponential decrease
			timeInSec = decay_full_samples / 32000.0; // time for full value to 0
			rgn->decay_time = timeInSec;
			rgn->sustain_level = (sl + 1) / 8.0;

			// get real env value
			decay_samples = GetSNESGAINEnvLength(0xa0 | dr_rate, env, (sl << 8) | 0xff, &env_sim); // exponential decrease
			env = env_sim;
		}

		// sustain time
		if (sr == 0)
		{
			rgn->sustain_time = -1;     // infinite
			sustain_samples = 0;        // must not be used
			sustain_full_samples = 0;   // must not be used
		}
		else
		{
			sustain_full_samples = GetSNESGAINEnvLength(0xa0 | sr, 0x7ff, 0, &env_sim); // exponential decrease
			timeInSec = sustain_full_samples / 32000.0; // time for full value to 0
			rgn->sustain_time = timeInSec;

			sustain_samples = GetSNESGAINEnvLength(0xa0 | sr, env, 0, &env_sim); // exponential decrease
		}

		// Merge decay and sustain into a single envelope, since DLS does not have sustain rate.
		if (rgn->sustain_level == 1.0)
		{
			// no decay, use sustain as decay
			if (rgn->sustain_time != -1)
			{
				rgn->decay_time = rgn->sustain_time;
				rgn->sustain_level = 0;
			}
		}
		else if (rgn->sustain_time != -1)
		{
			const double volume_threshold = 6.0; // dB (larger range value will modify decay more but keep more details of sustain)
			const double time_threshold = 2.0; // seconds

			double decay_part_length = decay_samples / 32000.0;
			double sustain_part_length = sustain_samples / 32000.0;
			if (decay_part_length >= time_threshold)
			{
				// decay is long enough, keep it.
				if (rgn->sustain_time <= rgn->decay_time) // sustain is steeper than decay
				{
					rgn->sustain_level = 0;
				}
			}
			else
			{
				// decay alters to sustain within a few seconds.
				// calculate the real volume at the time-threshold point.
				double real_volume_at_threshold;
				double ideal_sustain_time;
				if (decay_part_length + sustain_part_length >= time_threshold)
				{
					double threshold_crosspoint_rate = 1.0 - (((decay_part_length + sustain_part_length) - time_threshold) / (decay_part_length + sustain_part_length));
					real_volume_at_threshold = pow(1.0 - threshold_crosspoint_rate, 2.0);

					// make a new downward-convex curve that will become
					// the real volume above at the threshold timing.
					ideal_sustain_time = time_threshold / (1.0 - sqrt(real_volume_at_threshold));
				}
				else
				{
					real_volume_at_threshold = 0.0;
					ideal_sustain_time = decay_part_length + sustain_part_length;
				}

				// get the volume at decay-sustain crosspoint.
				double sustain_start_level_of_ideal_sustain_curve = pow(1.0 - (decay_part_length / ideal_sustain_time), 2.0);

				// make the current sustain start level closer to the "ideal" one.
				// TODO: add new threshold value for the volume diff range at time-threshold (will keep better decay)
				double level_diff_db = 20.0 * log10(sustain_start_level_of_ideal_sustain_curve / rgn->sustain_level);
				double new_sustain_start_level;
				if (fabs(level_diff_db) <= volume_threshold)
				{
					new_sustain_start_level = sustain_start_level_of_ideal_sustain_curve;
				}
				else
				{
					// difference too large
					double level_diff_amp;
					if (sustain_start_level_of_ideal_sustain_curve < rgn->sustain_level)
					{
						level_diff_amp = pow(10.0, -volume_threshold / 20);
					}
					else
					{
						level_diff_amp = pow(10.0, volume_threshold / 20);
					}
					new_sustain_start_level = rgn->sustain_level * level_diff_amp;
				}

				// make a new curve by new sustain start level.
				double adjusted_decay_time = decay_part_length / (1.0 - sqrt(new_sustain_start_level));
				rgn->decay_time = adjusted_decay_time;

				// check the volume at the threshold point, again.
				double adjusted_volume_at_threshold = pow(1.0 - (time_threshold / adjusted_decay_time), 2.0);
				level_diff_db = 20.0 * log10(adjusted_volume_at_threshold / real_volume_at_threshold);
				if (level_diff_db < -volume_threshold)
				{
					// adjusted volume is still too smaller!
					// keep the original sustain end level
					// (or, set adjusted volume to rgn->sustain_level?)
				}
				else
				{
					rgn->sustain_level = 0;
				}
			}
		}

		// xmp-midi/timidity plays decay much shorter than expected
		// I do not understand why at all...
		const double snes_exp_magic = 3.0;
		if (rgn->decay_time > 0)
			rgn->decay_time *= snes_exp_magic;

		// release time
		// decrease envelope by 8 for every sample
		samples = (env + 7) / 8;
		timeInSec = samples / 32000.0; // time for full value to 0
		rgn->release_time = LinAmpDecayTimeToLinDBDecayTime(timeInSec, 0x800);
	}
	else
	{
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

	static bool IsValidSampleDir(RawFile * file, uint32_t spcDirEntAddr);

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
