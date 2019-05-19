/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ChunSnesSeq.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(ChunSnes);

//  ***********
//  ChunSnesSeq
//  ***********
#define MAX_TRACKS 8
#define SEQ_PPQN 48
#define SEQ_KEYOFS 24

ChunSnesSeq::ChunSnesSeq(RawFile *file, ChunSnesVersion ver, ChunSnesMinorVersion minorVer,
                         uint32_t seqdataOffset, std::wstring newName)
    : VGMSeq(ChunSnesFormat::name, file, seqdataOffset, 0, newName),
      version(ver),
      minorVersion(minorVer),
      initialTempo(0) {
    bLoadTickByTick = true;
    bAllowDiscontinuousTrackData = true;
    bUseLinearAmplitudeScale = true;

    UseReverb();
    AlwaysWriteInitialReverb(0);

    LoadEventMap();
}

ChunSnesSeq::~ChunSnesSeq(void) {}

void ChunSnesSeq::ResetVars(void) {
    VGMSeq::ResetVars();

    conditionVar = 0;
}

bool ChunSnesSeq::GetHeaderInfo(void) {
    SetPPQN(SEQ_PPQN);

    VGMHeader *header = AddHeader(dwOffset, 0);
    uint32_t curOffset = dwOffset;
    if (curOffset + 2 > 0x10000) {
        return false;
    }

    header->AddTempo(curOffset, 1);
    initialTempo = GetByte(curOffset++);
    AlwaysWriteInitialTempo(GetTempoInBPM(initialTempo));

    header->AddSimpleItem(curOffset, 1, L"Number of Tracks");
    nNumTracks = GetByte(curOffset++);
    if (nNumTracks == 0 || nNumTracks > MAX_TRACKS) {
        return false;
    }

    for (uint8_t trackIndex = 0; trackIndex < nNumTracks; trackIndex++) {
        uint16_t ofsTrackStart = GetShort(curOffset);

        uint16_t addrTrackStart;
        if (version == CHUNSNES_SUMMER) {
            addrTrackStart = ofsTrackStart;
        } else {
            addrTrackStart = dwOffset + ofsTrackStart;
        }

        std::wstringstream trackName;
        trackName << L"Track Pointer " << (trackIndex + 1);
        header->AddSimpleItem(curOffset, 2, trackName.str());

        ChunSnesTrack *track = new ChunSnesTrack(this, addrTrackStart);
        track->index = aTracks.size();
        aTracks.push_back(track);

        curOffset += 2;
    }

    return true;
}

bool ChunSnesSeq::GetTrackPointers(void) {
    // already done by GetHeaderInfo
    return true;
}

