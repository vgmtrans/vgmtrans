/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CPS/Cps.h"

#include "value/synth/SynthMath.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <vector>

namespace vgmtrans::formats::cps {

using namespace core;

namespace {

constexpr u32 kCps1OkiSampleRate = 7576;
constexpr u32 kCps2SampleRate = 24038;
constexpr u32 kCps3SampleRate = 37287;

constexpr std::array<u16, 64> kAttackRates = {
    0,      0,      1,      1,      2,      2,      3,      3,      4,      5,      6,      7,      8,
    9,      0x0b,   0x0d,   0x0f,   0x11,   0x22,   0x33,   0x44,   0x55,   0x64,   0x84,   0x0a4,  0x0c5,
    0x0e6,  0x107,  0x149,  0x18b,  0x1cc,  0x20e,  0x292,  0x315,  0x398,  0x41c,  0x523,  0x62a,  0x731,
    0x838,  0x0a46, 0x0c54, 0x0e62, 0x1070, 0x148c, 0x18a8, 0x1cc4, 0x20e0, 0x2918, 0x3150, 0x3989, 0x41c1,
    0x5233, 0x62a0, 0x730d, 0x837d, 0xa45d, 0xc54c, 0xe61c, 0xefff, 0xf3ff, 0xf9ff, 0xfcff, 0xffff,
};

constexpr std::array<u16, 64> kDecayRates = {
    0,      1,      2,      2,      3,      3,      4,      4,      5,      6,      8,      0x0a,   0x0c,
    0x0e,   0x11,   0x13,   0x18,   0x1d,   0x21,   0x26,   0x30,   0x39,   0x43,   0x4c,   0x5f,   0x72,
    0x85,   0x98,   0x0be,  0x0e4,  0x10a,  0x130,  0x17d,  0x1c9,  0x215,  0x260,  0x2f9,  0x391,  0x42a,
    0x4c1,  0x5f2,  0x722,  0x853,  0x983,  0x0be4, 0x0e46, 0x10a6, 0x1307, 0x17c9, 0x1c8b, 0x214c, 0x2608,
    0x2f92, 0x3916, 0x4299, 0x4c1e, 0x5f24, 0x7228, 0x8533, 0x9835, 0xbe3e, 0xe451, 0xefff, 0xffff,
};

constexpr std::array<u16, 128> kSustainLevels = {
    0,      0x3ff,  0x5fe,  0x7ff,  0x9fe,  0xbfe,  0xdfd,  0xfff,  0x11fe, 0x13fe, 0x15fd, 0x17fe, 0x19fd,
    0x1bfd, 0x1dfc, 0x1fff, 0x21fd, 0x23fe, 0x25fd, 0x27fe, 0x29fd, 0x2bfd, 0x2dfc, 0x2ffe, 0x31fd, 0x33fd,
    0x35fc, 0x37fd, 0x39fc, 0x3bfc, 0x3dfb, 0x3fff, 0x41fe, 0x43fe, 0x45fd, 0x47fe, 0x49fd, 0x4bfd, 0x4dfc,
    0x4ffe, 0x51fd, 0x53fd, 0x55fc, 0x57fd, 0x59fc, 0x5bfc, 0x5dfb, 0x5ffe, 0x61fd, 0x63fd, 0x65fc, 0x67fd,
    0x69fc, 0x6bfc, 0x6dfb, 0x6ffd, 0x71fc, 0x73fc, 0x75fb, 0x77fc, 0x79fb, 0x7bfb, 0x7dfa, 0x7fff, 0x81fe,
    0x83fe, 0x85fd, 0x87fe, 0x89fd, 0x8bfd, 0x8dfc, 0x8ffe, 0x91fd, 0x93fd, 0x95fc, 0x97fd, 0x99fc, 0x9bfc,
    0x9dfb, 0x9ffe, 0xa1fd, 0xa3fd, 0xa5fc, 0xa7fd, 0xa9fc, 0xabfc, 0xadfb, 0xaffd, 0xb1fc, 0xb3fc, 0xb5fb,
    0xb7fc, 0xb9fb, 0xbbfb, 0xbdfa, 0xbffe, 0xc1fd, 0xc3fd, 0xc5fc, 0xc7fd, 0xc9fc, 0xcbfc, 0xcdfb, 0xcffd,
    0xd1fc, 0xd3fc, 0xd5fb, 0xd7fc, 0xd9fb, 0xdbfb, 0xddfa, 0xdffd, 0xe1fc, 0xe3fc, 0xe5fb, 0xe7fc, 0xe9fb,
    0xebfb, 0xedfa, 0xeffc, 0xf1fb, 0xf3fb, 0xf5fa, 0xf7fb, 0xf9fa, 0xfbfa, 0xfdf9, 0xfffe,
};

struct QSoundSampleInfo {
  u32 index = 0;
  u32 start = 0;
  u32 loop = 0;
  u32 end = 0;
  u8 unityKey = 60;
  SourceRange range;
};

[[nodiscard]] bool all(ByteReader reader, u32 offset, u32 size, u8 value) {
  if (!reader.has(offset, size)) {
    return false;
  }
  const auto bytes = reader.slice(offset, size);
  return std::ranges::all_of(bytes, [value](u8 byte) { return byte == value; });
}

[[nodiscard]] double signedAttenuationDb(double gain) {
  return gain <= 0.0 ? 96.0 : -20.0 * std::log10(gain);
}

[[nodiscard]] double linearDecayToDbSeconds(double seconds) {
  if (seconds <= 0.0 || !std::isfinite(seconds)) {
    return seconds;
  }
  constexpr double targetDbLeastSquares = 70.0;
  constexpr double targetDbInitialSlope = 140.0;
  constexpr double ln10 = 2.302585092994046;
  constexpr double kneeSeconds = 0.12;
  const double shortScale = targetDbInitialSlope / (20.0 / ln10);
  const double longScale = targetDbLeastSquares * ln10 / 45.0;
  const double x = seconds / kneeSeconds;
  const double weight = 1.0 / (1.0 + x * x);
  return seconds * (weight * shortScale + (1.0 - weight) * longScale);
}

[[nodiscard]] Envelope qsoundEnvelope(CpsVersion version, u8 attack, u8 decay, u8 sustainLevel, u8 sustain,
                                      u8 release) {
  const u16 ar = kAttackRates[std::min<u8>(attack, 63)];
  const u16 dr = kDecayRates[std::min<u8>(decay, 63)];
  const u16 sl = isCps3(version)
                     ? static_cast<u16>((static_cast<u32>(std::min<u8>(sustainLevel, 127)) + 1) * 65535 / 128)
                     : kSustainLevels[std::min<u8>(sustainLevel, 127)];
  const u16 sr = kDecayRates[std::min<u8>(sustain, 63)];
  const u16 rr = kDecayRates[std::min<u8>(release, 63)];
  const double rate = cpsDriverRateHertz(version);

  const auto stageSeconds = [rate](u16 sourceRate, bool zeroMeansInfinite) {
    if (sourceRate == 0) {
      return zeroMeansInfinite ? std::numeric_limits<double>::infinity() : 0.0;
    }
    if (sourceRate == 0xffff) {
      return 0.0;
    }
    return std::floor(65535.0 / sourceRate) / rate;
  };

  const double decaySeconds = stageSeconds(dr, true);
  const double secondDecaySeconds = stageSeconds(sr, true);
  const double sustainAmplitude = isCps3(version) ? (std::min<u8>(sustainLevel, 127) + 1) / 128.0 : sl / 65535.0;
  return Envelope{
      .attackSeconds = stageSeconds(ar, true),
      .decaySeconds = std::isinf(decaySeconds) ? decaySeconds : linearDecayToDbSeconds(decaySeconds),
      .secondDecaySeconds =
          dr == 0 || sustainAmplitude == 0.0
              ? std::nullopt
              : std::optional{std::isinf(secondDecaySeconds) ? secondDecaySeconds
                                                             : linearDecayToDbSeconds(secondDecaySeconds)},
      .releaseSeconds = linearDecayToDbSeconds(stageSeconds(rr, true)),
      .sustainAmplitude = sustainAmplitude,
  };
}

[[nodiscard]] u8 cps1VolumeAttenuation(u8 volume) {
  const u16 mixed = ((static_cast<u16>(volume) << 8) | (volume >> 4)) & 0x0f07;
  const u8 keyScaleAttenuation = static_cast<u8>(mixed >> 8);
  const u8 rotated = 0xfe;
  u8 product = static_cast<u8>((((rotated << 1) | (rotated >> 7)) & 0xf0) >> 4);
  product = static_cast<u8>(product * ((static_cast<s8>(mixed) << 1) & 0x0f));
  product >>= 4;
  if ((rotated & 0x80) != 0) {
    product = static_cast<u8>(-product);
  }
  return static_cast<u8>(std::min<int>(static_cast<s8>(product) + 0x10 + keyScaleAttenuation, 0x7f));
}

[[nodiscard]] Ym2151Voice cps1Voice(ByteReader reader, u32 offset, CpsVersion version, u8 masterVolume) {
  Ym2151Voice voice;
  u32 dt1Mul = 0;
  u32 totalLevel = 0;
  u32 ksAr = 0;
  u32 amD1r = 0;
  u32 dt2D2r = 0;
  u32 d1lRr = 0;
  u8 lfoControl = 0;
  u8 pmsAms = 0;
  u8 flCon = 0;
  u8 slotMask = 0;

  if (version == CpsVersion::Cps1V200) {
    slotMask = reader.u8At(offset);
    const bool enabled = reader.u8At(offset + 1) != 0;
    lfoControl = static_cast<u8>(enabled ? 0x80 | (reader.u8At(offset + 2) & 3) << 5 : 0);
    voice.lfo.frequency = reader.u8At(offset + 3);
    voice.lfo.pitchModulationDepth = reader.u8At(offset + 4);
    voice.lfo.amplitudeModulationDepth = reader.u8At(offset + 5);
    pmsAms = reader.u8At(offset + 6);
    flCon = reader.u8At(offset + 7);
    dt1Mul = offset + 8;
    totalLevel = offset + 12;
    ksAr = offset + 16;
    amD1r = offset + 20;
    dt2D2r = offset + 24;
    d1lRr = offset + 28;
  } else if (version == CpsVersion::Cps1V500 || version == CpsVersion::Cps1V502) {
    slotMask = reader.u8At(offset);
    lfoControl = reader.u8At(offset + 1);
    voice.lfo.frequency = reader.u8At(offset + 2);
    voice.lfo.pitchModulationDepth = reader.u8At(offset + 3);
    voice.lfo.amplitudeModulationDepth = reader.u8At(offset + 4);
    pmsAms = reader.u8At(offset + 6);
    flCon = reader.u8At(offset + 7);
    dt1Mul = offset + 8;
    ksAr = offset + 12;
    amD1r = offset + 16;
    dt2D2r = offset + 20;
    d1lRr = offset + 24;
    totalLevel = offset + 28;
  } else {
    lfoControl = reader.u8At(offset + 1);
    voice.lfo.frequency = reader.u8At(offset + 2);
    voice.lfo.pitchModulationDepth = reader.u8At(offset + 3);
    voice.lfo.amplitudeModulationDepth = reader.u8At(offset + 4);
    flCon = reader.u8At(offset + 5);
    pmsAms = reader.u8At(offset + 6);
    slotMask = reader.u8At(offset + 7);
    dt1Mul = offset + 20;
    ksAr = offset + 24;
    amD1r = offset + 28;
    dt2D2r = offset + 32;
    d1lRr = offset + 36;
  }

  voice.algorithm = flCon & 7;
  voice.feedback = (flCon >> 3) & 7;
  voice.operatorMask = (slotMask >> 3) & 0x0f;
  voice.lfoEnabled = (lfoControl & 0x80) != 0;
  voice.resetLfoOnSelect = (lfoControl & 0x02) != 0;
  voice.lfo.waveform = static_cast<Ym2151LfoWaveform>((lfoControl >> 5) & 3);
  voice.amplitudeModulationSensitivity = voice.lfoEnabled ? pmsAms & 3 : 0;
  voice.pitchModulationSensitivity = voice.lfoEnabled ? (pmsAms >> 4) & 7 : 0;

  constexpr std::array<u8, 4> algorithmLimits{7, 5, 4, 0};
  const u8 masterAttenuation = 0x7f - masterVolume;
  for (u32 index = 0; index < 4; ++index) {
    auto& op = voice.operators[index];
    const u8 dtMul = reader.u8At(dt1Mul + index);
    const u8 attack = reader.u8At(ksAr + index);
    const u8 firstDecay = reader.u8At(amD1r + index);
    const u8 secondDecay = reader.u8At(dt2D2r + index);
    const u8 sustainRelease = reader.u8At(d1lRr + index);
    op.attackRate = attack & 0x1f;
    op.firstDecayRate = firstDecay & 0x1f;
    op.secondDecayRate = secondDecay & 0x1f;
    op.releaseRate = sustainRelease & 0x0f;
    op.sustainLevel = sustainRelease >> 4;
    op.keyScale = attack >> 6;
    op.multiplier = dtMul & 0x0f;
    op.detune1 = (dtMul >> 4) & 7;
    op.detune2 = secondDecay >> 6;
    op.amplitudeModulationEnabled = (firstDecay & 0x80) != 0;

    if (version == CpsVersion::Cps1V100 || version == CpsVersion::Cps1V350 || version == CpsVersion::Cps1V425) {
      const u32 volumeData = offset + 8 + index * 3;
      const u8 extraAttenuation = reader.u8At(volumeData);
      const u8 volume = reader.u8At(volumeData + 2);
      const u32 base = cps1VolumeAttenuation(volume) + extraAttenuation;
      op.totalLevel = static_cast<u8>(
          voice.algorithm < algorithmLimits[index] ? base & 0x7f : std::min<u32>(base + masterAttenuation, 0x7f));
    } else {
      const u8 raw = reader.u8At(totalLevel + index) & 0x7f;
      op.totalLevel = static_cast<u8>(
          std::min<u32>(masterAttenuation + (voice.algorithm < algorithmLimits[index] ? raw : 0), 0x7f));
    }
  }
  return voice;
}

[[nodiscard]] std::vector<QSoundSampleInfo> qsoundSampleInfos(ByteReader reader, const CpsLayout& layout) {
  std::vector<QSoundSampleInfo> infos;
  const u32 rowSize = isCps3(layout.version) ? 16 : 8;
  u32 length = layout.sampleInfoTableLength;
  length -= length % rowSize;
  for (u32 relative = 0, index = 0; relative < length; relative += rowSize, ++index) {
    const u32 row = layout.sampleInfoTableOffset + relative;
    if (!reader.has(row, rowSize)) {
      break;
    }
    QSoundSampleInfo info{.index = index, .range = reader.range(row, rowSize)};
    if (isCps3(layout.version)) {
      info.start = reader.be32(row);
      info.loop = reader.be32(row + 4);
      info.end = reader.be32(row + 8);
      info.unityKey = static_cast<u8>(reader.be32(row + 12));
    } else {
      const u32 bank = reader.u8At(row);
      info.start = (bank << 16) | reader.le16(row + 1);
      info.loop = (bank << 16) | reader.le16(row + 3);
      const u16 end = reader.le16(row + 5);
      info.end = end == 0 ? ((bank + 1) << 16) : ((bank << 16) | end);
      info.unityKey = reader.u8At(row + 7);
    }
    if (info.end < info.start) {
      info.end = info.start;
    }
    if (info.loop < info.start || info.loop > info.end) {
      info.loop = info.end;
    }
    infos.push_back(info);
  }
  return infos;
}

[[nodiscard]] u32 normalizedSampleIndex(u32 raw, u32 sampleCount) {
  if (raw >= 0x8000) {
    raw -= 0x8000;
  }
  return raw < sampleCount ? raw : 0;
}

void addQSoundRegion(ScanInstrumentSetDraft& instruments, InstrumentSetBuilder::Entry instrument,
                     const ScanSampleCollectionDraft& samples, const std::vector<QSoundSampleInfo>& sampleInfos,
                     CpsVersion version, SourceRange range, u32 rawSample, s8 fineTune, u8 attack, u8 decay,
                     u8 sustainLevel, u8 sustain, u8 release, KeyRange keys = {}, double pan = 0.5,
                     double attenuationDb = 0.0) {
  if (sampleInfos.empty()) {
    return;
  }
  const u32 sampleIndex = normalizedSampleIndex(rawSample, static_cast<u32>(sampleInfos.size()));
  const auto sample = samples.find(sampleIndex);
  if (!sample) {
    instruments.warning("CPS instrument refers to sample data outside the QSound ROM", range);
    return;
  }
  const double fineSemitones = fineTune / 256.0;
  instrument
      .region(*sample,
              Region{
                  .keyRange = keys,
                  .range = range,
                  .unityKey = sampleInfos[sampleIndex].unityKey - fineSemitones,
                  .envelope = qsoundEnvelope(version, attack, decay, sustainLevel, sustain, release),
                  .pan = pan,
                  .attenuationDb = attenuationDb,
              })
      .source("Region", range, "cps-qsound-region")
      .derived("sample", sampleIndex)
      .derived("fine_tune_cents", fineSemitones * 100.0, SourceValueDisplay::Cents);
}

}  // namespace

Cps1SynthRefs addCps1Synth(ScanResultBuilder& builder, CpsLayout& layout) {
  Cps1SynthRefs refs;
  const ByteReader reader = builder.reader();
  const u32 patchSize = layout.version == CpsVersion::Cps1V200 || layout.version == CpsVersion::Cps1V500 ||
                                layout.version == CpsVersion::Cps1V502
                            ? 32
                            : 40;

  const u32 patchCount = std::min<u32>(127, layout.instrumentTableLength / patchSize);
  // Determine publication before creating a draft. Transposes are also needed
  // by sequence decoding, so this small plan is useful beyond asset emission.
  std::vector<u32> patchOffsets;
  layout.cps1InstrumentTransposes.clear();
  for (u32 index = 0; index < patchCount; ++index) {
    const u32 offset = layout.instrumentTableOffset + index * patchSize;
    if (!reader.has(offset, patchSize) || (all(reader, offset, 4, 0) && all(reader, offset + 4, 4, 0))) {
      break;
    }
    patchOffsets.push_back(offset);
    layout.cps1InstrumentTransposes.push_back(patchSize == 40 ? reader.s8At(offset) : 0);
  }

  if (!patchOffsets.empty()) {
    auto ym = builder.instrumentSet(layout.game + " YM2151 Instruments");
    ym.include(reader.range(layout.instrumentTableOffset, layout.instrumentTableLength));
    ym.source(SourceRole::Table, "YM2151 Patch Table",
              reader.range(layout.instrumentTableOffset, layout.instrumentTableLength), "cps1-ym2151-patch-table");
    for (u32 index = 0; index < patchOffsets.size(); ++index) {
      const u32 offset = patchOffsets[index];
      const SourceRange range = reader.range(offset, patchSize);
      const std::string name = fmt::format("YM2151 Instrument {}", index);
      const s8 transpose = layout.cps1InstrumentTransposes[index];
      auto instrument = ym.add(
          index,
          Instrument{
              .explicitAddress = InstrumentAddress{.bank = 0, .program = index},
              .identity = InstrumentIdentity{.domain = std::string(kCps1Ym2151Domain), .key = index},
              .reverb = 0.0,
              .name = name,
              .range = range,
              .synthVoice = Instrument::SynthVoice{cps1Voice(reader, offset, layout.version, layout.masterVolume)},
          });
      instrument.source(name, range, "cps1-ym2151-patch").derived("transpose", transpose);
    }
    refs.ym2151 = ym.ref();
  }

  if (!layout.sampleRom.valid() || layout.sampleRom.size < 0x400) {
    return refs;
  }

  auto oki = builder.instrumentSet(layout.game + " OKI Instruments");
  auto samples = builder.sampleCollection(layout.game + " OKI Samples", layout.sampleRom);
  const SourceRange directory =
      reader.range(layout.sampleRom.offset + 8, std::min<u64>(0x3f8, layout.sampleRom.size - 8));
  samples.source(SourceRole::Table, "OKI Sample Directory", directory, "cps1-oki-sample-directory");
  for (u32 index = 0; index < 127; ++index) {
    const u32 row = static_cast<u32>(layout.sampleRom.offset + 8 + index * 8);
    if (!reader.has(row, 8)) {
      break;
    }
    const u32 start = (static_cast<u32>(reader.u8At(row)) << 16) | (static_cast<u32>(reader.u8At(row + 1)) << 8) |
                      reader.u8At(row + 2);
    const u32 end = (static_cast<u32>(reader.u8At(row + 3)) << 16) | (static_cast<u32>(reader.u8At(row + 4)) << 8) |
                    reader.u8At(row + 5);
    const bool empty = start == 0 || start == 0xffffff || end <= start || start >= layout.sampleRom.size;
    const u32 length = empty ? 0 : static_cast<u32>(std::min<u64>(end - start, layout.sampleRom.size - start));
    const std::string name = fmt::format("{}OKI Sample {}", empty ? "Empty " : "", index);
    samples
        .add(index,
             Sample{
                 .name = name,
                 .codec = AudioCodec::OkiAdpcm,
                 .encodedData = reader.range(empty ? row : layout.sampleRom.offset + start, length),
                 .sampleRate = kCps1OkiSampleRate,
                 .bitsPerSample = 4,
                 .codecParameter = empty ? 8u : 0u,
             })
        .source(name + " Directory Entry", reader.range(row, 8), "cps1-oki-sample-info");
  }

  if (layout.version == CpsVersion::Cps1V425 && layout.cps1SampleInstrumentTableOffset) {
    const u32 table = *layout.cps1SampleInstrumentTableOffset;
    for (u32 program = 0; program < 128 && reader.has(table + program * 4, 4); ++program) {
      const u32 row = table + program * 4;
      if ((reader.u8At(row) & 0x80) == 0) {
        break;
      }
      const u32 sampleIndex = static_cast<u8>(reader.u8At(row + 1) - 1);
      const auto sample = samples.find(sampleIndex);
      const SourceRange range = reader.range(row, 4);
      const std::string name = fmt::format("OKI Instrument {}", program);
      auto instrument =
          oki.add(program, Instrument{
                               .explicitAddress = InstrumentAddress{.bank = 0, .program = program},
                               .identity = InstrumentIdentity{.domain = std::string(kCps1OkiDomain), .key = program},
                               .reverb = 0.0,
                               .name = name,
                               .range = range,
                           });
      instrument.source(name, range, "cps1-oki-instrument").derived("sample", sampleIndex);
      if (sample) {
        instrument
            .region(*sample, Region{.range = range, .unityKey = 60.0, .envelope = Envelope{.releaseSeconds = 10.0}})
            .source("Region", range, "cps1-oki-region");
      }
    }
  } else {
    for (u32 program = 1; program < 128; ++program) {
      const auto sample = samples.find(program - 1);
      if (!sample) {
        continue;
      }
      const SourceRange range = reader.range(layout.sampleRom.offset + 8 + (program - 1) * 8, 8);
      const std::string name = fmt::format("OKI Instrument {}", program);
      auto instrument =
          oki.add(program, Instrument{
                               .explicitAddress = InstrumentAddress{.bank = 0, .program = program},
                               .identity = InstrumentIdentity{.domain = std::string(kCps1OkiDomain), .key = program},
                               .reverb = 0.0,
                               .name = name,
                               .range = range,
                           });
      instrument.source(name, range, "cps1-oki-instrument").derived("sample", program - 1);
      instrument.region(*sample, Region{.range = range, .unityKey = 60.0, .envelope = Envelope{.releaseSeconds = 10.0}})
          .source("Region", range, "cps1-oki-region");
    }
  }
  refs.oki = ScanSynthRefs{
      .instruments = oki.ref(),
      .samples = samples.ref(),
  };
  return refs;
}

ScanSynthRefs addCpsQSoundSynth(ScanResultBuilder& builder, const CpsLayout& layout) {
  const ByteReader reader = builder.reader();
  auto instruments = builder.instrumentSet(layout.game + " QSound Instruments");
  auto samples = builder.sampleCollection(layout.game + " QSound Samples");
  const ScanSynthRefs refs{
      .instruments = instruments.ref(),
      .samples = samples.ref(),
  };

  const auto sampleInfos = qsoundSampleInfos(reader, layout);
  if (sampleInfos.empty()) {
    samples.warning("CPS QSound sample table contained no entries", layout.program);
    return refs;
  }
  if (!layout.sampleRom.valid()) {
    samples.warning("CPS QSound sample ROM region is unavailable", layout.program);
    return refs;
  }

  const u32 addressBase = sampleInfos.front().start & 0xff0000;
  for (const auto& info : sampleInfos) {
    if (info.start < addressBase || info.end <= info.start) {
      continue;
    }
    const u32 relative = info.start - addressBase;
    if (relative >= layout.sampleRom.size) {
      continue;
    }
    const u32 length = static_cast<u32>(std::min<u64>(info.end - info.start, layout.sampleRom.size - relative));
    if (length == 0) {
      continue;
    }
    const u32 loopStart = std::min(info.loop - info.start, length);
    const bool loops = length - loopStart >= 40;
    const std::string name = fmt::format("QSound Sample {}", info.index);
    samples
        .add(info.index,
             Sample{
                 .name = name,
                 .codec = AudioCodec::PcmS8,
                 .encodedData = reader.range(layout.sampleRom.offset + relative, length),
                 .sampleRate = isCps3(layout.version) ? kCps3SampleRate : kCps2SampleRate,
                 .bitsPerSample = 8,
                 .loop =
                     Loop{.enabled = loops, .start = loops ? loopStart : 0, .length = loops ? length - loopStart : 0},
             })
        .source(name + " Info", info.range, "cps-qsound-sample-info")
        .derived("unity_key", info.unityKey, SourceValueDisplay::MidiNote);
  }
  if (samples.empty()) {
    samples.warning("CPS QSound sample table contained no usable sample ranges", layout.sampleRom);
  }

  const auto addInstrument = [&](u32 key, InstrumentAddress address, SourceRange range) {
    const std::string name = fmt::format("QSound Instrument {} Bank {}", address.program, address.bank);
    auto entry =
        instruments.add(key, Instrument{
                                 .explicitAddress = address,
                                 .identity = InstrumentIdentity{.domain = std::string(kCpsQSoundDomain), .key = key},
                                 .name = name,
                                 .range = range,
                             });
    entry.source(name, range, "cps-qsound-instrument");
    return entry;
  };

  const bool fixedTable = layout.version >= CpsVersion::Cps2V100 && layout.version <= CpsVersion::Cps2V115;
  if (fixedTable) {
    for (u32 sourceBank = 0; sourceBank < layout.instrumentBanks; ++sourceBank) {
      for (u32 index = 0; index < 256; ++index) {
        const u32 row = layout.instrumentTableOffset + (sourceBank * 256 + index) * 8;
        if (!reader.has(row, 8)) {
          break;
        }
        const InstrumentAddress address{.bank = sourceBank * 2 + index / 128, .program = index % 128};
        const u32 key = address.bank * 128 + address.program;
        auto instrument = addInstrument(key, address, reader.range(row, 8));
        const bool early = layout.version < CpsVersion::Cps2V103;
        const u32 sample = early ? reader.u8At(row) : reader.le16(row);
        const s8 fine = early ? 0 : reader.s8At(row + 2);
        const u32 env = early ? row + 2 : row + 3;
        addQSoundRegion(instruments, instrument, samples, sampleInfos, layout.version, reader.range(row, 8), sample,
                        fine, reader.u8At(env), reader.u8At(env + 1), reader.u8At(env + 2), reader.u8At(env + 3),
                        reader.u8At(env + 4));
      }
    }
  } else if (isCps3(layout.version)) {
    for (u32 sourceBank = 0; sourceBank < layout.instrumentBanks; ++sourceBank) {
      const u32 pointerEntry = layout.instrumentTableOffset + sourceBank * 4;
      if (!reader.has(pointerEntry, 4)) {
        break;
      }
      const u32 encodedBank = reader.be32(pointerEntry);
      if (encodedBank < 0x06000000) {
        continue;
      }
      const u32 bankTable = static_cast<u32>(layout.program.offset + encodedBank - 0x06000000);
      if (!reader.has(bankTable, 256)) {
        continue;
      }
      for (u32 program = 0; program < 128; ++program) {
        const u16 relative = reader.be16(bankTable + program * 2);
        if (relative == 0) {
          continue;
        }
        const u32 begin = bankTable + relative;
        const u16 nextRelative = program == 127 ? 0 : reader.be16(bankTable + (program + 1) * 2);
        const u32 end = nextRelative > relative
                            ? bankTable + nextRelative
                            : static_cast<u32>(std::min<u64>(layout.program.endOffset(), begin + 12 * 128));
        if (!reader.has(begin, 12)) {
          continue;
        }
        const InstrumentAddress address{.bank = sourceBank * 2, .program = program};
        auto instrument = addInstrument(address.bank * 128 + program, address, reader.range(begin, end - begin));
        u8 keyLow = 0;
        for (u32 row = begin; row + 12 <= end && reader.has(row, 12) && reader.u8At(row) != 0xff; row += 12) {
          const u8 keyHigh = reader.u8At(row);
          const s8 panOverride = reader.s8At(row + 1);
          const s8 volumeAdjustment = reader.s8At(row + 2);
          const double gain = cpsVolumeAdjustmentGain(volumeAdjustment);
          addQSoundRegion(instruments, instrument, samples, sampleInfos, layout.version, reader.range(row, 12),
                          reader.be16(row + 4), reader.s8At(row + 6), reader.u8At(row + 7), reader.u8At(row + 8),
                          reader.u8At(row + 9), reader.u8At(row + 10), reader.u8At(row + 11),
                          KeyRange{.low = keyLow, .high = keyHigh},
                          panOverride == -1 ? 0.5 : panPositionFrom7Bit(static_cast<u8>(panOverride)),
                          signedAttenuationDb(gain));
          keyLow = keyHigh == 127 ? 127 : static_cast<u8>(keyHigh + 1);
        }
      }
    }
  } else {
    const bool rowIsEightBytes = layout.version < CpsVersion::Cps2V130 || layout.version == CpsVersion::Cps2V200 ||
                                 layout.version == CpsVersion::Cps2V201B;
    const u32 rowSize = rowIsEightBytes ? 8 : 4;
    for (u32 sourceBank = 0; sourceBank < layout.instrumentBanks; ++sourceBank) {
      const u32 pointer = layout.instrumentTableOffset + sourceBank * 2;
      if (!reader.has(pointer, 2)) {
        break;
      }
      const u32 begin = static_cast<u32>(layout.program.offset + reader.le16(pointer));
      u32 end = begin + rowSize * 256;
      if (sourceBank + 1 < layout.instrumentBanks && reader.has(pointer + 2, 2)) {
        const u32 next = static_cast<u32>(layout.program.offset + reader.le16(pointer + 2));
        if (next > begin) {
          end = next;
        }
      }
      end = static_cast<u32>(std::min<u64>(end, layout.program.endOffset()));
      for (u32 index = 0, row = begin; row + rowSize <= end && index < 256; row += rowSize, ++index) {
        if (sourceBank != 0 && reader.le16(row) == 0 && reader.u8At(row + 2) == 0) {
          break;
        }
        const InstrumentAddress address{.bank = sourceBank * 2 + index / 128, .program = index % 128};
        auto instrument = addInstrument(address.bank * 128 + address.program, address, reader.range(row, rowSize));
        const u32 sample = reader.le16(row);
        const s8 fine = reader.s8At(row + 2);
        if (rowIsEightBytes) {
          addQSoundRegion(instruments, instrument, samples, sampleInfos, layout.version, reader.range(row, rowSize),
                          sample, fine, reader.u8At(row + 3), reader.u8At(row + 4), reader.u8At(row + 5),
                          reader.u8At(row + 6), reader.u8At(row + 7));
        } else {
          const u8 articulation = reader.u8At(row + 3);
          if (!layout.articulationTableOffset || !reader.has(*layout.articulationTableOffset + articulation * 8, 5)) {
            instruments.warning("CPS instrument refers to a missing articulation", reader.range(row, rowSize));
            continue;
          }
          const u32 env = *layout.articulationTableOffset + articulation * 8;
          addQSoundRegion(instruments, instrument, samples, sampleInfos, layout.version, reader.range(row, rowSize),
                          sample, fine, reader.u8At(env), reader.u8At(env + 1), reader.u8At(env + 2),
                          reader.u8At(env + 3), reader.u8At(env + 4));
        }
      }
    }
  }

  if (instruments.empty()) {
    instruments.warning("CPS QSound instrument table contained no usable instruments", layout.program);
  }
  return refs;
}

}  // namespace vgmtrans::formats::cps
