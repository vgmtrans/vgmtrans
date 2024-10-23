// Emulation of the OKI MSM6295 DAC is provided by the oki_adpcm_state
// class, which is adopted from the MAME project with gratitude. It is
// licensed under the BSD-3-Clause. The copyright holders are
// Andrew Gardner and Aaron Giles.
//
// The original code is available at:
//  https://github.com/mamedev/mame/blob/master/src/devices/sound/okiadpcm.cpp

#include <cmath>
#include "OkiAdpcm.h"

//**************************************************************************
//  ADPCM STATE HELPER
//**************************************************************************

// ADPCM state and tables
bool oki_adpcm_state::s_tables_computed = false;
const int8_t oki_adpcm_state::s_index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
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

int16_t oki_adpcm_state::clock(uint8_t nibble)
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
  static const int8_t nbl2bit[16][4] =
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

DialogicAdpcmSamp::DialogicAdpcmSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length,
                                     uint32_t theRate, float gain, std::string name)
    : VGMSamp(sampColl, offset, length, offset, length, 1, 16, theRate,
         std::move(name)), gain(gain) {}

DialogicAdpcmSamp::~DialogicAdpcmSamp() {}

double DialogicAdpcmSamp::compressionRatio() {
  return (16.0 / 4); // 4 bit samples converted up to 16 bit samples
}

void DialogicAdpcmSamp::convertToStdWave(uint8_t *buf) {

  const int16_t maxValue = std::numeric_limits<int16_t>::max();
  const int16_t minValue = std::numeric_limits<int16_t>::min();

  auto* uncompBuf = reinterpret_cast<int16_t*>(buf);

  DialogicAdpcmSamp::okiAdpcmState.reset();

  int sampleNum = 0;
  for (uint32_t off = dwOffset; off < (dwOffset + unLength); ++off) {
    uint8_t byte = readByte(off);

    for (int n = 0; n < 2; ++n) {
      uint8_t nibble = byte >> (((n & 1) << 2) ^ 4);
      int16_t sample = DialogicAdpcmSamp::okiAdpcmState.clock(nibble);
      int32_t amplifiedSample = static_cast<int32_t>(sample) * gain;
      sample = static_cast<int16_t>(
        std::clamp(amplifiedSample,
          static_cast<int32_t>(minValue),
          static_cast<int32_t>(maxValue)
        )
      );
      uncompBuf[sampleNum++] = sample;
    }
  }
}
