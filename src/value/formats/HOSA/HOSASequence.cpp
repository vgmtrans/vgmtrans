/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HOSA/HOSA.h"
#include "value/formats/HOSA/HOSABytecode.h"
#include "value/formats/HOSA/HOSALfo.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::hosa {

using namespace core;

namespace {

constexpr u32 kPpqn = 48;
constexpr double kSpuFullScale = 16383.0;
constexpr u16 kReuseTiming = 0xffff;
constexpr u8 kReuseVelocity = 0xff;
constexpr u8 kInitialTempo = 0x72;

struct RuntimeConfig {
  std::array<u16, 32> durations{};
  s16 leftGain = 0x3fff;
  s16 rightGain = 0x3fff;
  double reverbSend = 0.0;
  std::vector<Instrument> instruments;
};

struct AdsrOverrides {
  std::optional<u8> attackRate;
  std::optional<u8> decayRate;
  std::optional<u8> sustainRate;
  std::optional<u8> sustainLevel;
  std::optional<u8> releaseRate;

  void clear() { *this = {}; }
};

// This is the exact high-level subset copied by the driver's global-loop
// checkpoint. ADSR overrides, vibrato, auto-pan, and pitch bend deliberately
// remain outside it.
struct LoopTrackState {
  u16 delta = 0;
  u16 noteDelta = 0;
  u16 duration = 0xff;
  u8 velocity = 0;
  u8 note = 0xff;
  u8 program = 0;
  u8 volume = 127;
  u8 expression = 127;
  u8 pan = 64;
  bool portamento = false;
  bool portamentoFresh = false;
  u8 portamentoTime = 0;
};

struct PlaybackTrack : LoopTrackState {
  AdsrOverrides adsr;
  const Region* currentRegion = nullptr;
  PerformanceNoteId lastVoice;
};

struct GlobalState {
  u16 tempo = kInitialTempo;
  u8 timeSignature = 0;
};

struct LoopState {
  GlobalState global;
  std::vector<LoopTrackState> tracks;
};

struct ProgramState {
  ProgramState(const SequenceProgram& program, const RuntimeConfig& config)
      : leftGain(config.leftGain), rightGain(config.rightGain), reverbSend(config.reverbSend),
        instruments(config.instruments), tracks(program.tracks.size()) {}

  [[nodiscard]] u8 resolveProgram(u8 program) const {
    return instruments.empty() || program < instruments.size() ? program : 0;
  }

  [[nodiscard]] const Region* region(u8 program, u8 key) const {
    if (program >= instruments.size()) return nullptr;
    const auto& regions = instruments[program].regions;
    const auto found = std::ranges::find_if(regions, [key](const Region& region) {
      return key >= region.keyLow && key <= region.keyHigh;
    });
    return found == regions.end() ? nullptr : &*found;
  }

  void saveLoop() {
    savedLoop.emplace(LoopState{.global = global});
    savedLoop->tracks.reserve(tracks.size());
    for (const auto& track : tracks) savedLoop->tracks.push_back(static_cast<const LoopTrackState&>(track));
  }

  void restoreLoop(u64 tick, PerformanceEmitter& out) {
    loopEnds.push_back(tick);
    if (!savedLoop) return;

    const GlobalState previous = global;
    global = savedLoop->global;
    for (std::size_t i = 0; i < tracks.size() && i < savedLoop->tracks.size(); ++i) {
      static_cast<LoopTrackState&>(tracks[i]) = savedLoop->tracks[i];
      // The driver releases every active SPU voice at the restore boundary.
      tracks[i].currentRegion = nullptr;
      tracks[i].lastVoice = {};
    }
    if (global.tempo != previous.tempo && global.tempo != 0) {
      out.tempo(static_cast<u32>(std::lround(60000000.0 / global.tempo)));
    }
    if (global.timeSignature != previous.timeSignature) emitTimeSignature(out, global.timeSignature);
  }

