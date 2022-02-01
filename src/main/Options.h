/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

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

  int GetNumSequenceLoops() { return m_sequence_loops; }
  void SetNumSequenceLoops(int numLoops) { m_sequence_loops = numLoops; }

private:
  ConversionOptions() = default;

  int m_sequence_loops{1};
};
