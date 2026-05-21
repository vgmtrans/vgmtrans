/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MP2kInstrSet.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <spdlog/fmt/fmt.h>

#include "MP2kFormat.h"
#include "VGMSampColl.h"
#include "VGMColl.h"
#include "VGMRgn.h"
#include "LogManager.h"
#include "PSGDSP.h"

namespace {
constexpr uint8_t kPsgSquareCount = 4;
constexpr uint8_t kPsgNoiseIndex = kPsgSquareCount;
constexpr uint8_t kPsgUnityKey = 69;
constexpr size_t kGbaRomPointerMask = 0x03FFFFFF;

double mp2kAttackTimeSeconds(int attack) {
  if (attack <= 0) {
    return 0.0;
  }
  constexpr double kEnvelopeTicksPerSecond = 60.0;
  constexpr double kEnvelopeMaxLevel = 256.0;
  return kEnvelopeMaxLevel / (kEnvelopeTicksPerSecond * attack);
}

double mp2kEnvelopeRateTimeSeconds(int rate) {
  if (rate <= 0) {
    return 0.0;
  }
  constexpr double kEnvelopeTicksPerSecond = 60.0;
  constexpr double kEnvelopeMaxLevel = 256.0;
  const double logMaxLevel = std::log(kEnvelopeMaxLevel);
  return logMaxLevel / (logMaxLevel - std::log(static_cast<double>(rate))) /
         kEnvelopeTicksPerSecond;
}

double mp2kDecayTimeSeconds(int decay) {
  constexpr double kEnvelopeMaxLevel = 256.0;
  return mp2kEnvelopeRateTimeSeconds(decay) * 10.0 / std::log(kEnvelopeMaxLevel);
}

double cgbEnvelopeRateTimeSeconds(int rate) {
  rate &= 0x07;
  if (rate == 0) {
    return 0.0;
  }
  constexpr double kEnvelopeClockHz = 64.0;
  constexpr double kEnvelopeMaxLevel = 15.0;
  return kEnvelopeMaxLevel * rate / kEnvelopeClockHz;
}

double cgbSustainLevel(int sustain) {
  if (sustain <= 0) {
    return 0.0;
  }
  constexpr double kEnvelopeMaxLevel = 15.0;
  if (sustain >= kEnvelopeMaxLevel) {
    return 1.0;
  }
  return sustain / kEnvelopeMaxLevel;
}

bool isGbaRomPointer(size_t pointer) {
  constexpr size_t kGbaRomAddressMask = 0x0E000000;
  constexpr size_t kGbaRomAddressMin = 0x08000000;
  constexpr size_t kGbaRomAddressMax = 0x0C000000;
  size_t address_space = pointer & kGbaRomAddressMask;
  return address_space >= kGbaRomAddressMin && address_space <= kGbaRomAddressMax;
}
}

MP2kInstrSet::MP2kInstrSet(RawFile *file, int rate, size_t offset, int count,
                           VGMSampColl *psg_samples, const std::string &name)
    : VGMInstrSet(MP2kFormat::name, file, offset, count * 12, name), m_count(count),
      m_operating_rate(rate), m_psg_samples(psg_samples) {
  sampColl = new VGMSampColl(MP2kFormat::name, file, this, offset);
}

bool MP2kInstrSet::loadInstrs() {
  auto instrs = std::move(aInstrs);
  aInstrs.clear();
  aInstrs.reserve(instrs.size());

  for (size_t i = 0; i < instrs.size(); i++) {
    VGMInstr *instr = instrs[i];
    if (!instr->loadInstr()) {
      delete instr;
      for (size_t j = i + 1; j < instrs.size(); j++) {
        delete instrs[j];
      }
      for (auto *loadedInstr : aInstrs) {
        delete loadedInstr;
      }
      aInstrs.clear();
      return false;
    }

    if (instr->regions().empty()) {
      delete instr;
    } else {
      aInstrs.push_back(instr);
    }
  }

  if (sampColl != nullptr && sampColl->samples.empty()) {
    delete sampColl;
    sampColl = nullptr;
  }

  return true;
}

bool MP2kInstrSet::parseInstrPointers() {
  for (int i = 0; i < m_count; i++) {
    size_t cur_ofs = offset() + i * 12;
    MP2kInstrData data{rawFile()->get<u32>(cur_ofs), rawFile()->get<u32>(cur_ofs + 4),
                       rawFile()->get<u32>(cur_ofs + 8)};
    aInstrs.push_back(new MP2kInstr(this, cur_ofs, 0, 0, i, data));
  }

  return true;
}

