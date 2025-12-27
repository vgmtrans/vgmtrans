/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <spdlog/fmt/fmt.h>

struct OPMData {
  struct LFO {
    uint8_t LFRQ;
    uint8_t AMD;
    uint8_t PMD;
    uint8_t WF;
    uint8_t NFRQ;
  };

  struct CH {
    uint8_t PAN;
    uint8_t FL;
    uint8_t CON;
    uint8_t AMS;
    uint8_t PMS;
    uint8_t SLOT_MASK;
    uint8_t NE;
  };

  struct OP {
    uint8_t AR;
    uint8_t D1R;
    uint8_t D2R;
    uint8_t RR;
    uint8_t D1L;
    uint8_t TL;
    uint8_t KS;
    uint8_t MUL;
    uint8_t DT1;
    uint8_t DT2;
    uint8_t AMS_EN;
  };

  std::string name;
  LFO lfo;
  CH ch;
  OP op[4];

  std::string toOPMString(int num) const {
    fmt::memory_buffer buf;
    buf.reserve(350);
    auto out = std::back_inserter(buf);

    fmt::format_to(out, "@:{} {}\n", num, name);
    fmt::format_to(
        out,
        "LFO:{}   {}   {}   {}   {}\n",
        +lfo.LFRQ, +lfo.AMD, +lfo.PMD, +lfo.WF, +lfo.NFRQ
    );
    fmt::format_to(
        out,
        "CH: {}   {}   {}   {}   {} {}   {}\n",
        +ch.PAN, +ch.FL, +ch.CON, +ch.AMS, +ch.PMS, +ch.SLOT_MASK, +ch.NE
    );

    const int   opIndex[4] = { 0, 2, 1, 3 };
    const char* opNames[4] = { "M1", "C1", "M2", "C2" };

    for (int i = 0; i < 4; ++i) {
      const OP& op = this->op[opIndex[i]];
      fmt::format_to(
          out,
          "{}: {}   {}   {}   {}  {}  {}   {}   {}   {}   {} {}\n",
          opNames[i], +op.AR, +op.D1R, +op.D2R, +op.RR, +op.D1L, +op.TL, +op.KS, +op.MUL, +op.DT1, +op.DT2, +op.AMS_EN
      );
    }
    return fmt::to_string(buf);
  }


  void set_fl_con(u8 fl_con) {
    ch.FL = (fl_con >> 3) & 0b111;
    ch.CON = fl_con & 0b111;
  }

  void set_dt1_mul(u8 dt1_mul[4]) {
    for (int i = 0; i < 4; ++i) {
      auto& opx = op[i];
      opx.DT1 = (dt1_mul[i] >> 4) & 0b111;
      opx.MUL = dt1_mul[i] & 0b1111;
    }
  }

  void set_dt1_mul(u8 dt1_mul, int opIdx) {
    op[opIdx].DT1 = (dt1_mul >> 4) & 0b111;
    op[opIdx].MUL = dt1_mul & 0b1111;
  }

  void set_tl(u8 tl[4]) {
    for (int i = 0; i < 4; ++i) {
      auto& opx = op[i];
      opx.TL = tl[i];
    }
  }

  void set_tl(u8 tl, int opIdx) {
    op[opIdx].TL = tl;
  }

  void set_ks_ar(u8 ks_ar[4]) {
    for (int i = 0; i < 4; ++i) {
      auto& opx = op[i];
      opx.KS = ks_ar[i] >> 6;
      opx.AR = ks_ar[i] & 0b11111;
    }
  }

  void set_ks_ar(u8 ks_ar, int opIdx) {
    op[opIdx].KS = ks_ar >> 6;
    op[opIdx].AR = ks_ar & 0b11111;
  }

  void set_asmen_d1r(u8 amsen_d1r[4]) {
    for (int i = 0; i < 4; ++i) {
      auto& opx = op[i];
      opx.AMS_EN = amsen_d1r[i] & 0b10000000;
      opx.D1R = amsen_d1r[i] & 0b11111;
    }
  }

  void set_asmen_d1r(u8 amsen_d1r, int opIdx) {
    op[opIdx].AMS_EN = amsen_d1r & 0b10000000;
    op[opIdx].D1R = amsen_d1r & 0b11111;
  }

  void set_dt2_d2r(u8 dt2_d2r[4]) {
    for (int i = 0; i < 4; ++i) {
      auto& opx = op[i];
      opx.DT2 = dt2_d2r[i] >> 6;
      opx.D2R = dt2_d2r[i] & 0b11111;
    }
  }

  void set_dt2_d2r(u8 dt2_d2r, int opIdx) {
    op[opIdx].DT2 = dt2_d2r >> 6;
    op[opIdx].D2R = dt2_d2r & 0b11111;
  }

  void set_d1l_rr(u8 d1l_rr[4]) {
    for (int i = 0; i < 4; ++i) {
      auto& opx = op[i];
      opx.D1L = d1l_rr[i] >> 4;
      opx.RR = d1l_rr[i] & 0b1111;
    }
  }

  void set_d1l_rr(u8 d1l_rr, int opIdx) {
    op[opIdx].D1L = d1l_rr >> 4;
    op[opIdx].RR = d1l_rr & 0b1111;
  }

  void set_pms_ams(u8 pms_ams) {
    ch.PMS = (pms_ams >> 4) & 0b1111;
    ch.AMS = pms_ams & 0b11;
  }
};