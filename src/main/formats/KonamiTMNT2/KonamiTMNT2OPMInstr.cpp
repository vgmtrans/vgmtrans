#include "KonamiTMNT2OPMInstr.h"
#include "KonamiTMNT2Format.h"
#include <spdlog/fmt/fmt.h>

KonamiTMNT2OPMInstrSet::KonamiTMNT2OPMInstrSet(
  RawFile *file,
  KonamiTMNT2FormatVer fmtVer,
  u32 offset,
  std::string name
)
  : VGMInstrSet(KonamiTMNT2Format::name, file, offset, 0, std::move(name)),
    m_fmtVer(fmtVer)
{
}

bool KonamiTMNT2OPMInstrSet::parseInstrPointers() {
  auto instrTableItem = addChild(dwOffset, 0, "Instrument Table");
  for (int i = 0; ; ++i) {
    u32 offset = dwOffset + (i * 2);
    u16 instrPtr = readShort(offset);
    if (instrPtr < dwOffset || instrPtr > dwOffset + 0x2000)
      break;
    instrTableItem->addChild(offset, 2, "Instrument Pointer");

    std::string name = fmt::format("Instrument {:03d}", i);
    VGMInstr* instr = new VGMInstr(this, instrPtr, sizeof(konami_tmnt2_ym2151_instr), 0, i, name, 0);
    instr->addChild(instrPtr, 1, "RL_FB_CONECT");
    for (int i = 0; i < 4; ++i) {
      int offset = instrPtr + 1 + (i * 6);
      auto opItem = instr->addChild(instrPtr + 1 + (i * 6), 6, fmt::format("OP {}", i));
      opItem->addChild(offset, 1, "DT1_MUL");
      opItem->addChild(offset+1, 1, "TL");
      opItem->addChild(offset+2, 1, "KS_AR");
      opItem->addChild(offset+3, 1, "AMSEN_D1R");
      opItem->addChild(offset+4, 1, "DT2_D2R");
      opItem->addChild(offset+5, 1, "D1L_RR");
    }
    aInstrs.push_back(instr);
  }
  return true;
}

std::string KonamiTMNT2OPMInstrSet::generateOPMFile() {

}

bool KonamiTMNT2OPMInstrSet::saveAsOPMFile(const std::string &filepath) {

}