/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HeartBeatSnes/HeartBeatSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceLfo.h"
#include "value/sequence/SequenceMotion.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace vgmtrans::formats::heartbeat_snes {

using namespace core;

namespace {

constexpr u32 kCommandLimit = 32768;

constexpr std::array<u8, 16> kDurationRates{
    0x23, 0x46, 0x69, 0x8c, 0xaf, 0xd2, 0xf5, 0xff, 0x19, 0x28, 0x37, 0x46, 0x55, 0x64, 0x73, 0x82,
};
constexpr std::array<u8, 16> kVelocities{
    0x19, 0x28, 0x37, 0x46, 0x55, 0x64, 0x73, 0x82, 0x91, 0xa0, 0xb0, 0xbe, 0xcd, 0xdc, 0xeb, 0xff,
};
constexpr std::array<u8, 22> kPanTable{
    0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29, 0x34, 0x42, 0x51,
    0x5e, 0x67, 0x6e, 0x73, 0x77, 0x7a, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f,
};
constexpr std::array<std::array<s8, 8>, 4> kFirPresets{{
    {{0x7f, 0, 0, 0, 0, 0, 0, 0}},
    {{0x58, -0x41, -0x25, -0x10, -2, 7, 0x0c, 0x0c}},
    {{0x0c, 0x21, 0x2b, 0x2b, 0x13, -2, -0x0d, -7}},
    {{0x34, 0x33, 0, -0x27, -0x1b, 1, -4, -0x15}},
}};

namespace math {

[[nodiscard]] double squaredGain(u8 value) {
  const double gain = value / 255.0;
  return gain * gain;
}

[[nodiscard]] double channelGain(u8 value) {
  // Master volume is initialized to C0 without a sequence event. Keep that
  // baseline in channel gain; explicit master events are relative to it.
  return squaredGain(value) * squaredGain(0xc0);
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u8 tempo) {
  if (tempo == 0) {
    return 60'000'000;
  }
  return static_cast<u32>(std::lround(kPpqn * 125.0 * 0x10 * 256.0 / tempo));
}

[[nodiscard]] u32 soundingTicks(u8 length, u8 rate) {
  return std::max<u32>((static_cast<u32>(length) * rate) >> 8, 1);
}

[[nodiscard]] double vibratoDepth(u8 depth) {
  if (depth >= 0xf1) {
    return ((depth & 0x0f) * 255.0) / 256.0;
  }
  return ((static_cast<u32>(depth) * 255u) >> 8) / 256.0;
}

[[nodiscard]] double tremoloDepth(u8 depth) {
  const u8 attenuation = static_cast<u8>((static_cast<u32>(depth) * 255u) >> 8);
  const double minimum = (255 - attenuation) / 255.0;
  return 1.0 - minimum * minimum;
}

[[nodiscard]] double tuningCents(u8 fraction) {
  return fraction * (100.0 / 256.0);
}

[[nodiscard]] StereoBalance panGains(u8 raw, bool invertLeft = false, bool invertRight = false) {
  const u8 pan = std::min<u8>(raw & 0x1f, 20);
  double left = kPanTable[20 - pan] / 127.0;
  double right = kPanTable[pan] / 127.0;
  if (invertLeft) {
    left = -left;
  }
  if (invertRight) {
    right = -right;
  }
  return {.leftGain = left, .rightGain = right};
}

[[nodiscard]] double panPosition(u8 raw) {
  const StereoBalance gains = panGains(raw);
  const double total = gains.leftGain + gains.rightGain;
  return total == 0.0 ? 0.0 : std::clamp((gains.rightGain / total) * 2.0 - 1.0, -1.0, 1.0);
}

}  // namespace math

enum class PitchEnvelopeKind : u8 {
  Off,
  To,
  From,
};

struct VibratoState {
  u8 delay = 0;
  u8 rate = 0;
  SequenceLfoDepthFadeState depthState;

  void configure(u8 newDelay, u8 newRate, u8 newDepth) {
    delay = newDelay;
    rate = newRate;
    depthState.resetDepth(newDepth);
  }

