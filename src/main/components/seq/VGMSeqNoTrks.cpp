/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMSeqNoTrks.h"
#include "Options.h"

VGMSeqNoTrks::VGMSeqNoTrks(const std::string &format, RawFile *file, uint32_t offset, std::string name)
    : VGMSeq(format, file, offset, 0, std::move(name)), SeqTrack(this) {
  VGMSeqNoTrks::resetVars();
}

VGMSeqNoTrks::~VGMSeqNoTrks() = default;

void VGMSeqNoTrks::resetVars() {
  midiTracks.clear();  // no need to delete the contents, that happens when the midi is deleted
  tryExpandMidiTracks(nNumTracks);

  channel = 0;
  setCurTrack(0);

  VGMSeq::resetVars();
  SeqTrack::resetVars();
}

// LoadMain() - loads all sequence data into the class
bool VGMSeqNoTrks::load() {
  this->SeqTrack::readMode = READMODE_ADD_TO_UI;
  this->VGMSeq::readMode = READMODE_ADD_TO_UI;
  if (!parseHeader())
    return false;

  if (!loadEvents())
    return false;

  // Workaround for this VGMSeqNoTrks' multiple inheritance diamond problem. Both VGMSeq and
  // SeqTrack have their own m_children fields. VGMSeq is the one we care about. We need to transfer
  // SeqTrack::m_children into VGMSeq::m_children and then clear it from SeqTrack so that their
  // destructors don't doubly delete the children.
  SeqTrack::transferChildren(static_cast<VGMSeq*>(this));

  if (length() == 0) {
    VGMSeq::setGuessedLength();
  }

  return true;
}

bool VGMSeqNoTrks::loadEvents(long stopTime) {
  resetVars();
  if (alwaysWriteInitialTempo())
    addTempoBPMNoItem(initialTempoBPM);
  if (alwaysWriteInitialVol())
    for (int i = 0; i < 16; i++) {
      channel = i;
      addVolNoItem(initialVolume());
    }
  if (alwaysWriteInitialExpression())
    for (int i = 0; i < 16; i++) {
      channel = i;
      addExpressionNoItem(initialExpression());
    }
  if (alwaysWriteInitialReverb())
    for (int i = 0; i < 16; i++) {
      channel = i;
      addReverbNoItem(initialReverbLevel());
    }
  if (alwaysWriteInitialPitchBendRange())
    for (int i = 0; i < 16; i++) {
      channel = i;
      addPitchBendRangeNoItem(initialPitchBendRange());
    }

  bInLoop = false;
  curOffset = eventsOffset();  // start at beginning of track
  while (curOffset < rawFile()->size()) {
    if (getTime() >= static_cast<u_long>(stopTime)) {
      break;
    }

    if (!readEvent()) {
      break;
    }
  }
  return true;
}

MidiFile *VGMSeqNoTrks::convertToMidi(const VGMColl* coll) {
  this->SeqTrack::readMode = this->VGMSeq::readMode = READMODE_FIND_DELTA_LENGTH;

  useColl(coll);

  if (!loadEvents())
    return nullptr;
  if (!postLoad())
    return nullptr;

  long stopTime = totalTicks;

  MidiFile *newmidi = new MidiFile(this);
  this->midi = newmidi;
  this->SeqTrack::readMode = this->VGMSeq::readMode = READMODE_CONVERT_TO_MIDI;
  timedEventIndex().clear();
  transposeTimeline().clear();
  if (!loadEvents(stopTime)) {
    delete midi;
    this->midi = nullptr;
    timedEventIndex().clear();
    transposeTimeline().clear();
    return nullptr;
  }
  if (!postLoad()) {
    delete midi;
    this->midi = nullptr;
    timedEventIndex().clear();
    transposeTimeline().clear();
    return nullptr;
  }
  this->midi = nullptr;
  return newmidi;
}

MidiTrack *VGMSeqNoTrks::firstMidiTrack() {
  if (midiTracks.size() > 0) {
    return midiTracks[0];
  } else {
    return pMidiTrack;
  }
}

// checks whether or not we have already created the given number of MidiTracks.  If not, it appends
// the extra tracks. doesn't ever need to be called directly by format code, since SetCurMidiTrack
// does so automatically.
void VGMSeqNoTrks::tryExpandMidiTracks(uint32_t numTracks) {
  if (VGMSeq::readMode != READMODE_CONVERT_TO_MIDI)
    return;
  if (midiTracks.size() < numTracks) {
    size_t initialTrackSize = midiTracks.size();
    for (size_t i = initialTrackSize; i < numTracks; i++) {
      auto* midiTrack = midi->addTrack();
      midiTracks.push_back(midiTrack);
      if (i == 9 && ConversionOptions::the().skipChannel10()) {
        midiTrack->setChannelGroup(1);
        midiTrack->addMidiPort(1);
      }
    }
  }
}

void VGMSeqNoTrks::setChannel(u8 newChannel) {
  setCurTrack(newChannel);
  if (newChannel == 9 && ConversionOptions::the().skipChannel10())
    channel = 0;
  else
    channel = newChannel;
}

void VGMSeqNoTrks::setCurTrack(uint32_t trackNum) {
  if (VGMSeq::readMode != READMODE_CONVERT_TO_MIDI)
    return;

  tryExpandMidiTracks(trackNum + 1);
  pMidiTrack = midiTracks[trackNum];
}

void VGMSeqNoTrks::setTime(uint32_t newTime) {
  time = newTime;
  if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI)
    for (uint32_t i = 0; i < midiTracks.size(); i++)
      pMidiTrack->setDelta(newTime);
}

void VGMSeqNoTrks::addTime(uint32_t delta) {
  time += delta;
  if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
    for (uint32_t i = 0; i < midiTracks.size(); i++)
      midiTracks[i]->addDelta(delta);
  }
}
