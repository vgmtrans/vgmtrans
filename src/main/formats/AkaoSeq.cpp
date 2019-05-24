/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AkaoSeq.h"
#include "AkaoInstr.h"

DECLARE_FORMAT(Akao);

using namespace std;

AkaoSeq::AkaoSeq(RawFile *file, uint32_t offset) : VGMSeq(AkaoFormat::name, file, offset) {
    UseLinearAmplitudeScale();  // I think this applies, but not certain, see FF9 320, track 3 for
                                // example of problem
    bUsesIndividualArts = false;
    UseReverb();
}

AkaoSeq::~AkaoSeq(void) {}

uint8_t AkaoSeq::GetNumPositiveBits(uint32_t ulWord) {
    return ((ulWord & 0x80000000) > 0) + ((ulWord & 0x40000000) > 0) + ((ulWord & 0x20000000) > 0) +
           ((ulWord & 0x10000000) > 0) + ((ulWord & 0x8000000) > 0) + ((ulWord & 0x4000000) > 0) +
           ((ulWord & 0x2000000) > 0) + ((ulWord & 0x1000000) > 0) + ((ulWord & 0x800000) > 0) +
           ((ulWord & 0x400000) > 0) + ((ulWord & 0x200000) > 0) + ((ulWord & 0x100000) > 0) +
           ((ulWord & 0x80000) > 0) + ((ulWord & 0x40000) > 0) + ((ulWord & 0x20000) > 0) +
           ((ulWord & 0x10000) > 0) + ((ulWord & 0x8000) > 0) + ((ulWord & 0x4000) > 0) +
           ((ulWord & 0x2000) > 0) + ((ulWord & 0x1000) > 0) + ((ulWord & 0x800) > 0) +
           ((ulWord & 0x400) > 0) + ((ulWord & 0x200) > 0) + ((ulWord & 0x100) > 0) +
           ((ulWord & 0x80) > 0) + ((ulWord & 0x40) > 0) + ((ulWord & 0x20) > 0) +
           ((ulWord & 0x10) > 0) + ((ulWord & 0x8) > 0) + ((ulWord & 0x4) > 0) +
           ((ulWord & 0x2) > 0) + ((ulWord & 0x1));
}

bool AkaoSeq::GetHeaderInfo(void) {
    // first do a version check to see if it's older or newer version of AKAO sequence format
    if (GetWord(dwOffset + 0x2C) == 0) {
        nVersion = VERSION_3;
        // assoc_ss_id = GetShort(0x14 + dwOffset);
        id = GetShort(0x14 + dwOffset);
        seq_id = GetShort(0x4 + dwOffset);
    } else if (GetWord(dwOffset + 0x1C) == 0) {
        nVersion = VERSION_2;
        return false;
    } else {
        nVersion = VERSION_1;
        return false;
    }

    name = L"Akao Seq";

    VGMHeader *hdr = AddHeader(dwOffset, 0x40);
    hdr->AddSig(dwOffset, 4);
    hdr->AddSimpleItem(dwOffset + 0x4, 2, L"ID");
    hdr->AddSimpleItem(dwOffset + 0x6, 2, L"Size");
    hdr->AddSimpleItem(dwOffset + 0x8, 1, L"Reverb Type");
    hdr->AddSimpleItem(dwOffset + 0x14, 2, L"Associated Sample Set ID");
    hdr->AddSimpleItem(dwOffset + 0x20, 4, L"Number of Tracks (# of true bits)");
    hdr->AddSimpleItem(dwOffset + 0x30, 4, L"Instrument Data Pointer");
    hdr->AddSimpleItem(dwOffset + 0x34, 4, L"Drumkit Data Pointer");

    SetPPQN(0x30);
    nNumTracks = GetNumPositiveBits(GetWord(dwOffset + 0x20));
    unLength = GetShort(dwOffset + 6);

    // There must be either a melodic instrument section, a drumkit, or both.  We determiine
    // the start of the InstrSet based on whether a melodic instrument section is given.
    uint32_t instrOff = GetWord(dwOffset + 0x30);
    uint32_t drumkitOff = GetWord(dwOffset + 0x34);
    if (instrOff != 0)
        instrOff += 0x30 + dwOffset;
    if (drumkitOff != 0)
        drumkitOff += 0x34 + dwOffset;
    uint32_t instrSetLength;
    if (instrOff != 0)
        instrSetLength = unLength - (instrOff - dwOffset);
    else
        instrSetLength = unLength - (drumkitOff - dwOffset);
    instrset =
        new AkaoInstrSet(rawfile, instrSetLength, instrOff, drumkitOff, id, L"Akao Instr Set");
    if (!instrset->LoadVGMFile()) {
        delete instrset;
        instrset = NULL;
    }

    return true;
}