void ChunSnesSeq::LoadEventMap() {
    int statusByte;

    for (statusByte = 0x00; statusByte <= 0x9f; statusByte++) {
        EventMap[statusByte] = EVENT_NOTE;
    }

    if (version == CHUNSNES_SUMMER) {
        for (statusByte = 0xa0; statusByte <= 0xdc; statusByte++) {
            EventMap[statusByte] = EVENT_NOP;
        }
    } else {
        for (statusByte = 0xa0; statusByte <= 0xb5; statusByte++) {
            EventMap[statusByte] = EVENT_DURATION_FROM_TABLE;
        }

        // appears in Bridal March at $7493
        for (statusByte = 0xb6; statusByte <= 0xda; statusByte++) {
            EventMap[statusByte] = EVENT_NOP;
        }
    }

    EventMap[0xdd] = EVENT_ADSR_RELEASE_SR;
    EventMap[0xde] = EVENT_ADSR_AND_RELEASE_SR;
    EventMap[0xdf] = EVENT_SURROUND;
    EventMap[0xe0] = EVENT_CONDITIONAL_JUMP;
    EventMap[0xe1] = EVENT_INC_COUNTER;
    EventMap[0xe2] = EVENT_PITCH_ENVELOPE;
    EventMap[0xe3] = EVENT_NOISE_ON;
    EventMap[0xe4] = EVENT_NOISE_OFF;
    EventMap[0xe5] = EVENT_MASTER_VOLUME_FADE;
    EventMap[0xe6] = EVENT_EXPRESSION_FADE;
    EventMap[0xe7] = EVENT_FULL_VOLUME_FADE;
    EventMap[0xe8] = EVENT_PAN_FADE;
    EventMap[0xe9] = EVENT_TUNING;
    EventMap[0xea] = EVENT_GOTO;
    EventMap[0xeb] = EVENT_TEMPO;
    EventMap[0xec] = EVENT_DURATION_RATE;
    EventMap[0xed] = EVENT_VOLUME;
    EventMap[0xee] = EVENT_PAN;
    EventMap[0xef] = EVENT_ADSR;
    EventMap[0xf0] = EVENT_PROGCHANGE;
    EventMap[0xf1] = EVENT_UNKNOWN1;
    EventMap[0xf2] = EVENT_SYNC_NOTE_LEN_ON;
    EventMap[0xf3] = EVENT_SYNC_NOTE_LEN_OFF;
    EventMap[0xf4] = EVENT_LOOP_AGAIN;
    EventMap[0xf5] = EVENT_LOOP_UNTIL;
    EventMap[0xf6] = EVENT_EXPRESSION;
    EventMap[0xf7] = EVENT_UNKNOWN1;
    EventMap[0xf8] = EVENT_CALL;
    EventMap[0xf9] = EVENT_RET;
    EventMap[0xfa] = EVENT_TRANSPOSE;
    EventMap[0xfb] = EVENT_PITCH_SLIDE;
    EventMap[0xfc] = EVENT_ECHO_ON;
    EventMap[0xfd] = EVENT_ECHO_OFF;
    EventMap[0xfe] = EVENT_LOAD_PRESET;
    EventMap[0xff] = EVENT_END;

    if (version != CHUNSNES_SUMMER) {
        EventMap[0xdb] = EVENT_LOOP_BREAK_ALT;
        EventMap[0xdc] = EVENT_LOOP_AGAIN_ALT;
        EventMap[0xf1] = EVENT_SYNC_NOTE_LEN_ON;
        EventMap[0xf7] = EVENT_NOP;
    }

    if (version == CHUNSNES_SUMMER) {
        PresetMap[0x00] = PRESET_CONDITION;
        PresetMap[0x01] = PRESET_CONDITION;
        PresetMap[0x02] = PRESET_CONDITION;
        PresetMap[0x03] = PRESET_CONDITION;
        PresetMap[0x04] = PRESET_CONDITION;
        PresetMap[0x05] = PRESET_CONDITION;
    } else {
        PresetMap[0x00] = PRESET_CONDITION;
        PresetMap[0x01] = PRESET_CONDITION;
        PresetMap[0x02] = PRESET_CONDITION;
        PresetMap[0x03] = PRESET_CONDITION;
        PresetMap[0x04] = PRESET_CONDITION;
        PresetMap[0x05] = PRESET_CONDITION;
        PresetMap[0x06] = PRESET_CONDITION;
        PresetMap[0x07] = PRESET_CONDITION;
        PresetMap[0x08] = PRESET_CONDITION;
        PresetMap[0x09] = PRESET_CONDITION;
        PresetMap[0x0a] = PRESET_CONDITION;
        PresetMap[0x0b] = PRESET_CONDITION;
        PresetMap[0x0c] = PRESET_CONDITION;
        PresetMap[0x0d] = PRESET_CONDITION;
        PresetMap[0x0e] = PRESET_CONDITION;
        PresetMap[0x0f] = PRESET_CONDITION;
        PresetMap[0x10] = PRESET_CONDITION;
        PresetMap[0x11] = PRESET_CONDITION;
        PresetMap[0x12] = PRESET_CONDITION;
        PresetMap[0x13] = PRESET_CONDITION;
        PresetMap[0x14] = PRESET_CONDITION;
    }
}

double ChunSnesSeq::GetTempoInBPM(uint8_t tempo) {
    if (tempo != 0) {
        return (double)tempo;
    } else {
        return 1.0;  // since tempo 0 cannot be expressed, this function returns a very small value.
    }
}

