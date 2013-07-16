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
	SeqTrack(VGMSeq* parentSeqFile, ULONG offset = 0, ULONG length = 0);
	virtual ~SeqTrack(void);								//note: virtual destructor
	virtual void ResetVars();
	//virtual int OnSelected(void);
	//virtual int OnPlay(void);
	virtual Icon GetIcon() { return ICON_TRACK; };

	//virtual void AddToUI(VGMItem* parent, VGMFile* theVGMFile = NULL);
	virtual int LoadTrack(int trackNum, ULONG stopOffset = 0xFFFFFFFF, long stopDelta = -1);
	virtual int LoadTrackInit(int trackNum);
	virtual int LoadTrackMainLoop(U32 stopOffset, long stopDelta);
	virtual void SetChannelAndGroupFromTrkNum(int theTrackNum);
	virtual int ReadEvent(void);

	ULONG GetDelta(void);
	void SetDelta(ULONG NewDelta);
	void AddDelta(ULONG AddDelta);
	void SubtractDelta(ULONG SubtractDelta);
	void ResetDelta(void);

	ULONG ReadVarLen(ULONG& offset);

public:
	static ULONG offsetInQuestion;
	
	struct IsEventAtOffset;
	virtual bool IsOffsetUsed(ULONG offset);

protected:
	virtual void AddEvent(SeqEvent* pSeqEvent);
	void AddControllerSlide(U32 offset, U32 length, U32 dur, BYTE& prevVal, BYTE targVal, 
		void (MidiTrack::*insertFunc)(BYTE, BYTE, U32));
