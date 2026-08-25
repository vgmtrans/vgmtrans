/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ChunSnes/ChunSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceMotion.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

namespace vgmtrans::formats::chun_snes {

using namespace core;

namespace {

constexpr std::array<u8, 22> kDurationRates{
    0x0d, 0x1a, 0x26, 0x33, 0x40, 0x4d, 0x5a, 0x66, 0x73, 0x80, 0x8c,
    0x99, 0xa6, 0xb3, 0xbf, 0xcc, 0xd9, 0xe6, 0xf2, 0xfe, 0xff, 0x00,
};

namespace math {

[[nodiscard]] u32 tempoMicroseconds(u32 bpm) {
  return 60'000'000 / std::max(1u, bpm);
}

[[nodiscard]] u8 scale(u8 value, u8 multiplier) {
  if (multiplier == 0xff) {
    return value;
  }
  const u16 factor = multiplier < 0x80 ? multiplier : multiplier + 1u;
  return static_cast<u8>((value * factor + 0x80) >> 8);
}

[[nodiscard]] double channelGain(u8 master, u8 volume, u8 alternate) {
  return scale(scale(master, volume), alternate) / 256.0;
}

[[nodiscard]] double masterGain(u8 value) {
  return std::min(value / 128.0, 1.0);
}

[[nodiscard]] double pitchCents(s8 raw) {
  if (raw == 0) {
    return 0.0;
  }
  const int sign = raw < 0 ? -1 : 1;
  const int magnitude = std::min(std::abs(static_cast<int>(raw)), 127);
  if (magnitude == 127) {
    return sign * 100.0;
  }
  int scaled = magnitude * 2;
  if (scaled >= 0x80) {
    ++scaled;
  }
  return sign * scaled * (100.0 / 256.0);
}

[[nodiscard]] u32 duration(u32 length, u8 rate) {
  if (rate == 0 || rate == 0xff) {
    return length;
  }
  if (rate == 0xfe) {
    return length == 0 ? 0 : length - 1;
  }
  const u32 multiplier = rate < 0x80 ? rate : rate + 1u;
  return length * multiplier >> 8;
}

[[nodiscard]] StereoBalance panGains(s8 pan, u8 surround) {
  const double quiet = (255.0 - 2.0 * std::abs(static_cast<int>(pan))) / 256.0;
  double left = pan < 0 ? quiet : 1.0;
  double right = pan > 0 ? quiet : 1.0;
  if ((surround & 1) != 0) {
    left = -left;
  }
  if ((surround & 2) != 0) {
    right = -right;
  }
  return {.leftGain = left, .rightGain = right};
}

[[nodiscard]] double panPosition(s8 pan) {
  return std::clamp(-pan / 127.0, -1.0, 1.0);
}

}  // namespace math

[[nodiscard]] u32 pointerCount(ByteReader reader, u16 table) {
  if (table == 0 || !reader.has(table, 2)) {
    return 0;
  }
  const u16 first = reader.le16(table);
  if (first <= table || ((first - table) & 1) != 0) {
    return 0;
  }
  const u32 count = (first - table) / 2;
  return count <= 128 && reader.has(table, count * 2) ? count : 0;
}

[[nodiscard]] u32 pitchScriptLength(ByteReader reader, u16 address) {
  if (!reader.has(address, 2)) {
    return 0;
  }
  const u32 declared = reader.u8At(address);
  return declared >= 2 && reader.has(address, declared) ? declared : 0;
}

[[nodiscard]] u32 miniCommandSize(u8 opcode) {
  if (opcode >= 0xc0) {
    return opcode == 0xfe ? 3 : 1;
  }
  if (opcode >= 0x20) {
    const u8 sub = opcode & 0x0f;
    return sub == 0 || sub == 1 ? 2 : 1;
  }
  switch (opcode) {
    case 0x00:
    case 0x02:
    case 0x04:
    case 0x05:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
      return 2;
    case 0x01:
    case 0x13:
    case 0x14:
      return 3;
    case 0x10:
      return 9;
    default:
      return 1;
  }
}

[[nodiscard]] u32 miniScriptLength(ByteReader reader, u16 address) {
  u32 offset = address;
  for (u32 commands = 0; commands < 256 && reader.has(offset, 1); ++commands) {
    const u8 opcode = reader.u8At(offset);
    const u32 size = miniCommandSize(opcode);
    if (!reader.has(offset, size)) {
      return 0;
    }
    offset += size;
    if (opcode == 0xff || (opcode >= 0xc0 && opcode != 0xfe)) {
      return offset - address;
    }
  }
  return 0;
}

[[nodiscard]] u32 durationScriptLength(ByteReader reader, u16 address) {
  u32 offset = address;
  for (u32 commands = 0; commands < 256 && reader.has(offset, 1); ++commands) {
    const u8 opcode = reader.u8At(offset);
    const u32 size = opcode == 0 || opcode == 0xfe ? (opcode == 0 ? 2 : 3) : 1;
    if (!reader.has(offset, size)) {
      return 0;
    }
    offset += size;
    if (opcode == 0xff) {
      return offset - address;
    }
  }
  return 0;
}

using ScriptLength = u32 (*)(ByteReader, u16);

[[nodiscard]] std::vector<SourceRange> scriptTable(ByteReader reader, u16 table, ScriptLength scriptLength) {
  std::vector<SourceRange> scripts(pointerCount(reader, table));
  for (u32 index = 0; index < scripts.size(); ++index) {
    const u16 address = reader.le16(table + index * 2);
    scripts[index] = reader.range(address, scriptLength(reader, address));
  }
  return scripts;
}

// Immutable driver data needed after scanning. Scripts remain source bytes;
// only their ranges are indexed for cheap playback lookup.
class DriverData {
public:
  DriverData(RetainedSource source, const Layout& layout, u8 baseTempo)
      : source_(std::move(source)), version_(layout.version), baseTempo_(baseTempo), echo_(layout.echo) {
    const ByteReader reader = source_.reader();
    pitch_ = scriptTable(reader, layout.pitchEnvelopeTableAddress, pitchScriptLength);
    mini_ = scriptTable(reader, layout.miniSequenceTableAddress, miniScriptLength);
    duration_ = scriptTable(reader, layout.durationScriptTableAddress, durationScriptLength);
    gain_ = scriptTable(reader, layout.gainEnvelopeTableAddress, pitchScriptLength);
  }