  void disable() { configure(0, 0, 0); }
};

struct TremoloConfig {
  u8 delay = 0;
  u8 rate = 0;
};

struct PitchEnvelope {
  PitchEnvelopeKind kind = PitchEnvelopeKind::Off;
  u8 delay = 0;
  u8 duration = 0;
  s8 depth = 0;
};

struct ProgramState {
  ProgramState() { masterVolume.reset(0xc0); }

  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> masterVolume;
  std::optional<u32> masterVolumeTrack;
  ReverbPerformanceEvent echo{.voiceMask = 0};
};

struct TrackState {
  explicit TrackState(const TrackProgram& sourceTrack) : trackNumber(sourceTrack.sourceTrackNumber) {
    volume.reset(0xff);
    pan.reset(10);
  }

  u32 trackNumber;
  u8 noteLength = 0x10;
  u8 durationRate = 0xaf;
  u8 velocity = 0xff;
  s8 transpose = 0;
  bool legato = false;
  bool invertLeft = false;
  bool invertRight = false;

  PerformanceNoteId lastNote;
  std::optional<double> lastKey;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> volume;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> pan;

  VibratoState vibrato;
  TremoloConfig tremolo;
  PitchEnvelope pitchEnvelope;
  u8 repeatCount = 0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] LfoPerformanceContext vibratoContext() const {
    return LfoPerformanceContext{
        .cyclesPerTick = static_cast<double>(track.vibrato.rate) / 256.0,
        .delayTicks = track.vibrato.delay,
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .initialPhaseCycles = (track.vibrato.depthState.fadeDurationTicks() & 1) != 0 ? 0.5 : 0.0,
        .sampleImmediatelyOnNote = true,
        .delayUpdateMode = LfoDelayUpdateMode::FutureNotesOnly,
        .phaseRunsAtZeroDepth = true,
    };
  }

  [[nodiscard]] LfoPerformanceContext tremoloContext() const {
    return LfoPerformanceContext{
        .cyclesPerTick = static_cast<double>(track.tremolo.rate) / 256.0,
        .delayTicks = track.tremolo.delay,
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .initialPhaseCycles = 0.25,
        .sampleImmediatelyOnNote = true,
        .delayUpdateMode = LfoDelayUpdateMode::FutureNotesOnly,
        .tremoloGainMode = TremoloGainMode::NoBoost,
    };
  }

  void emitPan(PerformanceEmitter output) const {
    const StereoBalance gains =
        math::panGains(static_cast<u8>(track.pan.currentRaw()), track.invertLeft, track.invertRight);
    output.stereoBalance(gains.leftGain, gains.rightGain);
  }

  void noteParameters(u8 value) {
    track.durationRate = kDurationRates[value >> 4];
    track.velocity = kVelocities[value & 0x0f];
  }

  void noteLength(u8 length, bool hasParameters, u8 parameters) {
    track.noteLength = length;
    if (hasParameters) {
      noteParameters(parameters);
    }
  }

  void beginPitchSlide(double start, double target, u8 delay, u8 duration) {
    if (!track.lastNote.valid() || start == target || duration == 0) {
      return;
    }
    PerformanceEmitter delayed = out.at(vm.tick() + delay);
    const double realized = delayed.currentPitchTransitionKey(track.lastNote).value_or(start);
    delayed.retargetPitchSlide(track.lastNote, realized, target, duration).preferPitchBend();
  }

  void beginPersistentPitchEnvelope(double key) {
    if (track.pitchEnvelope.kind == PitchEnvelopeKind::Off || track.pitchEnvelope.duration == 0 ||
        track.pitchEnvelope.depth == 0) {
      return;
    }
    if (track.pitchEnvelope.kind == PitchEnvelopeKind::To) {
      beginPitchSlide(key, key + track.pitchEnvelope.depth, track.pitchEnvelope.delay, track.pitchEnvelope.duration);
    } else {
      beginPitchSlide(key - track.pitchEnvelope.depth, key, track.pitchEnvelope.delay, track.pitchEnvelope.duration);
    }
  }

