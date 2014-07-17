#include "stdafx.h"
#include "ScaleConversion.h"
#include "common.h"


// A lot of games use a simple linear amplitude decay/release for their envelope.
// In other words, the envelope level drops at a constant rate (say from
// 0xFFFF to 0 (cps2) ), and to get the attenuation we multiply by this
// percent value (env_level / 0xFFFF).  This means the attenuation will be
// -20*log10( env_level / 0xFFFF ) decibels.  Wonderful, but SF2 and DLS have
// the a linear decay in decibels - not amplitude - for their decay/release slopes.
// So if you were to graph it, the SF2/DLS attenuation over time graph would be
// a simple line.

// (Note these are obviously crude ASCII drawings and in no way accurate!)
// 100db
// |                  /
// |               /
// |            /
// |         /
// |      /  
//10db /                   -  half volume
// |/
// |--------------------TIME

// But games using linear amplitude have a convex curve
// 100db
// |                  -
// |                  -
// |                 -
// |                -
// |             -  
//10db       x             -  half volume
// |-   -  
// |-------------------TIME

// Now keep in mind that 10db of attenuation is half volume to the human ear.
// What this mean is that SF2/DLS are going to sound like they have much shorter
// decay/release rates if we simply plug in a time value from 0 atten to full atten
// from a linear amplitude game. 

//My approach at the moment is to calculate the time it takes to get to half volume
// and then use that value accordingly with the SF2/DLS decay time.  In other words
// Take the second graph, find where y = 10db, and the draw a line from the origin
// through it to get your DLS/SF2 decay/release line 
// (the actual output value is time where y = 100db for sf2 or 96db for DLS, SynthFile class uses 100db).


// This next function converts seconds to full attenuation in a linear amplitude decay scale
// and approximates the time to full attenuation in a linear DB decay scale.
double LinAmpDecayTimeToLinDBDecayTime(double secondsToFullAtten, int linearVolumeRange)
{
	double expMinDecibel = -100.0;
	double linearMinDecibel = log10(1.0 / linearVolumeRange) * 20.0;
	double linearToExpScale = log(linearMinDecibel - expMinDecibel) / log(2.0);
	return secondsToFullAtten * linearToExpScale;
}


uint8_t Convert7bitPercentVolValToStdMidiVal(uint8_t percentVal)
{
	// MIDI uses the following formula for db attenuation on velocity/volume values.
	// (see http://www.midi.org/techspecs/gmguide2.pdf pg9 or dls1 spec page 14)
	//
	//   CC#7  Amplitude
	//   --------------
	//    127      0db
	//     96   -4.8db
	//     64  -11.9db
	//     32  -23.9db
	//     16  -36.0db
	//      0  -infinity
	//

	return round(sqrt(percentVal / 127.0) * 127.0);
}

// returns the attenuation in decibel units in the scale provided by maxVal;
//long ConvertPercentToLogVolScale(long maxVal, double percent)
//{
//	//double origAttenInDB = 20*log10(percent);
//	//negate (because we were given a percent value:  20*log10(127/64) = -20*log10(64/127)
//	//origAttenInDB = -origAttenInDB;
//	double origAttenInDB = ConvertPercentVolToAttenDB(percent);
//	return maxVal*pow(10.0,-0.025*origAttenInDB);
//}

// Takes a percentage amplitude value - that is one using a -20*log10(percent) scale for db attenuation
// and converts it to a standard midi value that uses -40*log10(x/127) for db attenuation
uint8_t ConvertPercentAmpToStdMidiVal(double percent)
{
	return round(127.0 * sqrt(percent));
}

double ConvertPercentAmpToStdMidiScale(double percent)
{
	return sqrt(percent);
}

double ConvertLogScaleValToAtten(double percent)
{
	if (percent == 0)
		return 100.0;		// assume 0 is -100.0db attenuation
	double atten = 20*log10(percent)*2;
	return min(-atten, 100.0);
}

// Convert a percent of volume value to it's attenuation in decibels.
//  ex: ConvertPercentVolToAttenDB(0.5) returns -(-6.02db) = half perceived loudness
double ConvertPercentAmplitudeToAttenDB(double percent)
{
	if (percent == 0)
		return 100.0;		// assume 0 is -100.0db attenuation
	double atten = 20*log10(percent);
	return min(-atten, 100.0);
}

double SecondsToTimecents(double secs)
{
	return log(secs)/log((double)2) * 1200;
}

// Takes a percentage pan value (linear volume curve)
// and converts it to a standard midi curve (sin/cos curve)
double ConvertPercentPanToStdMidiScale(double percent, double * ptr_volume_scale)
{
	double panPI2 = atan2(percent, 1.0 - percent);
	if (ptr_volume_scale != NULL)
	{
		*ptr_volume_scale = 1.0 / (cos(panPI2) + sin(panPI2)); // <= 1.0
	}
	return panPI2 / M_PI_2;
}

uint8_t Convert7bitPercentPanValToStdMidiVal(uint8_t percentVal, double * ptr_volume_scale)
{
	if (percentVal == 0)
	{
		if (ptr_volume_scale != NULL)
		{
			*ptr_volume_scale = 1.0;
		}
		return 0;
	}
	else
	{
		return (uint8_t) (ConvertPercentPanToStdMidiScale((percentVal - 1) / 126.0, ptr_volume_scale) * 126 + 1);
	}
}

// Convert a pan value where 0 = left 0.5 = center and 1 = right to
// 0.1% units where -50% = left 0 = center 50% = right (shared by DLS and SF2)
long ConvertPercentPanTo10thPercentUnits(double percentPan)
{
	return round(percentPan * 1000) - 500;
}

// Convert a percen of linear volume/panpot value to GM2 compatible sin/cos scale.
//  panpot: 0.0 = left, 0.5 = center, 1.0 = right
void ConvertPercentVolPanToStdMidiScale(double& vol, double& pan)
{
	if (vol != 0)
	{
		double panPI2 = atan2(pan, 1.0 - pan);
		vol = sqrt(vol / (cos(panPI2) + sin(panPI2)));
		pan = panPI2 / M_PI_2;
	}
}
