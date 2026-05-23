/**
 * VGMTrans (c) - 2002-2025
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once
#include <memory>
#include <string_view>

#include "ModSourceMap.h"

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
  void setNumSequenceLoops(int numLoops);

  bool skipChannel10() const { return m_skip_channel_10; }
  void setSkipChannel10(bool should) { m_skip_channel_10 = should; }

  [[nodiscard]] ModSourceMap& modSourceMap(ModulationSourceTarget target);

  [[nodiscard]] ModulationSourceTarget midiModulationSourceTarget() const {
    return m_midi_modulation_source_target;
  }

  void setMidiModulationSourceTarget(ModulationSourceTarget target) {
    m_midi_modulation_source_target = target;
  }

  void load(OptionStore& store);
  void save(OptionStore& store) const;

private:
  ConversionOptions() = default;

  BankSelectStyle m_bs_style{BankSelectStyle::GS};
  int m_sequence_loops{0};
  bool m_skip_channel_10{true};
  ModSourceMap m_sf2_mod_sources{ModulationSourceTarget::SoundFont};
  ModSourceMap m_dls_mod_sources{ModulationSourceTarget::DLS};
  ModulationSourceTarget m_midi_modulation_source_target{ModulationSourceTarget::SoundFont};
};

// RAII helper which updates ConversionOptions::m_midi_modulation_source_target.
// Sets the active MIDI modulation-source target for SeqTrack controller emission,
// then restores the previous ConversionOptions value when the scope exits.
class ScopedMidiModulationSourceTarget {
public:
  explicit ScopedMidiModulationSourceTarget(ModulationSourceTarget target);
  ~ScopedMidiModulationSourceTarget();

  ScopedMidiModulationSourceTarget(const ScopedMidiModulationSourceTarget&) = delete;
  ScopedMidiModulationSourceTarget& operator=(const ScopedMidiModulationSourceTarget&) = delete;

private:
  ModulationSourceTarget m_previous;
};
