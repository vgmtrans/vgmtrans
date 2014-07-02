#pragma once
#include <iterator>
#include <string>
#include <vector>
#include <list>

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
	MIDIEVENT_TEXT,
	MIDIEVENT_RESET
} MidiEventType;

class MidiTrack
{
public:
	MidiTrack(MidiFile* parentSeq, bool bMonophonic);
	virtual ~MidiTrack(void);

	void Sort(void);
	void WriteTrack(std::vector<uint8_t> & buf);

	//void SetChannel(int theChannel);
	void SetChannelGroup(int theChannelGroup);

	uint32_t GetDelta(void);
	void SetDelta(uint32_t NewDelta);
	void AddDelta(uint32_t AddDelta);
	void SubtractDelta(uint32_t SubtractDelta);
	void ResetDelta(void);

	void AddNoteOn(uint8_t channel, int8_t key, int8_t vel);
	void InsertNoteOn(uint8_t channel, int8_t key, int8_t vel, uint32_t absTime);
	void AddNoteOff(uint8_t channel, int8_t key);
	void InsertNoteOff(uint8_t channel, int8_t key, uint32_t absTime);
	void AddNoteByDur(uint8_t channel, int8_t key, int8_t vel, uint32_t duration);
	void InsertNoteByDur(uint8_t channel, int8_t key, int8_t vel, uint32_t duration, uint32_t absTime);
	//void AddVolMarker(uint8_t channel, uint8_t vol, int8_t priority = PRIORITY_HIGHER);
	//void InsertVolMarker(uint8_t channel, uint8_t vol, uint32_t absTime, int8_t priority = PRIORITY_HIGHER);
	void AddVol(uint8_t channel, uint8_t vol/*, int8_t priority = PRIORITY_MIDDLE*/);
	void InsertVol(uint8_t channel, uint8_t vol, uint32_t absTime/*, int8_t priority = PRIORITY_MIDDLE*/);
	void AddMasterVol(uint8_t channel, uint8_t mastVol/*, int8_t priority = PRIORITY_HIGHER*/);
	void InsertMasterVol(uint8_t channel, uint8_t mastVol, uint32_t absTime/*, int8_t priority = PRIORITY_HIGHER*/);
	void AddPan(uint8_t channel, uint8_t pan);
	void InsertPan(uint8_t channel, uint8_t pan, uint32_t absTime);
	void AddExpression(uint8_t channel, uint8_t expression);
	void InsertExpression(uint8_t channel, uint8_t expression, uint32_t absTime);
	void AddReverb(uint8_t channel, uint8_t reverb);
	void InsertReverb(uint8_t channel, uint8_t reverb, uint32_t absTime);
	void AddModulation(uint8_t channel, uint8_t depth);
	void InsertModulation(uint8_t channel, uint8_t depth, uint32_t absTime);
	void AddBreath(uint8_t channel, uint8_t depth);
	void InsertBreath(uint8_t channel, uint8_t depth, uint32_t absTime);
	void AddSustain(uint8_t channel, bool bOn);
	void InsertSustain(uint8_t channel, bool bOn, uint32_t absTime);
	void AddPortamento(uint8_t channel, bool bOn);
	void InsertPortamento(uint8_t channel, bool bOn, uint32_t absTime);
	void AddPortamentoTime(uint8_t channel, uint8_t time);
	void InsertPortamentoTime(uint8_t channel, uint8_t time, uint32_t absTime);

	void AddPitchBend(uint8_t channel, int16_t bend);
	void InsertPitchBend(uint8_t channel, short bend, uint32_t absTime);
	void AddPitchBendRange(uint8_t channel, uint8_t semitones, uint8_t cents);
	void InsertPitchBendRange(uint8_t channel, uint8_t semitones, uint8_t cents, uint32_t absTime);
	void AddFineTuning(uint8_t channel, uint8_t msb, uint8_t lsb);
	void InsertFineTuning(uint8_t channel, uint8_t msb, uint8_t lsb, uint32_t absTime);
	void AddFineTuning(uint8_t channel, double cents);
	void InsertFineTuning(uint8_t channel, double cents, uint32_t absTime);
	void AddCoarseTuning(uint8_t channel, uint8_t msb, uint8_t lsb);
	void InsertCoarseTuning(uint8_t channel, uint8_t msb, uint8_t lsb, uint32_t absTime);
	void AddCoarseTuning(uint8_t channel, double semitones);
	void InsertCoarseTuning(uint8_t channel, double semitones, uint32_t absTime);
	void AddModulationDepthRange(uint8_t channel, uint8_t msb, uint8_t lsb);
	void InsertModulationDepthRange(uint8_t channel, uint8_t msb, uint8_t lsb, uint32_t absTime);
	void AddModulationDepthRange(uint8_t channel, double semitones);
	void InsertModulationDepthRange(uint8_t channel, double semitones, uint32_t absTime);
	//void AddTranspose(uint8_t channel, int transpose);
	void AddProgramChange(uint8_t channel, uint8_t progNum);
	void AddBankSelect(uint8_t channel, uint8_t bank);
	void AddBankSelectFine(uint8_t channel, uint8_t lsb);
	void InsertBankSelect(uint8_t channel, uint8_t bank, uint32_t absTime);