  [[nodiscard]] Version version() const noexcept { return version_; }
  [[nodiscard]] u8 baseTempo() const noexcept { return baseTempo_; }
  [[nodiscard]] EchoState echo() const noexcept { return echo_; }
  [[nodiscard]] std::span<const u8> pitch(u8 index) const { return script(pitch_, index); }
  [[nodiscard]] std::span<const u8> mini(u8 index) const { return script(mini_, index); }
  [[nodiscard]] std::span<const u8> duration(u8 index) const { return script(duration_, index); }
  [[nodiscard]] std::span<const u8> gain(u8 index) const { return script(gain_, index); }

private:
  [[nodiscard]] std::span<const u8> script(const std::vector<SourceRange>& scripts, u8 index) const {
    if (index >= scripts.size() || scripts[index].size == 0) {
      return {};
    }
    return source_.reader().slice(scripts[index]);
  }

  RetainedSource source_;
  Version version_ = Version::Summer;
  u8 baseTempo_ = 1;
  EchoState echo_;
  std::vector<SourceRange> pitch_;
  std::vector<SourceRange> mini_;
  std::vector<SourceRange> duration_;
  std::vector<SourceRange> gain_;
};

struct TimedScriptCursor {
  u8 index = 0xff;
  u16 offset = 2;
  u16 delay = 0;

  void start(u8 script) {
    index = script;
    offset = 2;
    delay = script == 0xff ? 0 : 1;
  }

  [[nodiscard]] std::optional<u8> advance(std::span<const u8> script, u8 terminator) {
    if (index == 0xff || delay == 0 || --delay != 0) {
      return std::nullopt;
    }
    if (script.size() < 2 || static_cast<size_t>(offset) + 1 >= script.size() ||
        static_cast<u8>(script[offset]) == terminator) {
      index = 0xff;
      return std::nullopt;
    }

    const u8 value = static_cast<u8>(script[offset]);
    delay = static_cast<u8>(script[offset + 1]);
    delay = delay == 0 ? 256 : delay;
    offset += 2;
    if (offset == static_cast<u8>(script[0])) {
      offset = static_cast<u8>(script[1]);
    }
    return value;
  }
};

struct ProgramState {
  struct DurationState {
    u64 tick = 0;
    u16 length = 1;
    u8 rate = 0xcc;
  };

  explicit ProgramState(const DriverData& driver)
      : driver(&driver), version(driver.version()), baseTempo(driver.baseTempo()), echo(driver.echo()) {
    for (auto& timeline : durationTimeline) {
      timeline.push_back(DurationState{});
    }
  }

