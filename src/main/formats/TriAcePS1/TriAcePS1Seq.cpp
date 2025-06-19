// The following sequence analysis code is based on the work of Sound Tester 774 from 2ch.net,
// author of so2mml. The code is based on his write-up of the format specifications.  Many thanks to him.

#include "TriAcePS1Seq.h"
#include "SeqEvent.h"
#include "helper.h"

DECLARE_FORMAT(TriAcePS1);

// ************
// TriAcePS1Seq
// ************

TriAcePS1Seq::TriAcePS1Seq(RawFile *file, uint32_t offset, const std::string &name)
    : VGMSeq(TriAcePS1Format::name, file, offset, 0, name) {
  setUseLinearAmplitudeScale(true);
  useReverb();
  setAlwaysWriteInitialPitchBendRange(12 * 100);
}

bool TriAcePS1Seq::parseHeader() {
  setPPQN(0x30);

  header = addHeader(dwOffset, 0xD5);
  header->addChild(dwOffset + 2, 2, "Size");
  header->addChild(dwOffset + 0xB, 4, "Song title");
  header->addChild(dwOffset + 0xF, 1, "BPM");
  header->addChild(dwOffset + 0x10, 2, "Time Signature");

  unLength = readShort(dwOffset + 2);
  setAlwaysWriteInitialTempo(readByte(dwOffset + 0xF));
  return true;
}

bool TriAcePS1Seq::parseTrackPointers() {
  VGMHeader *TrkInfoHeader = header->addHeader(dwOffset + 0x16, 6 * 32, "Track Info Blocks");


  readBytes(dwOffset + 0x16, 6 * 32, &TrkInfos);
  for (int i = 0; i < 32; i++)
    if (TrkInfos[i].trkOffset != 0) {
      aTracks.push_back(new TriAcePS1Track(this, TrkInfos[i].trkOffset, 0));

      VGMHeader *TrkInfoBlock = TrkInfoHeader->addHeader(dwOffset + 0x16 + 6 * i, 6, "Track Info");
    }
  return true;
}

void TriAcePS1Seq::resetVars() {
  VGMSeq::resetVars();
}

bool TriAcePS1Seq::postLoad() {
  bool success = VGMSeq::postLoad();
  if (readMode == READMODE_ADD_TO_UI) {
    addChildren(aScorePatterns);
  }
  return success;
}

// **************
// TriAcePS1Track
// **************

TriAcePS1Track::TriAcePS1Track(TriAcePS1Seq *parentSeq, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length) {
}

void TriAcePS1Track::loadTrackMainLoop(uint32_t stopOffset, int32_t stopTime) {
  TriAcePS1Seq *seq = (TriAcePS1Seq *) parentSeq;
  uint32_t scorePatternPtrOffset = dwOffset;
  uint16_t scorePatternOffset = readShort(scorePatternPtrOffset);
  while (scorePatternOffset != 0xFFFF) {
    if (seq->patternMap[scorePatternOffset])
      seq->curScorePattern = NULL;
    else {
      TriAcePS1ScorePattern *pattern = new TriAcePS1ScorePattern(seq, scorePatternOffset);
      seq->patternMap[scorePatternOffset] = pattern;
      seq->curScorePattern = pattern;
      seq->aScorePatterns.push_back(pattern);
    }
    uint32_t endOffset = readScorePattern(scorePatternOffset);
    if (seq->curScorePattern)
      seq->curScorePattern->unLength = endOffset - seq->curScorePattern->dwOffset;
    addChild(scorePatternPtrOffset, 2, "Score Pattern Ptr");
    scorePatternPtrOffset += 2;
    scorePatternOffset = readShort(scorePatternPtrOffset);
  }
  addEndOfTrack(scorePatternPtrOffset, 2);
  unLength = scorePatternPtrOffset + 2 - dwOffset;
}


uint32_t TriAcePS1Track::readScorePattern(uint32_t offset) {
  curOffset = offset;    //start at beginning of track
  impliedNoteDur = 0;    //reset the implied values (from event 0x9E)
  impliedVelocity = 0;
  while (readEvent() == State::Active);
  return curOffset;
}

