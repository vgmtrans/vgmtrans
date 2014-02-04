#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "AkaoFormat.h"
#include "Matcher.h"

class AkaoInstrSet;

static const unsigned short delta_time_table[] = { 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x6, 0x3, 0x20, 0x10, 0x8, 0x4, 0x0, 0xA0A0, 0xA0A0 };


class AkaoSeq :
	public VGMSeq
{
public:
	AkaoSeq(RawFile* file, ULONG offset);
	virtual ~AkaoSeq(void);

	virtual bool GetHeaderInfo();
	virtual bool GetTrackPointers(void);
	
	BYTE GetNumPositiveBits(ULONG ulWord);

public:
	AkaoInstrSet* instrset;
	USHORT seq_id;
	USHORT assoc_ss_id;
	int nVersion;		//version of the AKAO format

	bool bUsesIndividualArts;

	enum { VERSION_1, VERSION_2, VERSION_3 };
};

class AkaoTrack
	: public SeqTrack
{
public:
	AkaoTrack(AkaoSeq* parentFile, long offset = 0, long length = 0);
	
	virtual void ResetVars(void);
	virtual bool ReadEvent(void);

public:
	

protected:
	BYTE relative_key, base_key;
	ULONG current_delta_time;
	unsigned long loop_begin_loc[8];
	unsigned long loopID[8];
	int loop_counter[8];
	int loop_layer;
	int loop_begin_layer;
	bool bNotePlaying;
	vector<ULONG> vCondJumpAddr;
};