int MP2kInstrSet::makeOrGetSample(size_t sample_pointer) {
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
  samp->setAttenuation(0);
  samp->setBPS(BPS::PCM8);
  samp->setName(fmt::format("{:#x}", sample_pointer));

  samp->setLoopStartMeasure(LM_SAMPLES);
  samp->setLoopLengthMeasure(LM_SAMPLES);
  if (loop == 0x40) {
    /* Uncompressed, looping */
    samp->setLoopStatus(true);
    samp->setLoopOffset(loop_pos);
    samp->setLoopLength(len - loop_pos + 1);
  } else if (loop == 0x00) {
    /* Uncompressed, not looping */
    samp->setLoopStatus(false);
  } else if (loop == 0x01) {
    /* Compressed sample, not looping */
    samp->setLoopStatus(false);
  } else {
    /* Invalid loop, assume the whole thing is garbage */
    L_ERROR("Garbage loop data {}", loop);
    delete samp;
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

bool MP2kInstr::loadInstr() {
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
      setLength(12);

      bool no_resampling = (m_type & 0xFF) == 0x08;
      if (no_resampling) {
        L_WARN("Ignoring resampling shenaningans!");
      }

      if (m_data.w1 == 0 || !isGbaRomPointer(m_data.w1)) {
        /* Sometimes the instrument table can have weird values */
        break;
      }

      size_t sample_pointer = m_data.w1 & kGbaRomPointerMask;
      if (auto sample_id =
              static_cast<MP2kInstrSet *>(parInstrSet)->makeOrGetSample(sample_pointer);
          sample_id != -1) {
        VGMRgn *rgn = addRgn(offset(), length(), sample_id);
        // rgn->sampCollPtr = static_cast<MP2kInstrSet *>(parInstrSet)->sampColl;
        setADSR(rgn, m_data.w2);
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
      setLength(12);
      if (auto *psgColl = static_cast<MP2kInstrSet *>(parInstrSet)->psgSampColl(); psgColl) {
        uint8_t duty = static_cast<uint8_t>(m_data.w1 & 0x03);
        VGMRgn *rgn = addRgn(offset(), length(), duty);
        rgn->sampCollPtr = psgColl;
        rgn->setUnityKey(kPsgUnityKey);
        setCgbADSR(rgn, m_data.w2);
      }
      break;
    }

    /* GameBoy channel 3 (programmable) */
    case 0x03:
      [[fallthrough]];
    case 0x0b: {
      setName("PSG programmable waveform");
      setLength(12);
      if (auto *psgColl =
              static_cast<MP2kPSGColl *>(static_cast<MP2kInstrSet *>(parInstrSet)->psgSampColl());
          psgColl) {
        if (auto sample_id = psgColl->makeOrGetProgrammableWave(m_data.w1); sample_id != -1) {
          VGMRgn *rgn = addRgn(offset(), length(), sample_id);
          rgn->sampCollPtr = psgColl;
          rgn->setUnityKey(kPsgUnityKey);
          setCgbADSR(rgn, m_data.w2);
        } else {
          L_DEBUG("No programmable wave could be loaded for {:#x}", m_data.w1);
          setName(name() + " (wave missing)");
        }
      }
      break;
    }

    /* PSG Noise */
    case 0x04:
      [[fallthrough]];
    case 0x0c: {
      setName("PSG noise");
      setLength(12);
      if (auto *psgColl = static_cast<MP2kInstrSet *>(parInstrSet)->psgSampColl(); psgColl) {
        VGMRgn *rgn = addRgn(offset(), length(), kPsgNoiseIndex);
        rgn->sampCollPtr = psgColl;
        rgn->setUnityKey(kPsgUnityKey);
        setCgbADSR(rgn, m_data.w2);
      }
      break;
    }

    /* Multi-region instrument */
    case 0x40: {
      setName("Multi-region instrument");
      setLength(12);

      u32 base_pointer = m_data.w1 & 0x3ffffff;
      u32 region_table = m_data.w2 & 0x3ffffff;
      auto *psgColl =
          static_cast<MP2kPSGColl *>(static_cast<MP2kInstrSet *>(parInstrSet)->psgSampColl());

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
        u32 off = base_pointer + 12 * index_list[i];

        auto type = rawFile()->get<u8>(off);
        if (type & (0x40 | 0x80)) {
          L_DEBUG("Recursive split/rhythm instrument in key-split");
          continue;
        }

        u32 raw_sample_pointer = rawFile()->get<u32>(off + 4);
        u32 sample_pointer = raw_sample_pointer & 0x3ffffff;
        uint8_t cgb_type = type & 0x07;

        if (cgb_type == 0) {
          if (sample_pointer == 0 || !isGbaRomPointer(raw_sample_pointer)) {
            continue;
          }

          if (auto sample_id =
                  static_cast<MP2kInstrSet *>(parInstrSet)->makeOrGetSample(sample_pointer);
              sample_id != -1) {
            VGMRgn *rgn = addRgn(off, 12, sample_id, split_list[i], split_list[i + 1] - 1);

            // rgn->sampCollPtr = static_cast<MP2kInstrSet *>(parInstrSet)->sampColl;
            setADSR(rgn, rawFile()->get<u32>(off + 8));
          }
        } else if (cgb_type == 1 || cgb_type == 2) {
          if (psgColl) {
            uint8_t duty = static_cast<uint8_t>(raw_sample_pointer & 0x03);
            VGMRgn *rgn = addRgn(off, 12, duty, split_list[i], split_list[i + 1] - 1);
            rgn->sampCollPtr = psgColl;
            rgn->setUnityKey(kPsgUnityKey);
            setCgbADSR(rgn, rawFile()->get<u32>(off + 8));
          }
        } else if (cgb_type == 3) {
          if (psgColl) {
            if (auto sample_id = psgColl->makeOrGetProgrammableWave(raw_sample_pointer);
                sample_id != -1) {
              VGMRgn *rgn = addRgn(off, 12, sample_id, split_list[i], split_list[i + 1] - 1);
              rgn->sampCollPtr = psgColl;
              rgn->setUnityKey(kPsgUnityKey);
              setCgbADSR(rgn, rawFile()->get<u32>(off + 8));
            }
          }
        } else if (cgb_type == 4) {
          if (psgColl) {
            VGMRgn *rgn = addRgn(off, 12, kPsgNoiseIndex, split_list[i], split_list[i + 1] - 1);
            rgn->sampCollPtr = psgColl;
            rgn->setUnityKey(kPsgUnityKey);
            setCgbADSR(rgn, rawFile()->get<u32>(off + 8));
          }
        }
      }

      break;
    }

    /* Full-keyboard instrument */
    case 0x80: {
      setName("Full-keyboard instrument");
      setLength(12);

      u32 base_pointer = m_data.w1 & 0x3ffffff;

      for (int key = 0; key < 128; key++) {
        u32 off = base_pointer + 12 * key;

        u32 type = rawFile()->get<u8>(off);
        u32 keynum = rawFile()->get<u8>(off + 1);
        u32 pan = rawFile()->get<u8>(off + 3);

        u32 raw_sample_pointer = rawFile()->get<u32>(off + 4);
        u32 sample_pointer = raw_sample_pointer & 0x3ffffff;

        if ((type & 0x0f) == 0 || (type & 0x0f) == 8) {
          if (sample_pointer == 0 || !isGbaRomPointer(raw_sample_pointer)) {
            continue;
          }

          if (auto sample_id =
                  static_cast<MP2kInstrSet *>(parInstrSet)->makeOrGetSample(sample_pointer);
              sample_id != -1) {
            VGMRgn *rgn = addRgn(offset(), length(), sample_id, key, key);
            // rgn->sampCollPtr = static_cast<MP2kInstrSet *>(parInstrSet)->sampColl;
            rgn->setPan(pan);

            u32 pitch = rawFile()->get<u32>(sample_pointer + 4);
            double delta_note = 12.0 * log2(static_cast<MP2kInstrSet *>(parInstrSet)->sampleRate() *
                                            1024.0 / pitch);
            int rootkey = 60 + int(round(delta_note));

            rgn->setUnityKey(rootkey - keynum + key);
            setADSR(rgn, rawFile()->get<u32>(off + 8));
          }
        } else if ((type & 0x0f) == 3 || (type & 0x0f) == 11) {
          if (auto *psgColl = static_cast<MP2kPSGColl *>(
                  static_cast<MP2kInstrSet *>(parInstrSet)->psgSampColl());
              psgColl) {
            if (auto sample_id = psgColl->makeOrGetProgrammableWave(raw_sample_pointer);
                sample_id != -1) {
              VGMRgn *rgn = addRgn(offset(), length(), sample_id, key, key);
              rgn->sampCollPtr = psgColl;
              rgn->setPan(pan);
              rgn->setUnityKey(kPsgUnityKey);
              setCgbADSR(rgn, rawFile()->get<u32>(off + 8));
            }
          }
        } else if ((type & 0x0f) == 4 || (type & 0x0f) == 12) {
          if (auto *psgColl = static_cast<MP2kInstrSet *>(parInstrSet)->psgSampColl(); psgColl) {
            VGMRgn *rgn = addRgn(offset(), length(), kPsgNoiseIndex, key, key);
            rgn->sampCollPtr = psgColl;
            rgn->setPan(pan);
            rgn->setUnityKey(kPsgUnityKey);
            setCgbADSR(rgn, rawFile()->get<u32>(off + 8));
          }
        }
      }
      break;
    }

    default: {
      L_WARN("Unhandled instrument type {}", m_type);
      return false;
    }
  }

  if (!regions().empty()) {
    constexpr double kDefaultLfoFrequencyHz = 60.0 * 22.0 / 256.0;
    constexpr double kVibratoMaxDepthCents = 127.0 * 4.0 * 100.0 / 64.0;
    addStandardVibratoHandling(kVibratoMaxDepthCents, kDefaultLfoFrequencyHz,
                               kDefaultLfoFrequencyHz);
  }

  return true;
}

