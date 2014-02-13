#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "QSoundFormat.h"

extern enum QSoundVer;

class QSoundSeq :
	public VGMSeq
{
public:
	QSoundSeq(RawFile* file, ULONG offset, QSoundVer fmt_version, std::wstring& name);
	virtual ~QSoundSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	/*virtual bool LoadTracks(void);*/
	virtual bool PostLoad(void);

public:
	QSoundVer fmt_version;
};


class QSoundTrack
	: public SeqTrack
{
public:
	QSoundTrack(QSoundSeq* parentSeq, long offset = 0, long length = 0);
	virtual void ResetVars();
	virtual bool ReadEvent(void);

private:
	QSoundVer GetVersion() { return ((QSoundSeq*)this->parentSeq)->fmt_version; }			

	bool bPrevNoteTie;
	BYTE prevTieNote;
	BYTE origTieNote;
	BYTE curDeltaTable;
	BYTE noteState;
	BYTE bank;
	BYTE loop[4];
	U32 loopOffset[4];	//used for detecting infinite loops, which truly occur in certain games
};