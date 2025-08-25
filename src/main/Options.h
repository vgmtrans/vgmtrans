/**
 * VGMTrans (c) - 2002-2025
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once
#include <memory>
#include <string_view>

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
  void setNumSequenceLoops(int numLoops) { m_sequence_loops = numLoops; }

  bool skipChannel10() const { return m_skip_channel_10; }
  void setSkipChannel10(bool should) { m_skip_channel_10 = should; }

  int minSequenceSize() const { return m_min_sequence_size; }
  void setMinSequenceSize(int size) { m_min_sequence_size = size; }

  void load(OptionStore& store) {
    auto g = store.beginGroup("ConversionOptions");
    const int bs = store.getInt("bankSelectStyle", static_cast<int>(BankSelectStyle::GS));
    m_bs_style = (bs == static_cast<int>(BankSelectStyle::MMA)) ? BankSelectStyle::MMA
                                                                : BankSelectStyle::GS;
    m_sequence_loops = store.getInt("sequenceLoops", 1);
    m_skip_channel_10 = store.getBool("skipChannel10", true);
    m_min_sequence_size = store.getInt("minSequenceSize", 0);
  }

  void save(OptionStore& store) const {
    auto g = store.beginGroup("ConversionOptions");
    store.setInt("bankSelectStyle", static_cast<int>(m_bs_style));
    store.setInt("sequenceLoops",   m_sequence_loops);
    store.setBool("skipChannel10", m_skip_channel_10);
    store.setInt("minSequenceSize", m_min_sequence_size);
  }

private:
  ConversionOptions() = default;

  BankSelectStyle m_bs_style{BankSelectStyle::GS};
  int m_sequence_loops{1};
  bool m_skip_channel_10{true};
  int m_min_sequence_size{0};
};