//  *************
//  ChunSnesTrack
//  *************

ChunSnesTrack::ChunSnesTrack(ChunSnesSeq *parentFile, long offset, long length)
    : SeqTrack(parentFile, offset, length) {
    ResetVars();
    bDetermineTrackLengthEventByEvent = true;
    bWriteGenericEventAsTextEvent = false;
}

void ChunSnesTrack::ResetVars(void) {
    SeqTrack::ResetVars();

    cKeyCorrection = SEQ_KEYOFS;

    vel = 100;
    prevNoteKey = -1;
    prevNoteSlurred = false;
    noteLength = 1;
    noteDurationRate = 0xcc;
    syncNoteLen = false;
    loopCount = 0;
    loopCountAlt = 0;
    subNestLevel = 0;
}

bool ChunSnesTrack::ReadEvent(void) {
    ChunSnesSeq *parentSeq = (ChunSnesSeq *)this->parentSeq;

    uint32_t beginOffset = curOffset;
    if (curOffset >= 0x10000) {
        return false;
    }

    if (syncNoteLen) {
        SyncNoteLengthWithPriorTrack();
    }

    uint8_t statusByte = GetByte(curOffset++);
    bool bContinue = true;

    std::wstringstream desc;

    ChunSnesSeqEventType eventType = (ChunSnesSeqEventType)0;
    std::map<uint8_t, ChunSnesSeqEventType>::iterator pEventType =
        parentSeq->EventMap.find(statusByte);
    if (pEventType != parentSeq->EventMap.end()) {
        eventType = pEventType->second;
    }

    switch (eventType) {
        case EVENT_UNKNOWN0:
            desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
                 << (int)statusByte;
            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str());
            break;

        case EVENT_UNKNOWN1: {
            uint8_t arg1 = GetByte(curOffset++);
            desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
                 << (int)statusByte << std::dec << std::setfill(L' ') << std::setw(0) << L"  Arg1: "
                 << (int)arg1;
            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str());
            break;
        }

        case EVENT_UNKNOWN2: {
            uint8_t arg1 = GetByte(curOffset++);
            uint8_t arg2 = GetByte(curOffset++);
            desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
                 << (int)statusByte << std::dec << std::setfill(L' ') << std::setw(0) << L"  Arg1: "
                 << (int)arg1 << L"  Arg2: " << (int)arg2;
            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str());
            break;
        }

        case EVENT_UNKNOWN3: {
            uint8_t arg1 = GetByte(curOffset++);
            uint8_t arg2 = GetByte(curOffset++);
            uint8_t arg3 = GetByte(curOffset++);
            desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
                 << (int)statusByte << std::dec << std::setfill(L' ') << std::setw(0) << L"  Arg1: "
                 << (int)arg1 << L"  Arg2: " << (int)arg2 << L"  Arg3: " << (int)arg3;
            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str());
            break;
        }

        case EVENT_UNKNOWN4: {
            uint8_t arg1 = GetByte(curOffset++);
            uint8_t arg2 = GetByte(curOffset++);
            uint8_t arg3 = GetByte(curOffset++);
            uint8_t arg4 = GetByte(curOffset++);
            desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
                 << (int)statusByte << std::dec << std::setfill(L' ') << std::setw(0) << L"  Arg1: "
                 << (int)arg1 << L"  Arg2: " << (int)arg2 << L"  Arg3: " << (int)arg3 << L"  Arg4: "
                 << (int)arg4;
            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str());
            break;
        }

        case EVENT_NOP: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"NOP", desc.str(), CLR_MISC,
                            ICON_BINARY);
            break;
        }

        case EVENT_NOTE:  // 00..9f
        {
            uint8_t noteIndex = statusByte;
            if (statusByte >= 0x50) {
                noteLength = GetByte(curOffset++);
                noteIndex -= 0x50;
            }

            bool rest = (noteIndex == 0x00);
            bool tie = (noteIndex == 0x4f);
            uint8_t key = noteIndex - 1;

            // formula for duration is:
            //   dur = len * (durRate + 1) / 256 (approx)
            // but there are a few of exceptions.
            //   durRate = 0   : full length (tie uses it)
            //   durRate = 254 : full length - 1 (tick)
            bool slur = (noteDurationRate == 0);
            uint8_t dur;
            if (slur) {
                // slur (cancel note off)
                // the note will be combined to the next one
                dur = noteLength;
            } else if (noteDurationRate == 254) {
                dur = noteLength - 1;
            } else {
                dur = Multiply8bit(noteLength, noteDurationRate);
            }

            if (rest) {
                // it actually does not operate note off, in the real music engine,
                // in other words, this event will continue slurred note in full length.
                // that's probably an unexpected behavior, since we have $4f for such operation.
                // so we simply stops the existing note. [nobody cares]
                AddRest(beginOffset, curOffset - beginOffset, noteLength);
            } else if (tie) {
                // update note duration without changing note pitch
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", desc.str(), CLR_TIE,
                                ICON_NOTE);
                MakePrevDurNoteEnd(GetTime() + dur);
                AddTime(noteLength);
            } else {
                if (prevNoteSlurred && key == prevNoteKey) {
                    // slurred note with same key works as tie
                    MakePrevDurNoteEnd(GetTime() + dur);
                    desc << L"Abs Key: " << key << " (" << MidiEvent::GetNoteName(key) << ") "
                         << L"  Duration: " << dur;
                    AddGenericEvent(beginOffset, curOffset - beginOffset, L"Note with Duration",
                                    desc.str(), CLR_TIE, ICON_NOTE);
                } else {
                    AddNoteByDur(beginOffset, curOffset - beginOffset, key, vel, dur);
                }
                AddTime(noteLength);
            }
            prevNoteSlurred = slur;

            break;
        }

        case EVENT_DURATION_FROM_TABLE:  // a0..b5
        {
            const uint8_t NOTE_DUR_TABLE[] = {0x0d, 0x1a, 0x26, 0x33, 0x40, 0x4d, 0x5a, 0x66,
                                              0x73, 0x80, 0x8c, 0x99, 0xa6, 0xb3, 0xbf, 0xcc,
                                              0xd9, 0xe6, 0xf2, 0xfe, 0xff, 0x00};

            uint8_t durIndex = statusByte - 0xa0;
            noteDurationRate = NOTE_DUR_TABLE[durIndex];
            if (noteDurationRate == 0) {
                desc << L"Duration Rate: Slur (Full)";
            } else if (noteDurationRate == 254) {
                desc << L"Duration Rate: Full - 1";
            } else {
                desc << L"Duration Rate: " << ((int)noteDurationRate + 1) << L"/256";
            }
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Duration Rate from Table",
                            desc.str(), CLR_DURNOTE);
            break;
        }

        case EVENT_LOOP_BREAK_ALT: {
            int16_t destOffset = GetShort(curOffset);
            curOffset += 2;
            uint16_t dest = curOffset + destOffset;
            desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4)
                 << std::uppercase << (int)dest;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Break (Alt)", desc.str(),
                            CLR_LOOP, ICON_ENDREP);

            if (loopCountAlt != 0) {
                curOffset = dest;
            }

            break;
        }

        case EVENT_LOOP_AGAIN_ALT: {
            int16_t destOffset = GetShort(curOffset);
            curOffset += 2;
            uint16_t dest = curOffset + destOffset;
            desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4)
                 << std::uppercase << (int)dest;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Again (Alt)", desc.str(),
                            CLR_LOOP, ICON_ENDREP);

            if (loopCountAlt == 0) {
                loopCountAlt = 2;
            }

            loopCountAlt--;
            if (loopCountAlt != 0) {
                curOffset = dest;
            }

            break;
        }

        case EVENT_ADSR_RELEASE_SR: {
            uint8_t release_sr = GetByte(curOffset++) & 31;
            desc << L"SR (Release): " << (int)release_sr;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Release Rate", desc.str(),
                            CLR_ADSR, ICON_CONTROL);
            break;
        }

        case EVENT_ADSR_AND_RELEASE_SR: {
            uint8_t adsr1 = GetByte(curOffset++);
            uint8_t adsr2 = GetByte(curOffset++);
            uint8_t release_sr = GetByte(curOffset++) & 31;

            uint8_t ar = adsr1 & 0x0f;
            uint8_t dr = (adsr1 & 0x70) >> 4;
            uint8_t sl = (adsr2 & 0xe0) >> 5;
            uint8_t sr = adsr2 & 0x1f;

            desc << L"AR: " << (int)ar << L"  DR: " << (int)dr << L"  SL: " << (int)sl << L"  SR: "
                 << (int)sr << L"  SR (Release): " << (int)release_sr;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR & Release Rate",
                            desc.str(), CLR_ADSR, ICON_CONTROL);
            break;
        }

        case EVENT_SURROUND: {
            uint8_t param = GetByte(curOffset++);
            bool invertLeft = (param & 1) != 0;
            bool invertRight = (param & 2) != 0;
            desc << L"Invert Left: " << (invertLeft ? L"On" : L"Off") << L"  Invert Right: "
                 << (invertRight ? L"On" : L"Off");
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Surround", desc.str(), CLR_PAN,
                            ICON_CONTROL);
            break;
        }

        case EVENT_CONDITIONAL_JUMP: {
            int16_t destOffset = GetShort(curOffset);
            curOffset += 2;
            uint8_t condValue = GetByte(curOffset++);
            uint16_t dest = curOffset + destOffset;
            desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4)
                 << std::uppercase << (int)dest;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Conditional Jump", desc.str(),
                            CLR_MISC);

            if ((parentSeq->conditionVar & 0x7f) == condValue) {
                // repeat again
                curOffset = dest;
            } else {
                // repeat end
                parentSeq->conditionVar |= 0x80;
            }

            break;
        }

        case EVENT_INC_COUNTER: {
            // increment a counter value, which will be sent to main CPU
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Increment Counter", desc.str(),
                            CLR_MISC);
            break;
        }

        case EVENT_PITCH_ENVELOPE: {
            uint8_t envelopeIndex = GetByte(curOffset++);
            if (envelopeIndex == 0xff) {
                desc << L"Envelope: Off";
            } else {
                desc << L"Envelope: " << envelopeIndex;
            }
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Envelope", desc.str(),
                            CLR_LFO, ICON_CONTROL);
            break;
        }

        case EVENT_NOISE_ON: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise On", desc.str(),
                            CLR_PROGCHANGE, ICON_PROGCHANGE);
            break;
        }

        case EVENT_NOISE_OFF: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise Off", desc.str(),
                            CLR_PROGCHANGE, ICON_PROGCHANGE);
            break;
        }

        case EVENT_MASTER_VOLUME_FADE: {
            uint8_t mastVol = GetByte(curOffset++);
            uint8_t fadeLength = GetByte(curOffset++);
            desc << L"Master Volume: " << (int)mastVol << L"  Fade Length: " << (int)fadeLength;

            uint8_t midiMastVol = std::min(mastVol, (uint8_t)0x7f);
            AddMastVolSlide(beginOffset, curOffset - beginOffset, fadeLength, midiMastVol);
            break;
        }

        case EVENT_EXPRESSION_FADE: {
            uint8_t vol = GetByte(curOffset++);
            uint8_t fadeLength = GetByte(curOffset++);
            desc << L"Expression: " << (int)vol << L"  Fade Length: " << (int)fadeLength;

            AddExpressionSlide(beginOffset, curOffset - beginOffset, fadeLength, vol >> 1);
            break;
        }

        case EVENT_FULL_VOLUME_FADE: {
            // fade channel volume to zero or full, do not know where it is used
            uint8_t arg1 = GetByte(curOffset++);
            desc << L"Arg1: " << arg1;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Fade", desc.str(), CLR_VOLUME,
                            ICON_CONTROL);
            break;
        }

        case EVENT_PAN_FADE: {
            int8_t pan = GetByte(curOffset++);
            uint8_t fadeLength = GetByte(curOffset++);
            desc << L"Pan: " << (int)pan << L"  Fade Length: " << (int)fadeLength;

            // TODO: slide in real curve, apply volume scale
            double volumeScale;
            int8_t midiPan = CalcPanValue(pan, volumeScale);
            AddPanSlide(beginOffset, curOffset - beginOffset, fadeLength, midiPan);
            break;
        }

        case EVENT_TUNING: {
            // it can be overwriten by pitch envelope (vibrato)
            int8_t newTuning = GetByte(curOffset++);
            double cents = CalcTuningValue(newTuning);
            AddFineTuning(beginOffset, curOffset - beginOffset, cents);
            break;
        }

        case EVENT_GOTO: {
            int16_t destOffset = GetShort(curOffset);
            curOffset += 2;
            uint16_t dest = curOffset + destOffset;
            desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4)
                 << std::uppercase << (int)dest;
            uint32_t length = curOffset - beginOffset;

            curOffset = dest;
            if (!IsOffsetUsed(dest)) {
                AddGenericEvent(beginOffset, length, L"Jump", desc.str(), CLR_LOOPFOREVER);
            } else {
                bContinue = AddLoopForever(beginOffset, length, L"Jump");
            }
            break;
        }

        case EVENT_TEMPO: {
            uint8_t tempoValue = GetByte(curOffset++);

            uint8_t newTempo = tempoValue;
            if (parentSeq->minorVersion == CHUNSNES_WINTER_V3) {
                newTempo = parentSeq->initialTempo * tempoValue / 64;
            }

            AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM(newTempo));
            break;
        }

        case EVENT_DURATION_RATE: {
            noteDurationRate = GetByte(curOffset++);
            if (noteDurationRate == 0) {
                desc << L"Duration Rate: Tie/Slur";
            } else if (noteDurationRate == 254) {
                desc << L"Duration Rate: Full - 1";
            } else {
                desc << L"Duration Rate: " << ((int)noteDurationRate + 1) << L"/256";
            }
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Duration Rate", desc.str(),
                            CLR_DURNOTE);
            break;
        }

        case EVENT_VOLUME: {
            uint8_t vol = GetByte(curOffset++);
            AddVol(beginOffset, curOffset - beginOffset, vol >> 1);
            break;
        }

        case EVENT_PAN: {
            int8_t pan = GetByte(curOffset++);

            // TODO: apply volume scale
            double volumeScale;
            int8_t midiPan = CalcPanValue(pan, volumeScale);
            AddPan(beginOffset, curOffset - beginOffset, midiPan);
            break;
        }

        case EVENT_ADSR: {
            uint8_t adsr1 = GetByte(curOffset++);
            uint8_t adsr2 = GetByte(curOffset++);

            uint8_t ar = adsr1 & 0x0f;
            uint8_t dr = (adsr1 & 0x70) >> 4;
            uint8_t sl = (adsr2 & 0xe0) >> 5;
            uint8_t sr = adsr2 & 0x1f;

            desc << L"AR: " << (int)ar << L"  DR: " << (int)dr << L"  SL: " << (int)sl << L"  SR: "
                 << (int)sr;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR", desc.str(), CLR_ADSR,
                            ICON_CONTROL);
            break;
        }

        case EVENT_PROGCHANGE: {
            uint8_t newProg = GetByte(curOffset++);
            AddProgramChange(beginOffset, curOffset - beginOffset, newProg);
            break;
        }

        case EVENT_SYNC_NOTE_LEN_ON: {
            syncNoteLen = true;

            // refresh duration info promptly
            SyncNoteLengthWithPriorTrack();

            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Sync Note Length On",
                            desc.str(), CLR_DURNOTE);
            break;
        }

        case EVENT_SYNC_NOTE_LEN_OFF: {
            syncNoteLen = false;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Sync Note Length Off",
                            desc.str(), CLR_DURNOTE);
            break;
        }

        case EVENT_LOOP_AGAIN: {
            int16_t destOffset = GetShort(curOffset);
            curOffset += 2;
            uint16_t dest = curOffset + destOffset;
            desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4)
                 << std::uppercase << (int)dest;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Again", desc.str(),
                            CLR_LOOP, ICON_ENDREP);

            if (loopCount == 0) {
                loopCount = 2;
            }

            loopCount--;
            if (loopCount != 0) {
                curOffset = dest;
            }

            break;
        }

        case EVENT_LOOP_UNTIL: {
            uint8_t times = GetByte(curOffset++);
            int16_t destOffset = GetShort(curOffset);
            curOffset += 2;
            uint16_t dest = curOffset + destOffset;
            desc << L"Times: " << (int)times << L"  Destination: $" << std::hex
                 << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Until", desc.str(),
                            CLR_LOOP, ICON_ENDREP);

            if (loopCount == 0) {
                loopCount = times;
            }

            loopCount--;
            if (loopCount != 0) {
                curOffset = dest;
            }

            break;
        }

        case EVENT_EXPRESSION: {
            uint8_t vol = GetByte(curOffset++);
            AddExpression(beginOffset, curOffset - beginOffset, vol >> 1);
            break;
        }

        case EVENT_CALL: {
            int16_t destOffset = GetShort(curOffset);
            curOffset += 2;
            uint16_t dest = curOffset + destOffset;
            desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4)
                 << std::uppercase << (int)dest;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern Play", desc.str(),
                            CLR_LOOP, ICON_STARTREP);

            if (subNestLevel >= CHUNSNES_SUBLEVEL_MAX) {
                // stack overflow
                bContinue = false;
                break;
            }

            subReturnAddr[subNestLevel] = curOffset;
            subNestLevel++;

            curOffset = dest;
            break;
        }

        case EVENT_RET: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern End", desc.str(),
                            CLR_LOOP, ICON_ENDREP);

            if (subNestLevel > 0) {
                curOffset = subReturnAddr[subNestLevel - 1];
                subNestLevel--;
            }

            break;
        }

        case EVENT_TRANSPOSE: {
            int8_t newTranspose = GetByte(curOffset++);
            AddTranspose(beginOffset, curOffset - beginOffset, newTranspose);
            break;
        }

        case EVENT_PITCH_SLIDE: {
            int8_t semitones = GetByte(curOffset++);
            uint8_t length = GetByte(curOffset++);
            desc << L"Key: " << (semitones > 0 ? L"+" : L"") << (int)semitones << L" semitones"
                 << L"  Length: " << (int)length;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Slide", desc.str(),
                            CLR_PITCHBEND, ICON_CONTROL);
            break;
        }

        case EVENT_ECHO_ON: {
            AddReverb(beginOffset, curOffset - beginOffset, 40, L"Echo On");
            break;
        }

        case EVENT_ECHO_OFF: {
            AddReverb(beginOffset, curOffset - beginOffset, 0, L"Echo Off");
            break;
        }

        case EVENT_LOAD_PRESET: {
            uint8_t presetIndex = GetByte(curOffset++);

            ChunSnesSeqPresetType presetType = (ChunSnesSeqPresetType)0;
            std::map<uint8_t, ChunSnesSeqPresetType>::iterator pPresetType =
                parentSeq->PresetMap.find(presetIndex);
            if (pPresetType != parentSeq->PresetMap.end()) {
                presetType = pPresetType->second;
            }

            // this event dispatches predefined short sequence,
            // then changes various voice parameters (echo, master volume, etc.)
            // here we dispatch only a part of them
            switch (presetType) {
                case PRESET_CONDITION:
                    desc << L"Value: " << presetIndex;
                    parentSeq->conditionVar =
                        presetIndex;  // luckily those preset starts from preset 0 :)
                    AddGenericEvent(beginOffset, curOffset - beginOffset, L"Set Condition Value",
                                    desc.str(), CLR_CHANGESTATE);
                    break;

                default:
                    desc << L"Preset: " << presetIndex;
                    AddGenericEvent(beginOffset, curOffset - beginOffset, L"Load Preset",
                                    desc.str(), CLR_MISC);
                    break;
            }

            break;
        }

        case EVENT_END: {
            if (subNestLevel > 0) {
                // return from subroutine (normally not used)
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"End of Track", desc.str(),
                                CLR_TRACKEND, ICON_TRACKEND);
                curOffset = subReturnAddr[subNestLevel - 1];
                subNestLevel--;
            } else {
                // end of track
                AddEndOfTrack(beginOffset, curOffset - beginOffset);
                bContinue = false;
            }
            break;
        }

        default:
            desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
                 << (int)statusByte;
            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
            pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()).c_str(),
                                          LOG_LEVEL_ERR, L"ChunSnesSeq"));
            bContinue = false;
            break;
    }

    ///
    // ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase <<
    // beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) <<
    // curOffset << std::endl; OutputDebugString(ssTrace.str().c_str());

    return bContinue;
}

