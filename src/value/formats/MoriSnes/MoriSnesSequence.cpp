/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/MoriSnes/MoriSnes.h"
#include "value/formats/MoriSnes/MoriSnesVoiceScript.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace vgmtrans::formats::mori_snes {

using namespace core;

namespace {

constexpr u8 kInitialMasterVolume = 0xf0;
constexpr u8 kInitialTrackVolume = 0xc8;
constexpr u8 kInitialTempo = 0x20;
constexpr s8 kInitialEchoVolume = 0x12;
constexpr s8 kInitialEchoFeedback = 0x50;
constexpr u8 kInitialEchoDelay = 4;
constexpr u8 kInitialEchoFilter = 2;
constexpr u32 kSequenceTickScale = 256;
constexpr PitchBendLayerId kVoiceScriptPitchLayer{1};

struct EventTiming {
  std::optional<u8> delay;
  std::optional<u8> duration;
  std::optional<u8> velocity;
};

struct InspectedEvent {
  EventTiming timing;
  u8 status = 0;
  u32 operandAddress = 0;
  bool valid = false;
};

[[nodiscard]] InspectedEvent inspectEvent(ByteReader reader, u32 begin) {
  if (!reader.has(begin, 1)) {
    return {};
  }
  u32 cursor = begin;
  const u8 first = reader.u8At(cursor++);
  InspectedEvent result;
  if (first < 0x80) {
    result.timing.delay = first;
    if (!reader.has(cursor, 1)) {
      return {};
    }
    if (reader.u8At(cursor) < 0x80) {
      result.timing.duration = reader.u8At(cursor++);
      if (!reader.has(cursor, 1)) {
        return {};
      }
      if (reader.u8At(cursor) < 0x80) {
        result.timing.velocity = reader.u8At(cursor++);
        if (!reader.has(cursor, 1)) {
          return {};
        }
      }
    }
    result.status = reader.u8At(cursor++);
  } else {
    result.status = first;
  }
  result.operandAddress = cursor;
  result.valid = true;
  return result;
}

[[nodiscard]] u16 relativeTarget(u16 continuation, s16 relative) {
  return static_cast<u16>(continuation + relative);
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u8 tempo, u8 timerTarget) {
  // Timer 0 ticks every 125 us. An 8.8 accumulator overflows once per
  // sequence tick. Gokinjo's E6 is represented by halving decoded waits,
  // including the driver's odd-value truncation.
  if (tempo == 0) {
    return 60'000'000;
  }
  return kPpqn * timerTarget * 125u / tempo;
}

[[nodiscard]] double signedDspGain(s8 value) { return value / 128.0; }

[[nodiscard]] double byteGain(u8 value) { return value / 256.0; }

[[nodiscard]] double sourceVelocityGain(u8 value) { return static_cast<u8>(value << 1) / 256.0; }

[[nodiscard]] double masterGain(DriverTraits traits, u8 value) {
  if (traits.version == Version::Shien) {
    // Shien first doubles C5 with saturation, then halves the high byte of
    // C5*$80. The mixer finally applies 2*($1f+1), where $1f starts at $7f.
    const u8 doubled = value >= 0x80 ? 0xff : static_cast<u8>(value << 1);
    return (doubled / 4u + 1u) * 2.0 / 256.0;
  }
  // $21 is initialized to $7f; the mixer takes the high byte of C2*$21,
  // doubles it, and uses that byte for its final 8x8 multiplication.
  return (static_cast<u16>(value) * 0x7f >> 8) * 2.0 / 256.0;
}

[[nodiscard]] double echoSend(s8 value) {
  return std::clamp(std::abs(static_cast<int>(value)) / 128.0, 0.0, 1.0);
}

[[nodiscard]] double bendSemitones(s8 position, u8 rawRange) {
  return position * (rawRange / 1024.0);
}

[[nodiscard]] std::pair<s8, u8> addFinePitch(s8 high, u8 low, s8 delta, bool absolutePitch) {
  const u16 initial = static_cast<u16>((static_cast<u16>(static_cast<u8>(high)) << 8) | low);
  u16 result = static_cast<u16>(initial + delta);
  // The driver clamps against the complete voice pitch. Before D7/E2 replaces
  // the inherited note, a negative result here is only a relative offset.
  if (absolutePitch && delta < 0 && (result & 0x8000) != 0) {
    result = 0;
  }
  return {static_cast<s8>(result >> 8), static_cast<u8>(result)};
}

[[nodiscard]] StereoBalance driverPanGains(const DriverConfig& driver, u8 raw) {
  const u8 pan = (raw & 0x80) != 0 ? driver.traits.initialPan : std::min(raw, driver.traits.maximumPan);
  if (driver.data.has(driver.panTable + pan, 1) &&
      driver.data.has(driver.panTable + driver.traits.maximumPan - pan, 1)) {
    return StereoBalance{
        .leftGain = driver.data.u8At(driver.panTable + pan) / 128.0,
        .rightGain = driver.data.u8At(driver.panTable + driver.traits.maximumPan - pan) / 128.0,
    };
  }
  return StereoBalance{
      .leftGain = (driver.traits.maximumPan - pan) / double(driver.traits.maximumPan),
      .rightGain = pan / double(driver.traits.maximumPan),
  };
}

void emitVoicePitchChanges(const VoiceScriptAnalysis& script, PerformanceEmitter& out, double inheritedFine,
                           u64 attackTick, u8 tempo) {
  // The DSP voice offset is independent of the track's pitch wheel and remains
  // live through release. A new physical attack starts with a fresh offset.
  out.pitchBend(0.0, kVoiceScriptPitchLayer);
  if (script.attacks.size() != 1) {
    return;
  }

  const auto bend = [&](double pitch256, bool fineExplicit) {
    const double replacedFine = fineExplicit && !script.attackFineExplicit ? inheritedFine : 0.0;
    return (pitch256 - script.attackPitch256) / 256.0 - replacedFine;
  };
  const u32 scale = std::max<u8>(tempo, 1);
  double currentBend = 0.0;
  const auto emit = [&](u32 scriptTick, double value) {
    if (value != currentBend) {
      out.at(attackTick + scriptTick * scale).pitchBend(value, kVoiceScriptPitchLayer);
      currentBend = value;
    }
  };

  for (const VoiceScriptAnalysis::Point& point : script.points) {
    if (script.cycleStart && point.tick >= *script.cycleStart) {
      break;
    }
    emit(point.tick, bend(point.pitch256, point.fineExplicit));
  }
  if (script.cycleStart && script.cyclePitchCenter256) {
    emit(*script.cycleStart, bend(*script.cyclePitchCenter256, script.cycleFineExplicit));
  }
}

void emitVoiceVolumeChanges(const VoiceScriptAnalysis& script, PerformanceEmitter& out, u64 attackTick, u8 tempo,
                            std::optional<u64> endTick = std::nullopt) {
  if (script.attacks.size() != 1 || script.attackVolume == 0) {
    return;
  }

  u8 previous = script.attackVolume;
  bool emitted = false;
  const auto emit = [&](u32 scriptTick, u8 volume) {
    const u64 tick = attackTick + scriptTick * std::max<u8>(tempo, 1);
    if (endTick && tick >= *endTick) {
      return false;
    }
    if (volume != previous && !emitted) {
      out.expression(1.0);
      emitted = true;
    }
    if (volume != previous) {
      out.at(tick).expression(volume / static_cast<double>(script.attackVolume));
      previous = volume;
    }
    return true;
  };
  for (const VoiceScriptAnalysis::Point& point : script.points) {
    if (script.cycleStart && point.tick >= *script.cycleStart) {
      break;
    }
    if (!emit(point.tick, point.volume)) {
      return;
    }
  }
  if (script.cycleStart && script.cycleVolume) {
    static_cast<void>(emit(*script.cycleStart, *script.cycleVolume));
  }
}

struct ProgramState : DriverConfig {
  struct NoteLimit {
    std::optional<u32> ticks;
    std::optional<double> milliseconds;
  };