  void emitVibratoDepth(s32 rawDepth, PerformanceEmitter output, bool force = false) {
    track.vibrato.depthState.emitPhysicalDepth(
        math::vibratoDepth(static_cast<u8>(rawDepth)),
        [&](double depth) { output.vibratoDepth(depth, vibratoContext()); }, force);
  }

  void beginVibratoFade() {
    const s32 depth = track.vibrato.depthState.targetDepth();
    const u32 fade = track.vibrato.depthState.fadeDurationTicks();
    if (track.vibrato.rate == 0 || depth == 0 || fade == 0) {
      return;
    }
    const s32 step = depth / static_cast<s32>(fade);
    static_cast<void>(track.vibrato.depthState.restartFade(track.vibrato.delay, step));
    track.vibrato.depthState.bindFade(out.noteEnvelope(PerformanceAutomationTarget::VibratoDepth,
                                                       math::vibratoDepth(static_cast<u8>(depth)), fade,
                                                       track.vibrato.delay));
    emitVibratoDepth(step, track.vibrato.depthState.fadeOutput(out.at(vm.tick() + track.vibrato.delay)), true);
  }

  [[nodiscard]] Effects note(u8 key) {
    const double outputKey = static_cast<double>(key) + track.transpose;
    const u32 duration = math::soundingTicks(track.noteLength, track.durationRate);
    const bool continues = track.legato && track.lastNote.valid();
    NotePerformanceEvent event{
        .key = outputKey,
        .linearVelocity = math::squaredGain(track.velocity),
        .durationTicks = track.legato ? track.noteLength : duration,
        .restartsLfoPhase = true,
    };
    if (continues) {
      if (track.lastKey && *track.lastKey == outputKey) {
        event.extendsPrevious = true;
        track.lastNote = out.note(std::move(event));
      } else {
        track.lastNote = out.continueVoice(track.lastNote, std::move(event));
      }
    } else {
      track.lastNote = out.note(std::move(event));
    }
    track.lastKey = outputKey;
    beginPersistentPitchEnvelope(outputKey);
    beginVibratoFade();
    return Effects::wait(track.noteLength);
  }

  [[nodiscard]] Effects tie() {
    if (track.lastNote.valid() && track.lastKey) {
      const u32 duration = math::soundingTicks(track.noteLength, track.durationRate);
      static_cast<void>(out.setNoteEnd(track.lastNote, vm.tick()));
      track.lastNote = out.note(NotePerformanceEvent{
          .key = *track.lastKey,
          .linearVelocity = math::squaredGain(track.velocity),
          .durationTicks = duration,
          .extendsPrevious = true,
          .restartsLfoPhase = false,
      });
    }
    return Effects::wait(track.noteLength);
  }

  [[nodiscard]] Effects rest() {
    if (track.legato && track.lastNote.valid()) {
      static_cast<void>(out.setNoteEnd(track.lastNote, vm.tick() + track.noteLength));
    } else {
      track.lastNote = {};
      track.lastKey.reset();
    }
    return Effects::wait(track.noteLength);
  }

  void pan(u8 value) {
    track.pan.setCurrentAt(vm.tick(), value & 0x1f);
    emitPan(out);
  }

  void panFade(u8 length, u8 target) {
    target &= 0x1f;
    if (length == 0) {
      pan(target);
      return;
    }
    static_cast<void>(track.pan.begin(out.fade(PerformanceAutomationTarget::Pan, math::panPosition(target), length),
                                      SequenceFixedPointMotion<s32>::toRawTarget(target, length)));
  }

  void vibrato(u8 delay, u8 rate, u8 depth) {
    track.vibrato.depthState.interruptFadeAutomationAt(vm.tick());
    track.vibrato.configure(delay, rate, depth);
    const auto context = vibratoContext();
    emitVibratoDepth(depth, out, true);
    out.vibratoRateCyclesPerTick(static_cast<double>(rate) / 256.0, context);
    out.vibratoDelayTicks(delay);
  }

