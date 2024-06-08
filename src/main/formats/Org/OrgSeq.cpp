#include "OrgSeq.h"

DECLARE_FORMAT(Org);

OrgSeq::OrgSeq(RawFile *file, uint32_t offset)
    : VGMSeq(OrgFormat::name, file, offset, 0, "Org Seq") {
}

OrgSeq::~OrgSeq(void) {
}

bool OrgSeq::parseHeader(void) {
  waitTime = readShort(dwOffset + 6);
  beatsPerMeasure = readByte(dwOffset + 8);
  setPPQN(readByte(dwOffset + 9));

  uint32_t notesSoFar = 0;        //this must be used to determine the length of the entire seq

  for (int i = 0; i < 16; i++) {
    if (readShort(dwOffset + 0x16 + i * 6))            //if there are notes in this track
    {
      nNumTracks++;                    //well then, we might as well say it exists
      OrgTrack
          *newOrgTrack = new OrgTrack(this, dwOffset + 0x12 + 16 * 6 + notesSoFar * 8, readShort(0x16 + i * 6) * 8, i);
      newOrgTrack->numNotes = readShort(dwOffset + 0x16 + i * 6);
      newOrgTrack->freq = readShort(dwOffset + 0x12 + i * 6);
      newOrgTrack->waveNum = readByte(dwOffset + 0x14 + i * 6);
      newOrgTrack->numNotes = readShort(dwOffset + 0x16 + i * 6);
      aTracks.push_back(newOrgTrack);

      notesSoFar += newOrgTrack->numNotes;
    }
  }
  unLength = 0x12 + 6 * 16 + notesSoFar * 8;

  return true;        //successful
}

OrgTrack::OrgTrack(OrgSeq *parentFile, uint32_t offset, uint32_t length, uint8_t realTrk)
    : SeqTrack(parentFile, offset, length), realTrkNum(realTrk) {
}

bool OrgTrack::loadTrack(uint32_t trackNum, uint32_t stopOffset, long stopDelta) {
  pMidiTrack = parentSeq->midi->AddTrack();
  //SetChannelAndGroupFromTrkNum(trackNum);

  if (realTrkNum > 7)
    channel = 9;        //all tracks above 8 are drum tracks (channel 10)
  else
    channel = trackNum;
  channelGroup = 0;
  pMidiTrack->SetChannelGroup(channelGroup);

  if (trackNum == 0) {
    addTempo(0, 0, ((OrgSeq *) parentSeq)->waitTime * 4000);
    addTimeSig(0, 0, 4, 4, (uint8_t) parentSeq->ppqn());
  }
  if (channel == 10)
    addProgramChange(0, 0, waveNum);

  bInLoop = false;
  curOffset = dwOffset;    //start at beginning of track
  curNote = 0;
  for (uint16_t i = 0; i < numNotes; i++)
    readEvent();

  return true;
}

bool OrgTrack::readEvent() {
  uint8_t key = readByte(curOffset + (numNotes - curNote) * 4 + curNote);
  uint8_t vel = readByte(curOffset + (numNotes - curNote) * 4 + numNotes * 2 + curNote) / 2;
  uint8_t dur = readByte(curOffset + (numNotes - curNote) * 4 + numNotes + curNote);
  if (key == 0xFF)
    key = prevKey;
  uint32_t absTime = getWord(curOffset);

  insertNoteByDur(curOffset, 4, key, vel, dur, absTime);
  uint8_t pan = readByte(curOffset + (numNotes - curNote) * 4 + numNotes * 3 + curNote);
  if (pan == 0xFF)
    pan = prevPan;
  else
    pan = (uint8_t) (readByte(curOffset + (numNotes - curNote) * 4 + numNotes * 3 + curNote)
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
