#include "pch.h"

#include <math.h>
#include "common.h"
#include "ScaleConversion.h"

#define M_PI_2      1.57079632679489661923132169163975144   /* pi/2           */

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
	//   20*log10(x^2/127^2) 
	// = 40*log10(x/127)
	// 
	// So the values translate as follows:
	//   CC#7  Amplitude
	//   --------------
	//    127      0db
	//     96   -4.8db
	//     64  -11.9db
	//     32  -23.9db
	//     16  -36.0db
	//      0  -infinity
	//
	// A 16-bit PCM sample uses a different formula for amplitude:
	//   20*log10(abs(x)/32767)
	//
	// So if a game uses this formula with volume values ranging from 0-127 like MIDI,
	// we would get the following chart:
	//   CC#7  Amplitude
	//   --------------
	//    127      0db
	//     96   -2.4db
	//     64   -6.0db
	//     32  -12.0db
	//     16  -18.0db
	//      0  -infinity
	//
	// This doesn't offer much dynamic range, as -10db is perceived as half-volume.
	//
	// To convert a range 0-127 value using the second formula to the MIDI formula, 
	// solve for y using the following equation:
	//   20*log10(x/127) = 40*log10(y/127)
	//   y = sqrt(x*127)


	//In standard MIDI, the attenuation for volume in db is 40*log10(127/val) == 20*log10(127^2/val^2). (dls1 spec page 14)
	// (Also stated in GM guidelines page 9 http://www.midi.org/techspecs/gmguide2.pdf)
	//Here, the scale is different.  We get rid of the exponents
	// so it's just 20*log10(127/val).
	//Therefore, we must convert from one scale to the other.
	//The equation for the first line is obvious.
	//For the second line, I simply solved db = 40*log10(127/val) for val.
	//The result is val = 127*10^(-0.025*db).  So, by plugging in the original val into
	//the original scale equation, we get a db that we can plug into that second equation to get
	//the new val.

	return ConvertPercentAmpToStdMidiVal(percentVal / 127.0);
}

// Takes a percentage amplitude value - that is one using a -20*log10(percent) scale for db attenuation
// and converts it to a standard midi value that uses -40*log10(x/127) for db attenuation
uint8_t ConvertPercentAmpToStdMidiVal(double percent)
{
	return roundi(127.0 * sqrt(percent));
}

double ConvertLogScaleValToAtten(double percent)
{
	if (percent == 0)
		return 100.0;		// assume 0 is -100.0db attenuation
	double atten = 20 * log10(percent) * 2;
	return min(-atten, 100.0);
}

// Convert a percent of volume value to it's attenuation in decibels.
//  ex: ConvertPercentVolToAttenDB(0.5) returns -6.02db = half perceived loudness
double ConvertPercentAmplitudeToAttenDB(double percent)
{
	return 20 * log10(percent);
}

// Convert a percent of volume value to it's attenuation in decibels.
//  ex: ConvertPercentVolToAttenDB_SF2(0.5) returns -(-6.02db) = half perceived loudness
double ConvertPercentAmplitudeToAttenDB_SF2(double percent)
{
	if (percent == 0)
		return 100.0;		// assume 0 is -100.0db attenuation
	double atten = 20 * log10(percent);
	return min(-atten, 100.0);
}

double SecondsToTimecents(double secs)
{
	return log(secs) / log((double)2) * 1200;
}

// Convert percent pan to midi pan (with no scale conversion)
uint8_t ConvertPercentPanValToStdMidiVal(double percent)
{
	uint8_t midiPan = roundi(percent * 126.0);
	if (midiPan != 0) {
		midiPan++;
	}
	return midiPan;
}

// Convert linear percent pan to midi pan (with scale conversion)
uint8_t ConvertLinearPercentPanValToStdMidiVal(double percent, double * ptrVolumeScale)
{
	uint8_t midiPan;
	double volumeScale;

	if (percent == 0) {
		midiPan = 0;
		volumeScale = 1.0;
	}
	else if (percent == 0.5) {
		midiPan = 64;
		volumeScale = 1.0 / sqrt(2);
	}
	else if (percent == 1.0) {
		midiPan = 127;
		volumeScale = 1.0;
	}
	else {
		double percentArcPan = atan2(percent, 1.0 - percent);
		midiPan = ConvertPercentPanValToStdMidiVal(percentArcPan / M_PI_2);

		double percentLeft;
		double percentRight;
		ConvertStdMidiPanToVolumeBalance(midiPan, percentLeft, percentRight);
		volumeScale = 1.0 / (percentLeft + percentRight); // <= 1.0
	}

	if (ptrVolumeScale != NULL) {
		*ptrVolumeScale = volumeScale;
	}
	return midiPan;
}

uint8_t Convert7bitLinearPercentPanValToStdMidiVal(uint8_t percentVal, double * ptrVolumeScale)
{
	// how to calculate volume balance from 7 bit pan depends on each music engines
	// the method below is one of the common method, but it's not always correct
	if (percentVal == 127) {
		percentVal++;
	}
	return ConvertLinearPercentPanValToStdMidiVal(percentVal / 128.0, ptrVolumeScale);
}

// Convert midi pan to L/R volume balance
void ConvertStdMidiPanToVolumeBalance(uint8_t midiPan, double & percentLeft, double & percentRight)
{
	if (midiPan == 0 || midiPan == 1) {
		// left
		percentLeft = 1.0;
		percentRight = 0.0;
		return;
	}
	else if (midiPan == 64) {
		// center
		percentLeft = percentRight = sqrt(2) / 2;
		return;
	}
	else if (midiPan == 127) {
		// right
		percentLeft = 0.0;
		percentRight = 1.0;
		return;
	}

	double percentPan = (midiPan - 1) / 126.0;
	percentLeft = cos(M_PI_2 * percentPan);
	percentRight = sin(M_PI_2 * percentPan);
	return;
}

// Convert L/R volume balance (0.0..1.0) to midi pan
uint8_t ConvertVolumeBalanceToStdMidiPan(double percentLeft, double percentRight, double * ptrVolumeScale)
{
	uint8_t midiPan;
	if (percentRight == 0) {
		midiPan = 0;
	}
	else if (percentLeft == percentRight) {
		midiPan = 64;
	}
	else if (percentLeft == 0) {
		midiPan = 127;
	}
	else {
		double percentPan = percentRight / (percentLeft + percentRight);
		midiPan = ConvertLinearPercentPanValToStdMidiVal(percentPan);
	}

	if (ptrVolumeScale != NULL) {
		double volumeLeftMidi;
		double volumeRightMidi;
		ConvertStdMidiPanToVolumeBalance(midiPan, volumeLeftMidi, volumeRightMidi);

		// note that it can be more than 1.0
		*ptrVolumeScale = (percentLeft + percentRight) / (volumeLeftMidi + volumeRightMidi);
	}

	return midiPan;
}

// Convert a pan value where 0 = left 0.5 = center and 1 = right to
// 0.1% units where -50% = left 0 = center 50% = right (shared by DLS and SF2)
long ConvertPercentPanTo10thPercentUnits(double percentPan)
{
	return roundi(percentPan * 1000) - 500;
}

double PitchScaleToCents(double scale)
{
	return 1200.0 * log(scale) / log(2.0);
}