  void vibratoFade(u8 length) {
    track.vibrato.depthState.configureLinearFade(length);
    emitVibratoDepth(track.vibrato.depthState.targetDepth(), out, true);
  }

  void vibratoOff() {
    track.vibrato.depthState.interruptFadeAutomationAt(vm.tick());
    track.vibrato.disable();
    emitVibratoDepth(0, out, true);
    out.vibratoRateCyclesPerTick(0.0, vibratoContext());
    out.vibratoDelayTicks(0);
  }

  void tremolo(u8 delay, u8 rate, u8 depth) {
    track.tremolo = {.delay = delay, .rate = rate};
    const auto context = tremoloContext();
    out.tremoloLinearGainDepth(math::tremoloDepth(depth), context);
    out.tremoloRateCyclesPerTick(static_cast<double>(rate) / 256.0, context);
    out.tremoloDelayTicks(delay);
  }

  void tremoloOff() {
    track.tremolo = {};
    out.tremoloLinearGainDepth(0.0, tremoloContext());
    out.tremoloRateCyclesPerTick(0.0, tremoloContext());
    out.tremoloDelayTicks(0);
  }

  void volume(u8 value) {
    track.volume.setCurrentAt(vm.tick(), value);
    out.level(math::channelGain(value), ValueQuantization{.levels = 256});
  }

  void volumeFade(u8 length, u8 target) {
    if (length == 0) {
      volume(target);
      return;
    }
    static_cast<void>(
        track.volume.begin(out.fade(PerformanceAutomationTarget::Level, math::channelGain(target), length),
                           SequenceFixedPointMotion<s32>::toRawTarget(target, length)));
  }

  [[nodiscard]] static double masterRelativeGain(u8 value) {
    return math::squaredGain(value) / math::squaredGain(0xc0);
  }

  void masterVolume(u8 value) {
    program.masterVolume.setCurrentAt(vm.tick(), value);
    program.masterVolumeTrack.reset();
    out.masterLevel(masterRelativeGain(value));
  }

  void masterVolumeFade(u8 length, u8 target) {
    if (length == 0) {
      masterVolume(target);
      return;
    }
    static_cast<void>(program.masterVolume.begin(
        out.fade(PerformanceAutomationTarget::MasterLevel, masterRelativeGain(target), length),
        SequenceFixedPointMotion<s32>::toRawTarget(target, length)));
    program.masterVolumeTrack = track.trackNumber;
  }

  void pitchEnvelope(PitchEnvelopeKind kind, u8 delay, u8 duration, s8 depth) {
    track.pitchEnvelope = {.kind = kind, .delay = delay, .duration = duration, .depth = depth};
  }

  void pitchEnvelopeOff() { track.pitchEnvelope.kind = PitchEnvelopeKind::Off; }

  [[nodiscard]] bool canInlinePitchSlide() const {
    return track.lastNote.valid() &&
           (track.pitchEnvelope.kind == PitchEnvelopeKind::Off || track.pitchEnvelope.duration == 0);
  }

  void pitchSlideTo(u8 delay, u8 duration, u8 rawTarget) {
    if (!track.lastNote.valid() || !track.lastKey) {
      return;
    }
    const double target = static_cast<double>(rawTarget & 0x7f) + track.transpose;
    beginPitchSlide(*track.lastKey, target, delay, duration);
  }

  void echoVolume(s8 left, s8 right) {
    program.echo.leftGain = std::clamp(left / 127.0, -1.0, 1.0);
    program.echo.rightGain = std::clamp(right / 127.0, -1.0, 1.0);
    program.echo.send = std::min(1.0, std::max(std::abs(*program.echo.leftGain), std::abs(*program.echo.rightGain)));
    out.reverb(program.echo);
  }

