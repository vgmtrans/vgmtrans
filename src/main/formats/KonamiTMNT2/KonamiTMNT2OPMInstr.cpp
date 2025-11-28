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
  for (int i = 0; ; ++i) {
    u32 offset = dwOffset + (i * 2);
    u16 instrPtr = readShort(offset);
    if (instrPtr < dwOffset || instrPtr > dwOffset + 0x2000)
      break;

    std::string name = fmt::format("Instrument {:03d}", i);
    VGMInstr* instr = new VGMInstr(this, offset, sizeof(konami_tmnt2_ym2151_instr), 0, i, name, 0);
    aInstrs.push_back(instr);
  }
}

std::string KonamiTMNT2OPMInstrSet::generateOPMFile() {

}

bool KonamiTMNT2OPMInstrSet::saveAsOPMFile(const std::string &filepath) {

}