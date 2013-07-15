#pragma once
#include <iterator>
#include <string>
#include <vector>
#include <list>
using namespace std;

class VGMSeq;

class MidiFile;
class MidiTrack;
class MidiEvent;
class DurNoteEvent;
class NoteEvent;

#define PRIORITY_LOWEST 127
#define PRIORITY_LOWER 96
#define PRIORITY_LOW 64
#define PRIORITY_MIDDLE 0
#define PRIORITY_HIGH -64
#define PRIORITY_HIGHER -96
#define PRIORITY_HIGHEST -128

typedef enum 
{ 
	MIDIEVENT_UNDEFINED, 
	MIDIEVENT_MASTERVOL,
	MIDIEVENT_GLOBALTRANSPOSE,
	MIDIEVENT_BANKSELECT,
	MIDIEVENT_BANKSELECTFINE,
	MIDIEVENT_MARKER,
	MIDIEVENT_NOTEON,
	MIDIEVENT_NOTEOFF,
	MIDIEVENT_DURNOTE, 
	MIDIEVENT_REST, 
	MIDIEVENT_EXPRESSION,
	MIDIEVENT_EXPRESSIONSLIDE, 
	MIDIEVENT_VOLUME,
	MIDIEVENT_VOLUMESLIDE,
	MIDIEVENT_PAN,
	MIDIEVENT_PROGRAMCHANGE, 
	MIDIEVENT_PITCHBEND, 
	MIDIEVENT_TEMPO, 
	MIDIEVENT_TIMESIG,
	MIDIEVENT_MODULATION, 
	MIDIEVENT_BREATH,
	MIDIEVENT_SUSTAIN,
	MIDIEVENT_PORTAMENTO,
	MIDIEVENT_PORTAMENTOTIME,
	MIDIEVENT_LFO,
	MIDIEVENT_VIBRATO,
	MIDIEVENT_ENDOFTRACK,
	MIDIEVENT_TEXT
} MidiEventType;

class MidiTrack
{
public:
	MidiTrack(MidiFile* parentSeq, bool bMonophonic);
	virtual ~MidiTrack(void);

	void Sort(void);
	void WriteTrack(vector<BYTE> & buf);

	//void SetChannel(int theChannel);
	void SetChannelGroup(int theChannelGroup);

	ULONG GetDelta(void);
	void SetDelta(ULONG NewDelta);
	void AddDelta(ULONG AddDelta);
	void SubtractDelta(ULONG SubtractDelta);
	void ResetDelta(void);

	void AddNoteOn(BYTE channel, char key, char vel);
	void InsertNoteOn(BYTE channel, char key, char vel, ULONG absTime);
	void AddNoteOff(BYTE channel, char key);
	void InsertNoteOff(BYTE channel, char key, ULONG absTime);
	void AddNoteByDur(BYTE channel, char key, char vel, ULONG duration);
	void InsertNoteByDur(BYTE channel, char key, char vel, ULONG duration, ULONG absTime);
	//void AddVolMarker(BYTE channel, BYTE vol, char priority = PRIORITY_HIGHER);
	//void InsertVolMarker(BYTE channel, BYTE vol, ULONG absTime, char priority = PRIORITY_HIGHER);
	void AddVol(BYTE channel, BYTE vol/*, char priority = PRIORITY_MIDDLE*/);
	void InsertVol(BYTE channel, BYTE vol, ULONG absTime/*, char priority = PRIORITY_MIDDLE*/);
	void AddMasterVol(BYTE channel, BYTE mastVol/*, char priority = PRIORITY_HIGHER*/);
	void InsertMasterVol(BYTE channel, BYTE mastVol, ULONG absTime/*, char priority = PRIORITY_HIGHER*/);
	void AddPan(BYTE channel, BYTE pan);
	void InsertPan(BYTE channel, BYTE pan, ULONG absTime);
	void AddExpression(BYTE channel, BYTE expression);
	void InsertExpression(BYTE channel, BYTE expression, ULONG absTime);
	void AddSustain(BYTE channel, bool bOn);
	void InsertSustain(BYTE channel, bool bOn, ULONG absTime);
	void AddPortamento(BYTE channel, bool bOn);
	void InsertPortamento(BYTE channel, bool bOn, ULONG absTime);
	void AddPortamentoTime(BYTE channel, BYTE time);
	void InsertPortamentoTime(BYTE channel, BYTE time, ULONG absTime);

