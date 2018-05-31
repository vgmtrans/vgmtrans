#include "pch.h"
#include "NamcoS2Seq.h"
#include "ScaleConversion.h"


bool NamcoS2Seq::ReadC140Event(SectionState& state) {
  uint32_t beginOffset = state.curOffset;
  curOffset = beginOffset;

  if (beginOffset < VGMSeq::dwOffset) {
    VGMSeq::dwOffset = beginOffset;
  }

  std::wstringstream desc;
  bool bContinue = true;

  uint8_t statusByte = GetByte(curOffset++);

  if (state.startingTrack == 8) {
//    printf("\nsection 1. time: %d  %d", VGMSeq::time, state.time);
  }

  if (statusByte > 0x7F) {
//    AddRest(beginOffset, 1, (statusByte & 0x7F) + state.delta * state.deltaMultiplier);
    AddTime(((uint32_t)(statusByte & 0x7F)+1) * state.delta);
    AddGenericEvent(beginOffset, 1, L"Delta", desc.str(), CLR_REST);
    state.curOffset = curOffset;
    return true;
  }

  // First load num data bytes / variable data bytes value
  // if it's variadic, jump to variadic logic
  // else read the first data byte and jump to the opcode sub
  // in both cases, continue reading events except on a rest or set key event

  // variadic opcode logic:
  // read first data byte. this byte is a bitfield of channels to execute the opcode for
  // if the opcode byte >= 0x40, read the next data byte. It will be used across all channels in the bitfield
  // if the opcode byte <= 0x3F, read as many data bytes as there are channels to update in bitfield, then execute opcode

  bool isChannelOpcode = false;
  switch (statusByte & 0x3F) {
    case 0x06 ... 0x08:
    case 0x0C ... 0x18:
    case 0x1B:
    case 0x1F:
    case 0x22:
      isChannelOpcode = true;
  }

  // Channel opcodes are variadic in the sense that a single event can cover a variable number of channels.

  if (isChannelOpcode) {

    uint8_t targetChannels = GetByte(curOffset++);
    uint8_t dataByte;
    if (statusByte >= 0x40)
      dataByte = GetByte(curOffset++);

    for (uint8_t c=0; c<8; c++) {

      if ((targetChannels & (0x80 >> c)) == 0)
        continue;
      if (statusByte <= 0x3F)
        dataByte = GetByte(curOffset++);

      const uint32_t trackNum = c + state.startingTrack;
      channel = trackNum % 16;
      channelGroup = trackNum / 16;
      SetCurTrack(trackNum);

      auto& chanState = state.chanStates[c];

      switch (statusByte & 0x3F) {

        case S2C140_OPC_06_NOTE: {
  //
  //          if (trackNum == 8) {
  //            printf("\n%d  key: %X", VGMSeq::time, dataByte);
  //          }

          if (dataByte == 0xFF) {
            AddNoteOffNoItem(chanState.prevKey);
//            chanState.hold = 0;
            break;
          }

          uint8_t keyToPlay = dataByte + 9 + chanState.transpose;

//          if (chanState.noteIsPlaying) {
            AddNoteOffNoItem(chanState.prevKey);
//            chanState.noteIsPlaying = false;
//          }

          AddC140ProgramChangeIfNeeded(chanState);

//          if (chanState.hold > 0) {
//            AddNoteByDurNoItem(keyToPlay, 100, chanState.hold);
//          }
//          else {
            AddNoteOnNoItem(keyToPlay, 100);
//            chanState.noteIsPlaying = true;
//          }


//          AddNoteOffNoItem(chanState.prevKey);

//          AddC140ProgramChangeIfNeeded(chanState);

//          AddNoteOnNoItem(keyToPlay, 100);
          chanState.prevKey = keyToPlay;
//          AddNoteByDurNoItem(dataByte, 100, chanState.duration);
          break;
        }

        case S2C140_OPC_07_PROG_CHANGE: {
          chanState.samp = dataByte;
//          AddBankSelectNoItem(chanState.bank);
//          AddProgramChangeNoItem(dataByte, false);

//          AddGenericEvent(beginOffset, curOffset - beginOffset, L"Program Change", desc.str(), CLR_CHANGESTATE);
          break;
        }

        case S2C140_OPC_08_VOLUME: {
          chanState.volume = dataByte / 2;
          AddVolNoItem(chanState.volume);
          break;
        }

        case S2C140_OPC_0C_PITCHBEND_HI: {
//          AddPitchBendRangeNoItem()
//          AddPitchBendNoItem(dataByte << 8);
          chanState.transpose = dataByte < 128 ? dataByte : dataByte - 256;
          break;
        }

        case S2C140_OPC_0D_PITCHBEND_LO: {
//          AddPitchBendNoItem((dataByte / 2));
//          transpose = dataByte;
//          AddVolNoItem(chanState.volume);
          break;
        }

        case S2C140_OPC_11_DURATION: {
//          chanState.duration = dataByte;
          chanState.hold = dataByte;
          break;
        }

        case S2C140_OPC_13_ARTICULATION: {
          chanState.artic = dataByte;
//          AddBankSelectNoItem(dataByte);
//          AddProgramChangeNoItem(chanState.samp, false);
          break;
        }

        case S2C140_OPC_16_PAN: {
          AddPanNoItem(dataByte / 2);
          break;
        }

        case S2C140_OPC_0E ... S2C140_OPC_10:
        case S2C140_OPC_12:
        case S2C140_OPC_14 ... S2C140_OPC_15_NOP:
        case S2C140_OPC_17 ... S2C140_OPC_18_NOP:
        case S2C140_OPC_1B:
        case S2C140_OPC_1F:
        case S2C140_OPC_22:
//          AddUnknown(beginOffset, curOffset-beginOffset);
          break;
      }
    }
    switch (statusByte & 0x3F) {
      case S2C140_OPC_06_NOTE:
        if (dataByte == 0xFF)
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"Note Off", desc.str(), CLR_NOTEOFF);
        else
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"Note On", desc.str(), CLR_NOTEON);
        AddTime(state.delta);
        break;

      case S2C140_OPC_07_PROG_CHANGE:
        //TODO: Add Note Off for ym2151
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Program Change", desc.str(), CLR_PROGCHANGE);
        break;

      case S2C140_OPC_08_VOLUME:
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Volume", desc.str(), CLR_VOLUME);
        break;

      case S2C140_OPC_0C_PITCHBEND_HI:
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitchbend High", desc.str(), CLR_PITCHBEND);
        break;

      case S2C140_OPC_0D_PITCHBEND_LO:
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitchbend Low", desc.str(), CLR_PITCHBEND);
        break;

      case S2C140_OPC_11_DURATION:
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Duration", desc.str(), CLR_CHANGESTATE);
        break;

      case S2C140_OPC_13_ARTICULATION:
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Articulation", desc.str(), CLR_PROGCHANGE);
        break;

      case S2C140_OPC_16_PAN:
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan", desc.str(), CLR_PAN);
        break;

      case S2C140_OPC_1B:
        AddUnknown(beginOffset, curOffset-beginOffset);
        AddTime(state.delta);
        break;

      case S2C140_OPC_0E ... S2C140_OPC_10:
      case S2C140_OPC_12:
      case S2C140_OPC_14 ... S2C140_OPC_15_NOP:
      case S2C140_OPC_17 ... S2C140_OPC_18_NOP:
      case S2C140_OPC_1F:
      case S2C140_OPC_22:
        AddUnknown(beginOffset, curOffset-beginOffset);
        break;
    }
  }
  else {

    switch (statusByte & 0x3F) {
      case S2C140_OPC_00_NOP:
      case S2C140_OPC_15_NOP:
      case S2C140_OPC_1A_NOP:
      case S2C140_OPC_20_NOP:
      case S2C140_OPC_21_NOP:AddGenericEvent(beginOffset, curOffset - beginOffset, L"NOP", desc.str(), CLR_CHANGESTATE);
        break;

      case S2C140_OPC_01_MASTERVOL: {
        uint8_t newVol = GetByte(curOffset++);
        AddMasterVol(beginOffset, curOffset - beginOffset, newVol);
        break;
      }

      case S2C140_OPC_02_SET_DELTA_MULTIPLE: {
        state.deltaMultiplier = GetByte(curOffset++);
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Delta Multiplier", desc.str(), CLR_CHANGESTATE);
        break;
      }

      case S2C140_OPC_03_SET_DELTA: {
        uint8_t delta = GetByte(curOffset++);
        if (GetTime() == 0) {
          uint16_t ppqn = delta * 4;
          this->parentSeq->SetPPQN(ppqn);
          AddTempoBPMNoItem((NAMCOS2_IRQ_HZ * 60.0) / ppqn);
        }
        state.delta = (delta * state.deltaMultiplier) & 0xFF;
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Delta", desc.str(), CLR_CHANGESTATE);
        break;
      }

      case S2C140_OPC_04_CALL: {
        state.callOffsets[state.callIndex++] = curOffset+2;
        curOffset = GetShortBE(curOffset) + state.bankOffset;
        AddGenericEvent(beginOffset, 3, L"Call", desc.str(), CLR_LOOP);
        break;
      }

      case S2C140_OPC_05_RETURN: {
        if (state.callIndex == 0) {
          AddEndOfTrack(beginOffset, 1);
          return false;
        }
        curOffset = state.callOffsets[--state.callIndex];
        AddGenericEvent(beginOffset, 1, L"Return", desc.str(), CLR_LOOP);
        break;
      }

      case S2C140_OPC_09_JUMP: {
        curOffset = GetShortBE(curOffset) + state.bankOffset;

        if (!IsOffsetUsed(curOffset) || state.loopBreakIndex > 0) {
          AddGenericEvent(beginOffset, 3, L"Jump", desc.str().c_str(), CLR_LOOP);
        }
        else {
          bContinue = AddLoopForever(beginOffset, 3, L"Jump");
        }
        break;
      }

      case S2C140_OPC_0A_LOOP: {

        if (state.loopIndex == 0 || state.loopOffsets[state.loopIndex-1] != curOffset) {
          // First time hitting this loop
          state.loopCounts[state.loopIndex] = GetByte(curOffset);
          state.loopOffsets[state.loopIndex] = curOffset;
          state.loopIndex++;
          curOffset = state.bankOffset + GetShortBE(curOffset+1);
          AddGenericEvent(beginOffset, 4, L"Loop", desc.str(), CLR_LOOP);
        }
        else if (--state.loopCounts[state.loopIndex-1] == 0) {
          // Loop count hit zero. Skip
          state.loopIndex--;
          curOffset += 3;
        }
        else {
          // We've already hit this loop and the count hasn't hit zero. Jump to the offset.
          curOffset = state.bankOffset + GetShortBE(curOffset+1);
        }
        break;
      }

      case S2C140_OPC_0B_LOOP_BREAK: {
        if (state.loopBreakIndex == 0 || state.loopBreakOffsets[state.loopBreakIndex-1] != curOffset) {
          // First time hitting this loop break
          state.loopBreakCounts[state.loopBreakIndex] = GetByte(curOffset);
          state.loopBreakOffsets[state.loopBreakIndex] = curOffset;
          curOffset += 3;
          state.loopBreakIndex++;
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Break", desc.str(), CLR_LOOP);
        }
        else if (--state.loopBreakCounts[state.loopBreakIndex-1] == 0) {
          // Loop break count hit zero. Jump to the offset.
          state.loopBreakIndex--;
          curOffset = state.bankOffset + GetShortBE(curOffset+1);
        }
        else {
          // We've already hit this loop break and the count hasn't hit zero. Skip.
          curOffset += 3;
        }
        break;
      }

      case S2C140_OPC_19: {
//      state.curOffset = GetShortBE(curOffset);
        curOffset += 2;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;
      }

      case S2C140_OPC_1C: {
        curOffset += 2;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;
      }

      case S2C140_OPC_1D_LOAD_FM_SECTION: {
        if (IsOffsetUsed(beginOffset)) {
          bContinue = false;
          break;
        }
        uint8_t sectionIndex = GetByte(curOffset+1);
        curOffset += 2;
//        LoadFMSection(sectionIndex);
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Load YM2151 Section", desc.str(), CLR_CHANGESTATE);
        break;
      }

      case S2C140_OPC_1E_LOAD_C140_SECTION: {
        if (IsOffsetUsed(beginOffset)) {
          bContinue = false;
          break;
        }
        uint8_t sectionIndex = GetByte(curOffset+1);
        curOffset += 2;
        LoadC140Section(sectionIndex);
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Load C140 Section", desc.str(), CLR_CHANGESTATE);
        break;
      }
    }
  }
  state.curOffset = curOffset;
  return bContinue;
}