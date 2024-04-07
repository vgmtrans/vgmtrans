/*
 * VGMTrans (c) 2002-2019
 * VGMTransQt (c) 2020
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file

 * GBA MusicPlayer2000 (Sappy)
 *
 * Special Thanks / Respects To:
 * GBAMusRiper (c) 2012 by Bregalad
 * http://www.romhacking.net/utilities/881/
 * http://www.romhacking.net/documents/%5B462%5Dsappy.txt
 */

#include "pch.h"
#include "MP2kSeq.h"

#include <array>
#include "MP2kFormat.h"

DECLARE_FORMAT(MP2k);

constexpr std::array<uint8_t, 0x31> length_table{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
    0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1C,
    0x1E, 0x20, 0x24, 0x28, 0x2A, 0x2C, 0x30, 0x34, 0x36, 0x38, 0x3C, 0x40, 0x42,
    0x44, 0x48, 0x4C, 0x4E, 0x50, 0x54, 0x58, 0x5A, 0x5C, 0x60};

constexpr uint8_t STATE_NOTE = 0;
constexpr uint8_t STATE_TIE = 1;
constexpr uint8_t STATE_TIE_END = 2;
constexpr uint8_t STATE_VOL = 3;
constexpr uint8_t STATE_PAN = 4;
constexpr uint8_t STATE_PITCHBEND = 5;
constexpr uint8_t STATE_MODULATION = 6;

MP2kSeq::MP2kSeq(RawFile *file, uint32_t offset, std::string name)
    : VGMSeq(MP2kFormat::name, file, offset, 0, name) {
    bAllowDiscontinuousTrackData = true;
}

bool MP2kSeq::GetHeaderInfo(void) {
    if (dwOffset + 2 > vgmfile->GetEndOffset()) {
        return false;
    }

    nNumTracks = GetShort(dwOffset);

    // if there are no tracks or there are more tracks than allowed
    // return an error; the sequence shall be deleted
    if (nNumTracks == 0 || nNumTracks > 24) {
        return false;
    }
    if (dwOffset + 8 + nNumTracks * 4 > vgmfile->GetEndOffset()) {
        return false;
    }

    VGMHeader *seqHdr = AddHeader(dwOffset, 8 + nNumTracks * 4, "Sequence header");
    seqHdr->AddSimpleItem(dwOffset, 1, "Number of tracks");
    seqHdr->AddSimpleItem(dwOffset + 1, 1, "Unknown");
    seqHdr->AddSimpleItem(dwOffset + 2, 1, "Priority");
    seqHdr->AddSimpleItem(dwOffset + 3, 1, "Reverb");

    uint32_t dwInstPtr = GetWord(dwOffset + 4);
    seqHdr->AddPointer(dwOffset + 4, 4, dwInstPtr - 0x8000000, true, "Instrument pointer");
    for (uint32_t i = 0; i < nNumTracks; i++) {
        uint32_t dwTrackPtrOffset = dwOffset + 8 + i * 4;
        uint32_t dwTrackPtr = GetWord(dwTrackPtrOffset);
        seqHdr->AddPointer(dwTrackPtrOffset, 4, dwTrackPtr - 0x8000000, true, "Track pointer");
    }

    SetPPQN(0x30);

    return true;
}

bool MP2kSeq::GetTrackPointers(void) {
    // Add each tracks
    for (unsigned int i = 0; i < nNumTracks; i++) {
        uint32_t dwTrackPtrOffset = dwOffset + 8 + i * 4;
        uint32_t dwTrackPtr = GetWord(dwTrackPtrOffset);
        aTracks.push_back(new MP2kTrack(this, dwTrackPtr - 0x8000000));
    }

    // Make seq offset the first track offset
    for (auto& track : aTracks) {
        if (track->dwOffset < dwOffset) {
            if (unLength != 0) {
                unLength += (dwOffset - track->dwOffset);
            }
            dwOffset = track->dwOffset;
        }
    }

    return true;
}

//  *********
//  MP2kTrack
//  *********

MP2kTrack::MP2kTrack(MP2kSeq *parentFile, long offset, long length)
    : SeqTrack(parentFile, offset, length) {}

