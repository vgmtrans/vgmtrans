/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "KonamiTMNT2OPMInstr.h"
#include "KonamiTMNT2Format.h"
#include <spdlog/fmt/fmt.h>

KonamiTMNT2OPMInstrSet::KonamiTMNT2OPMInstrSet(
  RawFile *file,
  KonamiTMNT2FormatVer fmtVer,
  u32 offset,
  std::string name
)
  : YM2151InstrSet(KonamiTMNT2Format::name, file, offset, 0, std::move(name)),
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
    konami_tmnt2_ym2151_instr instrData{};
    readBytes(instrPtr, sizeof(konami_tmnt2_ym2151_instr), &instrData);

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
    addOPMInstrument(convertToOPMData(instrData, name));
  }
  return true;
}

OPMData KonamiTMNT2OPMInstrSet::convertToOPMData(const konami_tmnt2_ym2151_instr& instr, const std::string& name) const {
  OPMData data{};
  data.name = name;

  data.ch.PAN = 0xC0;
  data.set_fl_con(instr.RL_FB_CONECT);
  data.ch.AMS = 0;
  data.ch.PMS = 0;
  data.ch.SLOT_MASK = 120;
  data.ch.NE = 0;

  const int slotOrder[4] = {0, 2, 1, 3};
  for (int i = 0; i < 4; ++i) {
    int slot = slotOrder[i];
    data.set_dt1_mul(instr.op[i].DT1_MUL, slot);
    data.set_tl(instr.op[i].TL, slot);
    data.set_ks_ar(instr.op[i].KS_AR, slot);
    data.set_asmen_d1r(instr.op[i].AMSEN_D1R, slot);
    data.set_dt2_d2r(instr.op[i].DT2_D2R, slot);
    data.set_d1l_rr(instr.op[i].D1L_RR, slot);
  }

  return data;
}