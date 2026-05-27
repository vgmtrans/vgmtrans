/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"
#include "SeqEvent.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <string>
#include <vector>

class VGMSeqNoTrks : public VGMSeq, public SeqTrack {
public:
  VGMSeqNoTrks(const std::string &format, RawFile *file, u32 offset,
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
  [[nodiscard]] inline u32 offset() const { return VGMSeq::offset(); }
  [[nodiscard]] inline u32 length() const { return VGMSeq::length(); }
  inline void setOffset(u32 offset) { VGMSeq::setOffset(offset); }
  inline void setLength(u32 length) { VGMSeq::setLength(length); }
  [[nodiscard]] inline std::string name() { return VGMSeq::name(); }

  [[nodiscard]] inline RawFile* rawFile() { return VGMSeq::rawFile(); }

  [[nodiscard]] inline u32 &eventsOffset() { return dwEventsOffset; }

  // this function must be called in GetHeaderInfo or before LoadEvents is called
  inline void setEventsOffset(u32 offset) {
    dwEventsOffset = offset;
    if (SeqTrack::readMode == READMODE_ADD_TO_UI) {
      SeqTrack::setOffset(offset);
    }
  }

  void setTime(u32 newTime) override;
  void addTime(u32 delta) override;

  void setChannel(u8 newChannel);
  void tryExpandMidiTracks(u32 numTracks);

  bool load() override;  // Function to load all the information about the sequence
  virtual bool loadEvents(long stopTime = 1000000);
  MidiFile *convertToMidi(const VGMColl* coll) override;
  MidiFile *convertToMidi(const VGMColl* coll, const ConversionContext& context) override;
  MidiTrack *firstMidiTrack() override;

  u32 dwEventsOffset;

protected:
  void setCurTrack(u32 trackNum);

  // an array of midi tracks... we will change pMidiTrack, which all the SeqTrack functions write
  // to, to the corresponding MidiTrack in this vector before we write every event
  std::vector<MidiTrack *> midiTracks;
};
