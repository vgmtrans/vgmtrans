/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <cmath>
#include <algorithm>
#include "common.h"
#include "ScaleConversion.h"

#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923132169163975144 /* pi/2           */
#endif

// Both SF2 and DLS specs define decay and release (not attack) as a constant
// rate of change in dB attenuation over time. In contrast, most game drivers
// change **amplitude** at a constant rate. SF2 and DLS typically express decay/release
// as the time to reach a large attenuation (100 dB for SF2; 96 dB for DLS).
//
// If we naively take the time it would take a linear-amplitude fade to reach −100 dB and
// plug that straight in as an SF2 decay/release time, the result will **sound** too short:
// a linear-in-dB envelope corresponds to an **exponential** drop in amplitude, which
// plunges quickly at the start and lingers at very low levels later. For example, halfway
// through a linear-amplitude fade, the level is 50% (−6.02 dB), but halfway through a
// linear-in-dB fade to −100 dB, the level is already at −50 dB.
//
// Even if we compute the time as “driver max internal volume -> 0,” the shape mismatch
// remains: linear amplitude vs. linear dB will not perceptually align.

// Goal: convert a constant-amplitude decay/release time (t = secondsToFullAtten)
// into an SF2-style time that is linear-in-dB but sounds similar. We blend two
// principled anchors and crossfade them with a smooth knee:
//
//   1) Short-time anchor (match initial dB/sec slope):
//        k_short = targetDb_InitialSlope * (ln 10) / 20
//      (For 100 dB, k_short ≈ 11.51.)
//
//   2) Long-time anchor (least-squares match in dB over the full fade):
//        k_long  = targetDb_LeastSquares * (ln 10) / 45
//      (For 100 dB, k_long ≈ 5.12.)
//
//   Knee blend (p > 1):
//        w(t)   = 1 / (1 + (t / T_knee)^p)
//        T_sf2  = t * [ k_long + (k_short - k_long) * w(t) ]
//
// Interpretation:
//   • T_knee is the **50/50 time** (at t = T_knee, short/long contributions are equal).
//   • p controls the **knee sharpness** (higher p -> crisper transition).
double linearAmpDecayTimeToLinDBDecayTime(double secondsToFullAtten,
                                          double targetDb_LeastSquares,
                                          double targetDb_InitialSlope) {
  if (secondsToFullAtten <= 0.0) return 0.0;

  const double ln10 = 2.302585092994046;
  const double k_short = targetDb_InitialSlope / (20.0 / ln10);
  const double k_long  = targetDb_LeastSquares * ln10 / 45.0;

  // Knee near temporal integration (100–150 ms). p controls sharpness.
  const double T_knee = 0.12; // seconds
  const double p = 2.0;

  const double x = secondsToFullAtten / T_knee;
  const double w = 1.0 / (1.0 + std::pow(x, p)); // w≈1 for very short; →0 for long

  return secondsToFullAtten * (w * k_short + (1.0 - w) * k_long);
}

uint8_t convert7bitPercentAmpToStdMidiVal(uint8_t percentVal) {
  return convertPercentAmpToStdMidiVal(percentVal / 127.0);
}

// Takes a percentage amplitude value - one using a -20*log10(percent) scale for db attenuation
// and converts it to a standard midi value that uses -40*log10(x/127) for db attenuation
uint8_t convertPercentAmpToStdMidiVal(double percent) {
  return std::round(127.0 * sqrt(percent));
}

// Takes a percentage amplitude value - one using a -20*log10(percent) scale for db attenuation
// and converts it to a standard 14 bit midi value that uses -40*log10(x/16383) for db attenuation
uint16_t convertPercentAmpToStd14BitMidiVal(double percent) {
  return std::round(16383.0 * sqrt(percent));
}

// dbAtten is positive for attenuation: 3.2 => -3.2 dB level
uint8_t convertDBAttenuationToStdMidiVal(double dbAtten) {
  const double amp = std::pow(10.0, -dbAtten / 40.0);
  int vi = (int)std::lround(amp * 127.0);
  return (uint8_t)std::clamp(vi, 0, 127);
}

