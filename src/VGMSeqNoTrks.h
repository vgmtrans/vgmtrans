#pragma once
#include "VGMSeq.h"
#include "SeqTrack.H"

class VGMSeqNoTrks :
	public VGMSeq,
	public SeqTrack
{
public:
	VGMSeqNoTrks(const std::string& format, RawFile* file, uint32_t offset);
public:
	virtual ~VGMSeqNoTrks(void);

	virtual void ResetVars();

	inline uint32_t GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) { return VGMSeq::GetBytes(nIndex, nCount, pBuffer); }
	inline uint8_t GetByte(uint32_t offset) { return VGMSeq::GetByte(offset);}
	inline uint16_t GetShort(uint32_t offset) { return VGMSeq::GetShort(offset);}
	inline uint32_t GetWord(uint32_t offset) { return VGMSeq::GetWord(offset);}
	inline uint16_t GetShortBE(uint32_t offset) { return VGMSeq::GetShortBE(offset);}
	inline uint32_t GetWordBE(uint32_t offset) { return VGMSeq::GetWordBE(offset);}
	inline uint32_t& offset(void) { return VGMSeq::dwOffset;}
	inline uint32_t& length(void) { return VGMSeq::unLength;}
	inline std::wstring& name(void) { return VGMSeq::name;}

	inline uint32_t& eventsOffset() { return SeqTrack::dwOffset;}
	inline void SetEventsOffset(long offset) { SeqTrack::dwOffset = offset;}		//this function must be called in GetHeaderInfo or before LoadEvents is called

	virtual void AddTime(uint32_t delta);
	//virtual bool AddEndOfTrack(uint32_t offset, uint32_t length, const wchar_t* sEventName = L"Track End");

	void SetCurTrack(uint32_t trackNum);
	void TryExpandMidiTracks(uint32_t numTracks);

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
	std::vector<MidiTrack*> midiTracks;		//an array of midi tracks... we will change pMidiTrack, which all the SeqTrack functions write to, to the correct one of these
										//tracks before we write every event
};
