#pragma once
#include "VGMFile.h"
#include "RawFile.h"
#include "MidiFile.h"
#include "Menu.h"

class SeqTrack;
class SeqEvent;

typedef enum ReadMode
{
	READMODE_ADD_TO_UI,
	READMODE_CONVERT_TO_MIDI,
	READMODE_FIND_DELTA_LENGTH
};


class VGMSeq :
	public VGMFile
{
public:
	BEGIN_MENU_SUB(VGMSeq, VGMFile)
		MENU_ITEM(VGMSeq, OnSaveAsMidi, L"Save as MIDI")
	END_MENU()

	VGMSeq(const string& format, RawFile* file, ULONG offset, ULONG length = 0, wstring name = L"VGM Sequence");
	virtual ~VGMSeq(void);

	virtual Icon GetIcon() { return ICON_SEQ; }
	
	virtual int Load();				//Function to load all the information about the sequence
	virtual int LoadMain();
	virtual int PostLoad();
	virtual int GetHeaderInfo(void);
	virtual int GetTrackPointers(void);	//Function to find all of the track pointers.   Returns number of total tracks.
	virtual void ResetVars(void);
	virtual MidiFile* ConvertToMidi();
//	virtual int LoadTracks(void);
	//virtual int ApplyTable(void);	//create and apply table handler object for sequence
	void SetPPQN(WORD ppqn);
	WORD GetPPQN(void);
	//void SetTimeSignature(BYTE numer, denom);
	void AddInstrumentRef(ULONG progNum);

	void UseReverb() { bReverb = true; }
	void HasMonophonicTracks() { bMonophonicTracks = true; }
	void UseLinearAmplitudeScale() { bUseLinearAmplitudeScale = true; }
	void AlwaysWriteInitialVol(BYTE theVol = 100) { bAlwaysWriteInitialVol = true; initialVol = theVol; }
	void AlwaysWriteInitialExpression(BYTE level = 127) { bAlwaysWriteInitialExpression = true; initialExpression = level;}
	void AlwaysWriteInitialPitchBendRange(U8 semitones, U8 cents) 
	{ bAlwaysWriteInitialPitchBendRange = true; initialPitchBendRangeSemiTones = semitones;
	  initialPitchBendRangeCents = cents; }

	bool OnSaveAsMidi(void);
	virtual bool SaveAsMidi(const wchar_t* filepath);

public:
	ULONG nNumTracks;
	ReadMode readMode;
	//USHORT ppqn;		//perhaps shouldn't include this
	MidiFile* midi;
	double tempoBPM;
	WORD ppqn;

//attributes
	bool bMonophonicTracks;		//Only 1 voice at a time on a track.  We can assume note offs always use last note on key.
								//which is important when drivers allow things like global transposition events mid note
	bool bUseLinearAmplitudeScale;  //This will cause all all velocity, volume, and expression events to be
									//automatically converted from a linear scale to MIDI's logarithmic scale
	bool bWriteInitialTempo;
	bool bAlwaysWriteInitialVol;
	bool bAlwaysWriteInitialExpression;
	bool bAlwaysWriteInitialPitchBendRange;
	bool bAllowDiscontinuousTrackData;
	U8 initialVol;
	U8 initialExpression;
	U8 initialPitchBendRangeSemiTones, initialPitchBendRangeCents;

	bool bReverb;
	float reverbTime;

	vector<SeqTrack*> aTracks;		//array of track pointers
	vector<ULONG> aInstrumentsUsed;

//protected:
	//map<int, InstrAssoc> InstrMap;
	//map<int, DrumAssoc> DrumMap;
	//static int count;
};


extern BYTE mode;