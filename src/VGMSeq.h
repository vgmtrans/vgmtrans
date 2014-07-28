#pragma once
#include "VGMFile.h"
#include "RawFile.h"
#include "MidiFile.h"
#include "SeqVoiceAllocator.h"
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

	VGMSeq(const std::string& format, RawFile* file, uint32_t offset, uint32_t length = 0, std::wstring name = L"VGM Sequence");
	virtual ~VGMSeq(void);

	virtual Icon GetIcon() { return ICON_SEQ; }
	
	virtual bool Load();				//Function to load all the information about the sequence
	virtual bool LoadMain();
	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);	//Function to find all of the track pointers.   Returns number of total tracks.
	virtual void ResetVars(void);
	virtual MidiFile* ConvertToMidi();
	virtual MidiTrack* GetFirstMidiTrack();
	//virtual int ApplyTable(void);	//create and apply table handler object for sequence
	void SetPPQN(uint16_t ppqn);
	uint16_t GetPPQN(void);
	//void SetTimeSignature(uint8_t numer, denom);
	void AddInstrumentRef(uint32_t progNum);

	void UseReverb() { bReverb = true; }
	void HasMonophonicTracks() { bMonophonicTracks = true; }
	void UseLinearAmplitudeScale() { bUseLinearAmplitudeScale = true; }
	void AlwaysWriteInitialVol(uint8_t theVol = 100) { bAlwaysWriteInitialVol = true; initialVol = theVol; }
	void AlwaysWriteInitialExpression(uint8_t level = 127) { bAlwaysWriteInitialExpression = true; initialExpression = level;}
	void AlwaysWriteInitialReverb(uint8_t level = 127) { bAlwaysWriteInitialReverb = true; initialReverb = level;}
	void AlwaysWriteInitialPitchBendRange(uint8_t semitones, uint8_t cents) 
	{ bAlwaysWriteInitialPitchBendRange = true; initialPitchBendRangeSemiTones = semitones;
	  initialPitchBendRangeCents = cents; }

	bool OnSaveAsMidi(void);
	virtual bool SaveAsMidi(const wchar_t* filepath);

	virtual bool HasActiveTracks();
	virtual void InactiveAllTracks();
	virtual int GetForeverLoops();

protected:
	virtual bool LoadTracks(ReadMode readMode, long stopTime = 1000000);
	virtual bool PostLoad();

public:
	uint32_t nNumTracks;
	ReadMode readMode;
	//uint16_t ppqn;		//perhaps shouldn't include this
	MidiFile* midi;
	SeqVoiceAllocator* voices;
	double tempoBPM;
	uint16_t ppqn;

//attributes
	bool bMonophonicTracks;		//Only 1 voice at a time on a track.  We can assume note offs always use last note on key.
								//which is important when drivers allow things like global transposition events mid note
	bool bUseLinearAmplitudeScale;  //This will cause all all velocity, volume, and expression events to be
									//automatically converted from a linear scale to MIDI's logarithmic scale
	bool bWriteInitialTempo;
	bool bAlwaysWriteInitialVol;
	bool bAlwaysWriteInitialExpression;
	bool bAlwaysWriteInitialReverb;
	bool bAlwaysWriteInitialPitchBendRange;
	bool bAllowDiscontinuousTrackData;
	bool bLoadTrackByTrack;
	uint8_t initialVol;
	uint8_t initialExpression;
	uint8_t initialReverb;
	uint8_t initialPitchBendRangeSemiTones, initialPitchBendRangeCents;

	bool bReverb;
	float reverbTime;

	std::vector<SeqTrack*> aTracks;		//array of track pointers
	std::vector<uint32_t> aInstrumentsUsed;

//protected:
	//map<int, InstrAssoc> InstrMap;
	//map<int, DrumAssoc> DrumMap;
	//static int count;
};


extern uint8_t mode;