  struct HardwareVoice {
    bool ownerActive = false;
    bool tieEligible = false;
    bool fixedClock = false;
    u32 ownerTrack = 0;
    u8 key = 0;
    u8 priority = 0;
    u8 releaseDelay = 0;
    PerformanceNoteId note;
    u64 attackTick = 0;
    double attackMilliseconds = 0.0;
    std::optional<u64> ownerEndTick;
    std::optional<double> ownerEndMilliseconds;
    std::optional<u64> audioEndTick;
    std::optional<double> audioEndMilliseconds;
  };

  ProgramState(const SequenceProgram& sequence, const DriverConfig& config) : DriverConfig(config) {
    for (u32 index = 0; index < sequence.tracks.size(); ++index) {
      outputTracks.emplace(sequence.tracks[index].sourceTrackNumber, index);
    }
  }

  [[nodiscard]] double millisecondsAt(u64 tick) const {
    const double tickMilliseconds =
        tempoMicrosecondsPerQuarter(tempo, traits.timerTarget) / (static_cast<double>(kPpqn) * 1000.0);
    return clockMilliseconds + static_cast<double>(tick - clockTick) * tickMilliseconds;
  }

  void setTempo(u64 tick, u8 value) {
    clockMilliseconds = millisecondsAt(tick);
    clockTick = tick;
    tempo = value;
  }

  [[nodiscard]] const VoiceScriptAnalysis& voice(u16 script, bool percussion, u8 rawNote) {
    const auto key = std::tuple{script, percussion, static_cast<u8>(rawNote & 0x1f)};
    const auto found = voiceCache.find(key);
    if (found != voiceCache.end()) {
      return found->second;
    }
    const std::optional<u8> percussionNote = percussion ? std::optional{rawNote} : std::nullopt;
    return voiceCache.emplace(key, analyzeVoiceScript(*this, script, percussionNote)).first->second;
  }

  void limitNoteTicks(const HardwareVoice& voice, u64 endTick) {
    if (!voice.note.valid()) {
      return;
    }
    const auto key = std::pair{voice.ownerTrack, voice.note.value};
    const u32 duration = static_cast<u32>(std::min<u64>(endTick - std::min(endTick, voice.attackTick),
                                                        std::numeric_limits<u32>::max()));
    NoteLimit& limit = noteLimits[key];
    limit.ticks = limit.ticks ? std::min(*limit.ticks, duration) : std::optional{duration};
  }

  void limitNoteMilliseconds(const HardwareVoice& voice, double endMilliseconds) {
    if (!voice.note.valid()) {
      return;
    }
    const auto key = std::pair{voice.ownerTrack, voice.note.value};
    const double duration = std::max(0.0, endMilliseconds - voice.attackMilliseconds);
    NoteLimit& limit = noteLimits[key];
    limit.milliseconds = limit.milliseconds ? std::min(*limit.milliseconds, duration) : std::optional{duration};
  }

  void expireVoices(u64 tick, double now) {
    for (HardwareVoice& voice : voices) {
      // Source commands run before the hardware-voice pass in the same timer
      // interrupt, so a voice whose counter reaches zero exactly now can still
      // be found or stolen by this source tick.
      const bool timelineExpired = voice.ownerEndTick && *voice.ownerEndTick < tick;
      const bool clockExpired = voice.ownerEndMilliseconds && *voice.ownerEndMilliseconds < now;
      if (voice.ownerActive && (timelineExpired || clockExpired)) {
        voice.ownerActive = false;
        voice.tieEligible = false;
        voice.priority = 0;
      }
    }
  }

  [[nodiscard]] HardwareVoice* tiedVoice(u32 track, u8 key, u64 tick, double now) {
    expireVoices(tick, now);
    const auto found = std::ranges::find_if(voices, [&](const HardwareVoice& voice) {
      return voice.ownerActive && voice.tieEligible && voice.ownerTrack == track && voice.key == key;
    });
    return found == voices.end() ? nullptr : &*found;
  }

  [[nodiscard]] HardwareVoice* allocateVoice(u32 track, u8 priority, u64 tick, double now) {
    expireVoices(tick, now);
    size_t selected = voices.size() - 1;
    for (size_t index = voices.size(); index-- > 0;) {
      if (voices[index].priority <= voices[selected].priority) {
        selected = index;
      }
    }
    const bool sameOwner = std::ranges::any_of(voices, [&](const HardwareVoice& voice) {
      return voice.ownerActive && voice.ownerTrack == track;
    });
    if (voices[selected].priority != 0 && !sameOwner && voices[selected].priority >= priority) {
      return nullptr;
    }
    HardwareVoice& voice = voices[selected];
    const bool soundingByTimeline = !voice.audioEndTick || *voice.audioEndTick > tick;
    const bool soundingByClock = !voice.audioEndMilliseconds || *voice.audioEndMilliseconds > now;
    if (voice.note.valid() && soundingByTimeline && soundingByClock) {
      limitNoteTicks(voice, tick);
    }
    voice = HardwareVoice{};
    return &voice;
  }

