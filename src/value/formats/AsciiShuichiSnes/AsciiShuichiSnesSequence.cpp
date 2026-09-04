/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AsciiShuichiSnes/AsciiShuichiSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <set>
#include <utility>

namespace vgmtrans::formats::ascii_shuichi_snes {

using namespace core;

namespace {

constexpr u32 kCommandLimit = 32768;
constexpr u8 kOutputKeyOffset = 36;
constexpr std::array<u8, 31> kPanTable{
    0x80, 0x80, 0x7f, 0x7f, 0x7d, 0x7c, 0x7a, 0x78, 0x76, 0x73, 0x70, 0x6d, 0x69, 0x65, 0x61, 0x5d,
    0x58, 0x54, 0x4f, 0x49, 0x44, 0x3e, 0x39, 0x33, 0x2d, 0x27, 0x21, 0x1a, 0x13, 0x0a, 0x00,
};
constexpr std::array<std::array<s8, 8>, 2> kKnownFir{{
    {{0x7f, 0, 0, 0, 0, 0, 0, 0}},
    {{0x0c, 0x21, 0x2b, 0x2b, -0x0d, -2, -0x0d, -7}},
}};

namespace math {

[[nodiscard]] constexpr u32 counter(u8 value) { return value == 0 ? 256 : value; }

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u8 tempo) {
  return kPpqn * 500u * counter(tempo);
}

[[nodiscard]] double levelGain(u8 volume, s8 temporary = 0) {
  return static_cast<u8>(volume + temporary) / 256.0;
}

[[nodiscard]] StereoBalance panGains(u8 raw) {
  const u8 pan = std::min<u8>(raw, 30);
  return StereoBalance{
      .leftGain = kPanTable[pan] / 128.0,
      .rightGain = kPanTable[30 - pan] / 128.0,
  };
}

[[nodiscard]] double vibratoSemitones(double key, s8 tuning, s8 step, u8 width) {
  const double tunedKey = key + driverTuningCents(tuning) / 100.0;
  const double pitch = 4096.0 * std::exp2((tunedKey - 81.0) / 12.0);
  // A fresh note loads width >> 1 into a DBNZ counter. Width 0 or 1 therefore
  // means 256 initial steps rather than zero.
  const double excursion =
      std::abs(static_cast<double>(step)) * counter(static_cast<u8>(width >> 1));
  return pitch > excursion ? 12.0 * std::log2((pitch + excursion) / pitch) : 0.0;
}

}  // namespace math

struct RuntimePatch {
  u8 adsr2 = 0;
  s8 tuning = 0;
};

struct RuntimeConfig {
  std::array<RuntimePatch, kPhysicalPatchCount> patches{};
};

[[nodiscard]] RuntimeConfig runtimeConfig(ByteReader reader, const Layout& layout) {
  RuntimeConfig config;
  for (u32 program = 0; program < config.patches.size(); ++program) {
    const u32 row = instrumentRowAddress(layout, static_cast<u8>(program));
    if (!reader.has(row, 4)) {
      break;
    }
    const u8 srcn = reader.u8At(row);
    config.patches[program] = RuntimePatch{
        .adsr2 = reader.u8At(row + 2),
        .tuning = reader.has(layout.tuningTableAddress + srcn, 1)
                      ? static_cast<s8>(reader.u8At(layout.tuningTableAddress + srcn))
                      : s8{0},
    };
  }
  return config;
}

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& runtime) : config(&runtime) {}

  const RuntimeConfig* config;
  ReverbPerformanceEvent echo{.voiceMask = 0};
};

struct VolumeFade {
  bool active = false;
  u32 counter = 0;
  u32 interval = 1;
  s8 step = 0;
  u32 remaining = 0;
};

struct PanFade {
  bool active = false;
  bool oscillates = false;
  u32 counter = 0;
  u32 interval = 1;
  u8 target = 15;
  s8 direction = 0;
};

struct Vibrato {
  u8 delay = 0;
  u8 rate = 0;
  s8 step = 0;
  u8 width = 0;

  [[nodiscard]] bool active() const { return step != 0; }
};

struct RepeatFrame {
  Address start;
  Address end;
  u32 remaining = 0;
  bool initialized = false;
};

