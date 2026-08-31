/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/MoriSnes/MoriSnes.h"
#include "value/formats/MoriSnes/MoriSnesVoiceScript.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace vgmtrans::formats::mori_snes {

using namespace core;

Envelope driverEnvelope(u8 adsr1, u8 adsr2, u8 gain) { return snesDspEnvelope(adsr1, adsr2, gain); }

namespace {

const double kPitchTableCorrection = 12.0 * std::log2(4286.0 / 4096.0);

[[nodiscard]] u16 relativeTarget(u16 continuation, s16 relative) {
  return static_cast<u16>(continuation + relative);
}

struct CycleSamples {
  std::vector<s32> pitch;
  std::vector<s32> volume;
  u8 precedingVolume = 0xff;
};

[[nodiscard]] CycleSamples sampleCycle(const VoiceScriptAnalysis& script) {
  CycleSamples samples;
  if (!script.cycleStart || !script.cycleLength) {
    return samples;
  }

  size_t nextPoint = 0;
  s32 pitch = script.attackPitch256;
  u8 volume = script.attackVolume;
  const auto applyThrough = [&](u32 tick) {
    while (nextPoint < script.points.size() && script.points[nextPoint].tick <= tick) {
      pitch = script.points[nextPoint].pitch256;
      volume = script.points[nextPoint].volume;
      ++nextPoint;
    }
  };
  if (*script.cycleStart != 0) {
    applyThrough(*script.cycleStart - 1);
  }
  samples.precedingVolume = volume;
  samples.pitch.reserve(*script.cycleLength);
  samples.volume.reserve(*script.cycleLength);
  for (u32 offset = 0; offset < *script.cycleLength; ++offset) {
    applyThrough(*script.cycleStart + offset);
    samples.pitch.push_back(pitch);
    samples.volume.push_back(volume);
  }
  return samples;
}

[[nodiscard]] LfoWaveform inferWaveform(const std::vector<s32>& samples) {
  std::set<s32> unique(samples.begin(), samples.end());
  if (unique.size() <= 2) {
    return LfoWaveform::Square;
  }
  u32 rising = 0;
  u32 falling = 0;
  for (u32 index = 1; index < samples.size(); ++index) {
    rising += samples[index] > samples[index - 1];
    falling += samples[index] < samples[index - 1];
  }
  if (rising > falling * 4u) {
    return LfoWaveform::SawtoothUp;
  }
  if (falling > rising * 4u) {
    return LfoWaveform::SawtoothDown;
  }
  return LfoWaveform::Triangle;
}

[[nodiscard]] InstrumentModulation modulation(const VoiceScriptAnalysis& script, double timerSeconds) {
  InstrumentModulation result;
  if (!script.cycleStart || !script.cycleLength || *script.cycleLength < 2) {
    return result;
  }
  const CycleSamples samples = sampleCycle(script);
  const double rate = 1.0 / (*script.cycleLength * timerSeconds);

  const auto [minimumPitch, maximumPitch] = std::minmax_element(samples.pitch.begin(), samples.pitch.end());
  if (*minimumPitch != *maximumPitch) {
    const double center = (*minimumPitch + *maximumPitch) / 2.0;
    const auto changed = std::ranges::find_if(samples.pitch, [&](s32 pitch) { return pitch != center; });
    const double delay = (*script.cycleStart + std::distance(samples.pitch.begin(), changed)) * timerSeconds;
    result.vibrato = VibratoSpec{
        .maxDepthCents = (*maximumPitch - *minimumPitch) * (50.0 / 256.0),
        .rateHertz = ModulationRange{.minimum = rate, .maximum = rate},
        .waveform = inferWaveform(samples.pitch),
        .delaySeconds = ModulationRange{.minimum = delay, .maximum = delay},
        .depthMode = ModulationDepthMode::Fixed,
    };
  }

  const auto [minimumVolume, maximumVolume] = std::minmax_element(samples.volume.begin(), samples.volume.end());
  if (*minimumVolume != *maximumVolume && script.attackVolume != 0) {
    const auto decibels = [&](u8 value) {
      return value == 0 ? -96.0 : 20.0 * std::log10(value / static_cast<double>(script.attackVolume));
    };
    const double low = decibels(static_cast<u8>(*minimumVolume));
    const double high = decibels(static_cast<u8>(*maximumVolume));
    const auto changed = std::ranges::find_if(samples.volume, [&](s32 volume) {
      return volume != samples.precedingVolume;
    });
    const double delay = (*script.cycleStart + std::distance(samples.volume.begin(), changed)) * timerSeconds;
    result.tremolo = TremoloSpec{
        .maxDepthDb = std::max(std::abs(low), std::abs(high)),
        .rateHertz = ModulationRange{.minimum = rate, .maximum = rate},
        .waveform = inferWaveform(samples.volume),
        .gainMode = *maximumVolume <= script.attackVolume ? TremoloGainMode::NoBoost
                                                          : TremoloGainMode::BipolarAroundNominal,
        .delaySeconds = ModulationRange{.minimum = delay, .maximum = delay},
        .depthMode = ModulationDepthMode::Fixed,
    };
  }
  return result;
}

struct Patch {
  u32 identity = 0;
  u16 descriptor = 0;
  std::optional<u8> rawKey;
  std::set<u8> outputKeys;
  VoiceScriptAnalysis script;
};

[[nodiscard]] std::vector<std::pair<u8, u16>> percussionScripts(ByteReader reader, const Layout& layout,
                                                                u16 descriptor) {
  std::vector<std::pair<u8, u16>> scripts;
  u32 entry = descriptor + 1u;
  u32 boundary = kAramSize;
  const bool absolute = driverTraits(layout.version).absolutePercussionPointers;
  for (u8 key = 0; key < 32 && entry + 2 <= boundary && reader.has(entry, 2); ++key) {
    const u16 continuation = static_cast<u16>(entry + 2);
    const u16 script = absolute ? reader.le16(entry)
                                : relativeTarget(continuation, static_cast<s16>(reader.le16(entry)));
    if (!reader.has(script, 1)) {
      break;
    }
    scripts.emplace_back(key, script);
    boundary = std::min<u32>(boundary, script);
    entry += 2;
  }
  return scripts;
}

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout,
                                                const ReferencedInstruments& references) {
  const DriverConfig driver{
      .data = reader,
      .traits = driverTraits(layout.version),
      .presetTable = layout.presetTableAddress,
      .presetPitchHigh = layout.presetPitchHighAddress,
      .panTable = layout.panTableAddress,
  };
  std::vector<Patch> patches;
  for (const u16 descriptor : references.descriptors) {
    if (!reader.has(descriptor, 1)) {
      continue;
    }
    const bool percussion = (reader.u8At(descriptor) & 1) != 0;
    if (!percussion) {
      auto script = analyzeVoiceScript(driver, static_cast<u16>(descriptor + 1));
      if (script.rowAddress) {
        std::set<u8> keys;
        if (const auto found = references.noteKeys.find(descriptor); found != references.noteKeys.end()) {
          keys = found->second;
        }
        patches.push_back(Patch{
            .identity = descriptor,
            .descriptor = descriptor,
            .outputKeys = std::move(keys),
            .script = std::move(script),
        });
      }
      continue;
    }

    const auto used = references.percussionKeys.find(descriptor);
    for (const auto& [rawKey, address] : percussionScripts(reader, layout, descriptor)) {
      auto script = analyzeVoiceScript(driver, address);
      if (!script.rowAddress) {
        continue;
      }
      std::set<u8> keys{rawKey};
      if (used != references.percussionKeys.end()) {
        if (const auto found = used->second.find(rawKey); found != used->second.end() && !found->second.empty()) {
          keys = found->second;
        }
      }
      patches.push_back(Patch{
          .identity = descriptor,
          .descriptor = descriptor,
          .rawKey = rawKey,
          .outputKeys = std::move(keys),
          .script = std::move(script),
      });
    }
  }
  for (const auto& [scriptAddress, keys] : references.directScriptKeys) {
    auto script = analyzeVoiceScript(driver, scriptAddress);
    if (script.rowAddress) {
      patches.push_back(Patch{
          .identity = kDirectInstrumentFlag | scriptAddress,
          .outputKeys = keys.empty() ? std::set<u8>{60} : keys,
          .script = std::move(script),
      });
    }
  }
  return patches;
}

