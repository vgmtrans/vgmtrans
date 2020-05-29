/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMMultiSectionSeq.h"
#include "SeqEvent.h"

VGMSeqSection::VGMSeqSection(VGMMultiSectionSeq *parentFile, uint32_t theOffset, uint32_t theLength,
                             const std::string theName, uint8_t color)
    : VGMContainerItem(parentFile, theOffset, theLength, theName, color), parentSeq(parentFile) {
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
        for (auto & aTrack : aTracks) {
            std::sort(aTrack->aEvents.begin(), aTrack->aEvents.end(), [](const VGMItem *a, const VGMItem *b) { return a->dwOffset < b->dwOffset; });
        }
    }

    return true;
}
