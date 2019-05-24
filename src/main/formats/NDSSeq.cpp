/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NDSSeq.h"

DECLARE_FORMAT(NDS);

using namespace std;

NDSSeq::NDSSeq(RawFile *file, uint32_t offset, uint32_t length, wstring name)
    : VGMSeq(NDSFormat::name, file, offset, length, name) {}

bool NDSSeq::GetHeaderInfo(void) {
    VGMHeader *SSEQHdr = AddHeader(dwOffset, 0x10, L"SSEQ Chunk Header");
    SSEQHdr->AddSig(dwOffset, 8);
    SSEQHdr->AddSimpleItem(dwOffset + 8, 4, L"Size");
    SSEQHdr->AddSimpleItem(dwOffset + 12, 2, L"Header Size");
    SSEQHdr->AddUnknownItem(dwOffset + 14, 2);
    // SeqChunkHdr->AddSimpleItem(dwOffset, 4, L"Blah");
    unLength = GetShort(dwOffset + 8);
    SetPPQN(0x30);
    return true;  // successful
}

bool NDSSeq::GetTrackPointers(void) {
    VGMHeader *DATAHdr = AddHeader(dwOffset + 0x10, 0xC, L"DATA Chunk Header");
    DATAHdr->AddSig(dwOffset + 0x10, 4);
    DATAHdr->AddSimpleItem(dwOffset + 0x10 + 4, 4, L"Size");
    DATAHdr->AddSimpleItem(dwOffset + 0x10 + 8, 4, L"Data Pointer");
    uint32_t offset = dwOffset + 0x1C;
    uint8_t b = GetByte(offset);
    aTracks.push_back(new NDSTrack(this));

    // FE XX XX signifies multiple tracks, each true bit in the XX values signifies there is a track
    // for that channel
    if (b == 0xFE) {
        VGMHeader *TrkPtrs = AddHeader(offset, 0, L"Track Pointers");
        TrkPtrs->AddSimpleItem(offset, 3, L"Valid Tracks");
        offset += 3;  // but all we need to do is check for subsequent 0x93 track pointer events
        b = GetByte(offset);
        uint32_t songDelay = 0;

        while (b == 0x80) {
            uint32_t value;
            uint8_t c;
            uint32_t beginOffset = offset;
            offset++;
            if ((value = GetByte(offset++)) & 0x80) {
                value &= 0x7F;
                do {
                    value = (value << 7) + ((c = GetByte(offset++)) & 0x7F);
                } while (c & 0x80);
            }
            songDelay += value;
            TrkPtrs->AddSimpleItem(beginOffset, offset - beginOffset, L"Delay");
            // songDelay += SeqTrack::ReadVarLen(++offset);
            b = GetByte(offset);
            break;
        }

        // Track/Channel assignment and pointer.  Channel # is irrelevant
        while (b == 0x93) {
            TrkPtrs->AddSimpleItem(offset, 5, L"Track Pointer");
            uint32_t trkOffset = GetByte(offset + 2) + (GetByte(offset + 3) << 8) +
                                 (GetByte(offset + 4) << 16) + dwOffset + 0x1C;
            NDSTrack *newTrack = new NDSTrack(this, trkOffset);
            aTracks.push_back(newTrack);
            // newTrack->
            offset += 5;
            b = GetByte(offset);
        }
        TrkPtrs->unLength = offset - TrkPtrs->dwOffset;
    }
    aTracks[0]->dwOffset = offset;
    aTracks[0]->dwStartOffset = offset;
    return true;
}

//  ********
//  NDSTrack
//  ********

NDSTrack::NDSTrack(NDSSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
    ResetVars();
    bDetermineTrackLengthEventByEvent = true;
}

void NDSTrack::ResetVars() {
    jumpCount = 0;
    loopReturnOffset = 0;
    hasLoopReturnOffset = false;
    SeqTrack::ResetVars();
}

