/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>

double LinAmpDecayTimeToLinDBDecayTime(double secondsToFullAtten, int linearVolumeRange);

uint8_t Convert7bitPercentVolValToStdMidiVal(uint8_t percentVal);
uint8_t ConvertPercentAmpToStdMidiVal(double percent);
uint8_t ConvertDBAttenuationToStdMidiVal(double dbAtten);
double ConvertLogScaleValToAtten(double percent);
double ConvertPercentAmplitudeToAttenDB(double percent);
double ConvertPercentAmplitudeToAttenDB_SF2(double percent);

double SecondsToTimecents(double secs);
uint8_t ConvertPercentPanValToStdMidiVal(double percent);
uint8_t ConvertLinearPercentPanValToStdMidiVal(double percent, double *ptrVolumeScale = nullptr);
uint8_t Convert7bitLinearPercentPanValToStdMidiVal(uint8_t percentVal, double *ptrVolumeScale = nullptr);
void ConvertStdMidiPanToVolumeBalance(uint8_t midiPan, double &percentLeft, double &percentRight);
uint8_t ConvertVolumeBalanceToStdMidiPan(double percentLeft, double percentRight, double *ptrVolumeScale = nullptr);
long ConvertPercentPanTo10thPercentUnits(double percentPan);

double PitchScaleToCents(double scale);

#define DLS_DECIBEL_UNIT 65536        // DLS1 spec p25