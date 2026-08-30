/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/MoriSnes/MoriSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace vgmtrans::formats::mori_snes {

using namespace core;

Envelope driverEnvelope(u8 adsr1, u8 adsr2, u8 gain) { return snesDspEnvelope(adsr1, adsr2, gain); }

namespace {

constexpr double kTimerSeconds = 0.000125 * 0x4f;
const double kPitchTableCorrection = 12.0 * std::log2(4286.0 / 4096.0);
constexpr u32 kScriptCommandLimit = 131072;

[[nodiscard]] u16 relativeTarget(u16 continuation, s16 relative) {
  return static_cast<u16>(continuation + relative);
}

[[nodiscard]] s32 addFinePitch(s32 pitch256, s8 delta) {
  const s8 high = static_cast<s8>(pitch256 >> 8);
  const u8 low = static_cast<u8>(pitch256);
  const u16 initial = static_cast<u16>((static_cast<u16>(static_cast<u8>(high)) << 8) | low);
  u16 result = static_cast<u16>(initial + delta);
  if (delta < 0 && (result & 0x8000) != 0) {
    result = 0;
  }
  return static_cast<s8>(result >> 8) * 256 + static_cast<u8>(result);
}

[[nodiscard]] u8 commandSize(u8 status) {
  constexpr std::array<u8, 39> sizes{
      2, 1, 1, 1, 1, 1, 1, 1, 0, 0, 4, 2, 2, 0, 1, 0, 0, 1, 0, 0,
      0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1,
  };
  return status >= 0xc0 && status <= 0xe6 ? sizes[status - 0xc0] : 0;
}

struct ScriptFrame {
  enum class Kind : u8 {
    Call,
    Repeat,
  };

  Kind kind = Kind::Call;
  u16 address = 0;
  u16 remaining = 0;

  friend bool operator<(const ScriptFrame& left, const ScriptFrame& right) {
    return std::tie(left.kind, left.address, left.remaining) < std::tie(right.kind, right.address, right.remaining);
  }
};

struct ScriptState {
  u16 pc = 0;
  std::vector<ScriptFrame> stack;
  s32 pitch256 = 0;
  bool fineExplicit = false;
  u8 volume = 0xff;
  u8 pan = 0x10;
  u16 rowAddress = 0;
  bool absolutePitch = false;
  bool panExplicit = false;
  bool keyOn = false;

  friend bool operator<(const ScriptState& left, const ScriptState& right) {
    return std::tie(left.pc, left.stack, left.pitch256, left.fineExplicit, left.volume, left.pan, left.rowAddress,
                    left.absolutePitch, left.panExplicit, left.keyOn) <
           std::tie(right.pc, right.stack, right.pitch256, right.fineExplicit, right.volume, right.pan,
                    right.rowAddress, right.absolutePitch, right.panExplicit, right.keyOn);
  }
};

struct VoiceSample {
  s32 pitch256 = 0;
  u8 volume = 0xff;
};

struct ScriptAnalysis {
  u16 scriptAddress = 0;
  u16 rowAddress = 0;
  SourceRange scriptRange;
  s32 attackPitch256 = 0;
  u8 attackVolume = 0xff;
  u8 attackPan = 0x10;
  bool absolutePitch = false;
  bool panExplicit = false;
  std::vector<VoiceSample> samples;
  std::optional<u32> cycleStart;
  std::optional<u32> cycleLength;
};

void includeRange(u32 begin, u32 size, u32& minimum, u32& maximum) {
  minimum = std::min(minimum, begin);
  maximum = std::max(maximum, begin + size);
}

[[nodiscard]] std::optional<ScriptAnalysis> analyzeScript(ByteReader reader, const Layout& layout, u16 start) {
  if (!reader.has(start, 1)) {
    return std::nullopt;
  }

  ScriptState state{.pc = start};
  std::map<ScriptState, u32> visited;
  std::vector<VoiceSample> samples;
  std::optional<u32> keyOnTick;
  std::optional<u32> cycleStart;
  std::optional<u32> cycleLength;
  s32 attackPitch = 0;
  u8 attackVolume = 0xff;
  u8 attackPan = 0x10;
  bool attackAbsolute = false;
  bool attackPanExplicit = false;
  u16 firstRow = 0;
  u32 tick = 0;
  u32 minimum = start;
  u32 maximum = start;

  for (u32 commands = 0; commands < kScriptCommandLimit; ++commands) {
    if (!reader.has(state.pc, 1)) {
      break;
    }
    if (const auto found = visited.find(state); found != visited.end()) {
      if (state.keyOn && keyOnTick && tick > found->second && found->second >= *keyOnTick) {
        cycleStart = found->second - *keyOnTick;
        cycleLength = tick - found->second;
      }
      break;
    }
    visited.emplace(state, tick);

    const u16 command = state.pc;
    const u8 status = reader.u8At(command);
    includeRange(command, 1, minimum, maximum);
    if (status < 0x80) {
      const u32 wait = status == 0 ? 256 : status;
      if (state.keyOn) {
        samples.insert(samples.end(), wait, VoiceSample{.pitch256 = state.pitch256, .volume = state.volume});
      }
      tick += wait;
      state.pc = static_cast<u16>(command + 1);
      continue;
    }
    if (status < 0xc0 || status > 0xe6) {
      break;
    }

    const u8 size = commandSize(status);
    const u16 operands = static_cast<u16>(command + 1);
    if (!reader.has(operands, size)) {
      break;
    }
    includeRange(operands, size, minimum, maximum);
    const u16 continuation = static_cast<u16>(operands + size);
    switch (status) {
      case 0xc1:
        state.pan = (reader.u8At(operands) & 0x80) != 0 ? 0x10 : std::min<u8>(reader.u8At(operands), 0x20);
        state.panExplicit = true;
        break;
      case 0xc5:
        state.volume = reader.u8At(operands);
        break;
      case 0xc7:
        state.pitch256 = (state.pitch256 & ~0xff) | reader.u8At(operands);
        state.fineExplicit = true;
        break;
      case 0xcb:
        state.pc = relativeTarget(continuation, static_cast<s16>(reader.le16(operands)));
        continue;
      case 0xcc:
        if (state.stack.size() >= 10) {
          return std::nullopt;
        }
        state.stack.push_back(ScriptFrame{.kind = ScriptFrame::Kind::Call, .address = continuation});
        state.pc = relativeTarget(continuation, static_cast<s16>(reader.le16(operands)));
        continue;
      case 0xcd:
        if (state.stack.empty() || state.stack.back().kind != ScriptFrame::Kind::Call) {
          return std::nullopt;
        }
        state.pc = state.stack.back().address;
        state.stack.pop_back();
        continue;
      case 0xce: {
        if (state.stack.size() >= 10) {
          return std::nullopt;
        }
        const u8 count = reader.u8At(operands);
        state.stack.push_back(ScriptFrame{
            .kind = ScriptFrame::Kind::Repeat,
            .address = continuation,
            .remaining = static_cast<u16>(count == 0 ? 256 : count),
        });
        break;
      }
      case 0xcf:
        if (state.stack.empty() || state.stack.back().kind != ScriptFrame::Kind::Repeat) {
          return std::nullopt;
        }
        if (--state.stack.back().remaining != 0) {
          state.pc = state.stack.back().address;
          continue;
        }
        state.stack.pop_back();
        break;
      case 0xd0:
        commands = kScriptCommandLimit;
        continue;
      case 0xd7:
        state.pitch256 = static_cast<s8>(reader.u8At(operands)) * 256 + static_cast<u8>(state.pitch256);
        state.absolutePitch = true;
        break;
      case 0xd8:
        state.pitch256 = static_cast<s8>(static_cast<u8>((state.pitch256 >> 8) +
                                                        static_cast<s8>(reader.u8At(operands)))) *
                             256 +
                         static_cast<u8>(state.pitch256);
        break;
      case 0xd9:
        state.pitch256 = addFinePitch(state.pitch256, static_cast<s8>(reader.u8At(operands)));
        break;
      case 0xda:
        state.keyOn = true;
        if (!keyOnTick) {
          keyOnTick = tick;
          attackPitch = state.pitch256;
          attackVolume = state.volume;
          attackPan = state.pan;
          attackAbsolute = state.absolutePitch;
          attackPanExplicit = state.panExplicit;
        }
        break;
      case 0xdb:
        state.keyOn = false;
        break;
      case 0xdc:
        state.volume = static_cast<u8>(state.volume + static_cast<s8>(reader.u8At(operands)));
        break;
      case 0xde:
        state.rowAddress = relativeTarget(continuation, static_cast<s16>(reader.le16(operands)));
        if (firstRow == 0) {
          firstRow = state.rowAddress;
        }
        if (!reader.has(state.rowAddress, 7)) {
          return std::nullopt;
        }
        break;
      case 0xe2: {
        const u8 index = reader.u8At(operands);
        if (reader.has(layout.presetTableAddress + index, 1) &&
            reader.has(layout.presetTableAddress + 4u + index, 1)) {
          state.pitch256 = static_cast<s8>(reader.u8At(layout.presetTableAddress + 4u + index)) * 256 +
                           reader.u8At(layout.presetTableAddress + index);
          state.absolutePitch = true;
          state.fineExplicit = true;
        }
        break;
      }
      case 0xe3: {
        const u8 index = reader.u8At(operands);
        if (reader.has(layout.presetTableAddress + index, 1)) {
          state.volume = reader.u8At(layout.presetTableAddress + index);
        }
        break;
      }
      case 0xe4: {
        const u8 index = reader.u8At(operands);
        if (reader.has(layout.presetTableAddress + index, 1)) {
          state.pan = std::min<u8>(reader.u8At(layout.presetTableAddress + index), 0x20);
          state.panExplicit = true;
        }
        break;
      }
      case 0xe5: {
        const u8 index = reader.u8At(operands);
        u32 wait =
            reader.has(layout.presetTableAddress + index, 1) ? reader.u8At(layout.presetTableAddress + index) : 1;
        wait = std::max<u32>(wait, 1);
        if (state.keyOn) {
          samples.insert(samples.end(), wait, VoiceSample{.pitch256 = state.pitch256, .volume = state.volume});
        }
        tick += wait;
        break;
      }
      default:
        break;
    }
    state.pc = continuation;
  }

  if (firstRow == 0 || !reader.has(firstRow, 7)) {
    return std::nullopt;
  }
  return ScriptAnalysis{
      .scriptAddress = start,
      .rowAddress = firstRow,
      .scriptRange = reader.range(minimum, maximum - minimum),
      .attackPitch256 = attackPitch,
      .attackVolume = attackVolume,
      .attackPan = attackPan,
      .absolutePitch = attackAbsolute,
      .panExplicit = attackPanExplicit,
      .samples = std::move(samples),
      .cycleStart = cycleStart,
      .cycleLength = cycleLength,
  };
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

[[nodiscard]] InstrumentModulation modulation(const ScriptAnalysis& script) {
  InstrumentModulation result;
  if (!script.cycleStart || !script.cycleLength || *script.cycleLength < 2 ||
      *script.cycleStart + *script.cycleLength > script.samples.size()) {
    return result;
  }
  const auto first = script.samples.begin() + *script.cycleStart;
  const auto last = first + *script.cycleLength;
  const double rate = 1.0 / (*script.cycleLength * kTimerSeconds);

  std::vector<s32> pitch;
  pitch.reserve(*script.cycleLength);
  std::transform(first, last, std::back_inserter(pitch), [](const VoiceSample& sample) { return sample.pitch256; });
  const auto [minimumPitch, maximumPitch] = std::minmax_element(pitch.begin(), pitch.end());
  if (*minimumPitch != *maximumPitch) {
    const double center = (*minimumPitch + *maximumPitch) / 2.0;
    const auto changed = std::find_if(first, last, [&](const VoiceSample& sample) {
      return sample.pitch256 != center;
    });
    const double delay = (*script.cycleStart + std::distance(first, changed)) * kTimerSeconds;
    result.vibrato = VibratoSpec{
        .maxDepthCents = (*maximumPitch - *minimumPitch) * (50.0 / 256.0),
        .rateHertz = ModulationRange{.minimum = rate, .maximum = rate},
        .waveform = inferWaveform(pitch),
        .delaySeconds = ModulationRange{.minimum = delay, .maximum = delay},
        .depthMode = ModulationDepthMode::Fixed,
    };
  }

  std::vector<s32> volume;
  volume.reserve(*script.cycleLength);
  std::transform(first, last, std::back_inserter(volume), [](const VoiceSample& sample) { return sample.volume; });
  const auto [minimumVolume, maximumVolume] = std::minmax_element(volume.begin(), volume.end());
  if (*minimumVolume != *maximumVolume && script.attackVolume != 0) {
    const auto decibels = [&](u8 value) {
      return value == 0 ? -96.0 : 20.0 * std::log10(value / static_cast<double>(script.attackVolume));
    };
    const double low = decibels(static_cast<u8>(*minimumVolume));
    const double high = decibels(static_cast<u8>(*maximumVolume));
    const u8 cycleBaseline = *script.cycleStart == 0 ? script.attackVolume
                                                     : script.samples[*script.cycleStart - 1].volume;
    const auto changed = std::find_if(first, last, [&](const VoiceSample& sample) {
      return sample.volume != cycleBaseline;
    });
    const double delay = (*script.cycleStart + std::distance(first, changed)) * kTimerSeconds;
    result.tremolo = TremoloSpec{
        .maxDepthDb = std::max(std::abs(low), std::abs(high)),
        .rateHertz = ModulationRange{.minimum = rate, .maximum = rate},
        .waveform = inferWaveform(volume),
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
  ScriptAnalysis script;
};

[[nodiscard]] std::vector<std::pair<u8, u16>> percussionScripts(ByteReader reader, u16 descriptor) {
  std::vector<std::pair<u8, u16>> scripts;
  u32 entry = descriptor + 1u;
  u32 boundary = kAramSize;
  for (u8 key = 0; key < 32 && entry + 2 <= boundary && reader.has(entry, 2); ++key) {
    const u16 continuation = static_cast<u16>(entry + 2);
    const u16 script = relativeTarget(continuation, static_cast<s16>(reader.le16(entry)));
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
  std::vector<Patch> patches;
  for (const u16 descriptor : references.descriptors) {
    if (!reader.has(descriptor, 1)) {
      continue;
    }
    const bool percussion = (reader.u8At(descriptor) & 1) != 0;
    if (!percussion) {
      if (auto script = analyzeScript(reader, layout, static_cast<u16>(descriptor + 1))) {
        std::set<u8> keys;
        if (const auto found = references.noteKeys.find(descriptor); found != references.noteKeys.end()) {
          keys = found->second;
        }
        patches.push_back(Patch{
            .identity = descriptor,
            .descriptor = descriptor,
            .outputKeys = std::move(keys),
            .script = std::move(*script),
        });
      }
      continue;
    }

    const auto used = references.percussionKeys.find(descriptor);
    for (const auto& [rawKey, address] : percussionScripts(reader, descriptor)) {
      auto script = analyzeScript(reader, layout, address);
      if (!script) {
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
          .script = std::move(*script),
      });
    }
  }
  for (const auto& [scriptAddress, keys] : references.directScriptKeys) {
    if (auto script = analyzeScript(reader, layout, scriptAddress)) {
      patches.push_back(Patch{
          .identity = kDirectInstrumentFlag | scriptAddress,
          .outputKeys = keys.empty() ? std::set<u8>{60} : keys,
          .script = std::move(*script),
      });
    }
  }
  return patches;
}

[[nodiscard]] std::vector<u8> referencedSrcns(ByteReader reader, const std::vector<Patch>& patches) {
  std::set<u8> unique;
  for (const Patch& patch : patches) {
    if (reader.has(patch.script.rowAddress, 1)) {
      unique.insert(reader.u8At(patch.script.rowAddress));
    }
  }
  return {unique.begin(), unique.end()};
}

[[nodiscard]] double attenuationDb(u8 volume) {
  return volume == 0 ? 96.0 : -20.0 * std::log10(volume / 256.0);
}

[[nodiscard]] double baseUnityKey(ByteReader reader, const Patch& patch, u8 outputKey) {
  const u32 row = patch.script.rowAddress;
  const double rowPitch = static_cast<s8>(reader.u8At(row + 5)) + reader.u8At(row + 6) / 256.0;
  const double scriptPitch = patch.script.attackPitch256 / 256.0;
  if (patch.script.absolutePitch) {
    return outputKey + 72.0 - (scriptPitch + rowPitch + kPitchTableCorrection);
  }
  return 72.0 - (scriptPitch + rowPitch + kPitchTableCorrection);
}

[[nodiscard]] Region makeRegion(ByteReader reader, const Patch& patch, SampleRef sample, std::optional<u8> key) {
  const u32 row = patch.script.rowAddress;
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
      .modulation = modulation(patch.script),
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
      if (patch.identity != identity || !reader.has(patch.script.rowAddress, 7)) {
        continue;
      }
      const u8 srcn = reader.u8At(patch.script.rowAddress);
      const auto sample = samples.findSrcn(srcn);
      if (!sample) {
        continue;
      }
      instrument.source(patch.rawKey ? fmt::format("Voice script (key {})", *patch.rawKey) : "Voice script",
                        patch.script.scriptRange, "mori-snes-voice-script")
          .parent(root)
          .description(fmt::format("Fixed-clock script at ${:04X}", patch.script.scriptAddress));

      const bool fixedPitch = patch.script.absolutePitch || patch.rawKey.has_value();
      if (!fixedPitch) {
        instrument
            .region(*sample, makeRegion(reader, patch, *sample, std::nullopt))
            .source("Region", reader.range(patch.script.rowAddress, 7), "mori-snes-region")
            .parent(root)
            .description(fmt::format("SRCN {}, DSP voice row ${:04X}", srcn, patch.script.rowAddress));
        continue;
      }
      for (const u8 key : patch.outputKeys) {
        instrument
            .region(*sample, makeRegion(reader, patch, *sample, key))
            .source(fmt::format("Region (key {})", key), reader.range(patch.script.rowAddress, 7),
                    "mori-snes-region")
            .parent(root)
            .description(fmt::format("SRCN {}, DSP voice row ${:04X}", srcn, patch.script.rowAddress));
      }
    }
  }
  return bank;
}

}  // namespace vgmtrans::formats::mori_snes
