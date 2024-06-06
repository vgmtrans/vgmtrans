/*
 * VGMTrans (c) 2002-2019
 * VGMTransQt (c) 2020
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MP2kInstrSet.h"

#include <spdlog/fmt/fmt.h>

#include "MP2kFormat.h"
#include "VGMSampColl.h"
#include "VGMColl.h"
#include "VGMRgn.h"
#include "LogManager.h"

MP2kInstrSet::MP2kInstrSet(RawFile *file, int rate, size_t offset, int count,
                           const std::string &name)
    : VGMInstrSet(MP2kFormat::name, file, offset, count * 12, name), m_count(count),
      m_operating_rate(rate) {
  sampColl = new VGMSampColl(MP2kFormat::name, file, this, offset);
}

bool MP2kInstrSet::LoadInstrs() {
  bool res = VGMInstrSet::LoadInstrs();
  sampColl = nullptr;

  return res;
}

bool MP2kInstrSet::GetInstrPointers() {
  for (int i = 0; i < m_count; i++) {
    size_t cur_ofs = dwOffset + i * 12;
    MP2kInstrData data{rawFile()->get<u32>(cur_ofs), rawFile()->get<u32>(cur_ofs + 4),
                       rawFile()->get<u32>(cur_ofs + 8)};
    aInstrs.push_back(new MP2kInstr(this, cur_ofs, 0, 0, i, data));
  }

  return true;
}

int MP2kInstrSet::MakeOrGetSample(size_t sample_pointer) {
  if (sample_pointer == 0 || sample_pointer >= rawFile()->size()) {
    L_WARN("Invalid sample pointer {:#x}", sample_pointer);
    return -1;
  }

  if (auto elem = m_samples.find(sample_pointer); elem != m_samples.end()) {
    return elem->second;
  }

  /* First 3 bytes are unused */
  auto loop = rawFile()->getBE<u32>(sample_pointer);
  auto pitch = rawFile()->get<u32>(sample_pointer + 4);
  auto loop_pos = rawFile()->get<u32>(sample_pointer + 8);
  auto len = rawFile()->get<u32>(sample_pointer + 12);
  /* Filter out samples with invalid lengths */
  if (len < 16 || len > 0x3FFFFF) {
    return -1;
  }

  /* Rectify illegal loop point */
  if (loop_pos > len - 8) {
    L_WARN("Illegal loop positioning: {}", loop_pos);
    loop_pos = 0;
    loop = 0;
  }

  MP2kSamp *samp = new MP2kSamp(sampColl, loop == 0x01 ? MP2kWaveType::BDPCM : MP2kWaveType::PCM8,
                                sample_pointer, 16 + len, sample_pointer + 16, len);

  double delta_note = 12 * log2(m_operating_rate * 1024.0 / pitch);
  double int_delta_note = round(delta_note);
  unsigned int original_pitch = 60 + (int)int_delta_note;
  unsigned int pitch_correction = int((int_delta_note - delta_note) * 100);

  samp->unityKey = original_pitch;
  samp->fineTune = pitch_correction;
  samp->rate = m_operating_rate;
  samp->volume = 1;
  samp->bps = 8;
  samp->setName(fmt::format("{:#x}", sample_pointer));

  samp->SetLoopStartMeasure(LM_SAMPLES);
  samp->SetLoopLengthMeasure(LM_SAMPLES);
  if (loop == 0x40) {
    /* Uncompressed, looping */
    samp->SetLoopStatus(true);
    samp->SetLoopOffset(loop_pos);
    samp->SetLoopLength(len - loop_pos + 1);
  } else if (loop == 0x00) {
    /* Uncompressed, not looping */
    samp->SetLoopStatus(false);
  } else if (loop == 0x01) {
    /* Compressed sample, not looping */
    samp->SetLoopStatus(false);
  } else {
    /* Invalid loop, assume the whole thing is garbage */
    L_ERROR("Garbage loop data {}", loop);
    return -1;
  }

  sampColl->samples.emplace_back(samp);
  m_samples.insert(std::pair(sample_pointer, sampColl->samples.size() - 1));

  return sampColl->samples.size() - 1;
}

MP2kInstr::MP2kInstr(MP2kInstrSet *set, size_t offset, size_t length, u32 bank, u32 number,
                     MP2kInstrData data)
    : VGMInstr(set, offset, length, bank, number), m_type(data.w0 & 0xFF), m_data(data) {
}

