/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/PandoraBoxSnes/PandoraBoxSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceProgramConfig.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace vgmtrans::formats::pandora_box_snes {

using namespace core;

namespace {

constexpr u32 kCommandLimit = 131072;
constexpr std::array<u8, 16> kVolumeTable{
    0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c,
    0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c,
};
constexpr std::array<u16, 12> kPitchTable{
    0x0983, 0x0a14, 0x0aad, 0x0b50, 0x0bfc, 0x0cb3,
    0x0d74, 0x0e41, 0x0f1a, 0x1000, 0x10f3, 0x11f6,
};
constexpr std::array<std::array<s8, 8>, 4> kFirPresets{{
    {{0x7f, 0, 0, 0, 0, 0, 0, 0}},
    {{-1, 8, 0x17, 0x24, 0x24, 0x17, 8, -1}},
    {{0x34, 0x33, 0, -0x27, -0x1b, 1, -4, -0x15}},
    {{0x58, -0x41, -0x25, -0x10, -2, 7, 0x0c, 0x0c}},
}};

[[nodiscard]] u32 tempoMicroseconds(u8 tempo) {
  return tempo == 0 ? 0 : static_cast<u32>(std::lround(60000000.0 / tempo));
}

[[nodiscard]] u32 counter(u8 value) { return value == 0 ? 256u : value; }

[[nodiscard]] double signedGain(s8 value) { return value / 128.0; }

[[nodiscard]] std::optional<u8> firPreset(const std::array<s8, 8>& coefficients) {
  const auto found = std::ranges::find(kFirPresets, coefficients);
  return found == kFirPresets.end() ? std::nullopt
                                    : std::optional<u8>{static_cast<u8>(found - kFirPresets.begin())};
}

[[nodiscard]] u8 indexedVolumeValue(Version version, u8 index) {
  return version == Version::Traverse ? static_cast<u8>(0xf0 + index) : index;
}

[[nodiscard]] u32 commandSize(ByteReader reader, u32 offset) {
  if (!reader.has(offset, 1)) {
    return 0;
  }
  const u8 opcode = reader.u8At(offset);
  if (opcode < 0x40) {
    return (opcode & 0x20) == 0 ? 2 : 1;
  }
  if (opcode < 0xe0) {
    return 1;
  }
  constexpr std::array<u8, 23> sizes{
      2, 2, 2, 2, 1, 1, 1, 1, 6, 2, 1, 1, 2, 1, 1, 1, 1, 3, 2, 6, 2, 1, 2,
  };
  return opcode <= 0xf6 ? sizes[opcode - 0xe0] : 0;
}

struct RepeatInfo {
  Address start;
  Address end;
  u8 plays;
  u8 slot;

  [[nodiscard]] bool isInfinite() const { return plays == 0 || plays == 0xff; }
};

struct TrackLayout {
  std::map<u32, RepeatInfo> begin;
  std::map<u32, RepeatInfo> end;
  std::map<u32, RepeatInfo> breaks;
};

[[nodiscard]] TrackLayout analyzeTrack(ByteReader reader, u32 start) {
  struct OpenRepeat {
    u32 command;
    Address start;
    u8 plays;
    u8 slot;
    std::vector<u32> breaks;
  };

  TrackLayout layout;
  std::vector<OpenRepeat> stack;
  for (u32 offset = start; offset < reader.size();) {
    const u32 size = commandSize(reader, offset);
    if (size == 0 || !reader.has(offset, size)) {
      break;
    }
    const u8 opcode = reader.u8At(offset);
    if (opcode == 0xec && stack.size() < 8) {
      const u8 count = reader.u8At(offset + 1);
      stack.push_back(OpenRepeat{
          .command = offset,
          .start = Address{offset + size},
          .plays = count,
          .slot = static_cast<u8>(stack.size()),
      });
    } else if (opcode == 0xee && !stack.empty()) {
      stack.back().breaks.push_back(offset);
    } else if (opcode == 0xed && !stack.empty()) {
      OpenRepeat open = std::move(stack.back());
      stack.pop_back();
      const RepeatInfo info{
          .start = open.start,
          .end = Address{offset + size},
          .plays = open.plays,
          .slot = open.slot,
      };
      layout.begin.emplace(open.command, info);
      layout.end.emplace(offset, info);
      for (const u32 branch : open.breaks) {
        layout.breaks.emplace(branch, info);
      }
      // No bytes after a declared infinite repeat are reachable.
      if (info.isInfinite()) {
        break;
      }
    }
    offset += size;
    if (opcode == 0xf5) {
      break;
    }
  }
  return layout;
}

struct ProgramState {
  explicit ProgramState(const Layout& layout) : version(layout.version), echo(layout.echo) {}

  Version version;
  EchoState echo;
  bool initialized = false;
};

struct VibratoState {
  bool enabled = false;
  u8 delay = 0;
  u8 interval = 0;
  s16 step = 0;
  u8 directionPeriod = 0;
};

struct TrackState {
  u8 octave = 3;
  u8 noteLength = 1;
  u8 quantize = 0;
  u8 rawVolume = 0;
  s8 tuning = 0;
  s8 transpose = 0;
  bool previousSlur = false;
  bool previousWasRest = true;
  int previousKey = 0;
  // The driver compares note and octave before applying transpose or tuning.
  int previousPitchKey = 0;
  PerformanceNoteId previousNote;
  u16 currentPitch = 0;
  VibratoState vibrato;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void beforeCommand() {
    if (program.initialized) {
      return;
    }
    program.initialized = true;
    const EchoState& echo = program.echo;
    out.reverb(ReverbPerformanceEvent{
        .voiceMask = 0,
        .send = 0.0,
        .leftGain = signedGain(echo.volume),
        .rightGain = signedGain(echo.volume),
        .delayMilliseconds = echo.delay * 16.0,
        .feedback = signedGain(echo.feedback),
        .filterIndex = firPreset(echo.fir),
    });
  }

  void tempo(u8 value) { out.tempo(tempoMicroseconds(value)); }

  void volume(u8 value) {
    track.rawVolume = value;
    out.level(decodedVolume(program.version, value) / 255.0, ValueQuantization{.levels = 256});
  }

  void indexedVolume(u8 index) { volume(indexedVolumeValue(program.version, index)); }

  void stepVolume(int direction) {
    const u8 stop = direction > 0 ? (program.version == Version::Traverse ? 0xff : 0x0f)
                                  : (program.version == Version::Traverse ? 0xf0 : 0x00);
    if (track.rawVolume == stop) {
      return;
    }
    volume(static_cast<u8>(track.rawVolume + direction));
  }

  void balance(u8 value) {
    const u8 position = std::min<u8>(value, 0x80);
    out.stereoBalance((0x80 - position) / 128.0, position / 128.0);
  }

  void instrument(u8 value) {
    out.instrument(InstrumentIdentity{.domain = kInstrumentDomain, .key = value},
                   InstrumentEnvelopeMode::PreserveDynamicOverride);
  }

  void dynamicEnvelope(u8 attack, u8 decay, u8 sustainRate, u8 sustainLevel) {
    const DynamicAdsr adsr = dynamicAdsr(attack, decay, sustainRate, sustainLevel);
    out.replaceEnvelope(snesDspEnvelope(adsr.adsr1, adsr.adsr2, 0),
                        VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void reverb(bool enabled) {
    out.reverb(enabled ? std::abs(program.echo.volume) / 128.0 : 0.0);
  }

  void beginRepeat(u8 slot, u8 plays) {
    RepeatCounter repeat = vm.repeatCounter(slot);
    if (repeat.firstVisit()) {
      repeat.start(plays);
    }
  }

  [[nodiscard]] u16 dspPitch(int key) const {
    const int noteClass = ((key % 12) + 12) % 12;
    const int octave = (key - noteClass) / 12;
    u16 pitch = static_cast<u16>(kPitchTable[noteClass] + track.tuning);
    for (int shift = octave - 3; shift > 0; --shift) {
      pitch = static_cast<u16>(pitch << 1);
    }
    for (int shift = octave - 3; shift < 0; ++shift) {
      pitch = static_cast<u16>(pitch >> 1);
    }
    return pitch;
  }

  [[nodiscard]] static double tuningCents(u16 pitch, int key) {
    return 100.0 * (45.0 - key + 12.0 * std::log2(std::max<u16>(1, pitch & 0x3fff) / 4096.0));
  }

  [[nodiscard]] LfoPerformanceContext vibratoContext(u16 basePitch) const {
    const u32 interval = counter(track.vibrato.interval);
    const u32 period = counter(track.vibrato.directionPeriod);
    // A direction count of one has a 256-step note-reset transient before it
    // settles into its two-step oscillation. Retain that audible first curve;
    // every other count returns to the note pitch after 2*period steps.
    const u32 curveSteps = track.vibrato.directionPeriod == 1 ? 512u : 2u * period;
    std::vector<double> curve;
    curve.reserve(curveSteps);
    curve.push_back(0.0);
    u8 directionCounter = static_cast<u8>(track.vibrato.directionPeriod >> 1);
    int direction = 1;
    u16 pitch = basePitch;
    const double audibleBase = std::max<u16>(1, basePitch & 0x3fff);
    const auto semitones = [&](u16 raw) {
      return 12.0 * std::log2(std::max<u16>(1, raw & 0x3fff) / audibleBase);
    };
    for (u32 sample = 1; sample < curveSteps; ++sample) {
      --directionCounter;
      if (directionCounter == 0) {
        directionCounter = track.vibrato.directionPeriod;
        direction = -direction;
      }
      pitch = static_cast<u16>(pitch + direction * track.vibrato.step);
      curve.push_back(semitones(pitch));
    }
    const auto [minimum, maximum] = std::ranges::minmax_element(curve);
    const ModulationRange range{.minimum = *minimum, .maximum = *maximum};
    for (double& value : curve) {
      value = value < 0.0 ? (range.minimum < 0.0 ? value / -range.minimum : 0.0)
                          : (range.maximum > 0.0 ? value / range.maximum : 0.0);
    }
    return LfoPerformanceContext{
        .cyclesPerTick = 1.0 / (curveSteps * interval),
        .delayTicks = track.vibrato.delay,
        .delayIsTempoRelative = true,
        .shape = LfoShape{.waveform = LfoWaveform::Triangle, .samples = std::move(curve)},
        .initialPhaseCycles = 0.0,
        .noteRestartInitialPhaseCycles = 0.0,
        .pitchRangeSemitones = range,
        .sampleImmediatelyOnNote = false,
        .delayUpdateMode = LfoDelayUpdateMode::FutureNotesOnly,
        .restartMode = LfoRestartMode::None,
        .zeroDepthBehavior = LfoZeroDepthBehavior::HoldOutputUntilNextNote,
    };
  }

  void emitVibrato(u16 basePitch) {
    if (!track.vibrato.enabled || track.vibrato.step == 0) {
      out.vibratoDepth(0.0, LfoPerformanceContext{
                                .restartMode = LfoRestartMode::None,
                                .zeroDepthBehavior = LfoZeroDepthBehavior::HoldOutputUntilNextNote,
                            });
      return;
    }
    LfoPerformanceContext context = vibratoContext(basePitch);
    const ModulationRange range = *context.pitchRangeSemitones;
    out.vibratoDepth(std::max(std::abs(range.minimum), std::abs(range.maximum)), context);
    out.vibratoRateCyclesPerTick(*context.cyclesPerTick, context);
  }

  void vibratoParameters(u8 delay, u8 interval, s16 step, u8 directionPeriod) {
    track.vibrato = VibratoState{
        .enabled = true,
        .delay = delay,
        .interval = interval,
        .step = step,
        .directionPeriod = directionPeriod,
    };
    if (track.currentPitch != 0) {
      emitVibrato(track.currentPitch);
    }
  }

  void vibratoEnable(bool enabled) {
    track.vibrato.enabled = enabled;
    if (track.currentPitch != 0 || !enabled) {
      emitVibrato(track.currentPitch);
    }
  }

  void extendPreviousNote(u32 duration) {
    if (!track.previousNote.valid()) {
      return;
    }
    track.previousNote = out.note(NotePerformanceEvent{
        .key = static_cast<double>(track.previousKey),
        .durationTicks = duration,
        .extendsPrevious = true,
        .restartsEnvelope = false,
        .restartsLfoPhase = false,
    });
  }

  [[nodiscard]] Effects note(u8 keyIndex, bool slur, std::optional<u8> explicitLength) {
    if (explicitLength) {
      track.noteLength = *explicitLength;
    }
    const u32 length = track.noteLength == 0 ? 1u : track.noteLength;
    const u32 sounding = slur || track.quantize == 0 ? length : std::max(1u, length * track.quantize / 8u);
    if (keyIndex == 0) {
      if (track.previousSlur) {
        extendPreviousNote(sounding);
      }
      track.previousSlur = slur;
      track.previousWasRest = true;
      return Effects::wait(length);
    }

    const int pitchKey = track.octave * 12 + keyIndex - 1;
    const bool continuesVoice = track.previousSlur && !track.previousWasRest;
    if (continuesVoice && track.previousPitchKey == pitchKey) {
      extendPreviousNote(sounding);
      track.previousSlur = slur;
      return Effects::wait(length);
    }

    const int sourceKey = pitchKey + track.transpose;
    track.currentPitch = dspPitch(sourceKey);
    out.tuning(tuningCents(track.currentPitch, sourceKey));
    emitVibrato(track.currentPitch);
    NotePerformanceEvent event{
        .key = static_cast<double>(sourceKey),
        .durationTicks = sounding,
        .restartsEnvelope = !continuesVoice,
        .restartsLfoPhase = !continuesVoice,
    };
    if (continuesVoice && track.previousNote.valid()) {
      if (track.previousKey == sourceKey) {
        event.extendsPrevious = true;
        track.previousNote = out.note(std::move(event));
      } else {
        track.previousNote = out.continueVoice(track.previousNote, std::move(event));
      }
    } else {
      track.previousNote = out.note(std::move(event));
    }
    track.previousKey = sourceKey;
    track.previousPitchKey = pitchKey;
    track.previousSlur = slur;
    track.previousWasRest = false;
    return Effects::wait(length);
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, const Layout& layout,
                                                   const TrackLayout& trackLayout,
                                                   std::vector<Diagnostic>* diagnostics,
                                                   std::set<u8>& programs) {
  Cursor cursor(reader, begin, kAramSize, kFormatId, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode < 0x40) {
    auto event = cursor.command((opcode & 0x0f) == 0 ? "Rest" : "Note",
                                (opcode & 0x0f) == 0 ? SequenceSemantic::Rest : SequenceSemantic::Note);
    const u8 key = event.opcodeBits<0, 4>("scale_step", SemanticOperandRole::NoteKey);
    const bool slur = event.opcodeBits<4, 1>("slur");
    const bool reusesLength = event.opcodeBits<5, 1>("reuse_length");
    const std::optional<u8> length = reusesLength ? std::nullopt
                                                  : std::optional<u8>{event.u8("length", SemanticOperandRole::Duration)};
    return event.invoke<&Playback::note>(key, slur, length);
  }
  if (opcode < 0x48) {
    auto event = cursor.command("Octave", SequenceSemantic::Pitch);
    const u8 octave = event.opcodeBits<0, 3>("octave");
    return event.set<&TrackState::octave>(octave);
  }
  if (opcode < 0x50) {
    auto event = cursor.command("Gate Fraction", SequenceSemantic::State);
    const u8 quantize = event.opcodeBits<0, 3>("eighths", SemanticOperandRole::Duration);
    return event.set<&TrackState::quantize>(quantize);
  }
  if (opcode < 0x60) {
    auto event = cursor.command("Indexed Volume", SequenceSemantic::Level);
    const u8 index = event.opcodeBits<0, 4>("index", SemanticOperandRole::Level);
    event.derived("volume", decodedVolume(layout.version, indexedVolumeValue(layout.version, index)),
                  SemanticOperandRole::Level);
    return event.invoke<&Playback::indexedVolume>(index);
  }
  if (opcode < 0xe0) {
    auto event = cursor.command("Program Change", SequenceSemantic::Program);
    const u8 program = event.opcodeValue("program", static_cast<u8>(opcode - 0x60), SourceValueDisplay::Default,
                                         SemanticOperandRole::InstrumentProgram);
    programs.insert(program);
    return event.invoke<&Playback::instrument>(program);
  }

  switch (opcode) {
    case 0xe0: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const u8 raw = event.u8("tempo");
      event.derived("bpm", raw, SourceValueDisplay::BeatsPerMinute);
      return event.invoke<&Playback::tempo>(raw);
    }
    case 0xe1: {
      auto event = cursor.command("Fine Pitch Offset", SequenceSemantic::Pitch);
      return event.set<&TrackState::tuning>(event.s8("dsp_pitch_offset", SemanticOperandRole::Pitch));
    }
    case 0xe2: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case 0xe3: {
      auto event = cursor.command("Stereo Balance", SequenceSemantic::Pan);
      return event.invoke<&Playback::balance>(event.u8("position", SemanticOperandRole::Pan));
    }
    case 0xe4:
      return cursor.command("Octave Up", SequenceSemantic::Pitch).add<&TrackState::octave>(1);
    case 0xe5:
      return cursor.command("Octave Down", SequenceSemantic::Pitch).add<&TrackState::octave>(-1);
    case 0xe6:
      return cursor.command("Volume Up", SequenceSemantic::Level).invoke<&Playback::stepVolume>(1);
    case 0xe7:
      return cursor.command("Volume Down", SequenceSemantic::Level).invoke<&Playback::stepVolume>(-1);
    case 0xe8: {
      auto event = cursor.command("Vibrato Parameters", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 interval = event.u8("step_interval", SemanticOperandRole::Duration);
      const s16 step = event.s16le("pitch_step", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Modulation);
      const u8 period = event.u8("direction_period", SemanticOperandRole::Duration);
      event.derived("steady_cycle_ticks", 2u * counter(interval) * counter(period), SemanticOperandRole::Duration);
      if (period == 1) {
        event.derived("note_reset_curve_ticks", 512u * counter(interval), SemanticOperandRole::Duration);
      }
      return event.invoke<&Playback::vibratoParameters>(delay, interval, step, period);
    }
    case 0xe9: {
      auto event = cursor.command("Vibrato Enable", SequenceSemantic::Modulation);
      const bool enabled = event.u8("enabled", SemanticOperandRole::State) != 0;
      return event.invoke<&Playback::vibratoEnable>(enabled);
    }
    case 0xea:
    case 0xeb:
      return cursor.command(opcode == 0xeb ? "Reverb On" : "Reverb Off", SequenceSemantic::State)
          .invoke<&Playback::reverb>(opcode == 0xeb);
    case 0xec: {
      auto event = cursor.command("Repeat Begin", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const auto found = trackLayout.begin.find(begin);
      if (found == trackLayout.begin.end()) {
        return event.ignore();
      }
      event.derived("total_plays", found->second.isInfinite() ? 0u : count);
      event.derived("destination", found->second.start, SourceValueDisplay::Address,
                    SemanticOperandRole::RepeatTarget);
      return found->second.isInfinite() ? event : event.invoke<&Playback::beginRepeat>(found->second.slot, count);
    }
    case 0xed: {
      auto event = cursor.command("Repeat End", SequenceSemantic::Repeat);
      const auto found = trackLayout.end.find(begin);
      if (found == trackLayout.end.end()) {
        return event.ignore();
      }
      event.derived("destination", found->second.start, SourceValueDisplay::Address,
                    SemanticOperandRole::RepeatTarget);
      return found->second.isInfinite() ? event.declaredLoop(found->second.start)
                                        : event.repeatUntil(found->second.slot, found->second.plays,
                                                            found->second.start);
    }
    case 0xee: {
      auto event = cursor.command("Repeat Break", SequenceSemantic::RepeatBreak);
      const auto found = trackLayout.breaks.find(begin);
      if (found == trackLayout.breaks.end() || found->second.isInfinite()) {
        return event.ignore();
      }
      event.derived("destination", found->second.end, SourceValueDisplay::Address,
                    SemanticOperandRole::JumpTarget);
      return event.repeatBreak(found->second.slot, found->second.end);
    }
    case 0xef:
    case 0xf0:
      return cursor.noOp("NOP");
    case 0xf1: {
      auto event = cursor.sourceOnly("Raw DSP Write", "raw-dsp-write");
      static_cast<void>(event.u8("register", SourceValueDisplay::Hex, SemanticOperandRole::Address));
      static_cast<void>(event.u8("value", SourceValueDisplay::Hex));
      return event;
    }
    case 0xf2: {
      auto event = cursor.sourceOnly("Noise Control", "noise-control");
      const u8 value = event.u8("value", SourceValueDisplay::Hex);
      event.derived("voice_mode", value & 3u, SourceValueDisplay::Default, SemanticOperandRole::State);
      event.derived("noise_clock", (value >> 2) & 0x1fu, SourceValueDisplay::Default,
                    SemanticOperandRole::Pitch);
      event.derived("preserve_clock", (value & 0x80) != 0, SemanticOperandRole::State);
      return event;
    }
    case 0xf3: {
      auto event = cursor.command("Dynamic ADSR", SequenceSemantic::Envelope);
      const u8 attack = event.u8("attack", SemanticOperandRole::Duration);
      const u8 decay = event.u8("decay", SemanticOperandRole::Duration);
      const u8 sustainRate = event.u8("sustain_rate", SemanticOperandRole::Duration);
      const u8 sustainLevel = event.u8("sustain_level", SemanticOperandRole::Level);
      static_cast<void>(event.u8("unused"));
      const DynamicAdsr adsr = dynamicAdsr(attack, decay, sustainRate, sustainLevel);
      event.derived("adsr1", adsr.adsr1, SourceValueDisplay::Hex);
      event.derived("adsr2", adsr.adsr2, SourceValueDisplay::Hex);
      return event.invoke<&Playback::dynamicEnvelope>(attack, decay, sustainRate, sustainLevel);
    }
    case 0xf4: {
      auto event = cursor.sourceOnly("Unused Driver Parameter", "unused-driver-parameter");
      static_cast<void>(event.u8("value", SourceValueDisplay::Hex));
      return event;
    }
    case 0xf5:
      return cursor.command("End", SequenceSemantic::End).end();
    case 0xf6: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      const u8 value = event.u8("value", SemanticOperandRole::Level);
      event.derived("decoded_volume", decodedVolume(layout.version, value), SemanticOperandRole::Level);
      return event.invoke<&Playback::volume>(value);
    }
    default:
      return cursor.unsupported("Invalid Command").stop();
  }
}

}  // namespace

u8 decodedVolume(Version version, u8 raw) {
  const bool indexed = version == Version::Traverse ? raw >= 0xf0 : raw < 0x10;
  return indexed ? kVolumeTable[raw & 0x0f] : raw;
}

DynamicAdsr dynamicAdsr(u8 attack, u8 decay, u8 sustainRate, u8 sustainLevel) {
  const auto scale = [](u8 value, u32 maximum) { return value * maximum / 127u; };
  return DynamicAdsr{
      .adsr1 = static_cast<u8>(0x80 | (scale(decay, 7) << 4) | scale(attack, 15)),
      .adsr2 = static_cast<u8>(((7u - scale(sustainLevel, 7)) << 5) | scale(sustainRate, 31)),
  };
}

namespace {

SequenceProgramConfig sequenceConfig(const Layout& layout) {
  return SequenceProgramConfig{
      .commandKindPrefix = kFormatId,
      .timebase = Timebase{.ppqn = static_cast<u16>(layout.timebase / 4)},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kCommandLimit,
              .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
              .initialSourceInstrument = InstrumentIdentity{.domain = kInstrumentDomain, .key = 0},
              .initialLevel = 0.0,
              .initialMasterLevel = std::abs(layout.echo.masterVolume) / 128.0,
              .initialReverbSend = 0.0,
              .initialStereoBalance = StereoBalance{.leftGain = 1.0, .rightGain = 0.0},
              .initialPitchBendRangeSemitones = 24,
              .initialTempoMicrosecondsPerQuarter = tempoMicroseconds(layout.initialTempo),
          },
  };
}

void annotateHeader(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder& sourceMap,
                    SourceAnnotationId annotation) {
  const u32 header = layout.sequenceHeaderAddress;
  AnnotationBuilder{sourceMap, annotation}
      .field("tempo", reader.range(header + 6, 1), layout.initialTempo, SourceValueDisplay::BeatsPerMinute)
      .field("timebase", reader.range(header + 7, 1), layout.timebase)
      .field("local_instrument_table_offset", reader.range(header + 0x0c, 1), layout.localInstrumentTableOffset,
             SourceValueDisplay::Address);
  auto echo = sourceMap.table("DSP Echo Configuration", reader.range(header + 0x20, 0x0c))
                  .kind("pandora-box-snes-echo-config")
                  .owner(ObjectRefs::sequence(sequenceId))
                  .parent(annotation);
  echo.fieldsAsChildren()
      .field("master_volume", reader.range(header + 0x20, 1), reader.s8At(header + 0x20),
             SourceValueDisplay::SignedDecimal)
      .field("echo_volume", reader.range(header + 0x21, 1), reader.s8At(header + 0x21),
             SourceValueDisplay::SignedDecimal)
      .field("echo_delay", reader.range(header + 0x22, 1), reader.u8At(header + 0x22))
      .field("echo_feedback", reader.range(header + 0x23, 1), reader.s8At(header + 0x23),
             SourceValueDisplay::SignedDecimal);
  for (u32 tap = 0; tap < 8; ++tap) {
    echo.field(fmt::format("fir_{}", tap), reader.range(header + 0x24 + tap, 1), reader.s8At(header + 0x24 + tap),
               SourceValueDisplay::SignedDecimal);
  }
  echo.description("MVOL, EVOL, EDL, EFB, and eight FIR coefficients; $FF in MVOL selects driver defaults");
}

}  // namespace

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  const SequenceProgramConfig config = sequenceConfig(layout);
  SequenceDecodeSession sequence{reader, config, sequenceId,
                                 reader.range(layout.sequenceHeaderAddress, kSequenceHeaderSize), sourceMap,
                                 kCommandLimit, kAramSize};
  std::set<u8> programs{0};

  if (const auto header = sequence.headerAnnotation(); sourceMap != nullptr && header) {
    annotateHeader(reader, layout, sequenceId, *sourceMap, *header);
  }

  for (u32 track = 0; track < layout.tracks.size(); ++track) {
    if (!layout.tracks[track]) {
      continue;
    }
    const u16 address = *layout.tracks[track];
    const SourceRange pointer = reader.range(layout.sequenceHeaderAddress + 0x10 + track * 2, 2);
    const TrackLayout trackLayout = analyzeTrack(reader, address);
    sequence.addTrack(
        track, pointer, address,
        [&](u32 offset) { return decodeCommand(reader, offset, layout, trackLayout, diagnostics, programs); },
        reader.le16(pointer.offset));
  }
  return SequenceParse{
      .program = sequence.finish(makeCompiledRuntime<Cursor, ProgramState>(layout)),
      .programs = std::move(programs),
  };
}

}  // namespace vgmtrans::formats::pandora_box_snes
