#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "Format.h"			//can replace this with NinSNES-specific format header file
#include "NinSnesFormat.h"
#include "NinSnesScanner.h"

class NinSnesSection;
class NinSnesTrack;

enum 
{
	EVENT_RET = 1,		//start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
	EVENT_TIE,
	EVENT_REST,
	//EVENT_NOTE,
	EVENT_PROGCHANGE,
	EVENT_PAN,
	EVENT_PANFADE,
	EVENT_MASTVOL,
	EVENT_MASTVOLFADE,
	EVENT_TEMPO,
	EVENT_TEMPOFADE,
	EVENT_GLOBTRANSP,
	EVENT_TRANSP,
	EVENT_VOL,
	EVENT_VOLFADE,
	EVENT_GOSUB,
	EVENT_SETPERCBASE,
	EVENT_UNKNOWN0,
	EVENT_UNKNOWN1, 
	EVENT_UNKNOWN2,
	EVENT_UNKNOWN3,
	EVENT_LOOPBEGIN,
	EVENT_LOOPEND
};

const uint8_t voltbl[16] = { 0x19, 0x32, 0x4c, 0x65, 0x72, 0x7f, 0x8c, 0x98,
					 0xa5, 0xb2, 0xbf, 0xcb, 0xd8, 0xe5, 0xf2, 0xfc };

const uint8_t durpcttbl[8] = { 0x32, 0x65, 0x7f, 0x98, 0xb2, 0xcb, 0xe5, 0xfc };

const uint8_t pantbl[0x15] = { 0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
							0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
							0x7a, 0x7c, 0x7d, 0x7e, 0x7f };


class NinSnesSeq :
	public VGMSeq
{
public:
	NinSnesSeq(RawFile* file, NinSnesVersion ver, uint32_t offset, uint32_t length = 0, std::wstring theName = L"NinSnes Seq");
	virtual ~NinSnesSeq();

	virtual bool LoadMain();
	bool GetSectionPointers();
	bool LoadAllSections();
	void LoadDefaultEventMap(NinSnesSeq *pSeqFile);

public:
	NinSnesVersion version;

	std::vector<uint16_t> sectPlayList;
	uint16_t playListRptPtr;

	std::vector<NinSnesSection*> aSections;
	std::map<uint16_t, NinSnesSection*> sectionMap;

	uint32_t curDelta;
	uint8_t META_CUTOFF;
	uint8_t NOTEREST_CUTOFF;
	uint8_t NOTE_CUTOFF;
	std::map<uint8_t, int> EventMap;
	std::map<uint8_t, uint8_t> DrumMap;
	uint8_t percbase;
	uint8_t mvol;
};

class NinSnesSection
	: public VGMContainerItem
{
public:
	NinSnesSection(NinSnesSeq* parentSeq, uint32_t offset);
	~NinSnesSection();
	bool GetHeaderInfo(uint16_t headerOffset);
	int LoadSection(int startTime);

public:
	ReadMode readMode;
	//uint32_t endTime;
	//uint32_t totalTime;
	
	//uint32_t endTime;		//when the end of Section event is hit, the absolute time is stored here.  This method assumes the end of section occurs on first track
	uint32_t hdrOffset;
	std::vector<uint16_t> trackOffsets;
	std::vector<SeqTrack*> aSectTracks;
	std::vector<SeqTrack*> aSongTracks;
};


class NinSnesTrack
	: public SeqTrack
{
public:
	NinSnesTrack(NinSnesSection* parentSect, uint32_t offset, int trackNumber);
	NinSnesTrack(NinSnesSeq* parentSeq, uint32_t offset, int trackNumber);
	bool ReadEvent(uint32_t totalTime);
	void AddTime(uint32_t AddDelta);
	void SetTime(uint32_t NewDelta);
	void SubtractTime(uint32_t SubtractDelta);
	void SetPercBase(uint8_t newBase);
	uint8_t GetPercBase();

public:
	NinSnesSection* prntSect;
	size_t beginEventIndex;
	size_t endEventIndex;
	int trackNum;
	uint32_t nextEventTime;

	uint32_t notedur;
	uint32_t durpct;

	uint32_t substart;
	uint32_t subret;
    uint8_t subcount;
	uint16_t loopdest;
	uint8_t loopcount;
};
