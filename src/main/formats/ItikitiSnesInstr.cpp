#include "pch.h"
#include "ItikitiSnesInstr.h"
#include "SNESDSP.h"

ItikitiSnesInstrSet::ItikitiSnesInstrSet(RawFile *file, uint32_t offset, uint16_t spc_dir_offset, std::wstring name)
    : VGMInstrSet(ItikitiSnesFormat::name, file, offset, 0, std::move(name)),
      m_spc_dir_offset(spc_dir_offset), m_num_instruments_to_scan(128) {
}

bool ItikitiSnesInstrSet::GetInstrPointers() {
  std::vector<uint8_t> srcns;
  for (int index = 0; index < m_num_instruments_to_scan; index++) {
    const uint32_t instrument_offset = dwOffset + 2 * index;
    if (instrument_offset + 2 > 0x10000)
      break;

    const uint16_t adsr = GetShort(instrument_offset);
    if (adsr == 0 || adsr == 0xffff)
      continue;

    const auto srcn = static_cast<uint8_t>(index);
    const auto dir_item_offset = spc_dir_offset() + (srcn * 4);
    if (dir_item_offset + 4 > 0x10000)
      break;

    srcns.push_back(srcn);

    std::wostringstream instrument_name;
    instrument_name << L"Instrument " << index;
    auto instrument = std::make_unique<ItikitiSnesInstr>(this, instrument_offset, 0, srcn, srcn,
                                                         spc_dir_offset(), instrument_name.str());
    aInstrs.push_back(instrument.release());
  }

  if (aInstrs.empty())
    return false;

  auto sampleColl = std::make_unique<SNESSampColl>(ItikitiSnesFormat::name, this->rawfile, spc_dir_offset(), srcns);
  if (!sampleColl->LoadVGMFile())
    return false;
  (void)sampleColl.release();

  return true;
}

ItikitiSnesInstr::ItikitiSnesInstr(VGMInstrSet *instrument_set, uint32_t offset, uint32_t bank,
                                   uint32_t instrument_number, uint8_t srcn,
                                   uint16_t spc_dir_offset, std::wstring name)
    : VGMInstr(instrument_set, offset, 2, bank, instrument_number, std::move(name)), srcn(srcn),
      m_spc_dir_offset(spc_dir_offset) {
}

bool ItikitiSnesInstr::LoadInstr() {
  const auto offset = spc_dir_offset() + (srcn * 4);
  if (offset + 4 > 0x10000)
    return false;

  const auto sample_offset = GetShort(offset);

  auto region =std::make_unique<ItikitiSnesRgn>(this, dwOffset, srcn);
  region->sampOffset = sample_offset - spc_dir_offset();
  aRgns.push_back(region.release());

  return true;
}


ItikitiSnesRgn::ItikitiSnesRgn(ItikitiSnesInstr *instrument, uint32_t offset, uint8_t srcn)
    : VGMRgn(instrument, offset, 2) {
  const auto adsr1 = GetByte(offset);
  const auto adsr2 = GetByte(offset + 1);

  sampNum = srcn;
  unityKey = 71;
  fineTune = -22; // frequency 4096/4286
  AddSimpleItem(offset, 1, L"ADSR1");
  AddSimpleItem(offset + 1, 1, L"ADSR2");
  SNESConvADSR<VGMRgn>(this, adsr1, adsr2, 0);
}