void ChunSnesTrack::SyncNoteLengthWithPriorTrack() {
    if (index != 0 && index < parentSeq->aTracks.size()) {
        ChunSnesTrack *priorTrack = (ChunSnesTrack *)parentSeq->aTracks[index - 1];
        noteLength = priorTrack->noteLength;
        noteDurationRate = priorTrack->noteDurationRate;
    }
}

uint8_t ChunSnesTrack::Multiply8bit(uint8_t multiplicand, uint8_t multiplier) {
    // approx: multiplicand * (multiplier + 1) / 256
    if (multiplier == 255) {
        return multiplicand;
    } else {
        if (multiplier >= 128) {
            multiplier++;
        }

        uint16_t result = multiplicand * multiplier;
        result += 0x80;  // +0.5 for rounding
        return result >> 8;
    }
}

void ChunSnesTrack::GetVolumeBalance(int8_t pan, double &volumeLeft, double &volumeRight) {
    ChunSnesSeq *parentSeq = (ChunSnesSeq *)this->parentSeq;

    if (pan == 0) {
        volumeLeft = 1.0;
        volumeRight = 1.0;
    } else {
        uint8_t volumeRateByte = 255 - (std::min((int8_t)abs(pan), (int8_t)127) * 2 + 1);

        // approx (volumeRateByte + 1) / 256
        double volumeRate;
        if (volumeRateByte == 255) {
            volumeRate = 1.0;
        } else {
            if (volumeRateByte >= 128) {
                volumeRateByte++;
            }
            volumeRate = volumeRateByte / 256.0;
        }

        if (pan > 0) {
            // pan left (decrease right volume)
            volumeLeft = volumeRate;
            volumeRight = 1.0;
        } else {
            // pan right (decrease left volume)
            volumeLeft = 1.0;
            volumeRight = volumeRate;
        }
    }
}

int8_t ChunSnesTrack::CalcPanValue(int8_t pan, double &volumeScale) {
    ChunSnesSeq *parentSeq = (ChunSnesSeq *)this->parentSeq;

    double volumeLeft;
    double volumeRight;
    GetVolumeBalance(pan, volumeLeft, volumeRight);

    uint8_t midiPan = ConvertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight);

    // TODO: convert volume scale to (0.0..1.0)
    double volumeLeftMidi;
    double volumeRightMidi;
    ConvertStdMidiPanToVolumeBalance(midiPan, volumeLeftMidi, volumeRightMidi);
    volumeScale = (volumeLeft + volumeRight) / (volumeLeftMidi + volumeRightMidi);

    return midiPan;
}

double ChunSnesTrack::CalcTuningValue(int8_t tuning) {
    uint8_t absTuning = abs(tuning);
    int8_t sign = (tuning >= 0) ? 1 : -1;

    if (absTuning == 0x7f) {
        return sign * 100.0;
    } else {
        absTuning <<= 1;
        if (absTuning >= 0x80) {
            absTuning++;  // +0.5 if abs(tuning) >= 0x40
        }
        return sign * (absTuning / 256.0) * 100.0;
    }
}
