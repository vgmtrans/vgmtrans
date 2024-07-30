#include "SonyPS2Seq.h"

DECLARE_FORMAT(SonyPS2);

using namespace std;

// ******
// BGMSeq
// ******

SonyPS2Seq::SonyPS2Seq(RawFile *file, uint32_t offset, std::string name)
    : VGMSeqNoTrks(SonyPS2Format::name, file, offset, std::move(name)),
      compOption(0),
      bSkipDeltaTime(0) {
  setUseLinearAmplitudeScale(true);        // Onimusha: Kaede Theme track 2 for example of linear vol scale.
  useReverb();
}

bool SonyPS2Seq::parseHeader() {
  uint32_t curOffset = offset();
  //read the version chunk
  readBytes(curOffset, 0x10, &versCk);
  VGMHeader *versCkHdr = VGMSeq::addHeader(curOffset, versCk.chunkSize, "Version Chunk");
  versCkHdr->addChild(curOffset, 4, "Creator");
  versCkHdr->addChild(curOffset + 4, 4, "Type");
  curOffset += versCk.chunkSize;

  //read the header chunk
  readBytes(curOffset, 0x20, &hdrCk);
  VGMHeader *hdrCkHdr = VGMSeq::addHeader(curOffset, hdrCk.chunkSize, "Header Chunk");
  hdrCkHdr->addChild(curOffset, 4, "Creator");
  hdrCkHdr->addChild(curOffset + 4, 4, "Type");
  curOffset += hdrCk.chunkSize;
  //Now we're at the Midi chunk, which starts with the sig "SCEIMidi" (in 32bit little endian)
  midiChunkSize = readWord(curOffset + 8);
  maxMidiNumber = readWord(curOffset + 12);
  //Get the first midi data block addr, which is provided relative to beginning of Midi chunk
  midiOffsetAddr = readWord(curOffset + 16) + curOffset;
  curOffset = midiOffsetAddr;
  //Now we're at the Midi Data Block
  uint32_t sequenceOffset = readWord(curOffset);        //read sequence offset
  setEventsOffset(curOffset + sequenceOffset);
  setPPQN(readShort(curOffset + 4));                    //read ppqn value

  //if a compression mode is being applied
  if (sequenceOffset != 6) {
    compOption = readShort(curOffset + 6);            //read compression mode
  }

  nNumTracks = 16;
  channel = 0;
  setCurTrack(channel);
  return true;
}


bool SonyPS2Seq::readEvent(void) {
  uint32_t beginOffset = curOffset;
  uint32_t deltaTime;
  if (bSkipDeltaTime)
    deltaTime = 0;
  else
    deltaTime = readVarLen(curOffset);
  addTime(deltaTime);
  if (curOffset >= rawFile()->size())
    return false;

  bSkipDeltaTime = false;

  uint8_t status_byte = readByte(curOffset++);

  // Running Status
  if (status_byte <= 0x7F) {
    // some games were ripped to PSF with the EndTrack event missing, so
    // if we read a sequence of four 0 bytes, then just treat that as the end of the track
    if (status_byte == 0 && readWord(curOffset) == 0) {
      return false;
    }
    status_byte = runningStatus;
    curOffset--;
  }
  else if (status_byte != 0xFF)
    runningStatus = status_byte;


  channel = status_byte & 0x0F;
  setCurTrack(channel);

  switch (status_byte & 0xF0) {
    //note off event. Unlike SMF, there is no velocity data byte, just the note val.
    case 0x80 : {
      auto key = getDataByte(curOffset++);
      addNoteOff(beginOffset, curOffset - beginOffset, key);
      break;
    }

    //note on event (note off if velocity is zero)
    case 0x90 : {
      auto key = readByte(curOffset++);
      auto vel = getDataByte(curOffset++);
      if (vel > 0)                                                    //if the velocity is > 0, it's a note on
        addNoteOn(beginOffset, curOffset - beginOffset, key, vel);
      else                                                            //otherwise it's a note off
        addNoteOff(beginOffset, curOffset - beginOffset, key);
      break;
    }

    case 0xB0 : {
      uint8_t controlNum = readByte(curOffset++);
      uint8_t value = getDataByte(curOffset++);

      //control number
      switch (controlNum) {
        case 1 :
          addModulation(beginOffset, curOffset - beginOffset, value);
          break;

        case 2 :
          addBreath(beginOffset, curOffset - beginOffset, value);
          break;

        case 6 :
          //AddGenericEvent(beginOffset, curOffset-beginOffset, "NRPN Data Entry", NULL, BG_CLR_PINK);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Loop start number", "", CLR_LOOP);
          break;

        //volume
        case 7 :
          addVol(beginOffset, curOffset - beginOffset, value);
          break;

        //pan
        case 10 :
          addPan(beginOffset, curOffset - beginOffset, value);
          break;

        //expression
        case 11 :
          addExpression(beginOffset, curOffset - beginOffset, value);
          break;

        //0 == endless loop
        case 38 :
          addGenericEvent(beginOffset, curOffset - beginOffset, "Loop count", "", CLR_LOOP);
          break;

        //(0x63) nrpn msb
        case 99 :
          switch (value) {
            case 0 :
              addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", "", CLR_LOOP);
              break;

            case 1 :
              addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", "", CLR_LOOP);
              break;
          }
          break;
      }
    }
      break;

    case 0xC0 : {
      uint8_t progNum = getDataByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, progNum);
    }
      break;

    case 0xE0 : {
      uint8_t hi = readByte(curOffset++);
      uint8_t lo = getDataByte(curOffset++);
      addPitchBendMidiFormat(beginOffset, curOffset - beginOffset, hi, lo);
    }
      break;

    case 0xF0 : {
      if (status_byte == 0xFF) {
        switch (readByte(curOffset++)) {
          //tempo. identical to SMF
          case 0x51 : {
            uint32_t microsPerQuarter = readWordBE(curOffset) & 0x00FFFFFF;    //mask out the hi byte 0x03
            addTempo(beginOffset, curOffset + 4 - beginOffset, microsPerQuarter);
            curOffset += 4;
            break;
          }

          case 0x2F :
            addEndOfTrack(beginOffset, curOffset - beginOffset);
            return false;

          default :
            addEndOfTrack(beginOffset, curOffset - beginOffset - 1);
            return false;
        }
      }
      break;
    }

    default:
      addGenericEvent(beginOffset, curOffset - beginOffset, "UNKNOWN", "", CLR_UNRECOGNIZED);
      return false;
  }
  return true;
}

uint8_t SonyPS2Seq::getDataByte(uint32_t offset) {
  uint8_t dataByte = readByte(offset);
  if (dataByte & 0x80) {
    bSkipDeltaTime = true;
    dataByte &= 0x7F;
  }
  else
    bSkipDeltaTime = false;
  return dataByte;
}