	void AddPitchBend(BYTE channel, SHORT bend);
	void InsertPitchBend(BYTE channel, short bend, ULONG absTime);
	void AddPitchBendRange(BYTE channel, BYTE semitones, BYTE cents);
	void InsertPitchBendRange(BYTE channel, BYTE semitones, BYTE cents, ULONG absTime);
	//void AddTranspose(BYTE channel, int transpose);
	void AddModulation(BYTE channel, BYTE depth);
	void AddBreath(BYTE channel, BYTE depth);
	void AddProgramChange(BYTE channel, BYTE progNum);
	void AddBankSelect(BYTE channel, BYTE bank);
	void AddBankSelectFine(BYTE channel, BYTE lsb);
	void InsertBankSelect(BYTE channel, BYTE bank, ULONG absTime);

	void AddTempo(ULONG microSeconds);
	void AddTempoBPM(double BPM);
	void InsertTempo(ULONG microSeconds, ULONG absTime);
	void InsertTempoBPM(double BPM, ULONG absTime);
	void AddTimeSig(BYTE numer, BYTE denom, BYTE clicksPerQuarter);
	void InsertTimeSig(BYTE numer, BYTE denom, BYTE ticksPerQuarter, ULONG absTime);
	void AddEndOfTrack(void);
	void InsertEndOfTrack(ULONG absTime);
	void AddText(const wchar_t* wstr);
	void InsertText(const wchar_t* wstr, ULONG absTime);

	// SPECIAL EVENTS
	//void AddTranspose(char semitones);
	void InsertGlobalTranspose(ULONG absTime, char semitones);
	void AddMarker(BYTE channel, string& markername, BYTE databyte1, BYTE databyte2, char priority = PRIORITY_MIDDLE);

public:
	MidiFile* parentSeq;
	bool bMonophonic;

	bool bHasEndOfTrack;
	int channelGroup;

	ULONG DeltaTime;			//a time value to be used for AddEvent

	DurNoteEvent* prevDurEvent;
	NoteEvent* prevDurNoteOff;
	char prevKey;
	//BYTE mastVol;
	//BYTE vol;
	//BYTE expression;
	bool bSustain;

	vector<MidiEvent*> aEvents;
};

class MidiFile
{
public:
	MidiFile(VGMSeq* assocSeq);
	MidiFile(ULONG thePpqn);
	~MidiFile(void);
	MidiTrack* AddTrack();
	MidiTrack* InsertTrack(int trackNum);
	void SetPPQN(WORD ppqn);
	UINT GetPPQN();
	void WriteMidiToBuffer(vector<BYTE> & buf);
	void Sort(void);
	bool SaveMidiFile(const wchar_t* filepath);

protected:
	//bool bAddedTempo;
	//bool bAddedTimeSig;

public:
	VGMSeq* assocSeq;
	WORD ppqn;

	vector<MidiTrack*> aTracks;
	MidiTrack globalTrack;			//events in the globalTrack will be copied into every other track
	char globalTranspose;
	bool bMonophonicTracks;
};

class MidiEvent
{
public:
	MidiEvent(MidiTrack* thePrntTrk, ULONG absoluteTime, BYTE theChannel, char thePriority);
	virtual ~MidiEvent(void);
	virtual MidiEventType GetEventType() = 0;
	void WriteVarLength(vector<BYTE> & buf, ULONG value);
	//virtual void PrepareWrite(void/*vector<MidiEvent*> & aEvents*/);
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time) = 0;
	ULONG WriteSysexEvent(vector<BYTE> & buf, UINT time, BYTE* data, size_t dataSize);
	ULONG WriteMetaEvent(vector<BYTE> & buf, UINT time, BYTE metaType, BYTE* data, size_t dataSize);
	ULONG WriteMetaTextEvent(vector<BYTE> & buf, UINT time, BYTE metaType, wstring wstr);

	bool operator<(const MidiEvent &) const;
	bool operator>(const MidiEvent &) const;

	MidiTrack* prntTrk;
	char priority;
	BYTE channel;
	ULONG AbsTime;			//absolute time... the number of ticks from the very beginning of the sequence at which this event occurs
};

