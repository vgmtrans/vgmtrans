/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/PrismSnes/PrismSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace vgmtrans::formats::prism_snes {

using namespace core;

namespace {

constexpr u32 kCommandLimit = 32768;

namespace math {

[[nodiscard]] constexpr u32 tempoMicrosecondsPerQuarter(u8 timerTarget) {
  return 6000u * (timerTarget == 0 ? 256u : timerTarget);
}

[[nodiscard]] constexpr u16 ticks(u8 encoded) {
  return encoded == 0 ? 256 : encoded;
}

[[nodiscard]] constexpr u16 counter(u8 encoded) {
  return encoded == 0 ? 256 : encoded;
}

[[nodiscard]] constexpr double tuningCents(u8 fraction) {
  return fraction * (100.0 / 256.0);
}

[[nodiscard]] constexpr double pitchDriftPerTick(s8 raw) {
  return raw / 128.0;
}

[[nodiscard]] constexpr double gain(u8 raw) {
  return raw / 255.0;
}

[[nodiscard]] constexpr double signedDspGain(s8 raw) {
  return raw / 128.0;
}

[[nodiscard]] Envelope neutralGainEnvelope() {
  return Envelope{
      .attackSeconds = 0.0,
      .holdSeconds = 0.0,
      .decaySeconds = std::numeric_limits<double>::infinity(),
      .releaseSeconds = 0.0,
      .sustainAmplitude = 1.0,
  };
}

}  // namespace math

struct RuntimeData {
  std::shared_ptr<const std::array<u8, kAramSize>> aram;
  u16 adsr1Table = 0;
  u16 adsr2Table = 0;
  u16 alternatePanTable = 0;
  u16 defaultPanTable = 0;
  u8 echoDelay = 0;
  u8 echoFilter = 0;

  [[nodiscard]] u8 u8At(u16 address) const { return (*aram)[address]; }

  [[nodiscard]] u16 le16(u16 address) const {
    return static_cast<u16>(u8At(address) | (u8At(static_cast<u16>(address + 1)) << 8));
  }

  [[nodiscard]] static RuntimeData capture(ByteReader reader, const Layout& layout) {
    auto aram = std::make_shared<std::array<u8, kAramSize>>();
    for (u32 address = 0; address < kAramSize; ++address) {
      (*aram)[address] = reader.u8At(address);
    }
    return RuntimeData{
        .aram = std::move(aram),
        .adsr1Table = layout.adsr1TableAddress,
        .adsr2Table = layout.adsr2TableAddress,
        .alternatePanTable = layout.alternatePanTableAddress,
        .defaultPanTable = layout.defaultPanTableAddress,
        .echoDelay = layout.echoDelay,
        .echoFilter = layout.echoFilter,
    };
  }
};

struct RuntimeTrackConfig {
  u8 logicalChannel = 0;
  u8 physicalChannelFlags = 0;
};

struct RuntimeConfig {
  Version version = Version::Modern;
  RuntimeData data;
  std::vector<RuntimeTrackConfig> tracks;

  [[nodiscard]] const RuntimeTrackConfig& track(const TrackProgram& source) const {
    return tracks.at(source.sourceTrackNumber);
  }
};

struct VolumeEnvelope {
  u16 cursor = 0;
  u16 recordCounter = 0;
  u16 speedCounter = 0;
  u16 speed = 0;
  s8 delta = 0;
  bool active = false;

  void start(u16 address) {
    cursor = address;
    recordCounter = 1;
    speedCounter = 0;
    speed = 0;
    delta = 0;
    active = static_cast<u8>(address >> 8) != 0;
  }

  void stop() { active = false; }
};

struct OffsetEnvelope {
  u16 address = 0;
  u8 offset = 0;
  u16 counter = 0;
  u16 speed = 1;
  bool active = false;

  void start(u16 newAddress, u8 encodedSpeed = 1) {
    address = newAddress;
    offset = 0;
    counter = 1;
    speed = math::counter(encodedSpeed);
    active = static_cast<u8>(newAddress >> 8) != 0;
  }

  void stop() { active = false; }
};

struct VibratoState {
  u16 address = 0;
  u8 offset = 0;
  u8 delay = 0;
  u16 delayCounter = 0;
  s8 sample = 0;
  bool enabled = false;

  void restartNote() {
    offset = 0;
    delayCounter = math::counter(delay);
    sample = 0;
  }
};

struct EchoState {
  s8 left = 0;
  s8 right = 0;
  s8 mono = 0;
  s8 feedback = 0;
  u8 delay = 0;
  u8 filter = 0;
  u8 voiceMask = 0;
};

struct MasterFade {
  u8 target = 0xff;
  u8 rate = 0;
  u8 fraction = 0;
  bool up = false;
};

struct ProgramState {
  ProgramState(const SequenceProgram&, const RuntimeConfig& config) : version(config.version), data(config.data) {
    echo.delay = data.echoDelay;
    echo.filter = data.echoFilter;
  }

  Version version = Version::Modern;
  RuntimeData data;
  bool condition = false;
  u8 masterVolume = 0xff;
  u8 masterDuck = 0;
  u32 tempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(0x82);
  MasterFade masterFade;
  EchoState echo;
  std::optional<EchoState> savedEcho;
  OffsetEnvelope echoEnvelope;
  std::optional<u64> lastGlobalTick;
};

struct RuntimeTrack;

struct TrackState {
  TrackState(const TrackProgram& source, const RuntimeConfig& config)
      : TrackState(config.version, config.data, config.track(source).logicalChannel,
                   config.track(source).physicalChannelFlags) {}

  TrackState(Version newVersion, RuntimeData runtimeData, u8 newLogicalChannel, u8 newPhysicalChannelFlags)
      : version(newVersion), data(runtimeData), logicalChannel(newLogicalChannel),
        physicalChannelFlags(newPhysicalChannelFlags), panTable(data.defaultPanTable) {
    loadDefaultAdsr();
  }

  void loadDefaultAdsr() {
    adsr1 = data.u8At(static_cast<u16>(data.adsr1Table + program));
    adsr2 = data.u8At(static_cast<u16>(data.adsr2Table + program));
  }

  Version version = Version::Modern;
  RuntimeData data;
  u8 logicalChannel = 0;
  u8 physicalChannelFlags = 0;
  u8 program = 0;
  bool programPending = false;
  InstrumentEnvelopeMode pendingProgramEnvelopeMode = InstrumentEnvelopeMode::UseInstrumentEnvelope;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 volume = 0;
  u8 pan = 10;
  u16 panTable = 0;
  s8 transpose = 0;
  bool slur = false;
  bool suppressAttack = false;
  bool specialSequence = false;
  u8 specialLogicalChannel = 0;
  u16 specialTable = 0;

  u16 remaining = 0;
  u8 releaseTime = 1;
  u8 activeReleaseTimer = 0;
  u8 activeSustainTimer = 0;
  u8 fixedGain = 0x9c;
  bool gainMode = false;
  u16 keyOnGainAddress = 0;
  u16 sustainGainAddress = 0;
  u16 tieGainAddress = 0;
  OffsetEnvelope gainEnvelope;
  u8 activeGain = 0x7f;
  s16 gainLevel = 0x7ff;
  s16 gainPhaseStart = 0x7ff;
  double gainPhaseSeconds = 0.0;
  PerformanceAutomationBinding gainAutomation;
  PerformanceAutomationBinding interruptedGainAutomation;
  u32 gainImmediateGeneration = 0;
  VolumeEnvelope volumeEnvelope;
  OffsetEnvelope panEnvelope;
  VibratoState vibrato;

  s8 pitchDrift = 0;
  double accumulatedDrift = 0.0;
  PerformanceNoteId lastNote;
  std::optional<double> lastKey;
  PerformanceNoteId interruptedNote;
  std::shared_ptr<RuntimeTrack> subtrack;
};

struct RuntimeTrack {
  RuntimeTrack(const TrackState& controller, u8 logicalChannel, u16 startAddress)
      : state(controller.version, controller.data, logicalChannel,
              static_cast<u8>(controller.physicalChannelFlags & 0x1f)),
        cursor(startAddress) {
    state.volume = controller.volume;
    state.pan = controller.pan;
    state.remaining = 2;
  }

  TrackState state;
  u16 cursor = 0;
  u8 defaultLength = 0;
  bool manualDuration = false;
  u8 autoDurationThreshold = 0;
  std::array<u8, 2> repeats{};
  u16 returnAddress = 0;
  u32 commands = 0;
  bool active = true;
  bool initialized = false;
};

struct Timing {
  u16 length = 0;
  u8 durationTimer = 0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] u8 physicalChannel() const { return track.physicalChannelFlags & 7; }

  void emitLevel() { out.level(math::gain(track.volume), ValueQuantization{.levels = 256}); }

  void emitPan() {
    const u8 pan = std::min<u8>(track.pan, 20);
    out.stereoBalance(track.data.u8At(static_cast<u16>(track.panTable + 20 - pan)) / 127.0,
                      track.data.u8At(static_cast<u16>(track.panTable + pan)) / 127.0);
  }

  void emitPitchBend() { out.pitchBend(track.accumulatedDrift + track.vibrato.sample / 256.0); }

  void emitMaster() {
    out.masterLevel(math::gain(
        static_cast<u8>(program.masterVolume > program.masterDuck ? program.masterVolume - program.masterDuck : 0)));
  }

  void emitEcho() {
    const s8 left = program.echo.left;
    const s8 right = program.echo.right;
    const double send =
        program.echo.voiceMask == 0
            ? 0.0
            : std::clamp(std::max(std::abs(static_cast<int>(left)), std::abs(static_cast<int>(right))) / 128.0, 0.0,
                         1.0);
    out.reverb(ReverbPerformanceEvent{
        .voiceMask = program.echo.voiceMask,
        .send = send,
        .leftGain = math::signedDspGain(left),
        .rightGain = math::signedDspGain(right),
        .delayMilliseconds = program.echo.delay * 16.0,
        .feedback = math::signedDspGain(program.echo.feedback),
        .filterIndex = program.echo.filter,
    });
  }

  void tickGlobal() {
    if (program.lastGlobalTick && *program.lastGlobalTick == vm.tick()) {
      return;
    }
    program.lastGlobalTick = vm.tick();

    if (program.masterFade.rate != 0) {
      // SPC700 arithmetic is eight-bit here; a carry out is discarded before
      // the high nibble becomes the integer fade step.
      const u8 sum = static_cast<u8>(program.masterFade.fraction + program.masterFade.rate);
      program.masterFade.fraction = sum & 0x0f;
      const u8 step = sum >> 4;
      const u8 before = program.masterVolume;
      if (program.masterFade.up) {
        program.masterVolume = static_cast<u8>(std::min<int>(program.masterVolume + step, program.masterFade.target));
      } else {
        program.masterVolume = static_cast<u8>(std::max<int>(program.masterVolume - step, program.masterFade.target));
      }
      if (program.masterVolume == program.masterFade.target) {
        program.masterFade.rate = 0;
      }
      if (before != program.masterVolume) {
        emitMaster();
      }
    }

    auto& envelope = program.echoEnvelope;
    if (!envelope.active || envelope.counter == 0 || --envelope.counter != 0) {
      return;
    }
    for (u32 redirects = 0; redirects < 256; ++redirects) {
      const u16 cursor = static_cast<u16>(envelope.address + envelope.offset);
      const u8 first = program.data.u8At(cursor);
      if (first == 0xff) {
        envelope.offset = program.data.u8At(static_cast<u16>(cursor + 1));
        continue;
      }
      program.echo.left = static_cast<s8>(first);
      program.echo.right = static_cast<s8>(program.data.u8At(static_cast<u16>(cursor + 1)));
      program.echo.mono = static_cast<s8>(program.data.u8At(static_cast<u16>(cursor + 2)));
      envelope.counter = math::counter(program.data.u8At(static_cast<u16>(cursor + 3)));
      envelope.offset = static_cast<u8>(envelope.offset + 4);
      emitEcho();
      return;
    }
    envelope.stop();
  }

  void tickVolumeEnvelope() {
    auto& envelope = track.volumeEnvelope;
    if (!envelope.active || envelope.recordCounter == 0 || --envelope.recordCounter == 0) {
      if (!envelope.active) {
        return;
      }
      for (u32 redirects = 0; redirects < 256; ++redirects) {
        const u8 volume = track.data.u8At(envelope.cursor);
        const u8 delta = track.data.u8At(static_cast<u16>(envelope.cursor + 1));
        if (volume == 0 && delta == 0) {
          envelope.cursor = track.data.le16(static_cast<u16>(envelope.cursor + 2));
          continue;
        }
        track.volume = volume;
        envelope.delta = static_cast<s8>(delta);
        envelope.speed = math::counter(track.data.u8At(static_cast<u16>(envelope.cursor + 2)));
        envelope.speedCounter = envelope.speed;
        envelope.recordCounter = math::counter(track.data.u8At(static_cast<u16>(envelope.cursor + 3)));
        envelope.cursor = static_cast<u16>(envelope.cursor + 4);
        emitLevel();
        return;
      }
      envelope.stop();
      return;
    }
    if (envelope.speedCounter != 0 && --envelope.speedCounter == 0) {
      envelope.speedCounter = envelope.speed;
      track.volume = static_cast<u8>(std::clamp<int>(track.volume + envelope.delta, 0, 255));
      emitLevel();
    }
  }

  void tickPanEnvelope() {
    auto& envelope = track.panEnvelope;
    if (!envelope.active || envelope.counter == 0 || --envelope.counter != 0) {
      return;
    }
    for (u32 redirects = 0; redirects < 256; ++redirects) {
      const u16 cursor = static_cast<u16>(envelope.address + envelope.offset);
      const u8 value = track.data.u8At(cursor);
      if ((value & 0x80) != 0) {
        envelope.offset = track.data.u8At(static_cast<u16>(cursor + 1));
        continue;
      }
      track.pan = value;
      envelope.offset = static_cast<u8>(envelope.offset + 1);
      envelope.counter = envelope.speed;
      emitPan();
      return;
    }
    envelope.stop();
  }

  void emitGainLevel() {
    if (track.gainAutomation.valid()) {
      track.gainAutomation.output(out).expression(std::clamp(track.gainLevel / 2047.0, 0.0, 1.0));
    }
  }

  void setGain(u8 gain) {
    track.activeGain = gain;
    track.gainPhaseStart = track.gainLevel;
    track.gainPhaseSeconds = 0.0;
    if (gain < 0x80) {
      track.gainLevel = static_cast<s16>((gain & 0x7f) * 0x10);
      track.gainPhaseStart = track.gainLevel;
      emitGainLevel();
    }
  }

  void tickGainLevel() {
    if (!track.gainAutomation.valid()) {
      return;
    }
    track.gainPhaseSeconds += program.tempoMicrosecondsPerQuarter / (1'000'000.0 * kPpqn);
    const s16 before = track.gainLevel;
    track.gainLevel = snesDspGainEnvelopeValue(track.activeGain, track.gainPhaseStart, track.gainPhaseSeconds);
    if (before != track.gainLevel) {
      emitGainLevel();
    }
  }

  void tickGainEnvelope() {
    auto& envelope = track.gainEnvelope;
    if (!envelope.active || envelope.counter == 0 || --envelope.counter != 0) {
      return;
    }
    for (u32 redirects = 0; redirects < 256; ++redirects) {
      const u16 cursor = static_cast<u16>(envelope.address + envelope.offset);
      const u8 gain = track.data.u8At(cursor);
      if (gain == 0xff) {
        envelope.offset = track.data.u8At(static_cast<u16>(cursor + 1));
        continue;
      }
      envelope.counter = math::counter(track.data.u8At(static_cast<u16>(cursor + 1)));
      envelope.offset = static_cast<u8>(envelope.offset + 2);
      setGain(gain);
      return;
    }
    envelope.stop();
  }

  bool beginGainNote(u16 length) {
    track.gainEnvelope.start(track.keyOnGainAddress);
    if (!track.gainEnvelope.active) {
      track.gainAutomation.interrupt(out);
      out.expression(1.0);
      out.replaceEnvelope(driverEnvelope(track.adsr1, track.adsr2), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
      return false;
    }

    track.gainLevel = 0;
    track.gainPhaseStart = 0;
    track.gainPhaseSeconds = 0.0;
    track.gainAutomation.interrupt(out);
    track.gainAutomation = out.noteEnvelope(PerformanceAutomationTarget::Expression, 1.0, length);
    out.replaceEnvelope(math::neutralGainEnvelope(), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    emitGainLevel();
    tickGainEnvelope();
    ++track.gainImmediateGeneration;
    return true;
  }

  void stopGainMode() {
    track.gainEnvelope.stop();
    const bool wasActive = track.gainAutomation.valid();
    track.gainAutomation.interrupt(out);
    if (wasActive) {
      out.expression(1.0);
    }
  }

  void tickVibrato() {
    if (!track.vibrato.enabled || track.vibrato.delayCounter == 0 || --track.vibrato.delayCounter != 0) {
      return;
    }
    for (u32 redirects = 0; redirects < 256; ++redirects) {
      const u16 cursor = static_cast<u16>(track.vibrato.address + track.vibrato.offset);
      const u8 value = track.data.u8At(cursor);
      if (value == 0x80) {
        track.vibrato.offset = track.data.u8At(static_cast<u16>(cursor + 1));
        continue;
      }
      track.vibrato.sample = static_cast<s8>(value);
      ++track.vibrato.offset;
      track.vibrato.delayCounter = 1;
      emitPitchBend();
      return;
    }
    track.vibrato.enabled = false;
  }

  void advanceWait() {
    if (track.remaining != 0) {
      --track.remaining;
      if (track.remaining == track.activeSustainTimer && track.activeSustainTimer != 0) {
        track.gainEnvelope.start(track.sustainGainAddress);
      }
      if (track.remaining == track.activeReleaseTimer && track.activeReleaseTimer != 0) {
        track.gainEnvelope.stop();
        setGain(track.fixedGain);
      }
    }
  }

  void tickModulation(bool advanceGainEnvelope = true) {
    if (track.pitchDrift != 0) {
      track.accumulatedDrift += math::pitchDriftPerTick(track.pitchDrift);
      emitPitchBend();
    }
    tickVibrato();
    tickPanEnvelope();
    tickVolumeEnvelope();
    if (advanceGainEnvelope) {
      tickGainEnvelope();
    }
  }

  void tickRuntimeTrack(RuntimeTrack& runtime);

  void tick() {
    tickGlobal();
    tickGainLevel();
    advanceWait();
    tickModulation();
    if (track.subtrack && track.subtrack->active) {
      tickRuntimeTrack(*track.subtrack);
    }
  }

  void beginWait(u16 length, u8 durationTimer, bool fullGate) {
    track.remaining = length;
    track.activeReleaseTimer = fullGate ? 0 : track.releaseTime;
    track.activeSustainTimer = fullGate ? 0 : durationTimer;
  }

  void emitPendingProgram() {
    if (!track.programPending) {
      return;
    }
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = track.program},
                   track.pendingProgramEnvelopeMode);
    track.programPending = false;
  }

  [[nodiscard]] Effects note(u8 encoded, u16 length, u8 durationTimer, bool tiesNext, bool special) {
    if (special) {
      const u16 address = track.data.le16(static_cast<u16>(track.specialTable + encoded * 2));
      auto subtrack = std::make_shared<RuntimeTrack>(track, track.specialLogicalChannel, address);
      if (track.subtrack) {
        subtrack->state.interruptedNote = track.subtrack->state.lastNote.valid()
                                              ? track.subtrack->state.lastNote
                                              : track.subtrack->state.interruptedNote;
        subtrack->state.interruptedGainAutomation = track.subtrack->state.gainAutomation;
      }
      track.subtrack = std::move(subtrack);
      beginWait(length, durationTimer, true);
      return Effects::wait(length);
    }

    const bool noise = encoded >= 0x80;
    const double key =
        noise ? static_cast<double>(encoded & 0x1f) : static_cast<double>(static_cast<u8>(encoded + track.transpose));
    const bool continues = (track.slur || track.suppressAttack) && track.lastNote.valid();
    NotePerformanceEvent event{
        .key = key,
        .linearVelocity = 1.0,
        .durationTicks = length,
        .restartsEnvelope = !continues,
        .restartsLfoPhase = !continues,
    };
    if (track.interruptedNote.valid()) {
      static_cast<void>(out.setNoteEnd(track.interruptedNote, vm.tick()));
      track.interruptedNote = {};
    }
    if (!continues) {
      emitPendingProgram();
      if (track.gainMode) {
        static_cast<void>(beginGainNote(length));
      } else {
        stopGainMode();
      }
    }
    if (continues) {
      if (track.lastKey && *track.lastKey == key) {
        event.extendsPrevious = true;
        track.lastNote = out.note(std::move(event));
      } else {
        track.lastNote = out.continueVoice(track.lastNote, std::move(event));
      }
    } else {
      track.lastNote = out.note(std::move(event));
    }
    track.lastKey = key;
    track.suppressAttack = false;
    track.pitchDrift = 0;
    track.accumulatedDrift = 0.0;
    track.vibrato.restartNote();
    emitPitchBend();
    beginWait(length, durationTimer, track.slur || tiesNext);
    return Effects::wait(length);
  }

  [[nodiscard]] Effects pitchSlide(u8 from, u8 to, u16 length, u8 durationTimer, bool tiesNext) {
    const Effects wait = note(from, length, durationTimer, tiesNext, false);
    if (track.lastNote.valid() && from < 0x80 && to < 0x80) {
      const double start = static_cast<u8>(from + track.transpose);
      const double target = static_cast<u8>(to + track.transpose);
      static_cast<void>(out.retargetPitchSlide(track.lastNote, start, target, length));
      track.lastKey = target;
    }
    return wait;
  }

  [[nodiscard]] Effects rest(u16 length) {
    track.lastNote = {};
    track.lastKey.reset();
    track.remaining = length;
    return Effects::wait(length);
  }

  [[nodiscard]] Effects tie(u16 length, bool tiesNext) {
    track.gainEnvelope.start(track.tieGainAddress);
    if (track.gainEnvelope.active && track.lastNote.valid()) {
      out.replaceEnvelope(math::neutralGainEnvelope(), VoiceEnvelopeScope::ActiveVoices);
    }
    tickGainEnvelope();
    ++track.gainImmediateGeneration;
    return extend(length, 0, tiesNext);
  }

  [[nodiscard]] Effects extend(u16 length, u8 durationTimer, bool fullGate) {
    if (track.lastNote.valid() && track.lastKey) {
      track.lastNote = out.note(NotePerformanceEvent{
          .key = *track.lastKey,
          .linearVelocity = 1.0,
          .durationTicks = length,
          .extendsPrevious = true,
          .restartsEnvelope = false,
          .restartsLfoPhase = false,
      });
    }
    beginWait(length, durationTimer, fullGate);
    return Effects::wait(length);
  }

  void slur(bool enabled) {
    track.slur = enabled;
    if (enabled) {
      track.suppressAttack = true;
    }
    out.legatoPedal(enabled);
  }

  void volume(s8 delta, bool relative) {
    track.volume = relative ? static_cast<u8>(std::clamp<int>(track.volume + delta, 0, 255)) : static_cast<u8>(delta);
    if (track.version != Version::CosmoGang) {
      track.volumeEnvelope.stop();
    }
    emitLevel();
  }

  void pan(u8 value) {
    track.pan = std::min<u8>(value, 20);
    track.panEnvelope.stop();
    emitPan();
  }

  void volumeEnvelope(u16 address) { track.volumeEnvelope.start(address); }

  void panEnvelope(u16 address, u8 speed) { track.panEnvelope.start(address, speed); }

  void panTable(u16 address) {
    track.panTable = address;
    emitPan();
  }

  void defaultPan(bool alternate) { panTable(alternate ? track.data.alternatePanTable : track.data.defaultPanTable); }

  void vibratoDelay(u8 delay) {
    track.vibrato.delay = delay;
    track.vibrato.enabled = true;
  }

  void vibrato(u8 delay, u16 address) {
    track.vibrato.delay = delay;
    track.vibrato.address = address;
    track.vibrato.enabled = true;
  }

  void vibratoOff() {
    track.vibrato.enabled = false;
    track.vibrato.sample = 0;
    emitPitchBend();
  }

  void pitchDrift(s8 amount) {
    if (track.lastNote.valid() && track.lastKey) {
      const double current = out.currentPitchTransitionKey(track.lastNote).value_or(*track.lastKey);
      static_cast<void>(out.retargetPitchSlide(track.lastNote, *track.lastKey, current, 0));
    }
    track.pitchDrift = amount;
  }

  void specialSequence(u8 logicalChannel, u16 table) {
    track.specialSequence = true;
    track.specialLogicalChannel = logicalChannel;
    track.specialTable = table;
  }

  void release(u8 time, u8 gain) {
    track.releaseTime = time;
    track.fixedGain = static_cast<u8>(gain | 0x80);
  }

  void gainAddress(u8 phase, u16 address) {
    if (phase == 0) {
      track.keyOnGainAddress = address;
      track.gainMode = true;
    } else if (phase == 1) {
      track.sustainGainAddress = address;
    } else {
      track.tieGainAddress = address;
    }
  }

  void programChange(u8 value) {
    track.program = value;
    track.loadDefaultAdsr();
    track.programPending = true;
    track.pendingProgramEnvelopeMode = InstrumentEnvelopeMode::UseInstrumentEnvelope;
    track.gainMode = false;
    out.replaceEnvelope(driverEnvelope(track.adsr1, track.adsr2), VoiceEnvelopeScope::ActiveVoices);
  }

  void adsr(u8 adsr1, u8 adsr2) {
    track.adsr1 = adsr1;
    track.adsr2 = adsr2;
    track.gainMode = false;
    stopGainMode();
    if (track.programPending) {
      track.pendingProgramEnvelopeMode = InstrumentEnvelopeMode::PreserveDynamicOverride;
    }
    out.replaceEnvelope(driverEnvelope(adsr1, adsr2), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void defaultAdsr() {
    track.loadDefaultAdsr();
    adsr(track.adsr1, track.adsr2);
  }

  void tempo(u8 target) {
    program.tempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(target);
    out.tempo(program.tempoMicrosecondsPerQuarter);
  }

  void masterVolume(u8 value) {
    program.masterVolume = value;
    program.masterFade.rate = 0;
    emitMaster();
  }

  void masterFade(u8 target, u8 rate, bool up) {
    program.masterFade = MasterFade{.target = target, .rate = rate, .fraction = 0, .up = up};
  }

  void masterDuck(u8 value) {
    program.masterDuck = value;
    emitMaster();
  }

  void condition() { program.condition = true; }

  [[nodiscard]] Effects conditionalJump(Address destination) {
    return program.condition ? vm.jump(destination) : Effects{};
  }

  [[nodiscard]] Effects return_() { return vm.inSubroutine() ? vm.return_() : vm.fallthrough(); }

  void echoEnvelope(u16 address) { program.echoEnvelope.start(address); }

  void echoVolume(s8 left, s8 right, s8 mono) {
    program.echo.left = left;
    program.echo.right = right;
    program.echo.mono = mono;
    program.echoEnvelope.stop();
    emitEcho();
  }

  void echoEnabled(bool enabled) {
    const u8 mask = static_cast<u8>(1u << physicalChannel());
    if (enabled) {
      program.echo.voiceMask |= mask;
    } else {
      program.echo.voiceMask &= static_cast<u8>(~mask);
    }
    emitEcho();
  }

  void echoParameters(u16 delay, s8 feedback, s8 left, s8 right, s8 mono) {
    if (delay <= 0xff) {
      program.echo.delay = static_cast<u8>(delay);
    }
    program.echo.feedback = feedback;
    echoVolume(left, right, mono);
  }

  void saveEcho() {
    if (!program.savedEcho) {
      program.savedEcho = program.echo;
    }
  }

  void restoreEcho() {
    if (program.savedEcho) {
      program.echo = *program.savedEcho;
      program.savedEcho.reset();
      emitEcho();
    }
  }
};

void Playback::tickRuntimeTrack(RuntimeTrack& runtime) {
  Playback child{runtime.state, out, vm, program};
  const u32 gainGeneration = runtime.state.gainImmediateGeneration;
  child.tickGlobal();
  child.tickGainLevel();
  child.advanceWait();

  if (runtime.state.remaining == 0) {
    if (!runtime.initialized) {
      if (runtime.state.interruptedGainAutomation.valid()) {
        runtime.state.interruptedGainAutomation.interrupt(out);
        out.expression(1.0);
      }
      child.programChange(0);
      child.emitLevel();
      child.emitPan();
      out.tuning(0.0);
      runtime.initialized = true;
    }

    const auto read8 = [&] {
      const u8 value = runtime.state.data.u8At(runtime.cursor);
      runtime.cursor = static_cast<u16>(runtime.cursor + 1);
      return value;
    };
    const auto readS8 = [&] { return static_cast<s8>(read8()); };
    const auto read16 = [&] {
      const u16 value = runtime.state.data.le16(runtime.cursor);
      runtime.cursor = static_cast<u16>(runtime.cursor + 2);
      return value;
    };
    const auto readTiming = [&](bool duration = true) {
      const u8 encodedLength = runtime.defaultLength == 0 ? read8() : runtime.defaultLength;
      const u8 durationTimer = !duration ? 0
                               : runtime.manualDuration
                                   ? read8()
                                   : std::min<u8>(static_cast<u8>(encodedLength >> 1), runtime.autoDurationThreshold);
      return Timing{.length = math::ticks(encodedLength), .durationTimer = durationTimer};
    };
    const auto tieNext = [&] {
      const u8 next = runtime.state.data.u8At(runtime.cursor);
      return next == 0xee || next == 0xf4 || next == 0xf5 || next == 0xce;
    };

    while (runtime.active && runtime.state.remaining == 0 && runtime.commands++ < kCommandLimit) {
      const u8 opcode = read8();
      if (opcode < 0xa0) {
        const Timing timing = readTiming();
        static_cast<void>(
            child.note(opcode, timing.length, timing.durationTimer, tieNext(), runtime.state.specialSequence));
        break;
      }
      if (opcode < 0xc0) {
        runtime.active = false;
        break;
      }
      if (runtime.state.version == Version::CosmoGang && opcode <= 0xd0) {
        child.defaultPan(true);
        continue;
      }
      if (runtime.state.version == Version::DualOrb && opcode <= 0xc5) {
        const u16 destination = read16();
        if (program.condition) {
          runtime.cursor = destination;
        }
        continue;
      }
      if (runtime.state.version == Version::Modern && opcode <= 0xc4) {
        child.tempo(read8());
        continue;
      }

      switch (opcode) {
        case 0xc5: {
          const u16 destination = read16();
          if (program.condition) {
            runtime.cursor = destination;
          }
          break;
        }
        case 0xc6:
          child.condition();
          break;
        case 0xc7:
          child.masterVolume(read8());
          break;
        case 0xc8:
        case 0xc9: {
          const u8 target = read8();
          child.masterFade(target, read8(), opcode == 0xc9);
          break;
        }
        case 0xca:
          child.restoreEcho();
          break;
        case 0xcb:
          child.saveEcho();
          break;
        case 0xcc:
          child.masterDuck(read8());
          break;
        case 0xcd:
        case 0xce:
          child.slur(opcode == 0xce);
          break;
        case 0xcf:
          child.volumeEnvelope(read16());
          break;
        case 0xd0:
        case 0xd1:
          child.defaultPan(opcode == 0xd0);
          break;
        case 0xd2:
          runtime.state.specialSequence = false;
          break;
        case 0xd3:
        case 0xd4:
          break;
        case 0xd5:
        case 0xd6:
        case 0xd7:
          static_cast<void>(read8());
          break;
        case 0xd8:
          runtime.state.transpose = static_cast<s8>(runtime.state.transpose + readS8());
          break;
        case 0xd9: {
          const u16 address = read16();
          child.panEnvelope(address, read8());
          break;
        }
        case 0xda:
          child.panTable(read16());
          break;
        case 0xdb:
          static_cast<void>(read16());
          break;
        case 0xdc:
          runtime.defaultLength = 0;
          break;
        case 0xdd:
          runtime.defaultLength = read8();
          break;
        case 0xde:
        case 0xdf: {
          const u8 slot = opcode - 0xde;
          const u8 count = read8();
          const u16 destination = read16();
          if (runtime.repeats[slot] == 0) {
            runtime.repeats[slot] = count;
            runtime.cursor = destination;
          } else if (--runtime.repeats[slot] != 0) {
            runtime.cursor = destination;
          }
          break;
        }
        case 0xe0:
          if (runtime.returnAddress != 0) {
            runtime.cursor = std::exchange(runtime.returnAddress, 0);
          }
          break;
        case 0xe1: {
          const u16 destination = read16();
          runtime.returnAddress = runtime.cursor;
          runtime.cursor = destination;
          break;
        }
        case 0xe2:
          runtime.cursor = read16();
          break;
        case 0xe3:
          runtime.state.transpose = readS8();
          break;
        case 0xe4:
          out.tuning(math::tuningCents(read8()));
          break;
        case 0xe5:
          child.vibratoDelay(read8());
          break;
        case 0xe6:
          child.vibratoOff();
          break;
        case 0xe7: {
          const u8 delay = read8();
          child.vibrato(delay, read16());
          break;
        }
        case 0xe8:
          child.pitchDrift(readS8());
          break;
        case 0xe9: {
          const u8 from = read8();
          const u8 to = read8();
          const Timing timing = readTiming();
          static_cast<void>(child.pitchSlide(from, to, timing.length, timing.durationTimer, tieNext()));
          break;
        }
        case 0xea:
          child.volume(readS8(), true);
          break;
        case 0xeb:
          child.pan(read8());
          break;
        case 0xec:
          child.volume(static_cast<s8>(read8()), false);
          break;
        case 0xed: {
          const u8 logicalChannel = read8();
          child.specialSequence(logicalChannel, read16());
          runtime.manualDuration = false;
          runtime.autoDurationThreshold = 0;
          break;
        }
        case 0xee: {
          const Timing timing = readTiming(false);
          static_cast<void>(child.tie(timing.length, tieNext()));
          break;
        }
        case 0xef:
          child.gainAddress(2, read16());
          break;
        case 0xf0: {
          const u8 time = read8();
          child.release(time, read8());
          break;
        }
        case 0xf1:
          runtime.manualDuration = false;
          break;
        case 0xf2:
          runtime.manualDuration = true;
          break;
        case 0xf3:
          runtime.autoDurationThreshold = read8();
          runtime.manualDuration = false;
          break;
        case 0xf4: {
          const Timing timing = readTiming();
          static_cast<void>(child.extend(timing.length, timing.durationTimer, tieNext()));
          break;
        }
        case 0xf5:
          runtime.state.suppressAttack = true;
          break;
        case 0xf6:
          child.gainAddress(1, read16());
          break;
        case 0xf7:
          child.echoEnvelope(read16());
          break;
        case 0xf8: {
          const s8 left = readS8();
          const s8 right = readS8();
          child.echoVolume(left, right, readS8());
          break;
        }
        case 0xf9:
        case 0xfa:
          child.echoEnabled(opcode == 0xfa);
          break;
        case 0xfb: {
          const u16 delay = runtime.state.version == Version::CosmoGang ? read8() : 0x100;
          const s8 feedback = readS8();
          const s8 left = readS8();
          const s8 right = readS8();
          child.echoParameters(delay, feedback, left, right, readS8());
          break;
        }
        case 0xfc: {
          const u8 adsr1 = read8();
          if (adsr1 < 0x80) {
            child.defaultAdsr();
          } else {
            child.adsr(adsr1, read8());
          }
          break;
        }
        case 0xfd:
          child.gainAddress(0, read16());
          break;
        case 0xfe:
          child.programChange(read8());
          break;
        case 0xff:
          runtime.active = false;
          break;
        default:
          runtime.active = false;
          break;
      }
    }
  }

  child.tickModulation(runtime.state.gainImmediateGeneration == gainGeneration);
  if (runtime.state.subtrack && runtime.state.subtrack->active) {
    child.tickRuntimeTrack(*runtime.state.subtrack);
  }
}

using Cursor = CompilerCursor<TrackState, Playback>;

struct DecodeState {
  u8 defaultLength = 0;
  bool manualDuration = false;
  u8 autoDurationThreshold = 0;
  bool specialSequence = false;
  u8 specialLogicalChannel = 0;
  u16 specialTable = 0;
};

struct WalkState {
  u32 offset = 0;
  DecodeState decode;
  u32 returnAddress = 0;

  friend bool operator<(const WalkState& left, const WalkState& right) {
    return std::tie(left.offset, left.decode.defaultLength, left.decode.manualDuration,
                    left.decode.autoDurationThreshold, left.decode.specialSequence, left.decode.specialLogicalChannel,
                    left.decode.specialTable, left.returnAddress) <
           std::tie(right.offset, right.decode.defaultLength, right.decode.manualDuration,
                    right.decode.autoDurationThreshold, right.decode.specialSequence,
                    right.decode.specialLogicalChannel, right.decode.specialTable, right.returnAddress);
  }
};

[[nodiscard]] Timing readTiming(Cursor::Event& event, const DecodeState& state, bool readsDuration = true) {
  const u8 encodedLength = state.defaultLength == 0
                               ? event.u8("length", SemanticOperandRole::Duration)
                               : event.derived("length", state.defaultLength, SemanticOperandRole::Duration);
  u8 duration = 0;
  if (readsDuration) {
    duration = state.manualDuration
                   ? event.u8("duration_timer", SemanticOperandRole::Duration)
                   : event.derived("duration_timer",
                                   std::min<u8>(static_cast<u8>(encodedLength >> 1), state.autoDurationThreshold),
                                   SemanticOperandRole::Duration);
  }
  return Timing{.length = math::ticks(encodedLength), .durationTimer = duration};
}

[[nodiscard]] bool tieFollows(const Cursor::Event& event) {
  const u8 next = event.peekU8().value_or(0);
  return next == 0xee || next == 0xf4 || next == 0xf5 || next == 0xce;
}

[[nodiscard]] constexpr bool timingDependsOnState(u8 opcode) {
  return opcode < 0xa0 || opcode == 0xe9 || opcode == 0xee || opcode == 0xf4;
}

[[nodiscard]] constexpr bool equivalentTimingState(const DecodeState& left, const DecodeState& right) {
  return left.defaultLength == right.defaultLength && left.manualDuration == right.manualDuration &&
         (left.manualDuration || left.autoDurationThreshold == right.autoDurationThreshold) &&
         left.specialSequence == right.specialSequence &&
         (!left.specialSequence ||
          (left.specialLogicalChannel == right.specialLogicalChannel && left.specialTable == right.specialTable));
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, Version version,
                                                   const DecodeState& state, std::set<u8>* programs,
                                                   std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, "prism-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode < 0xa0) {
    auto event = cursor.command(state.specialSequence ? "Subtrack Trigger" : (opcode < 0x80 ? "Note" : "Noise Note"),
                                SequenceSemantic::Note);
    const u8 note =
        event.opcodeValue("note", opcode, opcode < 0x80 ? SourceValueDisplay::MidiNote : SourceValueDisplay::Hex,
                          SemanticOperandRole::NoteKey);
    const Timing timing = readTiming(event, state);
    return event.invoke<&Playback::note>(note, timing.length, timing.durationTimer, tieFollows(event),
                                         state.specialSequence);
  }
  if (opcode < 0xc0) {
    return cursor.unsupported("Invalid Command").stop();
  }

  if (version == Version::CosmoGang && opcode <= 0xd0) {
    return cursor.command("Alternate Pan Table", SequenceSemantic::Pan).invoke<&Playback::defaultPan>(true);
  }
  if (version == Version::DualOrb && opcode <= 0xc5) {
    auto event = cursor.command("Conditional Jump", SequenceSemantic::Jump);
    const Address destination = event.addressLe("destination", SemanticOperandRole::JumpTarget);
    return event.invoke<&Playback::conditionalJump>(destination).mayBranchTo(destination);
  }
  if (version == Version::Modern && opcode <= 0xc4) {
    auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
    return event.invoke<&Playback::tempo>(event.u8("timer_target"));
  }

  switch (opcode) {
    case 0xc5: {
      auto event = cursor.command("Conditional Jump", SequenceSemantic::Jump);
      const Address destination = event.addressLe("destination", SemanticOperandRole::JumpTarget);
      return event.invoke<&Playback::conditionalJump>(destination).mayBranchTo(destination);
    }
    case 0xc6:
      return cursor.command("Set Condition", SequenceSemantic::State).invoke<&Playback::condition>();
    case 0xc7: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::masterVolume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xc8:
    case 0xc9: {
      auto event =
          cursor.command(opcode == 0xc8 ? "Master Volume Fade Down" : "Master Volume Fade Up", SequenceSemantic::Level);
      const u8 target = event.u8("target", SemanticOperandRole::Level);
      return event.invoke<&Playback::masterFade>(target, event.u8("rate"), opcode == 0xc9);
    }
    case 0xca:
      return cursor.command("Restore Echo Parameters", SequenceSemantic::State).invoke<&Playback::restoreEcho>();
    case 0xcb:
      return cursor.command("Save Echo Parameters", SequenceSemantic::State).invoke<&Playback::saveEcho>();
    case 0xcc: {
      auto event = cursor.command("Master Volume Duck", SequenceSemantic::Level);
      return event.invoke<&Playback::masterDuck>(event.u8("amount", SemanticOperandRole::Level));
    }
    case 0xcd:
    case 0xce:
      return cursor.command(opcode == 0xce ? "Slur On" : "Slur Off", SequenceSemantic::State)
          .invoke<&Playback::slur>(opcode == 0xce);
    case 0xcf: {
      auto event = cursor.command("Volume Envelope / Tremolo", SequenceSemantic::Level);
      return event.invoke<&Playback::volumeEnvelope>(event.u16le("envelope", SourceValueDisplay::Address));
    }
    case 0xd0:
    case 0xd1:
      return cursor.command(opcode == 0xd0 ? "Alternate Pan Table" : "Default Pan Table", SequenceSemantic::Pan)
          .invoke<&Playback::defaultPan>(opcode == 0xd0);
    case 0xd2:
      return cursor.command("Subtrack Trigger Mode Off", SequenceSemantic::State)
          .set<&TrackState::specialSequence>(false);
    case 0xd3:
    case 0xd4:
      return cursor.sourceOnly(opcode == 0xd3 ? "Increment APU Port 3" : "Increment APU Port 2");
    case 0xd5:
    case 0xd6:
    case 0xd7: {
      auto event = cursor.sourceOnly("Start Song");
      static_cast<void>(event.u8("song_index"));
      return event;
    }
    case 0xd8: {
      auto event = cursor.command("Relative Transpose", SequenceSemantic::Pitch);
      return event.add<&TrackState::transpose>(event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case 0xd9: {
      auto event = cursor.command("Pan Envelope", SequenceSemantic::Pan);
      const u16 address = event.u16le("envelope", SourceValueDisplay::Address, SemanticOperandRole::Address);
      return event.invoke<&Playback::panEnvelope>(address, event.u8("speed", SemanticOperandRole::Duration));
    }
    case 0xda: {
      auto event = cursor.command("Custom Pan Table", SequenceSemantic::Pan);
      return event.invoke<&Playback::panTable>(event.u16le("table", SourceValueDisplay::Address));
    }
    case 0xdb:
      return cursor.ignored("Driver Parameters", 2, "driver-parameters");
    case 0xdc:
      return cursor.sourceOnly("Default Length Off", "default-length-off");
    case 0xdd: {
      auto event = cursor.sourceOnly("Default Length", "default-length");
      static_cast<void>(event.u8("length", SemanticOperandRole::Duration));
      return event;
    }
    case 0xde:
    case 0xdf: {
      auto event = cursor.command(opcode == 0xde ? "Repeat" : "Repeat (Alternate)", SequenceSemantic::Loop);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const Address destination = event.addressLe("destination", SemanticOperandRole::LoopTarget);
      return count == 0 ? event.declaredLoop(destination)
                        : event.repeatUntil(static_cast<u8>(opcode - 0xde), static_cast<u32>(count) + 1, destination);
    }
    case 0xe0:
      return cursor.command("Return If Called", SequenceSemantic::Return).invoke<&Playback::return_>().discoverReturn();
    case 0xe1: {
      auto event = cursor.command("Call", SequenceSemantic::Call);
      return event.call(event.addressLe("destination", SemanticOperandRole::CallTarget));
    }
    case 0xe2: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      const Address destination = event.addressLe("destination", SemanticOperandRole::JumpTarget);
      return destination.value < begin ? event.loopCandidate(destination) : event.jump(destination);
    }
    case 0xe3: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case 0xe4: {
      auto event = cursor.command("Fine Tuning", SequenceSemantic::Pitch);
      return event.emitTuning(math::tuningCents(event.u8("fraction", SemanticOperandRole::Pitch)));
    }
    case 0xe5: {
      auto event = cursor.command("Vibrato Delay", SequenceSemantic::Modulation);
      return event.invoke<&Playback::vibratoDelay>(event.u8("delay", SemanticOperandRole::Duration));
    }
    case 0xe6:
      return cursor.command("Vibrato Off", SequenceSemantic::Modulation).invoke<&Playback::vibratoOff>();
    case 0xe7: {
      auto event = cursor.command("Table Vibrato", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::vibrato>(delay, event.u16le("table", SourceValueDisplay::Address));
    }
    case 0xe8: {
      auto event = cursor.command("Pitch Drift", SequenceSemantic::Pitch);
      return event.invoke<&Playback::pitchDrift>(event.s8("step", SemanticOperandRole::Pitch));
    }
    case 0xe9: {
      auto event = cursor.command("Pitch Slide", SequenceSemantic::Pitch);
      const u8 from = event.u8("from", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      const u8 to = event.u8("to", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      const Timing timing = readTiming(event, state);
      return event.invoke<&Playback::pitchSlide>(from, to, timing.length, timing.durationTimer, tieFollows(event));
    }
    case 0xea: {
      auto event = cursor.command("Relative Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.s8("delta", SemanticOperandRole::Level), true);
    }
    case 0xeb: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan", SemanticOperandRole::Pan));
    }
    case 0xec: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(static_cast<s8>(event.u8("volume", SemanticOperandRole::Level)), false);
    }
    case 0xed: {
      auto event = cursor.command("Subtrack Trigger Mode", SequenceSemantic::State);
      const u8 logical = event.u8("logical_channel");
      return event.invoke<&Playback::specialSequence>(logical,
                                                      event.u16le("pointer_table", SourceValueDisplay::Address));
    }
    case 0xee: {
      auto event = cursor.command("Tie", SequenceSemantic::Note);
      const Timing timing = readTiming(event, state, false);
      return event.invoke<&Playback::tie>(timing.length, tieFollows(event));
    }
    case 0xef: {
      auto event = cursor.command("Tie GAIN Sequence", SequenceSemantic::Envelope);
      return event.invoke<&Playback::gainAddress>(2, event.u16le("envelope", SourceValueDisplay::Address));
    }
    case 0xf0: {
      auto event = cursor.command("Release GAIN", SequenceSemantic::Envelope);
      const u8 time = event.u8("time", SemanticOperandRole::Duration);
      return event.invoke<&Playback::release>(time, event.u8("gain", SourceValueDisplay::Hex));
    }
    case 0xf1:
      return cursor.sourceOnly("Automatic Duration", "automatic-duration");
    case 0xf2:
      return cursor.sourceOnly("Manual Duration", "manual-duration");
    case 0xf3: {
      auto event = cursor.sourceOnly("Automatic Duration Threshold", "automatic-duration-threshold");
      static_cast<void>(event.u8("threshold", SemanticOperandRole::Duration));
      return event;
    }
    case 0xf4: {
      auto event = cursor.command("Tie With Duration", SequenceSemantic::Note);
      const Timing timing = readTiming(event, state);
      return event.invoke<&Playback::extend>(timing.length, timing.durationTimer, tieFollows(event));
    }
    case 0xf5:
      return cursor.command("Suppress Next Attack", SequenceSemantic::State).set<&TrackState::suppressAttack>(true);
    case 0xf6: {
      auto event = cursor.command("Sustain GAIN Sequence", SequenceSemantic::Envelope);
      return event.invoke<&Playback::gainAddress>(1, event.u16le("envelope", SourceValueDisplay::Address));
    }
    case 0xf7: {
      auto event = cursor.command("Echo Volume Envelope", SequenceSemantic::State);
      return event.invoke<&Playback::echoEnvelope>(event.u16le("envelope", SourceValueDisplay::Address));
    }
    case 0xf8: {
      auto event = cursor.command("Echo Volume", SequenceSemantic::State);
      const s8 left = event.s8("left", SemanticOperandRole::Level);
      const s8 right = event.s8("right", SemanticOperandRole::Level);
      return event.invoke<&Playback::echoVolume>(left, right, event.s8("mono", SemanticOperandRole::Level));
    }
    case 0xf9:
    case 0xfa:
      return cursor.command(opcode == 0xfa ? "Echo On" : "Echo Off", SequenceSemantic::State)
          .invoke<&Playback::echoEnabled>(opcode == 0xfa);
    case 0xfb: {
      auto event = cursor.command("Echo Parameters", SequenceSemantic::State);
      const u16 delay = version == Version::CosmoGang ? event.u8("delay", SemanticOperandRole::Duration) : 0x100;
      const s8 feedback = event.s8("feedback", SemanticOperandRole::Level);
      const s8 left = event.s8("left", SemanticOperandRole::Level);
      const s8 right = event.s8("right", SemanticOperandRole::Level);
      return event.invoke<&Playback::echoParameters>(delay, feedback, left, right,
                                                     event.s8("mono", SemanticOperandRole::Level));
    }
    case 0xfc: {
      auto event = cursor.command("Dynamic ADSR", SequenceSemantic::Envelope);
      const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
      if (adsr1 < 0x80) {
        return event.label("Restore Instrument ADSR").invoke<&Playback::defaultAdsr>();
      }
      return event.invoke<&Playback::adsr>(adsr1, event.u8("adsr2", SourceValueDisplay::Hex));
    }
    case 0xfd: {
      auto event = cursor.command("Key-On GAIN Sequence", SequenceSemantic::Envelope);
      return event.invoke<&Playback::gainAddress>(0, event.u16le("envelope", SourceValueDisplay::Address));
    }
    case 0xfe: {
      auto event = cursor.command("Instrument", SequenceSemantic::Program);
      const u8 value = event.u8("srcn", SemanticOperandRole::InstrumentProgram);
      if (programs != nullptr) {
        programs->insert(value);
      }
      return event.invoke<&Playback::programChange>(value);
    }
    case 0xff:
      return cursor.command("End", SequenceSemantic::End).end();
    default:
      return cursor.unsupported("Unknown Command").stop();
  }
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, Version version, u32 trackNumber, u32 startAddress,
                                       u8 logicalChannel, u8 physicalChannelFlags, std::set<u8>* programs,
                                       std::vector<Diagnostic>* diagnostics, const TrackDecodeScope& scope) {
  auto session = scope.begin(trackNumber, startAddress);
  std::deque<WalkState> pending{{.offset = startAddress}};
  std::set<WalkState> visited;
  std::map<u32, DecodeState> states;

  const auto queue = [&](u32 offset, const DecodeState& state, u32 returnAddress) {
    if (reader.has(offset, 1)) {
      pending.push_back(WalkState{.offset = offset, .decode = state, .returnAddress = returnAddress});
    }
  };

  while (!pending.empty() && visited.size() < kCommandLimit) {
    WalkState walk = pending.front();
    pending.pop_front();
    if (!visited.insert(walk).second || !reader.has(walk.offset, 1)) {
      continue;
    }
    const u8 opcode = reader.u8At(walk.offset);
    const auto existing = states.find(walk.offset);
    if (existing != states.end() && timingDependsOnState(opcode) &&
        !equivalentTimingState(existing->second, walk.decode)) {
      // The bytecode occasionally jumps back into a note/tie with a new
      // default-length state. TrackProgram has one semantic command per source
      // address, so retain the first reachable interpretation; the VM's actual
      // playback path carries the correct mutable driver state.
      continue;
    }
    states.try_emplace(walk.offset, walk.decode);
    const DecodedBytecodeCommand& command =
        session.findOrAppend(decodeCommand(reader, walk.offset, version, walk.decode, programs, diagnostics),
                             walk.offset);
    const u32 continuation = static_cast<u32>(command.flow.continuation.value);
    DecodeState next = walk.decode;
    if (opcode == 0xdc) {
      next.defaultLength = 0;
    } else if (opcode == 0xdd && reader.has(walk.offset + 1, 1)) {
      next.defaultLength = reader.u8At(walk.offset + 1);
    } else if (opcode == 0xf1) {
      next.manualDuration = false;
    } else if (opcode == 0xf2) {
      next.manualDuration = true;
    } else if (opcode == 0xf3 && reader.has(walk.offset + 1, 1)) {
      next.autoDurationThreshold = reader.u8At(walk.offset + 1);
      next.manualDuration = false;
    } else if (opcode == 0xed) {
      next.specialSequence = true;
      next.manualDuration = false;
      next.autoDurationThreshold = 0;
      next.specialLogicalChannel = reader.u8At(walk.offset + 1);
      next.specialTable = reader.le16(walk.offset + 2);
    } else if (opcode == 0xd2) {
      next.specialSequence = false;
    }

    if (opcode == 0xff || (opcode >= 0xa0 && opcode < 0xc0)) {
      continue;
    }
    const bool cosmoPanAlias = version == Version::CosmoGang && opcode >= 0xc0 && opcode <= 0xd0;
    const bool dualOrbConditional = version == Version::DualOrb && opcode >= 0xc0 && opcode <= 0xc5;
    if (dualOrbConditional || (!cosmoPanAlias && opcode == 0xc5)) {
      queue(reader.le16(walk.offset + 1), next, walk.returnAddress);
      queue(continuation, next, walk.returnAddress);
    } else if (opcode == 0xde || opcode == 0xdf) {
      queue(reader.le16(walk.offset + 2), next, walk.returnAddress);
      queue(continuation, next, walk.returnAddress);
    } else if (opcode == 0xe1) {
      queue(reader.le16(walk.offset + 1), next, continuation);
    } else if (opcode == 0xe2) {
      queue(reader.le16(walk.offset + 1), next, walk.returnAddress);
    } else if (opcode == 0xe0 && walk.returnAddress != 0) {
      queue(walk.returnAddress, next, 0);
    } else {
      queue(continuation, next, walk.returnAddress);
    }
  }

  return session.finish();
}

}  // namespace

const SequenceDialect& sequenceDialect() {
  static const SequenceDialect dialect = SequenceDialect{
      .commandDetailKindPrefix = "prism-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kCommandLimit,
              .initialSourceInstrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 0},
              .initialLevel = 0.0,
              .initialMasterLevel = 1.0,
              .initialReverbSend = 0.0,
              .initialStereoBalance = StereoBalance{.leftGain = 1.0, .rightGain = 1.0},
              .initialMonoModeChannels = 0,
              .initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(0x82),
          },
  };
  return dialect;
}

TrackProgram decodeSourceTrack(ByteReader reader, Version version, u32 trackNumber, u32 startAddress, u8 logicalChannel,
                               u8 physicalChannelFlags, std::set<u8>* programs, std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope scope{.reader = reader, .maxCommands = kCommandLimit};
  return decodeTrack(reader, version, trackNumber, startAddress, logicalChannel, physicalChannelFlags, programs,
                     diagnostics, scope);
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  const u32 headerSize = static_cast<u32>(layout.tracks.size()) * 4 + 1;
  const SourceRange header = reader.range(layout.sequenceHeaderAddress, headerSize);
  std::optional<SourceAnnotationId> headerAnnotation;
  if (sourceMap != nullptr) {
    headerAnnotation = sourceMap->header("Sequence Header", header)
                           .kind("prism-snes-sequence-header")
                           .owner(ObjectRefs::sequence(sequenceId))
                           .id();
  }
  TrackDecodeScope scope{
      .reader = reader,
      .maxCommands = kCommandLimit,
      .sequenceAsset = sequenceId,
      .sourceMap = sourceMap,
  };
  SequenceProgram program = sequenceDialect().makeProgram();
  RuntimeConfig runtime{
      .version = layout.version,
      .data = RuntimeData::capture(reader, layout),
  };
  runtime.tracks.reserve(layout.tracks.size());
  std::set<u8> programs{0};
  for (u32 index = 0; index < layout.tracks.size(); ++index) {
    const TrackHeader& track = layout.tracks[index];
    runtime.tracks.push_back(RuntimeTrackConfig{
        .logicalChannel = track.logicalChannel,
        .physicalChannelFlags = track.physicalChannelFlags,
    });
    if (sourceMap != nullptr) {
      auto pointer = sourceMap
                         ->pointer("Track Pointer", reader.range(static_cast<u32>(track.range.offset) + 2, 2),
                                   SourceTarget{reader.range(track.startAddress, 1)})
                         .kind("prism-snes-track-pointer")
                         .owner(ObjectRefs::sequenceTrack(sequenceId, index))
                         .field("logical_channel", reader.range(track.range.offset, 1), track.logicalChannel)
                         .field("physical_channel_flags", reader.range(track.range.offset + 1, 1),
                                track.physicalChannelFlags, SourceValueDisplay::Hex)
                         .field("destination", reader.range(track.range.offset + 2, 2), track.startAddress,
                                SourceValueDisplay::Address);
      if (headerAnnotation) {
        pointer.parent(*headerAnnotation);
      }
    }
    program.tracks.push_back(decodeTrack(reader, layout.version, index, track.startAddress, track.logicalChannel,
                                         track.physicalChannelFlags, &programs, diagnostics, scope));
  }
  program.runtime = makeCompiledRuntime<Cursor, ProgramState>(std::move(runtime));
  return SequenceParse{
      .program = std::move(program),
      .programs = std::move(programs),
      .headerRange = header,
  };
}

}  // namespace vgmtrans::formats::prism_snes
