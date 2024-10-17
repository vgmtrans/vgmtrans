#include "CPS0Seq.h"
#include "CPS2Format.h"
#include "CPSTrackV0.h"

CPS0Seq::CPS0Seq(RawFile *file, uint32_t offset, std::string name, std::vector<s8> instrTransposeTable)
    : VGMSeq(CPS1Format::name, file, offset, 0, std::move(name)),
      instrTransposeTable(instrTransposeTable) {
  setUsesMonophonicTracks();
  setAlwaysWriteInitialVol(127);
  setAlwaysWriteInitialMonoMode(true);
}

bool CPS0Seq::parseHeader() {
  setPPQN(0x30);
  return true;
}

bool CPS0Seq::parseTrackPointers() {
  VGMHeader *header = this->addHeader(dwOffset + 1, 8 * 2, "Track Pointers");

  const int maxTracks = 8;

  for (int i = 0; i < maxTracks; i++) {
    uint32_t offset = readShort(dwOffset + 1 + i * 2);
    if (offset == 0) {
      header->addChild(dwOffset + 1 + (i * 2), 2, "No Track");
      continue;
    }
    auto newTrack = new CPSTrackV0(this, CPSSynth::YM2151, offset);

    aTracks.push_back(newTrack);
    header->addChild(dwOffset + 1 + (i * 2), 2, "Track Pointer");
  }
  if (aTracks.size() == 0)
    return false;

  return true;
}

s8 CPS0Seq::getTransposeForInstr(u8 instrIndex) {
  if (instrIndex >= instrTransposeTable.size()) {
    return 0;
  }
  return instrTransposeTable[instrIndex];
}