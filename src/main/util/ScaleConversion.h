/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>

static double linAmpTimeToLinDb_MatchInitialDbSlope(double secondsToFullAtten, double targetDb = 100.0);
static double linAmpTimeToLinDb_LeastSquaresDB(double secondsToFullAtten, double targetDb = 100.0);
static double linAmpTimeToLinDb_LoudnessThreshold(double secondsToFullAtten, double targetDb = 100.0, double thresholdDb = 50.0);
static double smoothstep01(double x);
// double linearAmpDecayTimeToLinDBDecayTime(double secondsToFullAtten,
//                                        double targetDb_LeastSquares = 70.0,
//                                        double targetDb_InitialSlope = 120.0,
//                                        double shortThresholdSec = 0.2,
//                                        double blendWidthSec    = 0.10);
double linearAmpDecayTimeToLinDBDecayTime(double secondsToFullAtten,
                                          double targetDb_LeastSquares = 70,
                                          double targetDb_InitialSlope = 140);
// double linearAmpDecayTimeToLinDBDecayTime(double secondsToFullAtten,
//                                   double targetDb = 200.0,
//                                   double tau = 0.25,     // seconds; try 0.2–0.35
//                                   int samples = 64);      // integration resolution

uint8_t convert7bitPercentAmpValToStdMidiVal(uint8_t percentVal);
uint8_t convertPercentAmpToStdMidiVal(double percent);
uint16_t convertPercentAmpToStd14BitMidiVal(double percent);
uint8_t convertDBAttenuationToStdMidiVal(double dbAtten);
double convertLogScaleValToAtten(double percent);
double convertPercentAmplitudeToAttenDB(double percent, double maxAtten = 100.0);

double secondsToTimecents(double secs);
uint8_t convertPercentPanValToStdMidiVal(double percent);
uint8_t convertLinearPercentPanValToStdMidiVal(double percent, double *ptrVolumeScale = nullptr);
uint8_t convert7bitLinearPercentPanValToStdMidiVal(uint8_t percentVal, double *ptrVolumeScale = nullptr);
void convertStdMidiPanToVolumeBalance(uint8_t midiPan, double &percentLeft, double &percentRight);
uint8_t convertVolumeBalanceToStdMidiPan(double percentLeft, double percentRight, double *ptrVolumeScale = nullptr);
long convertPercentPanTo10thPercentUnits(double percentPan);

double pitchScaleToCents(double scale);

#define DLS_DECIBEL_UNIT 65536        // DLS1 spec p25