void MP2kInstr::setADSR(VGMRgn *rgn, u32 adsr) {
  int attack = adsr & 0xFF;
  int decay = (adsr >> 8) & 0xFF;
  int sustain = (adsr >> 16) & 0xFF;
  int release = adsr >> 24;
  constexpr double kEnvelopeMaxLevel = 256.0;

  if (attack != 0xFF) {
    rgn->attack_time = mp2kAttackTimeSeconds(attack);
  } else {
    L_INFO("Attack disabled {:#x}", this->offset());
  }

  if (sustain != 0xFF) {
    rgn->sustain_level = sustain / kEnvelopeMaxLevel;
    rgn->decay_time = mp2kDecayTimeSeconds(decay);
  } else {
    L_INFO("Sustain disabled {:#x}", this->offset());
  }

  if (release != 0x00) {
    rgn->release_time = mp2kEnvelopeRateTimeSeconds(release);
  } else {
    L_INFO("Release disabled {:#x}", this->offset());
  }
}

void MP2kInstr::setCgbADSR(VGMRgn *rgn, u32 adsr) {
  int attack = adsr & 0xFF;
  int decay = (adsr >> 8) & 0xFF;
  int sustain = (adsr >> 16) & 0xFF;
  int release = adsr >> 24;

  rgn->attack_time = cgbEnvelopeRateTimeSeconds(attack);
  rgn->decay_time = cgbEnvelopeRateTimeSeconds(decay);
  rgn->sustain_level = cgbSustainLevel(sustain);
  rgn->release_time = cgbEnvelopeRateTimeSeconds(release);
}