  const DriverData* driver;
  Version version;
  u8 baseTempo = 1;
  u8 condition = 0;
  u8 masterVolume = 0x80;
  EchoState echo;
  u8 echoFilter = 0;
  std::array<std::vector<DurationState>, kTrackCount> durationTimeline;
};

struct TrackState {
  struct PendingPitchSlide {
    s8 semitones = 0;
    u8 duration = 0;
  };

  explicit TrackState(const TrackProgram& trackProgram) : trackNumber(trackProgram.sourceTrackNumber) {
    volume.reset(0x80);
    volume.setRounding(SequenceFixedPointRounding::Nearest);
  }

  u32 trackNumber = 0;
  u16 noteLength = 1;
  u8 durationRate = 0xcc;
  u8 channelMaster = 0x60;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> volume;
  u8 alternateVolume = 0xff;
  s8 alternateRate = 0;
  s8 pan = 0;
  u8 surround = 0;
  s8 tuning = 0;
  bool syncLength = false;
  bool previousSlur = false;
  bool previousWasRest = true;
  PerformanceNoteId lastNote;
  std::optional<double> lastKey;
  std::optional<PendingPitchSlide> pendingPitchSlide;
  PitchSlideBinding activePitchSlide;
  TimedScriptCursor pitchEnvelope;
  u8 durationScript = 0xff;
  u16 durationScriptOffset = 0;
  TimedScriptCursor gainEnvelope;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void beforeCommand() {
    if (track.durationScript != 0xff) {
      advanceDurationScript();
    } else if (track.syncLength && track.trackNumber != 0 && track.trackNumber < kTrackCount) {
      const auto& timeline = program.durationTimeline[track.trackNumber - 1];
      auto found = std::ranges::upper_bound(timeline, vm.tick(), {}, &ProgramState::DurationState::tick);
      if (found != timeline.begin()) {
        --found;
        track.noteLength = found->length;
        track.durationRate = found->rate;
        rememberLength();
      }
    }
  }

  void rememberLength() {
    if (track.trackNumber < kTrackCount) {
      auto& timeline = program.durationTimeline[track.trackNumber];
      const ProgramState::DurationState state{
          .tick = vm.tick(),
          .length = track.noteLength,
          .rate = track.durationRate,
      };
      if (!timeline.empty() && timeline.back().tick == state.tick) {
        timeline.back() = state;
      } else {
        timeline.push_back(state);
      }
    }
  }

  void setDurationRate(u8 value) {
    track.durationRate = value;
    rememberLength();
  }

  void setLength(u8 value) {
    track.noteLength = value == 0 ? 256 : value;
    rememberLength();
  }

  [[nodiscard]] Effects note(u8 encodedNote, u16 explicitLength) {
    if (explicitLength != 0x100) {
      setLength(static_cast<u8>(explicitLength));
    }
    const u32 length = track.noteLength;
    const u32 sounding = math::duration(length, track.durationRate);
    if (encodedNote == 0) {
      track.pendingPitchSlide.reset();
      if (track.previousSlur && track.lastNote.valid() && track.lastKey) {
        track.lastNote = out.note(NotePerformanceEvent{.key = *track.lastKey,
                                                       .linearVelocity = 1.0,
                                                       .durationTicks = length,
                                                       .extendsPrevious = true,
                                                       .restartsLfoPhase = false});
      }
      track.previousWasRest = true;
      return Effects::wait(length);
    }
    if (encodedNote == 0x4f) {
      if (track.lastNote.valid() && track.lastKey && !track.previousWasRest) {
        track.lastNote = out.note(NotePerformanceEvent{.key = *track.lastKey,
                                                       .linearVelocity = 1.0,
                                                       .durationTicks = sounding,
                                                       .extendsPrevious = true,
                                                       .restartsLfoPhase = false});
        startPitchSlide(*track.lastKey);
      }
      track.previousSlur = track.durationRate == 0;
      track.previousWasRest = false;
      return Effects::wait(length);
    }

    const double key = encodedNote + 23.0;
    if (track.previousSlur && track.lastNote.valid() && track.lastKey == key && !track.previousWasRest) {
      track.lastNote = out.note(NotePerformanceEvent{.key = key,
                                                     .linearVelocity = 1.0,
                                                     .durationTicks = sounding,
                                                     .extendsPrevious = true,
                                                     .restartsLfoPhase = false});
    } else {
      track.lastNote = out.note(NotePerformanceEvent{.key = key, .linearVelocity = 1.0, .durationTicks = sounding});
    }
    track.lastKey = key;
    startPitchSlide(key);
    track.previousWasRest = false;
    track.previousSlur = track.durationRate == 0;
    return Effects::wait(length);
  }