	void AddTempo(uint32_t microSeconds);
	void AddTempoBPM(double BPM);
	void InsertTempo(uint32_t microSeconds, uint32_t absTime);
	void InsertTempoBPM(double BPM, uint32_t absTime);
	void AddTimeSig(uint8_t numer, uint8_t denom, uint8_t clicksPerQuarter);
	void InsertTimeSig(uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter, uint32_t absTime);
	void AddEndOfTrack(void);
	void InsertEndOfTrack(uint32_t absTime);
	void AddText(const wchar_t* wstr);
	void InsertText(const wchar_t* wstr, uint32_t absTime);
	void AddSeqName(const wchar_t* wstr);
	void InsertSeqName(const wchar_t* wstr, uint32_t absTime);
	void AddTrackName(const wchar_t* wstr);
	void InsertTrackName(const wchar_t* wstr, uint32_t absTime);
	void AddGMReset();
	void InsertGMReset(uint32_t absTime);
	void AddGM2Reset();
	void InsertGM2Reset(uint32_t absTime);
	void AddGSReset();
	void InsertGSReset(uint32_t absTime);
	void AddXGReset();
	void InsertXGReset(uint32_t absTime);

	// SPECIAL EVENTS
	//void AddTranspose(int8_t semitones);
	void InsertGlobalTranspose(uint32_t absTime, int8_t semitones);
	void AddMarker(uint8_t channel, std::string& markername, uint8_t databyte1, uint8_t databyte2, int8_t priority = PRIORITY_MIDDLE);

public:
	MidiFile* parentSeq;
	bool bMonophonic;

	bool bHasEndOfTrack;
	int channelGroup;

	uint32_t DeltaTime;			//a time value to be used for AddEvent

	DurNoteEvent* prevDurEvent;
	NoteEvent* prevDurNoteOff;
	int8_t prevKey;
	//uint8_t mastVol;
	//uint8_t vol;
	//uint8_t expression;
	bool bSustain;

	std::vector<MidiEvent*> aEvents;
};

class MidiFile
{
public:
	MidiFile(VGMSeq* assocSeq);
	MidiFile(uint32_t thePpqn);
	~MidiFile(void);
	MidiTrack* AddTrack();
	MidiTrack* InsertTrack(uint32_t trackNum);
	void SetPPQN(uint16_t ppqn);
	uint32_t GetPPQN();
	void WriteMidiToBuffer(std::vector<uint8_t> & buf);
	void Sort(void);
	bool SaveMidiFile(const wchar_t* filepath);

protected:
	//bool bAddedTempo;
	//bool bAddedTimeSig;

public:
	VGMSeq* assocSeq;
	uint16_t ppqn;

	std::vector<MidiTrack*> aTracks;
	MidiTrack globalTrack;			//events in the globalTrack will be copied into every other track
	int8_t globalTranspose;
	bool bMonophonicTracks;
};

class MidiEvent
{
public:
	MidiEvent(MidiTrack* thePrntTrk, uint32_t absoluteTime, uint8_t theChannel, int8_t thePriority);
	virtual ~MidiEvent(void);
	virtual MidiEventType GetEventType() = 0;
	void WriteVarLength(std::vector<uint8_t> & buf, uint32_t value);
	//virtual void PrepareWrite(void/*vector<MidiEvent*> & aEvents*/);
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time) = 0;
	uint32_t WriteSysexEvent(std::vector<uint8_t> & buf, uint32_t time, uint8_t* data, size_t dataSize);
	uint32_t WriteMetaEvent(std::vector<uint8_t> & buf, uint32_t time, uint8_t metaType, uint8_t* data, size_t dataSize);
	uint32_t WriteMetaTextEvent(std::vector<uint8_t> & buf, uint32_t time, uint8_t metaType, std::wstring wstr);

	bool operator<(const MidiEvent &) const;
	bool operator>(const MidiEvent &) const;

	MidiTrack* prntTrk;
	int8_t priority;
	uint8_t channel;
	uint32_t AbsTime;			//absolute time... the number of ticks from the very beginning of the sequence at which this event occurs
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
	NoteEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, bool bnoteDown, uint8_t theKey, uint8_t theVel = 64);
	virtual MidiEventType GetEventType() { return MIDIEVENT_NOTEON; }
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	bool bNoteDown;
	bool bUsePrevKey;
	int8_t key;
	int8_t vel;
};