  void finalizePerformance(PerformanceSequence& performance) {
    std::ranges::sort(loopEnds);
    loopEnds.erase(std::unique(loopEnds.begin(), loopEnds.end()), loopEnds.end());
    for (auto& track : performance.tracks) {
      for (auto& event : track.events) {
        auto* note = std::get_if<NotePerformanceEvent>(&event);
        if (note == nullptr) continue;
        const u64 start = note->header.tick;
        const auto boundary = std::ranges::upper_bound(loopEnds, start);
        if (boundary != loopEnds.end() && *boundary < start + note->durationTicks) {
          note->durationTicks = static_cast<u32>(*boundary - start);
        }
      }
    }
  }

  static void emitTimeSignature(PerformanceEmitter& out, u8 packed) {
    out.timeSignature(static_cast<u8>((packed >> 4) + 1), static_cast<u8>((packed & 0x0f) + 1), kPpqn);
  }

  GlobalState global;
  s16 leftGain;
  s16 rightGain;
  double reverbSend;
  std::span<const Instrument> instruments;
  std::vector<PlaybackTrack> tracks;
  std::optional<LoopState> savedLoop;
  std::vector<u64> loopEnds;
};

struct TrackHandle {
  explicit TrackHandle(const TrackProgram& track) : index(track.sourceTrackNumber) {}
  u32 index = 0;
};

struct Playback {
  Playback(TrackHandle& handle, PerformanceEmitter& output, VmApi& api, ProgramState& state)
      : track(state.tracks.at(handle.index)), out(output), vm(api), program(state) {}

  void emitLevel() { out.level(track.volume / 127.0, ValueQuantization{.levels = 128}); }
  void emitExpression() { out.expression(track.expression / 127.0, ValueQuantization{.levels = 128}); }

  void emitBalance() {
    const u8 pan = track.currentRegion && track.currentRegion->panOverride ? *track.currentRegion->panOverride
                                                                          : track.pan;
    const double right = std::min<u8>(pan, 127) / 127.0;
    out.stereoBalance((program.leftGain / kSpuFullScale) * (1.0 - right),
                      (program.rightGain / kSpuFullScale) * right);
  }

  void emitEnvelope(VoiceEnvelopeScope scope) {
    if (track.currentRegion == nullptr) return;
    u16 adsr1 = track.currentRegion->adsr1;
    u16 adsr2 = track.currentRegion->adsr2;
    if (track.adsr.attackRate) adsr1 = static_cast<u16>((adsr1 & ~0x7f00u) | ((*track.adsr.attackRate & 0x7f) << 8));
    if (track.adsr.decayRate) adsr1 = static_cast<u16>((adsr1 & ~0x00f0u) | ((*track.adsr.decayRate & 0x0f) << 4));
    if (track.adsr.sustainRate) adsr2 = static_cast<u16>((adsr2 & ~0x1fc0u) | ((*track.adsr.sustainRate & 0x7f) << 6));
    if (track.adsr.sustainLevel) adsr1 = static_cast<u16>((adsr1 & ~0x000fu) | (*track.adsr.sustainLevel & 0x0f));
    if (track.adsr.releaseRate) adsr2 = static_cast<u16>((adsr2 & ~0x001fu) | (*track.adsr.releaseRate & 0x1f));
    out.replaceEnvelope(psxSpuEnvelope(adsr1, adsr2), scope);
  }

  void prepareVoice(bool reusesVoice) {
    track.program = program.resolveProgram(track.program);
    out.instrument(instrumentIdentity(track.program), InstrumentEnvelopeMode::UseInstrumentEnvelope);
    emitLevel();
    emitExpression();
    track.currentRegion = program.region(track.program, track.note);
    emitEnvelope(reusesVoice ? VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks
                             : VoiceEnvelopeScope::FutureAttacks);
    out.reverb(track.currentRegion && track.currentRegion->reverb ? program.reverbSend : 0.0);
    emitBalance();
  }

