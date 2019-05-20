/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "HeartBeatSnesSeq.h"
#include "ScaleConversion.h"

using namespace std;

DECLARE_FORMAT(HeartBeatSnes);

//  ****************
//  HeartBeatSnesSeq
//  ****************
#define MAX_TRACKS 8
#define SEQ_PPQN 24

const uint8_t HeartBeatSnesSeq::NOTE_DUR_TABLE[16] = {
    0x23, 0x46, 0x69, 0x8c, 0xaf, 0xd2, 0xf5, 0xff, 0x19, 0x28, 0x37, 0x46, 0x55, 0x64, 0x73, 0x82,
};

const uint8_t HeartBeatSnesSeq::NOTE_VEL_TABLE[16] = {
    0x19, 0x28, 0x37, 0x46, 0x55, 0x64, 0x73, 0x82, 0x91, 0xa0, 0xb0, 0xbe, 0xcd, 0xdc, 0xeb, 0xff,
};

const uint8_t HeartBeatSnesSeq::PAN_TABLE[22] = {
    0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29, 0x34, 0x42, 0x51,
    0x5e, 0x67, 0x6e, 0x73, 0x77, 0x7a, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f,
};

HeartBeatSnesSeq::HeartBeatSnesSeq(RawFile *file, HeartBeatSnesVersion ver, uint32_t seqdataOffset,
                                   std::wstring newName)
    : VGMSeq(HeartBeatSnesFormat::name, file, seqdataOffset, 0, newName), version(ver) {
    bLoadTickByTick = true;
    bAllowDiscontinuousTrackData = true;

    UseReverb();

    LoadEventMap();
}

HeartBeatSnesSeq::~HeartBeatSnesSeq(void) {}

void HeartBeatSnesSeq::ResetVars(void) {
    VGMSeq::ResetVars();
}

bool HeartBeatSnesSeq::GetHeaderInfo(void) {
    SetPPQN(SEQ_PPQN);

    VGMHeader *header = AddHeader(dwOffset, 0);
    uint32_t curOffset = dwOffset;
    if (curOffset + 2 > 0x10000) {
        return false;
    }

    header->AddSimpleItem(curOffset, 2, L"Instrument Table Pointer");
    curOffset += 2;

    for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
        uint16_t ofsTrackStart = GetShort(curOffset);
        if (ofsTrackStart == 0) {
            // example: Dragon Quest 6 - Brave Fight
            header->AddSimpleItem(curOffset, 2, L"Track Pointer End");
            curOffset += 2;
            break;
        }

        std::wstringstream trackName;
        trackName << L"Track Pointer " << (trackIndex + 1);
        header->AddSimpleItem(curOffset, 2, trackName.str().c_str());

        curOffset += 2;
    }

    return true;
}

bool HeartBeatSnesSeq::GetTrackPointers(void) {
    uint32_t curOffset = dwOffset;

    curOffset += 2;

    for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
        uint16_t ofsTrackStart = GetShort(curOffset);
        curOffset += 2;
        if (ofsTrackStart == 0) {
            break;
        }

        uint16_t addrTrackStart = dwOffset + ofsTrackStart;
        if (addrTrackStart < dwOffset) {
            return false;
        }

        HeartBeatSnesTrack *track = new HeartBeatSnesTrack(this, addrTrackStart);
        aTracks.push_back(track);
    }

    return true;
}

