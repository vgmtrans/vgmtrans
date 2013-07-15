#pragma once
#include "Scanner.h"

class QSoundInstrSet;
class QSoundSampColl;
class QSoundSampleInfoTable;
class QSoundArticTable;

enum QSoundVer {
	VER_UNDEFINED,
	VER_100,
	VER_101,
	VER_103,
	VER_104,
	VER_105A,
	VER_105C,
	VER_105,
	VER_106B,
	VER_115C,
	VER_115,
	VER_201B,
	VER_116B,
	VER_116,
	VER_130,
	VER_131,
	VER_140,
	VER_171,
	VER_180,
};

class QSoundScanner :
	public VGMScanner
{
public:
	virtual void Scan(RawFile* file, void* info = 0);
	QSoundVer QSoundScanner::GetVersionEnum(string& versionStr);
};
