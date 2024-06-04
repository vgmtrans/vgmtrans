/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "common.h"
#include "Root.h"
#include "Options.h"
#include "VGMMultiSectionSeq.h"
#include "helper.h"

VGMMultiSectionSeq::VGMMultiSectionSeq(const std::string& format,
                                       RawFile *file,
                                       uint32_t offset,
                                       uint32_t length,
                                       std::string name)
    : VGMSeq(format, file, offset, length, std::move(name)),
      dwStartOffset(offset) {
}

void VGMMultiSectionSeq::ResetVars() {
  VGMSeq::ResetVars();

  foreverLoops = 0;
}

bool VGMMultiSectionSeq::LoadMain() {
  readMode = READMODE_ADD_TO_UI;

  if (!GetHeaderInfo())
    return false;

  if (!LoadTracks(readMode))
    return false;

  return true;
}

bool VGMMultiSectionSeq::LoadTracks(ReadMode readMode, uint32_t stopTime) {
  this->readMode = readMode;

  curOffset = dwStartOffset;

  // Clear all track pointers to prevent delete, they must be aliases of section tracks
  aTracks.clear();

  // reset variables
  ResetVars();

  // load all tracks
  uint32_t stopOffset = vgmFile()->GetEndOffset();
  while (curOffset < stopOffset && time < stopTime) {
    if (!ReadEvent(stopTime)) {
      break;
    }
  }

  return PostLoad();
}

bool VGMMultiSectionSeq::PostLoad() {
  if (readMode == READMODE_ADD_TO_UI) {
    std::ranges::sort(aInstrumentsUsed);

    for (const auto& track : aTracks) {
      track->sortChildrenByOffset();
    }
    for (const auto& section : aSections) {
      section->addChildren(section->aTracks);
    }
    addChildren(aSections);
    SetGuessedLength();
    if (unLength == 0) {
      return false;
    }
  } else if (readMode == READMODE_CONVERT_TO_MIDI) {
    midi->Sort();
  }

  return true;
}


bool VGMMultiSectionSeq::LoadSection(VGMSeqSection *section, uint32_t stopTime) {
  // reset variables
  assert(aTracks.size() == 0 || aTracks.size() == section->aTracks.size());
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    MidiTrack *previousMidiTrack = NULL;
    if (aTracks.size() != 0) {
      previousMidiTrack = aTracks[trackNum]->pMidiTrack;
    }

    section->aTracks[trackNum]->readMode = readMode;
    if (!section->aTracks[trackNum]->LoadTrackInit(trackNum, previousMidiTrack)) {
      return false;
    }
  }

  // set new track pointers
  aTracks.assign(section->aTracks.begin(), section->aTracks.end());

  LoadTracksMain(stopTime);

  if (!section->PostLoad()) {
    return false;
  }

  return true;
}

bool VGMMultiSectionSeq::IsOffsetUsed(uint32_t offset) {
  return IsItemAtOffset(offset, false);
}

bool VGMMultiSectionSeq::ReadEvent(long /*stopTime*/) {
  return false;        //by default, don't add any events, just stop immediately.
}

void VGMMultiSectionSeq::AddSection(VGMSeqSection *section) {
  if (dwOffset > section->dwOffset) {
    uint32_t distance = dwOffset - section->dwOffset;
    dwOffset = section->dwOffset;
    if (unLength != 0) {
      unLength += distance;
    }
  }
  aSections.push_back(section);
}

bool VGMMultiSectionSeq::AddLoopForeverNoItem() {
  foreverLoops++;
  if (readMode == READMODE_ADD_TO_UI) {
    return false;
  }
  else if (readMode == READMODE_FIND_DELTA_LENGTH) {
    return (foreverLoops < ConversionOptions::the().GetNumSequenceLoops());
  }
  return true;
}

VGMSeqSection *VGMMultiSectionSeq::GetSectionFromOffset(uint32_t offset) {
  for (size_t sectionIndex = 0; sectionIndex < aSections.size(); sectionIndex++) {
    VGMSeqSection *section = aSections[sectionIndex];
    if (section->dwOffset == offset) {
      return section;
    }
  }
  return NULL;
}