bool MP2kInstr::LoadInstr() {
  /* Unused instrument marking */
  if (m_data.w0 == 0x3c01 && m_data.w1 == 0x02 && m_data.w2 == 0x0F0000) {
    return true;
  }

  switch (m_type) {
    /* Sampled instruments (single region) */
    case 0x00:
      [[fallthrough]];
    case 0x08:
      [[fallthrough]];
    case 0x10:
      [[fallthrough]];
    case 0x18:
      [[fallthrough]];
    case 0x20:
      [[fallthrough]];
    case 0x28:
      [[fallthrough]];
    case 0x30:
      [[fallthrough]];
    case 0x38: {
      setName("Single-region instrument");
      unLength = 12;

      bool no_resampling = (m_type & 0xFF) == 0x08;
      if (no_resampling) {
        L_WARN("Ignoring resampling shenaningans!");
      }

      size_t sample_pointer = m_data.w1 & 0x3FFFFFF;
      if (sample_pointer == 0) {
        /* Sometimes the instrument table can have weird values */
        L_INFO("Tried to load a sample that pointed to nothing for instr @{:#x}", dwOffset);
        setName(name() + " (invalid)");
        break;
      }

      if (auto sample_id =
              static_cast<MP2kInstrSet *>(parInstrSet)->MakeOrGetSample(sample_pointer);
          sample_id != -1) {
        VGMRgn *rgn = AddRgn(dwOffset, unLength, sample_id);
        // rgn->sampCollPtr = static_cast<MP2kInstrSet *>(parInstrSet)->sampColl;
        SetADSR(rgn, m_data.w2);
      } else {
        L_WARN("No sample could be loaded for {:#x}", sample_pointer);
        setName(name() + " (sample missing)");
      }

      break;
    }

    /* PSQ Square (GameBoy channel 2) */
    case 0x01:
      [[fallthrough]];
    case 0x02:
      [[fallthrough]];
    case 0x09:
      [[fallthrough]];
    case 0x0a: {
      static constexpr const char *cycles[] = {"12,5%", "25%", "50%", "75%"};
      setName(fmt::format("PSG square {}", m_data.w1 < 4 ? cycles[m_data.w1] : "???"));
      unLength = 12;
      break;
    }

    /* GameBoy channel 3 (programmable) */
    case 0x03:
      [[fallthrough]];
    case 0x0b: {
      setName("PSG programmable waveform");
      unLength = 12;
      break;
    }

    /* PSG Noise */
    case 0x04:
      [[fallthrough]];
    case 0x0c: {
      setName("PSG noise");
      unLength = 12;
      break;
    }

    /* Multi-region instrument */
    case 0x40: {
      setName("Multi-region instrument");
      unLength = 12;

      u32 base_pointer = m_data.w1 & 0x3ffffff;
      u32 region_table = m_data.w2 & 0x3ffffff;

      std::vector<int8_t> split_list, index_list;

      int prev_index = -1;
      int current_index;
      /* Scan the whole table for changes and keep track of splits */
      for (int key = 0; key < 128; key++) {
        int index = rawFile()->get<u8>(region_table + key);

        current_index = index;
        if (prev_index != current_index) {
          split_list.push_back(key);
          index_list.push_back(current_index);

          prev_index = current_index;
        }
      }

      /* Final entry for the last split */
      split_list.push_back(0x80);

      for (int i = 0; i < index_list.size(); i++) {
        u32 offset = base_pointer + 12 * index_list[i];

        auto type = rawFile()->get<u8>(offset);
        if (type & 0x07) {
          L_WARN("GameBoy instrument in key-split (what game is this?!)");
          continue;
        }

        u32 sample_pointer = rawFile()->get<u32>(offset + 4) & 0x3ffffff;
        if (sample_pointer == 0) {
          continue;
        }

        if (auto sample_id =
                static_cast<MP2kInstrSet *>(parInstrSet)->MakeOrGetSample(sample_pointer);
            sample_id != -1) {
          VGMRgn *rgn = AddRgn(dwOffset, unLength, sample_id, split_list[i], split_list[i + 1] - 1);

          // rgn->sampCollPtr = static_cast<MP2kInstrSet *>(parInstrSet)->sampColl;
          SetADSR(rgn, rawFile()->get<u32>(offset + 8));
        } else {
          continue;
        }
      }

      break;
    }

    /* Full-keyboard instrument */
    case 0x80: {
      setName("Full-keyboard instrument");
      unLength = 12;

      u32 base_pointer = m_data.w1 & 0x3ffffff;

      for (int key = 0; key < 128; key++) {
        u32 offset = base_pointer + 12 * key;

        u32 type = rawFile()->get<u8>(offset);
        u32 keynum = rawFile()->get<u8>(offset + 1);
        u32 pan = rawFile()->get<u8>(offset + 3);

        u32 sample_pointer = rawFile()->get<u32>(offset + 4) & 0x3ffffff;
        if (sample_pointer == 0) {
          continue;
        }

        if ((type & 0x0f) == 0 || (type & 0x0f) == 8) {
          if (auto sample_id =
                  static_cast<MP2kInstrSet *>(parInstrSet)->MakeOrGetSample(sample_pointer);
              sample_id != -1) {
            VGMRgn *rgn = AddRgn(dwOffset, unLength, sample_id, key, key);
            // rgn->sampCollPtr = static_cast<MP2kInstrSet *>(parInstrSet)->sampColl;
            rgn->SetPan(pan);

            u32 pitch = rawFile()->get<u32>(sample_pointer + 4);
            double delta_note = 12.0 * log2(static_cast<MP2kInstrSet *>(parInstrSet)->sampleRate() *
                                            1024.0 / pitch);
            int rootkey = 60 + int(round(delta_note));

            rgn->SetUnityKey(rootkey - keynum + key);
            SetADSR(rgn, rawFile()->get<u32>(offset + 8));
          }
        } else if ((type & 0x0f) == 4 || (type & 0x0f) == 12) {
          /* Make noise sample here... */
        }
      }
      break;
    }

    default: {
      L_WARN("Unhandled instrument type {}", m_type);
      return false;
    }
  }

  return true;
}

