#pragma once

#include "base/Types.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "ItikitiSnesFormat.h"

class ItikitiSnesInstrSet;
class ItikitiSnesInstr;
class ItikitiSnesRgn;

class ItikitiSnesInstrSet : public VGMInstrSet {
 public:
  friend ItikitiSnesInstr;
  friend ItikitiSnesRgn;

  ItikitiSnesInstrSet(RawFile *file, u32 tuning_offset, u32 adsr_offset,
                      u16 spc_dir_offset, std::string name = "ItikitiSnesInstrSet");

  bool parseHeader() override { return true; }
  bool parseInstrPointers() override;

  [[nodiscard]] u16 spc_dir_offset() const { return m_spc_dir_offset; }

 private:
  u32 m_tuning_offset{};
  u32 m_adsr_offset{};
  u16 m_spc_dir_offset{};
  int m_num_instruments_to_scan{};
};

class ItikitiSnesInstr : public VGMInstr {
 public:
  ItikitiSnesInstr(VGMInstrSet *instrument_set, u32 tuning_offset, u32 adsr_offset,
                   u32 bank, u32 instrument_number, u8 srcn, u16 spc_dir_offset,
                   std::string name = "ItikitiSnesInstr");

  bool loadInstr() override;

  [[nodiscard]] u16 spc_dir_offset() const { return m_spc_dir_offset; }

 private:
  u32 m_tuning_offset{};
  u32 m_adsr_offset{};
  u8 srcn{};
  u16 m_spc_dir_offset{};
};

class ItikitiSnesRgn : public VGMRgn {
 public:
  ItikitiSnesRgn(ItikitiSnesInstr *instrument, u32 tuning_offset, u32 adsr_offset,
                 u8 srcn);

  bool loadRgn() override { return true; }

private:
  // u32 m_tuning_offset{};
  // u32 m_adsr_offset{};
};
