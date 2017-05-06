#include "pch.h"
#include "common.h"
#include "main/Core.h"
#include "Options.h"
#include "VGMMultiSectionSeq.h"

VGMMultiSectionSeq::VGMMultiSectionSeq(const std::string &format,
                                       RawFile *file,
                                       uint32_t offset,
                                       uint32_t length,
                                       std::wstring name)
    : VGMSeq(format, file, offset, length, name),
      dwStartOffset(offset) {
  AddContainer<VGMSeqSection>(aSections);
  RemoveContainer<SeqTrack>(aTracks);
}

VGMMultiSectionSeq::~VGMMultiSectionSeq() {
  // Clear all track pointers to prevent delete, they must be aliases of section tracks
  aTracks.clear();

  DeleteVect<VGMSeqSection>(aSections);
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

bool VGMMultiSectionSeq::LoadTracks(ReadMode readMode, long stopTime) {
  this->readMode = readMode;

  curOffset = dwStartOffset;

  // Clear all track pointers to prevent delete, they must be aliases of section tracks
  aTracks.clear();

  // reset variables
  ResetVars();

  // load all tracks
  uint32_t stopOffset = vgmfile->GetEndOffset();
  while (curOffset < stopOffset && time < stopTime) {
    if (!ReadEvent(stopTime)) {
      break;
    }
  }

  if (readMode == READMODE_ADD_TO_UI) {
    SetGuessedLength();
    if (unLength == 0) {
      return false;
    }
  }

  bool succeeded = true;
  if (!PostLoad()) {
    succeeded = false;
  }

  return succeeded;
}

bool VGMMultiSectionSeq::LoadSection(VGMSeqSection *section, long stopTime) {
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

bool VGMMultiSectionSeq::ReadEvent(long stopTime) {
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
    return (foreverLoops < ConversionOptions::GetNumSequenceLoops());
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
