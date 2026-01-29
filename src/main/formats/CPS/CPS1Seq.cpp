#include "CPS1Seq.h"
#include "CPS2Format.h"
#include "CPS1TrackV1.h"
#include "CPS1TrackV2.h"

CPS1Seq::CPS1Seq(RawFile *file, uint32_t offset, CPS1FormatVer fmtVersion, std::string name, std::vector<s8> instrTransposeTable)
    : VGMSeq(CPS1Format::name, file, offset, 0, std::move(name)),
      fmtVersion(fmtVersion),
      instrTransposeTable(instrTransposeTable) {
  setUsesMonophonicTracks();
  setAlwaysWriteInitialVol(127);
  setAlwaysWriteInitialMonoMode(true);
}

bool CPS1Seq::parseHeader() {
  if (fmtVersion == CPS1FormatVer::CPS1_V100)
    setPPQN(24);
  else {
    setPPQN(48);
  }
  return true;
}

bool CPS1Seq::parseTrackPointers() {
  // CPS1 games sometimes have this set. Perhaps flag for trivial sound effect sequence?
  if ((readByte(offset()) & 0x80) > 0)
    return false;

  std::function<u16(u32)> read16;
  if (fmtVersion == CPS1_V100) {
    read16 = [this](uint32_t offset) { return this->readShort(offset); };
  } else {
    read16 = [this](uint32_t offset) { return this->readShortBE(offset); };
  }

  this->addHeader(offset(), 1, "Sequence Flags");
  VGMHeader *header = this->addHeader(offset() + 1, read16(offset() + 1) - 1, "Track Pointers");

  const int maxTracks = fmtVersion == CPS1_V100 ? 8 : 12;

  for (int i = 0; i < maxTracks; i++) {
    uint32_t trkOff = read16(offset() + 1 + i * 2);
    if (trkOff == 0) {
      header->addChild(offset() + 1 + (i * 2), 2, "No Track");
      continue;
    }

    SeqTrack *newTrack;
    switch (fmtVersion) {
      case CPS1_VERSION_UNDEFINED:
        return false;
      case CPS1_V100:
        newTrack = new CPS1TrackV1(this, CPSSynth::YM2151, trkOff);
        break;
      case CPS1_V200:
      case CPS1_V350:
      case CPS1_V425:
        newTrack = new CPS1TrackV2(this, i < 8 ? CPSSynth::YM2151 : CPSSynth::OKIM6295, trkOff);
        break;
      case CPS1_V500:
      case CPS1_V502:
        newTrack = new CPS1TrackV2(this, i < 8 ? CPSSynth::YM2151 : CPSSynth::OKIM6295, trkOff + offset());
        break;
    }
    aTracks.push_back(newTrack);
    header->addChild(offset() + 1 + (i * 2), 2, "Track Pointer");
  }
  if (aTracks.size() == 0)
    return false;

  return true;
}


s8 CPS1Seq::transposeForInstr(u8 instrIndex) {
  if (instrIndex >= instrTransposeTable.size()) {
    return 0;
  }
  return instrTransposeTable[instrIndex];
}