bool AkaoSeq::GetTrackPointers(void) {
    for (unsigned int i = 0; i < nNumTracks; i++)
        aTracks.push_back(
            new AkaoTrack(this, GetShort(dwOffset + 0x40 + (i * 2)) + i * 2 + 0x40 + dwOffset));
    return true;
}

AkaoTrack::AkaoTrack(AkaoSeq *parentFile, long offset, long length)
    : SeqTrack(parentFile, offset, length) {}

void AkaoTrack::ResetVars(void) {
    SeqTrack::ResetVars();

    memset(loopID, 0, 8);
    memset(loop_counter, 0, 8);

    loop_begin_layer = 0;
    loop_layer = 0;
    bNotePlaying = false;

    vCondJumpAddr.clear();
}

//--------------------------------------------------
// Revisions:
//	2009. 6.17(Wed.) :	Re-make by "Sound tester 774" in "内蔵音源をMIDI変換するスレ(in
// http://www.2ch.net)" 						Add un-known
// command(op-code).
//--------------------------------------------------
bool AkaoTrack::ReadEvent(void) {
    uint32_t beginOffset = curOffset;
    uint8_t status_byte = GetByte(curOffset++);

    int i, k;

    if (status_byte < 0x9A)  // it's either a  note-on message, a tie message, or a rest message
    {
        // looking at offset 8005E5C8 in the FFO FF2 exe for this delta time table code  4294967296
        // 0x100000000/0xBA2E8BA3   = 1.374999... = 11/8 0x68 after eq = 9
        /*i = ((status_byte * 0xBA2E8BA3) & 0xFF00000000) >> 32; // emulating multu and mfhi
        instruction
        //i >>= 3;  //srl 3 */
        i = status_byte / 11;
        k = i * 2;            // sll 1 - aka multiply by 2
        k += i;               // addu $v0, $v1
        k *= 4;               // sll 2 - aka multiply by 4
        k -= i;               // subu    $v0, $v1
        k = status_byte - k;  // subu    $v0, $s1, $v0
        // k now equals the table index value
        // relative key calculation found at 8005E6F8 in FFO FF2 exe

        if (status_byte < 0x83)  // it's a note-on message
        {
            relative_key = status_byte / 11;
            base_key = octave * 12;
            if (bNotePlaying) {
                AddNoteOffNoItem(prevKey);
                bNotePlaying = false;
            }
            // if (bAssociatedWithSSTable)
            //	FindNoteInstrAssoc(hFile, base_key + relative_key);

            AddNoteOn(beginOffset, curOffset - beginOffset, base_key + relative_key, vel);
            bNotePlaying = true;

            AddTime(delta_time_table[k]);
        } else if (status_byte < 0x8F)  // if it's between 0x83 and 0x8E it is a tie event
        {
            AddTime(delta_time_table[k]);
            if (loop_counter[loop_layer] == 0 &&
                loop_layer == 0)  // do this so we don't repeat this for every single time it loops
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", L"", CLR_TIE);
        } else  // otherwise, it's between 0x8F and 0x99 and it's a rest message
        {
            if (bNotePlaying) {
                AddNoteOff(beginOffset, curOffset - beginOffset, prevKey);
                bNotePlaying = false;
            }
            AddRest(beginOffset, curOffset - beginOffset, delta_time_table[k]);
        }
    } else if ((status_byte >= 0xF0) && (status_byte <= 0xFB)) {
        relative_key = status_byte - 0xF0;
        base_key = octave * 12;
        if (bNotePlaying) {
            AddNoteOffNoItem(prevKey);
            bNotePlaying = false;
        }
        bNotePlaying = true;
        AddNoteOn(beginOffset, curOffset - beginOffset + 1, base_key + relative_key, vel);
        AddTime(GetByte(curOffset++));
    } else
        switch (status_byte) {
            case 0xA0:
                //		----[ 2009. 6.12	change ]-----
                //		 AddUnknown(beginOffset, curOffset-beginOffset);
                //		 break;
                AddEndOfTrack(beginOffset, curOffset - beginOffset);
                return false;

            // change program to articulation number
            case 0xA1: {
                ((AkaoSeq *)parentSeq)->bUsesIndividualArts = true;
                uint8_t artNum = GetByte(curOffset++);
                uint8_t progNum =
                    (uint8_t)(((AkaoSeq *)parentSeq)->instrset->aInstrs.size() + artNum);
                AddProgramChange(beginOffset, curOffset - beginOffset, progNum);
                break;
            }

            // set next note length [ticks]
            case 0xA2:
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Next note length", L"",
                                CLR_CHANGESTATE);
                break;

            // set track volume
            case 0xA3:
                vol = GetByte(curOffset++);  // expression value
                AddVol(beginOffset, curOffset - beginOffset, vol);
                break;

            // pitch slide half steps.  (Portamento)
            case 0xA4: {
                uint8_t dur = GetByte(curOffset++);  // first byte is duration of slide
                uint8_t steps =
                    GetByte(curOffset++);  // second byte is number of halfsteps to slide... not
                                           // sure if signed or not, only seen positive
                // AddPitchBendSlide(
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Portamento", L"",
                                CLR_PORTAMENTO);
                break;
            }

            // set octave
            case 0xA5:
                octave = GetByte(curOffset++);
                AddSetOctave(beginOffset, curOffset - beginOffset, octave);
                break;

            // set octave + 1
            case 0xA6:
                AddIncrementOctave(beginOffset, curOffset - beginOffset);
                break;

            // set octave - 1
            case 0xA7:
                AddDecrementOctave(beginOffset, curOffset - beginOffset);
                break;

            case 0xA8: {
                // vel = GetByte(curOffset++);
                // vel = Convert7bitPercentVolValToStdMidiVal(vel);		//I THINK THIS
                // APPLIES, BUT NOT POSITIVE AddGenericEvent(beginOffset, curOffset-beginOffset,
                // L"Set Velocity", NULL, BG_CLR_CYAN);
                uint8_t cExpression = GetByte(curOffset++);
                ////			 こっちのlog演算は要らない
                ////			 vel = Convert7bitPercentVolValToStdMidiVal(vel);
                /////I THINK THIS APPLIES, BUT NOT POSITIVE
                vel = 127;  //とりあえず 127 にしておく
                AddExpression(beginOffset, curOffset - beginOffset, cExpression);
                break;
            }

            // set Expression fade
            case 0xA9: {
                uint8_t dur = GetByte(curOffset++);
                uint8_t targExpr = GetByte(curOffset++);
                AddExpressionSlide(beginOffset, curOffset - beginOffset, dur, targExpr);
                break;
            }

            // set pan
            case 0xAA: {
                uint8_t pan = GetByte(curOffset++);
                AddPan(beginOffset, curOffset - beginOffset, pan);
                break;
            }

            // set pan fade
            case 0xAB: {
                uint8_t dur = GetByte(curOffset++);
                uint8_t targPan = GetByte(curOffset++);
                AddPanSlide(beginOffset, curOffset - beginOffset, dur, targPan);
                break;
            }

            case 0xAC:  // unknown
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset);
                break;

                /*AD = set attack AE = set decay AF = set sustain level  B0 = AE then AF
                <CBongo> B1 = set sustain release  B2 = set release
                 B7 might be set A,D,S,R values all at once
                 B3 appears to be "reset ADSR to the initial values from the instrument/sample
                data"*/

            case 0xAD:
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR", L"", CLR_ADSR);
                break;
            case 0xAE:
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR", L"", CLR_ADSR);
                break;
            case 0xAF:
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR", L"", CLR_ADSR);
                break;
            case 0xB0:
                curOffset++;
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR", L"", CLR_ADSR);
                break;
            case 0xB1:
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR", L"", CLR_ADSR);
                break;
            case 0xB2:
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR", L"", CLR_ADSR);
                break;
            case 0xB3:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR", L"", CLR_ADSR);
                break;

                //
                //	0xB4 to 0xB7	LFO Pitch bend
                //	0xB8 to 0xBC	LFO Expression
                //	0xBC to 0xBF	LFO Panpot
                //

            case 0xB4:
                curOffset += 3;
                AddGenericEvent(beginOffset, curOffset - beginOffset,
                                L"LFO(Pitch bend) Length, cycle", L"", CLR_LFO);
                break;
            case 0xB5:
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"LFO(Pitch bend) Depth", L"",
                                CLR_LFO);
                break;
            case 0xB6:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"LFO(Pitch bend) off", L"",
                                CLR_LFO);
                break;
            case 0xB7:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset);
                break;

            case 0xB8:
                curOffset += 3;
                AddGenericEvent(beginOffset, curOffset - beginOffset,
                                L"LFO(Expression) Length, cycle", L"", CLR_LFO);
                break;
            case 0xB9:
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"LFO(Expression) Depth", L"",
                                CLR_LFO);
                break;
            case 0xBA:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"LFO(Expression) off", L"",
                                CLR_LFO);
                break;
            case 0xBB:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset);
                break;

            case 0xBC:
                curOffset += 2;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"LFO(Panpot) Length, cycle",
                                L"", CLR_LFO);
                break;
            case 0xBD:
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"LFO(Panpot) Depth", L"",
                                CLR_LFO);
                break;
            case 0xBE:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"LFO(Panpot) off", L"",
                                CLR_LFO);
                break;
            case 0xBF:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset);
                break;

            case 0xC0: {
                int8_t cTranspose = GetByte(curOffset++);
                AddTranspose(beginOffset, curOffset - beginOffset, cTranspose);
                break;
            }

            case 0xC1: {
                uint8_t cTranspose = GetByte(curOffset++);
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Transpose move", L"",
                                CLR_TRANSPOSE);
                break;
            }

            case 0xC2:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Reverb On", L"",
                                CLR_REVERB);
                break;
            case 0xC3:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Reverb Off", L"",
                                CLR_REVERB);
                break;

            case 0xC4:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise On", L"",
                                CLR_UNKNOWN);
                break;
            case 0xC5:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise Off", L"",
                                CLR_UNKNOWN);
                break;

            case 0xC6:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"FM Modulation On", L"",
                                CLR_UNKNOWN);
                break;
            case 0xC7:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"FM Modulation Off", L"",
                                CLR_UNKNOWN);
                break;

            // set loop begin marker
            case 0xC8:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Repeat Start", L"",
                                CLR_LOOP);
                loop_begin_layer++;
                loop_begin_loc[loop_begin_layer] = curOffset;
                // bInLoop = true;
                break;

            case 0xC9: {
                uint8_t value1 = GetByte(curOffset++);
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Repeat End", L"", CLR_LOOP);

                // if loop_begin_layer == 0, then there was never a matching loop begin event!  this
                // is seen in ff9 402 and ff9 hunter's chance
                if (loop_begin_layer == 0)
                    break;
                if (loopID[loop_layer] != curOffset) {
                    loop_layer++;
                    loopID[loop_layer] = curOffset;
                    curOffset = loop_begin_loc[loop_begin_layer];
                    loop_counter[loop_layer] = value1 - 2;
                    bInLoop = true;
                } else if (loop_counter[loop_layer] > 0) {
                    loop_counter[loop_layer]--;
                    curOffset = loop_begin_loc[loop_begin_layer];
                    // if (loop_counter[loop_layer] == 0 && loop_layer == 1)
                    //	bInLoop = false;
                } else {
                    loopID[loop_layer] = 0;
                    loop_layer--;
                    loop_begin_layer--;
                    if (loop_layer == 0)
                        bInLoop = false;
                }
                break;
            }

            case 0xCA: {
                uint8_t value1 = 2;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Repeat End", L"", CLR_LOOP);
                if (loop_begin_layer == 0)
                    break;
                if (loopID[loop_layer] != curOffset) {
                    loop_layer++;
                    loopID[loop_layer] = curOffset;
                    curOffset = loop_begin_loc[loop_begin_layer];
                    loop_counter[loop_layer] = value1 - 2;
                    bInLoop = true;
                } else if (loop_counter[loop_layer] > 0) {
                    loop_counter[loop_layer]--;
                    curOffset = loop_begin_loc[loop_begin_layer];
                    // if (loop_counter[loop_layer] == 0 && loop_layer == 1)
                    //	bInLoop = false;
                } else {
                    loopID[loop_layer] = 0;
                    loop_layer--;
                    loop_begin_layer--;
                    if (loop_layer == 0)
                        bInLoop = false;
                }
                break;
            }

            case 0xCC:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Slur On", L"",
                                CLR_PORTAMENTO);
                break;

            case 0xCD:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Slur Off", L"",
                                CLR_PORTAMENTO);
                break;

            case 0xD0:
                AddNoteOff(beginOffset, curOffset - beginOffset, prevKey);
                break;

            case 0xD1:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Deactivate Notes?", L"",
                                CLR_UNKNOWN);
                break;

            case 0xD2:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset);
                break;

            case 0xD3:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset);
                break;

            // pitch bend
            case 0xD8: {
                // signed data byte.  range of 1 octave (0x7F = +1 octave 0x80 = -1 octave)
                uint8_t cValue = GetByte(curOffset++);
                int fullValue = (int)(cValue * 64.503937007874015748031496062992);
                fullValue += 0x2000;
                uint8_t lo = fullValue & 0x7F;
                uint8_t hi = (fullValue & 0x3F80) >> 7;
                AddPitchBendMidiFormat(beginOffset, curOffset - beginOffset, lo, hi);
                break;
            }

            case 0xD9:
                curOffset++;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch bend move", L"",
                                CLR_UNKNOWN);
                break;

            case 0xDA:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset);
                break;

            case 0xDC:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset);
                break;

            case 0xDD:
                curOffset += 2;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"LFO(Pitch bend) times", L"",
                                CLR_UNKNOWN);
                break;

            case 0xDE:
                curOffset += 2;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"LFO(Expression) times", L"",
                                CLR_UNKNOWN);
                break;

            case 0xDF:
                curOffset += 2;
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"LFO(Panpot) times", L"",
                                CLR_UNKNOWN);
                break;

            case 0xE1:
                curOffset++;
                AddUnknown(beginOffset, curOffset - beginOffset);
                break;

            case 0xE6:
                curOffset += 2;
                AddUnknown(beginOffset, curOffset - beginOffset);
                break;

            // tie time
            case 0xFC:
                // if (nVersion == VERSION_1 || nVersion == VERSION_2)
                //	 goto MetaEvent;
                // rest_time += pDoc->GetByte(j++);
                AddTime(GetByte(curOffset++));
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie (custom)", L"",
                                CLR_TIE);
                break;

            case 0xFD: {
                if (bNotePlaying) {
                    AddNoteOffNoItem(prevKey);
                    bNotePlaying = false;
                }
                uint8_t restTime = GetByte(curOffset++);
                AddRest(beginOffset, curOffset - beginOffset, restTime);
                break;
            }

            // meta event
            case 0xFE:
                // MetaEvent:
                switch (GetByte(curOffset++)) {
                    // tempo
                    case 0x00: {
                        uint8_t value1 = GetByte(curOffset++);
                        uint8_t value2 = GetByte(curOffset++);
                        // no clue how this magic number is properly derived
                        double dValue1 = ((value2 << 8) + value1) / 218.4555555555555555555555555;
                        AddTempoBPM(beginOffset, curOffset - beginOffset, dValue1);
                        break;
                    }

                    // tempo slide
                    case 0x01: {
                        uint8_t value1 = GetByte(curOffset++);  // NEED TO ADDRESS value 1
                        uint8_t value2 = GetByte(curOffset++);
                        // AddTempoSlide(
                        // RecordMidiSetTempo(current_delta_time, value2);
                        break;
                    }

                    case 0x02:
                        curOffset++;
                        curOffset++;
                        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Reverb Level", L"",
                                        CLR_REVERB);
                        break;

                    case 0x03:
                        curOffset++;
                        curOffset++;
                        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Reverb Fade", L"",
                                        CLR_REVERB);
                        break;

                    // signals this track uses the drum kit
                    case 0x04:
                        AddProgramChange(beginOffset, curOffset - beginOffset, 127, L"Drum kit On");
                        // channel = 9;
                        break;

                    case 0x05:
                        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Drum kit Off", L"",
                                        CLR_UNKNOWN);
                        break;

                    // Branch Relative
                    case 0x06: {
                        uint32_t dest = (int16_t)GetShort(curOffset) + curOffset;
                        curOffset += 2;
                        uint32_t eventLength = curOffset - beginOffset;
                        bool bContinue = false;

                        curOffset = dest;

                        // Check the remaining area that will be processed by CPU-controlled jump.
                        for (vector<uint32_t>::iterator itAddr = vCondJumpAddr.begin();
                             itAddr != vCondJumpAddr.end(); ++itAddr) {
                            if (!IsOffsetUsed(*itAddr)) {
                                // There is an area not visited yet, VGMTrans must try to go there.
                                bContinue = true;
                                break;
                            }
                        }

                        if (readMode == READMODE_FIND_DELTA_LENGTH)
                            deltaLength = GetTime();

                        AddGenericEvent(beginOffset, eventLength, L"Dal Segno.(Loop)", L"",
                                        CLR_LOOP);
                        return bContinue;

                        /*uint16_t siValue = GetShort(pDoc, j);
                        if (nScanMode == MODE_CONVERT_MIDI)		//if we are converting the
                        midi, the actually branch
                        {
                            if (seq_repeat_counter-- > 0)
                                curOffset += siValue;
                            else
                                curOffset += 2;
                        }
                        else
                            curOffset += 2;								//otherwise,
                        just skip over the relative branch offset*/
                        break;
                    }

                    // Permanence Loop break with conditional.
                    case 0x07: {
                        uint8_t condValue = GetByte(curOffset++);
                        uint32_t dest = (int16_t)GetShort(curOffset) + curOffset;
                        curOffset += 2;
                        uint32_t eventLength = curOffset - beginOffset;

                        // This event performs conditional jump if certain CPU variable matches to
                        // the condValue. VGMTrans will simply try to parse all events as far as
                        // possible, instead. (Test case: FF9 416 Final Battle)
                        if (!IsOffsetUsed(beginOffset)) {
                            // For the first time, VGMTrans just skips the event,
                            // but remembers the destination address for future jump.
                            vCondJumpAddr.push_back(dest);
                        } else {
                            // For the second time, VGMTrans jumps to the destination address.
                            curOffset = dest;
                        }

                        AddGenericEvent(beginOffset, eventLength, L"Dal Segno (Loop) Break", L"",
                                        CLR_LOOP);
                        break;
                    }

                    // Repeat break with conditional.
                    case 0x09:
                        curOffset++;
                        curOffset++;
                        curOffset++;
                        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Repeat Break", L"",
                                        CLR_LOOP);
                        break;

                        // case 0x0E :			//call subroutine
                        // case 0x0F :			//return from subroutine

                    case 0x10:
                        curOffset++;
                        AddUnknown(beginOffset, curOffset - beginOffset);
                        break;

                    // program change
                    case 0x14: {
                        uint8_t curProgram = GetByte(curOffset++);
                        // if (!bAssociatedWithSSTable)
                        //	RecordMidiProgramChange(current_delta_time, curProgram, hFile);
                        AddProgramChange(beginOffset, curOffset - beginOffset, curProgram);
                        break;
                    }

                    // Time Signature
                    case 0x15: {
                        uint8_t denom = 4;
                        curOffset++;  //(192 / GetByte(curOffset++));
                        uint8_t numer = GetByte(curOffset++);
                        // AddTimeSig(beginOffset, curOffset-beginOffset, numer, denom,
                        // parentSeq->GetPPQN()); // why it's disabled?
                        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Time Signature",
                                        L"", CLR_TIMESIG, ICON_TIMESIG);
                        break;
                    }

                    case 0x16:
                        curOffset += 2;
                        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Maker", L"",
                                        CLR_UNKNOWN);
                        break;

                    case 0x1C:
                        curOffset++;
                        AddUnknown(beginOffset, curOffset - beginOffset);
                        break;

                    default:
                        AddGenericEvent(beginOffset, curOffset - beginOffset, L"UNKNOWN META EVENT",
                                        L"", CLR_UNRECOGNIZED);
                        break;
                }
                break;

            default:
                AddGenericEvent(beginOffset, curOffset - beginOffset, L"UNKNOWN EVENT", L"",
                                CLR_UNRECOGNIZED);
                break;
        }
    return true;
}
