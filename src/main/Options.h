/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

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

private:
  ConversionOptions() = default;

  BankSelectStyle m_bs_style{BankSelectStyle::GS};
  int m_sequence_loops{2};
};
