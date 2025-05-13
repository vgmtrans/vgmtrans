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
                             Type type)
    : VGMItem(parentFile, theOffset, theLength, name, type),
      parentSeq(parentFile) {
}

bool VGMSeqSection::load() {
  ReadMode readMode = parentSeq->readMode;

  if (readMode == READMODE_ADD_TO_UI) {
    if (!parseTrackPointers()) {
      return false;
    }
  }

  return true;
}

bool VGMSeqSection::parseTrackPointers() {
  return true;
}

bool VGMSeqSection::postLoad() {
  if (parentSeq->readMode == READMODE_ADD_TO_UI) {
    for (const auto track : aTracks) {
      track->sortChildrenByOffset();
    }
  }

  return true;
}
