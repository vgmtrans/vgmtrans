/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NamcoSnes/NamcoSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace vgmtrans::formats::namco_snes {

using namespace core;

namespace {

constexpr std::string_view kFormatId = "namco-snes";
constexpr u8 kRest = 0x54;
constexpr PitchBendLayerId kPitchTableBendLayer{1};
namespace math {

[[nodiscard]] constexpr u8 voiceBit(u32 track) { return static_cast<u8>(0x80u >> track); }

[[nodiscard]] u8 dspVoiceMask(u8 sequenceMask) {
  u8 result = 0;
  for (u8 voice = 0; voice < 8; ++voice) {
    if ((sequenceMask & voiceBit(voice)) != 0) {
      result |= static_cast<u8>(1u << voice);
    }
  }
  return result;
}

[[nodiscard]] constexpr u32 waitTicks(u8 delta, u8 multiplier) {
  return static_cast<u32>(delta) * multiplier;
}

[[nodiscard]] double tuningCents(u8 fraction) { return fraction * (100.0 / 256.0); }

[[nodiscard]] constexpr double signedDspGain(s8 value) { return value / 128.0; }

[[nodiscard]] double modulationScale(Version version, u8 depth) {
  if (version != Version::BlueCrystalRod && depth == 0) {
    return 1.0;
  }
  return depth / 256.0;
}

}  // namespace math

struct MaskedValues {
  u8 mask = 0;
  std::array<u8, kTrackCount> values{};
};

enum Parameter : u8 {
  kSrcn,
  kVolume,
  kBalance,
  kGate,
  kPitchTable,
  kTranspose,
  kFineTuning,
  kPortamento,
  kPitchRate,
  kPitchDepth,
  kEnvelope,
  kParameterCount,
};

struct ParameterCommand {
  std::string_view name;
  SequenceSemantic semantic;
  SemanticOperandRole role;
};

constexpr std::array<ParameterCommand, kParameterCount> kParameterCommands{{
    {"Instrument", SequenceSemantic::Program, SemanticOperandRole::Instrument},
    {"Voice Volume", SequenceSemantic::Level, SemanticOperandRole::Level},
    {"Stereo Balance", SequenceSemantic::Pan, SemanticOperandRole::Pan},
    {"Gate Time", SequenceSemantic::State, SemanticOperandRole::Duration},
    {"Pitch Modulation Table", SequenceSemantic::Modulation, SemanticOperandRole::Modulation},
    {"Transpose", SequenceSemantic::Pitch, SemanticOperandRole::Pitch},
    {"Fine Tuning", SequenceSemantic::Pitch, SemanticOperandRole::Pitch},
    {"Portamento Speed", SequenceSemantic::Portamento, SemanticOperandRole::Duration},
    {"Pitch Table Rate", SequenceSemantic::Modulation, SemanticOperandRole::Modulation},
    {"Pitch Table Depth", SequenceSemantic::Modulation, SemanticOperandRole::Modulation},
    {"Envelope Preset", SequenceSemantic::Envelope, SemanticOperandRole::Value},
}};

struct DriverData {
  RetainedSource source;
  Layout layout;
};

struct ProgramState {
  explicit ProgramState(const DriverData& data) : data(data) {}

  DriverData data;
  ReverbPerformanceEvent echo{.voiceMask = 0};
  bool echoEnabled = false;
};

enum class VoiceSource : u8 {
  Melodic,
  Percussion,
  Noise,
};

struct VoiceInstrument {
  VoiceSource source = VoiceSource::Melodic;
  u8 srcn = 0;

  friend bool operator==(const VoiceInstrument&, const VoiceInstrument&) noexcept = default;
};

struct TrackState {
  explicit TrackState(const TrackProgram& source) : number(source.sourceTrackNumber) { resetVoice(); }

  void resetVoice() {
    controls = {0, 0x88, 0x88, 0, 0, 0, 0, 0, 0, 0, 0};
    voiceControls = controls;
    triggerDelay = 0;
    pendingNote.reset();
    triggerTicks = 0;
    gateTicks = 0;
    lastNote = {};
    lastSourceNote = kRest;
    haveDriverPitch = false;
    driverPitch = 0.0;
    pitchTable = 0;
    pitchPosition = 0;
    pitchPhase = 0;
    pitchBend.reset();
    portamentoActive = false;
  }

  [[nodiscard]] bool selected(u8 mask) const { return (mask & math::voiceBit(number)) != 0; }
  [[nodiscard]] bool active() const { return selected(activeMask); }

  u32 number;
  u8 delta = 1;
  u8 multiplier = 1;
  u8 activeMask = 0;
  // $0360 is command-facing state; $0200 is copied from it only on a fresh
  // attack. Keeping both prevents a mid-note command from changing live DSP state.
  std::array<u8, kParameterCount> controls{};
  std::array<u8, kParameterCount> voiceControls{};
  u8 triggerDelay = 0;
  bool slur = false;

  std::optional<u8> pendingNote;
  u16 triggerTicks = 0;
  u16 gateTicks = 0;
  PerformanceNoteId lastNote;
  u8 lastSourceNote = kRest;
  VoiceInstrument emittedInstrument;

  bool haveDriverPitch = false;
  double driverPitch = 0.0;
  bool portamentoActive = false;
  double portamentoTarget = 0.0;
  double portamentoStep = 0.0;

  u16 pitchTable = 0;
  u8 pitchPosition = 0;
  u8 pitchPhase = 0;
  std::optional<double> pitchBend;
  u8 pitchBendRange = 48;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] ByteReader reader() const { return program.data.source.reader(); }
  [[nodiscard]] const Layout& layout() const { return program.data.layout; }

  void closeVoice(u64 tick) {
    if (track.lastNote.valid()) {
      static_cast<void>(out.setNoteEnd(track.lastNote, tick));
    }
    track.lastNote = {};
  }

  void selectInstrument(VoiceInstrument instrument) {
    if (track.emittedInstrument == instrument) {
      return;
    }
    track.emittedInstrument = instrument;
    switch (instrument.source) {
      case VoiceSource::Melodic:
        out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = instrument.srcn},
                       InstrumentEnvelopeMode::PreserveDynamicOverride);
        break;
      case VoiceSource::Percussion:
        out.instrument(127, 0, InstrumentEnvelopeMode::PreserveDynamicOverride);
        break;
      case VoiceSource::Noise:
        out.instrument(0, 126, InstrumentEnvelopeMode::PreserveDynamicOverride);
        break;
    }
  }

  void delta(u8 value) { track.delta = value; }
  void multiplier(u8 value) { track.multiplier = value; }

  void activeVoices(u8 mask) {
    const bool wasActive = track.active();
    track.activeMask = mask;
    const bool isActive = track.active();
    if (wasActive && !isActive) {
      closeVoice(vm.tick());
      track.pendingNote.reset();
    } else if (!wasActive && isActive) {
      track.resetVoice();
      track.activeMask = mask;
    }
    if (track.number == 0 && program.echoEnabled) {
      program.echo.voiceMask = math::dspVoiceMask(mask);
      out.reverb(program.echo);
    }
  }

  void control(u8 index, MaskedValues values) {
    if (index < track.controls.size() && track.selected(values.mask)) {
      track.controls[index] = values.values[track.number];
    }
  }

  void noteDelay(MaskedValues values) {
    if (track.selected(values.mask)) {
      track.triggerDelay = values.values[track.number];
    }
  }

  void slur(u8 mask) { track.slur = track.selected(mask); }

  void emitMix(u8 volume, u8 balance) {
    out.level(volume / 256.0, ValueQuantization{.levels = 256});
    const double left = (balance >> 4) / 16.0;
    const double right = (balance & 0x0f) / 16.0;
    if (layout().mono) {
      const double mono = (left + right) / 2.0;
      out.stereoBalance(mono, mono);
    } else {
      out.stereoBalance(left, right);
    }
  }

  void emitPitchBend(double semitones) {
    const u8 requiredRange =
        static_cast<u8>(std::clamp<int>(static_cast<int>(std::ceil(std::abs(semitones))), 2, 127));
    if (requiredRange > track.pitchBendRange) {
      track.pitchBendRange = requiredRange;
      out.pitchBendRange(requiredRange);
    }
    if (!track.pitchBend || std::abs(*track.pitchBend - semitones) > 0.0001) {
      out.pitchBend(semitones, kPitchTableBendLayer);
      track.pitchBend = semitones;
    }
  }

  [[nodiscard]] u8 resolvePitchPosition(u16 table, u8 position, u8 fallback) const {
    // F0 holds the preceding point. Every other F1-FF marker jumps to the
    // following byte offset, which permits both periodic vibrato and one-shot curves.
    for (u32 hops = 0; hops < 16; ++hops) {
      if (!reader().has(table + position, 1)) {
        return fallback;
      }
      const u8 value = reader().u8At(table + position);
      if (value < 0xf0) {
        return position;
      }
      if (value == 0xf0 || !reader().has(table + position + 1u, 1)) {
        return fallback;
      }
      position = reader().u8At(table + position + 1u);
    }
    return fallback;
  }

  [[nodiscard]] double modulationValue() const {
    if (track.pitchTable == 0 || !reader().has(track.pitchTable + track.pitchPosition, 1)) {
      return 0.0;
    }
    const u8 nextPosition = resolvePitchPosition(track.pitchTable, static_cast<u8>(track.pitchPosition + 1),
                                                 track.pitchPosition);
    const double current = static_cast<int>(reader().u8At(track.pitchTable + track.pitchPosition)) - 0x64;
    const double next = static_cast<int>(reader().u8At(track.pitchTable + nextPosition)) - 0x64;
    const double interpolated = current + (next - current) * (track.pitchPhase / 256.0);
    return interpolated * math::modulationScale(layout().version, track.voiceControls[kPitchDepth]);
  }

  void beginPitchModulation() {
    const u8 index = track.voiceControls[kPitchTable];
    if (index == 0) {
      track.pitchTable = 0;
      emitPitchBend(0.0);
      return;
    }
    const u16 pointers = layout().pitchPointerTable(reader());
    const u32 entry = pointers + index * 2u;
    if (!reader().has(entry, 2) || !reader().has(reader().le16(entry), 1)) {
      track.pitchTable = 0;
      emitPitchBend(0.0);
      return;
    }
    track.pitchTable = reader().le16(entry);
    track.pitchPosition = resolvePitchPosition(track.pitchTable, 0, 0);
    track.pitchPhase = 0;
    emitPitchBend(modulationValue());
  }

  void beginAttack(VoiceInstrument instrument) {
    closeVoice(vm.tick());
    selectInstrument(instrument);
    out.replaceEnvelope(driverEnvelope(reader(), layout(), track.voiceControls[kEnvelope]),
                        VoiceEnvelopeScope::FutureAttacks);
    emitMix(track.voiceControls[kVolume], track.voiceControls[kBalance]);
    beginPitchModulation();
  }

  [[nodiscard]] double targetPitch(u8 sourceNote) const {
    const u8 coarse = static_cast<u8>(sourceNote + static_cast<s8>(track.voiceControls[kTranspose]));
    return coarse + track.voiceControls[kFineTuning] / 256.0;
  }

  void beginPortamento(double target, double outputKey, PerformanceNoteId previous, bool continues) {
    const u8 speed = track.voiceControls[kPortamento];
    if (speed == 0 || track.lastSourceNote == kRest || !track.haveDriverPitch) {
      track.driverPitch = target;
      track.haveDriverPitch = true;
      track.portamentoActive = false;
      return;
    }
    const double distance = std::abs(target - track.driverPitch);
    const double step = (std::floor(distance) + 1.0) * speed / 512.0;
    if (step <= 0.0 || distance <= 0.0) {
      track.driverPitch = target;
      track.portamentoActive = false;
      return;
    }
    const u32 duration = static_cast<u32>(std::ceil(distance / step));
    auto slide = out.pitchSlide(track.lastNote, outputKey + track.driverPitch - target, outputKey, duration);
    if (continues) {
      slide.continueFrom(previous);
    }
    slide.preferPortamento();
    track.portamentoTarget = target;
    track.portamentoStep = step;
    track.portamentoActive = true;
  }

  void startNote(u8 sourceNote, std::optional<u8> percussionIndex = std::nullopt, std::optional<u8> srcnOverride = {},
                 std::optional<u8> envelopeOverride = {}, std::optional<u8> volumeOverride = {},
                 std::optional<u8> balanceOverride = {}) {
    if (!track.slur) {
      track.voiceControls = track.controls;
      if (srcnOverride) {
        track.voiceControls[kSrcn] = *srcnOverride;
      }
      if (envelopeOverride) {
        track.voiceControls[kEnvelope] = *envelopeOverride;
      }
      if (volumeOverride) {
        track.voiceControls[kVolume] = *volumeOverride;
      }
      if (balanceOverride) {
        track.voiceControls[kBalance] = *balanceOverride;
      }
    }
    const u8 srcn = track.voiceControls[kSrcn];
    const s8 transpose = static_cast<s8>(track.voiceControls[kTranspose]);
    const u8 coarse = static_cast<u8>(sourceNote + transpose);
    const double fine = math::tuningCents(track.voiceControls[kFineTuning]);
    const double outputKey = percussionIndex ? *percussionIndex : std::min<u8>(coarse, 127);
    // The drum region maps its sequence key to the table's source note. Only
    // live pitch controls remain to be applied by the sequence.
    const double tuning = percussionIndex ? transpose * 100.0 + fine : fine;
    const double target = targetPitch(sourceNote);

    const PerformanceNoteId previous = track.lastNote;
    const bool continues = track.slur && previous.valid();
    if (track.slur && !previous.valid()) {
      track.driverPitch = target;
      track.haveDriverPitch = true;
      track.lastSourceNote = sourceNote;
      return;
    }
    if (!continues) {
      beginAttack(percussionIndex ? VoiceInstrument{.source = VoiceSource::Percussion}
                                  : VoiceInstrument{.source = VoiceSource::Melodic, .srcn = srcn});
    }
    out.tuning(tuning);

    NotePerformanceEvent event{
        .key = outputKey,
        .linearVelocity = 1.0,
        .durationTicks = std::numeric_limits<u32>::max(),
        .restartsEnvelope = !continues,
        .restartsLfoPhase = !continues,
    };
    track.lastNote = continues ? out.continueVoice(previous, std::move(event)) : out.note(std::move(event));
    beginPortamento(target, outputKey, previous, continues);
    track.lastSourceNote = sourceNote;
    track.gateTicks = 0;
  }

  void startNoise(u8 raw) {
    const PerformanceNoteId previous = track.lastNote;
    const bool continues = track.slur && previous.valid();
    if (track.slur && !previous.valid()) {
      return;
    }
    if (!continues) {
      track.voiceControls = track.controls;
      beginAttack(VoiceInstrument{.source = VoiceSource::Noise});
    }
    out.tuning(0.0);
    const double key = std::min<int>(35 + (raw & 0x1f), 127);
    NotePerformanceEvent event{.key = key,
                               .linearVelocity = 1.0,
                               .durationTicks = std::numeric_limits<u32>::max(),
                               .restartsEnvelope = !continues,
                               .restartsLfoPhase = !continues};
    track.lastNote = continues ? out.continueVoice(previous, std::move(event)) : out.note(std::move(event));
    track.lastSourceNote = raw;
    track.haveDriverPitch = false;
    track.portamentoActive = false;
    track.gateTicks = 0;
  }

  void trigger(u8 raw) {
    track.pendingNote.reset();
    track.triggerTicks = 0;
    if (!track.active()) {
      return;
    }
    if (raw == kRest) {
      closeVoice(vm.tick());
      track.lastSourceNote = raw;
      track.portamentoActive = false;
      return;
    }
    if (raw < kRest) {
      startNote(raw);
      return;
    }
    if (raw < 0x80) {
      startNoise(raw);
      return;
    }

    const u8 index = raw & 0x7f;
    const u32 row = layout().percussionTable(reader()) + index * 5u;
    if (!reader().has(row, 5)) {
      return;
    }
    const u8 note = reader().u8At(row + 4);
    if (note >= kRest) {
      return;
    }
    startNote(note, index, reader().u8At(row), reader().u8At(row + 1), reader().u8At(row + 2),
              reader().u8At(row + 3));
  }

  [[nodiscard]] Effects note(MaskedValues notes) {
    if (track.selected(notes.mask)) {
      track.pendingNote = notes.values[track.number];
      track.triggerTicks = 0;
      if (track.triggerDelay == 0) {
        trigger(*track.pendingNote);
      }
    }
    return Effects::wait(math::waitTicks(track.delta, track.multiplier));
  }

  [[nodiscard]] Effects wait() const { return Effects::wait(math::waitTicks(track.delta, track.multiplier)); }

  void masterVolume(u8 value) {
    if (track.number == 0) {
      out.masterLevel(value / 256.0);
    }
  }

  void echoDelay(u8 value) {
    if (track.number == 0) {
      program.echo.delayMilliseconds = (value & 0x0f) * 16.0;
      out.reverb(program.echo);
    }
  }

  void echoEnabled(u8 value) {
    if (track.number == 0) {
      program.echoEnabled = value != 0;
      program.echo.voiceMask = program.echoEnabled ? math::dspVoiceMask(track.activeMask) : 0;
      out.reverb(program.echo);
    }
  }

  void echoFeedback(s8 value) {
    if (track.number == 0) {
      program.echo.feedback = math::signedDspGain(value);
      out.reverb(program.echo);
    }
  }

  void echoFilter(u8 value) {
    if (track.number == 0) {
      program.echo.filterIndex = value;
      out.reverb(program.echo);
    }
  }

  void echoVolume(s8 value) {
    if (track.number == 0) {
      const double gain = math::signedDspGain(value);
      program.echo.leftGain = gain;
      program.echo.rightGain = gain;
      program.echo.send = std::abs(gain);
      out.reverb(program.echo);
    }
  }

  [[nodiscard]] Effects repeatUntil(u8 slot, u8 count, Address destination) {
    // The SPC700 counter is eight-bit, so encoded zero completes on its 256th visit.
    return vm.countedRepeatUntil(slot, count == 0 ? 256 : count, destination);
  }

  [[nodiscard]] Effects repeatBreak(u8 slot, u8 count, Address destination) {
    RepeatCounter counter = vm.repeatCounter(slot);
    if (counter.firstVisit()) {
      counter.start(count == 0 ? 256 : count);
    }
    if (counter.consumeReplay()) {
      return vm.fallthrough();
    }
    counter.finish();
    return vm.finiteBranch(destination);
  }

  [[nodiscard]] Effects returnOrEnd() {
    if (vm.inSubroutine()) {
      return vm.return_();
    }
    closeVoice(vm.tick());
    return vm.end();
  }

  void tick() {
    if (track.pendingNote && ++track.triggerTicks == track.triggerDelay) {
      trigger(*track.pendingNote);
    }

    if (track.lastNote.valid() && track.voiceControls[kGate] != 0 && ++track.gateTicks > track.voiceControls[kGate]) {
      closeVoice(vm.tick());
    }

    if (track.portamentoActive) {
      const double distance = track.portamentoTarget - track.driverPitch;
      if (std::abs(distance) <= track.portamentoStep) {
        track.driverPitch = track.portamentoTarget;
        track.portamentoActive = false;
      } else {
        track.driverPitch += std::copysign(track.portamentoStep, distance);
      }
    }

    if (track.pitchTable == 0 || !track.lastNote.valid()) {
      return;
    }
    const u16 phase = static_cast<u16>(track.pitchPhase) + track.voiceControls[kPitchRate];
    if (phase > 0xff) {
      track.pitchPhase = track.voiceControls[kPitchRate];
      track.pitchPosition = resolvePitchPosition(track.pitchTable, static_cast<u8>(track.pitchPosition + 1),
                                                 track.pitchPosition);
    } else {
      track.pitchPhase = static_cast<u8>(phase);
    }
    emitPitchBend(modulationValue());
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

template <class Event>
[[nodiscard]] MaskedValues maskedValues(Event& event, std::string_view valueName, SemanticOperandRole role,
                                        SourceValueDisplay display = SourceValueDisplay::Default) {
  MaskedValues result;
  result.mask = event.u8("voice_mask", SourceValueDisplay::Hex);
  for (u32 voice = 0; voice < kTrackCount; ++voice) {
    if ((result.mask & math::voiceBit(voice)) == 0) {
      continue;
    }
    const std::string name = std::string(valueName) + "_" + std::to_string(voice);
    result.values[voice] = event.u8(name, display, role);
  }
  return result;
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin,
                                                   std::vector<Diagnostic>* diagnostics, std::set<u8>* srcns,
                                                   std::set<u8>* percussion) {
  Cursor cursor(reader, begin, kFormatId, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  const u8 opcode = cursor.opcode();
  switch (opcode) {
    case 0x00: {
      auto event = cursor.command("Delta Time", SequenceSemantic::State);
      return event.invoke<&Playback::delta>(event.u8("ticks", SemanticOperandRole::Duration));
    }
    case 0x01: {
      auto event = cursor.command("Active Voices", SequenceSemantic::State);
      return event.invoke<&Playback::activeVoices>(event.u8("mask", SourceValueDisplay::Hex));
    }
    case 0x02: {
      auto event = cursor.command("Call", SequenceSemantic::Call);
      return event.call(event.addressLe("destination", SemanticOperandRole::CallTarget));
    }
    case 0x03:
      return cursor.command("Return / End", SequenceSemantic::End)
          .invokeFlow<&Playback::returnOrEnd>()
          .discoverReturn();
    case 0x04: {
      auto event = cursor.command("Timebase Multiplier", SequenceSemantic::State);
      return event.invoke<&Playback::multiplier>(event.u8("multiplier"));
    }
    case 0x05: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::masterVolume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0x06:
    case 0x0f: {
      auto event = cursor.command(opcode == 0x06 ? "Repeat Until A" : "Repeat Until B", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const Address destination = event.addressLe("destination", SemanticOperandRole::RepeatTarget);
      const u8 slot = opcode == 0x06 ? 0 : 1;
      event.invokeFlow<&Playback::repeatUntil>(slot, count, destination);
      return event.mayBranchTo(destination);
    }
    case 0x07:
    case 0x10: {
      auto event =
          cursor.command(opcode == 0x07 ? "Repeat Break A" : "Repeat Break B", SequenceSemantic::RepeatBreak);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const Address destination = event.addressLe("destination", SemanticOperandRole::RepeatTarget);
      const u8 slot = opcode == 0x07 ? 0 : 1;
      event.invokeFlow<&Playback::repeatBreak>(slot, count, destination);
      return event.mayBranchTo(destination);
    }
    case 0x08: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      return event.jump(event.addressLe("destination", SemanticOperandRole::JumpTarget));
    }
    case 0x09: {
      auto event = cursor.command("Notes", SequenceSemantic::Note);
      const MaskedValues notes = maskedValues(event, "note", SemanticOperandRole::NoteKey, SourceValueDisplay::Hex);
      if (percussion != nullptr) {
        for (u32 voice = 0; voice < kTrackCount; ++voice) {
          if ((notes.mask & math::voiceBit(voice)) != 0 && notes.values[voice] >= 0x80) {
            percussion->insert(notes.values[voice] & 0x7f);
          }
        }
      }
      return event.invoke<&Playback::note>(notes);
    }
    case 0x0a: {
      auto event = cursor.command("Echo Delay", SequenceSemantic::State);
      return event.invoke<&Playback::echoDelay>(event.u8("delay"));
    }
    case 0x0b: {
      auto event = cursor.command("Note Trigger Delay", SequenceSemantic::State);
      return event.invoke<&Playback::noteDelay>(maskedValues(event, "delay", SemanticOperandRole::Duration));
    }
    case 0x0c: {
      auto event = cursor.command("Legato Voice Mask", SequenceSemantic::State);
      return event.invoke<&Playback::slur>(event.u8("mask", SourceValueDisplay::Hex));
    }
    case 0x0d: {
      auto event = cursor.command("Echo Enable", SequenceSemantic::State);
      return event.invoke<&Playback::echoEnabled>(event.u8("enabled"));
    }
    case 0x0e:
      return cursor.command("Wait", SequenceSemantic::Wait).invoke<&Playback::wait>();
    case 0x11: {
      auto event = cursor.command("Echo Feedback", SequenceSemantic::State);
      return event.invoke<&Playback::echoFeedback>(event.s8("feedback"));
    }
    case 0x12: {
      auto event = cursor.command("Echo FIR Preset", SequenceSemantic::State);
      return event.invoke<&Playback::echoFilter>(event.u8("preset"));
    }
    case 0x13: {
      auto event = cursor.command("Echo Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::echoVolume>(event.s8("volume", SemanticOperandRole::Level));
    }
    case 0x14: {
      auto event = cursor.sourceOnly("Echo Start Address", "echo-start-address");
      static_cast<void>(event.u8("esa_high", SourceValueDisplay::Hex));
      return event;
    }
    default:
      break;
  }

  if (opcode >= 0x20 && opcode - 0x20 < kParameterCommands.size()) {
    const u8 index = opcode - 0x20;
    const ParameterCommand& command = kParameterCommands[index];
    auto event = cursor.command(command.name, command.semantic);
    const MaskedValues values = maskedValues(event, "value", command.role);
    if (index == kSrcn && srcns != nullptr) {
      for (u32 voice = 0; voice < kTrackCount; ++voice) {
        if ((values.mask & math::voiceBit(voice)) != 0) {
          srcns->insert(values.values[voice]);
        }
      }
    }
    return event.invoke<&Playback::control>(index, values);
  }
  return cursor.unsupported("Invalid Command").stop();
}

}  // namespace

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config{
      .commandKindPrefix = std::string(kFormatId),
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior = SequenceProgramBehavior{
          .commandLimit = kCommandLimit,
          .preferredPitchTransitionRendering = PitchTransitionRenderingHint::Portamento,
          .initialSourceInstrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 0},
          .initialLevel = 0x88 / 256.0,
          .initialMasterLevel = 1.0,
          .initialReverbSend = 0.0,
          .initialStereoBalance = StereoBalance{.leftGain = 0.5, .rightGain = 0.5},
          .initialMonoModeChannels = 0,
          .initialPitchBendRangeSemitones = 48,
          .initialTempoMicrosecondsPerQuarter = 804000,
      },
  };
  return config;
}