bool MP2kTrack::ReadEvent(void) {
    uint32_t beginOffset = curOffset;
    uint8_t status_byte = GetByte(curOffset++);
    bool bContinue = true;

    /* Status change event (note, vel (fade), vol, ...) */
    if (status_byte <= 0x7F) {
        handleStatusCommand(beginOffset, status_byte);
    } else if (status_byte >= 0x80 && status_byte <= 0xB0) { /* Rest event */
        AddRest(beginOffset, curOffset - beginOffset, length_table[status_byte - 0x80] * 2);
    } else if (status_byte >= 0xD0) { /* Note duration event */
        state = STATE_NOTE;
        curDuration = length_table[status_byte - 0xCF] * 2;

        /* Assume key and velocity values if the next value is greater than 0x7F */
        if (GetByte(curOffset) > 0x7F) {
            AddNoteByDurNoItem(prevKey, prevVel, curDuration);
            AddGenericEvent(beginOffset, curOffset - beginOffset,
                            "Duration Note State + Note On (prev key and vel)", {}, CLR_DURNOTE);
        } else {
            AddGenericEvent(beginOffset, curOffset - beginOffset, "Duration Note State", {},
                            CLR_CHANGESTATE);
        }
    } else if (status_byte == 0xB1) { /* End of track */
        AddEndOfTrack(beginOffset, curOffset - beginOffset);
        return false;
    } else if (status_byte == 0xB2) { /* Goto */
        uint32_t destOffset = GetWord(curOffset) - 0x8000000;
        curOffset += 4;
        uint32_t length = curOffset - beginOffset;
        uint32_t dwEndTrackOffset = curOffset;

        curOffset = destOffset;
        if (!IsOffsetUsed(destOffset) || loopEndPositions.size() != 0) {
            AddGenericEvent(beginOffset, length, "Goto", {}, CLR_LOOPFOREVER);
        } else {
            bContinue = AddLoopForever(beginOffset, length, "Goto");
        }

        // Add next end of track event
        if (readMode == READMODE_ADD_TO_UI) {
            if (dwEndTrackOffset < this->parentSeq->vgmfile->GetEndOffset()) {
                uint8_t nextCmdByte = GetByte(dwEndTrackOffset);
                if (nextCmdByte == 0xB1) {
                    AddEndOfTrack(dwEndTrackOffset, 1);
                }
            }
        }
    } else if (status_byte > 0xB2 && status_byte <= 0xCF) { /* Special event */
        handleSpecialCommand(beginOffset, status_byte);
    }

    return bContinue;
}

void MP2kTrack::handleStatusCommand(uint32_t beginOffset, uint8_t status_byte) {
    switch (state) {
        case STATE_NOTE: {
            /* Velocity update might be needed */
            if (GetByte(curOffset) <= 0x7F) {
                current_vel = GetByte(curOffset++);
                /* If the next value is 0-127, it's unknown (velocity-related?) */
                if (GetByte(curOffset) <= 0x7F) {
                    curOffset++;
                }
            }
            AddNoteByDur(beginOffset, curOffset - beginOffset, status_byte, current_vel,
                         curDuration);
            break;
        }

        case STATE_TIE: {
            /* Velocity update might be needed */
            if (GetByte(curOffset) <= 0x7F) {
                current_vel = GetByte(curOffset++);
                /* If the next value is 0-127, it's unknown (velocity-related?) */
                if (GetByte(curOffset) <= 0x7F) {
                    curOffset++;
                }
            }
            AddNoteOn(beginOffset, curOffset - beginOffset, status_byte, current_vel, "Tie");
            break;
        }

        case STATE_TIE_END: {
            AddNoteOff(beginOffset, curOffset - beginOffset, status_byte, "End Tie");
            break;
        }

        case STATE_VOL: {
            AddVol(beginOffset, curOffset - beginOffset, status_byte);
            break;
        }

        case STATE_PAN: {
            AddPan(beginOffset, curOffset - beginOffset, status_byte);
            break;
        }

        case STATE_PITCHBEND: {
            AddPitchBend(beginOffset, curOffset - beginOffset,
                         ((int16_t)(status_byte - 0x40)) * 128);
            break;
        }

        case STATE_MODULATION: {
            AddModulation(beginOffset, curOffset - beginOffset, status_byte);
            break;
        }

        default: {
            pRoot->AddLogItem(new LogItem("Illegal status", LOG_LEVEL_ERR, "MP2kSeq"));
            break;
        }
    }
}

