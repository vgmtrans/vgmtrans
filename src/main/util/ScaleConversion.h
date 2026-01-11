/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>

double linearAmpDecayTimeToLinDBDecayTime(double secondsToFullAtten,
                                          double targetDb_LeastSquares = 70,
                                          double targetDb_InitialSlope = 140);
uint8_t convert7bitPercentAmpToStdMidiVal(uint8_t percentVal);
uint8_t convertPercentAmpToStdMidiVal(double percent);
uint16_t convertPercentAmpToStd14BitMidiVal(double percent);
uint8_t convertDBAttenuationToStdMidiVal(double dbAtten);
double convertLogScaleValToAtten(double percent);
double ampToDb(double amp, double maxAtten = 100.0);
double dbToAmp(double db);

int16_t secondsToSf2Timecents(double seconds);
int32_t secondsToDlsTimecents(double seconds);
int32_t centsToDlsPitchScale(double cents);
int32_t hertzToDlsPitch(double hz);
uint8_t convertPercentPanValToStdMidiVal(double percent);
uint8_t convertLinearPercentPanValToStdMidiVal(double percent, double *ptrVolumeScale = nullptr);
uint8_t convert7bitLinearPercentPanValToStdMidiVal(uint8_t percentVal, double *ptrVolumeScale = nullptr);
void convertStdMidiPanToVolumeBalance(uint8_t midiPan, double &percentLeft, double &percentRight);
uint8_t convertVolumeBalanceToStdMidiPan(double percentLeft, double percentRight, double *ptrVolumeScale = nullptr);
double convertVolumeBalanceToStdMidiPercentPan(double percentLeft, double percentRight, double *ptrVolumeScale = nullptr);
long convertPercentPanTo10thPercentUnits(double percentPan);

double pitchScaleToCents(double scale);

#define DLS_DECIBEL_UNIT 65536        // DLS1 spec p25