class PriorityCmp
{
public:
	bool operator() (const MidiEvent* a, const MidiEvent* b) const
	{
		return (a->priority < b->priority);
	}
};

class AbsTimeCmp
{
public:
	bool operator() (const MidiEvent* a, const MidiEvent* b) const
	{
		return (a->AbsTime < b->AbsTime);
	}
};

class NoteEvent
	: public MidiEvent
{
public:
	NoteEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, bool bnoteDown, BYTE theKey, BYTE theVel = 64);
	virtual MidiEventType GetEventType() { return MIDIEVENT_NOTEON; }
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);

	bool bNoteDown;
	bool bUsePrevKey;
	char key;
	char vel;
};

//class DurNoteEvent
//	: public MidiEvent
//{
//public:
//	DurNoteEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE theKey, BYTE theVel, ULONG theDur);
//	//virtual void PrepareWrite(vector<MidiEvent*> & aEvents);
//	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);
//
//	bool bNoteDown;
//	char key;
//	char vel;
//	ULONG duration;
//};

class ControllerEvent
	: public MidiEvent
{
public:
	ControllerEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE controllerNum, BYTE theDataByte, char thePriority = PRIORITY_MIDDLE);
	virtual MidiEventType GetEventType() { return MIDIEVENT_UNDEFINED; }
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);

	BYTE controlNum;
	BYTE dataByte;
};

class VolumeEvent
	: public ControllerEvent
{
public:
	VolumeEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE volume) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 7, volume, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_VOLUME; }
};

class ExpressionEvent
	: public ControllerEvent
{
public:
	ExpressionEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE expression) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 11, expression, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_EXPRESSION; }
};

class SustainEvent
	: public ControllerEvent
{
public:
	SustainEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE bOn) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 64, (bOn) ? 0x7F : 0, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_SUSTAIN; }
};

class PortamentoEvent
	: public ControllerEvent
{
public:
	PortamentoEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE bOn) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 65, (bOn) ? 0x7F : 0, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_PORTAMENTO; }
};

class PortamentoTimeEvent
	: public ControllerEvent
{
public:
	PortamentoTimeEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE time) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 5, time, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_PORTAMENTOTIME; }
};

class PanEvent
	: public ControllerEvent
{
public:
	PanEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE pan) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 10, pan, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_PAN; }
};

class ModulationEvent
	: public ControllerEvent
{
public:
	ModulationEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE depth) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 1, depth, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_MODULATION; }
};

class BreathEvent
	: public ControllerEvent
{
public:
	BreathEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE depth) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 2, depth, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_BREATH; }
};

class BankSelectEvent
	: public ControllerEvent
{
public:
	BankSelectEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE bank) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 0, bank, PRIORITY_HIGH) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_BANKSELECT; }
};

class BankSelectFineEvent
	: public ControllerEvent
{
public:
	BankSelectFineEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE bank) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 32, bank, PRIORITY_HIGH) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_BANKSELECTFINE; }
};

/*
class VolEvent
	: public ControllerEvent
{
public:
	VolEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE theVol, char thePriority = PRIORITY_MIDDLE);
	virtual VolEvent* MakeCopy();
	//virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);

	BYTE vol;
};

class VolMarkerEvent
	: public MidiEvent
{
public:
	VolMarkerEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE theVol, char thePriority = PRIORITY_HIGHER);
	virtual VolMarkerEvent* MakeCopy();
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);

	BYTE vol;
};*/

