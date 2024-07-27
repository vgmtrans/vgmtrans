/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"

class VGMSeqNoTrks : public VGMSeq, public SeqTrack {
public:
  VGMSeqNoTrks(const std::string &format, RawFile *file, uint32_t offset,
               std::string name = "VGM Sequence");

public:
  ~VGMSeqNoTrks() override;

  void resetVars() override;

  using VGMSeq::readBytes;
  using VGMSeq::readByte;
  using VGMSeq::readShort;
  using VGMSeq::readWord;
  using VGMSeq::readShortBE;
  using VGMSeq::readWordBE;
  inline uint32_t &offset() { return VGMSeq::dwOffset; }
  inline uint32_t &length() { return VGMSeq::unLength; }
  inline std::string name() { return VGMSeq::name(); }

  inline RawFile* rawFile() { return VGMSeq::rawFile(); }

  inline uint32_t &eventsOffset() { return dwEventsOffset; }

  // this function must be called in GetHeaderInfo or before LoadEvents is called
  inline void setEventsOffset(uint32_t offset) {
    dwEventsOffset = offset;
    if (SeqTrack::readMode == READMODE_ADD_TO_UI) {
      SeqTrack::dwOffset = offset;
    }
  }

  virtual void addTime(uint32_t delta);

  void setCurTrack(uint32_t trackNum);
  void tryExpandMidiTracks(uint32_t numTracks);

  bool loadMain() override;  // Function to load all the information about the sequence
  virtual bool loadEvents(long stopTime = 1000000);
  MidiFile *convertToMidi() override;
  MidiTrack *firstMidiTrack() override;

  uint32_t dwEventsOffset;

protected:
  // an array of midi tracks... we will change pMidiTrack, which all the SeqTrack functions write
  // to, to the corresponding MidiTrack in this vector before we write every event
  std::vector<MidiTrack *> midiTracks;
};
