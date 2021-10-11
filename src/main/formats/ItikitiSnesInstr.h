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

  ItikitiSnesInstrSet(RawFile *file, uint32_t offset, uint16_t spc_dir_offset,
                      std::wstring name = L"ItikitiSnesInstrSet");

  bool GetHeaderInfo() override { return true; }
  bool GetInstrPointers() override;

  [[nodiscard]] uint16_t spc_dir_offset() const { return m_spc_dir_offset; }

 private:
  uint16_t m_spc_dir_offset{};
  int m_num_instruments_to_scan{};
};

class ItikitiSnesInstr : public VGMInstr {
 public:
  ItikitiSnesInstr(VGMInstrSet *instrument_set, uint32_t offset, uint32_t bank,
                   uint32_t instrument_number, uint8_t srcn, uint16_t spc_dir_offset,
                   std::wstring name = L"ItikitiSnesInstr");

  bool LoadInstr() override;

  [[nodiscard]] uint16_t spc_dir_offset() const { return m_spc_dir_offset; }

 private:
  uint8_t srcn{};
  uint16_t m_spc_dir_offset{};
};

class ItikitiSnesRgn : public VGMRgn {
 public:
  ItikitiSnesRgn(ItikitiSnesInstr *instrument, uint32_t offset, uint8_t srcn);

  bool LoadRgn() override { return true; }
};