//class DurNoteEvent
//	: public MidiEvent
//{
//public:
//	DurNoteEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t theKey, uint8_t theVel, uint32_t theDur);
//	//virtual void PrepareWrite(std::vector<MidiEvent*> & aEvents);
//	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);
//
//	bool bNoteDown;
//	int8_t key;
//	int8_t vel;
//	uint32_t duration;
//};

class ControllerEvent
	: public MidiEvent
{
public:
	ControllerEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t controllerNum, uint8_t theDataByte, int8_t thePriority = PRIORITY_MIDDLE);
	virtual MidiEventType GetEventType() { return MIDIEVENT_UNDEFINED; }
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	uint8_t controlNum;
	uint8_t dataByte;
};

class VolumeEvent
	: public ControllerEvent
{
public:
	VolumeEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t volume) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 7, volume, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_VOLUME; }
};

class ExpressionEvent
	: public ControllerEvent
{
public:
	ExpressionEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t expression) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 11, expression, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_EXPRESSION; }
};

class SustainEvent
	: public ControllerEvent
{
public:
	SustainEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t bOn) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 64, (bOn) ? 0x7F : 0, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_SUSTAIN; }
};

class PortamentoEvent
	: public ControllerEvent
{
public:
	PortamentoEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t bOn) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 65, (bOn) ? 0x7F : 0, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_PORTAMENTO; }
};

class PortamentoTimeEvent
	: public ControllerEvent
{
public:
	PortamentoTimeEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t time) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 5, time, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_PORTAMENTOTIME; }
};

class PanEvent
	: public ControllerEvent
{
public:
	PanEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t pan) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 10, pan, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_PAN; }
};

class ModulationEvent
	: public ControllerEvent
{
public:
	ModulationEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t depth) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 1, depth, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_MODULATION; }
};

class BreathEvent
	: public ControllerEvent
{
public:
	BreathEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t depth) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 2, depth, PRIORITY_MIDDLE) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_BREATH; }
};

class BankSelectEvent
	: public ControllerEvent
{
public:
	BankSelectEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t bank) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 0, bank, PRIORITY_HIGH) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_BANKSELECT; }
};

class BankSelectFineEvent
	: public ControllerEvent
{
public:
	BankSelectFineEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t bank) 
		: ControllerEvent(prntTrk, channel, absoluteTime, 32, bank, PRIORITY_HIGH) 
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_BANKSELECTFINE; }
};

/*
class VolEvent
	: public ControllerEvent
{
public:
	VolEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t theVol, int8_t thePriority = PRIORITY_MIDDLE);
	virtual VolEvent* MakeCopy();
	//virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	uint8_t vol;
};

class VolMarkerEvent
	: public MidiEvent
{
public:
	VolMarkerEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t theVol, int8_t thePriority = PRIORITY_HIGHER);
	virtual VolMarkerEvent* MakeCopy();
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	uint8_t vol;
};*/

class MastVolEvent
	: public MidiEvent
{
public:
	MastVolEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t mastVol);
	virtual MidiEventType GetEventType() { return MIDIEVENT_MASTERVOL; }
	//virtual MastVolEvent* MakeCopy();
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	uint8_t mastVol;
};
/*
class ExpressionEvent
	: public MidiEvent
{
public:
	ExpressionEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t expression, int8_t thePriority = PRIORITY_HIGHER);
	virtual ExpressionEvent* MakeCopy();
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	uint8_t expression;
};*/

class ProgChangeEvent
	: public MidiEvent
{
public:
	ProgChangeEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t progNum);
	virtual MidiEventType GetEventType() { return MIDIEVENT_PROGRAMCHANGE; }
	//virtual ProgChangeEvent* MakeCopy();
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	uint8_t programNum;
};

class PitchBendEvent
	: public MidiEvent
{
public:
	PitchBendEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, int16_t bend);
	virtual MidiEventType GetEventType() { return MIDIEVENT_PITCHBEND; }
	//virtual PitchBendEvent* MakeCopy();
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	int16_t bend;
};