public:
	void AddGenericEvent(ULONG offset, ULONG length, const wchar_t* sEventName, BYTE color);
	void AddSetOctave(ULONG offset, ULONG length, BYTE newOctave, const wchar_t* sEventName = L"Set Octave");
	void AddIncrementOctave(ULONG offset, ULONG length, const wchar_t* sEventName = L"Increment Octave");	// 1,Sep.2009 revise
	void AddDecrementOctave(ULONG offset, ULONG length, const wchar_t* sEventName = L"Decrement Octave");	// 1,Sep.2009 revise
	void AddRest(ULONG offset, ULONG length, UINT restTime, const wchar_t* sEventName = L"Rest");
	void AddHold(ULONG offset, ULONG length, const wchar_t* sEventName = L"Hold");
	void AddUnknown(ULONG offset, ULONG length, const wchar_t* sEventName = L"Unknown Event");
	
	void AddNoteOn(ULONG offset, ULONG length, char key, char vel, const wchar_t* sEventName = L"Note On");
	void AddNoteOnNoItem(char key, char vel);
	void AddPercNoteOn(ULONG offset, ULONG length, char key, char vel, const wchar_t* sEventName = L"Percussion Note On");
	void AddPercNoteOnNoItem(char key, char vel);
	void InsertNoteOn(ULONG offset, ULONG length, char key, char vel, ULONG absTime, const wchar_t* sEventName = L"Note On");

	void AddNoteOff(ULONG offset, ULONG length, char key, const wchar_t* sEventName = L"Note Off");
	void AddNoteOffNoItem(char key);
	void AddPercNoteOff(ULONG offset, ULONG length, char key, const wchar_t* sEventName = L"Percussion Note Off");
	void AddPercNoteOffNoItem(char key);
	void InsertNoteOff(ULONG offset, ULONG length, char key, ULONG absTime, const wchar_t* sEventName = L"Note Off");

	void AddNoteByDur(ULONG offset, ULONG length, char key, char vel, UINT dur, const wchar_t* sEventName = L"Note with Duration");
	void AddNoteByDurNoItem(char key, char vel, UINT dur);
	void AddPercNoteByDur(ULONG offset, ULONG length, char key, char vel, UINT dur, const wchar_t* sEventName = L"Percussion Note with Duration");
	void AddPercNoteByDurNoItem(char key, char vel, UINT dur);
	//void AddNoteByDur(ULONG offset, ULONG length, char key, char vel, UINT dur, BYTE chan, const wchar_t* sEventName = "Note On With Duration");
	void InsertNoteByDur(ULONG offset, ULONG length, char key, char vel, UINT dur, ULONG absTime, const wchar_t* sEventName = L"Note On With Duration");

	void MakePrevDurNoteEnd();
	void AddVol(ULONG offset, ULONG length, BYTE vol, const wchar_t* sEventName = L"Volume");
	void AddVolNoItem(BYTE vol);
	void AddVolSlide(ULONG offset, ULONG length, ULONG dur, BYTE targVol, const wchar_t* sEventName = L"Volume Slide");
	void InsertVol(ULONG offset, ULONG length, BYTE vol, ULONG absTime, const wchar_t* sEventName = L"Volume");
	void AddExpression(ULONG offset, ULONG length, BYTE level, const wchar_t* sEventName = L"Expression");
	void AddExpressionNoItem(BYTE level);
	void AddExpressionSlide(ULONG offset, ULONG length, ULONG dur, BYTE targExpr, const wchar_t* sEventName = L"Expression Slide");
	void InsertExpression(ULONG offset, ULONG length, BYTE level, ULONG absTime, const wchar_t* sEventName = L"Expression");
	void AddMasterVol(ULONG offset, ULONG length, BYTE vol, const wchar_t* sEventName = L"Master Volume");
	void AddMasterVolNoItem(BYTE newVol);
	//void AddMastVolSlide(ULONG offset, ULONG length, ULONG dur, BYTE targVol, const wchar_t* sEventName = "Master Volume Slide");
	//void InsertMastVol(ULONG offset, ULONG length, BYTE vol, ULONG absTime, const wchar_t* sEventName = "Master Volume");

	void AddPan(ULONG offset, ULONG length, BYTE pan, const wchar_t* sEventName = L"Pan");
	void AddPanNoItem(BYTE pan);
	void AddPanSlide(ULONG offset, ULONG length, ULONG dur, BYTE targPan, const wchar_t* sEventName = L"Pan Slide");
	void InsertPan(ULONG offset, ULONG length, BYTE pan, ULONG absTime, const wchar_t* sEventName = L"Pan");
	void AddReverb(ULONG offset, ULONG length, BYTE reverb, const wchar_t* sEventName = L"Reverb");
	void AddReverbNoItem(BYTE reverb);
	void InsertReverb(ULONG offset, ULONG length, BYTE reverb, ULONG absTime, const wchar_t* sEventName = L"Reverb");
	void AddPitchBend(ULONG offset, ULONG length, SHORT bend, const wchar_t* sEventName = L"Pitch Bend");
	void AddPitchBendRange(ULONG offset, ULONG length, BYTE semitones, BYTE cents = 0, const wchar_t* sEventName = L"Pitch Bend Range");
	void AddPitchBendRangeNoItem(BYTE range, BYTE cents = 0);
	void AddTranspose(ULONG offset, ULONG length, char transpose, const wchar_t* sEventName = L"Transpose");
	void AddPitchBendMidiFormat(ULONG offset, ULONG length, BYTE lo, BYTE hi, const wchar_t* sEventName = L"Pitch Bend");
	void AddModulation(ULONG offset, ULONG length, BYTE depth, const wchar_t* sEventName = L"Modulation Depth");
	void InsertModulation(ULONG offset, ULONG length, BYTE depth, ULONG absTime, const wchar_t* sEventName = L"Modulation Depth");
	void AddBreath(ULONG offset, ULONG length, BYTE depth, const wchar_t* sEventName = L"Breath Depth");
	void InsertBreath(ULONG offset, ULONG length, BYTE depth, ULONG absTime, const wchar_t* sEventName = L"Breath Depth");

	void AddSustainEvent(ULONG offset, ULONG length, bool bOn, const wchar_t* sEventName = L"Sustain");
	void InsertSustainEvent(ULONG offset, ULONG length, bool bOn, ULONG absTime, const wchar_t* sEventName = L"Sustain");
	void AddPortamento(ULONG offset, ULONG length, bool bOn, const wchar_t* sEventName = L"Portamento");
	void AddPortamentoNoItem(bool bOn);
	void InsertPortamento(ULONG offset, ULONG length, bool bOn, ULONG absTime, const wchar_t* sEventName = L"Portamento");
	void AddPortamentoTime(ULONG offset, ULONG length, BYTE time, const wchar_t* sEventName = L"Portamento Time");
	void InsertPortamentoTime(ULONG offset, ULONG length, BYTE time, ULONG absTime, const wchar_t* sEventName = L"Portamento Time");
	//void AddSustainEventDur();
	//void AddPitchBendSlide();
	//void InsertPitchBendSlide();
	void AddProgramChange(ULONG offset, ULONG length, BYTE progNum,const wchar_t* sEventName = L"Program Change");
	void AddProgramChange(ULONG offset, ULONG length, BYTE progNum, BYTE chan, const wchar_t* sEventName = L"Program Change");
	void AddBankSelectNoItem(BYTE bank);
	void AddTempo(ULONG offset, ULONG length, ULONG microsPerQuarter, const wchar_t* sEventName = L"Tempo");
	void AddTempoSlide(ULONG offset, ULONG length, ULONG dur, ULONG targMicrosPerQuarter, const wchar_t* sEventName = L"Tempo Slide");
	void AddTempoBPM(ULONG offset, ULONG length, double bpm, const wchar_t* sEventName = L"Tempo");
	void AddTempoBPMNoItem(double bpm);
	void AddTempoBPMSlide(ULONG offset, ULONG length, ULONG dur, double targBPM, const wchar_t* sEventName = L"Tempo Slide");
	void AddTimeSig(ULONG offset, ULONG length, BYTE numer, BYTE denom, BYTE ticksPerQuarter,const wchar_t* sEventName = L"Time Signature");
	void InsertTimeSig(ULONG offset, ULONG length, BYTE numer, BYTE denom, BYTE ticksPerQuarter,ULONG absTime,const wchar_t* sEventName = L"Time Signature");
	bool AddEndOfTrack(ULONG offset, ULONG length, const wchar_t* sEventName = L"Track End");
	bool AddEndOfTrackNoItem();

	void AddGlobalTranspose(ULONG offset, ULONG length, char semitones, const wchar_t* sEventName = L"Global Transpose");
	void AddMarker(ULONG offset, ULONG length, string& markername, BYTE databyte1, BYTE databyte2, const wchar_t* sEventName, char priority = 0, BYTE color = CLR_MISC);

	bool AddLoopForever(ULONG offset, ULONG length, const wchar_t* sEventName = L"Loop Forever");

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

	ULONG deltaTime;
	char vel;
	char key;
	ULONG dur;
	BYTE prevKey;
	BYTE prevVel;
	//BYTE prevDur;
	BYTE octave;
	BYTE vol;
	BYTE expression;
	//BYTE mastVol;
	BYTE prevPan;
	BYTE prevReverb;
	char transpose;
	bool bNoteOn;			//indicates whether a note is playing
	ULONG curOffset;
	BOOL bInLoop;
	char cDrumNote;			//-1 signals do not use drumNote, otherwise,

	wchar_t numberedName[10];

	//Table Related Variables
	char cKeyCorrection;	//steps to offset the key by


	//CTypedPtrArray<CPtrArray, SeqEvent*> aEvents;
	vector<SeqEvent*> aEvents;

protected:
	//SETTINGS
	bool bDetermineTrackLengthEventByEvent;

};
