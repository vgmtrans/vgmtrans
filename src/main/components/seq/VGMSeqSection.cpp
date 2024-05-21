/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include <ranges>

#include "helper.h"
#include "VGMMultiSectionSeq.h"
#include "SeqEvent.h"

VGMSeqSection::VGMSeqSection(VGMMultiSectionSeq *parentFile,
                             uint32_t theOffset,
                             uint32_t theLength,
                             const std::string& name,
                             EventColor color)
    : VGMContainerItem(parentFile, theOffset, theLength, name, color),
      parentSeq(parentFile) {
  AddContainer<SeqTrack>(aTracks);
}

VGMSeqSection::~VGMSeqSection() {
  DeleteVect<SeqTrack>(aTracks);
}

bool VGMSeqSection::Load() {
  ReadMode readMode = parentSeq->readMode;

  if (readMode == READMODE_ADD_TO_UI) {
    if (!GetTrackPointers()) {
      return false;
    }
  }

  return true;
}

bool VGMSeqSection::GetTrackPointers() {
  return true;
}

bool VGMSeqSection::PostLoad() {
  if (parentSeq->readMode == READMODE_ADD_TO_UI) {
    for (uint32_t i = 0; i < aTracks.size(); i++) {
      std::ranges::sort(aTracks[i]->aEvents, ItemPtrOffsetCmp());
    }
  }

  return true;
}