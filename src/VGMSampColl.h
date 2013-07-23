#pragma once
#include "VGMFile.h"

class VGMInstrSet;
class VGMSamp;

// ***********
// VGMSampColl
// ***********

class VGMSampColl :
	public VGMFile
{
public:
	BEGIN_MENU_SUB(VGMSampColl, VGMFile)
		MENU_ITEM(VGMSampColl, OnSaveAllAsWav, L"Save all as WAV")
	END_MENU()

	VGMSampColl(const string& format, RawFile* rawfile, ULONG offset, ULONG length = 0,
	            wstring theName = L"VGMSampColl");
	VGMSampColl(const string& format, RawFile* rawfile, VGMInstrSet* instrset,
		        ULONG offset, ULONG length = 0, wstring theName = L"VGMSampColl");
	virtual ~VGMSampColl(void);
	void UseInstrSet(VGMInstrSet* instrset) { parInstrSet = instrset; }

	virtual bool Load();
	virtual bool GetHeaderInfo();		//retrieve any header data
	virtual bool GetSampleInfo();		//retrieve sample info, including pointers to data, # channels, rate, etc.

	VGMSamp* AddSamp(ULONG offset, ULONG length, ULONG dataOffset, ULONG dataLength,
					 BYTE nChannels = 1, USHORT bps = 16, ULONG theRate = 0,
					 wstring name = L"Sample");
	bool OnSaveAllAsWav();

protected:
	void LoadOnInstrMatch()		{ bLoadOnInstrSetMatch = true; }

public:
	bool bLoadOnInstrSetMatch, bLoaded;

	U32 sampDataOffset;			//offset of the beginning of the sample data.  Used for rgn->sampOffset matching
	VGMInstrSet* parInstrSet;
	vector<VGMSamp*> samples;
};
