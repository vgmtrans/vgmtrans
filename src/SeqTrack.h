#pragma once
#include "VGMItem.h"

class VGMSeq;
class SeqEvent;
class MidiTrack;

extern enum ReadMode;

class SeqTrack :
	public VGMContainerItem
{
public:
	SeqTrack(VGMSeq* parentSeqFile, uint32_t offset = 0, uint32_t length = 0);
	virtual ~SeqTrack(void);								//note: virtual destructor
	virtual void ResetVars();
	//virtual int OnSelected(void);
	//virtual int OnPlay(void);
	virtual Icon GetIcon() { return ICON_TRACK; };

	//virtual void AddToUI(VGMItem* parent, VGMFile* theVGMFile = NULL);
	virtual bool LoadTrackInit(int trackNum);
	virtual bool LoadTrackMainLoop(uint32_t stopOffset);
	virtual void SetChannelAndGroupFromTrkNum(int theTrackNum);
	virtual bool ReadEvent(void);
	virtual void OnTickBegin(){};
	virtual void OnTickEnd(){};

	uint32_t GetTime(void);
	void SetTime(uint32_t NewDelta); // in general, derived class should not use this method.
	void AddTime(uint32_t AddDelta);
	void ResetTime(void);

	uint32_t ReadVarLen(uint32_t& offset);

public:
	static uint32_t offsetInQuestion;
	
	struct IsEventAtOffset;
	virtual bool IsOffsetUsed(uint32_t offset);

	uint32_t dwStartOffset;

protected:
	virtual void AddEvent(SeqEvent* pSeqEvent);
	void AddControllerSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t& prevVal, uint8_t targVal, 
		void (MidiTrack::*insertFunc)(uint8_t, uint8_t, uint32_t));
