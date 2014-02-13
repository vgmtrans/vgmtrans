#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "Format.h"			//can replace this with NinSNES-specific format header file
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

const BYTE voltbl[16] = { 0x19, 0x32, 0x4c, 0x65, 0x72, 0x7f, 0x8c, 0x98,
					 0xa5, 0xb2, 0xbf, 0xcb, 0xd8, 0xe5, 0xf2, 0xfc };

const BYTE durpcttbl[8] = { 0x32, 0x65, 0x7f, 0x98, 0xb2, 0xcb, 0xe5, 0xfc };

const BYTE pantbl[0x15] = { 0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
							0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
							0x7a, 0x7c, 0x7d, 0x7e, 0x7f };


class NinSnesSeq :
	public VGMSeq
{
public:
	NinSnesSeq(RawFile* file, ULONG offset, ULONG length = 0, std::wstring theName = L"NinSnes Seq");
	virtual ~NinSnesSeq();

	virtual bool LoadMain();
	bool GetSectionPointers();
	bool LoadAllSections();
	void LoadDefaultEventMap(NinSnesSeq *pSeqFile);

public:
	std::vector<USHORT> sectPlayList;
	USHORT playListRptPtr;

	std::vector<NinSnesSection*> aSections;
	std::map<USHORT, NinSnesSection*> sectionMap;

	ULONG curDelta;
	BYTE META_CUTOFF;
	BYTE NOTEREST_CUTOFF;
	BYTE NOTE_CUTOFF;
	std::map<BYTE, int> EventMap;
	std::map<BYTE, BYTE> DrumMap;
	BYTE percbase;
	BYTE mvol;
};

class NinSnesSection
	: public VGMContainerItem
{
public:
	NinSnesSection(NinSnesSeq* parentSeq, ULONG offset);
	~NinSnesSection();
	bool GetHeaderInfo(USHORT headerOffset);
	int LoadSection(int startTime);

public:
	ReadMode readMode;
	//ULONG endTime;
	//ULONG totalTime;
	
	//ULONG endTime;		//when the end of Section event is hit, the absolute time is stored here.  This method assumes the end of section occurs on first track
	UINT hdrOffset;
	std::vector<USHORT> trackOffsets;
	std::vector<SeqTrack*> aSectTracks;
	std::vector<SeqTrack*> aSongTracks;
};


class NinSnesTrack
	: public SeqTrack
{
public:
	NinSnesTrack(NinSnesSection* parentSect, ULONG offset, int trackNumber);
	NinSnesTrack(NinSnesSeq* parentSeq, ULONG offset, int trackNumber);
	bool ReadEvent(ULONG totalTime);
	void AddTime(ULONG AddDelta);
	void SetTime(ULONG NewDelta);
	void SubtractTime(ULONG SubtractDelta);
	void SetPercBase(BYTE newBase);
	BYTE GetPercBase();

public:
	NinSnesSection* prntSect;
	ULONG beginEventIndex;
	ULONG endEventIndex;
	int trackNum;
	ULONG nextEventTime;

	ULONG notedur;
	ULONG durpct;

	ULONG substart;
	ULONG subret;
    BYTE subcount;
	USHORT loopdest;
	BYTE loopcount;
};