[[nodiscard]] std::vector<u8> referencedSrcns(ByteReader reader, const std::vector<Patch>& patches) {
  std::set<u8> unique;
  for (const Patch& patch : patches) {
    if (patch.script.rowAddress && reader.has(*patch.script.rowAddress, 1)) {
      unique.insert(reader.u8At(*patch.script.rowAddress));
    }
  }
  return {unique.begin(), unique.end()};
}

[[nodiscard]] double attenuationDb(u8 volume) {
  return volume == 0 ? 96.0 : -20.0 * std::log10(volume / 256.0);
}

[[nodiscard]] double baseUnityKey(ByteReader reader, const Patch& patch, u8 outputKey) {
  const u32 row = *patch.script.rowAddress;
  const double rowPitch = static_cast<s8>(reader.u8At(row + 5)) + reader.u8At(row + 6) / 256.0;
  const double scriptPitch = patch.script.attackPitch256 / 256.0;
  if (patch.script.attackAbsolutePitch) {
    return outputKey + 72.0 - (scriptPitch + rowPitch + kPitchTableCorrection);
  }
  return 72.0 - (scriptPitch + rowPitch + kPitchTableCorrection);
}

[[nodiscard]] Region makeRegion(ByteReader reader, const Layout& layout, const Patch& patch, SampleRef sample,
                                std::optional<u8> key) {
  const u32 row = *patch.script.rowAddress;
  Region region{
      .sample = sample,
      .range = reader.range(row, 7),
      .unityKey = baseUnityKey(reader, patch, key.value_or(60)),
      .envelope = driverEnvelope(reader.u8At(row + 1), reader.u8At(row + 2), reader.u8At(row + 3)),
      // Voice-script C1 overrides the copied source pan at note-on. Sequence
      // playback emits that effective table-based balance per attack so it is
      // not combined a second time with a region pan.
      .pan = 0.5,
      .attenuationDb = attenuationDb(patch.script.attackVolume),
      .modulation = modulation(patch.script, driverTraits(layout.version).timerSeconds()),
  };
  if (key) {
    region.keyRange = KeyRange{.low = *key, .high = *key};
  }
  return region;
}

}  // namespace

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const ReferencedInstruments& references, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<Patch> patches = collectPatches(reader, layout, references);
  if (patches.empty()) {
    return std::nullopt;
  }
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, referencedSrcns(reader, patches));
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  const SnesBrrSampleRefs samples = addSnesBrrSamples(bank.localSamples(), reader, catalog);
  u32 exportProgram = 0;
  std::set<u32> identities;
  for (const Patch& patch : patches) {
    identities.insert(patch.identity);
  }
  for (const u32 identity : identities) {
    const auto first = std::ranges::find(patches, identity, &Patch::identity);
    if (first == patches.end()) {
      continue;
    }
    const bool direct = (identity & kDirectInstrumentFlag) != 0;
    const u16 descriptor = first->descriptor;
    const SourceRange instrumentRange = direct ? first->script.scriptRange : reader.range(descriptor, 1);
    auto instrument = bank.instruments().append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = exportProgram >> 7, .program = exportProgram & 0x7f},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = identity},
        .name = direct ? fmt::format("Voice Script ${:04X}", first->script.scriptAddress)
                       : fmt::format("Instrument ${:04X}", descriptor),
        .range = instrumentRange,
    });
    ++exportProgram;
    const SourceAnnotationId root =
        instrument.source(direct ? fmt::format("Voice Script ${:04X}", first->script.scriptAddress)
                                 : fmt::format("Instrument ${:04X}", descriptor),
                          instrumentRange,
                          "mori-snes-instrument")
            .description(direct ? "Directly selected voice script"
                                : (reader.u8At(descriptor) & 1) != 0 ? "Percussion script directory"
                                                                      : "Melodic voice script")
            .id();

    for (const Patch& patch : patches) {
      if (patch.identity != identity || !patch.script.rowAddress || !reader.has(*patch.script.rowAddress, 7)) {
        continue;
      }
      const u16 row = *patch.script.rowAddress;
      const u8 srcn = reader.u8At(row);
      const auto sample = samples.findSrcn(srcn);
      if (!sample) {
        continue;
      }
      instrument.source(patch.rawKey ? fmt::format("Voice script (key {})", *patch.rawKey) : "Voice script",
                        patch.script.scriptRange, "mori-snes-voice-script")
          .parent(root)
          .description(fmt::format("Fixed-clock script at ${:04X}", patch.script.scriptAddress));

      const bool fixedPitch = patch.script.attackAbsolutePitch || patch.rawKey.has_value();
      if (!fixedPitch) {
        instrument
            .region(*sample, makeRegion(reader, layout, patch, *sample, std::nullopt))
            .source("Region", reader.range(row, 7), "mori-snes-region")
            .parent(root)
            .description(fmt::format("SRCN {}, DSP voice row ${:04X}", srcn, row));
        continue;
      }
      for (const u8 key : patch.outputKeys) {
        instrument
            .region(*sample, makeRegion(reader, layout, patch, *sample, key))
            .source(fmt::format("Region (key {})", key), reader.range(row, 7), "mori-snes-region")
            .parent(root)
            .description(fmt::format("SRCN {}, DSP voice row ${:04X}", srcn, row));
      }
    }
  }
  return bank;
}

}  // namespace vgmtrans::formats::mori_snes