  [[nodiscard]] u8 channelVolume() const {
    return static_cast<u8>(std::clamp<s32>(track.volume.currentRaw(), 0, 0xff));
  }

  void emitLevel(PerformanceEmitter output) {
    output.level(math::channelGain(track.channelMaster, channelVolume(), track.alternateVolume),
                 LevelPrecisionHint::FourteenBit);
  }

  void emitLevel() { emitLevel(out); }

  void channelMaster(u8 value) {
    track.channelMaster = value;
    emitLevel();
  }

  void volume(u8 value) {
    track.volume.setCurrentAt(vm.tick(), value);
    emitLevel();
  }

  void volumeFade(u8 target, u8 duration) {
    if (duration != 0) {
      static_cast<void>(
          track.volume.begin(out.fade(PerformanceAutomationTarget::Level,
                                      math::channelGain(track.channelMaster, target, track.alternateVolume), duration),
                             SequenceFixedPointMotion<s32>::toRawTarget(target, duration)));
    }
  }

  void alternateFade(s8 rate) {
    track.alternateRate = rate;
    if (rate != 0) {
      track.alternateVolume = rate < 0 ? 0xff : 0;
      emitLevel();
    }
  }

  void emitPan() {
    const StereoBalance gains = math::panGains(track.pan, track.surround);
    out.stereoBalance(gains.leftGain, gains.rightGain);
  }

  void pan(s8 value) {
    track.pan = value;
    emitPan();
  }

  void panFade(s8 target, u8 duration) {
    if (duration != 0) {
      static_cast<void>(out.fade(PerformanceAutomationTarget::Pan, math::panPosition(target), duration));
      track.pan = target;
    }
  }

  void surround(u8 value) {
    track.surround = value & 3;
    emitPan();
  }

  void emitPitchOffset() { out.pitchBend(math::pitchCents(track.tuning) / 100.0); }

  void tuning(s8 value) {
    track.tuning = value;
    emitPitchOffset();
  }

  void pitchEnvelope(u8 index) {
    track.pitchEnvelope.start(index);
    tuning(0);
  }

  void durationScript(u8 index) {
    track.durationScript = index;
    track.durationScriptOffset = 0;
  }

  void advanceDurationScript() {
    const auto script = program.driver->duration(track.durationScript);
    for (u32 controls = 0; controls < 32 && track.durationScriptOffset < script.size(); ++controls) {
      const u8 value = static_cast<u8>(script[track.durationScriptOffset++]);
      if (value == 0xff) {
        track.durationScript = 0xff;
        return;
      }
      if (value == 0 && track.durationScriptOffset < script.size()) {
        setDurationRate(static_cast<u8>(script[track.durationScriptOffset++]));
        continue;
      }
      if (value == 0xfe && static_cast<size_t>(track.durationScriptOffset) + 1 < script.size()) {
        const u16 raw =
            static_cast<u16>(script[track.durationScriptOffset] | (script[track.durationScriptOffset + 1] << 8));
        const s16 relative = static_cast<s16>(raw);
        const s32 target = static_cast<s32>(track.durationScriptOffset + 2) + relative;
        if (target < 0 || static_cast<u32>(target) >= script.size()) {
          track.durationScript = 0xff;
          return;
        }
        track.durationScriptOffset = static_cast<u16>(target);
        continue;
      }
      setLength(value);
      return;
    }
    track.durationScript = 0xff;
  }

  void gainEnvelope(u8 index) {
    track.gainEnvelope.start(index);
    if (index == 0xff) {
      out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::FutureAttacks);
    } else {
      advanceGainEnvelope();
    }
  }