SeqTrack::State TriAcePS1Track::readEvent() {
  uint32_t beginOffset = curOffset;

  uint8_t status_byte = readByte(curOffset++);
  uint8_t event_dur = 0;

  //0-0x7F is a note event
  if (status_byte <= 0x7F) {
    event_dur = readByte(curOffset++); //Delta time from "Note on" to "Next command(op-code)".
    uint8_t note_dur;
    uint8_t velocity;
    if (!impliedNoteDur) note_dur = readByte(curOffset++);  //Delta time from "Note on" to "Note off".
    else note_dur = impliedNoteDur;
    if (!impliedVelocity) velocity = readByte(curOffset++);
    else velocity = impliedVelocity;
    addNoteByDur_Extend(beginOffset, curOffset - beginOffset, status_byte, velocity, note_dur);
  }
  else
    switch (status_byte) {
      case 0x80 :
        addGenericEvent(beginOffset, curOffset - beginOffset, "Score Pattern End", "", Type::TrackEnd);
        return State::Finished;

      //unknown
      case 0x81 :
        event_dur = readByte(curOffset++);
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //unknown
      case 0x82 :
        event_dur = readByte(curOffset++);
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //program change
      case 0x83 : {
        event_dur = readByte(curOffset++);
        uint8_t progNum = readByte(curOffset++);
        uint8_t bankNum = readByte(curOffset++);

        uint8_t bank = (bankNum * 2) + ((progNum > 0x7F) ? 1 : 0);
        if (progNum > 0x7F)
          progNum -= 0x80;

        addBankSelectNoItem(bank);
        addProgramChange(beginOffset, curOffset - beginOffset, progNum);
        break;
      }

      //pitch bend
      case 0x84 : {
        event_dur = readByte(curOffset++);
        short bend = ((char) readByte(curOffset++)) << 7;
        addPitchBend(beginOffset, curOffset - beginOffset, bend);
        break;
      }

      //volume
      case 0x85 : {
        event_dur = readByte(curOffset++);
        uint8_t val = readByte(curOffset++);
        addVol(beginOffset, curOffset - beginOffset, val);
        break;
      }

      //expression
      case 0x86 : {
        event_dur = readByte(curOffset++);
        uint8_t val = readByte(curOffset++);
        addExpression(beginOffset, curOffset - beginOffset, val);
        break;
      }

      case 0x87 :            //pan
      {
        event_dur = readByte(curOffset++);
        uint8_t pan = readByte(curOffset++);
        addPan(beginOffset, curOffset - beginOffset, pan);
      }
        break;

      case 0x88 :            //unknown
        event_dur = readByte(curOffset++);
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //damper pedal
      case 0x89 : {
        event_dur = readByte(curOffset++);
        uint8_t val = readByte(curOffset++);
        addSustainEvent(beginOffset, curOffset - beginOffset, val);
        break;
      }

      //unknown (tempo?)
      case 0x8A : {
        event_dur = readByte(curOffset++);
        uint8_t val = readByte(curOffset++);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event (tempo?)");
        break;
      }

      //Dal Segno: start point
      case 0x8D :
        addGenericEvent(beginOffset, curOffset - beginOffset, "Dal Segno: start point", "", Type::Unknown);
        break;

      //Dal Segno: end point
      case 0x8E :
        curOffset++;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Dal Segno: end point", "", Type::Unknown);
        break;

      //rest
      case 0x8F : {
        uint8_t rest = readByte(curOffset++);
        addRest(beginOffset, curOffset - beginOffset, rest);
        break;
      }

      //unknown
      case 0x90 :
        event_dur = readByte(curOffset++);
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //unknown
      case 0x92 :
        event_dur = readByte(curOffset++);
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //unknown
      case 0x93 :
        event_dur = readByte(curOffset++);
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //unknown
      case 0x94 :
        event_dur = readByte(curOffset++);
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //unknown
      case 0x95 :
        event_dur = readByte(curOffset++);
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event  (Tie?)");
        break;

      //Pitch Bend Range
      case 0x96 : {
        event_dur = readByte(curOffset++);
        uint8_t semitones = readByte(curOffset++);
        addPitchBendRange(beginOffset, curOffset - beginOffset, semitones * 100);
        break;
      }

      //unknown
      case 0x97 :
        event_dur = readByte(curOffset++);
        curOffset += 4;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //unknown
      case 0x99 :
        event_dur = readByte(curOffset++);
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //unknown
      case 0x9A :
        event_dur = readByte(curOffset++);
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //unknown
      case 0x9B :
        event_dur = readByte(curOffset++);
        curOffset += 5;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //imply note params
      case 0x9E :
        impliedNoteDur = readByte(curOffset++);
        impliedVelocity = readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Imply Note Params", "", Type::ChangeState);
        break;

      default :
        addUnknown(beginOffset, curOffset - beginOffset);
        return State::Finished;
    }

  if (event_dur)
    addTime(event_dur);

  return State::Active;
}

// The following two functions are overridden so that events become children of the Score Patterns and not the tracks.
bool TriAcePS1Track::isOffsetUsed(uint32_t offset) {
  return false;
}

void TriAcePS1Track::addEvent(SeqEvent *pSeqEvent) {
  TriAcePS1ScorePattern *pattern = ((TriAcePS1Seq *) parentSeq)->curScorePattern;
  if (pattern == NULL) {
    // it must be already added, reject it
    delete pSeqEvent;
    return;
  }

  if (readMode != READMODE_ADD_TO_UI)
    return;

  pattern->addChild(pSeqEvent);
}
