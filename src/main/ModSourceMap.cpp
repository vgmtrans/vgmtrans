/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ModSourceMap.h"
#include "Options.h"

namespace {

struct ModSourceSettingSpec {
  ModDest destination;
  const char* sf2Key;
  ModSource sf2Default;
  const char* dlsKey;
  ModSource dlsDefault;
};

constexpr std::array<ModSourceSettingSpec, kModDests.size()> kModSourceSettings {{
    {
        ModDest::VibLfoToPitch,
        "sf2ModSourceVibLfoToPitch",
        ModSource::ModWheel,
        "dlsModSourceVibLfoToPitch",
        ModSource::ModWheel,
    },
    {
        ModDest::VibLfoFreq,
        "sf2ModSourceVibLfoFreq",
        ModSource::VibratoRate,
        "dlsModSourceVibLfoFreq",
        ModSource::ChannelPressure,
    },
    {
        ModDest::VibLfoDelay,
        "sf2ModSourceVibLfoDelay",
        ModSource::VibratoDelay,
        "dlsModSourceVibLfoDelay",
        ModSource::ChorusSend,
    },
    {
        ModDest::ModLfoToVol,
        "sf2ModSourceModLfoToVol",
        ModSource::TremoloDepth,
        "dlsModSourceModLfoToVol",
        ModSource::ChorusSend,
    },
    {
        ModDest::ModLfoFreq,
        "sf2ModSourceModLfoFreq",
        ModSource::SoundController6,
        "dlsModSourceModLfoFreq",
        ModSource::ChannelPressure,
    },
    {
        ModDest::ModLfoDelay,
        "sf2ModSourceModLfoDelay",
        ModSource::SoundController10,
        "dlsModSourceModLfoDelay",
        ModSource::ReverbSend,
    },
    {
        ModDest::InitialAtten,
        "sf2ModSourceInitialAtten",
        ModSource::TremoloDepth,
        "dlsModSourceInitialAtten",
        ModSource::ChorusSend,
    },
}};

constexpr const char* settingKeyFor(const ModSourceSettingSpec& spec, SynthTarget target) {
  return target == SynthTarget::DLS ? spec.dlsKey : spec.sf2Key;
}

constexpr ModSource defaultSourceFor(const ModSourceSettingSpec& spec, SynthTarget target) {
  return target == SynthTarget::DLS ? spec.dlsDefault : spec.sf2Default;
}

constexpr int settingValueFor(ModSource source) {
  return static_cast<int>(source);
}

constexpr ModSource sourceFromSettingValue(int value, ModSource fallback) {
  if (value == static_cast<int>(ModSource::None) ||
      (value >= 0 && value <= static_cast<int>(ModSource::PitchWheel))) {
    return static_cast<ModSource>(value);
  }
  return fallback;
}

}  // namespace

ModSourceMap::ModSourceMap(SynthTarget target) {
  resetToDefaults(target);
}

void ModSourceMap::resetToDefaults(SynthTarget target) {
  for (const auto& spec : kModSourceSettings) {
    m_sources[modDestIndex(spec.destination)] = defaultSourceFor(spec, target);
  }
}

void ModSourceMap::load(OptionStore& store, SynthTarget target) {
  resetToDefaults(target);
  for (const auto& spec : kModSourceSettings) {
    const auto index = modDestIndex(spec.destination);
    m_sources[index] = sourceFromSettingValue(
        store.getInt(settingKeyFor(spec, target), settingValueFor(m_sources[index])),
        m_sources[index]);
  }
}

void ModSourceMap::save(OptionStore& store, SynthTarget target) const {
  for (const auto& spec : kModSourceSettings) {
    store.setInt(settingKeyFor(spec, target),
                 settingValueFor(m_sources[modDestIndex(spec.destination)]));
  }
}

ModSource ModSourceMap::sourceFor(ModDest destination) const {
  return m_sources[modDestIndex(destination)];
}

void ModSourceMap::setSourceFor(ModDest destination, ModSource source) {
  m_sources[modDestIndex(destination)] = source;
}