  [[nodiscard]] Effects playNote(u8 previousKey) {
    const bool glide = track.portamento && !track.portamentoFresh && track.lastVoice.valid() &&
                       previousKey != track.note && track.portamentoTime != 0;
    if (track.portamento && track.portamentoFresh) out.portamentoEnable(false);
    prepareVoice(glide);
    const PerformanceNoteId note = out.note(track.note, track.velocity / 127.0, track.duration);
    if (glide) {
      out.pitchSlide(note, previousKey, track.note, track.portamentoTime)
          .continueFrom(track.lastVoice)
          .requirePortamento();
    } else if (track.portamento && track.portamentoFresh) {
      // Bit 4 in the driver's portamento flags forces this first note to key on.
      // Enable native portamento only after it so the following note can reuse it.
      out.portamentoEnable(true);
    }
    if (track.portamento) track.portamentoFresh = false;
    track.lastVoice = note;
    return Effects::wait(track.delta);
  }

  [[nodiscard]] Effects note(u8 key, u16 duration, u16 deltaUpdate, bool deltaEqualsDuration, u8 velocityUpdate) {
    const u8 previous = track.note;
    track.note = key & 0x7f;
    track.duration = duration;
    if (deltaEqualsDuration) track.delta = duration;
    if (deltaUpdate != kReuseTiming) track.delta = deltaUpdate;
    track.noteDelta = track.delta;
    if (velocityUpdate != kReuseVelocity) track.velocity = velocityUpdate;
    return playNote(previous & 0x7f);
  }

  [[nodiscard]] Effects relativeNote(bool ascending, u8 distance) {
    const u8 previous = track.note;
    const u8 adjusted = ascending ? static_cast<u8>(track.note + distance) : static_cast<u8>(track.note - distance);
    track.note = adjusted & 0x7f;
    track.delta = track.noteDelta;
    return playNote(previous & 0x7f);
  }

  [[nodiscard]] Effects controlWait(u16 update) {
    if (update != kReuseTiming) track.delta = update;
    return Effects::wait(track.delta);
  }

  void tempo(u8 value) {
    program.global.tempo = value;
    if (value != 0) out.tempo(static_cast<u32>(std::lround(60000000.0 / value)));
  }

  void timeSignature(u8 value) {
    program.global.timeSignature = value;
    ProgramState::emitTimeSignature(out, value);
  }

  void programChange(u8 value) {
    track.program = value;
    track.adsr.clear();
    out.instrument(instrumentIdentity(value), InstrumentEnvelopeMode::UseInstrumentEnvelope);
  }

  void volume(u8 value) {
    track.volume = value;
    emitLevel();
  }

  void pan(u8 value) {
    track.pan = value;
    emitBalance();
  }

  void expression(u8 value) {
    track.expression = value;
    emitExpression();
  }

  void autoPan(u8 depth, u8 halfPeriod) {
    const double rate = 1.0 / (4.0 * std::max<u8>(halfPeriod, 1));
    LfoPerformanceContext context{
        .cyclesPerTick = rate,
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .restartMode = LfoRestartMode::PhaseAndDelay,
        .panLaw = PanLaw::ConstantSum,
    };
    out.panLfoDepth(std::min(2.0, depth / 127.0), context);
    out.panLfoRateCyclesPerTick(rate, std::move(context));
  }

  void vibrato(u8 depth, u8 rate, u8 waveform, u8 delay) {
    LfoShape shape = vibratoShape(waveform);
    const double cycles = 1.0 / (shape.samples.size() * (rate == 0 ? 256.0 : rate));
    const double scaledDepth = (depth & 0x80) != 0 ? (depth & 0x7f) * 12.0 : depth;
    LfoPerformanceContext context{
        .cyclesPerTick = cycles,
        .delayTicks = delay,
        .delayIsTempoRelative = true,
        .shape = std::move(shape),
        .restartMode = LfoRestartMode::PhaseAndDelay,
    };
    out.vibratoDepth(scaledDepth / 128.0, context);
    out.vibratoRateCyclesPerTick(cycles, std::move(context));
  }

  void pitchBend(u8 low, u8 high) {
    out.pitchBend((static_cast<int>(low) + static_cast<int>(high) * 128 - 0x2000) / 682.0);
  }

