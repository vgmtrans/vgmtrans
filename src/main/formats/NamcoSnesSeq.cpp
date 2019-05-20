/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NamcoSnesSeq.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(NamcoSnes);

//  ************
//  NamcoSnesSeq
//  ************
#define MAX_TRACKS 8
#define SEQ_PPQN 48

NamcoSnesSeq::NamcoSnesSeq(RawFile *file, NamcoSnesVersion ver, uint32_t seqdataOffset,
                           std::wstring newName)
    : VGMSeqNoTrks(NamcoSnesFormat::name, file, seqdataOffset, newName), version(ver) {
    bAllowDiscontinuousTrackData = true;
    bUseLinearAmplitudeScale = true;

    AlwaysWriteInitialTempo(60000000.0 / (SEQ_PPQN * (125 * 0x86)));

    UseReverb();

    LoadEventMap();
}

NamcoSnesSeq::~NamcoSnesSeq(void) {}

void NamcoSnesSeq::ResetVars(void) {
    VGMSeqNoTrks::ResetVars();

    vel = 100;
    spcDeltaTime = 1;
    spcDeltaTimeScale = 1;
    subReturnAddress = 0;
    loopCount = 0;
    loopCountAlt = 0;

    for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
        prevNoteKey[trackIndex] = -1;
        prevNoteType[trackIndex] = NOTE_MELODY;
        instrNum[trackIndex] = -1;
    }
}

bool NamcoSnesSeq::GetHeaderInfo(void) {
    SetPPQN(SEQ_PPQN);
    nNumTracks = MAX_TRACKS;

    SetEventsOffset(VGMSeq::dwOffset);
    return true;
}

void NamcoSnesSeq::LoadEventMap() {
    int statusByte;

    NOTE_NUMBER_REST = 0x54;
    NOTE_NUMBER_NOISE_MIN = 0x55;
    NOTE_NUMBER_PERCUSSION_MIN = 0x80;

    EventMap[0x00] = EVENT_DELTA_TIME;
    EventMap[0x01] = EVENT_OPEN_TRACKS;
    EventMap[0x02] = EVENT_CALL;
    EventMap[0x03] = EVENT_END;
    EventMap[0x04] = EVENT_DELTA_MULTIPLIER;
    EventMap[0x05] = EVENT_MASTER_VOLUME;
    EventMap[0x06] = EVENT_LOOP_AGAIN;
    EventMap[0x07] = EVENT_LOOP_BREAK;
    EventMap[0x08] = EVENT_GOTO;
    EventMap[0x09] = EVENT_NOTE;
    EventMap[0x0a] = EVENT_ECHO_DELAY;
    EventMap[0x0b] = EVENT_UNKNOWN_0B;
    EventMap[0x0c] = EVENT_UNKNOWN1;
    EventMap[0x0d] = EVENT_ECHO;
    EventMap[0x0e] = EVENT_WAIT;
    EventMap[0x0f] = EVENT_LOOP_AGAIN_ALT;
    EventMap[0x10] = EVENT_LOOP_BREAK_ALT;
    EventMap[0x11] = EVENT_ECHO_FEEDBACK;
    EventMap[0x12] = EVENT_ECHO_FIR;
    EventMap[0x13] = EVENT_ECHO_VOLUME;
    EventMap[0x14] = EVENT_ECHO_ADDRESS;
    // EventMap[0x15] = (NamcoSnesSeqEventType)0;
    // EventMap[0x16] = (NamcoSnesSeqEventType)0;
    // EventMap[0x17] = (NamcoSnesSeqEventType)0;

    // note: vcmd 2b-ff are not used
    for (statusByte = 0x18; statusByte <= 0xff; statusByte++) {
        EventMap[statusByte] = EVENT_CONTROL_CHANGES;
    }

    ControlChangeMap[0x00] = CONTROL_PROGCHANGE;
    ControlChangeMap[0x01] = CONTROL_VOLUME;
    ControlChangeMap[0x02] = CONTROL_PAN;
    // ControlChangeMap[0x03] = (NamcoSnesSeqControlType)0;
    // ControlChangeMap[0x04] = (NamcoSnesSeqControlType)0;
    // ControlChangeMap[0x05] = (NamcoSnesSeqControlType)0;
    // ControlChangeMap[0x06] = (NamcoSnesSeqControlType)0;
    // ControlChangeMap[0x07] = (NamcoSnesSeqControlType)0;
    // ControlChangeMap[0x08] = (NamcoSnesSeqControlType)0;
    // ControlChangeMap[0x09] = (NamcoSnesSeqControlType)0;
    ControlChangeMap[0x0a] = CONTROL_ADSR;
    // ControlChangeMap[0x0b] = (NamcoSnesSeqControlType)0;
    // ControlChangeMap[0x0c] = (NamcoSnesSeqControlType)0;
    // ControlChangeMap[0x0d] = (NamcoSnesSeqControlType)0;
    // ControlChangeMap[0x0e] = (NamcoSnesSeqControlType)0;
    // ControlChangeMap[0x0f] = (NamcoSnesSeqControlType)0;

    ControlChangeNames[CONTROL_PROGCHANGE] = L"Program Change";
    ControlChangeNames[CONTROL_VOLUME] = L"Volume";
    ControlChangeNames[CONTROL_PAN] = L"Pan";
    ControlChangeNames[CONTROL_ADSR] = L"ADSR";
}

