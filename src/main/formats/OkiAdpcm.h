// The oki_adpcm_state code comes from the MAME project. It is shared under the BSD-3-Clause license.
// The original code is available at: https://github.com/mamedev/mame/blob/a5579831cae73eb13c01b10d74666ee41a217887/src/devices/sound/okiadpcm.cpp
// The copyright holders are Andrew Gardner and Aaron Giles.

#pragma once

#include "common.h"
#include "VGMSamp.h"

class VGMSampColl;
class VGMInstrSet;

// ======================> oki_adpcm_state

// Internal ADPCM state, used by external ADPCM generators with compatible specs to the OKIM 6295.
class oki_adpcm_state
{
public:
  oki_adpcm_state() { compute_tables(); reset(); }

  void reset();
  int16_t clock(uint8_t nibble);
  int16_t output() { return m_signal; }
  void save();
  void restore();

  int32_t   m_signal;
  int32_t   m_step;
  int32_t   m_loop_signal;
  int32_t   m_loop_step;
  bool      m_saved;

private:
  static const int8_t s_index_shift[8];
  static int s_diff_lookup[49*16];

  static void compute_tables();
  static bool s_tables_computed;
};

class DailogicAdpcmSamp
    : public VGMSamp {
public:

  DailogicAdpcmSamp(
      VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t theRate, string name
  );
  virtual ~DailogicAdpcmSamp(void);

  virtual double GetCompressionRatio();
  virtual void ConvertToStdWave(uint8_t *buf);

private:
  static oki_adpcm_state okiAdpcmState;
};
