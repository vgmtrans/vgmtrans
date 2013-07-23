#pragma once
#include "VGMSeq.h"
#include "SeqTrack.H"

class VGMSeqNoTrks :
	public VGMSeq,
	public SeqTrack
{
public:
	VGMSeqNoTrks(const string& format, RawFile* file, ULONG offset);
public:
	virtual ~VGMSeqNoTrks(void);

	virtual void ResetVars();

	inline UINT GetBytes(UINT nIndex, UINT nCount, void *pBuffer) { return VGMSeq::GetBytes(nIndex, nCount, pBuffer); }
	inline BYTE GetByte(ULONG offset) { return VGMSeq::GetByte(offset);}
	inline USHORT GetShort(ULONG offset) { return VGMSeq::GetShort(offset);}
	inline UINT GetWord(ULONG offset) { return VGMSeq::GetWord(offset);}
	inline USHORT GetShortBE(ULONG offset) { return VGMSeq::GetShortBE(offset);}
	inline UINT GetWordBE(ULONG offset) { return VGMSeq::GetWordBE(offset);}
	inline ULONG& offset(void) { return VGMSeq::dwOffset;}
	inline ULONG& length(void) { return VGMSeq::unLength;}
	inline wstring& name(void) { return VGMSeq::name;}

	inline ULONG& eventsOffset() { return SeqTrack::dwOffset;}
	inline void SetEventsOffset(long offset) { SeqTrack::dwOffset = offset;}		//this function must be called in GetHeaderInfo or before LoadEvents is called

	virtual void AddDelta(ULONG delta);
	virtual void AddEndOfTrack(unsigned long offset, unsigned long length, const wchar_t* sEventName = L"Track End");

	void SetCurTrack(ULONG trackNum);
	void TryExpandMidiTracks(ULONG numTracks);

	//virtual VGMItem* GetItemFromOffset(long offset);
	//virtual vector<const char*>* GetMenuItemNames() {return menu.GetMenuItemNames();}
	//virtual bool CallMenuItem(VGMItem* item, int menuItemNum){ return menu.CallMenuItem(item, menuItemNum); }
			
	virtual bool LoadMain();				//Function to load all the information about the sequence
	//virtual int GetHeaderInfo(void);
//	virtual void AddTrack(SeqTrack* track);		//should be called for all tracks before LoadTracks.
	virtual bool LoadEvents();
	virtual MidiFile* ConvertToMidi();
	//virtual int ApplyTable(void);	//create and apply table handler object for sequence

	//virtual bool SaveAsMidi(const wchar_t* filepath);

protected:
	vector<MidiTrack*> midiTracks;		//an array of midi tracks... we will change pMidiTrack, which all the SeqTrack functions write to, to the correct one of these
										//tracks before we write every event
};