bool NamcoSnesSeq::ReadEvent(void) {
    uint32_t beginOffset = curOffset;
    if (curOffset >= 0x10000) {
        return false;
    }

    uint8_t statusByte = GetByte(curOffset++);
    bool bContinue = true;

    std::wstringstream desc;

    NamcoSnesSeqEventType eventType = (NamcoSnesSeqEventType)0;
    std::map<uint8_t, NamcoSnesSeqEventType>::iterator pEventType = EventMap.find(statusByte);
    if (pEventType != EventMap.end()) {
        eventType = pEventType->second;
    }

    // default first track
    channel = 0;
    SetCurTrack(channel);

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

        case EVENT_DELTA_TIME: {
            spcDeltaTime = GetByte(curOffset++);
            desc << L"Duration: " << spcDeltaTime;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Delta Time", desc.str(),
                            CLR_CHANGESTATE);
            break;
        }

        case EVENT_OPEN_TRACKS: {
            uint8_t targetChannels = GetByte(curOffset++);

            desc << L"Tracks:";
            for (uint8_t trackIndex = 0; trackIndex < 8; trackIndex++) {
                if ((targetChannels & (0x80 >> trackIndex)) != 0) {
                    desc << L" " << (trackIndex + 1);
                }
            }

            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Open Tracks", desc.str(),
                            CLR_MISC, ICON_TRACK);
            break;
        }

        case EVENT_CALL: {
            uint16_t dest = GetShort(curOffset);
            curOffset += 2;
            desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4)
                 << std::uppercase << (int)dest;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern Play",
                            desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

            subReturnAddress = curOffset;
            curOffset = dest;
            break;
        }

        case EVENT_END: {
            if ((subReturnAddress & 0xff00) == 0) {
                // end of track
                AddEndOfTrack(beginOffset, curOffset - beginOffset);
                bContinue = false;
            } else {
                // end of subroutine
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern End",
                                desc.str().c_str(), CLR_LOOP, ICON_ENDREP);
                curOffset = subReturnAddress;
                subReturnAddress = 0;
            }
            break;
        }

        case EVENT_DELTA_MULTIPLIER: {
            spcDeltaTimeScale = GetByte(curOffset++);
            desc << L"Delta Time Scale: " << spcDeltaTimeScale;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Delta Time Multiplier",
                            desc.str(), CLR_MISC, ICON_TEMPO);
            break;
        }

        case EVENT_MASTER_VOLUME: {
            uint8_t newVol = GetByte(curOffset++);
            AddMasterVol(beginOffset, curOffset - beginOffset, newVol / 2);
            break;
        }

        case EVENT_LOOP_AGAIN: {
            uint8_t count = GetByte(curOffset++);
            uint16_t dest = GetShort(curOffset);
            curOffset += 2;
            desc << L"Times: " << count << L"  Destination: $" << std::hex << std::setfill(L'0')
                 << std::setw(4) << std::uppercase << (int)dest;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Again", desc.str().c_str(),
                            CLR_LOOP, ICON_ENDREP);

            loopCount++;
            if (loopCount == count) {
                // repeat end
                loopCount = 0;
            } else {
                // repeat again
                curOffset = dest;
            }

            break;
        }

        case EVENT_LOOP_BREAK: {
            uint8_t count = GetByte(curOffset++);
            uint16_t dest = GetShort(curOffset);
            curOffset += 2;
            desc << L"Times: " << count << L"  Destination: $" << std::hex << std::setfill(L'0')
                 << std::setw(4) << std::uppercase << (int)dest;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Break", desc.str().c_str(),
                            CLR_LOOP, ICON_ENDREP);

            loopCount++;
            if (loopCount == count) {
                // repeat break
                curOffset = dest;
                loopCount = 0;
            }

            break;
        }

        case EVENT_LOOP_AGAIN_ALT: {
            uint8_t count = GetByte(curOffset++);
            uint16_t dest = GetShort(curOffset);
            curOffset += 2;
            desc << L"Times: " << count << L"  Destination: $" << std::hex << std::setfill(L'0')
                 << std::setw(4) << std::uppercase << (int)dest;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Again (Alt)",
                            desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

            loopCountAlt++;
            if (loopCountAlt == count) {
                // repeat end
                loopCountAlt = 0;
            } else {
                // repeat again
                curOffset = dest;
            }

            break;
        }

        case EVENT_LOOP_BREAK_ALT: {
            uint8_t count = GetByte(curOffset++);
            uint16_t dest = GetShort(curOffset);
            curOffset += 2;
            desc << L"Times: " << count << L"  Destination: $" << std::hex << std::setfill(L'0')
                 << std::setw(4) << std::uppercase << (int)dest;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Break", desc.str().c_str(),
                            CLR_LOOP, ICON_ENDREP);

            loopCountAlt++;
            if (loopCountAlt == count) {
                // repeat break
                curOffset = dest;
                loopCountAlt = 0;
            }

            break;
        }

        case EVENT_GOTO: {
            uint16_t dest = GetShort(curOffset);
            curOffset += 2;
            desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4)
                 << std::uppercase << (int)dest;
            uint32_t length = curOffset - beginOffset;

            // scan end event
            if (curOffset + 1 <= 0x10000 && GetByte(curOffset) == 0x03 &&
                (subReturnAddress & 0xff00) == 0) {
                AddGenericEvent(curOffset, 1, L"End of Track", L"", CLR_TRACKEND, ICON_TRACKEND);
            }

            curOffset = dest;
            if (!IsOffsetUsed(dest)) {
                AddGenericEvent(beginOffset, length, L"Jump", desc.str().c_str(), CLR_LOOPFOREVER);
            } else {
                bContinue = AddLoopForever(beginOffset, length, L"Jump");
            }
            break;
        }

        case EVENT_NOTE: {
            uint8_t targetChannels = GetByte(curOffset++);
            uint16_t dur = spcDeltaTime * spcDeltaTimeScale;

            desc << L"Values:";
            for (uint8_t trackIndex = 0; trackIndex < 8; trackIndex++) {
                if ((targetChannels & (0x80 >> trackIndex)) != 0) {
                    uint8_t keyByte = GetByte(curOffset++);

                    int8_t key;
                    NamcoSnesSeqNoteType noteType;
                    if (keyByte >= NOTE_NUMBER_PERCUSSION_MIN) {
                        key = keyByte - NOTE_NUMBER_PERCUSSION_MIN;
                        noteType = NOTE_PERCUSSION;
                        desc << L" [" << (trackIndex + 1) << L"] " << L"Perc " << key;
                    } else if (keyByte >= NOTE_NUMBER_NOISE_MIN) {
                        key = keyByte & 0x1f;
                        noteType = NOTE_NOISE;
                        desc << L" [" << (trackIndex + 1) << L"] " << L"Noise " << key;
                    } else if (keyByte == NOTE_NUMBER_REST) {
                        noteType = prevNoteType[trackIndex];
                        desc << L" [" << (trackIndex + 1) << L"] " << L"Rest";
                    } else {
                        key = keyByte;
                        noteType = NOTE_MELODY;
                        desc << L" [" << (trackIndex + 1) << L"] " << key << L" ("
                             << MidiEvent::GetNoteName(key + transpose) << L")";
                    }

                    if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
                        channel = trackIndex;
                        SetCurTrack(channel);

                        // key off previous note
                        if (prevNoteKey[trackIndex] != -1) {
                            AddNoteOffNoItem(prevNoteKey[trackIndex]);
                            prevNoteKey[trackIndex] = -1;
                        }

                        // program changes for noise/percussion
                        if (noteType != prevNoteType[trackIndex]) {
                            switch (noteType) {
                                case NOTE_MELODY:
                                    AddProgramChangeNoItem(instrNum[trackIndex], false);
                                    break;

                                case NOTE_NOISE:
                                    AddProgramChangeNoItem(126, false);
                                    break;

                                case NOTE_PERCUSSION:
                                    AddProgramChangeNoItem(127, false);

                                    // actual music engine applies per-instrument pan
                                    AddPanNoItem(64);
                                    break;
                            }

                            noteType = prevNoteType[trackIndex];
                        }

                        if (keyByte != NOTE_NUMBER_REST) {
                            prevNoteKey[trackIndex] = key;
                            AddNoteOnNoItem(key, vel);
                        }
                    }
                }
            }

            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Note", desc.str(), CLR_DURNOTE,
                            ICON_NOTE);
            AddTime(dur);
            break;
        }

        case EVENT_ECHO_DELAY: {
            uint8_t echoDelay = GetByte(curOffset++);
            desc << L"Echo Delay: " << echoDelay;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Delay", desc.str(),
                            CLR_REVERB, ICON_CONTROL);
            break;
        }

        case EVENT_UNKNOWN_0B: {
            uint8_t targetChannels = GetByte(curOffset++);

            desc << L"Values:";
            for (uint8_t trackIndex = 0; trackIndex < 8; trackIndex++) {
                if ((targetChannels & (0x80 >> trackIndex)) != 0) {
                    uint8_t newValue = GetByte(curOffset++);
                    desc << L" [" << (trackIndex + 1) << L"] " << newValue;
                }
            }

            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str());
            break;
        }

        case EVENT_ECHO: {
            bool echoOn = (GetByte(curOffset++) != 0);
            desc << L"Echo Write: " << (echoOn ? L"On" : L"Off");
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo", desc.str(), CLR_REVERB,
                            ICON_CONTROL);
            break;
        }

        case EVENT_WAIT: {
            uint16_t dur = spcDeltaTime * spcDeltaTimeScale;
            desc << L"Delta Time: " << spcDeltaTime;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Wait", desc.str(), CLR_TIE);
            AddTime(dur);
            break;
        }

        case EVENT_ECHO_FEEDBACK: {
            int8_t echoFeedback = GetByte(curOffset++);
            desc << L"Echo Feedback: " << echoFeedback;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Feedback", desc.str(),
                            CLR_REVERB, ICON_CONTROL);
            break;
        }

        case EVENT_ECHO_FIR: {
            uint8_t echoFilter = GetByte(curOffset++);
            desc << L"Echo FIR: " << echoFilter;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo FIR", desc.str(),
                            CLR_REVERB, ICON_CONTROL);
            break;
        }

        case EVENT_ECHO_VOLUME: {
            int8_t echoVolumeLeft = GetByte(curOffset++);
            int8_t echoVolumeRight = GetByte(curOffset++);
            desc << L"Echo Volume Left: " << echoVolumeLeft << L"  Echo Volume Right: "
                 << echoVolumeRight;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Volume", desc.str(),
                            CLR_REVERB, ICON_CONTROL);
            break;
        }

        case EVENT_ECHO_ADDRESS: {
            uint16_t echoAddress = GetByte(curOffset++) << 8;
            desc << L"ESA: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase
                 << (int)echoAddress;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Address", desc.str(),
                            CLR_REVERB, ICON_CONTROL);
            break;
        }

        case EVENT_CONTROL_CHANGES: {
            uint8_t controlTypeIndex = statusByte & 0x0f;
            uint8_t targetChannels = GetByte(curOffset++);

            NamcoSnesSeqControlType controlType = (NamcoSnesSeqControlType)0;
            std::map<uint8_t, NamcoSnesSeqControlType>::iterator pControlType =
                ControlChangeMap.find(controlTypeIndex);
            std::wstring controlName = L"Unknown Event";
            if (pControlType != ControlChangeMap.end()) {
                controlType = pControlType->second;
                controlName = ControlChangeNames[controlType];
            }

            desc << L"Values:";
            for (uint8_t trackIndex = 0; trackIndex < 8; trackIndex++) {
                if ((targetChannels & (0x80 >> trackIndex)) != 0) {
                    uint8_t newValue = GetByte(curOffset++);
                    desc << L" [" << (trackIndex + 1) << L"] " << newValue;

                    if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
                        channel = trackIndex;
                        SetCurTrack(channel);

                        switch (controlType) {
                            case CONTROL_PROGCHANGE:
                                AddProgramChangeNoItem(newValue, false);
                                instrNum[trackIndex] = newValue;
                                break;

                            case CONTROL_VOLUME:
                                AddVolNoItem(newValue / 2);
                                break;

                            case CONTROL_PAN: {
                                uint8_t volumeLeft = newValue & 0xf0;
                                uint8_t volumeRight = (newValue & 0x0f) << 4;

                                // TODO: apply volume scale
                                double linearPan = (double)volumeRight / (volumeLeft + volumeRight);
                                uint8_t midiPan = ConvertLinearPercentPanValToStdMidiVal(linearPan);

                                AddPanNoItem(midiPan);
                                break;
                            }
                        }
                    }
                }
            }

            switch (controlType) {
                case CONTROL_PROGCHANGE:
                    AddGenericEvent(beginOffset, curOffset - beginOffset, controlName, desc.str(),
                                    CLR_PROGCHANGE, ICON_PROGCHANGE);
                    break;

                case CONTROL_VOLUME:
                    AddGenericEvent(beginOffset, curOffset - beginOffset, controlName, desc.str(),
                                    CLR_VOLUME, ICON_CONTROL);
                    break;

                case CONTROL_PAN:
                    AddGenericEvent(beginOffset, curOffset - beginOffset, controlName, desc.str(),
                                    CLR_PAN, ICON_CONTROL);
                    break;

                case CONTROL_ADSR:
                    AddGenericEvent(beginOffset, curOffset - beginOffset, controlName, desc.str(),
                                    CLR_ADSR, ICON_CONTROL);
                    break;

                default:
                    AddUnknown(beginOffset, curOffset - beginOffset, controlName, desc.str());
                    break;
            }

            break;
        }

        default:
            desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
                 << (int)statusByte;
            AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str());
            L_ERROR("Unknown event {:#x}", statusByte);

            bContinue = false;
            break;
    }

    ///
    // ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase <<
    // beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) <<
    // curOffset << std::endl; OutputDebugString(ssTrace.str().c_str());

    return bContinue;
}

bool NamcoSnesSeq::PostLoad(void) {
    bool succeeded = VGMSeqNoTrks::PostLoad();

    if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
        KeyOffAllNotes();
    }

    return succeeded;
}

void NamcoSnesSeq::KeyOffAllNotes(void) {
    for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
        if (prevNoteKey[trackIndex] != -1) {
            channel = trackIndex;
            SetCurTrack(channel);

            AddNoteOffNoItem(prevNoteKey[trackIndex]);
            prevNoteKey[trackIndex] = -1;
        }
    }
}