class MastVolEvent
	: public MidiEvent
{
public:
	MastVolEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE mastVol);
	virtual MidiEventType GetEventType() { return MIDIEVENT_MASTERVOL; }
	//virtual MastVolEvent* MakeCopy();
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);

	BYTE mastVol;
};
/*
class ExpressionEvent
	: public MidiEvent
{
public:
	ExpressionEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE expression, char thePriority = PRIORITY_HIGHER);
	virtual ExpressionEvent* MakeCopy();
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);

	BYTE expression;
};*/

class ProgChangeEvent
	: public MidiEvent
{
public:
	ProgChangeEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, BYTE progNum);
	virtual MidiEventType GetEventType() { return MIDIEVENT_PROGRAMCHANGE; }
	//virtual ProgChangeEvent* MakeCopy();
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);

	BYTE programNum;
};

class PitchBendEvent
	: public MidiEvent
{
public:
	PitchBendEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, SHORT bend);
	virtual MidiEventType GetEventType() { return MIDIEVENT_PITCHBEND; }
	//virtual PitchBendEvent* MakeCopy();
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);

	SHORT bend;
};


class TempoEvent
	: public MidiEvent
{
public:
	TempoEvent(MidiTrack* prntTrk, ULONG absoluteTime, ULONG microSeconds);
	virtual MidiEventType GetEventType() { return MIDIEVENT_TEMPO; }
	//virtual TempoEvent* MakeCopy();
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);

	ULONG microSecs;
};

class TimeSigEvent
	: public MidiEvent
{
public:
	TimeSigEvent(MidiTrack* prntTrk, ULONG absoluteTime, BYTE numerator, BYTE denominator, BYTE clicksPerQuarter);
	virtual MidiEventType GetEventType() { return MIDIEVENT_TIMESIG; }
	//virtual TimeSigEvent* MakeCopy();
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);

	BYTE numer;
	BYTE denom;
	BYTE ticksPerQuarter;
};

class EndOfTrackEvent
	: public MidiEvent
{
public:
	EndOfTrackEvent(MidiTrack* prntTrk, ULONG absoluteTime);
	virtual MidiEventType GetEventType() { return MIDIEVENT_ENDOFTRACK; }
	//virtual EndOfTrackEvent* MakeCopy();
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);
};

class TextEvent
	: public MidiEvent
{
public:
	TextEvent(MidiTrack* prntTrk, ULONG absoluteTime, const wchar_t* wstr);
	virtual MidiEventType GetEventType() { return MIDIEVENT_TEXT; }
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);

	wstring text;
};

// SPECIAL EVENTS THAT AFFECT OTHER MIDI EVENTS RATHER THAN DIRECTLY OUTPUT TO THE FILE
class GlobalTransposeEvent
	: public MidiEvent
{
public:
	GlobalTransposeEvent(MidiTrack* prntTrk, ULONG absoluteTime, char semitones);
	virtual MidiEventType GetEventType() { return MIDIEVENT_GLOBALTRANSPOSE; }
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time);

	char semitones;
};

class MarkerEvent
	: public MidiEvent
{
public:
	MarkerEvent(MidiTrack* prntTrk, BYTE channel, ULONG absoluteTime, string& name, BYTE databyte1, BYTE databyte2, char thePriority = PRIORITY_MIDDLE)
		: MidiEvent(prntTrk, absoluteTime, channel, thePriority), name(name), databyte1(databyte1), databyte2(databyte2)
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_MARKER; }
	virtual ULONG WriteEvent(vector<BYTE> & buf, UINT time) { return time; }

	string name;
	BYTE databyte1, databyte2;
};



/*
class AFX_EXT_CLASS ProgChangeEvent
	: public MidiEvent
{
	ProgChangeEvent(void);
	~ProgramChangeEvent(void);

	BYTE progNum;
}

class AFX_EXT_CLASS ControllerEvent
	: public MidiEvent
{
public:
	NoteEvent(void);
	~NoteEvent(void);

	char key;
	char vel;
};*/

/*class AFX_EXT_CLASS NoteEventDur
	: public MidiEvent
{
public:
	NoteEvent(void);
	~NoteEvent(void);

	char key;
	char vel;
}*/