struct TrackState {
  u8 rawLength = 0;
  u8 duration = 0;
  u8 durationRate = 0;
  bool durationUsesRate = true;
  u8 volume = 0;
  s8 temporaryVolume = 0;
  std::optional<u64> temporaryVolumeTick;
  u8 pan = 15;
  s8 transpose = 0;
  s8 tuning = 0;
  u8 program = 0;
  bool slurNext = false;
  u64 noteStartTick = 0;
  u32 noteWaitTicks = 0;
  PerformanceNoteId lastNote;
  double lastKey = kOutputKeyOffset;
  Address loopPoint;
  std::array<RepeatFrame, 3> repeats{};
  u8 repeatDepth = 0;
  VolumeFade volumeFade;
  PanFade panFade;
  Vibrato vibrato;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] const RuntimePatch& patch() const {
    return program.config->patches[track.program & (kPhysicalPatchCount - 1)];
  }

  void beforeCommand() {
    if (track.temporaryVolumeTick && vm.tick() > *track.temporaryVolumeTick) {
      track.temporaryVolume = 0;
      track.temporaryVolumeTick.reset();
      emitLevel(out);
    }
  }

  void emitLevel(PerformanceEmitter output) const {
    output.level(math::levelGain(track.volume, track.temporaryVolume), ValueQuantization{.levels = 256});
  }

  void emitPan(PerformanceEmitter output) const {
    const StereoBalance gains = math::panGains(track.pan);
    output.stereoBalance(gains.leftGain, gains.rightGain);
  }

  [[nodiscard]] u32 waitTicks() const { return math::counter(track.rawLength); }

  [[nodiscard]] u32 soundingTicks() const {
    u32 gate;
    if (track.durationUsesRate) {
      gate = track.durationRate == 0
                 ? waitTicks()
                 : math::counter(static_cast<u8>((static_cast<u32>(track.rawLength) * track.durationRate) >> 8));
    } else {
      gate = math::counter(track.duration);
    }
    // The per-tick routine forces the patch GAIN two ticks before the next
    // command, except that an encoded 0 first compares as zero and releases at tick 1.
    const u32 forced = track.rawLength == 0 ? 1 : std::max<u32>(1, track.rawLength - 2u);
    return std::min(gate, forced);
  }

  [[nodiscard]] LfoPerformanceContext vibratoContext() const {
    const double period = 2.0 * math::counter(track.vibrato.width) * math::counter(track.vibrato.rate);
    return LfoPerformanceContext{
        .cyclesPerTick = 1.0 / period,
        // Both additions and counters are eight-bit in the driver. Zero is a
        // complete 256-tick DBNZ cycle, not an immediate update.
        .delayTicks = math::counter(static_cast<u8>(track.vibrato.delay + track.vibrato.rate)),
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .initialPhaseCycles = 0.0,
        .noteRestartInitialPhaseCycles = 0.0,
        .directionReversalTicks = math::counter(track.vibrato.width) * math::counter(track.vibrato.rate),
        .delayUpdateMode = LfoDelayUpdateMode::FutureNotesOnly,
        .restartMode = LfoRestartMode::PhaseAndDelay,
    };
  }

  void emitVibrato(double key, PerformanceEmitter output) const {
    const LfoPerformanceContext context = vibratoContext();
    const double depth = track.vibrato.active()
                             ? math::vibratoSemitones(key, static_cast<s8>(patch().tuning + track.tuning),
                                                     track.vibrato.step, track.vibrato.width)
                             : 0.0;
    output.vibratoDepth(depth, context);
    output.vibratoRateCyclesPerTick(context.cyclesPerTick.value_or(0.0), context);
    output.vibratoDelayTicks(context.delayTicks.value_or(0));
  }

  [[nodiscard]] Effects note(u8 sourceKey, std::optional<u8> length, bool followedByRest) {
    if (length) {
      track.rawLength = *length;
    }
    const double key = static_cast<double>(sourceKey + track.transpose);
    NotePerformanceEvent event{
        .key = key,
        .linearVelocity = 1.0,
        // A directly following rest is detected by the note handler itself;
        // it suppresses the ordinary gate and performs pseudo key-off only
        // when the rest is dispatched at the end of this delta.
        .durationTicks = followedByRest ? waitTicks() : soundingTicks(),
        .restartsEnvelope = !track.slurNext,
        .restartsLfoPhase = true,
    };
    if (track.slurNext && track.lastNote.valid()) {
      if (std::abs(track.lastKey - key) < 0.000001) {
        event.extendsPrevious = true;
        track.lastNote = out.note(std::move(event));
      } else {
        track.lastNote = out.continueVoice(track.lastNote, std::move(event));
      }
    } else {
      track.lastNote = out.note(std::move(event));
    }
    track.slurNext = false;
    track.lastKey = key;
    track.noteStartTick = vm.tick();
    track.noteWaitTicks = waitTicks();
    if (track.vibrato.active()) {
      emitVibrato(key, out);
    }
    return Effects::wait(track.noteWaitTicks);
  }

  [[nodiscard]] bool canInlineNoteControl() const { return track.lastNote.valid(); }

  void tie() {
    if (!track.lastNote.valid()) {
      return;
    }
    static_cast<void>(out.setNoteEnd(track.lastNote, track.noteStartTick + track.noteWaitTicks));
    track.slurNext = true;
  }

  [[nodiscard]] Effects rest(std::optional<u8> length) {
    if (length) {
      track.rawLength = *length;
    }
    track.lastNote = {};
    track.lastKey = kOutputKeyOffset;
    track.slurNext = false;
    return Effects::wait(waitTicks());
  }

  void releaseGain(u8 gain) {
    Envelope envelope;
    envelope.releaseSeconds = driverReleaseSeconds(patch().adsr2, gain);
    out.updateEnvelope(std::move(envelope), EnvelopeFields::Release,
                       VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void fineTuning(s8 value) {
    track.tuning = value;
    emitTrackTuning();
  }

  void emitTrackTuning() {
    const s8 combined = static_cast<s8>(patch().tuning + track.tuning);
    out.tuning(driverTuningCents(combined) - driverTuningCents(patch().tuning));
  }

  void pitchSlide(u8 delay, u8 duration, u8 sourceTarget) {
    if (!track.lastNote.valid()) {
      return;
    }
    const double target = sourceTarget + track.transpose;
    PerformanceEmitter delayed = out.at(vm.tick() + delay);
    const double start = delayed.currentPitchTransitionKey(track.lastNote).value_or(track.lastKey);
    delayed.retargetPitchSlide(track.lastNote, start, target, math::counter(duration)).preferPitchBend();
    track.lastKey = target;
  }

  void stopVolumeFade() { track.volumeFade.active = false; }
  void stopPanFade() { track.panFade.active = false; }

  void volume(u8 value) {
    stopVolumeFade();
    track.volume = value;
    emitLevel(out);
  }

  void relativeVolume(s8 value) { volume(static_cast<u8>(track.volume + value)); }

  void temporaryVolume(s8 value) {
    track.temporaryVolume = value;
    track.temporaryVolumeTick = vm.tick();
    emitLevel(out);
  }

  void pan(u8 value) {
    stopPanFade();
    track.pan = std::min<u8>(value, 30);
    emitPan(out);
  }

  void panFade(u8 intervalMinusOne, u8 delay, u8 endpoint) {
    const u8 target = std::min<u8>(endpoint >> 1, 30);
    const u8 rawInterval = static_cast<u8>(intervalMinusOne + 1);
    track.panFade = PanFade{
        .active = track.pan != target,
        .oscillates = (endpoint & 1) == 0,
        .counter = math::counter(static_cast<u8>(rawInterval + delay)),
        .interval = math::counter(rawInterval),
        .target = target,
        .direction = static_cast<s8>(target > track.pan ? 1 : -1),
    };
  }

  void volumeFade(u8 delay, u8 interval, s8 step, u8 count) {
    track.volumeFade = VolumeFade{
        .active = step != 0,
        .counter = math::counter(static_cast<u8>(delay + interval)),
        .interval = math::counter(interval),
        .step = step,
        .remaining = math::counter(count),
    };
  }

  void echoVolume(s8 left, s8 right) {
    program.echo.leftGain = std::clamp(left / 127.0, -1.0, 1.0);
    program.echo.rightGain = std::clamp(right / 127.0, -1.0, 1.0);
    program.echo.send = std::max(std::abs(*program.echo.leftGain), std::abs(*program.echo.rightGain));
    out.reverb(program.echo);
  }

  void echoParameters(u8 delay, s8 feedback, u8) {
    program.echo.delayMilliseconds = (delay & 0x0f) * 16.0;
    program.echo.feedback = feedback / 128.0;
    program.echo.filterIndex = 1;
    out.reverb(program.echo);
  }

  void echoMask(u8 mask) {
    program.echo.voiceMask = mask;
    out.reverb(program.echo);
  }

  void echoFir(s8 c0, s8 c1, s8 c2, s8 c3, s8 c4, s8 c5, s8 c6, s8 c7) {
    const std::array<s8, 8> coefficients{c0, c1, c2, c3, c4, c5, c6, c7};
    const auto preset = std::ranges::find(kKnownFir, coefficients);
    if (preset == kKnownFir.end()) {
      program.echo.filterIndex.reset();
    } else {
      program.echo.filterIndex = static_cast<u8>(preset - kKnownFir.begin());
    }
    out.reverb(program.echo);
  }

  void vibrato(Vibrato value) {
    track.vibrato = value;
    emitVibrato(track.lastKey, out);
  }

  [[nodiscard]] Effects loopForever() { return vm.declaredLoop(track.loopPoint); }

  void repeatStart(Address address) {
    if (track.repeatDepth < track.repeats.size()) {
      track.repeats[track.repeatDepth++] = RepeatFrame{.start = address};
    }
  }

  [[nodiscard]] Effects repeatEnd(u8 count, Address end) {
    if (track.repeatDepth == 0) {
      return {};
    }
    RepeatFrame& frame = track.repeats[track.repeatDepth - 1];
    frame.end = end;
    if (!frame.initialized) {
      frame.initialized = true;
      frame.remaining = math::counter(count);
    }
    if (frame.remaining > 1) {
      --frame.remaining;
      return vm.finiteBranch(frame.start);
    }
    --track.repeatDepth;
    return {};
  }

  [[nodiscard]] Effects repeatBreak() {
    if (track.repeatDepth == 0) {
      return {};
    }
    RepeatFrame& frame = track.repeats[track.repeatDepth - 1];
    if (frame.initialized && frame.remaining == 1) {
      const Address destination = frame.end;
      --track.repeatDepth;
      return vm.finiteBranch(destination);
    }
    return {};
  }

  void tick() {
    if (track.volumeFade.active && --track.volumeFade.counter == 0) {
      track.volume = static_cast<u8>(track.volume + track.volumeFade.step);
      emitLevel(out);
      if (--track.volumeFade.remaining == 0) {
        track.volumeFade.active = false;
      } else {
        track.volumeFade.counter = track.volumeFade.interval;
      }
    }
    if (track.panFade.active && --track.panFade.counter == 0) {
      track.pan = static_cast<u8>(track.pan + track.panFade.direction);
      emitPan(out);
      if (track.pan == track.panFade.target) {
        if (!track.panFade.oscillates) {
          track.panFade.active = false;
        } else {
          track.panFade.target = static_cast<u8>(30 - track.panFade.target);
          track.panFade.direction = static_cast<s8>(-track.panFade.direction);
        }
      }
      track.panFade.counter = track.panFade.interval;
    }
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

enum class Command : u8 {
  Program = 0x89,
  ReleaseGain = 0x8a,
  Tempo = 0x8b,
  Transpose = 0x8c,
  RelativeTranspose = 0x8d,
  FineTuning = 0x8e,
  PitchSlide = 0x8f,
  DurationRate = 0x90,
  VolumeAndPan = 0x92,
  Volume = 0x93,
  RelativeVolume = 0x94,
  TemporaryVolume = 0x95,
  Pan = 0x96,
  AutomaticPan = 0x98,
  VolumeFade = 0x99,
  EchoVolume = 0x9a,
  EchoParameters = 0x9b,
  EchoMask = 0x9c,
  Vibrato = 0x9d,
  VibratoOff = 0x9e,
  Rest = 0x9f,
  NoiseOn = 0xa0,
  NoiseOff = 0xa1,
  EchoFir = 0xa2,
  MuteVoiceMask = 0xa3,
  Combined = 0xa4,
  DurationTicks = 0xa5,
  WriteCpuPort = 0xa6,
  EndVoice = 0xab,
  MasterVolume = 0xac,
};

constexpr std::array<Command, 23> kEarlyCommands{
    Command::Program,       Command::Tempo,          Command::Transpose,    Command::TemporaryVolume,
    Command::FineTuning,    Command::DurationRate,   Command::MasterVolume, Command::VolumeAndPan,
    Command::Volume,        Command::Pan,            Command::AutomaticPan, Command::VolumeFade,
    Command::EchoVolume,    Command::EchoParameters, Command::EchoMask,     Command::Vibrato,
    Command::VibratoOff,    Command::PitchSlide,     Command::Rest,         Command::NoiseOn,
    Command::NoiseOff,      Command::EchoFir,        Command::EndVoice,
};

[[nodiscard]] Command semanticCommand(Version version, u8 opcode) {
  return version == Version::Early ? kEarlyCommands[opcode - 0x89] : static_cast<Command>(opcode);
}

[[nodiscard]] std::optional<u8> optionalLength(Cursor::Event& event) {
  if (event.peekU8().value_or(0xff) >= 0x80) {
    return std::nullopt;
  }
  return event.u8("length", SemanticOperandRole::Duration);
}

[[nodiscard]] u8 programOperand(Cursor::Event& event, std::set<u8>* programs) {
  const u8 program = event.u8("program", SemanticOperandRole::InstrumentProgram);
  if (programs) {
    programs->insert(program);
  }
  return program;
}

Cursor::Event& selectInstrument(Cursor::Event& event, u8 program) {
  return event.set<&TrackState::program>(program)
      .emitInstrument(std::string_view{kInstrumentDomain}, program, InstrumentEnvelopeMode::UseInstrumentEnvelope)
      .restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks)
      .invoke<&Playback::emitTrackTuning>();
}

[[nodiscard]] u8 targetNote(Cursor::Event& event, u8 noteBase) {
  const auto encoded = event.rawU8("encoded_target", SourceValueDisplay::Hex);
  return event.resolved(
      "target_note", encoded,
      [=](u8 value) { return static_cast<u8>(value - noteBase + kOutputKeyOffset); },
      SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
}

[[nodiscard]] DecodedBytecodeCommand decodeFir(Cursor& cursor) {
  auto event = cursor.command("Echo FIR", SequenceSemantic::State);
  const s8 c0 = event.s8("coefficient_0");
  const s8 c1 = event.s8("coefficient_1");
  const s8 c2 = event.s8("coefficient_2");
  const s8 c3 = event.s8("coefficient_3");
  const s8 c4 = event.s8("coefficient_4");
  const s8 c5 = event.s8("coefficient_5");
  const s8 c6 = event.s8("coefficient_6");
  return event.invoke<&Playback::echoFir>(c0, c1, c2, c3, c4, c5, c6, event.s8("coefficient_7"));
}

[[nodiscard]] DecodedBytecodeCommand decodeCommonFlow(Cursor& cursor, u8 opcode) {
  switch (opcode) {
    case 0x80:
      return cursor.command("End", SequenceSemantic::End).end();
    case 0x81: {
      auto event = cursor.command("Infinite Loop Point", SequenceSemantic::Repeat);
      return event.set<&TrackState::loopPoint>(event.nextAddress());
    }
    case 0x82:
      return cursor.command("Infinite Loop", SequenceSemantic::Jump).invokeFlow<&Playback::loopForever>();
    case 0x83: {
      auto event = cursor.command("Repeat Start", SequenceSemantic::Repeat);
      return event.invoke<&Playback::repeatStart>(event.nextAddress());
    }
    case 0x84: {
      auto event = cursor.command("Repeat End", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      return event.invokeFlow<&Playback::repeatEnd>(count, event.nextAddress());
    }
    case 0x85:
      return cursor.command("Repeat Break", SequenceSemantic::RepeatBreak).invokeFlow<&Playback::repeatBreak>();
    case 0x86: {
      auto event = cursor.command("Call", SequenceSemantic::Call);
      return event.call(event.addressLe("destination", SemanticOperandRole::CallTarget));
    }
    case 0x87:
      return cursor.command("Return", SequenceSemantic::Return).return_();
    case 0x88:
      return cursor.command("Stop Song", SequenceSemantic::End).end();
    default:
      return cursor.unsupported("Invalid Flow Command").stop();
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeSemantic(Cursor& cursor, Command command, const Layout& layout,
                                                    std::set<u8>* programs) {
  switch (command) {
    case Command::Program: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      return selectInstrument(event, programOperand(event, programs));
    }
    case Command::ReleaseGain: {
      auto event = cursor.command("Release GAIN", SequenceSemantic::Envelope);
      return event.invoke<&Playback::releaseGain>(event.u8("gain", SourceValueDisplay::Hex));
    }
    case Command::Tempo: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      return event.emitTempo(event.resolved("microseconds_per_quarter", event.rawU8("tempo"),
                                            math::tempoMicrosecondsPerQuarter));
    }
    case Command::Transpose: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case Command::RelativeTranspose: {
      auto event = cursor.command("Relative Transpose", SequenceSemantic::Pitch);
      return event.add<&TrackState::transpose>(event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case Command::FineTuning: {
      auto event = cursor.command("Fine Tuning", SequenceSemantic::Pitch);
      return event.invoke<&Playback::fineTuning>(event.s8("fraction", SemanticOperandRole::Pitch));
    }
    case Command::PitchSlide: {
      auto event = cursor.command("Pitch Slide to Note", SequenceSemantic::Pitch);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      event.invoke<&Playback::pitchSlide>(delay, duration, targetNote(event, layout.noteBase()));
      return event.duringWaitWhen<&Playback::canInlineNoteControl>();
    }
    case Command::DurationRate: {
      auto event = cursor.command("Duration Rate", SequenceSemantic::State);
      const u8 rate = event.u8("rate", SemanticOperandRole::Duration);
      return event.set<&TrackState::durationUsesRate>(true).set<&TrackState::durationRate>(rate);
    }
    case Command::VolumeAndPan: {
      auto event = cursor.command("Volume and Pan", SequenceSemantic::Level);
      const u8 volume = event.u8("volume", SemanticOperandRole::Level);
      const u8 pan = event.u8("pan", SemanticOperandRole::Pan);
      return event.invoke<&Playback::volume>(volume).invoke<&Playback::pan>(pan);
    }
    case Command::Volume: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case Command::RelativeVolume: {
      auto event = cursor.command("Relative Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::relativeVolume>(event.s8("delta", SemanticOperandRole::Level));
    }
    case Command::TemporaryVolume: {
      auto event = cursor.command("Temporary Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::temporaryVolume>(event.s8("delta", SemanticOperandRole::Level));
    }
    case Command::Pan: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan", SemanticOperandRole::Pan));
    }
    case Command::AutomaticPan: {
      auto event = cursor.command("Automatic Pan", SequenceSemantic::Pan);
      const u8 interval = event.u8("interval_minus_one", SemanticOperandRole::Duration);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::panFade>(interval, delay, event.u8("endpoint_and_mode", SemanticOperandRole::Pan));
    }
    case Command::VolumeFade: {
      auto event = cursor.command("Volume Fade", SequenceSemantic::Level);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 interval = event.u8("interval", SemanticOperandRole::Duration);
      const s8 step = event.s8("step", SemanticOperandRole::Level);
      return event.invoke<&Playback::volumeFade>(delay, interval, step,
                                                 event.u8("step_count", SemanticOperandRole::Count));
    }
    case Command::EchoVolume: {
      auto event = cursor.command("Echo Volume", SequenceSemantic::State);
      const s8 left = event.s8("left", SemanticOperandRole::Level);
      return event.invoke<&Playback::echoVolume>(left, event.s8("right", SemanticOperandRole::Level));
    }
    case Command::EchoParameters: {
      auto event = cursor.command("Echo Parameters", SequenceSemantic::State);
      const u8 delay = event.u8("delay");
      const s8 feedback = event.s8("feedback");
      return event.invoke<&Playback::echoParameters>(delay, feedback, event.u8("reserved"));
    }
    case Command::EchoMask: {
      auto event = cursor.command("Echo Voice Mask", SequenceSemantic::State);
      return event.invoke<&Playback::echoMask>(event.u8("mask", SourceValueDisplay::Hex));
    }
    case Command::Vibrato: {
      auto event = cursor.command("Vibrato", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 rate = event.u8("step_interval", SemanticOperandRole::Modulation);
      const s8 step = event.s8("pitch_step", SemanticOperandRole::Modulation);
      const u8 width = event.u8("direction_width", SemanticOperandRole::Duration);
      return event.invoke<&Playback::vibrato>(Vibrato{.delay = delay, .rate = rate, .step = step, .width = width});
    }
    case Command::VibratoOff:
      return cursor.command("Vibrato Off", SequenceSemantic::Modulation).invoke<&Playback::vibrato>(Vibrato{});
    case Command::Rest: {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      return event.invoke<&Playback::rest>(optionalLength(event));
    }
    case Command::NoiseOn:
      return cursor.sourceOnly("DSP Noise On", "noise-on");
    case Command::NoiseOff:
      return cursor.sourceOnly("DSP Noise Off", "noise-off");
    case Command::EchoFir:
      return layout.version == Version::Early || layout.hasEchoFirCommand
                 ? decodeFir(cursor)
                 : cursor.unsupported("Invalid Command").stop();
    case Command::MuteVoiceMask: {
      auto event = cursor.sourceOnly("Mute Voice Mask", "mute-voice-mask");
      static_cast<void>(event.u8("mask", SourceValueDisplay::Hex));
      return event;
    }
    case Command::Combined: {
      auto event = cursor.command("Program, Volume, Pan and Transpose", SequenceSemantic::State);
      const u8 program = programOperand(event, programs);
      const u8 volume = event.u8("volume", SemanticOperandRole::Level);
      const u8 pan = event.u8("pan", SemanticOperandRole::Pan);
      const s8 transpose = event.s8("transpose", SemanticOperandRole::Pitch);
      selectInstrument(event, program);
      return event.invoke<&Playback::volume>(volume)
          .invoke<&Playback::pan>(pan)
          .set<&TrackState::transpose>(transpose);
    }
    case Command::DurationTicks: {
      auto event = cursor.command("Duration Ticks", SequenceSemantic::State);
      const u8 ticks = event.u8("ticks", SemanticOperandRole::Duration);
      return event.set<&TrackState::durationUsesRate>(false).set<&TrackState::duration>(ticks);
    }
    case Command::WriteCpuPort: {
      auto event = cursor.sourceOnly("Write CPU Port", "cpu-port");
      static_cast<void>(event.u8("value", SourceValueDisplay::Hex));
      return event;
    }
    case Command::EndVoice:
      return cursor.command("End Voice", SequenceSemantic::End).end();
    case Command::MasterVolume: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      const u8 volume = event.u8("volume", SemanticOperandRole::Level);
      return event.emitMasterLevel(std::abs(static_cast<s8>(volume)) / 127.0);
    }
    default:
      return cursor.unsupported("Invalid Command").stop();
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, const Layout& layout, u32 begin,
                                                   std::vector<Diagnostic>* diagnostics,
                                                   std::set<u8>* programs = nullptr) {
  Cursor cursor(reader, begin, kFormatId, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode == 0xff) {
    auto event = cursor.command("Tie / Slur", SequenceSemantic::Note);
    event.invoke<&Playback::tie>();
    return event.duringWaitWhen<&Playback::canInlineNoteControl>();
  }
  if (opcode >= layout.noteBase()) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 key = event.opcodeValue("key", static_cast<u8>(opcode - layout.noteBase() + kOutputKeyOffset),
                                     SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    const std::optional<u8> length = optionalLength(event);
    const bool followedByRest = event.peekU8() == (layout.version == Version::Early ? 0x9b : 0x9f);
    return event.invoke<&Playback::note>(key, length, followedByRest);
  }
  if (opcode <= 0x88) {
    return decodeCommonFlow(cursor, opcode);
  }
  return decodeSemantic(cursor, semanticCommand(layout.version, opcode), layout, programs);
}

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config{
      .commandKindPrefix = kFormatId,
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior = SequenceProgramBehavior{
          .commandLimit = kCommandLimit,
          .initialSourceInstrument = InstrumentIdentity{.domain = kInstrumentDomain, .key = 0},
          .initialLevel = 0.0,
          .initialMasterLevel = 1.0,
          .initialReverbSend = 0.0,
          .initialStereoBalance = math::panGains(15),
          .initialMonoModeChannels = 0,
          .initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(0x14),
      },
  };
  return config;
}

}  // namespace

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  const SourceRange header = reader.range(layout.sequenceHeaderAddress, kTrackCount * 2);
  std::set<u8> programs{0};
  SequenceDecodeSession sequence{reader, sequenceConfig(), sequenceId, header, sourceMap, kCommandLimit, kAramSize};
  for (u32 track = 0; track < kTrackCount; ++track) {
    const u32 pointer = layout.sequenceHeaderAddress + track;
    sequence.addTrack(track, reader.range(pointer, 1), layout.trackAddresses[track],
                      [&](u32 offset) { return decodeCommand(reader, layout, offset, diagnostics, &programs); },
                      layout.trackAddresses[track]);
  }
  SequenceProgram program =
      sequence.finish(makeCompiledRuntime<Cursor, ProgramState>(runtimeConfig(reader, layout)));
  return SequenceParse{
      .program = std::move(program),
      .programs = std::move(programs),
      .headerRange = header,
  };
}

}  // namespace vgmtrans::formats::ascii_shuichi_snes
