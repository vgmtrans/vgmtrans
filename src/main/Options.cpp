#include "Options.h"

#include <algorithm>

void ConversionOptions::setNumSequenceLoops(int numLoops) {
  m_sequence_loops = std::clamp(numLoops, 0, kMaxSequenceLoops);
}

ModSourceMap& ConversionOptions::modSourceMap(ModulationSourceTarget target) {
  return target == ModulationSourceTarget::DLS ? m_dls_mod_sources : m_sf2_mod_sources;
}

const ModSourceMap& ConversionOptions::modSourceMap(ModulationSourceTarget target) const {
  return target == ModulationSourceTarget::DLS ? m_dls_mod_sources : m_sf2_mod_sources;
}

void ConversionOptions::load(OptionStore& store) {
  auto g = store.beginGroup("ConversionOptions");
  const int bs = store.getInt("bankSelectStyle", static_cast<int>(BankSelectStyle::GS));
  m_bs_style = (bs == static_cast<int>(BankSelectStyle::MMA)) ? BankSelectStyle::MMA
                                                              : BankSelectStyle::GS;
  m_sequence_loops = std::clamp(store.getInt("sequenceLoops", 1), 0, kMaxSequenceLoops);
  m_skip_channel_10 = store.getBool("skipChannel10", true);
  m_sf2_mod_sources.load(store, ModulationSourceTarget::SoundFont);
  m_dls_mod_sources.load(store, ModulationSourceTarget::DLS);
}

void ConversionOptions::save(OptionStore& store) const {
  auto g = store.beginGroup("ConversionOptions");
  store.setInt("bankSelectStyle", static_cast<int>(m_bs_style));
  store.setInt("sequenceLoops", m_sequence_loops);
  store.setBool("skipChannel10", m_skip_channel_10);
  m_sf2_mod_sources.save(store, ModulationSourceTarget::SoundFont);
  m_dls_mod_sources.save(store, ModulationSourceTarget::DLS);
}
