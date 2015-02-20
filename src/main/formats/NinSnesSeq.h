#pragma once
#include "VGMMultiSectionSeq.h"
#include "SeqTrack.h"
#include "NinSnesFormat.h"
#include "NinSnesScanner.h"

enum NinSnesSeqEventType
{
	EVENT_UNKNOWN0 = 1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_UNKNOWN1,
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_UNKNOWN4,
	EVENT_NOP,
	EVENT_NOP1,
	EVENT_END,
	EVENT_NOTE_PARAM,
	EVENT_NOTE,
	EVENT_TIE,
	EVENT_REST,
	EVENT_PERCUSSION_NOTE,
	EVENT_PROGCHANGE,
	EVENT_PAN,
	EVENT_PAN_FADE,
	EVENT_VIBRATO_ON,
	EVENT_VIBRATO_OFF,
	EVENT_MASTER_VOLUME,
	EVENT_MASTER_VOLUME_FADE,
	EVENT_TEMPO,
	EVENT_TEMPO_FADE,
	EVENT_GLOBAL_TRANSPOSE,
	EVENT_TRANSPOSE,
	EVENT_TREMOLO_ON,
	EVENT_TREMOLO_OFF,
	EVENT_VOLUME,
	EVENT_VOLUME_FADE,
	EVENT_CALL,
	EVENT_VIBRATO_FADE,
	EVENT_PITCH_ENVELOPE_TO,
	EVENT_PITCH_ENVELOPE_FROM,
	EVENT_PITCH_ENVELOPE_OFF,
	EVENT_TUNING,
	EVENT_ECHO_ON,
	EVENT_ECHO_OFF,
	EVENT_ECHO_PARAM,
	EVENT_ECHO_VOLUME_FADE,
	EVENT_PITCH_SLIDE,
	EVENT_PERCCUSION_PATCH_BASE,

	// Nintendo RD2:
	EVENT_RD2_PROGCHANGE_AND_ADSR,

	// Konami:
	EVENT_KONAMI_LOOP_START,
	EVENT_KONAMI_LOOP_END,
	EVENT_KONAMI_ADSR_AND_GAIN,

	// Lemmings:
	EVENT_LEMMINGS_NOTE_PARAM,

	// Intelligent Systems:
	// Fire Emblem 3 & 4:
	EVENT_INTELLI_NOTE_PARAM,
	EVENT_INTELLI_JUMP_SHORT,
	EVENT_INTELLI_FE3_UNKNOWN_EVENT_F5,
	EVENT_INTELLI_FE3_UNKNOWN_EVENT_F9,
	EVENT_INTELLI_FE3_UNKNOWN_EVENT_FA,
	EVENT_INTELLI_FE4_UNKNOWN_EVENT_FC,
	EVENT_INTELLI_FE4_UNKNOWN_EVENT_FD,
};

class NinSnesTrackSharedData
{
public:
	NinSnesTrackSharedData();

	virtual void ResetVars(void);

	uint8_t spcNoteDuration;
	uint8_t spcNoteDurRate;
	uint8_t spcNoteVolume;
	int8_t spcTranspose;
	uint16_t loopReturnAddress;
	uint16_t loopStartAddress;
	uint8_t loopCount;

	// Konami:
	uint16_t konamiLoopStart;
	uint8_t konamiLoopCount;

	// Intelligent Systems:
	uint8_t intelliFE3NoteFlags;
};

class NinSnesSeq :
	public VGMMultiSectionSeq
{
public:
	NinSnesSeq(RawFile* file, NinSnesVersion ver, uint32_t offset, uint8_t percussion_base = 0, const std::vector<uint8_t>& theVolumeTable = std::vector<uint8_t>(), const std::vector<uint8_t>& theDurRateTable = std::vector<uint8_t>(), std::wstring theName = L"NinSnes Seq");
	virtual ~NinSnesSeq();

	virtual bool GetHeaderInfo();
	virtual void ResetVars();
	virtual bool ReadEvent(long stopTime);

	double GetTempoInBPM();
	double GetTempoInBPM(uint8_t tempo);

	uint16_t ConvertToAPUAddress(uint16_t offset);
	uint16_t GetShortAddress(uint32_t offset);

	NinSnesVersion version;
	uint8_t STATUS_END;
	uint8_t STATUS_NOTE_MIN;
	uint8_t STATUS_NOTE_MAX;
	uint8_t STATUS_PERCUSSION_NOTE_MIN;
	uint8_t STATUS_PERCUSSION_NOTE_MAX;
	std::map<uint8_t, NinSnesSeqEventType> EventMap;

	NinSnesTrackSharedData sharedTrackData[8];

	std::vector<uint8_t> volumeTable;
	std::vector<uint8_t> durRateTable;
	std::vector<uint8_t> panTable;

	uint8_t spcPercussionBase;
	uint8_t sectionRepeatCount;

	// Konami:
	uint16_t konamiBaseAddress;

	// Intelligent Systems:
	std::vector<uint8_t> intelliDurVolTable;

protected:
	VGMHeader* header;

private:
	void LoadEventMap(void);

	uint8_t spcPercussionBaseInit;
};

class NinSnesSection
	: public VGMSeqSection
{
public:
	NinSnesSection(NinSnesSeq* parentFile, long offset = 0, long length = 0);

	virtual bool GetTrackPointers();

	uint16_t ConvertToAPUAddress(uint16_t offset);
	uint16_t GetShortAddress(uint32_t offset);
};

class NinSnesTrack
	: public SeqTrack
{
public:
	NinSnesTrack(NinSnesSection* parentSection, long offset = 0, long length = 0, const std::wstring& theName = L"NinSnes Track");

	virtual void ResetVars(void);
	virtual bool ReadEvent(void);

	uint16_t ConvertToAPUAddress(uint16_t offset);
	uint16_t GetShortAddress(uint32_t offset);
	void GetVolumeBalance(uint16_t pan, double & volumeLeft, double & volumeRight);
	uint8_t ReadPanTable(uint16_t pan);

	NinSnesSection* parentSection;
	NinSnesTrackSharedData* shared;
	bool available;
};
