/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "YM2151InstrSet.h"

enum KonamiTMNT2FormatVer : u8;

struct konami_tmnt2_ym2151_instr {
  struct op_regs {
    u8 DT1_MUL;
    u8 TL;
    u8 KS_AR;
    u8 AMSEN_D1R;
    u8 DT2_D2R;
    u8 D1L_RR;
  };

  u8 RL_FB_CONECT;
  op_regs op[4];
};


class KonamiTMNT2OPMInstrSet
    : public YM2151InstrSet {
public:

  KonamiTMNT2OPMInstrSet(RawFile *file,
                         KonamiTMNT2FormatVer fmtVer,
                         u32 offset,
                         std::string name);
  ~KonamiTMNT2OPMInstrSet() override = default;

  bool parseInstrPointers() override;

private:
  KonamiTMNT2FormatVer m_fmtVer;

  [[nodiscard]] OPMData convertToOPMData(const konami_tmnt2_ym2151_instr& instr, const std::string& name) const;
};
