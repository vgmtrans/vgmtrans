/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MP2kInstrSet.h"

#include "base/Types.h"
#include "LogManager.h"
#include "MP2kFormat.h"
#include "PSGDSP.h"
#include "VGMColl.h"
#include "VGMRgn.h"
#include "VGMSampColl.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <utility>

#include <spdlog/fmt/fmt.h>

namespace {
constexpr u8 kPsgSquareCount = 4;
constexpr u8 kPsgNoiseIndex = kPsgSquareCount;
constexpr u8 kPsgUnityKey = 69;
constexpr size_t kGbaRomPointerMask = 0x03FFFFFF;
constexpr double kGbaFrameRateHz = 16777216.0 / 280896.0;
constexpr double kBonWaveSamplesPerCycle = 64.0;
constexpr u32 kBonPwmLoopFrames = 256;
constexpr u32 kBonPwmPhaseSteps = 256;
constexpr u32 kBonSawPrerollSamples = 64;
constexpr double kBonPeriodicLoopSeconds = 2.0;

// Tone records contain a channel number plus independent playback and bank flags.
enum class VoiceType : u8 {
  Pcm = 0,
  Square1 = 1,
  Square2 = 2,
  Wave = 3,
  Noise = 4,
  KeySplit = 0x40,
  Rhythm = 0x80,
  Unsupported = 0xff,
};

constexpr u8 kVoiceChannelMask = 0x07;
constexpr u8 kFixedPitchFlag = 0x08;
constexpr u8 kPcmModeFlags = 0x30;
constexpr u8 kKeySplitFlag = 0x40;
constexpr u8 kRhythmFlag = 0x80;
constexpr u8 kRhythmPanOverrideFlag = 0x80;
constexpr u32 kSquareDutyMask = 0x03;
constexpr u32 kNoiseShortModeFlag = 0x01;
constexpr u32 kToneRecordSize = 12;
constexpr int kMidiKeyCount = 128;

constexpr VoiceType voiceChannel(u8 type) {
  return static_cast<VoiceType>(type & kVoiceChannelMask);
}

constexpr bool isNestedBank(u8 type) {
  return (type & (kKeySplitFlag | kRhythmFlag)) != 0;
}

constexpr VoiceType instrumentVoice(u8 type) {
  if (type == kKeySplitFlag) {
    return VoiceType::KeySplit;
  }
  if (type == kRhythmFlag) {
    return VoiceType::Rhythm;
  }

  const auto voice = voiceChannel(type);
  const u8 allowedFlags = voice == VoiceType::Pcm ? kFixedPitchFlag | kPcmModeFlags : kFixedPitchFlag;
  if ((type & ~(kVoiceChannelMask | allowedFlags)) != 0) {
    return VoiceType::Unsupported;
  }
  return voice;
}

constexpr u32 romOffset(u32 pointer) {
  return pointer & kGbaRomPointerMask;
}

struct Tone {
  u8 type;
  u8 key;
  u8 pan;
  u32 sampleOrTable;

  constexpr VoiceType voice() const { return voiceChannel(type); }
  constexpr bool fixedPitch() const { return (type & kFixedPitchFlag) != 0; }
  constexpr u8 squareDuty() const { return static_cast<u8>(sampleOrTable & kSquareDutyMask); }
  constexpr bool shortNoise() const { return (sampleOrTable & kNoiseShortModeFlag) != 0; }
  constexpr u32 sampleOffset() const { return romOffset(sampleOrTable); }

