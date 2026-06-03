// Emulation of the OKI MSM6295 DAC is provided by the oki_adpcm_state
// class, which is adopted from the MAME project with gratitude. It is
// licensed under the BSD-3-Clause. The copyright holders are
// Andrew Gardner and Aaron Giles.
//
// The original code is available at:
//  https://github.com/mamedev/mame/blob/master/src/devices/sound/okiadpcm.h

#pragma once

#include "base/Types.h"
#include "VGMSamp.h"

#include <string>
#include <vector>

class VGMSampColl;
class VGMInstrSet;

// ======================> oki_adpcm_state

// Internal ADPCM state, used by external ADPCM generators with compatible specs to the OKIM 6295.
class oki_adpcm_state
{
public:
  oki_adpcm_state() { compute_tables(); reset(); }

  void reset();
  s16 clock(u8 nibble);
  s16 output() const { return m_signal; }
  void save();
  void restore();

  s32   m_signal;
  s32   m_step;
  s32   m_loop_signal;
  s32   m_loop_step;
  bool      m_saved;

private:
  static const s8 s_index_shift[8];
  static int s_diff_lookup[49*16];

  static void compute_tables();
  static bool s_tables_computed;
};

class DialogicAdpcmSamp
    : public VGMSamp {
public:

  DialogicAdpcmSamp(
      VGMSampColl *sampColl, u32 offset, u32 length, u32 rate, float gain, std::string name
  );
  ~DialogicAdpcmSamp() override;

  double compressionRatio() const override;

  float gain;

private:
  std::vector<u8> decodeToNativePcm() override;
  static oki_adpcm_state okiAdpcmState;
};
