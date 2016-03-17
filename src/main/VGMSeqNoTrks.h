#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"

class VGMSeqNoTrks:
    public VGMSeq,
    public SeqTrack {
 public:
  VGMSeqNoTrks(const std::string &format, RawFile *file, uint32_t offset, std::wstring name = L"VGM Sequence");
 public:
  virtual ~VGMSeqNoTrks(void);

  virtual void ResetVars();

  inline uint32_t GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) {
    return VGMSeq::GetBytes(nIndex,
                            nCount,
                            pBuffer);
  }
  inline uint8_t GetByte(uint32_t offset) { return VGMSeq::GetByte(offset); }
  inline uint16_t GetShort(uint32_t offset) { return VGMSeq::GetShort(offset); }
  inline uint32_t GetWord(uint32_t offset) { return VGMSeq::GetWord(offset); }
  inline uint16_t GetShortBE(uint32_t offset) { return VGMSeq::GetShortBE(offset); }
  inline uint32_t GetWordBE(uint32_t offset) { return VGMSeq::GetWordBE(offset); }
  inline uint32_t &offset(void) { return VGMSeq::dwOffset; }
  inline uint32_t &length(void) { return VGMSeq::unLength; }
  inline std::wstring &name(void) { return VGMSeq::name; }

  inline uint32_t &eventsOffset() { return dwEventsOffset; }

  //this function must be called in GetHeaderInfo or before LoadEvents is called
  inline void SetEventsOffset(long offset) {
    dwEventsOffset = offset;
    if (SeqTrack::readMode == READMODE_ADD_TO_UI) {
      SeqTrack::dwOffset = offset;
    }
  }

  virtual void AddTime(uint32_t delta);

  void SetCurTrack(uint32_t trackNum);
  void TryExpandMidiTracks(uint32_t numTracks);

  virtual bool LoadMain();                //Function to load all the information about the sequence
  virtual bool LoadEvents(long stopTime = 1000000);
  virtual MidiFile *ConvertToMidi();
  virtual MidiTrack *GetFirstMidiTrack();

  uint32_t dwEventsOffset;

 protected:
  //an array of midi tracks... we will change pMidiTrack, which all the SeqTrack functions write to, to the
  // corresponding MidiTrack in this vector before we write every event
  std::vector<MidiTrack *> midiTracks;
};