bool NDSTrack::ReadEvent(void) {
    uint32_t beginOffset = curOffset;
    uint8_t status_byte = GetByte(curOffset++);

    if (status_byte < 0x80)  // then it's a note on event
    {
        uint8_t vel = GetByte(curOffset++);
        dur = ReadVarLen(curOffset);  // GetByte(curOffset++);
        AddNoteByDur(beginOffset, curOffset - beginOffset, status_byte, vel, dur);
        if (noteWithDelta) {
            AddTime(dur);
        }
    } else
        switch (status_byte) {
            case 0x80:
                dur = ReadVarLen(curOffset);
                AddRest(beginOffset, curOffset - beginOffset, dur);
                break;

            case 0x81: {
                uint8_t newProg = (uint8_t)ReadVarLen(curOffset);
                AddProgramChange(beginOffset, curOffset - beginOffset, newProg);
                break;
            }

            // [loveemu] open track, however should not handle in this function
            case 0x93:
                curOffset += 4;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Open Track");
                break;

            case 0x94: {
                uint32_t jumpAddr = GetByte(curOffset) + (GetByte(curOffset + 1) << 8) +
                                    (GetByte(curOffset + 2) << 16) + parentSeq->dwOffset + 0x1C;
                curOffset += 3;

                // Add an End Track if it exists afterward, for completeness sake
                if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(curOffset)) {
                    if (GetByte(curOffset) == 0xFF) {
                        AddGenericEvent(curOffset, 1, L"End of Track", L"", CLR_TRACKEND,
                                        ICON_TRACKEND);
                    }
                }

                // The event usually appears at last of the song, but there can be an exception.
                // See Zelda The Spirit Tracks - SSEQ_0018 (overworld train theme)
                bool bContinue = true;
                if (IsOffsetUsed(jumpAddr)) {
                    bContinue = AddLoopForever(beginOffset, 4, L"Loop");
                } else {
                    AddGenericEvent(beginOffset, 4, L"Jump", L"", CLR_LOOPFOREVER);
                }

                curOffset = jumpAddr;
                return bContinue;
            }

            case 0x95:
                hasLoopReturnOffset = true;
                loopReturnOffset = curOffset + 3;
                AddGenericEvent(beginOffset, curOffset + 3 - beginOffset, L"Call", L"", CLR_LOOP);
                curOffset = GetByte(curOffset) + (GetByte(curOffset + 1) << 8) +
                            (GetByte(curOffset + 2) << 16) + parentSeq->dwOffset + 0x1C;
                break;

            // [loveemu] (ex: Hanjuku Hero DS: NSE_45, New Mario Bros: BGM_AMB_CHIKA, Slime Morimori
            // Dragon Quest 2: SE_187, SE_210, Advance Wars)
            case 0xA0: {
                uint8_t subStatusByte;
                int16_t randMin;
                int16_t randMax;

                subStatusByte = GetByte(curOffset++);
                randMin = (signed)GetShort(curOffset);
                curOffset += 2;
                randMax = (signed)GetShort(curOffset);
                curOffset += 2;

                AddUnknown(beginOffset, curOffset - beginOffset, L"Cmd with Random Value");
                break;
            }

            // [loveemu] (ex: New Mario Bros: BGM_AMB_SABAKU)
            case 0xA1: {
                uint8_t subStatusByte = GetByte(curOffset++);
                uint8_t varNumber = GetByte(curOffset++);

                AddUnknown(beginOffset, curOffset - beginOffset, L"Cmd with Variable");
                break;
            }

            case 0xA2: {
                AddUnknown(beginOffset, curOffset - beginOffset, L"If");
                break;
            }

            case 0xB0:  // [loveemu] (ex: Children of Mana: SEQ_BGM001)
            case 0xB1:  // [loveemu] (ex: Advance Wars - Dual Strike: SE_TAGPT_COUNT01)
            case 0xB2:  // [loveemu]
            case 0xB3:  // [loveemu]
            case 0xB4:  // [loveemu]
            case 0xB5:  // [loveemu]
            case 0xB6:  // [loveemu] (ex: Mario Kart DS: 76th sequence)
            case 0xB8:  // [loveemu] (ex: Tottoko Hamutaro: MUS_ENDROOL, Nintendogs)
            case 0xB9:  // [loveemu]
            case 0xBA:  // [loveemu]
            case 0xBB:  // [loveemu]
            case 0xBC:  // [loveemu]
            case 0xBD: {
                uint8_t varNumber;
                int16_t val;
                const wchar_t *eventName[] = {
                    L"Set Variable",   L"Add Variable",   L"Sub Variable",  L"Mul Variable",
                    L"Div Variable",   L"Shift Vabiable", L"Rand Variable", L"",
                    L"If Variable ==", L"If Variable >=", L"If Variable >", L"If Variable <=",
                    L"If Variable <",  L"If Variable !="};

                varNumber = GetByte(curOffset++);
                val = GetShort(curOffset);
                curOffset += 2;

                AddUnknown(beginOffset, curOffset - beginOffset, eventName[status_byte - 0xB0]);
                break;
            }

            case 0xC0: {
                uint8_t pan = GetByte(curOffset++);
                AddPan(beginOffset, curOffset - beginOffset, pan);
                break;
            }

            case 0xC1:
                vol = GetByte(curOffset++);
                AddVol(beginOffset, curOffset - beginOffset, vol);
                break;

            // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_BOSS1_)
            case 0xC2: {
                uint8_t mvol = GetByte(curOffset++);
                AddUnknown(beginOffset, curOffset - beginOffset, L"Master Volume");
                break;
            }

            // [loveemu] (ex: Puyo Pop Fever 2: BGM00)
            case 0xC3: {
                int8_t transpose = (signed)GetByte(curOffset++);
                AddTranspose(beginOffset, curOffset - beginOffset, transpose);
                //			AddGenericEvent(beginOffset, curOffset-beginOffset,
                // L"Transpose", NULL, BG_CLR_GREEN);
                break;
            }

            // [loveemu] pitch bend (ex: Castlevania Dawn of Sorrow: BGM_BOSS2)
            case 0xC4: {
                int16_t bend = (signed)GetByte(curOffset++) * 64;
                AddPitchBend(beginOffset, curOffset - beginOffset, bend);
                break;
            }

            // [loveemu] pitch bend range (ex: Castlevania Dawn of Sorrow: BGM_BOSS2)
            case 0xC5: {
                uint8_t semitones = GetByte(curOffset++);
                AddPitchBendRange(beginOffset, curOffset - beginOffset, semitones);
                break;
            }

            // [loveemu] (ex: Children of Mana: SEQ_BGM000)
            case 0xC6:
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Priority", L"",
                                CLR_CHANGESTATE);
                break;

            // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
            case 0xC7: {
                uint8_t notewait = GetByte(curOffset++);
                noteWithDelta = (notewait != 0);
                AddUnknown(beginOffset, curOffset - beginOffset, L"Notewait Mode");
                break;
            }

            // [loveemu] (ex: Hanjuku Hero DS: NSE_42)
            case 0xC8:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Tie");
                break;

            // [loveemu] (ex: Hanjuku Hero DS: NSE_50)
            case 0xC9:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Portamento Control");
                break;

            // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
            case 0xCA: {
                uint8_t amount = GetByte(curOffset++);
                AddModulation(beginOffset, curOffset - beginOffset, amount, L"Modulation Depth");
                break;
            }

            // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
            case 0xCB:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Modulation Speed");
                break;

            // [loveemu] (ex: Children of Mana: SEQ_BGM001)
            case 0xCC:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Modulation Type");
                break;

            // [loveemu] (ex: Phoenix Wright - Ace Attorney: BGM021)
            case 0xCD:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Modulation Range");
                break;

            // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
            case 0xCE: {
                bool bPortOn = (GetByte(curOffset++) != 0);
                AddPortamento(beginOffset, curOffset - beginOffset, bPortOn);
                break;
            }

            // [loveemu] (ex: Bomberman: SEQ_AREA04)
            case 0xCF: {
                uint8_t portTime = GetByte(curOffset++);
                AddPortamentoTime(beginOffset, curOffset - beginOffset, portTime);
                break;
            }

            // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
            case 0xD0:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Attack Rate");
                break;

            // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
            case 0xD1:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Decay Rate");
                break;

            // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
            case 0xD2:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Sustain Level");
                break;

            // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
            case 0xD3:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Release Rate");
                break;

            // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
            case 0xD4:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Loop Start");
                break;

            case 0xD5: {
                uint8_t expression = GetByte(curOffset++);
                AddExpression(beginOffset, curOffset - beginOffset, expression);
                break;
            }

            // [loveemu]
            case 0xD6:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Print Variable");
                break;

            // [loveemu] (ex: Children of Mana: SEQ_BGM001)
            case 0xE0:
                curOffset += 2;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Modulation Delay");
                break;

            case 0xE1: {
                uint16_t bpm = GetShort(curOffset);
                curOffset += 2;
                AddTempoBPM(beginOffset, curOffset - beginOffset, bpm);
                break;
            }

            // [loveemu] (ex: Hippatte! Puzzle Bobble: SEQ_1pbgm03)
            case 0xE3:
                curOffset += 2;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Sweep Pitch");
                break;

            // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
            case 0xFC:
                AddUnknown(beginOffset, curOffset - beginOffset, L"Loop End");
                break;

            case 0xFD: {
                // This event usually does not cause an infinite loop.
                // However, a complicated sequence with a ton of conditional events, it sometimes
                // confuses the parser and causes an infinite loop. See Animal Crossing: Wild World
                // - SSEQ_270
                bool bContinue = true;
                if (!hasLoopReturnOffset || IsOffsetUsed(loopReturnOffset)) {
                    bContinue = false;
                }

                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Return", L"", CLR_LOOP);
                curOffset = loopReturnOffset;
                return bContinue;
            }

            // [loveemu] allocate track, however should not handle in this function
            case 0xFE:
                curOffset += 2;
                AddUnknown(beginOffset, curOffset - beginOffset, L"Allocate Track");
                break;

            case 0xFF:
                AddEndOfTrack(beginOffset, curOffset - beginOffset);
                return false;

            default:
                AddUnknown(beginOffset, curOffset - beginOffset);
                return false;
        }
    return true;
}