SequenceParse decodeSequence(RetainedSource source, const Layout& layout, AssetId sequenceId,
                             SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const ByteReader reader = source.reader();
  const SourceRange header = reader.range(layout.sequenceReferenceAddress, layout.sequenceReferenceSize);
  std::set<u8> srcns{0};
  std::set<u8> percussion;
  SequenceDecodeSession sequence{reader, sequenceConfig(), sequenceId, header, sourceMap, kCommandLimit, kAramSize};
  const u32 pointer = layout.sequenceReferenceAddress + layout.sequenceReferenceSize - 2u;
  sequence.addTrack(
      0, reader.range(pointer, 2), layout.sequenceAddress,
      [&](u32 offset) { return decodeCommand(reader, offset, diagnostics, &srcns, &percussion); },
      layout.sequenceAddress);
  SequenceProgram program =
      sequence.finish(makeCompiledRuntime<Cursor, ProgramState>(DriverData{std::move(source), layout}));
  const TrackProgram stream = program.tracks.front();
  for (u32 voice = 1; voice < kTrackCount; ++voice) {
    TrackProgram copy = stream;
    copy.sourceTrackNumber = voice;
    program.tracks.push_back(std::move(copy));
  }
  return SequenceParse{
      .program = std::move(program),
      .srcns = std::move(srcns),
      .percussion = std::move(percussion),
      .headerRange = header,
  };
}

}  // namespace vgmtrans::formats::namco_snes