// Convert a linear amplitude multiplier to attenuation in decibels.
//  ex: ampToDb(0.5) returns 6.02db
double ampToDb(double amp, double maxAtten) {
  if (amp == 0)
    return maxAtten;
  double atten = -20 * log10(amp);
  return std::min(atten, maxAtten);
}

// Convert a dB of attenuation value to a linear amplitude multiplier.
//  ex: dbToAmp(6.02) returns 0.5
double dbToAmp(double db) {
  return std::pow(10.0, -db / 20.0);
}

double secondsToTimecents(double secs) {
  return log(secs) / log((double) 2) * 1200;
}

// Convert percent pan to midi pan (with no scale conversion)
uint8_t convertPercentPanValToStdMidiVal(double percent) {
  uint8_t midiPan = std::round(percent * 126.0);
  if (midiPan != 0) {
    midiPan++;
  }
  return midiPan;
}

// Convert linear percent pan to midi pan (with scale conversion)
uint8_t convertLinearPercentPanValToStdMidiVal(double percent, double *ptrVolumeScale) {
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
    midiPan = convertPercentPanValToStdMidiVal(percentArcPan / M_PI_2);

    double percentLeft;
    double percentRight;
    convertStdMidiPanToVolumeBalance(midiPan, percentLeft, percentRight);
    volumeScale = 1.0 / (percentLeft + percentRight);  // <= 1.0
  }

  if (ptrVolumeScale != NULL) {
    *ptrVolumeScale = volumeScale;
  }
  return midiPan;
}

uint8_t convert7bitLinearPercentPanValToStdMidiVal(uint8_t percentVal, double *ptrVolumeScale) {
  // how to calculate volume balance from 7 bit pan depends on each music engines
  // the method below is one of the common method, but it's not always correct
  if (percentVal == 127) {
    percentVal++;
  }
  return convertLinearPercentPanValToStdMidiVal(percentVal / 128.0, ptrVolumeScale);
}

// Convert midi pan to L/R volume balance
void convertStdMidiPanToVolumeBalance(uint8_t midiPan, double &percentLeft, double &percentRight) {
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
uint8_t convertVolumeBalanceToStdMidiPan(double percentLeft, double percentRight, double *ptrVolumeScale) {
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
    midiPan = convertLinearPercentPanValToStdMidiVal(percentPan);
  }

  if (ptrVolumeScale != NULL) {
    double volumeLeftMidi;
    double volumeRightMidi;
    convertStdMidiPanToVolumeBalance(midiPan, volumeLeftMidi, volumeRightMidi);

    // note that it can be more than 1.0
    *ptrVolumeScale = (percentLeft + percentRight) / (volumeLeftMidi + volumeRightMidi);
  }

  return midiPan;
}

// Convert L/R volume balance (0.0..1.0) to midi percent pan, where
// 0 is hard left, 0.5 is center, and 1 is hard right. Also writes volume attenuation as a
// linear amplitude multiplier via the ptrVolumeScale param
double convertVolumeBalanceToStdMidiPercentPan(double percentLeft, double percentRight, double *ptrVolumeScale) {
  uint8_t midiPan;
  double percentPan;
  if (percentRight == 0) {
    midiPan = 0;
    percentPan = 0;
  }
  else if (percentLeft == percentRight) {
    midiPan = 64;
    percentPan = 0.5;
  }
  else if (percentLeft == 0) {
    midiPan = 127;
    percentPan = 1.0;
  }
  else {
    percentPan = percentRight / (percentLeft + percentRight);
    midiPan = convertLinearPercentPanValToStdMidiVal(percentPan);
  }

  if (ptrVolumeScale != NULL) {
    double volumeLeftMidi;
    double volumeRightMidi;
    convertStdMidiPanToVolumeBalance(midiPan, volumeLeftMidi, volumeRightMidi);

    // note that it can be more than 1.0
    *ptrVolumeScale = (percentLeft + percentRight) / (volumeLeftMidi + volumeRightMidi);
  }

  return percentPan;
}

// Convert a pan value where 0 = left 0.5 = center and 1 = right to
// 0.1% units where -50% = left 0 = center 50% = right (shared by DLS and SF2)
long convertPercentPanTo10thPercentUnits(double percentPan) {
  return std::round(percentPan * 1000) - 500;
}

double pitchScaleToCents(double scale) {
  return 1200.0 * log(scale) / log(2.0);
}