MP2kSamp::MP2kSamp(VGMSampColl *sampColl, MP2kWaveType type, uint32_t offset, uint32_t length,
                   uint32_t dataOffset, uint32_t dataLength, uint8_t channels, BPS bps,
                   uint32_t rate, uint8_t waveType, std::string name)
    : VGMSamp(sampColl, offset, length, dataOffset, dataLength, channels, bps, rate, name),
      m_type(type){};

std::vector<uint8_t> MP2kSamp::decodeToNativePcm() {
  std::vector<uint8_t> buf(uncompressedSize());
  switch (m_type) {
    case MP2kWaveType::PCM8: {
      readBytes(dataOff, dataLength, buf.data());
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

      auto data = std::make_unique<char[]>(dataLength);
      readBytes(dataOff, dataLength, data.get());
      auto blocks = reinterpret_cast<char(*)[33]>(data.get());

      for (unsigned int block = 0; block < nblocks; ++block) {
        int8_t sample = blocks[block][0];
        buf[64 * block] = sample << 8;
        sample += delta_lut[blocks[block][1] & 0xf];
        buf[64 * block + 1] = sample << 8;
        for (unsigned int j = 1; j < 32; ++j) {
          uint8_t d = blocks[block][j + 1];
          sample += delta_lut[d >> 4];
          buf[64 * block + 2 * j] = sample << 8;
          sample += delta_lut[d & 0xf];
          buf[64 * block + 2 * j + 1] = sample << 8;
        }
      }
      memset(buf.data() + 64 * nblocks, 0,
             dataLength - 64 * nblocks);  // Remaining samples are always 0
      break;
    }
  }

  return buf;
}

namespace {
constexpr uint8_t kCgbWaveRamBytes = 16;
constexpr uint8_t kCgbWaveRamSamples = kCgbWaveRamBytes * 2;

double psgDutyRatio(uint8_t dutyIndex) {
  constexpr double kDutyRatios[4] = {0.125, 0.25, 0.5, 0.75};
  return kDutyRatios[dutyIndex & 0x03];
}
}