void MP2kTrack::handleSpecialCommand(uint32_t beginOffset, uint8_t status_byte) {
    switch (status_byte) {
        // Branch
        case 0xB3: {
            uint32_t destOffset = GetWord(curOffset);
            curOffset += 4;
            loopEndPositions.push_back(curOffset);
            AddGenericEvent(beginOffset, curOffset - beginOffset, "Pattern Play", {}, CLR_LOOP);
            curOffset = destOffset - 0x8000000;
            break;
        }

        // Branch Break
        case 0xB4: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, "Pattern End", {}, CLR_LOOP);
            if (loopEndPositions.size() != 0) {
                curOffset = loopEndPositions.back();
                loopEndPositions.pop_back();
            }
            break;
        }

        case 0xBA: {
            curOffset++;
            AddGenericEvent(beginOffset, curOffset - beginOffset, "Priority", {}, CLR_PRIORITY);
            break;
        }

        case 0xBB: {
            uint8_t tempo = GetByte(curOffset++) * 2;  // tempo in bpm is data byte * 2
            AddTempoBPM(beginOffset, curOffset - beginOffset, tempo);
            break;
        }

        case 0xBC: {
            transpose = GetByte(curOffset++);
            AddGenericEvent(beginOffset, curOffset - beginOffset, "Key Shift", {}, CLR_TRANSPOSE);
            break;
        }

        case 0xBD: {
            uint8_t progNum = GetByte(curOffset++);
            AddProgramChange(beginOffset, curOffset - beginOffset, progNum);
            break;
        }

        case 0xBE: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, "Volume State", {},
                            CLR_CHANGESTATE);
            state = STATE_VOL;
            break;
        }

        // pan
        case 0xBF: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, "Pan State", {}, CLR_CHANGESTATE);
            state = STATE_PAN;
            break;
        }

        // pitch bend
        case 0xC0: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Bend State", {},
                            CLR_CHANGESTATE);
            state = STATE_PITCHBEND;
            break;
        }

        // pitch bend range
        case 0xC1: {
            curOffset++;
            AddGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Bend Range", {},
                            CLR_PITCHBENDRANGE);
            break;
        }

        // lfo speed
        case 0xC2: {
            curOffset++;
            AddGenericEvent(beginOffset, curOffset - beginOffset, "LFO Speed", {}, CLR_LFO);
            break;
        }

        // lfo delay
        case 0xC3: {
            curOffset++;
            AddGenericEvent(beginOffset, curOffset - beginOffset, "LFO Delay", {}, CLR_LFO);
            break;
        }

        // modulation depth
        case 0xC4: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Depth State", {},
                            CLR_MODULATION);
            state = STATE_MODULATION;
            break;
        }

        // modulation type
        case 0xC5: {
            curOffset++;
            AddGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Type", {},
                            CLR_MODULATION);
            break;
        }

        case 0xC8: {
            curOffset++;
            AddGenericEvent(beginOffset, curOffset - beginOffset, "Microtune", {}, CLR_PITCHBEND);
            break;
        }

        // extend command
        case 0xCD: {
            // This command has a subcommand-byte and its arguments.
            // xIECV = 0x08;		//  imi.echo vol   ***lib
            // xIECL = 0x09;		//  imi.echo len   ***lib
            //
            // Probably, some games extend this command by their own code.

            uint8_t subCommand = GetByte(curOffset++);
            uint8_t subParam = GetByte(curOffset);

            if (subCommand == 0x08 && subParam <= 127) {
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume", {}, CLR_MISC);
            } else if (subCommand == 0x09 && subParam <= 127) {
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, "Echo Length", {}, CLR_MISC);
            } else {
                // Heuristic method
                while (subParam <= 127) {
                    curOffset++;
                    subParam = GetByte(curOffset);
                }
                AddGenericEvent(beginOffset, curOffset - beginOffset, "Extend Command", {},
                                CLR_UNKNOWN);
            }
            break;
        }

        case 0xCE: {
            state = STATE_TIE_END;

            // yes, this seems to be how the actual driver code handles it.  Ex. Aria of Sorrow
            // (U): 0x80D91C0 - handle 0xCE event
            if (GetByte(curOffset) > 0x7F) {
                AddNoteOffNoItem(prevKey);
                AddGenericEvent(beginOffset, curOffset - beginOffset, "End Tie State + End Tie", {},
                                CLR_TIE);
            } else
                AddGenericEvent(beginOffset, curOffset - beginOffset, "End Tie State", {}, CLR_TIE);
            break;
        }

        case 0xCF: {
            state = STATE_TIE;
            if (GetByte(curOffset) > 0x7F) {
                AddNoteOnNoItem(prevKey, prevVel);
                AddGenericEvent(beginOffset, curOffset - beginOffset,
                                "Tie State + Tie (with prev key and vel)", {}, CLR_TIE);
            } else
                AddGenericEvent(beginOffset, curOffset - beginOffset, "Tie State", {}, CLR_TIE);
            break;
        }

        default: {
            AddUnknown(beginOffset, curOffset - beginOffset);
            break;
        }
    }
}

//  *********
//  MP2kEvent
//  *********

MP2kEvent::MP2kEvent(MP2kTrack *pTrack, uint8_t stateType)
    : SeqEvent(pTrack), eventState(stateType) {}