  void echoParameters(u8 delay, s8 feedback, u8 filter) {
    program.echo.delayMilliseconds = (delay & 0x0f) * 16.0;
    program.echo.feedback = feedback / 128.0;
    program.echo.filterIndex = filter;
    out.reverb(program.echo);
  }

  void echoEnabled(bool enabled) {
    const u8 mask = program.echo.voiceMask.value_or(0);
    const u8 voice = static_cast<u8>(1u << std::min(track.trackNumber, u32{7}));
    program.echo.voiceMask = enabled ? static_cast<u8>(mask | voice) : static_cast<u8>(mask & ~voice);
    out.reverb(program.echo);
  }

  void echoFir(s8 c0, s8 c1, s8 c2, s8 c3, s8 c4, s8 c5, s8 c6, s8 c7) {
    const std::array<s8, 8> coefficients{c0, c1, c2, c3, c4, c5, c6, c7};
    const auto found = std::ranges::find(kFirPresets, coefficients);
    if (found == kFirPresets.end()) {
      program.echo.filterIndex.reset();
    } else {
      program.echo.filterIndex = static_cast<u8>(found - kFirPresets.begin());
    }
    out.reverb(program.echo);
  }

  void adsr(u8 adsr1, u8 adsr2) {
    out.replaceEnvelope(driverEnvelope(adsr1, adsr2), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void surround(bool left, bool right) {
    track.invertLeft = left;
    track.invertRight = right;
    emitPan(out);
  }

  void repeatCount(u8 count) {
    track.repeatCount = count;
    vm.repeatCounter(0).finish();
  }

  [[nodiscard]] Effects conditionalLoop(Address destination) {
    return vm.countedRepeatUntil(0, track.repeatCount, destination);
  }

  void tick() {
    static_cast<void>(track.volume.tickRaw([&](s32 value) {
      track.volume.output(out).level(math::channelGain(static_cast<u8>(std::clamp<s32>(value, 0, 0xff))),
                                     ValueQuantization{.levels = 256});
    }));
    static_cast<void>(track.pan.tickRaw([&](s32) { emitPan(track.pan.output(out)); }));
    const auto vibratoTick = track.vibrato.depthState.tickFade();
    if (vibratoTick.shouldApply() && vibratoTick.changed) {
      emitVibratoDepth(track.vibrato.depthState.currentDepth(), track.vibrato.depthState.fadeOutput(out));
    }
    if (program.masterVolumeTrack == track.trackNumber) {
      static_cast<void>(program.masterVolume.tickRaw([&](s32 value) {
        const auto volume = static_cast<u8>(std::clamp<s32>(value, 0, 0xff));
        program.masterVolume.output(out).masterLevel(masterRelativeGain(volume));
      }));
      if (!program.masterVolume.active()) {
        program.masterVolumeTrack.reset();
      }
    }
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] Address readRelativeTarget(Cursor::Event& event, u32 sequenceBase, SemanticOperandRole role) {
  const u16 relative = event.u16le("relative", SourceValueDisplay::Address, role);
  const Address destination{static_cast<u16>(sequenceBase + relative)};
  event.derived("destination", destination, SourceValueDisplay::Address, role);
  return destination;
}

[[nodiscard]] DecodedBytecodeCommand decodeExtendedCommand(Cursor& cursor, u32 sequenceBase) {
  auto event = cursor.command("Extended Command", SequenceSemantic::State);
  const u8 subcommand = event.u8("subcommand", SourceValueDisplay::Hex);
  switch (subcommand) {
    case 0x00:
      event.label("Repeat Count");
      return event.invoke<&Playback::repeatCount>(event.u8("count", SemanticOperandRole::Count));
    case 0x01: {
      event.label("Repeat End");
      const Address destination = readRelativeTarget(event, sequenceBase, SemanticOperandRole::RepeatTarget);
      return event.invoke<&Playback::conditionalLoop>(destination).mayBranchTo(destination).runtimeControlFlow();
    }
    case 0x02:
      return event.label("No Operation").ignore();
    case 0x03:
      event.label("Attack Rate");
      return event.emitEnvelopeField<EnvelopeFields::Attack>(snesDspAdsrAttackSeconds(event.u8("rate") & 0x0f));
    case 0x04:
      event.label("Decay Rate");
      return event.emitEnvelopeField<EnvelopeFields::Decay>(snesDspAdsrDecaySeconds(event.u8("rate") & 0x07));
    case 0x05:
      event.label("Sustain Level");
      return event.emitEnvelopeField<EnvelopeFields::Sustain>(((event.u8("level") & 0x07) + 1) / 8.0);
    case 0x06:
      event.label("Held Sustain Rate");
      return event.emitEnvelopeField<EnvelopeFields::SecondDecay>(snesDspAdsrSustainSeconds(event.u8("rate") & 0x1f));
    case 0x07:
      event.label("Gate Release Rate");
      return event.emitEnvelopeField<EnvelopeFields::Release>(snesDspAdsrSustainSeconds(event.u8("rate") & 0x1f));
    case 0x09: {
      event.label("Surround Phase");
      const bool left = event.u8("invert_left") != 0;
      return event.invoke<&Playback::surround>(left, event.u8("invert_right") != 0);
    }
    case 0x08:
    case 0x0a:
      return event.label("Invalid Extended Command").stop();
    default:
      return event.label("Unknown Extended Command").ignore();
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, Version version, u32 sequenceBase,
                                                   std::vector<Diagnostic>* diagnostics,
                                                   std::set<u8>* programs = nullptr) {
  Cursor cursor(reader, begin, "heartbeat-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode >= 0x01 && opcode <= 0x7f) {
    auto event = cursor.command("Note Length", SequenceSemantic::State);
    const u8 length = event.opcodeValue("length", opcode, SourceValueDisplay::Default, SemanticOperandRole::Duration);
    const bool hasParameters = event.peekU8().value_or(0xff) < 0x80;
    const u8 parameters = hasParameters ? event.u8("note_parameters", SourceValueDisplay::Hex) : 0;
    return event.invoke<&Playback::noteLength>(length, hasParameters, parameters);
  }
  if (opcode >= 0x80 && opcode <= 0xcf) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 key = event.opcodeValue("key", static_cast<u8>(opcode & 0x7f), SourceValueDisplay::MidiNote,
                                     SemanticOperandRole::NoteKey);
    return event.invoke<&Playback::note>(key);
  }

  switch (opcode) {
    case 0x00:
      return cursor.command("End", SequenceSemantic::End).end();
    case 0xd0:
      return cursor.command("Tie", SequenceSemantic::Note).invoke<&Playback::tie>();
    case 0xd1:
      return cursor.command("Rest", SequenceSemantic::Rest).invoke<&Playback::rest>();
    case 0xd2:
    case 0xd3: {
      const bool enabled = opcode == 0xd2;
      return cursor.command(enabled ? "Legato On" : "Legato Off", SequenceSemantic::State)
          .set<&TrackState::legato>(enabled)
          .emitLegatoPedal(enabled);
    }
    case 0xd4: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      const u8 program = event.u8("program", SemanticOperandRole::InstrumentProgram);
      if (programs != nullptr) {
        programs->insert(program);
      }
      return event.emitInstrument(kInstrumentDomain, program, InstrumentEnvelopeMode::UseInstrumentEnvelope)
          .restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
    case 0xd5:
      return cursor.ignored("Reserved", 7, "reserved");
    case 0xd6: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan", SemanticOperandRole::Pan));
    }
    case 0xd7: {
      auto event = cursor.command("Pan Fade", SequenceSemantic::Pan);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      return event.invoke<&Playback::panFade>(length, event.u8("target", SemanticOperandRole::Pan));
    }
    case 0xd8: {
      auto event = cursor.command("Vibrato On", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::vibrato>(delay, rate, event.u8("depth", SemanticOperandRole::Modulation));
    }
    case 0xd9: {
      auto event = cursor.command("Vibrato Fade", SequenceSemantic::Modulation);
      return event.invoke<&Playback::vibratoFade>(event.u8("length", SemanticOperandRole::Duration));
    }
    case 0xda:
      return cursor.command("Vibrato Off", SequenceSemantic::Modulation).invoke<&Playback::vibratoOff>();
    case 0xdb: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::masterVolume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xdc: {
      auto event = cursor.command("Master Volume Fade", SequenceSemantic::Level);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      return event.invoke<&Playback::masterVolumeFade>(length, event.u8("target", SemanticOperandRole::Level));
    }
    case 0xdd: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const auto raw = event.rawU8("raw");
      const u32 tempo = event.resolved("microseconds_per_quarter", raw, math::tempoMicrosecondsPerQuarter);
      return event.emitTempo(tempo);
    }
    case 0xde:
      if (version == Version::DragonQuest3) {
        return cursor.sourceOnly("DSP Pitch Modulation On", "pitch-modulation-on").ignore();
      }
      return cursor.ignored("Reserved", 2, "reserved");
    case 0xdf: {
      auto event = cursor.command("Global Transpose", SequenceSemantic::Pitch);
      return event.emitGlobalTranspose(event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case 0xe0: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case 0xe1: {
      auto event = cursor.command("Tremolo On", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::tremolo>(delay, rate, event.u8("depth", SemanticOperandRole::Modulation));
    }
    case 0xe2:
      return cursor.command("Tremolo Off", SequenceSemantic::Modulation).invoke<&Playback::tremoloOff>();
    case 0xe3: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xe4: {
      auto event = cursor.command("Volume Fade", SequenceSemantic::Level);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      return event.invoke<&Playback::volumeFade>(length, event.u8("target", SemanticOperandRole::Level));
    }
    case 0xe5: {
      auto event = cursor.command("Pitch Slide To Note", SequenceSemantic::Pitch);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      event.invoke<&Playback::pitchSlideTo>(
          delay, duration, event.u8("target_note", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey));
      return event.duringWaitWhen<&Playback::canInlinePitchSlide>();
    }
    case 0xe6:
    case 0xe7: {
      auto event =
          cursor.command(opcode == 0xe6 ? "Pitch Envelope To" : "Pitch Envelope From", SequenceSemantic::Pitch);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const s8 depth = event.s8("semitones", SemanticOperandRole::Pitch);
      return event.invoke<&Playback::pitchEnvelope>(opcode == 0xe6 ? PitchEnvelopeKind::To : PitchEnvelopeKind::From,
                                                    delay, duration, depth);
    }
    case 0xe8:
      return cursor.command("Pitch Envelope Off", SequenceSemantic::Pitch).invoke<&Playback::pitchEnvelopeOff>();
    case 0xe9: {
      auto event = cursor.command("Fine Tuning", SequenceSemantic::Pitch);
      return event.emitTuning(event.resolved("cents", event.rawU8("fraction"), math::tuningCents));
    }
    case 0xea: {
      auto event = cursor.command("Echo Volume", SequenceSemantic::State);
      const s8 left = event.s8("left", SemanticOperandRole::Level);
      return event.invoke<&Playback::echoVolume>(left, event.s8("right", SemanticOperandRole::Level));
    }
    case 0xeb: {
      auto event = cursor.command("Echo Parameters", SequenceSemantic::State);
      const u8 delay = event.u8("delay");
      const s8 feedback = event.s8("feedback");
      return event.invoke<&Playback::echoParameters>(delay, feedback, event.u8("fir_preset"));
    }
    case 0xec:
      if (version == Version::DragonQuest3) {
        return cursor.sourceOnly("DSP Pitch Modulation Off", "pitch-modulation-off").ignore();
      }
      return cursor.ignored("Reserved", 3, "reserved");
    case 0xed:
    case 0xee:
      return cursor.command(opcode == 0xee ? "Echo On" : "Echo Off", SequenceSemantic::State)
          .invoke<&Playback::echoEnabled>(opcode == 0xee);
    case 0xef: {
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
    case 0xf0: {
      auto event = cursor.command("ADSR", SequenceSemantic::Envelope);
      const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
      return event.invoke<&Playback::adsr>(adsr1, event.u8("adsr2", SourceValueDisplay::Hex));
    }
    case 0xf1: {
      auto event = cursor.command("Note Parameters", SequenceSemantic::State);
      return event.invoke<&Playback::noteParameters>(event.u8("parameters", SourceValueDisplay::Hex));
    }
    case 0xf2:
    case 0xf3: {
      auto event = cursor.command(opcode == 0xf2 ? "Jump" : "Call",
                                  opcode == 0xf2 ? SequenceSemantic::Jump : SequenceSemantic::Call);
      const Address destination = readRelativeTarget(
          event, sequenceBase, opcode == 0xf2 ? SemanticOperandRole::JumpTarget : SemanticOperandRole::CallTarget);
      return opcode == 0xf2 ? event.loopCandidate(destination) : event.call(destination);
    }
    case 0xf4:
      return cursor.command("Return", SequenceSemantic::Return).return_();
    case 0xf5:
    case 0xf6:
      return cursor.sourceOnly(opcode == 0xf5 ? "DSP Noise On" : "DSP Noise Off", "noise").ignore();
    case 0xf7: {
      auto event = cursor.sourceOnly("DSP Noise Frequency", "noise-frequency");
      static_cast<void>(event.u8("clock", SourceValueDisplay::Hex));
      return event.ignore();
    }
    case 0xf8:
      return cursor.sourceOnly("Note-Keyed DSP Noise On", "keyed-noise").ignore();
    case 0xf9:
      return decodeExtendedCommand(cursor, sequenceBase);
    default:
      return cursor.unsupported("Invalid Command").stop();
  }
}

}  // namespace

const SequenceDialect& sequenceDialect() {
  static const SequenceDialect dialect = SequenceDialect{
      .commandDetailKindPrefix = "heartbeat-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kCommandLimit,
              .initialSourceInstrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 0},
              .initialLevel = math::squaredGain(0xc0),
              .initialReverbSend = 0.0,
              .initialStereoBalance = math::panGains(10),
              .initialMonoModeChannels = 0,
              .initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(0x10),
          },
  };
  return dialect;
}

SequenceRuntime sequenceRuntime() {
  return makeCompiledRuntime<TrackState, Playback, ProgramState>();
}

TrackProgram decodeSourceTrack(ByteReader reader, Version version, u32 trackNumber, u32 startAddress, u32 sequenceBase,
                               std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope tracks{.reader = reader, .maxCommands = kCommandLimit};
  return tracks.reachable(trackNumber, startAddress, [&](u32 offset) {
    return decodeCommand(reader, offset, version, sequenceBase, diagnostics);
  });
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  const u32 headerSize = 2 + layout.trackCount * 2 + (layout.trackCount < kTrackCount ? 2 : 0);
  const SourceRange header = reader.range(layout.sequenceHeaderAddress, headerSize);
  std::set<u8> programs{0};
  SequenceDecodeSession sequence{reader, sequenceDialect(), sequenceId, header, sourceMap, kCommandLimit, kAramSize};
  for (u32 track = 0; track < layout.trackCount; ++track) {
    const u32 pointer = layout.sequenceHeaderAddress + 2 + track * 2;
    const u16 relative = reader.le16(pointer);
    sequence.addReachableTrack(
        track, reader.range(pointer, 2), static_cast<u16>(layout.sequenceHeaderAddress + relative),
        [&](u32 offset) {
          return decodeCommand(reader, offset, layout.version, layout.sequenceHeaderAddress, diagnostics, &programs);
        },
        relative);
  }
  SequenceProgram program = sequence.finish(sequenceRuntime());
  program.sourceBaseAddress = Address{layout.sequenceHeaderAddress};
  return SequenceParse{
      .program = std::move(program),
      .programs = std::move(programs),
      .headerRange = header,
  };
}

}  // namespace vgmtrans::formats::heartbeat_snes
