/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/MoriSnes/MoriSnesVoiceScript.h"

#include <algorithm>
#include <map>
#include <tuple>
#include <utility>

namespace vgmtrans::formats::mori_snes {

using namespace core;

namespace {

[[nodiscard]] u16 relativeTarget(u16 continuation, s16 relative) {
  return static_cast<u16>(continuation + relative);
}

[[nodiscard]] s32 addFinePitch(s32 pitch256, s8 delta, bool absolutePitch) {
  const u16 initial = static_cast<u16>((static_cast<u16>(static_cast<u8>(pitch256 >> 8)) << 8) |
                                       static_cast<u8>(pitch256));
  u16 result = static_cast<u16>(initial + delta);
  // Before D7/E2 replaces the inherited note, a negative result is a relative
  // offset. Once the pitch is absolute, the driver clamps it at zero.
  if (absolutePitch && delta < 0 && (result & 0x8000) != 0) {
    result = 0;
  }
  return static_cast<s8>(result >> 8) * 256 + static_cast<u8>(result);
}

struct ScriptFrame {
  bool repeat = false;
  u16 address = 0;
  u16 remaining = 0;

  friend bool operator<(const ScriptFrame& left, const ScriptFrame& right) {
    return std::tie(left.repeat, left.address, left.remaining) <
           std::tie(right.repeat, right.address, right.remaining);
  }
};

struct ScriptState {
  u16 address = 0;
  std::vector<ScriptFrame> stack;
  s32 pitch256 = 0;
  bool fineExplicit = false;
  bool absolutePitch = false;
  u8 volume = 0xff;
  u8 pan = 0;
  bool panExplicit = false;
  bool keyOn = false;
  std::optional<u16> rowAddress;