  void advanceGainEnvelope() {
    const auto gain = track.gainEnvelope.advance(program.driver->gain(track.gainEnvelope.index), 0xff);
    if (!gain) {
      return;
    }
    out.replaceEnvelope(snesDspEnvelope(0, 0, *gain), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void adsr(u8 adsr1, u8 adsr2, u16 release) {
    Envelope envelope = snesDspEnvelope(adsr1, adsr2, 0);
    const u8 releaseRate = release == 0x100 ? defaultReleaseRate(program.version) : static_cast<u8>(release);
    envelope.releaseSeconds = snesDspAdsrSustainSeconds(releaseRate & 0x1f);
    out.replaceEnvelope(envelope, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void tempo(u8 value) {
    const u32 bpm = program.version == Version::WinterV3 ? program.baseTempo * value / 64u : value;
    out.tempo(math::tempoMicroseconds(bpm));
  }

  void fadeMaster(u8 target, u32 speed) {
    speed = std::max(1u, speed);
    const u32 distance = std::abs(static_cast<int>(target) - program.masterVolume);
    const u32 ticks = std::max(1u, (distance + speed - 1) / speed);
    static_cast<void>(out.fade(PerformanceAutomationTarget::MasterLevel, math::masterGain(target), ticks));
    program.masterVolume = target;
  }

  void masterFade(s8 rate) {
    if (rate == 0) {
      return;
    }
    fadeMaster(rate < 0 ? 0 : 0xff, std::abs(static_cast<int>(rate)) * 8u);
  }

  void pitchSlide(s8 semitones, u8 duration) {
    track.activePitchSlide.interrupt(out);
    track.pendingPitchSlide = TrackState::PendingPitchSlide{.semitones = semitones, .duration = duration};
  }

  void startPitchSlide(double startKey) {
    if (!track.pendingPitchSlide) {
      return;
    }
    const auto [semitones, duration] = *track.pendingPitchSlide;
    track.pendingPitchSlide.reset();
    if (duration == 0 || semitones == 0 || !track.lastNote.valid()) {
      return;
    }
    const double target = startKey + semitones;
    auto slide = out.pitchSlide(track.lastNote, startKey, target, duration);
    slide.preferPitchBend();
    track.activePitchSlide = std::move(slide);
    track.lastKey = target;
  }

  void echo(bool enabled) { emitEcho(static_cast<u8>(1u << std::min(track.trackNumber, 7u)), enabled ? 1.0 : 0.0); }

  void emitEcho(u8 mask, double send) {
    out.reverb(ReverbPerformanceEvent{
        .voiceMask = mask,
        .send = send,
        .leftGain = program.echo.left / 128.0,
        .rightGain = program.echo.right / 128.0,
        .delayMilliseconds = program.echo.delay * 16.0,
        .feedback = program.echo.feedback / 128.0,
        .filterIndex = program.echoFilter,
    });
  }

  void writeDspRegister(u8 reg, u8 value) {
    switch (reg) {
      case 0x0d:
        program.echo.feedback = static_cast<s8>(value);
        break;
      case 0x2c:
        program.echo.left = static_cast<s8>(value);
        break;
      case 0x3c:
        program.echo.right = static_cast<s8>(value);
        break;
      case 0x4d:
        emitEcho(value, value == 0 ? 0.0 : 1.0);
        return;
      case 0x7d:
        program.echo.delay = value & 0x0f;
        break;
      default:
        return;
    }
    emitEcho(0xff, 1.0);
  }

  void preset(u8 index) {
    const auto script = program.driver->mini(index);
    for (u32 offset = 0; offset < script.size();) {
      const u8 opcode = static_cast<u8>(script[offset]);
      const u32 size = miniCommandSize(opcode);
      if (size == 0 || size > script.size() - offset || opcode == 0xff || (opcode >= 0xc0 && opcode != 0xfe)) {
        break;
      }
      const auto arg = [&](u32 n) { return static_cast<u8>(script[offset + n]); };
      switch (opcode) {
        case 0x00:
          program.masterVolume = arg(1);
          out.masterLevel(math::masterGain(program.masterVolume));
          break;
        case 0x01:
          fadeMaster(arg(2), std::abs(static_cast<int>(static_cast<s8>(arg(1)))) * 8u);
          break;
        case 0x02:
          program.baseTempo = arg(1);
          out.tempo(math::tempoMicroseconds(program.baseTempo));
          break;
        case 0x04:
        case 0x18:
          masterFade(static_cast<s8>(arg(1)));
          break;
        case 0x05:
          program.condition = arg(1);
          break;
        case 0x10:
          program.echoFilter = index;
          emitEcho(0xff, 1.0);
          break;
        case 0x13:
          out.masterLevel(std::max(std::abs(static_cast<s8>(arg(1))), std::abs(static_cast<s8>(arg(2)))) / 127.0);
          break;
        case 0x14:
          program.echo.left = static_cast<s8>(arg(1));
          program.echo.right = static_cast<s8>(arg(2));
          emitEcho(0xff, 1.0);
          break;
        case 0x16:
          program.echo.feedback = static_cast<s8>(arg(1));
          emitEcho(0xff, 1.0);
          break;
        case 0x17:
          program.echo.delay = std::min<u8>(arg(1), 5);
          emitEcho(0xff, 1.0);
          break;
        case 0xfe:
          writeDspRegister(arg(1), arg(2));
          break;
        default:
          if (opcode >= 0x20 && opcode < 0xc0) {
            const u8 voice = static_cast<u8>(std::min<int>((opcode >> 4) - 2, 7));
            const u8 sub = opcode & 0x0f;
            if (sub == 2 || sub == 3) {
              emitEcho(static_cast<u8>(1u << voice), sub == 2 ? 1.0 : 0.0);
            }
          }
          break;
      }
      offset += size;
    }
  }

  [[nodiscard]] Effects conditional(Address destination, u8 value) {
    const bool matches = (program.condition & 0x7f) == value;
    program.condition |= 0x80;
    if (matches) {
      return vm.finiteBranch(destination);
    }
    return {};
  }

  [[nodiscard]] Effects return_() { return vm.inSubroutine() ? vm.return_() : vm.fallthrough(); }

  [[nodiscard]] Effects returnOrEnd() { return vm.inSubroutine() ? vm.return_() : vm.end(); }

  void tick() {
    if (track.alternateRate != 0) {
      const int next = track.alternateVolume + static_cast<int>(track.alternateRate) * 4;
      if (next <= 0 || next >= 255) {
        track.alternateVolume = static_cast<u8>(std::clamp(next, 0, 255));
        track.alternateRate = 0;
      } else {
        track.alternateVolume = static_cast<u8>(next);
      }
      emitLevel();
    }

    static_cast<void>(track.volume.tickRaw([&](s32) { emitLevel(track.volume.output(out)); }));

    advanceGainEnvelope();

    const auto pitch = track.pitchEnvelope.advance(program.driver->pitch(track.pitchEnvelope.index), 0x80);
    if (pitch) {
      track.tuning = static_cast<s8>(*pitch);
      emitPitchOffset();
    }
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] Address relativeTarget(s16 relative, u32 continuation) {
  return Address{static_cast<u32>(continuation + relative) & 0xffff};
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, Version version,
                                                   std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, "chun-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode <= 0x9f) {
    auto event = cursor.command(opcode == 0 || opcode == 0x50 ? "Rest" : "Note", SequenceSemantic::Note);
    const u8 note = event.opcodeValue("note", opcode >= 0x50 ? opcode - 0x50 : opcode, SourceValueDisplay::Default,
                                      SemanticOperandRole::NoteKey);
    u16 length = 0x100;
    if (opcode >= 0x50) {
      length = event.u8("length", SemanticOperandRole::Duration);
    }
    return event.invoke<&Playback::note>(note, length);
  }
  if (version != Version::Summer && opcode >= 0xa0 && opcode <= 0xb5) {
    auto event = cursor.command("Duration Rate", SequenceSemantic::State);
    const u8 rate = kDurationRates[opcode - 0xa0];
    event.derived("rate", rate);
    return event.invoke<&Playback::setDurationRate>(rate);
  }
  if ((version == Version::Summer && opcode >= 0xa0 && opcode <= 0xdc) ||
      (version != Version::Summer && opcode >= 0xb6 && opcode <= 0xda)) {
    return cursor.noOp("No Operation", "nop");
  }

  switch (opcode) {
    case 0xdb: {
      if (version == Version::Summer) {
        return cursor.noOp("No Operation", "nop");
      }
      auto event = cursor.command("Alternate Repeat Break", SequenceSemantic::RepeatBreak);
      const s16 relative = event.s16le("relative");
      const Address destination = relativeTarget(relative, begin + 3);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      return event.repeatBreak(1, destination);
    }
    case 0xdc: {
      if (version == Version::Summer) {
        return cursor.noOp("No Operation", "nop");
      }
      auto event = cursor.command("Alternate Repeat Twice", SequenceSemantic::Repeat);
      const s16 relative = event.s16le("relative");
      const Address destination = relativeTarget(relative, begin + 3);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      return event.repeatUntil(1, 2, destination);
    }
    case 0xdd: {
      auto event = cursor.command("Release Rate", SequenceSemantic::Envelope);
      const u8 rate = event.u8("rate") & 0x1f;
      return event.emitEnvelopeField<EnvelopeFields::Release>(snesDspAdsrSustainSeconds(rate),
                                                              VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
    case 0xde: {
      auto event = cursor.command("ADSR and Release Rate", SequenceSemantic::Envelope);
      const u8 adsr1 = event.u8("adsr1");
      const u8 adsr2 = event.u8("adsr2");
      return event.invoke<&Playback::adsr>(adsr1, adsr2, static_cast<u16>(event.u8("release_rate") & 0x1f));
    }
    case 0xdf: {
      auto event = cursor.command("Surround Phase", SequenceSemantic::Pan);
      return event.invoke<&Playback::surround>(event.u8("phase_mask"));
    }
    case 0xe0: {
      auto event = cursor.command("Conditional Jump", SequenceSemantic::State);
      const s16 relative = event.s16le("relative");
      const u8 value = event.u8("value");
      const Address destination = relativeTarget(relative, begin + 4);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      event.mayBranchTo(destination);
      return event.invoke<&Playback::conditional>(destination, value);
    }
    case 0xe1:
      return cursor.sourceOnly("Increment CPU Counter", "cpu-counter");
    case 0xe2: {
      auto event = cursor.command("Pitch Envelope / Vibrato", SequenceSemantic::Modulation);
      return event.invoke<&Playback::pitchEnvelope>(event.u8("script"));
    }
    case 0xe3:
    case 0xe4:
      return cursor.sourceOnly(opcode == 0xe3 ? "Noise On" : "Noise Off", "noise");
    case 0xe5: {
      auto event = cursor.command("Master Volume Rate", SequenceSemantic::Level);
      return event.invoke<&Playback::masterFade>(event.s8("rate"));
    }
    case 0xe6: {
      auto event = cursor.command("Channel Volume Fade", SequenceSemantic::Level);
      const u8 target = event.u8("target", SemanticOperandRole::Level);
      return event.invoke<&Playback::volumeFade>(target, event.u8("duration", SemanticOperandRole::Duration));
    }
    case 0xe7: {
      auto event = cursor.command("Full-Range Volume Rate", SequenceSemantic::Level);
      return event.invoke<&Playback::alternateFade>(event.s8("rate"));
    }
    case 0xe8: {
      auto event = cursor.command("Pan Fade", SequenceSemantic::Pan);
      const s8 target = event.s8("target", SemanticOperandRole::Pan);
      return event.invoke<&Playback::panFade>(target, event.u8("duration", SemanticOperandRole::Duration));
    }
    case 0xe9: {
      auto event = cursor.command("Fine Tuning", SequenceSemantic::Pitch);
      const s8 raw = event.s8("raw");
      event.derived("cents", math::pitchCents(raw), SourceValueDisplay::Cents);
      return event.invoke<&Playback::tuning>(raw);
    }
    case 0xea: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      const s16 relative = event.s16le("relative");
      const Address destination = relativeTarget(relative, begin + 3);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      return destination.value < begin ? event.loopCandidate(destination) : event.jump(destination);
    }
    case 0xeb: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      return event.invoke<&Playback::tempo>(event.u8("value"));
    }
    case 0xec: {
      auto event = cursor.command("Duration Rate", SequenceSemantic::State);
      return event.invoke<&Playback::setDurationRate>(event.u8("rate"));
    }
    case 0xed: {
      auto event = cursor.command("Channel Master Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::channelMaster>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xee: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.s8("pan", SemanticOperandRole::Pan));
    }
    case 0xef: {
      auto event = cursor.command("ADSR", SequenceSemantic::Envelope);
      const u8 adsr1 = event.u8("adsr1");
      return event.invoke<&Playback::adsr>(adsr1, event.u8("adsr2"), static_cast<u16>(0x100));
    }
    case 0xf0: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      return event.emitInstrument(kInstrumentDomain, event.u8("program", SemanticOperandRole::InstrumentProgram));
    }
    case 0xf1:
      if (version == Version::Summer) {
        auto event = cursor.command("Duration Script", SequenceSemantic::State);
        return event.invoke<&Playback::durationScript>(event.u8("script"));
      }
      return cursor.command("Duration Copy On", SequenceSemantic::State).set<&TrackState::syncLength>(true);
    case 0xf2:
      return cursor.command("Duration Copy On", SequenceSemantic::State).set<&TrackState::syncLength>(true);
    case 0xf3:
      return cursor.command("Duration Copy Off", SequenceSemantic::State).set<&TrackState::syncLength>(false);
    case 0xf4: {
      auto event = cursor.command("Repeat Twice", SequenceSemantic::Repeat);
      const s16 relative = event.s16le("relative");
      const Address destination = relativeTarget(relative, begin + 3);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      return event.repeatUntil(0, 2, destination);
    }
    case 0xf5: {
      auto event = cursor.command("Repeat", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const s16 relative = event.s16le("relative");
      const Address destination = relativeTarget(relative, begin + 4);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      return event.repeatUntil(0, count == 0 ? 256u : count, destination);
    }
    case 0xf6: {
      auto event = cursor.command("Channel Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xf7:
      if (version == Version::Summer) {
        auto event = cursor.command("Gain Envelope", SequenceSemantic::Envelope);
        return event.invoke<&Playback::gainEnvelope>(event.u8("script"));
      }
      return cursor.noOp("No Operation", "nop");
    case 0xf8: {
      auto event = cursor.command("Call", SequenceSemantic::Call);
      const s16 relative = event.s16le("relative");
      const Address destination = relativeTarget(relative, begin + 3);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::CallTarget);
      return event.call(destination);
    }
    case 0xf9: {
      auto event = cursor.command("Pattern End / Return", SequenceSemantic::Return);
      event.invoke<&Playback::return_>();
      // The driver treats F9 as a no-op at top level, so both the physical
      // continuation and a caller's return address are reachable.
      event.discoverTarget(event.nextAddress());
      return event.discoverReturn();
    }
    case 0xfa: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      const s8 semitones = event.s8("semitones", SemanticOperandRole::Pitch);
      return event.emitTuning(semitones * 100.0);
    }
    case 0xfb: {
      auto event = cursor.command("Pitch Slide", SequenceSemantic::Portamento);
      const s8 semitones = event.s8("semitones", SemanticOperandRole::Pitch);
      return event.invoke<&Playback::pitchSlide>(semitones, event.u8("duration", SemanticOperandRole::Duration));
    }
    case 0xfc:
    case 0xfd:
      return cursor.command(opcode == 0xfc ? "Echo On" : "Echo Off", SequenceSemantic::State)
          .invoke<&Playback::echo>(opcode == 0xfc);
    case 0xfe: {
      auto event = cursor.command("Run Driver Preset", SequenceSemantic::State);
      return event.invoke<&Playback::preset>(event.u8("preset"));
    }
    case 0xff: {
      auto event = cursor.command("Return / End", SequenceSemantic::End);
      event.invoke<&Playback::returnOrEnd>();
      return event.discoverReturn();
    }
    default:
      return cursor.unsupported("Unknown Opcode").stop();
  }
}

}  // namespace

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config = SequenceProgramConfig{
      .commandKindPrefix = "chun-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = 8192,
              .initialSourceInstrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 0},
              .initialLevel = math::channelGain(0x60, 0x80, 0xff),
              .initialReverbSend = 0.0,
              .initialStereoBalance = StereoBalance{},
              .initialMonoModeChannels = 0,
          },
  };
  return config;
}

SequenceParse decodeSequence(RetainedSource source, const Layout& layout, AssetId sequenceId,
                             SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const ByteReader reader = source.reader();
  const u32 headerSize = 2 + reader.u8At(layout.sequenceHeaderAddress + 1) * 2;
  const SourceRange headerRange = reader.range(layout.sequenceHeaderAddress, headerSize);
  SequenceDecodeSession sequence{reader, sequenceConfig(), sequenceId, headerRange, sourceMap, 32768};
  const u8 tracks = reader.u8At(layout.sequenceHeaderAddress + 1);
  for (u32 track = 0; track < tracks; ++track) {
    const u32 pointer = layout.sequenceHeaderAddress + 2 + track * 2;
    const u16 raw = reader.le16(pointer);
    const u32 start = layout.version == Version::Summer ? raw : layout.sequenceHeaderAddress + raw;
    sequence.addTrack(
        track, reader.range(pointer, 2), start,
        [&](u32 offset) { return decodeCommand(reader, offset, layout.version, diagnostics); }, raw);
  }
  const u8 initialTempo = reader.u8At(layout.sequenceHeaderAddress);
  SequenceProgram program =
      sequence.finish(makeCompiledRuntime<Cursor, ProgramState>(DriverData{std::move(source), layout, initialTempo}));
  program.behavior.initialTempoMicrosecondsPerQuarter = math::tempoMicroseconds(initialTempo);
  return {.program = std::move(program), .headerRange = headerRange};
}

}  // namespace vgmtrans::formats::chun_snes