public:
	void AddGenericEvent(uint32_t offset, uint32_t length, const wchar_t* sEventName, const wchar_t* sEventDesc, uint8_t color, Icon icon = ICON_BINARY);
	void AddSetOctave(uint32_t offset, uint32_t length, uint8_t newOctave, const wchar_t* sEventName = L"Set Octave");
	void AddIncrementOctave(uint32_t offset, uint32_t length, const wchar_t* sEventName = L"Increment Octave");	// 1,Sep.2009 revise
	void AddDecrementOctave(uint32_t offset, uint32_t length, const wchar_t* sEventName = L"Decrement Octave");	// 1,Sep.2009 revise
	void AddRest(uint32_t offset, uint32_t length, uint32_t restTime, const wchar_t* sEventName = L"Rest");
	void AddHold(uint32_t offset, uint32_t length, const wchar_t* sEventName = L"Hold");
	void AddUnknown(uint32_t offset, uint32_t length, const wchar_t* sEventName = L"Unknown Event", const wchar_t* sEventDesc = NULL);
	
	void AddNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, const wchar_t* sEventName = L"Note On");
	void AddNoteOnNoItem(int8_t key, int8_t vel);
	void AddPercNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, const wchar_t* sEventName = L"Percussion Note On");
	void AddPercNoteOnNoItem(int8_t key, int8_t vel);
	void InsertNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t absTime, const wchar_t* sEventName = L"Note On");

	void AddNoteOff(uint32_t offset, uint32_t length, int8_t key, const wchar_t* sEventName = L"Note Off");
	void AddNoteOffNoItem(int8_t key);
	void AddPercNoteOff(uint32_t offset, uint32_t length, int8_t key, const wchar_t* sEventName = L"Percussion Note Off");
	void AddPercNoteOffNoItem(int8_t key);
	void InsertNoteOff(uint32_t offset, uint32_t length, int8_t key, uint32_t absTime, const wchar_t* sEventName = L"Note Off");

	void AddNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, const wchar_t* sEventName = L"Note with Duration");
	void AddNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur);
	void AddPercNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, const wchar_t* sEventName = L"Percussion Note with Duration");
	void AddPercNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur);
	//void AddNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, uint8_t chan, const wchar_t* sEventName = "Note On With Duration");
	void InsertNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, uint32_t absTime, const wchar_t* sEventName = L"Note On With Duration");

	void MakePrevDurNoteEnd();
	void AddVol(uint32_t offset, uint32_t length, uint8_t vol, const wchar_t* sEventName = L"Volume");
	void AddVolNoItem(uint8_t vol);
	void AddVolSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targVol, const wchar_t* sEventName = L"Volume Slide");
	void InsertVol(uint32_t offset, uint32_t length, uint8_t vol, uint32_t absTime, const wchar_t* sEventName = L"Volume");
	void AddExpression(uint32_t offset, uint32_t length, uint8_t level, const wchar_t* sEventName = L"Expression");
	void AddExpressionNoItem(uint8_t level);
	void AddExpressionSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targExpr, const wchar_t* sEventName = L"Expression Slide");
	void InsertExpression(uint32_t offset, uint32_t length, uint8_t level, uint32_t absTime, const wchar_t* sEventName = L"Expression");
	void AddMasterVol(uint32_t offset, uint32_t length, uint8_t vol, const wchar_t* sEventName = L"Master Volume");
	void AddMasterVolNoItem(uint8_t newVol);
	//void AddMastVolSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targVol, const wchar_t* sEventName = "Master Volume Slide");
	//void InsertMastVol(uint32_t offset, uint32_t length, uint8_t vol, uint32_t absTime, const wchar_t* sEventName = "Master Volume");

	void AddPan(uint32_t offset, uint32_t length, uint8_t pan, const wchar_t* sEventName = L"Pan");
	void AddPanNoItem(uint8_t pan);
	void AddPanSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targPan, const wchar_t* sEventName = L"Pan Slide");
	void InsertPan(uint32_t offset, uint32_t length, uint8_t pan, uint32_t absTime, const wchar_t* sEventName = L"Pan");
	void AddReverb(uint32_t offset, uint32_t length, uint8_t reverb, const wchar_t* sEventName = L"Reverb");
	void AddReverbNoItem(uint8_t reverb);
	void InsertReverb(uint32_t offset, uint32_t length, uint8_t reverb, uint32_t absTime, const wchar_t* sEventName = L"Reverb");
	void AddPitchBend(uint32_t offset, uint32_t length, int16_t bend, const wchar_t* sEventName = L"Pitch Bend");
	void AddPitchBendRange(uint32_t offset, uint32_t length, uint8_t semitones, uint8_t cents = 0, const wchar_t* sEventName = L"Pitch Bend Range");
	void AddPitchBendRangeNoItem(uint8_t range, uint8_t cents = 0);
	void AddFineTuning(uint32_t offset, uint32_t length, double cents, const wchar_t* sEventName = L"Fine Tuning");
	void AddFineTuningNoItem(double cents);
	void AddModulationDepthRange(uint32_t offset, uint32_t length, double semitones, const wchar_t* sEventName = L"Modulation Depth Range");
	void AddModulationDepthRangeNoItem(double semitones);
	void AddTranspose(uint32_t offset, uint32_t length, int8_t transpose, const wchar_t* sEventName = L"Transpose");
	void AddPitchBendMidiFormat(uint32_t offset, uint32_t length, uint8_t lo, uint8_t hi, const wchar_t* sEventName = L"Pitch Bend");
	void AddModulation(uint32_t offset, uint32_t length, uint8_t depth, const wchar_t* sEventName = L"Modulation Depth");
	void InsertModulation(uint32_t offset, uint32_t length, uint8_t depth, uint32_t absTime, const wchar_t* sEventName = L"Modulation Depth");
	void AddBreath(uint32_t offset, uint32_t length, uint8_t depth, const wchar_t* sEventName = L"Breath Depth");
	void InsertBreath(uint32_t offset, uint32_t length, uint8_t depth, uint32_t absTime, const wchar_t* sEventName = L"Breath Depth");

	void AddSustainEvent(uint32_t offset, uint32_t length, bool bOn, const wchar_t* sEventName = L"Sustain");
	void InsertSustainEvent(uint32_t offset, uint32_t length, bool bOn, uint32_t absTime, const wchar_t* sEventName = L"Sustain");
	void AddPortamento(uint32_t offset, uint32_t length, bool bOn, const wchar_t* sEventName = L"Portamento");
	void AddPortamentoNoItem(bool bOn);
	void InsertPortamento(uint32_t offset, uint32_t length, bool bOn, uint32_t absTime, const wchar_t* sEventName = L"Portamento");
	void AddPortamentoTime(uint32_t offset, uint32_t length, uint8_t time, const wchar_t* sEventName = L"Portamento Time");
	void InsertPortamentoTime(uint32_t offset, uint32_t length, uint8_t time, uint32_t absTime, const wchar_t* sEventName = L"Portamento Time");
	//void AddSustainEventDur();
	//void AddPitchBendSlide();
	//void InsertPitchBendSlide();
	void AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, const wchar_t* sEventName = L"Program Change");
	void AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, uint8_t chan, const wchar_t* sEventName = L"Program Change");
	void AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, bool requireBank, const wchar_t* sEventName = L"Program Change");
	void AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, bool requireBank, uint8_t chan, const wchar_t* sEventName = L"Program Change");
	void AddBankSelectNoItem(uint8_t bank);
	void AddTempo(uint32_t offset, uint32_t length, uint32_t microsPerQuarter, const wchar_t* sEventName = L"Tempo");
	void AddTempoNoItem(uint32_t microsPerQuarter);
	void AddTempoSlide(uint32_t offset, uint32_t length, uint32_t dur, uint32_t targMicrosPerQuarter, const wchar_t* sEventName = L"Tempo Slide");
	void AddTempoBPM(uint32_t offset, uint32_t length, double bpm, const wchar_t* sEventName = L"Tempo");
	void AddTempoBPMNoItem(double bpm);
	void AddTempoBPMSlide(uint32_t offset, uint32_t length, uint32_t dur, double targBPM, const wchar_t* sEventName = L"Tempo Slide");
	void AddTimeSig(uint32_t offset, uint32_t length, uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter,const wchar_t* sEventName = L"Time Signature");
	void InsertTimeSig(uint32_t offset, uint32_t length, uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter,uint32_t absTime,const wchar_t* sEventName = L"Time Signature");
	bool AddEndOfTrack(uint32_t offset, uint32_t length, const wchar_t* sEventName = L"Track End");
	bool AddEndOfTrackNoItem();

	void AddGlobalTranspose(uint32_t offset, uint32_t length, int8_t semitones, const wchar_t* sEventName = L"Global Transpose");
	void AddMarker(uint32_t offset, uint32_t length, std::string& markername, uint8_t databyte1, uint8_t databyte2, const wchar_t* sEventName, int8_t priority = 0, uint8_t color = CLR_MISC);

	bool AddLoopForever(uint32_t offset, uint32_t length, const wchar_t* sEventName = L"Loop Forever");

	//void SetChannel(int theChannel);

	//int SaveAsMidi(LPSTR filename, bool bOpenSaveFileDlg);


