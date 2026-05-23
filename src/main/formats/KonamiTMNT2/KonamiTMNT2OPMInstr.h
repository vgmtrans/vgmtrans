/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "YM2151InstrSet.h"

enum KonamiTMNT2FormatVer : uint8_t;

struct konami_tmnt2_ym2151_instr {
  struct op_regs {
    uint8_t DT1_MUL;
    uint8_t TL;
    uint8_t KS_AR;
    uint8_t AMSEN_D1R;
    uint8_t DT2_D2R;
    uint8_t D1L_RR;
  };

  uint8_t RL_FB_CONECT;
  op_regs op[4];
};


class KonamiTMNT2OPMInstrSet
    : public YM2151InstrSet {
public:

  KonamiTMNT2OPMInstrSet(RawFile *file,
                         KonamiTMNT2FormatVer fmtVer,
                         uint32_t offset,
                         std::string name);
  ~KonamiTMNT2OPMInstrSet() override = default;

  bool parseInstrPointers() override;

private:
  KonamiTMNT2FormatVer m_fmtVer;

  [[nodiscard]] OPMData convertToOPMData(const konami_tmnt2_ym2151_instr& instr, const std::string& name) const;
};
