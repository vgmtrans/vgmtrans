#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "QSoundFormat.h"

extern enum QSoundVer;

class QSoundSeq :
	public VGMSeq
{
public:
	QSoundSeq(RawFile* file, ULONG offset, QSoundVer fmt_version, wstring& name);
	virtual ~QSoundSeq(void);

	virtual int GetHeaderInfo(void);
	virtual int GetTrackPointers(void);
	/*virtual int LoadTracks(void);*/
	virtual int PostLoad(void);

public:
	QSoundVer fmt_version;
};


class QSoundTrack
	: public SeqTrack
{
public:
	QSoundTrack(QSoundSeq* parentSeq, long offset = 0, long length = 0);
	virtual void ResetVars();
	virtual int ReadEvent(void);

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