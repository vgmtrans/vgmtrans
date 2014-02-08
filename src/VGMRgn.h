#pragma once
#include "SynthFile.h"
#include "VGMItem.h"
#include "Loop.h"

class VGMInstr;
class VGMRgnItem;
class VGMSampColl;

// ******
// VGMRgn
// ******

class VGMRgn :
	public VGMContainerItem
{
public:
	//VGMRgn(void): Wsmp(NULL), Art(NULL) {}
	//VGMRgn(USHORT keyLow, USHORT keyHigh, USHORT velLow, USHORT velHigh)
	//	: usKeyLow(keyLow), usKeyHigh(keyHigh), usVelLow(velLow), usVelHigh(velHigh), Wsmp(NULL), Art(NULL) {}
	VGMRgn(VGMInstr* instr, ULONG offset, ULONG length = 0, const wchar_t* name = L"Region");
	VGMRgn(VGMInstr* instr, ULONG offset, ULONG length, BYTE keyLow, BYTE keyHigh, BYTE velLow,
		   BYTE velHigh, int sampNum, const wchar_t* name = L"Region");
	//: usKeyLow(keyLow), usKeyHigh(keyHigh), usVelLow(velLow), usVelHigh(velHigh) {}
	~VGMRgn(void);

	virtual bool LoadRgn() { return true; }

	//VGMArt* AddArt(vector<connectionBlock*> connBlocks);
	//VGMSamp* AddSamp(void);
	void SetRanges(BYTE keyLow, BYTE keyHigh, BYTE velLow = 0, BYTE velHigh = 0x7F);
	void SetUnityKey(BYTE unityNote);
	void SetSampNum(BYTE sampNumber);
	void SetLoopInfo(int theLoopStatus, ULONG theLoopStart, ULONG theLoopLength);
	void SetADSR(long attack_time, USHORT atk_transform, long decay_time, long sustain_lev,
				 USHORT rls_transform, long release_time);

	void AddGeneralItem(ULONG offset, ULONG length, const wchar_t* name);
	void AddUnknown(ULONG offset, ULONG length);
	void SetFineTune(S16 relativePitchCents) { fineTune = relativePitchCents; }
	void SetPan(BYTE pan);
	void AddPan(BYTE pan, ULONG offset, ULONG length = 1);
	//void SetAttenuation(long attenuation);
	//void AddAttenuation(long atten, ULONG offset, ULONG length = 1);
	void SetVolume(double volume);
	void AddVolume(double volume, ULONG offset, ULONG length = 1);
	void AddUnityKey(BYTE unityKey, ULONG offset, ULONG length = 1);
	void AddKeyLow(BYTE keyLow, ULONG offset, ULONG length = 1);
	void AddKeyHigh(BYTE keyHigh, ULONG offset, ULONG length = 1);
	void AddVelLow(BYTE velLow, ULONG offset, ULONG length = 1);
	void AddVelHigh(BYTE velHigh, ULONG offset, ULONG length = 1);
	void AddSampNum(int sampNum, ULONG offset, ULONG length = 1);
	//void SetAttack();
	//void SetDelay();
	//void SetSustain();
	//void SetRelease();

	//void SetWaveLinkInfo(USHORT options, USHORT phaseGroup, ULONG theChannel, ULONG theTableIndex);

public:
	VGMInstr* parInstr;
	BYTE keyLow;
	BYTE keyHigh;
	BYTE velLow;
	BYTE velHigh;

	BYTE unityKey;
	short fineTune;

	Loop loop;

	//USHORT fusOptions;
	//USHORT usPhaseGroup;
	//ULONG channel;
	//ULONG tableIndex;

	int sampNum;
	U32 sampOffset;		//optional value. If a sample offset is provided, then find the sample number based on this offset.
						// This is an absolute offset into the SampColl.  It's not necessarily relative to the beginning of the
						// actual sample data, unless the sample data begins at offset 0.
	//int sampCollNum;	//optional value. for formats that use multiple sampColls and reference samples base 0 for each sampColl (NDS, for instance)
	VGMSampColl* sampCollPtr;

	//long attenuation;
	double volume;	// as percentage of full volume.  This will be converted to to an attenuation when we convert to a SynthFile
	double pan;		//percentage.  0 = full left. 0.5 = center.  1 = full right
	double attack_time;			//in seconds
	USHORT attack_transform;
	double decay_time;			//in seconds
	double sustain_level;		//as a percentage
	double sustain_time;		//in seconds (we don't support positive rate here, as is possible on psx)
	USHORT release_transform;
	double release_time;		//in seconds

	vector<VGMRgnItem*> items;
};


// **********
// VGMRgnItem
// **********


class VGMRgnItem :
	public VGMItem
{
public:
	enum RgnItemType { RIT_GENERIC, RIT_UNKNOWN, RIT_UNITYKEY, RIT_KEYLOW, RIT_KEYHIGH, RIT_VELLOW, RIT_VELHIGH, RIT_PAN, RIT_VOL, RIT_SAMPNUM};		//HIT = Header Item Type

	VGMRgnItem(VGMRgn* rgn, RgnItemType theType, ULONG offset, ULONG length, const wchar_t* name);
	virtual Icon GetIcon();

public:
	RgnItemType type;
};



/*
class VGMArt :
	public VGMItem
{
public:
	VGMArt();
	~VGMArt();
};*/