  void finalizePerformance(PerformanceSequence& performance) {
    for (PerformanceTrack& track : performance.tracks) {
      const auto source = std::ranges::find_if(outputTracks, [&](const auto& entry) {
        return entry.second == track.id.value;
      });
      if (source == outputTracks.end()) {
        continue;
      }
      for (PerformanceEvent& event : track.events) {
        auto* note = std::get_if<NotePerformanceEvent>(&event);
        if (note == nullptr || !note->note.valid()) {
          continue;
        }
        const auto limit = noteLimits.find(std::pair{source->first, note->note.value});
        if (limit == noteLimits.end()) {
          continue;
        }
        if (limit->second.ticks) {
          note->durationTicks = std::min(note->durationTicks, *limit->second.ticks);
        }
        if (limit->second.milliseconds) {
          note->maximumDurationMilliseconds =
              note->maximumDurationMilliseconds
                  ? std::min(*note->maximumDurationMilliseconds, *limit->second.milliseconds)
                  : limit->second.milliseconds;
        }
      }
    }
  }

  u8 tempo = kInitialTempo;
  bool fast = false;
  s8 echoVolume = kInitialEchoVolume;
  s8 echoFeedback = kInitialEchoFeedback;
  u8 echoDelay = kInitialEchoDelay;
  u8 echoFilter = kInitialEchoFilter;
  u8 noiseClock = 0x20;
  u64 clockTick = 0;
  double clockMilliseconds = 0.0;
  std::array<HardwareVoice, 8> voices{};
  std::map<std::tuple<u16, bool, u8>, VoiceScriptAnalysis> voiceCache;
  std::map<u32, u32> outputTracks;
  std::map<std::pair<u32, u32>, NoteLimit> noteLimits;
  ReferencedInstruments references;
};

struct RepeatFrame {
  Address start;
  u16 remaining = 0;
};

struct TrackState : DriverConfig {
  TrackState(const SequenceProgram&, const TrackProgram& source, const DriverConfig& config)
      : DriverConfig(config), trackNumber(source.sourceTrackNumber) {}

  u32 trackNumber = 0;
  u8 delta = 0;
  u8 duration = 1;
  u8 velocity = 1;
  u8 noteBase = 0;
  s8 transpose = 0;
  u8 fineTuning = 0;
  u8 volume = kInitialTrackVolume;
  u8 pan = traits.initialPan;
  u8 bendRange = traits.initialBendRange;
  u8 priority = 0x40;
  u8 mode = 0x08;
  u8 group = 0;
  bool echoEnabled = true;
  u16 descriptor = 0;
  u16 scriptAddress = 0;
  bool percussion = false;
  std::array<RepeatFrame, 4> repeats{};
  u8 repeatDepth = 0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] u32 clockTicks(u8 raw, bool applyFastMode = true) const {
    const u8 physical = applyFastMode && program.fast ? raw >> 1 : raw;
    // In normal music mode the driver decrements countdowns only when the
    // 8.8 tempo accumulator carries. Mode bit 2 instead decrements them on
    // every 9.875 ms Timer 0 interrupt.
    const u32 scale = (track.mode & 0x04) != 0 ? program.tempo : kSequenceTickScale;
    return static_cast<u32>(physical) * scale;
  }

  [[nodiscard]] u32 beginEvent(const EventTiming& timing, bool noteDelay = false) {
    if (timing.duration) {
      track.duration = *timing.duration;
    }
    if (timing.velocity) {
      track.velocity = *timing.velocity;
    }
    if (timing.delay) {
      if (*timing.delay != 0) {
        track.delta = *timing.delay;
      }
    }
    if (!noteDelay) {
      // The dispatcher clears the source countdown before every C0-E6
      // handler. State and flow commands therefore consume no song time.
      return 0;
    }
    return clockTicks(timing.delay.value_or(track.delta));
  }

  [[nodiscard]] Effects wait(const EventTiming& timing) { return Effects::wait(beginEvent(timing)); }

  void emitEcho(PerformanceEmitter output) const {
    output.reverb(ReverbPerformanceEvent{
        .send = track.echoEnabled ? echoSend(program.echoVolume) : 0.0,
        .leftGain = signedDspGain(program.echoVolume),
        .rightGain = signedDspGain(program.echoVolume),
        .delayMilliseconds = program.echoDelay * 16.0,
        .feedback = signedDspGain(program.echoFeedback),
        .filterIndex = program.echoFilter,
    });
  }

