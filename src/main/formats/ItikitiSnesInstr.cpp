#include "pch.h"
#include "ItikitiSnesInstr.h"
#include "SNESDSP.h"

ItikitiSnesInstrSet::ItikitiSnesInstrSet(RawFile *file, uint32_t tuning_offset,
                                         uint32_t adsr_offset, uint16_t spc_dir_offset,
                                         std::wstring name)
    : VGMInstrSet(ItikitiSnesFormat::name, file, adsr_offset, 0, std::move(name)),
      m_tuning_offset(tuning_offset), m_adsr_offset(adsr_offset), m_spc_dir_offset(spc_dir_offset),
      m_num_instruments_to_scan(128) {
}

bool ItikitiSnesInstrSet::GetInstrPointers() {
  std::vector<uint8_t> srcns;
  for (int index = 0; index < m_num_instruments_to_scan; index++) {
    const uint32_t ins_tuning_offset = m_tuning_offset + 2 * index;
    const uint32_t ins_adsr_offset = m_adsr_offset + 2 * index;
    if (ins_tuning_offset + 2 > 0x10000 || ins_adsr_offset + 2 > 0x10000)
      break;

    const uint16_t adsr = GetShort(ins_adsr_offset);
    if (adsr == 0 || adsr == 0xffff)
      continue;

    const auto srcn = static_cast<uint8_t>(index);
    const auto dir_item_offset = spc_dir_offset() + (srcn * 4);
    if (dir_item_offset + 4 > 0x10000)
      break;

    srcns.push_back(srcn);

    std::wostringstream instrument_name;
    instrument_name << L"Instrument " << index;
    auto instrument =
        std::make_unique<ItikitiSnesInstr>(this, ins_tuning_offset, ins_adsr_offset, 0, srcn, srcn,
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

ItikitiSnesInstr::ItikitiSnesInstr(VGMInstrSet *instrument_set, uint32_t tuning_offset,
                                   uint32_t adsr_offset, uint32_t bank, uint32_t instrument_number,
                                   uint8_t srcn, uint16_t spc_dir_offset, std::wstring name)
    : VGMInstr(instrument_set, adsr_offset, 2, bank, instrument_number, std::move(name)),
      m_tuning_offset(tuning_offset), m_adsr_offset(adsr_offset), srcn(srcn),
      m_spc_dir_offset(spc_dir_offset) {
}

bool ItikitiSnesInstr::LoadInstr() {
  const auto offset = spc_dir_offset() + (srcn * 4);
  if (offset + 4 > 0x10000)
    return false;

  const auto sample_offset = GetShort(offset);

  auto region = std::make_unique<ItikitiSnesRgn>(this, m_tuning_offset, m_adsr_offset, srcn);
  region->sampOffset = sample_offset - spc_dir_offset();
  aRgns.push_back(region.release());

  return true;
}


ItikitiSnesRgn::ItikitiSnesRgn(ItikitiSnesInstr *instrument, uint32_t tuning_offset,
                               uint32_t adsr_offset, uint8_t srcn)
    : VGMRgn(instrument, adsr_offset, 2) {
  const uint16_t tuning = GetShortBE(tuning_offset);
  const uint8_t adsr1 = GetByte(adsr_offset) | 0x80;
  const uint8_t adsr2 = GetByte(adsr_offset + 1);

  double pitch_scale;
  if (tuning <= 0x7fff) {
    pitch_scale = 1.0 + (tuning / 32768.0);
  } else {
    pitch_scale = tuning / 65536.0;
  }

  double fine_tuning;
  double coarse_tuning;
  fine_tuning = modf((log(pitch_scale) / log(2.0)) * 12.0, &coarse_tuning);

  // normalize
  if (fine_tuning >= 0.5) {
    coarse_tuning += 1.0;
    fine_tuning -= 1.0;
  } else if (fine_tuning <= -0.5) {
    coarse_tuning -= 1.0;
    fine_tuning += 1.0;
  }

  sampNum = srcn;
  unityKey = static_cast<uint8_t>(72 - static_cast<int>(coarse_tuning));
  fineTune = static_cast<int16_t>(fine_tuning * 100.0);
  AddSimpleItem(adsr_offset, 1, L"ADSR1");
  AddSimpleItem(adsr_offset + 1, 1, L"ADSR2");
  SNESConvADSR<VGMRgn>(this, adsr1, adsr2, 0);
}