  void applyRhythmPan(VGMRgn* region) const {
    // Preserve the existing raw pan conversion when the tone requests an override.
    if ((pan & kRhythmPanOverrideFlag) != 0) {
      region->setPan(pan);
    }
  }
};

constexpr Tone decodeTone(MP2kInstrData data) {
  return {static_cast<u8>(data.w0), static_cast<u8>(data.w0 >> 8),
          static_cast<u8>(data.w0 >> 24), data.w1};
}

Tone readTone(const RawFile* file, u32 offset, u8 type) {
  return {type, file->get<u8>(offset + 1), file->get<u8>(offset + 3),
          file->get<u32>(offset + 4)};
}

struct Envelope {
  int attack;
  int decay;
  int sustain;
  int release;
};

constexpr Envelope decodeEnvelope(u32 data) {
  return {static_cast<u8>(data), static_cast<u8>(data >> 8),
          static_cast<u8>(data >> 16), static_cast<u8>(data >> 24)};
}

double mp2kAttackTimeSeconds(int attack) {
  if (attack <= 0) {
    return 0.0;
  }
  constexpr double kEnvelopeMaxLevel = 256.0;
  return kEnvelopeMaxLevel / (kGbaFrameRateHz * attack);
}

double mp2kEnvelopeRateTimeSeconds(int rate) {
  if (rate <= 0) {
    return 0.0;
  }
  constexpr double kEnvelopeMaxLevel = 256.0;
  // Each frame multiplies amplitude by rate/256. SoundFont decay/release times
  // describe a full 100 dB slope, rather than the time to reach one 8-bit unit.
  constexpr double kLogAmplitudeRange = 5.0 * std::numbers::ln10;
  return kLogAmplitudeRange / (kGbaFrameRateHz * std::log(kEnvelopeMaxLevel / rate));
}

double cgbEnvelopeRateTimeSeconds(int rate) {
  constexpr int kCgbEnvelopeRateMask = 0x07;
  rate &= kCgbEnvelopeRateMask;
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
                           MP2kPSGColl *psg_samples, MP2kDriverVariant variant, const std::string &name)
    : VGMInstrSet(MP2kFormat::name, file, offset, count * 12, name), m_count(count),
    m_operating_rate(rate), m_variant(variant), m_psg_samples(psg_samples) {
  assert(m_psg_samples != nullptr);
  addSampColl<VGMSampColl>(MP2kFormat::name, file, this, offset);
}

MP2kPSGColl& MP2kInstrSet::psgSampColl() const noexcept {
  assert(m_psg_samples != nullptr);
  return *m_psg_samples;
}

bool MP2kInstrSet::loadInstrs() {
  auto instrs = releaseInstrs();
  reserveInstrs(instrs.size());

  for (size_t i = 0; i < instrs.size(); i++) {
    auto instr = std::move(instrs[i]);
    if (!instr->loadInstr()) {
      L_ERROR("Failed to load instrument #{} ({}), cannot load bank {} at offset {:#x}", i, instr->name(), name(), offset());
      clearInstrs();
      return false;
    }

    if (instr->regions().empty()) {
      continue;
    } else {
      sinkInstr(std::move(instr));
    }
  }

  if (sampColl() != nullptr && !sampColl()->hasSamples()) {
    clearSampColl();
  }

  return true;
}

bool MP2kInstrSet::parseInstrPointers() {
  for (int i = 0; i < m_count; i++) {
    size_t cur_ofs = offset() + i * 12;
    MP2kInstrData data{rawFile()->get<u32>(cur_ofs), rawFile()->get<u32>(cur_ofs + 4),
                       rawFile()->get<u32>(cur_ofs + 8)};
    addInstr<MP2kInstr>(this, cur_ofs, 0, 0, i, data);
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

  auto* sampleColl = sampColl();
  auto samp = std::make_unique<MP2kSamp>(sampleColl, MP2kSampParams{
      .type = loop == 0x01 ? MP2kWaveType::BDPCM : MP2kWaveType::PCM8,
      .offset = static_cast<u32>(sample_pointer),
      .length = 16 + len,
      .dataOffset = static_cast<u32>(sample_pointer + 16),
      .dataLength = len,
  });

  double delta_note = 12 * log2(m_operating_rate * 1024.0 / pitch);
  double int_delta_note = round(delta_note);
  int original_pitch = 60 + static_cast<int>(int_delta_note);
  int pitch_correction = static_cast<int>((int_delta_note - delta_note) * 100);

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
    return -1;
  }

  const auto sample_id = sampleColl->sampleCount();
  sampleColl->sinkSamp(std::move(samp));
  m_samples.emplace(sample_pointer, sample_id);

  return static_cast<int>(sample_id);
}

bool MP2kInstrSet::isBonSample(size_t pointer) const {
  if (m_variant != MP2kDriverVariant::Bon || pointer > rawFile()->size() ||
      rawFile()->size() - pointer < 24) {
    return false;
  }
  // Bon stores oscillator descriptors in looping WaveData records with no PCM payload.
  return rawFile()->getBE<u32>(pointer) == 0x40 &&
         rawFile()->get<u32>(pointer + 8) == 0 &&
         rawFile()->get<u32>(pointer + 12) == 0 &&
         rawFile()->get<u32>(pointer + 4) != 0 &&
         rawFile()->get<u8>(pointer + 16) == 0x80 &&
         rawFile()->get<u8>(pointer + 17) <= static_cast<u8>(MP2kBonSamp::Shape::Triangle);
}

int MP2kInstrSet::makeOrGetBonSample(size_t pointer, u8 key) {
  if (key >= kMidiKeyCount || !isBonSample(pointer) || m_operating_rate <= 0) {
    return -1;
  }
  const auto cacheKey = std::make_pair(pointer, key);
  if (const auto found = m_bon_samples.find(cacheKey); found != m_bon_samples.end()) {
    return found->second;
  }
  const auto shape = static_cast<MP2kBonSamp::Shape>(rawFile()->get<u8>(pointer + 17));
  const double middleCFrequency = rawFile()->get<u32>(pointer + 4) / (1024.0 * kBonWaveSamplesPerCycle);
  const double frequency = middleCFrequency * std::exp2((static_cast<int>(key) - 60) / 12.0);
  // Sample each key separately to preserve the frame-clock PWM speed across the keyboard.
  const double seconds = shape == MP2kBonSamp::Shape::Pulse
                             ? kBonPwmLoopFrames / kGbaFrameRateHz : kBonPeriodicLoopSeconds;
  const auto count = std::lround(m_operating_rate * seconds);
  const auto cycles = std::max(1LL, std::llround(frequency * count / m_operating_rate));
  if (std::cmp_less_equal(count, kBonSawPrerollSamples) ||
      std::cmp_greater(count, std::numeric_limits<u32>::max()) ||
      std::cmp_greater(cycles, std::numeric_limits<u32>::max())) {
    return -1;
  }
  const double generatedFrequency = static_cast<double>(cycles) * m_operating_rate / count;
  const auto correction = std::lround(1200.0 * std::log2(frequency / generatedFrequency));
  // Root-key overrides preserve the fractional-semitone sample correction in both exporters.
  if (correction < -99 || correction > 99) {
    return -1;
  }
  const MP2kBonSamp::RenderSettings settings{
      .shape = shape,
      .pwm = {rawFile()->get<u8>(pointer + 18), rawFile()->get<u8>(pointer + 19),
              rawFile()->get<u8>(pointer + 20), rawFile()->get<u8>(pointer + 21)},
      .key = key,
      .sampleRate = static_cast<u32>(m_operating_rate),
      .loopSamples = static_cast<u32>(count),
      .carrierCycles = static_cast<u32>(cycles),
      .fineTune = static_cast<s16>(correction),
  };
  const int sampleId = static_cast<int>(sampColl()->sampleCount());
  sampColl()->sinkSamp(std::make_unique<MP2kBonSamp>(sampColl(), pointer, settings));
  m_bon_samples.emplace(cacheKey, sampleId);
  return sampleId;
}

MP2kInstr::MP2kInstr(MP2kInstrSet *set, size_t offset, size_t length, u32 bank, u32 number,
                     MP2kInstrData data)
    : VGMInstr(set, offset, length, bank, number), m_data(data) {
  reverb = 0.0f;
}

bool MP2kInstr::loadInstr() {
  auto& psgSampColl = static_cast<MP2kInstrSet *>(parInstrSet)->psgSampColl();

  auto addPsgRgn = [this](VGMSampColl &psgColl, int sampNum, u32 adsr,
                          u8 keyLow = 0, u8 keyHigh = 127) {
    VGMRgn* rgn = this->addRgn(offset(), length(), sampNum, keyLow, keyHigh);
    rgn->sampCollPtr = &psgColl;
    rgn->setUnityKey(kPsgUnityKey);
    setCgbADSR(rgn, adsr);
    return rgn;
  };

  const Tone instrument = decodeTone(m_data);
  const u32 envelopeOrKeyMap = m_data.w2;
  auto& instrSet = *static_cast<MP2kInstrSet*>(parInstrSet);
  auto addPcmRegions = [this](const Tone& tone, u32 toneOffset, int sampleId, u32 adsr,
                              u8 keyLow = 0, u8 keyHigh = 127) {
    auto addRegion = [&](u8 low, u8 high) {
      auto* region = addRgn(toneOffset, kToneRecordSize, sampleId, low, high);
      setADSR(region, adsr);
      return region;
    };
    if (!tone.fixedPitch()) {
      addRegion(keyLow, keyHigh);
      return;
    }

    // One-key regions preserve fixed PCM's mixer-rate pitch in both SF2 and DLS.
    const auto* sample = parInstrSet->sampColl()->sample(sampleId);
    for (int key = keyLow; key <= keyHigh; ++key) {
      auto* region = addRegion(static_cast<u8>(key), static_cast<u8>(key));
      region->setUnityKey(static_cast<u8>(key));
      region->setFineTune(-sample->fineTune);
    }
  };

  auto addBonRegions = [this, &instrSet](const Tone& tone, u32 toneOffset, u32 adsr,
                                        u8 low = 0, u8 high = 127) {
    const auto pointer = tone.sampleOffset();
    if (!instrSet.isBonSample(pointer)) {
      return false;
    }
    for (int key = low; key <= high; ++key) {
      const int sampleId = instrSet.makeOrGetBonSample(pointer, static_cast<u8>(key));
      if (sampleId == -1) {
        continue;
      }
      auto* region = addRgn(toneOffset, kToneRecordSize, sampleId, key, key);
      region->setUnityKey(static_cast<u8>(key));
      setADSR(region, adsr);
    }
    return true;
  };

  switch (instrumentVoice(instrument.type)) {
    case VoiceType::Pcm: {
      setName("Single-region instrument");
      setLength(kToneRecordSize);

      if (instrument.sampleOrTable == 0 || !isGbaRomPointer(instrument.sampleOrTable)) {
        break;
      }

      const auto sampleOffset = instrument.sampleOffset();
      if (addBonRegions(instrument, offset(), envelopeOrKeyMap)) {
        break;
      }
      if (const int sampleId = instrSet.makeOrGetSample(sampleOffset); sampleId != -1) {
        addPcmRegions(instrument, offset(), sampleId, envelopeOrKeyMap);
      } else {
        L_WARN("No sample could be loaded for {:#x}", sampleOffset);
        setName(name() + " (sample missing)");
      }
      break;
    }

    case VoiceType::Square1:
    case VoiceType::Square2: {
      static constexpr const char* cycles[] = {"12,5%", "25%", "50%", "75%"};
      setName(fmt::format("PSG square {}", instrument.sampleOrTable < kPsgSquareCount
                                              ? cycles[instrument.sampleOrTable] : "???"));
      setLength(kToneRecordSize);
      addPsgRgn(psgSampColl, instrument.squareDuty(), envelopeOrKeyMap);
      break;
    }

    case VoiceType::Wave: {
      setName("PSG programmable waveform");
      setLength(kToneRecordSize);
      if (const int sampleId = psgSampColl.makeOrGetProgrammableWave(instrument.sampleOrTable); sampleId != -1) {
        addPsgRgn(psgSampColl, sampleId, envelopeOrKeyMap);
      } else {
        L_DEBUG("No programmable wave could be loaded for {:#x}", instrument.sampleOrTable);
        setName(name() + " (wave missing)");
      }
      break;
    }

    case VoiceType::Noise:
      setName("PSG noise");
      setLength(kToneRecordSize);
      addPsgRgn(psgSampColl, kPsgNoiseIndex, envelopeOrKeyMap);
      break;

    case VoiceType::KeySplit: {
      setName("Multi-region instrument");
      setLength(kToneRecordSize);
      const u32 toneTable = instrument.sampleOffset();
      const u32 keyMap = romOffset(envelopeOrKeyMap);
      std::vector<u8> splitKeys;
      std::vector<u8> toneIndices;
      int previousIndex = -1;
      for (int key = 0; key < kMidiKeyCount; ++key) {
        const u8 toneIndex = rawFile()->get<u8>(keyMap + key);
        if (toneIndex != previousIndex) {
          splitKeys.push_back(static_cast<u8>(key));
          toneIndices.push_back(toneIndex);
          previousIndex = toneIndex;
        }
      }
      splitKeys.push_back(kMidiKeyCount);

      for (size_t split = 0; split < toneIndices.size(); ++split) {
        const u32 toneOffset = toneTable + kToneRecordSize * toneIndices[split];
        const u8 type = rawFile()->get<u8>(toneOffset);
        if (isNestedBank(type)) {
          L_DEBUG("Recursive split/rhythm instrument in key-split");
          continue;
        }
        const Tone tone = readTone(rawFile(), toneOffset, type);
        const auto envelope = [this, toneOffset] { return rawFile()->get<u32>(toneOffset + 8); };
        const u8 keyLow = splitKeys[split];
        const u8 keyHigh = splitKeys[split + 1] - 1;
        switch (tone.voice()) {
          case VoiceType::Pcm: {
            if (tone.sampleOffset() == 0 || !isGbaRomPointer(tone.sampleOrTable)) {
              break;
            }
            if (addBonRegions(tone, toneOffset, envelope(), keyLow, keyHigh)) {
              break;
            }
            if (const int sampleId = instrSet.makeOrGetSample(tone.sampleOffset()); sampleId != -1) {
              addPcmRegions(tone, toneOffset, sampleId, envelope(), keyLow, keyHigh);
            }
            break;
          }
          case VoiceType::Square1:
          case VoiceType::Square2:
            addPsgRgn(psgSampColl, tone.squareDuty(), envelope(), keyLow, keyHigh);
            break;
          case VoiceType::Wave:
            if (const int sampleId = psgSampColl.makeOrGetProgrammableWave(tone.sampleOrTable); sampleId != -1) {
              addPsgRgn(psgSampColl, sampleId, envelope(), keyLow, keyHigh);
            }
            break;
          case VoiceType::Noise:
            addPsgRgn(psgSampColl, kPsgNoiseIndex, envelope(), keyLow, keyHigh);
            break;
          default:
            break;
        }
      }
      break;
    }

    case VoiceType::Rhythm: {
      setName("Full-keyboard instrument");
      setLength(kToneRecordSize);
      const u32 toneTable = instrument.sampleOffset();
      for (int key = 0; key < kMidiKeyCount; ++key) {
        const u32 toneOffset = toneTable + kToneRecordSize * key;
        const Tone tone = readTone(rawFile(), toneOffset, rawFile()->get<u8>(toneOffset));
        const auto envelope = [this, toneOffset] { return rawFile()->get<u32>(toneOffset + 8); };
        VGMRgn* region = nullptr;
        switch (tone.voice()) {
          case VoiceType::Pcm: {
            const u32 sampleOffset = tone.sampleOffset();
            if (sampleOffset == 0 || !isGbaRomPointer(tone.sampleOrTable)) {
              break;
            }
            if (instrSet.isBonSample(sampleOffset)) {
              const int synthId = instrSet.makeOrGetBonSample(sampleOffset, tone.key);
              if (synthId == -1) {
                break;
              }
              region = addRgn(offset(), length(), synthId, key, key);
              region->setUnityKey(static_cast<u8>(key));
              setADSR(region, envelope());
              break;
            }
            const int sampleId = instrSet.makeOrGetSample(sampleOffset);
            if (sampleId == -1) {
              break;
            }
            region = addRgn(offset(), length(), sampleId, key, key);
            if (tone.fixedPitch()) {
              // Fixed PCM advances at the mixer rate, ignoring the tone key and sample tuning.
              const auto* sample = parInstrSet->sampColl()->sample(sampleId);
              region->setUnityKey(static_cast<u8>(key));
              region->setFineTune(-sample->fineTune);
            } else {
              const u32 pitch = rawFile()->get<u32>(sampleOffset + 4);
              const double deltaNote = 12.0 * log2(instrSet.sampleRate() * 1024.0 / pitch);
              const int rootKey = 60 + int(round(deltaNote));
              region->setUnityKey(rootKey - tone.key + key);
            }
            setADSR(region, envelope());
            break;
          }
          case VoiceType::Square1:
          case VoiceType::Square2: {
            region = addPsgRgn(psgSampColl, tone.squareDuty(), envelope(), key, key);
            // A rhythm key selects the tone; the tone's key byte determines square pitch.
            const int rootKey = kPsgUnityKey - static_cast<int>(tone.key) + key;
            const int storedRootKey = std::clamp(rootKey, 0, 127);
            region->setUnityKey(static_cast<u8>(storedRootKey));
            region->coarseTune = static_cast<s16>(storedRootKey - rootKey);
            break;
          }
          case VoiceType::Wave:
            if (const int sampleId = psgSampColl.makeOrGetProgrammableWave(tone.sampleOrTable); sampleId != -1) {
              region = addPsgRgn(psgSampColl, sampleId, envelope(), key, key);
            }
            break;
          case VoiceType::Noise: {
            const int sampleId = psgSampColl.makeOrGetNoise(tone.key, tone.shortNoise());
            region = addPsgRgn(psgSampColl, sampleId, envelope(), key, key);
            region->setUnityKey(static_cast<u8>(key));
            break;
          }
          default:
            break;
        }
        if (region != nullptr) {
          tone.applyRhythmPan(region);
        }
      }
      break;
    }

    default:
      L_WARN("{:x}: Unhandled instrument type {}", offset(), instrument.type);
      return false;
  }

  if (!regions().empty()) {
    for (auto* rgn : regions()) {
      // Only Direct Sound PCM voices enter the driver's reverb mix, including in split banks.
      rgn->reverbSendScale = rgn->sampCollPtr == &psgSampColl ? 0.0 : 1.0;
    }
    constexpr double kDefaultLfoFrequencyHz = 60.0 * 22.0 / 256.0;
    constexpr double kVibratoMaxDepthCents = 127.0 * 4.0 * 100.0 / 64.0;
    addStandardVibratoHandling(kVibratoMaxDepthCents, kDefaultLfoFrequencyHz,
                               kDefaultLfoFrequencyHz);
    // MP2k tremolo applies volume * (modM + 128) / 128, so full positive
    // depth is about +6 dB rather than attenuation-only.
    constexpr double kTremoloMaxDepthDb = 6.0;
    addStandardTremoloHandling(kTremoloMaxDepthDb, kDefaultLfoFrequencyHz,
                               kDefaultLfoFrequencyHz,
                               TremoloGainMode::BipolarAroundNominal);
  }

  return true;
}

void MP2kInstr::setADSR(VGMRgn *rgn, u32 adsr) {
  const auto [attack, decay, sustain, release] = decodeEnvelope(adsr);
  constexpr double kEnvelopeMaxLevel = 256.0;

  if (attack != 0xFF) {
    rgn->attack_time = mp2kAttackTimeSeconds(attack);
  }

  if (sustain != 0xFF) {
    rgn->sustain_level = sustain / kEnvelopeMaxLevel;
    rgn->decay_time = mp2kEnvelopeRateTimeSeconds(decay);
  }

  if (release != 0x00) {
    rgn->release_time = mp2kEnvelopeRateTimeSeconds(release);
  }
}

void MP2kInstr::setCgbADSR(VGMRgn *rgn, u32 adsr) {
  const auto [attack, decay, sustain, release] = decodeEnvelope(adsr);

  rgn->attack_time = cgbEnvelopeRateTimeSeconds(attack);
  rgn->decay_time = cgbEnvelopeRateTimeSeconds(decay);
  rgn->sustain_level = cgbSustainLevel(sustain);
  rgn->release_time = cgbEnvelopeRateTimeSeconds(release);
}

MP2kSamp::MP2kSamp(VGMSampColl *sampColl, MP2kSampParams params)
    : VGMSamp(sampColl, params.offset, params.length, params.dataOffset, params.dataLength,
              1, BPS::PCM16, 0, "Sample"),
      m_type(params.type){};

std::vector<u8> MP2kSamp::decodeToNativePcm() {
  std::vector<u8> buf(uncompressedSize());
  switch (m_type) {
    case MP2kWaveType::PCM8: {
      readBytes(dataOff, dataLength, buf.data());
      break;
    }

    case MP2kWaveType::BDPCM: {
      static constexpr s8 delta_lut[] = {0,   1,   4,   9,   16,  25, 36, 49,
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
        s8 sample = blocks[block][0];
        buf[64 * block] = sample << 8;
        sample += delta_lut[blocks[block][1] & 0xf];
        buf[64 * block + 1] = sample << 8;
        for (unsigned int j = 1; j < 32; ++j) {
          u8 d = blocks[block][j + 1];
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
constexpr u8 kCgbWaveRamBytes = 16;
constexpr u8 kCgbWaveRamSamples = kCgbWaveRamBytes * 2;

constexpr u64 kNoiseClockHz = 524288;
constexpr u8 kNoiseDivisorMask = 0x07;
constexpr u8 kNoiseWidthFlag = 0x08;
constexpr unsigned kNoiseClockShiftPosition = 4;
constexpr unsigned kLongNoiseFeedbackBit = 14;
constexpr unsigned kShortNoiseFeedbackBit = 6;
constexpr u16 kShortNoiseFeedbackMask = u16{1} << kShortNoiseFeedbackBit;
constexpr u16 kNoiseInitialState = 0x7fff;
constexpr u64 kLongNoisePeriod = 32767;
constexpr u64 kShortNoisePeriod = 127;
constexpr u32 kMinNoiseSampleRate = 512;
constexpr u32 kTargetNoiseLoopSamples = 512 * 1024;

struct NoiseControl {
  u8 divisorCode;
  u8 clockShift;
  bool shortMode;

  constexpr u8 registerValue() const {
    const u8 widthFlag = shortMode ? kNoiseWidthFlag : 0;
    return static_cast<u8>((clockShift << kNoiseClockShiftPosition) | widthFlag | divisorCode);
  }

  constexpr u64 clockDenominator(u32 sampleRate) const {
    // Code zero means one half; double all divisors to use an integer clock ratio.
    const u64 doubledDivisor = divisorCode == 0 ? 1 : 2 * divisorCode;
    return (doubledDivisor << clockShift) * sampleRate;
  }

  constexpr u64 sequencePeriod() const {
    return shortMode ? kShortNoisePeriod : kLongNoisePeriod;
  }
};

constexpr NoiseControl decodeNoiseControl(u8 control) {
  return {static_cast<u8>(control & kNoiseDivisorMask),
          static_cast<u8>(control >> kNoiseClockShiftPosition), (control & kNoiseWidthFlag) != 0};
}

constexpr NoiseControl noiseControlForKey(u8 key, bool shortMode) {
  // MusicPlayer2000 maps keys 21..80 to quantized hardware noise frequencies.
  constexpr int kLowestNoiseKey = 21;
  constexpr int kHighestNoiseKey = 80;
  constexpr int kKeysPerClockShift = 4;
  constexpr int kShiftedKeyCount = 56;
  constexpr int kHighestClockShift = 13;
  constexpr int kHighestDivisorCode = 7;
  const int keyIndex = std::clamp(static_cast<int>(key) - kLowestNoiseKey,
                                 0, kHighestNoiseKey - kLowestNoiseKey);
  if (keyIndex < kShiftedKeyCount) {
    return {static_cast<u8>(kHighestDivisorCode - keyIndex % kKeysPerClockShift),
            static_cast<u8>(kHighestClockShift - keyIndex / kKeysPerClockShift), shortMode};
  }
  // The four highest keys use divisor codes 3, 2, 1 and 0 with no clock shift.
  return {static_cast<u8>(kHighestNoiseKey - kLowestNoiseKey - keyIndex), 0, shortMode};
}

constexpr u32 noiseLoopSamples(NoiseControl control, u32 sampleRate) {
  const u64 denominator = control.clockDenominator(sampleRate);
  const u64 clockPeriod = denominator / std::gcd(denominator, kNoiseClockHz);
  // Repeat only after both the LFSR sequence and fractional clock phase return to their start.
  return static_cast<u32>(control.sequencePeriod() * clockPeriod);
}

constexpr u32 noiseSampleRate(NoiseControl control, u32 pwmSampleRate) {
  u32 sampleRate = pwmSampleRate;
  // Keep PWM resolution and its aliasing whenever the complete loop fits the sample budget.
  // Larger loops trade edge precision for memory, with at most 1/512 second of quantization.
  // At that minimum rate, preserve the full cycle even when it exceeds the target budget.
  while (sampleRate > kMinNoiseSampleRate && noiseLoopSamples(control, sampleRate) > kTargetNoiseLoopSamples) {
    sampleRate = std::max(kMinNoiseSampleRate, sampleRate / 2);
  }
  return sampleRate;
}

constexpr u16 advanceNoiseLfsr(u16 state, bool shortMode) {
  // XOR the two low bits, shift, and feed the result back into the 15-bit register.
  const u16 feedback = (state ^ (state >> 1)) & 1;
  state = (state >> 1) | (feedback << kLongNoiseFeedbackBit);
  if (shortMode) {
    // Short mode also replaces bit 6, producing the 7-bit sequence.
    state = (state & ~kShortNoiseFeedbackMask) | (feedback << kShortNoiseFeedbackBit);
  }
  return state;
}

double psgDutyRatio(u8 dutyIndex) {
  constexpr double kDutyRatios[4] = {0.125, 0.25, 0.5, 0.75};
  return kDutyRatios[dutyIndex & kSquareDutyMask];
}
}

MP2kPSGColl::MP2kPSGColl(RawFile *file, u32 sampleRate, u32 loopSamples, u32 noiseSampleRate)
    : VGMSampColl(MP2kFormat::name, file, 0, 0, "MP2k PSG samples"),
      m_sample_rate(sampleRate),
      m_loop_samples(loopSamples),
      m_noise_sample_rate(noiseSampleRate) {
}

bool MP2kPSGColl::parseSampleInfo() {
  constexpr const char* kDutyLabels[4] = {"12.5%", "25%", "50%", "75%"};
  for (u8 duty = 0; duty < kPsgSquareCount; duty++) {
    auto name = fmt::format("PSG square {}", kDutyLabels[duty]);
    addSamp<MP2kPSGSamp>(this, duty, false, m_sample_rate, m_loop_samples, name);
  }

  addSamp<MP2kPSGSamp>(this, 0, true, m_sample_rate, m_loop_samples, "PSG noise");
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

  int sample_id = static_cast<int>(sampleCount());
  auto name = fmt::format("PSG programmable wave {:#x}", wave_offset);
  // MP2k uses the square-channel timer table for wave voices. The wave channel
  // has half the square-channel fundamental at the same timer setting.
  constexpr u32 kCgbWaveSampleRate = (kCgbWaveRamSamples / 2) * 440;
  addSamp<MP2kPSGWaveSamp>(this, wave_offset, kCgbWaveSampleRate, name);
  m_programmable_waves.emplace(wave_offset, sample_id);
  return sample_id;
}

int MP2kPSGColl::makeOrGetNoise(u8 key, bool shortMode) {
  const NoiseControl noise = noiseControlForKey(key, shortMode);
  const u8 control = noise.registerValue();
  if (const auto found = m_noise_samples.find(control); found != m_noise_samples.end()) {
    return found->second;
  }

  const u32 sampleRate = noiseSampleRate(noise, m_noise_sample_rate);
  const u32 loopSamples = noiseLoopSamples(noise, sampleRate);
  const int sampleId = static_cast<int>(sampleCount());
  addSamp<MP2kPSGNoiseSamp>(this, control, sampleRate, loopSamples,
                           fmt::format("PSG noise {:#04x}", control));
  m_noise_samples.emplace(control, sampleId);
  return sampleId;
}

MP2kPSGNoiseSamp::MP2kPSGNoiseSamp(VGMSampColl* sampColl, u8 control, u32 sampleRate,
                                 u32 loopSamples, std::string name)
    : VGMSamp(sampColl, 0, loopSamples * sizeof(s16), 0, loopSamples * sizeof(s16),
              1, BPS::PCM16, sampleRate, std::move(name)),
      m_control(control) {
  setLoopStatus(true);
  setLoopOffset(0);
  setLoopLength(loopSamples);
  setLoopStartMeasure(LM_SAMPLES);
  setLoopLengthMeasure(LM_SAMPLES);
  unityKey = kPsgUnityKey;
  ulUncompressedSize = loopSamples * bytesPerSample();
}

std::vector<u8> MP2kPSGNoiseSamp::decodeToNativePcm() {
  std::vector<u8> samples(uncompressedSize());
  auto* output = reinterpret_cast<s16*>(samples.data());
  const NoiseControl noise = decodeNoiseControl(m_control);
  const u64 denominator = noise.clockDenominator(rate);
  u16 lfsr = kNoiseInitialState;
  u64 phase = 0;
  for (int i = 0; i < loopLength(); ++i) {
    output[i] = (lfsr & 1) != 0 ? -32767 : 32767;
    phase += kNoiseClockHz;
    while (phase >= denominator) {
      lfsr = advanceNoiseLfsr(lfsr, noise.shortMode);
      phase -= denominator;
    }
  }
  return samples;
}

MP2kPSGSamp::MP2kPSGSamp(VGMSampColl *sampColl, u8 dutyIndex, bool noise,
                         u32 sampleRate, u32 loopSamples, std::string name)
    : VGMSamp(sampColl, 0, loopSamples * sizeof(s16), 0, loopSamples * sizeof(s16), 1,
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

std::vector<u8> MP2kPSGSamp::decodeToNativePcm() {
  if (m_noise) {
    return psg::synthesizeLfsrNoisePCM16(loopLength());
  }

  return psg::synthesizeBandLimitedPulsePCM16(m_duty_ratio, rate, loopLength());
}

MP2kPSGWaveSamp::MP2kPSGWaveSamp(VGMSampColl *sampColl, size_t wavePointer,
                                 u32 sampleRate, std::string name)
    : VGMSamp(sampColl, static_cast<u32>(wavePointer), kCgbWaveRamBytes,
              static_cast<u32>(wavePointer), kCgbWaveRamBytes, 1, BPS::PCM16, sampleRate,
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

std::vector<u8> MP2kPSGWaveSamp::decodeToNativePcm() {
  std::vector<u8> samples(kCgbWaveRamSamples * sizeof(s16));
  auto *output = reinterpret_cast<s16 *>(samples.data());

  for (u8 i = 0; i < kCgbWaveRamBytes; i++) {
    u8 packed = readByte(m_wave_pointer + i);
    u8 high = packed >> 4;
    u8 low = packed & 0x0F;
    output[i * 2] = static_cast<s16>(high * 0x1111 - 0x8000);
    output[i * 2 + 1] = static_cast<s16>(low * 0x1111 - 0x8000);
  }

  return samples;
}

MP2kBonSamp::MP2kBonSamp(VGMSampColl* collection, size_t pointer, const RenderSettings& settings)
    : VGMSamp(collection, pointer, 24, pointer, 24, 1, BPS::PCM16, settings.sampleRate,
              fmt::format("Bon synth {} key {}", static_cast<u8>(settings.shape), settings.key)),
      m_settings(settings) {
  unityKey = settings.key;
  fineTune = settings.fineTune;
  setLoopStatus(true);
  setLoopOffset(0);
  setLoopLength(settings.loopSamples);
  setLoopStartMeasure(LM_SAMPLES);
  setLoopLengthMeasure(LM_SAMPLES);
  ulUncompressedSize = settings.loopSamples * sizeof(s16);
}

std::vector<u8> MP2kBonSamp::decodeToNativePcm() {
  std::vector<u8> data(uncompressedSize());
  auto* output = reinterpret_cast<s16*>(data.data());
  int sawState = 0;
  const int preroll = m_settings.shape == Shape::Saw ? kBonSawPrerollSamples : 0;
  for (int i = -preroll; i < static_cast<int>(loopLength()); ++i) {
    const u64 phasePosition = static_cast<u64>(i + static_cast<int>(loopLength()) + 1) *
                              m_settings.carrierCycles;
    const double phase = (phasePosition % loopLength()) / static_cast<double>(loopLength());
    double amplitude = 0;
    switch (m_settings.shape) {
      case Shape::Pulse: {
        // Duty changes once per driver frame. Byte steps wrap before folding the triangle.
        const auto& pwm = m_settings.pwm;
        const u32 frame = static_cast<u64>(i) * kBonPwmLoopFrames / loopLength() + 1;
        const double modulation = ((pwm.initialPhase + frame * pwm.step) % kBonPwmPhaseSteps) /
                                  static_cast<double>(kBonPwmPhaseSteps);
        const double folded = std::min(modulation, 1.0 - modulation);
        const double rawWidth = (pwm.base + pwm.depth * folded) / kBonPwmPhaseSteps;
        const double width = rawWidth - std::floor(rawWidth);
        amplitude = (phase < width ? 1.0 : 0.0) - width;
        break;
      }
      case Shape::Saw: {
        // The driver's pseudo-saw combines a quantized ramp with a one-pole recurrence.
        const int ramp = static_cast<int>(std::floor(phase * 256.0)) - 112;
        const int staircase = static_cast<int>(std::floor(phase * 64.0)) % 32;
        sawState = ramp - staircase + static_cast<int>(std::floor(sawState * 0.5));
        amplitude = sawState / 256.0;
        break;
      }
      case Shape::Triangle:
        amplitude = 1.0 - 4.0 * std::abs(phase - 0.5);
        break;
    }
    if (i >= 0) {
      output[i] = static_cast<s16>(std::lround(std::clamp(amplitude, -1.0, 1.0) * 32767.0));
    }
  }
  return data;
}
