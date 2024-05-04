// Emulation of the OKI MSM6295 DAC is provided by the oki_adpcm_state
// class, which is adopted from the MAME project with gratitude. It is
// licensed under the BSD-3-Clause. The copyright holders are
// Andrew Gardner and Aaron Giles.
//
// The original code is available at:
//  https://github.com/mamedev/mame/blob/master/src/devices/sound/okiadpcm.h

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

class DialogicAdpcmSamp
    : public VGMSamp {
public:

  DialogicAdpcmSamp(
      VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t theRate, std::string name
  );
  virtual ~DialogicAdpcmSamp(void);

  virtual double GetCompressionRatio();
  virtual void ConvertToStdWave(uint8_t *buf);

private:
  static oki_adpcm_state okiAdpcmState;
};