void MP2kInstr::SetADSR(VGMRgn *rgn, u32 adsr) {
  int attack = adsr & 0xFF;
  int decay = (adsr >> 8) & 0xFF;
  int sustain = (adsr >> 16) & 0xFF;
  int release = adsr >> 24;

  if (attack != 0xFF) {
    double att_time = (256 / 60.0) / attack;
    double att = 1200 * log2(att_time);

    rgn->attack_time = att_time;
  } else {
    L_INFO("Attack disabled {:#x}", this->dwOffset);
  }

  if (sustain != 0xFF) {
    double sus = 0;
    if (sustain != 0) {
      sus = 100 * log(256.0 / sustain);
    } else {
      sus = 1000;
    }
    rgn->sustain_time = sus;

    double dec_time = (log(256.0) / (log(256) - log(decay))) / 60.0;
    dec_time *= 10 / log(256);
    double dec = 1200 * log2(dec_time);
    rgn->decay_time = dec;
  } else {
    L_INFO("Sustain disabled {:#x}", this->dwOffset);
  }

  if (release != 0x00) {
    double rel_time = (log(256.0) / (log(256) - log(release))) / 60.0;
    double rel = 1200 * log2(rel_time);
    rgn->release_time = rel;
  } else {
    L_INFO("Release disabled {:#x}", this->dwOffset);
  }
}

MP2kSamp::MP2kSamp(VGMSampColl *sampColl, MP2kWaveType type, uint32_t offset, uint32_t length,
                   uint32_t dataOffset, uint32_t dataLength, uint8_t channels, uint16_t bps,
                   uint32_t rate, uint8_t waveType, std::string name)
    : VGMSamp(sampColl, offset, length, dataOffset, dataLength, channels, bps, rate, name),
      m_type(type){};

void MP2kSamp::ConvertToStdWave(u8 *buf) {
  switch (m_type) {
    case MP2kWaveType::PCM8: {
      GetBytes(dataOff, dataLength, buf);
      for (unsigned int i = 0; i < dataLength; i++) {
        buf[i] ^= 0x80;
      }
      break;
    }

    case MP2kWaveType::BDPCM: {
      static constexpr int8_t delta_lut[] = {0,   1,   4,   9,   16,  25, 36, 49,
                                             -64, -49, -36, -25, -16, -9, -4, -1};
      /*
       * A block consists of an initial signed 8 bit PCM byte
       * followed by 63 nibbles stored in 32 bytes.
       * The first of these bytes has a zero padded (unused) high nibble.
       * This makes up of a total block size of 65 (0x21) bytes each.
       *
       * Decoding works like this:
       * The initial byte can be directly read without decoding. Then each
       * next sample can be decoded by putting the nibble into the delta-lookup-table
       * and adding that value to the previously calculated sample
       * until the end of the block is reached.
       */

      unsigned int nblocks = dataLength / 64;  // 64 samples per block

      char(*data)[33] = new char[nblocks][33];
      GetBytes(dataOff, dataLength, data);

      for (unsigned int block = 0; block < nblocks; ++block) {
        int8_t sample = data[block][0];
        buf[64 * block] = sample << 8;
        sample += delta_lut[data[block][1] & 0xf];
        buf[64 * block + 1] = sample << 8;
        for (unsigned int j = 1; j < 32; ++j) {
          uint8_t d = data[block][j + 1];
          sample += delta_lut[d >> 4];
          buf[64 * block + 2 * j] = sample << 8;
          sample += delta_lut[d & 0xf];
          buf[64 * block + 2 * j + 1] = sample << 8;
        }
      }
      memset(buf + 64 * nblocks, 0,
             dataLength - 64 * nblocks);  // Remaining samples are always 0

      delete[] data;
      break;
    }
  }
}