void HeartBeatSnesSeq::LoadEventMap() {
    int statusByte;

    EventMap[0x00] = EVENT_END;

    for (statusByte = 0x01; statusByte <= 0x7f; statusByte++) {
        EventMap[statusByte] = EVENT_NOTE_LENGTH;
    }

    for (statusByte = 0x80; statusByte <= 0xcf; statusByte++) {
        EventMap[statusByte] = EVENT_NOTE;
    }

    EventMap[0xd0] = EVENT_TIE;
    EventMap[0xd1] = EVENT_REST;
    EventMap[0xd2] = EVENT_SLUR_ON;
    EventMap[0xd3] = EVENT_SLUR_OFF;
    EventMap[0xd4] = EVENT_PROGCHANGE;
    EventMap[0xd5] = EVENT_UNKNOWN0;  // 7 bytes?
    EventMap[0xd6] = EVENT_PAN;
    EventMap[0xd7] = EVENT_PAN_FADE;
    EventMap[0xd8] = EVENT_VIBRATO_ON;
    EventMap[0xd9] = EVENT_VIBRATO_FADE;
    EventMap[0xda] = EVENT_VIBRATO_OFF;
    EventMap[0xdb] = EVENT_MASTER_VOLUME;
    EventMap[0xdc] = EVENT_MASTER_VOLUME_FADE;
    EventMap[0xdd] = EVENT_TEMPO;
    EventMap[0xde] = EVENT_UNKNOWN1;
    EventMap[0xdf] = EVENT_GLOBAL_TRANSPOSE;
    EventMap[0xe0] = EVENT_TRANSPOSE;
    EventMap[0xe1] = EVENT_TREMOLO_ON;
    EventMap[0xe2] = EVENT_TREMOLO_OFF;
    EventMap[0xe3] = EVENT_VOLUME;
    EventMap[0xe4] = EVENT_VOLUME_FADE;
    EventMap[0xe5] = EVENT_UNKNOWN3;
    EventMap[0xe6] = EVENT_PITCH_ENVELOPE_TO;
    EventMap[0xe7] = EVENT_PITCH_ENVELOPE_FROM;
    EventMap[0xe8] = EVENT_PITCH_ENVELOPE_OFF;
    EventMap[0xe9] = EVENT_TUNING;
    EventMap[0xea] = EVENT_ECHO_VOLUME;
    EventMap[0xeb] = EVENT_ECHO_PARAM;
    EventMap[0xec] = EVENT_UNKNOWN3;
    EventMap[0xed] = EVENT_ECHO_OFF;
    EventMap[0xee] = EVENT_ECHO_ON;
    EventMap[0xef] = EVENT_ECHO_FIR;
    EventMap[0xf0] = EVENT_ADSR;
    EventMap[0xf1] = EVENT_NOTE_PARAM;
    EventMap[0xf2] = EVENT_GOTO;
    EventMap[0xf3] = EVENT_CALL;
    EventMap[0xf4] = EVENT_RET;
    EventMap[0xf5] = EVENT_NOISE_ON;
    EventMap[0xf6] = EVENT_NOISE_OFF;
    EventMap[0xf7] = EVENT_NOISE_FREQ;
    EventMap[0xf8] = EVENT_UNKNOWN0;
    EventMap[0xf9] = EVENT_SUBEVENT;

    SubEventMap[0x00] = SUBEVENT_LOOP_COUNT;
    SubEventMap[0x01] = SUBEVENT_LOOP_AGAIN;
    SubEventMap[0x02] = SUBEVENT_UNKNOWN0;
    SubEventMap[0x03] = SUBEVENT_ADSR_AR;
    SubEventMap[0x04] = SUBEVENT_ADSR_DR;
    SubEventMap[0x05] = SUBEVENT_ADSR_SL;
    SubEventMap[0x06] = SUBEVENT_ADSR_RR;
    SubEventMap[0x07] = SUBEVENT_ADSR_SR;
    SubEventMap[0x09] = SUBEVENT_SURROUND;
}

double HeartBeatSnesSeq::GetTempoInBPM(uint8_t tempo) {
    if (tempo != 0) {
        return 60000000.0 / (SEQ_PPQN * (125 * 0x10)) * (tempo / 256.0);
    } else {
        return 1.0;  // since tempo 0 cannot be expressed, this function returns a very small value.
    }
}

//  ******************
//  HeartBeatSnesTrack
//  ******************

HeartBeatSnesTrack::HeartBeatSnesTrack(HeartBeatSnesSeq *parentFile, long offset, long length)
    : SeqTrack(parentFile, offset, length) {
    ResetVars();
    bDetermineTrackLengthEventByEvent = true;
    bWriteGenericEventAsTextEvent = false;
}

void HeartBeatSnesTrack::ResetVars(void) {
    SeqTrack::ResetVars();

    spcNoteDuration = 0x10;
    spcNoteDurRate = 0xaf;
    spcNoteVolume = 0xff;  // just in case
    subReturnOffset = 0;
    slur = false;
}

