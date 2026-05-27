// Emulation of the OKI MSM6295 DAC is provided by the oki_adpcm_state
// class, which is adopted from the MAME project with gratitude. It is
// licensed under the BSD-3-Clause. The copyright holders are
// Andrew Gardner and Aaron Giles.
//
// The original code is available at:
//  https://github.com/mamedev/mame/blob/master/src/devices/sound/okiadpcm.cpp

#include "base/Types.h"
#include <cmath>
#include "OkiAdpcm.h"

//**************************************************************************
//  ADPCM STATE HELPER
//**************************************************************************

// ADPCM state and tables
bool oki_adpcm_state::s_tables_computed = false;
const s8 oki_adpcm_state::s_index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
int oki_adpcm_state::s_diff_lookup[49*16];

//-------------------------------------------------
//  reset - reset the ADPCM state
//-------------------------------------------------

void oki_adpcm_state::reset()
{
  // reset the signal/step
  m_signal = m_loop_signal = 0;
  m_step = m_loop_step = 0;
  m_saved = false;
}


//-------------------------------------------------
//  clock - decode single nibble and update
//  ADPCM output
//-------------------------------------------------

s16 oki_adpcm_state::clock(u8 nibble)
{
  // update the signal
  m_signal += s_diff_lookup[m_step * 16 + (nibble & 15)];

  // clamp to the maximum
  if (m_signal > 2047)
    m_signal = 2047;
  else if (m_signal < -2048)
    m_signal = -2048;

  // adjust the step size and clamp
  m_step += s_index_shift[nibble & 7];
  if (m_step > 48)
    m_step = 48;
  else if (m_step < 0)
    m_step = 0;

  // return the signal
  return m_signal;
}


//-------------------------------------------------
//  save - save current ADPCM state to buffer
//-------------------------------------------------

void oki_adpcm_state::save()
{
  if (!m_saved)
  {
    m_loop_signal = m_signal;
    m_loop_step = m_step;
    m_saved = true;
  }
}


//-------------------------------------------------
//  restore - restore previous ADPCM state
//  from buffer
//-------------------------------------------------

void oki_adpcm_state::restore()
{
  m_signal = m_loop_signal;
  m_step = m_loop_step;
}


//-------------------------------------------------
//  compute_tables - precompute tables for faster
//  sound generation
//-------------------------------------------------

void oki_adpcm_state::compute_tables()
{
  // skip if we already did it
  if (s_tables_computed)
    return;
  s_tables_computed = true;

  // nibble to bit map
  static const s8 nbl2bit[16][4] =
      {
          { 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
          { 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
          {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
          {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
      };

  // loop over all possible steps
  for (int step = 0; step <= 48; step++)
  {
    // compute the step value
    int stepval = floor(16.0 * pow(11.0 / 10.0, static_cast<double>(step)));

    // loop over all nibbles and compute the difference
    for (int nib = 0; nib < 16; nib++)
    {
      s_diff_lookup[step*16 + nib] = nbl2bit[nib][0] *
                                       (stepval * nbl2bit[nib][1] +
                                        stepval/2 * nbl2bit[nib][2] +
                                        stepval/4 * nbl2bit[nib][3] +
                                        stepval/8);
    }
  }
}



//  *****************
//  DialogicAdpcmSamp
//  *****************

oki_adpcm_state DialogicAdpcmSamp::okiAdpcmState;

DialogicAdpcmSamp::DialogicAdpcmSamp(VGMSampColl *sampColl, u32 offset, u32 length,
                                     u32 theRate, float gain, std::string name)
    : VGMSamp(sampColl, offset, length, offset, length, 1, BPS::PCM16, theRate,
         std::move(name)), gain(gain) {}

DialogicAdpcmSamp::~DialogicAdpcmSamp() {}

double DialogicAdpcmSamp::compressionRatio() const {
  return (16.0 / 4); // 4 bit samples converted up to 16 bit samples
}

std::vector<u8> DialogicAdpcmSamp::decodeToNativePcm() {
  const s16 maxValue = std::numeric_limits<s16>::max();
  const s16 minValue = std::numeric_limits<s16>::min();

  const u32 sampleCount = uncompressedSize() / sizeof(s16);
  std::vector<u8> samples(sampleCount * sizeof(s16));
  auto* uncompBuf = reinterpret_cast<s16*>(samples.data());

  DialogicAdpcmSamp::okiAdpcmState.reset();

  int sampleNum = 0;
  for (u32 off = offset(); off < (offset() + length()); ++off) {
    u8 byte = readByte(off);

    for (int n = 0; n < 2; ++n) {
      u8 nibble = byte >> (((n & 1) << 2) ^ 4);
      s16 sample = DialogicAdpcmSamp::okiAdpcmState.clock(nibble);
      s32 amplifiedSample = static_cast<s32>(sample) * gain;
      sample = static_cast<s16>(
        std::clamp(amplifiedSample,
          static_cast<s32>(minValue),
          static_cast<s32>(maxValue)
        )
      );
      uncompBuf[sampleNum++] = sample;
    }
  }

  return samples;
}
