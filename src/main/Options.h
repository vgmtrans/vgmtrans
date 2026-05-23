/**
 * VGMTrans (c) - 2002-2025
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

#include "Modulation.h"

struct OptionStore {
  struct Group {
    virtual ~Group() = default;
  };

  virtual ~OptionStore() = default;

  // Grouping, RAII-style so callers can't forget endGroup()
  virtual std::unique_ptr<Group> beginGroup(std::string_view path) = 0;

  virtual int getInt(std::string_view key, int def) const = 0;
  virtual void setInt(std::string_view key, int value) = 0;
  virtual int getBool(std::string_view key, bool def) const = 0;
  virtual void setBool(std::string_view key, bool value) = 0;
};

enum class BankSelectStyle {
  /* CC0 MSB (default) */
  GS,
  /* CC0 * 128 + CC32 */
  MMA
};

class ConversionOptions {
public:
  static constexpr int kMaxSequenceLoops = 100;

  static auto &the() {
    static ConversionOptions instance;
    return instance;
  }

  ConversionOptions(const ConversionOptions &) = delete;
  ConversionOptions &operator=(const ConversionOptions &) = delete;
  ConversionOptions(ConversionOptions &&) = delete;
  ConversionOptions &operator=(ConversionOptions &&) = delete;

  ~ConversionOptions() = default;

  BankSelectStyle bankSelectStyle() const { return m_bs_style; }
  void setBankSelectStyle(BankSelectStyle style) { m_bs_style = style; }

  int numSequenceLoops() const { return m_sequence_loops; }
  void setNumSequenceLoops(int numLoops) {
    m_sequence_loops = std::clamp(numLoops, 0, kMaxSequenceLoops);
  }

  bool skipChannel10() const { return m_skip_channel_10; }
  void setSkipChannel10(bool should) { m_skip_channel_10 = should; }

  [[nodiscard]] ModSource modSourceFor(ModulationSourceTarget target, ModDest destination) const {
    return modSourcesForTarget(target)[modDestIndex(destination)];
  }

  void setModSourceFor(ModulationSourceTarget target, ModDest destination, ModSource source) {
    modSourcesForTarget(target)[modDestIndex(destination)] = source;
  }

  [[nodiscard]] ModulationSourceTarget midiModulationSourceTarget() const {
    return m_midi_modulation_source_target;
  }

  void setMidiModulationSourceTarget(ModulationSourceTarget target) {
    m_midi_modulation_source_target = target;
  }

  class ScopedMidiModulationSourceTarget {
  public:
    explicit ScopedMidiModulationSourceTarget(ModulationSourceTarget target)
        : m_previous(ConversionOptions::the().midiModulationSourceTarget()) {
      ConversionOptions::the().setMidiModulationSourceTarget(target);
    }

    ~ScopedMidiModulationSourceTarget() {
      ConversionOptions::the().setMidiModulationSourceTarget(m_previous);
    }

    ScopedMidiModulationSourceTarget(const ScopedMidiModulationSourceTarget&) = delete;
    ScopedMidiModulationSourceTarget& operator=(const ScopedMidiModulationSourceTarget&) = delete;

  private:
    ModulationSourceTarget m_previous;
  };

  void load(OptionStore& store) {
    auto g = store.beginGroup("ConversionOptions");
    const int bs = store.getInt("bankSelectStyle", static_cast<int>(BankSelectStyle::GS));
    m_bs_style = (bs == static_cast<int>(BankSelectStyle::MMA)) ? BankSelectStyle::MMA
                                                                : BankSelectStyle::GS;
    m_sequence_loops = std::clamp(store.getInt("sequenceLoops", 1), 0, kMaxSequenceLoops);
    m_skip_channel_10 = store.getBool("skipChannel10", true);

    m_sf2_mod_sources = defaultModSources(ModulationSourceTarget::SoundFont);
    m_dls_mod_sources = defaultModSources(ModulationSourceTarget::DLS);
    for (const ModDest destination : kModDests) {
      const auto index = modDestIndex(destination);
      m_sf2_mod_sources[index] = modSourceFromSettingValue(
          store.getInt(modSourceSettingKey(ModulationSourceTarget::SoundFont, destination),
                       modSourceSettingValue(m_sf2_mod_sources[index])),
          m_sf2_mod_sources[index]);
      m_dls_mod_sources[index] = modSourceFromSettingValue(
          store.getInt(modSourceSettingKey(ModulationSourceTarget::DLS, destination),
                       modSourceSettingValue(m_dls_mod_sources[index])),
          m_dls_mod_sources[index]);
    }
  }

