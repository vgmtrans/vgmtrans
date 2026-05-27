/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/types.h"

#include <cstdint>

double linearAmpDecayTimeToLinDBDecayTime(double secondsToFullAtten,
                                          double targetDb_LeastSquares = 70,
                                          double targetDb_InitialSlope = 140);
u8 convert7bitPercentAmpToStdMidiVal(u8 percentVal);
u8 convertPercentAmpToStdMidiVal(double percent);
u16 convertPercentAmpToStd14BitMidiVal(double percent);
u8 convertDBAttenuationToStdMidiVal(double dbAtten);
double convertLogScaleValToAtten(double percent);
double ampToDb(double amp, double maxAtten = 100.0);
double dbToAmp(double db);

s16 secondsToSf2Timecents(double seconds);
s32 secondsToDlsTimecents(double seconds);
s32 centsToDlsPitchScale(double cents);
s32 hertzToDlsPitch(double hz);
u8 convertPercentPanValToStdMidiVal(double percent);
u8 convertLinearPercentPanValToStdMidiVal(double percent, double *ptrVolumeScale = nullptr);
u8 convert7bitLinearPercentPanValToStdMidiVal(u8 percentVal, double *ptrVolumeScale = nullptr);
void convertStdMidiPanToVolumeBalance(u8 midiPan, double &percentLeft, double &percentRight);
u8 convertVolumeBalanceToStdMidiPan(double percentLeft, double percentRight, double *ptrVolumeScale = nullptr);
double convertVolumeBalanceToStdMidiPercentPan(double percentLeft, double percentRight, double *ptrVolumeScale = nullptr);
long convertPercentPanTo10thPercentUnits(double percentPan);

double pitchScaleToCents(double scale);

#define DLS_DECIBEL_UNIT 65536        // DLS1 spec p25