class TempoEvent
	: public MidiEvent
{
public:
	TempoEvent(MidiTrack* prntTrk, uint32_t absoluteTime, uint32_t microSeconds);
	virtual MidiEventType GetEventType() { return MIDIEVENT_TEMPO; }
	//virtual TempoEvent* MakeCopy();
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	uint32_t microSecs;
};

class TimeSigEvent
	: public MidiEvent
{
public:
	TimeSigEvent(MidiTrack* prntTrk, uint32_t absoluteTime, uint8_t numerator, uint8_t denominator, uint8_t clicksPerQuarter);
	virtual MidiEventType GetEventType() { return MIDIEVENT_TIMESIG; }
	//virtual TimeSigEvent* MakeCopy();
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	uint8_t numer;
	uint8_t denom;
	uint8_t ticksPerQuarter;
};

class EndOfTrackEvent
	: public MidiEvent
{
public:
	EndOfTrackEvent(MidiTrack* prntTrk, uint32_t absoluteTime);
	virtual MidiEventType GetEventType() { return MIDIEVENT_ENDOFTRACK; }
	//virtual EndOfTrackEvent* MakeCopy();
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);
};

class TextEvent
	: public MidiEvent
{
public:
	TextEvent(MidiTrack* prntTrk, uint32_t absoluteTime, const wchar_t* wstr);
	virtual MidiEventType GetEventType() { return MIDIEVENT_TEXT; }
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	std::wstring text;
};

class SeqNameEvent
	: public MidiEvent
{
public:
	SeqNameEvent(MidiTrack* prntTrk, uint32_t absoluteTime, const wchar_t* wstr);
	virtual MidiEventType GetEventType() { return MIDIEVENT_TEXT; }
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	std::wstring text;
};

class TrackNameEvent
	: public MidiEvent
{
public:
	TrackNameEvent(MidiTrack* prntTrk, uint32_t absoluteTime, const wchar_t* wstr);
	virtual MidiEventType GetEventType() { return MIDIEVENT_TEXT; }
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	std::wstring text;
};

// SPECIAL EVENTS THAT AFFECT OTHER MIDI EVENTS RATHER THAN DIRECTLY OUTPUT TO THE FILE
class GlobalTransposeEvent
	: public MidiEvent
{
public:
	GlobalTransposeEvent(MidiTrack* prntTrk, uint32_t absoluteTime, int8_t semitones);
	virtual MidiEventType GetEventType() { return MIDIEVENT_GLOBALTRANSPOSE; }
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	int8_t semitones;
};

class MarkerEvent
	: public MidiEvent
{
public:
	MarkerEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, std::string& name, uint8_t databyte1, uint8_t databyte2, int8_t thePriority = PRIORITY_MIDDLE)
		: MidiEvent(prntTrk, absoluteTime, channel, thePriority), name(name), databyte1(databyte1), databyte2(databyte2)
	{}
	virtual MidiEventType GetEventType() { return MIDIEVENT_MARKER; }
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time) { return time; }

	std::string name;
	uint8_t databyte1, databyte2;
};

class GMResetEvent
	: public MidiEvent
{
public:
	GMResetEvent(MidiTrack* prntTrk, uint32_t absoluteTime);
	virtual MidiEventType GetEventType() { return MIDIEVENT_RESET; }
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);
};

class GM2ResetEvent
	: public MidiEvent
{
public:
	GM2ResetEvent(MidiTrack* prntTrk, uint32_t absoluteTime);
	virtual MidiEventType GetEventType() { return MIDIEVENT_RESET; }
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);
};

class GSResetEvent
	: public MidiEvent
{
public:
	GSResetEvent(MidiTrack* prntTrk, uint32_t absoluteTime);
	virtual MidiEventType GetEventType() { return MIDIEVENT_RESET; }
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);
};

class XGResetEvent
	: public MidiEvent
{
public:
	XGResetEvent(MidiTrack* prntTrk, uint32_t absoluteTime);
	virtual MidiEventType GetEventType() { return MIDIEVENT_RESET; }
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);
};



/*
class AFX_EXT_CLASS ProgChangeEvent
	: public MidiEvent
{
	ProgChangeEvent(void);
	~ProgramChangeEvent(void);

	uint8_t progNum;
}

class AFX_EXT_CLASS ControllerEvent
	: public MidiEvent
{
public:
	NoteEvent(void);
	~NoteEvent(void);

	int8_t key;
	int8_t vel;
};*/

/*class AFX_EXT_CLASS NoteEventDur
	: public MidiEvent
{
public:
	NoteEvent(void);
	~NoteEvent(void);

	int8_t key;
	int8_t vel;
}*/