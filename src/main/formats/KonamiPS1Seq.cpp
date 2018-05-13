#include "pch.h"
#include "ScaleConversion.h"
#include "KonamiPS1Seq.h"

DECLARE_FORMAT(KonamiPS1);

//  ************
//  KonamiPS1Seq
//  ************

KonamiPS1Seq::KonamiPS1Seq(RawFile *file, uint32_t offset, const std::wstring &name)
    : VGMSeq(KonamiPS1Format::name, file, offset, kHeaderSize + file->GetWord(offset + 4), name) {
    bLoadTickByTick = true;
    bUseLinearAmplitudeScale = true;

    UseReverb();
}

void KonamiPS1Seq::ResetVars(void) {
    VGMSeq::ResetVars();
}

bool KonamiPS1Seq::GetHeaderInfo(void) {
    if (!IsKDT1Seq(rawfile, dwOffset)) {
        return false;
    }

    VGMHeader *header = AddHeader(dwOffset, kHeaderSize);
    header->AddSig(dwOffset, 4);
    header->AddSimpleItem(dwOffset + 4, 4, L"Size");
    header->AddSimpleItem(dwOffset + 8, 4, L"Timebase");
    SetPPQN(GetWord(dwOffset + 8));
    header->AddSimpleItem(dwOffset + 12, 4, L"Number Of Tracks");

    uint32_t numTracks = GetWord(dwOffset + 12);
    VGMHeader *trackSizeHeader = AddHeader(dwOffset + kHeaderSize, 2 * numTracks, L"Track Size");
    for (size_t trackIndex = 0; trackIndex < numTracks; trackIndex++) {
        std::wstringstream itemName;
        itemName << L"Track " << (trackIndex + 1) << L" Size";
        trackSizeHeader->AddSimpleItem(trackSizeHeader->dwOffset + (trackIndex * 2), 2, itemName.str());
    }

    return true;
}

bool KonamiPS1Seq::GetTrackPointers(void) {
    uint32_t numTracks = GetWord(dwOffset + 12);
    uint32_t trackStart = dwOffset + kHeaderSize + (numTracks * 2);
    for (size_t trackIndex = 0; trackIndex < numTracks; trackIndex++) {
        uint16_t trackSize = GetShort(dwOffset + kHeaderSize + (trackIndex * 2));
        KonamiPS1Track *track = new KonamiPS1Track(this, trackStart, trackSize);
        aTracks.push_back(track);
        trackStart += trackSize;
    }

    return true;
}

bool KonamiPS1Seq::IsKDT1Seq(RawFile *file, uint32_t offset) {
    if (offset + kHeaderSize >= file->size()) {
        return false;
    }

    if (file->GetByte(offset) != 'K' || file->GetByte(offset + 1) != 'D' ||
        file->GetByte(offset + 2) != 'T' || file->GetByte(offset + 3) != '1') {
        return false;
    }

    uint32_t dataSize = file->GetWord(offset + 4);
    uint32_t fileSize = kHeaderSize + dataSize;
    if (offset + fileSize >= file->size()) {
        return false;
    }

    uint32_t numTracks = file->GetWord(offset + 12);
    if (numTracks == 0 || offset + kHeaderSize + (numTracks * 2) >= file->size()) {
        return false;
    }

    uint32_t trackSize = 0;
    for (size_t trackIndex = 0; trackIndex < numTracks; trackIndex++) {
        trackSize += file->GetShort(offset + kHeaderSize + (trackIndex * 2));
    }

    if (offset + trackSize >= file->size()) {
        return false;
    }
    if (trackSize > fileSize) {
        return false;
    }

    return true;
}

uint32_t KonamiPS1Seq::GetKDT1FileSize(RawFile *file, uint32_t offset) {
    return kHeaderSize + file->GetWord(offset + 4);
}

//  ***************
//  KonamiPS1Track
//  ***************

KonamiPS1Track::KonamiPS1Track(KonamiPS1Seq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
    ResetVars();
    //bDetermineTrackLengthEventByEvent = true;
    //bWriteGenericEventAsTextEvent = true;
}

void KonamiPS1Track::ResetVars(void) {
    SeqTrack::ResetVars();

    vel = 100;
}

bool KonamiPS1Track::ReadEvent(void) {
    KonamiPS1Seq *parentSeq = (KonamiPS1Seq *) this->parentSeq;

    uint32_t beginOffset = curOffset;
    if (curOffset >= vgmfile->GetEndOffset()) {
        return false;
    }

    uint8_t statusByte = GetByte(curOffset++);
    bool bContinue = false;

    // TODO KDT1 parser
    AddUnknownItem(beginOffset, 1);

    return bContinue;
}
