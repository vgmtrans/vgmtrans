#include "OrgSeq.h"

#include "base/Types.h"

DECLARE_FORMAT(Org);

OrgSeq::OrgSeq(RawFile *file, u32 offset)
    : VGMSeq(OrgFormat::name, file, offset, 0, "Org Seq") {
}

OrgSeq::~OrgSeq(void) {
}

bool OrgSeq::parseHeader(void) {
  waitTime = readShort(offset() + 6);
  beatsPerMeasure = readByte(offset() + 8);
  setPPQN(readByte(offset() + 9));

  u32 notesSoFar = 0;        //this must be used to determine the length of the entire seq

  for (int i = 0; i < 16; i++) {
    if (readShort(offset() + 0x16 + i * 6))            //if there are notes in this track
    {
      nNumTracks++;                    //well then, we might as well say it exists
      auto *newOrgTrack = addTrack<OrgTrack>(
          this, offset() + 0x12 + 16 * 6 + notesSoFar * 8, readShort(0x16 + i * 6) * 8, i);
      newOrgTrack->numNotes = readShort(offset() + 0x16 + i * 6);
      newOrgTrack->freq = readShort(offset() + 0x12 + i * 6);
      newOrgTrack->waveNum = readByte(offset() + 0x14 + i * 6);
      newOrgTrack->numNotes = readShort(offset() + 0x16 + i * 6);

      notesSoFar += newOrgTrack->numNotes;
    }
  }
  setLength(0x12 + 6 * 16 + notesSoFar * 8);

  return true;        //successful
}

OrgTrack::OrgTrack(OrgSeq *parentFile, u32 offset, u32 length, u8 realTrk)
    : SeqTrack(parentFile, offset, length), realTrkNum(realTrk) {
}

bool OrgTrack::loadTrack(u32 trackNum, u32 stopOffset, long stopDelta) {
  pMidiTrack = parentSeq->midi->addTrack();
  //SetChannelAndGroupFromTrkNum(trackNum);

  if (realTrkNum > 7)
    channel = 9;        //all tracks above 8 are drum tracks (channel 10)
  else
    channel = trackNum;
  channelGroup = 0;
  pMidiTrack->setChannelGroup(channelGroup);

  if (trackNum == 0) {
    addTempo(0, 0, ((OrgSeq *) parentSeq)->waitTime * 4000);
    addTimeSig(0, 0, 4, 4, (u8) parentSeq->ppqn());
  }
  if (channel == 10)
    addProgramChange(0, 0, waveNum);

  bInLoop = false;
  curOffset = offset();    //start at beginning of track
  curNote = 0;
  for (u16 i = 0; i < numNotes; i++)
    readEvent();

  return true;
}

bool OrgTrack::readEvent() {
  u8 key = readByte(curOffset + (numNotes - curNote) * 4 + curNote);
  u8 vel = readByte(curOffset + (numNotes - curNote) * 4 + numNotes * 2 + curNote) / 2;
  u8 dur = readByte(curOffset + (numNotes - curNote) * 4 + numNotes + curNote);
  if (key == 0xFF)
    key = prevKey;
  u32 absTime = getWord(curOffset);

  insertNoteByDur(curOffset, 4, key, vel, dur, absTime);
  u8 pan = readByte(curOffset + (numNotes - curNote) * 4 + numNotes * 3 + curNote);
  if (pan == 0xFF)
    pan = prevPan;
  else
    pan = (u8) (readByte(curOffset + (numNotes - curNote) * 4 + numNotes * 3 + curNote)
        * 10.66666666666666666666666666666);
  //if (newPan > 0x7F)	//sometimes the value is 0xFF, even though the range would seem to be 0-C according to the org editor
  //	newPan = 64;	//in this case, set it to the center position, i can't distinguish it from center on hearing tests
  if (pan != prevPan) {
    insertPan(curOffset + (numNotes - curNote) * 4 + numNotes * 3 + curNote, 1, pan, absTime);
    prevPan = pan;
  }

  curOffset += 4;
  curNote++;

  return true;
}