  void portamentoOn(u8 duration) {
    track.portamento = true;
    track.portamentoFresh = true;
    track.portamentoTime = duration;
  }

  void portamentoOff() {
    track.portamento = false;
    track.portamentoFresh = false;
    out.portamentoEnable(false);
  }

  void adsr(u8 field, u8 value) {
    switch (field) {
      case 0: track.adsr.attackRate = value; break;
      case 1: track.adsr.decayRate = value; break;
      case 2: track.adsr.sustainRate = value; break;
      case 3: track.adsr.sustainLevel = value; break;
      case 4: track.adsr.releaseRate = value; break;
      default: break;
    }
  }

  void saveLoop() { program.saveLoop(); }
  void restoreLoop() { program.restoreLoop(vm.tick(), out); }

  PlaybackTrack& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;
};

using Cursor = CompilerCursor<TrackHandle, Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end,
                                                   const RuntimeConfig& config,
                                                   std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, end, kHosaCommandKindPrefix, diagnostics);
  if (!cursor.hasOpcode()) return cursor.truncated();
  const u8 status = cursor.opcode();

  auto readVariable = [](auto& event, std::string_view name, SemanticOperandRole role) -> u16 {
    const u8 first = event.u8(name, SourceValueDisplay::Hex, role);
    if ((first & 0x80) == 0) return first;
    return static_cast<u16>(((first & 0x7f) << 7) |
                            (event.u8("value_low", SourceValueDisplay::Hex, role) & 0x7f));
  };
  auto readTiming = [&](auto& event) -> u16 {
    if ((status & 0x60) == 0x40) return readVariable(event, "delta", SemanticOperandRole::Duration);
    if ((status & 0x60) == 0x60) {
      const u8 index = event.u8("delta_index", SourceValueDisplay::Hex, SemanticOperandRole::Duration) & 0x1f;
      return config.durations[index];
    }
    return kReuseTiming;
  };

  if (status < 0x80) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 encodedNote = event.u8("note", SourceValueDisplay::Hex, SemanticOperandRole::NoteKey);
    const u16 delta = readTiming(event);
    const u8 durationIndex = status & 0x1f;
    const u16 duration = durationIndex == 0x1f ? readVariable(event, "duration", SemanticOperandRole::Duration)
                                               : config.durations[durationIndex];
    const u8 velocity = (encodedNote & 0x80) != 0
                            ? event.u8("velocity", SemanticOperandRole::Level)
                            : kReuseVelocity;
    return event.invoke<&Playback::note>(encodedNote & 0x7f, duration, delta, (status & 0x60) == 0x20,
                                         velocity);
  }
  if ((status & 0x60) == 0x20) {
    auto event = cursor.command("Relative Note", SequenceSemantic::Note);
    const bool ascending = (status & 0x10) != 0;
    event.derived("direction", std::string(ascending ? "up" : "down"));
    event.derived("distance", status & 0x0f, SemanticOperandRole::Pitch);
    return event.invoke<&Playback::relativeNote>(ascending, status & 0x0f);
  }

  const u8 command = status & 0x1f;
  if (command == 0) return cursor.command("End of Track", SequenceSemantic::End).end();
  switch (command) {
    case 1: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const u8 value = event.u8("bpm");
      event.derived("tempo", value, SourceValueDisplay::BeatsPerMinute);
      return event.invoke<&Playback::tempo>(value).invoke<&Playback::controlWait>(readTiming(event));
    }
    case 2: {
      auto event = cursor.command("Time Signature", SequenceSemantic::Meta);
      const u8 value = event.u8("packed", SourceValueDisplay::Hex);
      event.derived("numerator", static_cast<u8>((value >> 4) + 1));
      event.derived("denominator", static_cast<u8>((value & 0x0f) + 1));
      return event.invoke<&Playback::timeSignature>(value).invoke<&Playback::controlWait>(readTiming(event));
    }
    case 3: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      return event.invoke<&Playback::programChange>(event.u8("program", SemanticOperandRole::InstrumentProgram))
          .invoke<&Playback::controlWait>(readTiming(event));
    }
    case 4: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level))
          .invoke<&Playback::controlWait>(readTiming(event));
    }
    case 5: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan", SemanticOperandRole::Pan))
          .invoke<&Playback::controlWait>(readTiming(event));
    }
    case 6: {
      auto event = cursor.command("Expression", SequenceSemantic::Level);
      return event.invoke<&Playback::expression>(event.u8("expression", SemanticOperandRole::Level))
          .invoke<&Playback::controlWait>(readTiming(event));
    }
    case 7: {
      auto event = cursor.command("Auto Pan", SequenceSemantic::Modulation);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      const u8 period = event.u8("half_period", SemanticOperandRole::Duration);
      return event.invoke<&Playback::autoPan>(depth, period).invoke<&Playback::controlWait>(readTiming(event));
    }
    case 8: {
      auto event = cursor.command("Vibrato", SequenceSemantic::Modulation);
      const u8 depth = event.u8("depth", SourceValueDisplay::Hex, SemanticOperandRole::Modulation);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      const u8 waveform = event.u8("waveform", SourceValueDisplay::Hex, SemanticOperandRole::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::vibrato>(depth, rate, waveform, delay)
          .invoke<&Playback::controlWait>(readTiming(event));
    }
    case 9: {
      auto event = cursor.command("Sequence Loop", SequenceSemantic::Loop);
      const u8 mode = event.u8("mode");
      if (mode != 0) return event.invoke<&Playback::restoreLoop>().synchronizedLoopEnd();
      const u16 wait = readTiming(event);
      return event.invoke<&Playback::controlWait>(wait).invoke<&Playback::saveLoop>().synchronizedLoopStart();
    }
    case 10: {
      auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
      const u8 low = event.u8("low", SourceValueDisplay::Hex, SemanticOperandRole::Pitch);
      const u8 high = event.u8("high", SourceValueDisplay::Hex, SemanticOperandRole::Pitch);
      return event.invoke<&Playback::pitchBend>(low, high).invoke<&Playback::controlWait>(readTiming(event));
    }
    case 14: {
      auto event = cursor.command("Portamento On", SequenceSemantic::Portamento);
      return event.invoke<&Playback::portamentoOn>(event.u8("duration", SemanticOperandRole::Duration))
          .invoke<&Playback::controlWait>(readTiming(event));
    }
    case 15: {
      auto event = cursor.command("Portamento Off", SequenceSemantic::Portamento);
      return event.invoke<&Playback::portamentoOff>().invoke<&Playback::controlWait>(readTiming(event));
    }
    case 16:
    case 17:
    case 18:
    case 19:
    case 20: {
      static constexpr std::array labels{"Attack Rate", "Decay Rate", "Sustain Rate", "Sustain Level",
                                          "Release Rate"};
      auto event = cursor.command(labels[command - 16], SequenceSemantic::Envelope);
      const u8 value = event.u8("value");
      return event.invoke<&Playback::adsr>(command - 16, value).invoke<&Playback::controlWait>(readTiming(event));
    }
    case 21: {
      auto event = cursor.command("Voice Allocation Class", SequenceSemantic::State,
                                  CommandPlaybackStatus::SourceOnly);
      event.u8("class");
      return event.invoke<&Playback::controlWait>(readTiming(event));
    }
    default: {
      auto event = cursor.command(command == 13 ? "Driver Parameter" : "Driver No-op", SequenceSemantic::Meta,
                                  CommandPlaybackStatus::NoOp);
      for (u8 i = 0; i < bytecode::kControlParameterBytes[command]; ++i) {
        event.u8("parameter", SourceValueDisplay::Hex);
      }
      return event.invoke<&Playback::controlWait>(readTiming(event));
    }
  }
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, AssetId sequence, u32 trackIndex, u32 start, u32 end,
                                       const RuntimeConfig& config, SourceMapBuilder* sourceMap,
                                       std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = end,
      .maxCommands = bytecode::kMaximumCommands,
      .sequenceAsset = sequence,
      .sourceMap = sourceMap,
  };
  TrackProgram track = tracks.decode(trackIndex, start, [&](u32 offset) {
    return decodeCommand(reader, offset, end, config, diagnostics);
  });
  if (!track.commands.empty()) {
    auto& last = track.commands.back();
    if (last.flow.defaultTransition.kind == CommandTransitionKind::Fallthrough &&
        last.flow.continuation.value == end) {
      last.flow = CommandFlow::end(Address{end});
    }
  }
  return track;
}