MP2kPSGColl::MP2kPSGColl(RawFile *file, uint32_t sampleRate, uint32_t loopSamples)
    : VGMSampColl(MP2kFormat::name, file, 0, 0, "MP2k PSG samples"),
      m_sample_rate(sampleRate),
      m_loop_samples(loopSamples) {
}

bool MP2kPSGColl::parseSampleInfo() {
  constexpr const char* kDutyLabels[4] = {"12.5%", "25%", "50%", "75%"};
  for (uint8_t duty = 0; duty < kPsgSquareCount; duty++) {
    auto name = fmt::format("PSG square {}", kDutyLabels[duty]);
    auto *sample = new MP2kPSGSamp(this, duty, false, m_sample_rate, m_loop_samples, name);
    samples.push_back(sample);
  }

  samples.push_back(new MP2kPSGSamp(this, 0, true, m_sample_rate, m_loop_samples, "PSG noise"));
  return true;
}

int MP2kPSGColl::makeOrGetProgrammableWave(size_t wavePointer) {
  if (!isGbaRomPointer(wavePointer)) {
    L_DEBUG("Invalid programmable wave pointer {:#x}", wavePointer);
    return -1;
  }

  size_t wave_offset = wavePointer & kGbaRomPointerMask;
  if (wave_offset == 0 || wave_offset + kCgbWaveRamBytes > rawFile()->size()) {
    L_DEBUG("Invalid programmable wave pointer {:#x}", wavePointer);
    return -1;
  }

  if (auto elem = m_programmable_waves.find(wave_offset); elem != m_programmable_waves.end()) {
    return elem->second;
  }

  int sample_id = static_cast<int>(samples.size());
  auto name = fmt::format("PSG programmable wave {:#x}", wave_offset);
  constexpr uint32_t kCgbWaveSampleRate = kCgbWaveRamSamples * 440;
  samples.push_back(new MP2kPSGWaveSamp(this, wave_offset, kCgbWaveSampleRate, name));
  m_programmable_waves.emplace(wave_offset, sample_id);
  return sample_id;
}

MP2kPSGSamp::MP2kPSGSamp(VGMSampColl *sampColl, uint8_t dutyIndex, bool noise,
                         uint32_t sampleRate, uint32_t loopSamples, std::string name)
    : VGMSamp(sampColl, 0, loopSamples * sizeof(int16_t), 0, loopSamples * sizeof(int16_t), 1,
              BPS::PCM16, sampleRate, std::move(name)),
      m_duty_ratio(psgDutyRatio(dutyIndex)),
      m_noise(noise) {
  setLoopStatus(true);
  setLoopOffset(0);
  setLoopLength(loopSamples);
  setLoopStartMeasure(LM_SAMPLES);
  setLoopLengthMeasure(LM_SAMPLES);
  unityKey = kPsgUnityKey;
  ulUncompressedSize = loopSamples * bytesPerSample();
}

std::vector<uint8_t> MP2kPSGSamp::decodeToNativePcm() {
  if (m_noise) {
    return psg::synthesizeLfsrNoisePCM16(loopLength());
  }

  return psg::synthesizeBandLimitedPulsePCM16(m_duty_ratio, rate, loopLength());
}

MP2kPSGWaveSamp::MP2kPSGWaveSamp(VGMSampColl *sampColl, size_t wavePointer,
                                 uint32_t sampleRate, std::string name)
    : VGMSamp(sampColl, static_cast<uint32_t>(wavePointer), kCgbWaveRamBytes,
              static_cast<uint32_t>(wavePointer), kCgbWaveRamBytes, 1, BPS::PCM16, sampleRate,
              std::move(name)),
      m_wave_pointer(wavePointer) {
  setLoopStatus(true);
  setLoopOffset(0);
  setLoopLength(kCgbWaveRamSamples);
  setLoopStartMeasure(LM_SAMPLES);
  setLoopLengthMeasure(LM_SAMPLES);
  unityKey = kPsgUnityKey;
  ulUncompressedSize = kCgbWaveRamSamples * bytesPerSample();
}

std::vector<uint8_t> MP2kPSGWaveSamp::decodeToNativePcm() {
  std::vector<uint8_t> samples(kCgbWaveRamSamples * sizeof(int16_t));
  auto *output = reinterpret_cast<int16_t *>(samples.data());

  for (uint8_t i = 0; i < kCgbWaveRamBytes; i++) {
    uint8_t packed = readByte(m_wave_pointer + i);
    uint8_t high = packed >> 4;
    uint8_t low = packed & 0x0F;
    output[i * 2] = static_cast<int16_t>(high * 0x1111 - 0x8000);
    output[i * 2 + 1] = static_cast<int16_t>(low * 0x1111 - 0x8000);
  }

  return samples;
}