  [[nodiscard]] Effects note(const EventTiming& timing, u8 rawNote, std::optional<u8> parameter) {
    const u32 delay = beginEvent(timing, true);
    if (parameter) {
      if ((*parameter & 0x80) != 0) {
        track.velocity = *parameter & 0x7f;
      } else {
        track.duration = *parameter;
      }
    }

    const u8 note = rawNote & 0x1f;
    const u8 sourceKey = static_cast<u8>(track.noteBase + note + track.transpose) & 0x7f;
    if (track.descriptor != 0 && track.data.has(track.descriptor, 1)) {
      if (!track.percussion && track.scriptAddress != static_cast<u16>(track.descriptor + 1)) {
        program.references.directScriptKeys[track.scriptAddress].insert(sourceKey);
      } else {
        program.references.noteKeys[track.descriptor].insert(sourceKey);
      }
      if (track.percussion) {
        program.references.percussionKeys[track.descriptor][note].insert(sourceKey);
      }
    }
    const u64 nowTick = vm.tick();
    const double now = program.millisecondsAt(nowTick);
    const VoiceScriptAnalysis& limits = program.voice(track.scriptAddress, track.percussion, note);
    const u8 physicalDuration = program.fast ? track.duration >> 1 : track.duration;
    const u32 gateClocks = physicalDuration + limits.releaseDelay.value_or(0);
    const bool fixedClock = (track.mode & 0x04) != 0;
    const std::optional<u64> autoEndTick = track.duration != 0 && !fixedClock
                                              ? std::optional{nowTick + gateClocks * kSequenceTickScale}
                                              : std::nullopt;
    const std::optional<double> autoEndMilliseconds = track.duration != 0 && fixedClock
                                                          ? std::optional{now + gateClocks * track.traits.timerMilliseconds()}
                                                          : std::nullopt;

    if (ProgramState::HardwareVoice* tied = program.tiedVoice(track.trackNumber, sourceKey, nowTick, now)) {
      if (track.duration != 0) {
        tied->tieEligible = false;
        const u32 tiedClocks = physicalDuration + tied->releaseDelay;
        if (tied->fixedClock) {
          const double tiedEnd = now + tiedClocks * program.traits.timerMilliseconds();
          tied->ownerEndMilliseconds = tied->ownerEndMilliseconds
                                            ? std::min(*tied->ownerEndMilliseconds, tiedEnd)
                                            : std::optional{tiedEnd};
          tied->audioEndMilliseconds = tied->audioEndMilliseconds
                                            ? std::min(*tied->audioEndMilliseconds, tiedEnd)
                                            : std::optional{tiedEnd};
          program.limitNoteMilliseconds(*tied, *tied->audioEndMilliseconds);
        } else {
          const u64 tiedEnd = nowTick + tiedClocks * kSequenceTickScale;
          tied->ownerEndTick = tied->ownerEndTick ? std::min(*tied->ownerEndTick, tiedEnd)
                                                  : std::optional{tiedEnd};
          tied->audioEndTick = tied->audioEndTick ? std::min(*tied->audioEndTick, tiedEnd)
                                                  : std::optional{tiedEnd};
          program.limitNoteTicks(*tied, *tied->audioEndTick);
        }
      }
      return Effects::wait(delay);
    }

    ProgramState::HardwareVoice* voice = program.allocateVoice(track.trackNumber, track.priority, nowTick, now);
    if (voice == nullptr) {
      return Effects::wait(delay);
    }

    const u8 effectivePan = limits.attackPanExplicit ? limits.attackPan : track.pan;
    const StereoBalance balance = driverPanGains(track, effectivePan);
    out.stereoBalance(balance.leftGain, balance.rightGain);
    emitEcho(out);

    const double key = sourceKey + (limits.attackFineExplicit ? 0.0 : track.fineTuning / 256.0);
    NotePerformanceEvent event{
        .key = key,
        .linearVelocity = sourceVelocityGain(track.velocity),
        .durationTicks = autoEndTick
                             ? static_cast<u32>(std::min<u64>(*autoEndTick - nowTick,
                                                               std::numeric_limits<u32>::max()))
                             : std::numeric_limits<u32>::max(),
    };
    std::optional<double> audioEndMilliseconds = autoEndMilliseconds;
    if (!limits.attacks.empty() && limits.attacks.front().keyOff) {
      const double scripted = now + *limits.attacks.front().keyOff * program.traits.timerMilliseconds();
      audioEndMilliseconds = audioEndMilliseconds ? std::min(*audioEndMilliseconds, scripted)
                                                  : std::optional{scripted};
    }
    if (audioEndMilliseconds) {
      event.maximumDurationMilliseconds = std::max(0.0, *audioEndMilliseconds - now);
    }
    emitVoicePitchChanges(limits, out, track.fineTuning / 256.0, nowTick, program.tempo);
    const PerformanceNoteId emitted = out.note(std::move(event));
    emitVoiceVolumeChanges(limits, out, nowTick, program.tempo, autoEndTick);

    if (limits.attacks.size() > 1) {
      const VoiceScriptAnalysis::Attack& first = limits.attacks.front();
      for (auto attack = std::next(limits.attacks.begin()); attack != limits.attacks.end(); ++attack) {
        const u64 attackTick = nowTick + attack->tick * std::max<u8>(program.tempo, 1);
        const double attackMilliseconds = now + attack->tick * program.traits.timerMilliseconds();
        if ((autoEndTick && attackTick >= *autoEndTick) ||
            (autoEndMilliseconds && attackMilliseconds >= *autoEndMilliseconds)) {
          break;
        }

        auto output = out.at(attackTick);
        const StereoBalance attackBalance = driverPanGains(track, attack->panExplicit ? attack->pan : track.pan);
        output.stereoBalance(attackBalance.leftGain, attackBalance.rightGain);
        emitEcho(output);

        NotePerformanceEvent retrigger{
            .key = sourceKey + (attack->fineExplicit ? 0.0 : track.fineTuning / 256.0) +
                   (attack->pitch256 - first.pitch256) / 256.0,
            .linearVelocity = first.volume == 0
                                  ? 0.0
                                  : sourceVelocityGain(track.velocity) * attack->volume / first.volume,
            .durationTicks = autoEndTick
                                 ? static_cast<u32>(std::min<u64>(*autoEndTick - attackTick,
                                                                   std::numeric_limits<u32>::max()))
                                 : std::numeric_limits<u32>::max(),
        };
        std::optional<double> retriggerEnd = autoEndMilliseconds;
        if (attack->keyOff) {
          const double scripted = attackMilliseconds + *attack->keyOff * program.traits.timerMilliseconds();
          retriggerEnd = retriggerEnd ? std::min(*retriggerEnd, scripted) : std::optional{scripted};
        }
        if (retriggerEnd) {
          retrigger.maximumDurationMilliseconds = std::max(0.0, *retriggerEnd - attackMilliseconds);
        }
        output.pitchBend(0.0, kVoiceScriptPitchLayer);
        output.note(std::move(retrigger));
      }
    }

    voice->ownerActive = true;
    voice->tieEligible = track.duration == 0;
    voice->fixedClock = fixedClock;
    voice->ownerTrack = track.trackNumber;
    voice->key = sourceKey;
    voice->priority = track.priority;
    voice->releaseDelay = limits.releaseDelay.value_or(0);
    voice->note = emitted;
    voice->attackTick = nowTick;
    voice->attackMilliseconds = now;
    voice->audioEndTick = autoEndTick;
    voice->audioEndMilliseconds = audioEndMilliseconds;
    voice->ownerEndTick = autoEndTick;
    voice->ownerEndMilliseconds = autoEndMilliseconds;
    if (limits.scriptEnd) {
      const double scriptedEnd = now + *limits.scriptEnd * program.traits.timerMilliseconds();
      voice->ownerEndMilliseconds = voice->ownerEndMilliseconds
                                        ? std::min(*voice->ownerEndMilliseconds, scriptedEnd)
                                        : std::optional{scriptedEnd};
    }
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects instrument(const EventTiming& timing, Address descriptor) {
    const u32 delay = beginEvent(timing);
    track.descriptor = static_cast<u16>(descriptor.value);
    program.references.descriptors.insert(track.descriptor);
    if (track.data.has(track.descriptor, 1)) {
      track.percussion = (track.data.u8At(track.descriptor) & 1) != 0;
      track.mode = static_cast<u8>((track.mode & ~u8{1}) | static_cast<u8>(track.percussion));
      track.scriptAddress = static_cast<u16>(track.descriptor + 1);
    }
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain),
                                      .key = static_cast<u32>(descriptor.value)},
                   InstrumentEnvelopeMode::UseInstrumentEnvelope);
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects pan(const EventTiming& timing, u8 value) {
    const u32 delay = beginEvent(timing);
    track.pan = value;
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects masterVolume(const EventTiming& timing, u8 value) {
    const u32 delay = beginEvent(timing);
    out.masterLevel(masterGain(program.traits, value));
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects tempo(const EventTiming& timing, u8 value) {
    const u32 delay = beginEvent(timing);
    program.setTempo(vm.tick(), value);
    out.tempo(tempoMicrosecondsPerQuarter(program.tempo, program.traits.timerTarget));
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects transpose(const EventTiming& timing, s8 value) {
    const u32 delay = beginEvent(timing);
    track.transpose = value;
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects volume(const EventTiming& timing, u8 value) {
    const u32 delay = beginEvent(timing);
    track.volume = value;
    out.level(byteGain(value), ValueQuantization{.levels = 256});
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects priority(const EventTiming& timing, u8 value) {
    const u32 delay = beginEvent(timing);
    track.priority = value;
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects fineTuning(const EventTiming& timing, u8 value) {
    const u32 delay = beginEvent(timing);
    track.fineTuning = value;
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects echoEnabled(const EventTiming& timing, bool enabled) {
    const u32 delay = beginEvent(timing);
    track.mode = enabled ? static_cast<u8>(track.mode | 0x08) : static_cast<u8>(track.mode & ~u8{0x08});
    track.echoEnabled = enabled;
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects echoParameters(const EventTiming& timing, u8 delayValue, s8 volumeValue, s8 feedbackValue,
                                       u8 filter) {
    const u32 delay = beginEvent(timing);
    program.echoDelay = delayValue;
    program.echoVolume = volumeValue;
    program.echoFeedback = feedbackValue;
    program.echoFilter = filter;
    emitEcho(out);
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects repeatStart(const EventTiming& timing, u8 count, Address start) {
    const u32 delay = beginEvent(timing);
    if (track.repeatDepth >= track.repeats.size()) {
      return vm.end();
    }
    track.repeats[track.repeatDepth++] = RepeatFrame{
        .start = start,
        .remaining = static_cast<u16>(count == 0 ? 256 : count),
    };
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects repeatEnd(const EventTiming& timing) {
    const u32 delay = beginEvent(timing);
    if (track.repeatDepth == 0) {
      return vm.end();
    }
    RepeatFrame& repeat = track.repeats[track.repeatDepth - 1];
    Effects result;
    if (--repeat.remaining != 0) {
      result = vm.finiteBranch(repeat.start);
    } else {
      --track.repeatDepth;
    }
    result.advanceTicks = delay;
    return result;
  }

  [[nodiscard]] Effects noteBase(const EventTiming& timing, u8 value) {
    const u32 delay = beginEvent(timing);
    track.noteBase = value;
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects changeOctave(const EventTiming& timing, s8 amount) {
    const u32 delay = beginEvent(timing);
    track.noteBase = static_cast<u8>(track.noteBase + amount);
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects persistentWait(const EventTiming& timing) {
    static_cast<void>(beginEvent(timing));
    return Effects::wait(clockTicks(track.delta));
  }

  [[nodiscard]] Effects mode(const EventTiming& timing, u8 value) {
    const u32 delay = beginEvent(timing);
    track.mode = static_cast<u8>((track.mode & ~u8{2}) | value);
    track.echoEnabled = (track.mode & 0x08) != 0;
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects bendRange(const EventTiming& timing, u8 value) {
    const u32 delay = beginEvent(timing);
    track.bendRange = value;
    out.pitchBendRange(PitchBendRangePerformanceEvent{
        .cents = static_cast<u16>(std::lround(value * (100.0 / 8.0))),
    });
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects transposeAdd(const EventTiming& timing, s8 value) {
    const u32 delay = beginEvent(timing);
    track.transpose = static_cast<s8>(track.transpose + value);
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects fineTuningAdd(const EventTiming& timing, s8 value) {
    const u32 delay = beginEvent(timing);
    std::tie(track.transpose, track.fineTuning) = addFinePitch(track.transpose, track.fineTuning, value, true);
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects relativeVolume(const EventTiming& timing, s8 value) {
    const u32 delay = beginEvent(timing);
    // The SPC700 ADC is intentionally eight-bit; the legacy parser's
    // saturating/double-add behavior was not what the driver executes.
    track.volume = static_cast<u8>(track.volume + value);
    out.level(byteGain(track.volume), ValueQuantization{.levels = 256});
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects pitchBend(const EventTiming& timing, s8 position) {
    const u32 delay = beginEvent(timing);
    out.pitchBend(PitchBendPerformanceEvent{
        .semitones = bendSemitones(position, track.bendRange),
        .normalizedWheelPosition = position / 128.0,
    });
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects voiceScript(const EventTiming& timing, Address address) {
    const u32 delay = beginEvent(timing);
    track.scriptAddress = static_cast<u16>(address.value);
    if (!track.percussion) {
      program.references.directScriptKeys.try_emplace(track.scriptAddress);
      out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain),
                                        .key = kDirectInstrumentFlag | track.scriptAddress},
                     InstrumentEnvelopeMode::UseInstrumentEnvelope);
    }
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects group(const EventTiming& timing, u8 value) {
    const u32 delay = beginEvent(timing);
    track.group = value;
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects noise(const EventTiming& timing, u8 value, bool relative) {
    const u32 delay = beginEvent(timing);
    program.noiseClock = relative ? static_cast<u8>((program.noiseClock + value) & 0x1f) : value & 0x1f;
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects presetPitch(const EventTiming& timing, u8 index) {
    const u32 delay = beginEvent(timing);
    if (track.data.has(track.presetTable + index, 1) && track.data.has(track.presetPitchHigh + index, 1)) {
      track.fineTuning = track.data.u8At(track.presetTable + index);
      track.transpose = static_cast<s8>(track.data.u8At(track.presetPitchHigh + index));
    }
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects presetVolume(const EventTiming& timing, u8 index) {
    const u8 value = track.data.has(track.presetTable + index, 1) ? track.data.u8At(track.presetTable + index) : 0;
    return volume(timing, value);
  }

  [[nodiscard]] Effects presetPan(const EventTiming& timing, u8 index) {
    const u8 value = track.data.has(track.presetTable + index, 1) ? track.data.u8At(track.presetTable + index) : 0;
    return pan(timing, value);
  }

  [[nodiscard]] Effects presetWait(const EventTiming& timing, u8 index) {
    // E5 writes its own countdown and does not pass the table value through
    // E6's halving helper.
    static_cast<void>(beginEvent(timing));
    u8 value = track.data.has(track.presetTable + index, 1) ? track.data.u8At(track.presetTable + index) : 1;
    if (value == 0) {
      value = 1;
    }
    return Effects::wait(clockTicks(value, false));
  }

  [[nodiscard]] Effects timebase(const EventTiming& timing, u8 value) {
    const u32 delay = beginEvent(timing);
    program.fast = (value & 1) != 0;
    return Effects::wait(delay);
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

struct SfxRuntimeConfig {
  DriverConfig driver;
  std::vector<u16> scripts;
};

struct SfxTrackState : DriverConfig {
  SfxTrackState(const TrackProgram& source, const SfxRuntimeConfig& config)
      : DriverConfig(config.driver), script(config.scripts.at(source.sourceTrackNumber)) {}

  u16 script = 0;
};

struct SfxPlayback {
  SfxTrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;

  [[nodiscard]] Effects play() {
    const VoiceScriptAnalysis limits = analyzeVoiceScript(track, track.script);
    const auto emitPan = [&](PerformanceEmitter output, u8 value, bool explicitPan) {
      const u8 raw = explicitPan && value < 0x80 ? value : track.data.u8At(0x3b);
      const StereoBalance balance = driverPanGains(track, raw);
      output.stereoBalance(balance.leftGain, balance.rightGain);
    };
    emitPan(out, limits.attackPan, limits.attackPanExplicit);
    out.reverb(0.0);
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain),
                                      .key = kDirectInstrumentFlag | track.script},
                   InstrumentEnvelopeMode::UseInstrumentEnvelope);

    NotePerformanceEvent note{
        .key = 60.0,
        .linearVelocity = sourceVelocityGain(track.data.u8At(0x3a)),
        .durationTicks = std::numeric_limits<u32>::max(),
    };
    const std::optional<u32> end = limits.attacks.empty() ? std::nullopt : limits.attacks.front().keyOff;
    if (end) {
      note.maximumDurationMilliseconds = *end * track.traits.timerMilliseconds();
    }
    const u64 attackTick = vm.tick();
    emitVoicePitchChanges(limits, out, 0.0, attackTick, kInitialTempo);
    out.note(std::move(note));
    emitVoiceVolumeChanges(limits, out, attackTick, kInitialTempo);

    if (limits.attacks.size() > 1) {
      const VoiceScriptAnalysis::Attack& first = limits.attacks.front();
      const double sourceVelocity = sourceVelocityGain(track.data.u8At(0x3a));
      for (auto attack = std::next(limits.attacks.begin()); attack != limits.attacks.end(); ++attack) {
        auto output = out.at(attackTick + attack->tick * kInitialTempo);
        emitPan(output, attack->pan, attack->panExplicit);
        output.reverb(0.0);
        NotePerformanceEvent retrigger{
            .key = 60.0 + (attack->pitch256 - first.pitch256) / 256.0,
            .linearVelocity = first.volume == 0 ? 0.0 : sourceVelocity * attack->volume / first.volume,
            .durationTicks = std::numeric_limits<u32>::max(),
        };
        if (attack->keyOff) {
          retrigger.maximumDurationMilliseconds = *attack->keyOff * track.traits.timerMilliseconds();
        }
        output.pitchBend(0.0, kVoiceScriptPitchLayer);
        output.note(std::move(retrigger));
      }
    }
    return {};
  }
};

struct SfxCursor {
  using TrackState = SfxTrackState;
  using Playback = SfxPlayback;
};

[[nodiscard]] const char* commandLabel(Version version, u8 status) {
  if (status < 0xc0) {
    return "Note";
  }
  constexpr std::array<const char*, 39> labels{
      "Instrument",       "Pan",              "Master Volume",   "Tempo",           "Initial Transpose",
      "Volume",           "Priority",         "Fine Tuning",     "Echo On",         "Echo Off",
      "Echo Parameters",  "Jump",             "Call",            "Return",          "Repeat Start",
      "Repeat End",       "End",              "Note Base",       "Octave Up",       "Octave Down",
      "Wait",             "Track Mode",       "Pitch Bend Range", "Transpose",       "Transpose Add",
      "Fine Tuning Add",  "Key On",           "Key Off",         "Volume Add",      "Pitch Bend",
      "Voice Script",     "Voice Group",      "DSP FLG",         "DSP FLG Add",     "Pitch Preset",
      "Volume Preset",    "Pan Preset",       "Wait Preset",     "Timebase",
  };
  if (const auto command = canonicalCommand(version, status)) {
    return labels[*command - 0xc0];
  }
  return version == Version::Shien && status == 0xf7 ? "Sync Counter" : "No-op";
}

[[nodiscard]] SequenceSemantic commandSemantic(Version version, u8 status) {
  if (status < 0xc0) {
    return SequenceSemantic::Note;
  }
  const auto command = canonicalCommand(version, status);
  if (!command) {
    return SequenceSemantic::State;
  }
  switch (*command) {
    case 0xc0:
      return SequenceSemantic::Program;
    case 0xc1:
    case 0xe4:
      return SequenceSemantic::Pan;
    case 0xc2:
    case 0xc5:
    case 0xdc:
    case 0xe3:
      return SequenceSemantic::Level;
    case 0xc3:
    case 0xe6:
      return SequenceSemantic::Tempo;
    case 0xc4:
    case 0xc7:
    case 0xd6:
    case 0xd7:
    case 0xd8:
    case 0xd9:
    case 0xdd:
    case 0xe2:
      return SequenceSemantic::Pitch;
    case 0xcb:
      return SequenceSemantic::Jump;
    case 0xcc:
      return SequenceSemantic::Call;
    case 0xcd:
      return SequenceSemantic::Return;
    case 0xce:
    case 0xcf:
      return SequenceSemantic::Repeat;
    case 0xd0:
      return SequenceSemantic::End;
    case 0xd4:
    case 0xe5:
      return SequenceSemantic::Rest;
    default:
      return SequenceSemantic::State;
  }
}

void consumeTiming(Cursor::Event& event, u8 first, const InspectedEvent& inspected) {
  if (!inspected.timing.delay) {
    return;
  }
  event.opcodeValue("delta", first, SourceValueDisplay::Default, SemanticOperandRole::Duration);
  if (inspected.timing.duration) {
    static_cast<void>(event.u8("duration", SemanticOperandRole::Duration));
  }
  if (inspected.timing.velocity) {
    static_cast<void>(event.u8("velocity", SemanticOperandRole::Level));
  }
  static_cast<void>(event.u8("status", SourceValueDisplay::Hex));
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, const Layout& layout, u32 begin,
                                                   std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, "mori-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const InspectedEvent inspected = inspectEvent(reader, begin);
  if (!inspected.valid) {
    return cursor.truncated();
  }
  const u8 status = inspected.status;
  auto event = cursor.command(commandLabel(layout.version, status), commandSemantic(layout.version, status));
  consumeTiming(event, cursor.opcode(), inspected);

  if (status < 0xc0) {
    const u8 note = status & 0x1f;
    event.derived("note_index", note, SemanticOperandRole::NoteKey);
    std::optional<u8> parameter;
    if (status >= 0xa0) {
      parameter = event.u8("note_parameter", SourceValueDisplay::Hex);
    }
    return event.invoke<&Playback::note>(inspected.timing, note, parameter);
  }

  if (!isCommand(layout.version, status)) {
    return event.stop();
  }
  const std::optional<u8> command = canonicalCommand(layout.version, status);
  if (!command) {
    if (commandSize(layout.version, status) != 0) {
      static_cast<void>(event.u8("value", SourceValueDisplay::Hex));
    }
    return event.invoke<&Playback::wait>(inspected.timing);
  }

  switch (*command) {
    case 0xc0: {
      const s16 relative = event.s16le("relative", SourceValueDisplay::SignedDecimal,
                                      SemanticOperandRole::InstrumentTablePointer);
      const Address descriptor{relativeTarget(static_cast<u16>(event.nextAddress().value), relative)};
      event.derived("descriptor", descriptor, SourceValueDisplay::Address, SemanticOperandRole::Instrument);
      return event.invoke<&Playback::instrument>(inspected.timing, descriptor);
    }
    case 0xc1:
      return event.invoke<&Playback::pan>(inspected.timing, event.u8("pan", SemanticOperandRole::Pan));
    case 0xc2:
      return event.invoke<&Playback::masterVolume>(inspected.timing,
                                                   event.u8("volume", SemanticOperandRole::Level));
    case 0xc3:
      return event.invoke<&Playback::tempo>(inspected.timing, event.u8("tempo"));
    case 0xc4:
      return event.invoke<&Playback::transpose>(inspected.timing,
                                                event.s8("semitones", SemanticOperandRole::Pitch));
    case 0xc5:
      return event.invoke<&Playback::volume>(inspected.timing, event.u8("volume", SemanticOperandRole::Level));
    case 0xc6:
      return event.invoke<&Playback::priority>(inspected.timing, event.u8("priority"));
    case 0xc7:
      return event.invoke<&Playback::fineTuning>(inspected.timing,
                                                 event.u8("fraction", SemanticOperandRole::Pitch));
    case 0xc8:
    case 0xc9:
      return event.invoke<&Playback::echoEnabled>(inspected.timing, *command == 0xc8);
    case 0xca: {
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const s8 volume = event.s8("volume", SemanticOperandRole::Level);
      const s8 feedback = event.s8("feedback");
      return event.invoke<&Playback::echoParameters>(inspected.timing, delay, volume, feedback,
                                                     event.u8("fir_preset"));
    }
    case 0xcb:
    case 0xcc: {
      const s16 relative = event.s16le("relative", SourceValueDisplay::SignedDecimal,
                                      *command == 0xcb ? SemanticOperandRole::JumpTarget
                                                       : SemanticOperandRole::CallTarget);
      const Address destination{relativeTarget(static_cast<u16>(event.nextAddress().value), relative)};
      event.derived("destination", destination, SourceValueDisplay::Address,
                    *command == 0xcb ? SemanticOperandRole::JumpTarget : SemanticOperandRole::CallTarget);
      event.invoke<&Playback::wait>(inspected.timing);
      if (*command == 0xcc) {
        return event.call(destination);
      }
      return destination.value < begin ? event.loopCandidate(destination) : event.jump(destination);
    }
    case 0xcd:
      return event.invoke<&Playback::wait>(inspected.timing).return_();
    case 0xce: {
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      return event.invokeFlow<&Playback::repeatStart>(inspected.timing, count, event.nextAddress());
    }
    case 0xcf:
      return event.invokeFlow<&Playback::repeatEnd>(inspected.timing);
    case 0xd0:
      return event.end();
    case 0xd1:
      return event.invoke<&Playback::noteBase>(inspected.timing,
                                               event.u8("note", SourceValueDisplay::MidiNote,
                                                        SemanticOperandRole::NoteKey));
    case 0xd2:
      return event.invoke<&Playback::changeOctave>(inspected.timing, s8{12});
    case 0xd3:
      return event.invoke<&Playback::changeOctave>(inspected.timing, s8{-12});
    case 0xd4:
      return event.invoke<&Playback::persistentWait>(inspected.timing);
    case 0xd5:
      return event.invoke<&Playback::mode>(inspected.timing, event.u8("flags", SourceValueDisplay::Hex));
    case 0xd6:
      return event.invoke<&Playback::bendRange>(inspected.timing,
                                                event.u8("eighth_semitones", SemanticOperandRole::Pitch));
    case 0xd7:
      return event.invoke<&Playback::transpose>(inspected.timing,
                                                event.s8("semitones", SemanticOperandRole::Pitch));
    case 0xd8:
      return event.invoke<&Playback::transposeAdd>(inspected.timing,
                                                   event.s8("semitones", SemanticOperandRole::Pitch));
    case 0xd9:
      return event.invoke<&Playback::fineTuningAdd>(inspected.timing,
                                                    event.s8("fraction", SemanticOperandRole::Pitch));
    case 0xda:
    case 0xdb:
      // These handlers manipulate the current hardware-voice mask. The source
      // track pass has no voice mask selected, so both are inert in song data.
      return event.invoke<&Playback::wait>(inspected.timing);
    case 0xdc:
      return event.invoke<&Playback::relativeVolume>(inspected.timing,
                                                     event.s8("delta", SemanticOperandRole::Level));
    case 0xdd:
      return event.invoke<&Playback::pitchBend>(inspected.timing,
                                               event.s8("position", SemanticOperandRole::Pitch));
    case 0xde: {
      const s16 relative = event.s16le("relative", SourceValueDisplay::SignedDecimal,
                                      SemanticOperandRole::InstrumentTablePointer);
      const Address address{relativeTarget(static_cast<u16>(event.nextAddress().value), relative)};
      event.derived("script", address, SourceValueDisplay::Address, SemanticOperandRole::Instrument);
      return event.invoke<&Playback::voiceScript>(inspected.timing, address);
    }
    case 0xdf:
      return event.invoke<&Playback::group>(inspected.timing, event.u8("group"));
    case 0xe0:
    case 0xe1:
      return event.invoke<&Playback::noise>(inspected.timing, event.u8("noise_clock"), *command == 0xe1);
    case 0xe2:
      return event.invoke<&Playback::presetPitch>(inspected.timing, event.u8("preset"));
    case 0xe3:
      return event.invoke<&Playback::presetVolume>(inspected.timing, event.u8("preset"));
    case 0xe4:
      return event.invoke<&Playback::presetPan>(inspected.timing, event.u8("preset"));
    case 0xe5:
      return event.invoke<&Playback::presetWait>(inspected.timing, event.u8("preset"));
    case 0xe6:
      return event.invoke<&Playback::timebase>(inspected.timing, event.u8("flags", SourceValueDisplay::Hex));
    default:
      return event.stop();
  }
}

}  // namespace

const SequenceProgramConfig& sequenceConfig(Version version) {
  const auto makeConfig = [](Version version) {
    const DriverTraits traits = driverTraits(version);
    const double center = (version == Version::Shien ? 0x64 : 0x60) / 128.0;
    return SequenceProgramConfig{
        .commandKindPrefix = "mori-snes",
        .timebase = Timebase{.ppqn = kPpqn},
        .behavior =
            SequenceProgramBehavior{
                .commandLimit = kCommandLimit,
                .initialLevel = byteGain(kInitialTrackVolume),
                .initialMasterLevel = masterGain(traits, kInitialMasterVolume),
                .initialReverbSend = echoSend(kInitialEchoVolume),
                .initialStereoBalance = StereoBalance{.leftGain = center, .rightGain = center},
                .initialPitchBendRangeSemitones = traits.initialBendRange / 8,
                .initialTempoMicrosecondsPerQuarter = tempoMicrosecondsPerQuarter(kInitialTempo, traits.timerTarget),
            },
    };
  };
  static const SequenceProgramConfig gokinjo = makeConfig(Version::Gokinjo);
  static const SequenceProgramConfig shien = makeConfig(Version::Shien);
  return version == Version::Shien ? shien : gokinjo;
}

SequenceRuntime sequenceRuntime(ByteReader reader, const Layout& layout) {
  return makeCompiledRuntime<Cursor, ProgramState>(DriverConfig{
      .data = reader,
      .traits = driverTraits(layout.version),
      .presetTable = layout.presetTableAddress,
      .presetPitchHigh = layout.presetPitchHighAddress,
      .panTable = layout.panTableAddress,
  });
}

TrackProgram decodeSourceTrack(ByteReader reader, const Layout& layout, u32 trackNumber, u32 startAddress,
                               std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope tracks{.reader = reader, .maxCommands = kCommandLimit, .bytecodeEnd = kAramSize};
  return tracks.decode(trackNumber, startAddress,
                       [&](u32 offset) { return decodeCommand(reader, layout, offset, diagnostics); });
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  if (!layout.sfxVoices.empty()) {
    const SourceRange header = reader.range(layout.songHeaderAddress, 2u + layout.sfxVoices.size() * 3u);
    const SourceAnnotationId root = sourceMap != nullptr
                                        ? sourceMap->header("Sound Effect", header)
                                              .kind("mori-snes-sfx")
                                              .owner(ObjectRefs::sequence(sequenceId))
                                              .id()
                                        : SourceAnnotationId{};
    SequenceProgram program = sequenceConfig(layout.version).makeProgram();
    program.behavior.initialLevel = 1.0;
    program.behavior.initialReverbSend = 0.0;
    std::vector<u16> scripts;
    scripts.reserve(layout.sfxVoices.size());
    ReferencedInstruments references;
    for (u32 index = 0; index < layout.sfxVoices.size(); ++index) {
      const SfxVoice& voice = layout.sfxVoices[index];
      scripts.push_back(voice.scriptAddress);
      references.directScriptKeys[voice.scriptAddress].insert(60);
      const Address commandAddress{voice.range.offset};
      const Address continuation{voice.range.offset + voice.range.size};
      SourceAnnotationId trackAnnotation;
      SourceAnnotationId commandAnnotation;
      if (sourceMap != nullptr) {
        trackAnnotation = sourceMap
                              ->annotation(SourceRole::SequenceTrack, fmt::format("Voice {}", index + 1), voice.range)
                              .kind("mori-snes-sfx-track")
                              .parent(root)
                              .id();
        commandAnnotation = sourceMap->command("Play Voice Script", voice.range, SequenceSemantic::Note)
                                .kind("mori-snes-sfx-voice")
                                .parent(trackAnnotation)
                                .derived("script", voice.scriptAddress, SourceValueDisplay::Address)
                                .id();
      }
      SourceCommand command{
          .address = commandAddress,
          .range = voice.range,
          .annotation = commandAnnotation,
          .semantic = SequenceSemantic::Note,
          .flow = CommandFlow::end(continuation),
      };
      command.execution.body = [](void* playback) {
        return static_cast<SfxPlayback*>(playback)->play();
      };
      program.tracks.push_back(TrackProgram{
          .sourceTrackNumber = index,
          .startAddress = commandAddress,
          .annotation = trackAnnotation,
          .commands = {std::move(command)},
      });
    }
    program.runtime = makeCompiledRuntime<SfxCursor>(SfxRuntimeConfig{
        .driver = DriverConfig{
            .data = reader,
            .traits = driverTraits(layout.version),
            .presetTable = layout.presetTableAddress,
            .presetPitchHigh = layout.presetPitchHighAddress,
            .panTable = layout.panTableAddress,
        },
        .scripts = std::move(scripts),
    });
    return SequenceParse{
        .program = std::move(program),
        .references = std::move(references),
        .headerRange = header,
    };
  }

  const u32 headerSize = layout.tracks.size() * 3u + 1u;
  const SourceRange header = reader.range(layout.songHeaderAddress, headerSize);
  SequenceDecodeSession sequence{reader, sequenceConfig(layout.version), sequenceId, header, sourceMap, kCommandLimit,
                                 kAramSize};
  for (const TrackHeader& track : layout.tracks) {
    const u16 encoded = reader.le16(track.range.offset + 1);
    sequence.addTrack(track.channel, reader.range(track.range.offset + 1, 2), track.startAddress,
                      [&](u32 offset) { return decodeCommand(reader, layout, offset, diagnostics); }, encoded);
  }
  SequenceProgram program = sequence.finish(sequenceRuntime(reader, layout));
  const DriverTraits traits = driverTraits(layout.version);
  if (reader.has(layout.panTableAddress + traits.initialPan, 1)) {
    const double center = reader.u8At(layout.panTableAddress + traits.initialPan) / 128.0;
    program.behavior.initialStereoBalance = StereoBalance{.leftGain = center, .rightGain = center};
  }
  ReferencedInstruments references = analyzeCompiledProgram<ProgramState>(
      program, &ProgramState::references, diagnostics, SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce});
  return SequenceParse{.program = std::move(program), .references = std::move(references), .headerRange = header};
}

}  // namespace vgmtrans::formats::mori_snes