[[nodiscard]] double reverbSend(const ReverbConfig& reverb) {
  return reverb.mode == 0 ? 0.0 : std::clamp(reverb.depth / 32767.0, 0.0, 1.0);
}

}  // namespace

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config{
      .commandKindPrefix = std::string(kHosaCommandKindPrefix),
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior = SequenceProgramBehavior{
          .commandLimit = bytecode::kMaximumCommands,
          .inferLoopsFromRepeatedState = false,
          .panLaw = PanLaw::ConstantSum,
          .initialSourceInstrument = instrumentIdentity(0),
          .initialLevel = 1.0,
          .initialExpression = 1.0,
          .initialTempoMicrosecondsPerQuarter =
              static_cast<u32>(std::lround(60000000.0 / kInitialTempo)),
      },
  };
  return config;
}

SequenceProgram parseSequence(ByteReader reader, AssetId id, const SequenceLayout& layout,
                              const std::vector<Instrument>& instruments, SourceMapBuilder* sourceMap,
                              std::vector<Diagnostic>* diagnostics) {
  SequenceProgram sequence = sequenceConfig().makeProgram();
  constexpr double initialRight = 64.0 / 127.0;
  sequence.behavior.initialStereoBalance = StereoBalance{
      .leftGain = (layout.leftGain / kSpuFullScale) * (1.0 - initialRight),
      .rightGain = (layout.rightGain / kSpuFullScale) * initialRight,
  };
  RuntimeConfig runtime{
      .durations = layout.durations,
      .leftGain = layout.leftGain,
      .rightGain = layout.rightGain,
      .reverbSend = reverbSend(layout.reverb),
      .instruments = instruments,
  };
  if (sourceMap != nullptr) {
    sourceMap->header("HOSA Sequence Header", reader.range(layout.offset, 0x50))
        .kind("hosa-sequence-header")
        .owner(ObjectRefs::sequence(id))
        .field("signature", reader.range(layout.offset, 5), "HOSAV")
        .field("version", reader.range(layout.offset + 5, 1), layout.version)
        .field("track_count", reader.range(layout.offset + 6, 1), static_cast<u8>(layout.tracks.size()))
        .field("reverb_mode", reader.range(layout.offset + 7, 1), layout.reverb.mode)
        .field("reverb_depth", reader.range(layout.offset + 8, 2), layout.reverb.depth)
        .field("reverb_delay", reader.range(layout.offset + 10, 1), layout.reverb.delay)
        .field("reverb_feedback", reader.range(layout.offset + 11, 1), layout.reverb.feedback)
        .field("left_gain", reader.range(layout.offset + 12, 2), layout.leftGain)
        .field("right_gain", reader.range(layout.offset + 14, 2), layout.rightGain);
    sourceMap->table("Duration Table", reader.range(layout.offset + 0x10, 0x40))
        .kind("hosa-duration-table")
        .owner(ObjectRefs::sequence(id));
    sourceMap->table("Track Pointers", reader.range(layout.offset + 0x50, layout.tracks.size() * 2))
        .kind("hosa-track-pointers")
        .owner(ObjectRefs::sequence(id));
  }

  for (u32 i = 0; i < layout.tracks.size(); ++i) {
    auto track = decodeTrack(reader, id, i, layout.tracks[i].offset, layout.tracks[i].end, runtime, sourceMap,
                             diagnostics);
    track.sourceTrackNumber = i;
    sequence.tracks.push_back(std::move(track));
  }
  sequence.runtime = makeCompiledRuntime<Cursor, ProgramState>(std::move(runtime));
  return sequence;
}

}  // namespace vgmtrans::formats::hosa