  friend bool operator<(const ScriptState& left, const ScriptState& right) {
    return std::tie(left.address, left.stack, left.pitch256, left.fineExplicit, left.absolutePitch, left.volume,
                    left.pan, left.panExplicit, left.keyOn, left.rowAddress) <
           std::tie(right.address, right.stack, right.pitch256, right.fineExplicit, right.absolutePitch,
                    right.volume, right.pan, right.panExplicit, right.keyOn, right.rowAddress);
  }
};

}  // namespace

VoiceScriptAnalysis analyzeVoiceScript(const DriverConfig& driver, u16 script, std::optional<u8> percussionNote) {
  const ByteReader reader = driver.data;
  if (script == 0 || !reader.has(script, 1)) {
    return {};
  }
  if (percussionNote) {
    const u16 entry = static_cast<u16>(script + (*percussionNote & 0x1f) * 2u);
    if (!reader.has(entry, 2)) {
      return {};
    }
    script = driver.traits.absolutePercussionPointers
                 ? reader.le16(entry)
                 : relativeTarget(static_cast<u16>(entry + 2), static_cast<s16>(reader.le16(entry)));
    if (script == 0 || !reader.has(script, 1)) {
      return {};
    }
  }

  VoiceScriptAnalysis result{.scriptAddress = script, .attackPan = driver.traits.initialPan};
  ScriptState state{.address = script, .pan = driver.traits.initialPan};
  std::map<ScriptState, u32> visited;
  std::optional<u32> keyOnTick;
  std::optional<size_t> activeAttack;
  u32 tick = 0;
  u32 minimum = script;
  u32 maximum = script;

  const auto finish = [&]() {
    result.scriptRange = reader.range(minimum, maximum - minimum);
    return result;
  };
  const auto point = [&]() {
    if (!state.keyOn || !keyOnTick) {
      return;
    }
    VoiceScriptAnalysis::Point value{
        .tick = tick - *keyOnTick,
        .pitch256 = state.pitch256,
        .fineExplicit = state.fineExplicit,
        .volume = state.volume,
    };
    if (!result.points.empty() && result.points.back().tick == value.tick) {
      result.points.back() = value;
    } else {
      result.points.push_back(value);
    }
  };

  for (u32 commands = 0; commands < kCommandLimit; ++commands) {
    const auto [prior, inserted] = visited.emplace(state, tick);
    if (!inserted) {
      if (state.keyOn && keyOnTick && prior->second >= *keyOnTick && tick > prior->second) {
        const u32 cycleBegin = prior->second - *keyOnTick;
        const u32 cycleEnd = tick - *keyOnTick;
        s32 minimumPitch = state.pitch256;
        s32 maximumPitch = state.pitch256;
        for (const VoiceScriptAnalysis::Point& value : result.points) {
          if (value.tick >= cycleBegin && value.tick < cycleEnd) {
            minimumPitch = std::min(minimumPitch, value.pitch256);
            maximumPitch = std::max(maximumPitch, value.pitch256);
          }
        }
        result.cycleStart = cycleBegin;
        result.cycleLength = cycleEnd - cycleBegin;
        result.cyclePitchCenter256 = (minimumPitch + maximumPitch) / 2.0;
        result.cycleFineExplicit = state.fineExplicit;
        result.cycleVolume = state.volume;
      }
      return finish();
    }
    if (!reader.has(state.address, 1)) {
      return finish();
    }

    const u16 commandAddress = state.address;
    const u8 status = reader.u8At(state.address++);
    minimum = std::min(minimum, static_cast<u32>(commandAddress));
    maximum = std::max(maximum, static_cast<u32>(commandAddress) + 1);
    if (status < 0x80) {
      tick += status == 0 ? 256 : status;
      continue;
    }
    if (!isCommand(driver.traits.version, status)) {
      return finish();
    }

    const u8 size = commandSize(driver.traits.version, status);
    if (!reader.has(state.address, size)) {
      return finish();
    }
    const u16 operands = state.address;
    const u16 continuation = static_cast<u16>(operands + size);
    minimum = std::min(minimum, static_cast<u32>(operands));
    maximum = std::max(maximum, static_cast<u32>(operands) + size);
    const std::optional<u8> canonical = canonicalCommand(driver.traits.version, status);
    if (!canonical) {
      state.address = continuation;
      continue;
    }

    switch (*canonical) {
      case 0xc1:
        state.pan = (reader.u8At(operands) & 0x80) != 0
                        ? driver.traits.initialPan
                        : std::min(reader.u8At(operands), driver.traits.maximumPan);
        state.panExplicit = true;
        break;
      case 0xc5:
        state.volume = reader.u8At(operands);
        point();
        break;
      case 0xc7:
        state.pitch256 = (state.pitch256 & ~0xff) | reader.u8At(operands);
        state.fineExplicit = true;
        point();
        break;
      case 0xcb:
        state.address = relativeTarget(continuation, static_cast<s16>(reader.le16(operands)));
        continue;
      case 0xcc:
        if (state.stack.size() >= 10) {
          return finish();
        }
        state.stack.push_back(ScriptFrame{.address = continuation});
        state.address = relativeTarget(continuation, static_cast<s16>(reader.le16(operands)));
        continue;
      case 0xcd:
        if (state.stack.empty() || state.stack.back().repeat) {
          return finish();
        }
        state.address = state.stack.back().address;
        state.stack.pop_back();
        continue;
      case 0xce: {
        if (state.stack.size() >= 10) {
          return finish();
        }
        const u8 count = reader.u8At(operands);
        state.stack.push_back(ScriptFrame{
            .repeat = true,
            .address = continuation,
            .remaining = static_cast<u16>(count == 0 ? 256 : count),
        });
        break;
      }
      case 0xcf:
        if (state.stack.empty() || !state.stack.back().repeat) {
          return finish();
        }
        if (--state.stack.back().remaining != 0) {
          state.address = state.stack.back().address;
          continue;
        }
        state.stack.pop_back();
        break;
      case 0xd0:
        result.scriptEnd = keyOnTick ? tick - *keyOnTick : tick;
        return finish();
      case 0xd7:
        state.pitch256 = static_cast<s8>(reader.u8At(operands)) * 256 + static_cast<u8>(state.pitch256);
        state.absolutePitch = true;
        point();
        break;
      case 0xd8:
        state.pitch256 =
            static_cast<s8>(static_cast<u8>((state.pitch256 >> 8) + static_cast<s8>(reader.u8At(operands)))) * 256 +
            static_cast<u8>(state.pitch256);
        point();
        break;
      case 0xd9:
        state.pitch256 =
            addFinePitch(state.pitch256, static_cast<s8>(reader.u8At(operands)), state.absolutePitch);
        point();
        break;
      case 0xda:
        if (!keyOnTick) {
          keyOnTick = tick;
          result.attackPitch256 = state.pitch256;
          result.attackFineExplicit = state.fineExplicit;
          result.attackAbsolutePitch = state.absolutePitch;
          result.attackVolume = state.volume;
          result.attackPan = state.pan;
          result.attackPanExplicit = state.panExplicit;
        }
        if (state.keyOn && activeAttack && !result.attacks[*activeAttack].keyOff) {
          result.attacks[*activeAttack].keyOff = tick - *keyOnTick - result.attacks[*activeAttack].tick;
        }
        state.keyOn = true;
        result.attacks.push_back(VoiceScriptAnalysis::Attack{
            .tick = tick - *keyOnTick,
            .pitch256 = state.pitch256,
            .fineExplicit = state.fineExplicit,
            .volume = state.volume,
            .pan = state.pan,
            .panExplicit = state.panExplicit,
        });
        activeAttack = result.attacks.size() - 1;
        point();
        break;
      case 0xdb:
        if (keyOnTick && state.keyOn && activeAttack) {
          result.attacks[*activeAttack].keyOff = tick - *keyOnTick - result.attacks[*activeAttack].tick;
        }
        state.keyOn = false;
        activeAttack.reset();
        break;
      case 0xdc:
        state.volume = static_cast<u8>(state.volume + static_cast<s8>(reader.u8At(operands)));
        point();
        break;
      case 0xde: {
        const u16 row = relativeTarget(continuation, static_cast<s16>(reader.le16(operands)));
        state.rowAddress = row;
        if (reader.has(row, 7)) {
          if (!result.rowAddress) {
            result.rowAddress = row;
          }
          if (!keyOnTick) {
            result.releaseDelay = reader.u8At(row + 4);
          }
        }
        break;
      }
      case 0xe2: {
        const u8 index = reader.u8At(operands);
        if (reader.has(driver.presetTable + index, 1) && reader.has(driver.presetPitchHigh + index, 1)) {
          state.pitch256 = static_cast<s8>(reader.u8At(driver.presetPitchHigh + index)) * 256 +
                           reader.u8At(driver.presetTable + index);
          state.fineExplicit = true;
          state.absolutePitch = true;
          point();
        }
        break;
      }
      case 0xe3: {
        const u8 index = reader.u8At(operands);
        if (reader.has(driver.presetTable + index, 1)) {
          state.volume = reader.u8At(driver.presetTable + index);
          point();
        }
        break;
      }
      case 0xe4: {
        const u8 index = reader.u8At(operands);
        const u8 raw = reader.has(driver.presetTable + index, 1) ? reader.u8At(driver.presetTable + index)
                                                                 : driver.traits.initialPan;
        state.pan = (raw & 0x80) != 0 ? driver.traits.initialPan
                                      : std::min(raw, driver.traits.maximumPan);
        state.panExplicit = true;
        break;
      }
      case 0xe5: {
        u8 wait = reader.has(driver.presetTable + reader.u8At(operands), 1)
                      ? reader.u8At(driver.presetTable + reader.u8At(operands))
                      : 1;
        tick += std::max<u8>(wait, 1);
        break;
      }
      default:
        break;
    }
    state.address = continuation;
  }
  return finish();
}

}  // namespace vgmtrans::formats::mori_snes
