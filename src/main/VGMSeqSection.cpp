/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 

#include <cassert>
#include <cwchar>
#include <cmath>
#include <algorithm>
#include <climits>
#include <stdio.h>
#include <cstdint>

#include <fstream>
#include <vector>
#include <list>
#include <map>
#include <string>
#include <cstring>
#include <sstream>
#include <unordered_set>
#include <iterator>
#include <iostream>
#include <iomanip>
#include <ctype.h>
#include "portable.h"
#define countof(arr) sizeof(arr) / sizeof(arr[0])


#include "common.h"
#include "VGMMultiSectionSeq.h"
#include "SeqEvent.h"

VGMSeqSection::VGMSeqSection(VGMMultiSectionSeq *parentFile,
                             uint32_t theOffset,
                             uint32_t theLength,
                             const std::wstring theName,
                             uint8_t color)
    : VGMContainerItem(parentFile, theOffset, theLength, theName, color),
      parentSeq(parentFile) {
  AddContainer<SeqTrack>(aTracks);
}

VGMSeqSection::~VGMSeqSection(void) {
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
      std::sort(aTracks[i]->aEvents.begin(), aTracks[i]->aEvents.end(), ItemPtrOffsetCmp());
    }
  }

  return true;
}