public:
	ReadMode readMode;		//state variable that determines behavior for all methods.  Are we adding UI items or converting to MIDI?

	VGMSeq* parentSeq;
	MidiTrack* pMidiTrack;
	bool bMonophonic;
	int channel;
	int channelGroup;

	long deltaLength;
	int foreverLoops;
	bool active;			//indicates whether a VGMSeq is loading this track

	long time;				//absolute current time (ticks)
	long deltaTime;			//delta time, an interval to the next event (ticks)
	int8_t vel;
	int8_t key;
	uint32_t dur;
	uint8_t prevKey;
	uint8_t prevVel;
	//uint8_t prevDur;
	uint8_t octave;
	uint8_t vol;
	uint8_t expression;
	//uint8_t mastVol;
	uint8_t prevPan;
	uint8_t prevReverb;
	int8_t transpose;
	bool bNoteOn;			//indicates whether a note is playing
	uint32_t curOffset;
	bool bInLoop;
	int8_t cDrumNote;			//-1 signals do not use drumNote, otherwise,

	wchar_t numberedName[10];

	//Table Related Variables
	int8_t cKeyCorrection;	//steps to offset the key by


	//CTypedPtrArray<CPtrArray, SeqEvent*> aEvents;
	std::vector<SeqEvent*> aEvents;

protected:
	//SETTINGS
	bool bDetermineTrackLengthEventByEvent;
	bool bWriteGenericEventAsTextEvent;

};
