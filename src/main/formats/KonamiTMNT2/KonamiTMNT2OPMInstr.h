#pragma once

#include "YM2151InstrSet.h"

enum KonamiTMNT2FormatVer : u8;


// uint8_t SLOT_MASK;
// uint8_t useLFO;
// uint8_t LFO_WAVEFORM = 0;
// uint8_t LFRQ = 0;
// uint8_t PMD = 0;
// uint8_t AMD = 0;
// uint8_t PMS_AMS = 0;
// uint8_t FL_CON;
// uint8_t DT1_MUL[4];
// uint8_t op_vol[4];
// uint8_t KS_AR[4];
// uint8_t AMSEN_D1R[4];
// uint8_t DT2_D2R[4];
// uint8_t D1L_RR[4];

// u8 RL_FB_CONECT;   // C2
// u8 DT1_MUL_0;   // 0D
// u8 TL_0;        // 17
// u8 KS_AR_0;     // 1F
// u8 AMSEN_D1R_0; // 1B
// u8 DT2_D2R_0;   // 00
// u8 D1L_RR_0;    // F5
// u8 DT1_MUL_2;   // 07
// u8 TL_2;        // 20
// u8 KS_AR_2;     // 1F
// u8 AMSEN_D1R_2; // 0D
// u8 DT2_D2R_2;   // 00
// u8 D1L_RR_2;    // F5
// u8 DT1_MUL_1;   // 07
// u8 TL_1;        // 20
// u8 KS_AR_1;     // 1F
// u8 AMSEN_D1R_1; // 12
// u8 DT2_D2R_1;   // 00
// u8 D1L_RR_1;    // F4
// u8 DT1_MUL_3;   // 01
// u8 unknown_6;   // 08
// u8 KS_AR_3;     // 1F
// u8 AMSEN_D1R_3; // 8C
// u8 DT2_D2R_3;   // 00
// u8 D1L_RR_3;    // F6

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
  std::vector<konami_tmnt2_ym2151_instr> m_instrs;

  [[nodiscard]] OPMData convertToOPMData(const konami_tmnt2_ym2151_instr& instr, const std::string& name) const;
};