  void save(OptionStore& store) const {
    auto g = store.beginGroup("ConversionOptions");
    store.setInt("bankSelectStyle", static_cast<int>(m_bs_style));
    store.setInt("sequenceLoops",   m_sequence_loops);
    store.setBool("skipChannel10", m_skip_channel_10);
    for (const ModDest destination : kModDests) {
      store.setInt(modSourceSettingKey(ModulationSourceTarget::SoundFont, destination),
                   modSourceSettingValue(modSourceFor(ModulationSourceTarget::SoundFont, destination)));
      store.setInt(modSourceSettingKey(ModulationSourceTarget::DLS, destination),
                   modSourceSettingValue(modSourceFor(ModulationSourceTarget::DLS, destination)));
    }
  }

private:
  ConversionOptions() = default;

  using ModSourceMap = std::array<ModSource, kModDests.size()>;

  static constexpr ModSourceMap defaultModSources(ModulationSourceTarget target) {
    if (target == ModulationSourceTarget::DLS) {
      return {
          ModSource::ModWheel,
          ModSource::ChannelPressure,
          ModSource::ChorusSend,
          ModSource::ChorusSend,
          ModSource::ChannelPressure,
          ModSource::ReverbSend,
          ModSource::ChorusSend,
      };
    }

    return {
        ModSource::ModWheel,
        ModSource::VibratoRate,
        ModSource::VibratoDelay,
        ModSource::TremoloDepth,
        ModSource::SoundController6,
        ModSource::SoundController10,
        ModSource::TremoloDepth,
    };
  }

  static constexpr const char* modSourceSettingKey(ModulationSourceTarget target, ModDest destination) {
    switch (target) {
      case ModulationSourceTarget::SoundFont:
        switch (destination) {
          case ModDest::VibLfoToPitch:
            return "sf2ModSourceVibLfoToPitch";
          case ModDest::VibLfoFreq:
            return "sf2ModSourceVibLfoFreq";
          case ModDest::VibLfoDelay:
            return "sf2ModSourceVibLfoDelay";
          case ModDest::ModLfoToVol:
            return "sf2ModSourceModLfoToVol";
          case ModDest::ModLfoFreq:
            return "sf2ModSourceModLfoFreq";
          case ModDest::ModLfoDelay:
            return "sf2ModSourceModLfoDelay";
          case ModDest::InitialAtten:
            return "sf2ModSourceInitialAtten";
        }
        break;
      case ModulationSourceTarget::DLS:
        switch (destination) {
          case ModDest::VibLfoToPitch:
            return "dlsModSourceVibLfoToPitch";
          case ModDest::VibLfoFreq:
            return "dlsModSourceVibLfoFreq";
          case ModDest::VibLfoDelay:
            return "dlsModSourceVibLfoDelay";
          case ModDest::ModLfoToVol:
            return "dlsModSourceModLfoToVol";
          case ModDest::ModLfoFreq:
            return "dlsModSourceModLfoFreq";
          case ModDest::ModLfoDelay:
            return "dlsModSourceModLfoDelay";
          case ModDest::InitialAtten:
            return "dlsModSourceInitialAtten";
        }
        break;
    }
    return "";
  }

  static constexpr int modSourceSettingValue(ModSource source) {
    return static_cast<int>(source);
  }

  static constexpr ModSource modSourceFromSettingValue(int value, ModSource fallback) {
    if (value == static_cast<int>(ModSource::None) ||
        (value >= 0 && value <= static_cast<int>(ModSource::PitchWheel))) {
      return static_cast<ModSource>(value);
    }
    return fallback;
  }

  ModSourceMap& modSourcesForTarget(ModulationSourceTarget target) {
    return target == ModulationSourceTarget::DLS ? m_dls_mod_sources : m_sf2_mod_sources;
  }

  [[nodiscard]] const ModSourceMap& modSourcesForTarget(ModulationSourceTarget target) const {
    return target == ModulationSourceTarget::DLS ? m_dls_mod_sources : m_sf2_mod_sources;
  }

  BankSelectStyle m_bs_style{BankSelectStyle::GS};
  int m_sequence_loops{0};
  bool m_skip_channel_10{true};
  ModSourceMap m_sf2_mod_sources{defaultModSources(ModulationSourceTarget::SoundFont)};
  ModSourceMap m_dls_mod_sources{defaultModSources(ModulationSourceTarget::DLS)};
  ModulationSourceTarget m_midi_modulation_source_target{ModulationSourceTarget::SoundFont};
};
