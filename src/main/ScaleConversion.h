#pragma once

double LinAmpDecayTimeToLinDBDecayTime(double secondsToFullAtten, int linearVolumeRange);

uint8_t Convert7bitPercentVolValToStdMidiVal(uint8_t percentVal);
uint8_t ConvertPercentAmpToStdMidiVal(double percent);
double ConvertLogScaleValToAtten(double percent);
double ConvertPercentAmplitudeToAttenDB(double percent);
double ConvertPercentAmplitudeToAttenDB_SF2(double percent);

double SecondsToTimecents(double secs);
uint8_t ConvertPercentPanValToStdMidiVal(double percent);
uint8_t ConvertLinearPercentPanValToStdMidiVal(double percent, double * ptrVolumeScale = NULL);
uint8_t Convert7bitLinearPercentPanValToStdMidiVal(uint8_t percentVal, double * ptrVolumeScale = NULL);
void ConvertStdMidiPanToVolumeBalance(uint8_t midiPan, double & percentLeft, double & percentRight);
uint8_t ConvertVolumeBalanceToStdMidiPan(double percentLeft, double percentRight, double * ptrVolumeScale = NULL);
long ConvertPercentPanTo10thPercentUnits(double percentPan);

#define DLS_DECIBEL_UNIT 65536		//DLS1 spec p25