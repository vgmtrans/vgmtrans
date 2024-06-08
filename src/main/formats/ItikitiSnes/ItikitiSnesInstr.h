#pragma once
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

  ItikitiSnesInstrSet(RawFile *file, uint32_t tuning_offset, uint32_t adsr_offset,
                      uint16_t spc_dir_offset, std::string name = "ItikitiSnesInstrSet");

  bool parseHeader() override { return true; }
  bool parseInstrPointers() override;

  [[nodiscard]] uint16_t spc_dir_offset() const { return m_spc_dir_offset; }

 private:
  uint32_t m_tuning_offset{};
  uint32_t m_adsr_offset{};
  uint16_t m_spc_dir_offset{};
  int m_num_instruments_to_scan{};
};

class ItikitiSnesInstr : public VGMInstr {
 public:
  ItikitiSnesInstr(VGMInstrSet *instrument_set, uint32_t tuning_offset, uint32_t adsr_offset,
                   uint32_t bank, uint32_t instrument_number, uint8_t srcn, uint16_t spc_dir_offset,
                   std::string name = "ItikitiSnesInstr");

  bool loadInstr() override;

  [[nodiscard]] uint16_t spc_dir_offset() const { return m_spc_dir_offset; }

 private:
  uint32_t m_tuning_offset{};
  uint32_t m_adsr_offset{};
  uint8_t srcn{};
  uint16_t m_spc_dir_offset{};
};

class ItikitiSnesRgn : public VGMRgn {
 public:
  ItikitiSnesRgn(ItikitiSnesInstr *instrument, uint32_t tuning_offset, uint32_t adsr_offset,
                 uint8_t srcn);

  bool loadRgn() override { return true; }

private:
  // uint32_t m_tuning_offset{};
  // uint32_t m_adsr_offset{};
};