bool HeartBeatSnesTrack::ReadEvent(void) {
    HeartBeatSnesSeq *parentSeq = (HeartBeatSnesSeq *)this->parentSeq;

    uint32_t beginOffset = curOffset;
    if (curOffset >= 0x10000) {
        return false;
    }

    uint8_t statusByte = GetByte(curOffset++);
    bool bContinue = true;

    std::wstringstream desc;

    HeartBeatSnesSeqEventType eventType = (HeartBeatSnesSeqEventType)0;
    std::map<uint8_t, HeartBeatSnesSeqEventType>::iterator pEventType =
        parentSeq->EventMap.find(statusByte);
    if (pEventType != parentSeq->EventMap.end()) {
        eventType = pEventType->second;
    }

    switch (eventType) {
        case EVENT_UNKNOWN0:
            desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
                 << (int)statusByte;
            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
            break;

        case EVENT_UNKNOWN1: {
            uint8_t arg1 = GetByte(curOffset++);
            desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
                 << (int)statusByte << std::dec << std::setfill(L' ') << std::setw(0) << L"  Arg1: "
                 << (int)arg1;
            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
            break;
        }

        case EVENT_UNKNOWN2: {
            uint8_t arg1 = GetByte(curOffset++);
            uint8_t arg2 = GetByte(curOffset++);
            desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
                 << (int)statusByte << std::dec << std::setfill(L' ') << std::setw(0) << L"  Arg1: "
                 << (int)arg1 << L"  Arg2: " << (int)arg2;
            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
            break;
        }

        case EVENT_UNKNOWN3: {
            uint8_t arg1 = GetByte(curOffset++);
            uint8_t arg2 = GetByte(curOffset++);
            uint8_t arg3 = GetByte(curOffset++);
            desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
                 << (int)statusByte << std::dec << std::setfill(L' ') << std::setw(0) << L"  Arg1: "
                 << (int)arg1 << L"  Arg2: " << (int)arg2 << L"  Arg3: " << (int)arg3;
            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
            break;
        }

        case EVENT_END: {
            AddEndOfTrack(beginOffset, curOffset - beginOffset);
            bContinue = false;
            break;
        }

        case EVENT_NOTE_LENGTH: {
            spcNoteDuration = statusByte;
            desc << L"Length: " << (int)spcNoteDuration;

            uint8_t noteParam = GetByte(curOffset);
            if (noteParam < 0x80) {
                curOffset++;
                uint8_t durIndex = noteParam >> 4;
                uint8_t velIndex = noteParam & 0x0f;
                spcNoteDurRate = HeartBeatSnesSeq::NOTE_DUR_TABLE[durIndex];
                spcNoteVolume = HeartBeatSnesSeq::NOTE_VEL_TABLE[velIndex];
                desc << L"  Duration: " << (int)durIndex << L" (" << (int)spcNoteDurRate << L")"
                     << L"  Velocity: " << (int)velIndex << L" (" << (int)spcNoteVolume << L")";
            }

            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Note Length & Param",
                            desc.str().c_str(), CLR_CHANGESTATE);
            break;
        }

        case EVENT_NOTE: {
            uint8_t key = statusByte - 0x80;

            uint8_t dur;
            if (slur) {
                // full-length
                dur = spcNoteDuration;
            } else {
                dur = std::max((spcNoteDuration * spcNoteDurRate) >> 8, 0);
                dur = (dur > 2) ? dur - 1 : 1;
            }

            AddNoteByDur(beginOffset, curOffset - beginOffset, key, spcNoteVolume / 2, dur,
                         L"Note");
            AddTime(spcNoteDuration);
            break;
        }

        case EVENT_TIE: {
            uint8_t dur;
            if (slur) {
                // full-length
                dur = spcNoteDuration;
            } else {
                dur = std::max((spcNoteDuration * spcNoteDurRate) >> 8, 0);
                dur = (dur > 2) ? dur - 1 : 1;
            }

            desc << L"Duration: " << (int)dur;
            MakePrevDurNoteEnd(GetTime() + dur);
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", desc.str().c_str(),
                            CLR_TIE);
            AddTime(spcNoteDuration);
            break;
        }

        case EVENT_REST: {
            AddRest(beginOffset, curOffset - beginOffset, spcNoteDuration);
            break;
        }

        case EVENT_SLUR_ON: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Slur On", desc.str().c_str(),
                            CLR_PORTAMENTO, ICON_CONTROL);
            slur = true;
            break;
        }

        case EVENT_SLUR_OFF: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Slur Off", desc.str().c_str(),
                            CLR_PORTAMENTO, ICON_CONTROL);
            slur = false;
            break;
        }

        case EVENT_PROGCHANGE: {
            uint8_t newProgNum = GetByte(curOffset++);
            AddProgramChange(beginOffset, curOffset - beginOffset, newProgNum, true);
            break;
        }

        case EVENT_PAN: {
            uint8_t newPan = GetByte(curOffset++);

            uint8_t panIndex = (uint8_t)std::min((unsigned)(newPan & 0x1f), (unsigned)20);

            uint8_t volumeLeft = HeartBeatSnesSeq::PAN_TABLE[20 - panIndex];
            uint8_t volumeRight = HeartBeatSnesSeq::PAN_TABLE[panIndex];

            double linearPan = (double)volumeRight / (volumeLeft + volumeRight);
            uint8_t midiPan = ConvertLinearPercentPanValToStdMidiVal(linearPan);

            // TODO: apply volume scale
            AddPan(beginOffset, curOffset - beginOffset, midiPan);
            break;
        }

        case EVENT_PAN_FADE: {
            uint8_t fadeLength = GetByte(curOffset++);
            uint8_t newPan = GetByte(curOffset++);

            uint8_t panIndex = (uint8_t)min<uint8_t>((unsigned)(newPan & 0x1f), (unsigned)20);

            double volumeLeft = HeartBeatSnesSeq::PAN_TABLE[20 - panIndex] / 128.0;
            double volumeRight = HeartBeatSnesSeq::PAN_TABLE[panIndex] / 128.0;

            double linearPan = (double)volumeRight / (volumeLeft + volumeRight);
            uint8_t midiPan = ConvertLinearPercentPanValToStdMidiVal(linearPan);

            // TODO: fade in real curve, apply volume scale
            AddPanSlide(beginOffset, curOffset - beginOffset, fadeLength, midiPan);
            break;
        }

        case EVENT_VIBRATO_ON: {
            uint8_t vibratoDelay = GetByte(curOffset++);
            uint8_t vibratoRate = GetByte(curOffset++);
            uint8_t vibratoDepth = GetByte(curOffset++);

            desc << L"Delay: " << (int)vibratoDelay << L"  Rate: " << (int)vibratoRate
                 << L"  Depth: " << (int)vibratoDepth;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato", desc.str().c_str(),
                            CLR_MODULATION, ICON_CONTROL);
            break;
        }

        case EVENT_VIBRATO_FADE: {
            uint8_t fadeLength = GetByte(curOffset++);
            desc << L"Length: " << (int)fadeLength;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato Fade",
                            desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
            break;
        }

        case EVENT_VIBRATO_OFF: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato Off",
                            desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
            break;
        }

        case EVENT_MASTER_VOLUME: {
            uint8_t newVol = GetByte(curOffset++);
            AddMasterVol(beginOffset, curOffset - beginOffset, newVol / 2);
            break;
        }

        case EVENT_MASTER_VOLUME_FADE: {
            uint8_t fadeLength = GetByte(curOffset++);
            uint8_t newVol = GetByte(curOffset++);

            desc << L"Length: " << (int)fadeLength << L"  Volume: " << (int)newVol;
            AddMastVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol / 2);
            break;
        }

        case EVENT_TEMPO: {
            uint8_t newTempo = GetByte(curOffset++);
            AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM(newTempo));
            break;
        }

        case EVENT_GLOBAL_TRANSPOSE: {
            int8_t semitones = GetByte(curOffset++);
            AddGlobalTranspose(beginOffset, curOffset - beginOffset, semitones);
            break;
        }

        case EVENT_TRANSPOSE: {
            int8_t semitones = GetByte(curOffset++);
            AddTranspose(beginOffset, curOffset - beginOffset, semitones);
            break;
        }

        case EVENT_TREMOLO_ON: {
            uint8_t tremoloDelay = GetByte(curOffset++);
            uint8_t tremoloRate = GetByte(curOffset++);
            uint8_t tremoloDepth = GetByte(curOffset++);

            desc << L"Delay: " << (int)tremoloDelay << L"  Rate: " << (int)tremoloRate
                 << L"  Depth: " << (int)tremoloDepth;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tremolo", desc.str().c_str(),
                            CLR_MODULATION, ICON_CONTROL);
            break;
        }

        case EVENT_TREMOLO_OFF: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tremolo Off",
                            desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
            break;
        }

        case EVENT_VOLUME: {
            uint8_t newVol = GetByte(curOffset++);
            AddVol(beginOffset, curOffset - beginOffset, newVol / 2);
            break;
        }

        case EVENT_VOLUME_FADE: {
            uint8_t fadeLength = GetByte(curOffset++);
            uint8_t newVol = GetByte(curOffset++);
            AddVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol / 2);
            break;
        }

        case EVENT_PITCH_ENVELOPE_TO: {
            uint8_t pitchEnvDelay = GetByte(curOffset++);
            uint8_t pitchEnvLength = GetByte(curOffset++);
            int8_t pitchEnvSemitones = (int8_t)GetByte(curOffset++);

            desc << L"Delay: " << (int)pitchEnvDelay << L"  Length: " << (int)pitchEnvLength
                 << L"  Semitones: " << (int)pitchEnvSemitones;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Envelope (To)",
                            desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
            break;
        }

        case EVENT_PITCH_ENVELOPE_FROM: {
            uint8_t pitchEnvDelay = GetByte(curOffset++);
            uint8_t pitchEnvLength = GetByte(curOffset++);
            int8_t pitchEnvSemitones = (int8_t)GetByte(curOffset++);

            desc << L"Delay: " << (int)pitchEnvDelay << L"  Length: " << (int)pitchEnvLength
                 << L"  Semitones: " << (int)pitchEnvSemitones;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Envelope (From)",
                            desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
            break;
        }

        case EVENT_PITCH_ENVELOPE_OFF: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Envelope Off",
                            desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
            break;
        }

        case EVENT_TUNING: {
            uint8_t newTuning = GetByte(curOffset++);
            AddFineTuning(beginOffset, curOffset - beginOffset, (newTuning / 256.0) * 100.0);
            break;
        }

        case EVENT_ECHO_VOLUME: {
            uint8_t spcEVOL_L = GetByte(curOffset++);
            uint8_t spcEVOL_R = GetByte(curOffset++);
            desc << L"Volume Left: " << (int)spcEVOL_L << L"  Volume Right: " << (int)spcEVOL_R;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Volume",
                            desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
            break;
        }

        case EVENT_ECHO_PARAM: {
            uint8_t spcEDL = GetByte(curOffset++);
            uint8_t spcEFB = GetByte(curOffset++);
            uint8_t spcFIR = GetByte(curOffset++);

            desc << L"Delay: " << (int)spcEDL << L"  Feedback: " << (int)spcEFB << L"  FIR: "
                 << (int)spcFIR;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Off", desc.str().c_str(),
                            CLR_REVERB, ICON_CONTROL);
            break;
        }

        case EVENT_ECHO_OFF: {
            AddReverb(beginOffset, curOffset - beginOffset, 0, L"Echo Off");
            break;
        }

        case EVENT_ECHO_ON: {
            AddReverb(beginOffset, curOffset - beginOffset, 100, L"Echo On");
            break;
        }

        case EVENT_ECHO_FIR: {
            curOffset += 8;  // FIR C0-C7
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo FIR", desc.str().c_str(),
                            CLR_REVERB, ICON_CONTROL);
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
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR", desc.str().c_str(),
                            CLR_ADSR, ICON_CONTROL);
            break;
        }

        case EVENT_NOTE_PARAM: {
            uint8_t noteParam = GetByte(curOffset++);
            uint8_t durIndex = noteParam >> 4;
            uint8_t velIndex = noteParam & 0x0f;
            spcNoteDurRate = HeartBeatSnesSeq::NOTE_DUR_TABLE[durIndex];
            spcNoteVolume = HeartBeatSnesSeq::NOTE_VEL_TABLE[velIndex];
            desc << L"Duration: " << (int)durIndex << L" (" << (int)spcNoteDurRate << L")"
                 << L"  Velocity: " << (int)velIndex << L" (" << (int)spcNoteVolume << L")";
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Note Param", desc.str().c_str(),
                            CLR_CHANGESTATE);
            break;
        }

        case EVENT_GOTO: {
            int16_t destOffset = GetShort(curOffset);
            curOffset += 2;
            uint16_t dest = parentSeq->dwOffset + destOffset;
            desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4)
                 << std::uppercase << (int)dest;
            uint32_t length = curOffset - beginOffset;

            curOffset = dest;
            if (!IsOffsetUsed(dest)) {
                AddGenericEvent(beginOffset, length, L"Jump", desc.str().c_str(), CLR_LOOPFOREVER);
            } else {
                bContinue = AddLoopForever(beginOffset, length, L"Jump");
            }
            break;
        }

        case EVENT_CALL: {
            int16_t destOffset = GetShort(curOffset);
            curOffset += 2;
            uint16_t dest = parentSeq->dwOffset + destOffset;
            desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4)
                 << std::uppercase << (int)dest;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern Play",
                            desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

            subReturnOffset = curOffset - parentSeq->dwOffset;

            curOffset = dest;
            break;
        }

        case EVENT_RET: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern End",
                            desc.str().c_str(), CLR_LOOP, ICON_ENDREP);
            curOffset = parentSeq->dwOffset + subReturnOffset;
            break;
        }

        case EVENT_NOISE_ON: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise On", desc.str().c_str(),
                            CLR_CHANGESTATE, ICON_CONTROL);
            break;
        }

        case EVENT_NOISE_OFF: {
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise Off", desc.str().c_str(),
                            CLR_CHANGESTATE, ICON_CONTROL);
            break;
        }

        case EVENT_NOISE_FREQ: {
            uint8_t newNCK = GetByte(curOffset++) & 0x1f;
            desc << L"Noise Frequency (NCK): " << (int)newNCK;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise Frequency",
                            desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
            break;
        }

        case EVENT_SUBEVENT: {
            uint8_t subStatusByte = GetByte(curOffset++);
            HeartBeatSnesSeqSubEventType subEventType = (HeartBeatSnesSeqSubEventType)0;
            std::map<uint8_t, HeartBeatSnesSeqSubEventType>::iterator pSubEventType =
                parentSeq->SubEventMap.find(subStatusByte);
            if (pSubEventType != parentSeq->SubEventMap.end()) {
                subEventType = pSubEventType->second;
            }

            switch (subEventType) {
                case SUBEVENT_UNKNOWN0:
                    desc << L"Subevent: 0x" << std::hex << std::setfill(L'0') << std::setw(2)
                         << std::uppercase << (int)subStatusByte;
                    AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event",
                               desc.str().c_str());
                    break;

                case SUBEVENT_UNKNOWN1: {
                    uint8_t arg1 = GetByte(curOffset++);
                    desc << L"Subevent: 0x" << std::hex << std::setfill(L'0') << std::setw(2)
                         << std::uppercase << (int)subStatusByte << std::dec << std::setfill(L' ')
                         << std::setw(0) << L"  Arg1: " << (int)arg1;
                    AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event",
                               desc.str().c_str());
                    break;
                }

                case SUBEVENT_UNKNOWN2: {
                    uint8_t arg1 = GetByte(curOffset++);
                    uint8_t arg2 = GetByte(curOffset++);
                    desc << L"Subevent: 0x" << std::hex << std::setfill(L'0') << std::setw(2)
                         << std::uppercase << (int)subStatusByte << std::dec << std::setfill(L' ')
                         << std::setw(0) << L"  Arg1: " << (int)arg1 << L"  Arg2: " << (int)arg2;
                    AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event",
                               desc.str().c_str());
                    break;
                }

                case SUBEVENT_UNKNOWN3: {
                    uint8_t arg1 = GetByte(curOffset++);
                    uint8_t arg2 = GetByte(curOffset++);
                    uint8_t arg3 = GetByte(curOffset++);
                    desc << L"Subevent: 0x" << std::hex << std::setfill(L'0') << std::setw(2)
                         << std::uppercase << (int)subStatusByte << std::dec << std::setfill(L' ')
                         << std::setw(0) << L"  Arg1: " << (int)arg1 << L"  Arg2: " << (int)arg2
                         << L"  Arg3: " << (int)arg3;
                    AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event",
                               desc.str().c_str());
                    break;
                }

                case SUBEVENT_UNKNOWN4: {
                    uint8_t arg1 = GetByte(curOffset++);
                    uint8_t arg2 = GetByte(curOffset++);
                    uint8_t arg3 = GetByte(curOffset++);
                    uint8_t arg4 = GetByte(curOffset++);
                    desc << L"Subevent: 0x" << std::hex << std::setfill(L'0') << std::setw(2)
                         << std::uppercase << (int)subStatusByte << std::dec << std::setfill(L' ')
                         << std::setw(0) << L"  Arg1: " << (int)arg1 << L"  Arg2: " << (int)arg2
                         << L"  Arg3: " << (int)arg3 << L"  Arg4: " << (int)arg4;
                    AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event",
                               desc.str().c_str());
                    break;
                }

                case SUBEVENT_LOOP_COUNT: {
                    loopCount = GetByte(curOffset++);
                    desc << L"Times: " << (int)loopCount;
                    AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Count",
                                    desc.str().c_str(), CLR_LOOP, ICON_STARTREP);
                    break;
                }

                case SUBEVENT_LOOP_AGAIN: {
                    int16_t destOffset = GetShort(curOffset);
                    curOffset += 2;
                    uint16_t dest = parentSeq->dwOffset + destOffset;
                    desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4)
                         << std::uppercase << (int)dest;
                    AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Again",
                                    desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

                    if (loopCount != 0) {
                        loopCount--;
                        if (loopCount != 0) {
                            curOffset = dest;
                        }
                    }
                    break;
                }

                case SUBEVENT_ADSR_AR: {
                    uint8_t newAR = GetByte(curOffset++) & 15;
                    desc << L"AR: " << (int)newAR;
                    AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Attack Rate",
                                    desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
                    break;
                }

                case SUBEVENT_ADSR_DR: {
                    uint8_t newDR = GetByte(curOffset++) & 7;
                    desc << L"DR: " << (int)newDR;
                    AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Decay Rate",
                                    desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
                    break;
                }

                case SUBEVENT_ADSR_SL: {
                    uint8_t newSL = GetByte(curOffset++) & 7;
                    desc << L"SL: " << (int)newSL;
                    AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Sustain Level",
                                    desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
                    break;
                }

                case SUBEVENT_ADSR_RR: {
                    uint8_t newSR = GetByte(curOffset++) & 15;
                    desc << L"SR: " << (int)newSR;
                    AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Release Rate",
                                    desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
                    break;
                }

                case SUBEVENT_ADSR_SR: {
                    uint8_t newSR = GetByte(curOffset++) & 15;
                    desc << L"SR: " << (int)newSR;
                    AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Sustain Rate",
                                    desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
                    break;
                }

                case SUBEVENT_SURROUND: {
                    bool invertLeft = GetByte(curOffset++) != 0;
                    bool invertRight = GetByte(curOffset++) != 0;
                    desc << L"Invert Left: " << (invertLeft ? L"On" : L"Off") << L"  Invert Right: "
                         << (invertRight ? L"On" : L"Off");
                    AddGenericEvent(beginOffset, curOffset - beginOffset, L"Surround",
                                    desc.str().c_str(), CLR_PAN, ICON_CONTROL);
                    break;
                }

                default:
                    desc << L"Subevent: 0x" << std::hex << std::setfill(L'0') << std::setw(2)
                         << std::uppercase << (int)subStatusByte;
                    AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event",
                               desc.str().c_str());
                    L_ERROR("Unknown (sub)event {:#X}", statusByte);

                    bContinue = false;
                    break;
            }

            break;
        }

        default:
            desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
                 << (int)statusByte;
            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
            L_ERROR("Unknown event {:#X}", statusByte);

            bContinue = false;
            break;
    }

    // ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase <<
    // beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) <<
    // curOffset << std::endl; OutputDebugString(ssTrace.str().c_str());

    return bContinue;
}
