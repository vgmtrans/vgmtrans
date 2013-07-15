#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "AkaoFormat.h"
#include "Matcher.h"

// ************
// AkaoInstrSet
// ************

class AkaoInstrSet : public VGMInstrSet
{
public:
	AkaoInstrSet(RawFile* file, U32 length, U32 instrOff, U32 dkitOff, U32 id, wstring name = L"Akao Instrument Bank"/*, VGMSampColl* sampColl = NULL*/);
	virtual int GetInstrPointers();
public:
	bool bMelInstrs, bDrumKit;
	ULONG drumkitOff;
};

// *********
// AkaoInstr
// *********


class AkaoInstr : public VGMInstr
{
public:
	AkaoInstr(AkaoInstrSet* instrSet, ULONG offset, ULONG length, ULONG bank, ULONG instrNum, const wchar_t* name = L"Instrument");
	virtual int LoadInstr();

public:
	BYTE instrType;
	bool bDrumKit;
};

// ***********
// AkaoDrumKit
// ***********


class AkaoDrumKit : public AkaoInstr
{
public:
	AkaoDrumKit(AkaoInstrSet* instrSet, ULONG offset, ULONG length, ULONG bank, ULONG instrNum);
	virtual int LoadInstr();
};


// *******
// AkaoRgn
// *******

class AkaoRgn :
	public VGMRgn
{
public:
	AkaoRgn(VGMInstr* instr, ULONG offset, ULONG length = 0, const wchar_t* name = L"Region");
	AkaoRgn(VGMInstr* instr, ULONG offset, ULONG length, BYTE keyLow, BYTE keyHigh, 
		BYTE artIDNum, const wchar_t* name = L"Region");

	virtual int LoadRgn();

public:
	unsigned short ADSR1;				//raw psx ADSR1 value (articulation data)
	unsigned short ADSR2;				//raw psx ADSR2 value (articulation data)
	BYTE artNum;
	BYTE drumRelUnityKey;
};



// ***********
// AkaoSampColl
// ***********

typedef struct _AkaoArt
{
	BYTE unityKey;
	short fineTune;
	ULONG sample_offset;
	ULONG loop_point;
	USHORT ADSR1;
	USHORT ADSR2;
	ULONG artID;
	int sample_num;
} AkaoArt;

//typedef struct _ADSR
//{


class AkaoSampColl :
	public VGMSampColl
{
public:
	AkaoSampColl(RawFile* file, ULONG offset, ULONG length, wstring name = L"Akao Sample Collection");
	virtual ~AkaoSampColl();

	virtual bool GetHeaderInfo();
	virtual bool GetSampleInfo();

public:
	vector<AkaoArt> akArts;
	ULONG starting_art_id;
	USHORT sample_set_id;

private:
	ULONG sample_section_size;
	ULONG nNumArts;
	ULONG arts_offset;
	ULONG sample_section_offset;
};

