#pragma once

double LinAmpDecayTimeToLinDBDecayTime(double secondsToFullAtten);

BYTE Convert7bitPercentVolValToStdMidiVal(BYTE percentVal);
BYTE ConvertPercentAmpToStdMidiVal(double percent);
long ConvertPercentToLogVolScale(long maxVal, double percent);
double ConvertLogScaleValToAtten(double percent);
double ConvertPercentAmplitudeToAttenDB(double percent);

double SecondsToTimecents(double secs);
long ConvertPercentPanTo10thPercentUnits(double percentPan);

#define DLS_DECIBEL_UNIT 65536		//DLS1 spec p25