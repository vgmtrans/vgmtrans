/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/OhoriAkaPS1/OhoriAkaPS1.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::ohori_aka_ps1 {

using namespace core;

namespace {

constexpr u32 kPpqn = 48;
constexpr u32 kMaxCommands = 262144;
constexpr double kSpuFullScale = 16383.0;
constexpr std::array<u16, 16> kVibratoWaveLength{8, 2, 7, 3, 7, 3, 156, 39, 64, 16, 80, 20, 256, 256, 40, 8};
constexpr std::array<u16, 16> kVibratoWavePeak{32767, 32767, 32767, 32767, 32767, 32767, 32768, 32768,
                                              32767, 32767, 32768, 32768, 32767, 32623, 31190, 31173};
constexpr std::array<LfoWaveform, 16> kVibratoWaveform{
    LfoWaveform::SawtoothUp, LfoWaveform::Square, LfoWaveform::SawtoothUp, LfoWaveform::Square,
    LfoWaveform::SawtoothDown, LfoWaveform::Square, LfoWaveform::Sine, LfoWaveform::Sine,
    LfoWaveform::Triangle, LfoWaveform::Triangle, LfoWaveform::Sine, LfoWaveform::Sine,
    LfoWaveform::Noise, LfoWaveform::Noise, LfoWaveform::Sine, LfoWaveform::Sine,
};
constexpr std::array<u8, 32> kControlParameters{
    0, 1, 1, 1, 1, 1, 1, 2, 4, 1, 2, 0, 0, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

struct TrackLayout {
  std::optional<Address> loopPoint;
  std::map<u32, Address> loops;
};

[[nodiscard]] std::optional<u32> skipVariable(ByteReader reader, u32 cursor, u32 end) {
  if (cursor >= end || !reader.has(cursor, 1)) return std::nullopt;
  if ((reader.u8At(cursor) & 0x80) == 0) return cursor + 1;
  return cursor + 1 < end ? std::optional<u32>{cursor + 2} : std::nullopt;
}

[[nodiscard]] std::optional<u32> encodedEnd(ByteReader reader, u32 cursor, u32 end) {
  if (cursor >= end || !reader.has(cursor, 1)) return std::nullopt;
  const u8 status = reader.u8At(cursor++);
  if (status < 0x80) {
    if (cursor >= end) return std::nullopt;
    const u8 note = reader.u8At(cursor++);
    if ((status & 0x60) == 0x40) {
      const auto next = skipVariable(reader, cursor, end);
      if (!next) return std::nullopt;
      cursor = *next;
    } else if ((status & 0x60) == 0x60) {
      ++cursor;
    }
    if ((status & 0x1f) == 0x1f) {
      const auto next = skipVariable(reader, cursor, end);
      if (!next) return std::nullopt;
      cursor = *next;
    }
    if ((note & 0x80) != 0) ++cursor;
    return cursor <= end ? std::optional<u32>{cursor} : std::nullopt;
  }
  if ((status & 0x60) == 0x20) return cursor;  // relative note
  cursor += kControlParameters[status & 0x1f];
  if ((status & 0x60) == 0x40) {
    const auto next = skipVariable(reader, cursor, end);
    if (!next) return std::nullopt;
    cursor = *next;
  } else if ((status & 0x60) == 0x60) {
    ++cursor;
  }
  return cursor <= end ? std::optional<u32>{cursor} : std::nullopt;
}

[[nodiscard]] TrackLayout analyzeTrack(ByteReader reader, u32 start, u32 end) {
  TrackLayout layout;
  for (u32 offset = start; offset < end;) {
    const auto next = encodedEnd(reader, offset, end);
    if (!next || *next <= offset) break;
    const u8 status = reader.u8At(offset);
    if (status >= 0x80 && (status & 0x60) != 0x20 && (status & 0x1f) == 9) {
      if (reader.u8At(offset + 1) == 0) {
        layout.loopPoint = Address{*next};
      } else if (layout.loopPoint) {
        layout.loops.emplace(offset, *layout.loopPoint);
      }
    }
    offset = *next;
    if (status >= 0x80 && (status & 0x60) != 0x20 && (status & 0x1f) == 0) break;
  }
  return layout;
}

struct RuntimeConfig {
  std::array<u16, 32> durations{};
  u16 leftGain = 0x3fff;
  u16 rightGain = 0x3fff;
  std::vector<OhoriAkaPs1Instrument> instruments;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config) : instruments(config.instruments) {}

  [[nodiscard]] const OhoriAkaPs1Region* region(u8 program, u8 key) const {
    const auto instrument = std::ranges::find_if(instruments, [&](const auto& item) { return item.program == program; });
    if (instrument == instruments.end()) return nullptr;
    const auto region = std::ranges::find_if(instrument->regions, [&](const auto& item) {
      return key >= item.keyLow && key <= item.keyHigh;
    });
    return region == instrument->regions.end() ? nullptr : &*region;
  }

  std::span<const OhoriAkaPs1Instrument> instruments;
};

struct TrackSnapshot {
  u16 delta = 0;
  u16 noteDelta = 0;
  u16 duration = 0;
  u8 velocity = 127;
  u8 note = 60;
  u8 program = 0;
  u8 volume = 127;
  u8 expression = 127;
  u8 pan = 64;
  bool portamento = false;
  u8 portamentoTime = 0;
  std::array<std::optional<u8>, 5> adsrOverrides{};
};

struct TrackState : TrackSnapshot {
  explicit TrackState(const RuntimeConfig& config) : leftGain(config.leftGain), rightGain(config.rightGain) {}
  bool initialized = false;
  u16 leftGain = 0x3fff;
  u16 rightGain = 0x3fff;
  const OhoriAkaPs1Region* region = nullptr;
  std::optional<TrackSnapshot> loopSnapshot;
  bool hasPreviousNote = false;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void beforeCommand() {
    if (track.initialized) return;
    track.initialized = true;
    out.instrument(ohoriAkaPs1InstrumentIdentity(track.program), InstrumentEnvelopeMode::UseInstrumentEnvelope);
    emitLevel();
    emitBalance();
  }

  void emitLevel() { out.level((track.volume / 127.0) * (track.expression / 127.0)); }

  void emitBalance() {
    const u8 pan = track.region != nullptr && track.region->panOverride ? *track.region->panOverride : track.pan;
    out.stereoBalance((track.leftGain / kSpuFullScale) * ((127 - std::min<u8>(pan, 127)) / 127.0),
                      (track.rightGain / kSpuFullScale) * (std::min<u8>(pan, 127) / 127.0));
  }

  void loadRegion(u8 key) {
    track.region = program.region(track.program, key);
    if (track.region == nullptr) return;
    u16 adsr1 = track.region->adsr1;
    u16 adsr2 = track.region->adsr2;
    if (track.adsrOverrides[0]) adsr1 = static_cast<u16>((adsr1 & ~0x7f00u) | ((*track.adsrOverrides[0] & 0x7f) << 8));
    if (track.adsrOverrides[1]) adsr1 = static_cast<u16>((adsr1 & ~0x00f0u) | ((*track.adsrOverrides[1] & 0x0f) << 4));
    if (track.adsrOverrides[2]) adsr2 = static_cast<u16>((adsr2 & ~0x1fc0u) | ((*track.adsrOverrides[2] & 0x7f) << 6));
    if (track.adsrOverrides[3]) adsr1 = static_cast<u16>((adsr1 & ~0x000fu) | (*track.adsrOverrides[3] & 0x0f));
    if (track.adsrOverrides[4]) adsr2 = static_cast<u16>((adsr2 & ~0x001fu) | (*track.adsrOverrides[4] & 0x1f));
    out.replaceEnvelope(psxSpuEnvelope(adsr1, adsr2), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    out.reverb(track.region->reverb ? 1.0 : 0.0);
    emitBalance();
  }

  Effects note(u8 key, u16 duration, u16 deltaUpdate, bool deltaEqualsDuration, u8 velocityUpdate) {
    const u8 previousKey = track.note;
    track.note = key & 0x7f;
    track.duration = duration;
    if (deltaEqualsDuration) track.delta = duration;
    if (deltaUpdate != 0xffff) track.delta = deltaUpdate;
    track.noteDelta = track.delta;
    if (velocityUpdate != 0xff) track.velocity = velocityUpdate;
    loadRegion(track.note);
    const PerformanceNoteId note = out.note(track.note, track.velocity / 127.0, duration);
    if (track.portamento && track.hasPreviousNote && track.portamentoTime != 0 && previousKey != track.note) {
      out.pitchSlide(note, previousKey, track.note, track.portamentoTime).requirePortamento();
    }
    track.hasPreviousNote = true;
    return Effects::wait(track.delta);
  }

  Effects relativeNote(bool ascending, u8 distance) {
    const u8 previousKey = track.note;
    track.note = static_cast<u8>(ascending ? track.note + distance : track.note - distance);
    track.delta = track.noteDelta;
    loadRegion(track.note);
    const PerformanceNoteId note = out.note(track.note, track.velocity / 127.0, track.duration);
    if (track.portamento && track.hasPreviousNote && track.portamentoTime != 0 && previousKey != track.note) {
      out.pitchSlide(note, previousKey, track.note, track.portamentoTime).requirePortamento();
    }
    track.hasPreviousNote = true;
    return Effects::wait(track.delta);
  }

  Effects controlWait(u16 update) {
    if (update != 0xffff) track.delta = update;
    return Effects::wait(track.delta);
  }

  void tempo(u8 value) {
    if (value != 0) out.tempo(static_cast<u32>(std::lround(60000000.0 / value)));
  }
  void timeSignature(u8 value) { out.timeSignature(static_cast<u8>((value >> 4) + 1), static_cast<u8>((value & 15) + 1), kPpqn); }
  void programChange(u8 value) {
    track.program = value;
    track.adsrOverrides.fill(std::nullopt);
    out.instrument(ohoriAkaPs1InstrumentIdentity(value), InstrumentEnvelopeMode::UseInstrumentEnvelope);
  }
  void volume(u8 value) { track.volume = value; emitLevel(); }
  void pan(u8 value) { track.pan = std::min<u8>(value, 127); emitBalance(); }
  void expression(u8 value) { track.expression = value; emitLevel(); }
  void autoPan(u8 depth, u8 halfPeriod) {
    const double rate = 1.0 / (4.0 * std::max<u8>(halfPeriod, 1));
    LfoPerformanceContext context{
        .cyclesPerTick = rate,
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .restartMode = LfoRestartMode::PhaseAndDelay,
        .panLaw = PanLaw::ConstantSum,
    };
    out.panLfoDepth(std::min(2.0, 2.0 * std::min<u8>(depth, 127) / 127.0), context);
    out.panLfoRateCyclesPerTick(rate, std::move(context));
  }
  void vibrato(u8 depth, u8 rate, u8 waveform, u8 delay) {
    const u8 wave = waveform & 15;
    const double scaledDepth = (depth & 0x80) != 0 ? (depth & 0x7f) * 12.0 : depth;
    const double semitones = scaledDepth * kVibratoWavePeak[wave] / 4194304.0;
    const double cycles = 1.0 / (kVibratoWaveLength[wave] * (rate == 0 ? 256.0 : rate));
    LfoPerformanceContext context{
        .cyclesPerTick = cycles,
        .delayTicks = delay,
        .delayIsTempoRelative = true,
        .shape = LfoShape{.waveform = kVibratoWaveform[wave]},
        .restartMode = LfoRestartMode::PhaseAndDelay,
    };
    out.vibratoDepth(semitones, context);
    out.vibratoRateCyclesPerTick(cycles, std::move(context));
  }
  void pitchBend(u8 low, u8 high) { out.pitchBend((static_cast<int>(low) + static_cast<int>(high) * 128 - 0x2000) / 682.0); }
  void portamentoOn(u8 duration) { track.portamento = true; track.portamentoTime = duration; out.portamentoEnable(true); }
  void portamentoOff() { track.portamento = false; out.portamentoEnable(false); }
  void adsr(u8 field, u8 value) {
    track.adsrOverrides[field] = value;
    if (track.region != nullptr) loadRegion(track.note);
  }
  void loopMarker() { track.loopSnapshot = static_cast<const TrackSnapshot&>(track); }
  void restoreLoop() {
    if (track.loopSnapshot) static_cast<TrackSnapshot&>(track) = *track.loopSnapshot;
    track.region = nullptr;
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end,
                                                   const RuntimeConfig& config, const TrackLayout& layout,
                                                   std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, end, kOhoriAkaPs1CommandKindPrefix, diagnostics);
  if (!cursor.hasOpcode()) return cursor.truncated();
  const u8 status = cursor.opcode();

  auto readVariable = [](auto& event, std::string_view name, SemanticOperandRole role) -> u16 {
    const u8 first = event.u8(name, SourceValueDisplay::Hex, role);
    if ((first & 0x80) == 0) return first;
    const u8 second = event.u8("value_low", SourceValueDisplay::Hex, role);
    return static_cast<u16>(((first & 0x7f) << 7) | (second & 0x7f));
  };
  auto readTiming = [&](auto& event) -> u16 {
    if ((status & 0x60) == 0x40) return readVariable(event, "delta", SemanticOperandRole::Duration);
    if ((status & 0x60) == 0x60) {
      const u8 index = event.u8("delta_index", SourceValueDisplay::Hex, SemanticOperandRole::Duration) & 0x1f;
      return config.durations[index];
    }
    return 0xffff;
  };

  if (status < 0x80) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 encodedNote = event.u8("note", SourceValueDisplay::Hex, SemanticOperandRole::NoteKey);
    const u8 note = encodedNote & 0x7f;
    const u16 delta = readTiming(event);
    const u8 durationIndex = status & 0x1f;
    const u16 duration = durationIndex == 0x1f ? readVariable(event, "duration", SemanticOperandRole::Duration)
                                               : config.durations[durationIndex];
    const u8 velocity = (encodedNote & 0x80) != 0
                            ? event.u8("velocity", SemanticOperandRole::Level)
                            : 0xff;
    return event.invoke<&Playback::note>(note, duration, delta, (status & 0x60) == 0x20, velocity);
  }
  if ((status & 0x60) == 0x20) {
    auto event = cursor.command("Relative Note", SequenceSemantic::Note);
    event.derived("direction", std::string((status & 0x10) != 0 ? "up" : "down"));
    event.derived("distance", status & 0x0f, SemanticOperandRole::Pitch);
    return event.invoke<&Playback::relativeNote>((status & 0x10) != 0, status & 0x0f);
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
      event.derived("denominator", static_cast<u8>((value & 15) + 1));
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
      auto event = cursor.command("Loop Marker", SequenceSemantic::Loop);
      const u8 mode = event.u8("mode");
      const u16 wait = readTiming(event);
      if (mode == 0) return event.invoke<&Playback::loopMarker>().invoke<&Playback::controlWait>(wait);
      const auto found = layout.loops.find(begin);
      if (found == layout.loops.end()) return event.invoke<&Playback::controlWait>(wait);
      event.derived("destination", found->second, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      return event.invoke<&Playback::restoreLoop>().loopCandidate(found->second);
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
      static constexpr std::array labels{"Attack Rate", "Decay Rate", "Sustain Rate", "Sustain Level", "Release Rate"};
      auto event = cursor.command(labels[command - 16], SequenceSemantic::Envelope);
      const u8 value = event.u8("value");
      return event.invoke<&Playback::adsr>(command - 16, value).invoke<&Playback::controlWait>(readTiming(event));
    }
    case 21: {
      auto event = cursor.command("Voice Allocation Class", SequenceSemantic::State, CommandPlaybackStatus::SourceOnly);
      event.u8("class");
      return event.invoke<&Playback::controlWait>(readTiming(event));
    }
    default: {
      auto event = cursor.command(command == 13 ? "Driver Parameter" : "Driver No-op", SequenceSemantic::Meta,
                                  CommandPlaybackStatus::NoOp);
      for (u8 i = 0; i < kControlParameters[command]; ++i) event.u8("parameter", SourceValueDisplay::Hex);
      return event.invoke<&Playback::controlWait>(readTiming(event));
    }
  }
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, AssetId sequence, u32 trackIndex, u32 start, u32 end,
                                       const RuntimeConfig& config, SourceMapBuilder* sourceMap,
                                       std::vector<Diagnostic>* diagnostics) {
  const TrackLayout layout = analyzeTrack(reader, start, end);
  const TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = end,
      .maxCommands = kMaxCommands,
      .sequenceAsset = sequence,
      .sourceMap = sourceMap,
  };
  TrackProgram track = tracks.decode(
      trackIndex, start, [&](u32 offset) { return decodeCommand(reader, offset, end, config, layout, diagnostics); });
  if (!track.commands.empty()) {
    auto& last = track.commands.back();
    if (last.flow.defaultTransition.kind == CommandTransitionKind::Fallthrough &&
        last.flow.continuation.value == end) {
      last.flow = CommandFlow::end(Address{end});
    }
  }
  return track;
}

}  // namespace

const SequenceProgramConfig& ohoriAkaPs1SequenceConfig() {
  static const SequenceProgramConfig config{
      .commandKindPrefix = std::string(kOhoriAkaPs1CommandKindPrefix),
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior = SequenceProgramBehavior{.commandLimit = kMaxCommands, .panLaw = PanLaw::ConstantSum, .initialLevel = 1.0},
  };
  return config;
}

SequenceProgram parseOhoriAkaPs1Sequence(ByteReader reader, AssetId id, const OhoriAkaPs1SequenceLayout& layout,
                                         const std::vector<OhoriAkaPs1Instrument>& instruments,
                                         SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  SequenceProgram sequence = ohoriAkaPs1SequenceConfig().makeProgram();
  RuntimeConfig runtime{.leftGain = layout.leftGain, .rightGain = layout.rightGain, .instruments = instruments};
  std::ranges::copy(layout.durations, runtime.durations.begin());
  sequence.runtime = makeCompiledRuntime<Cursor, ProgramState>(runtime);

  if (sourceMap != nullptr) {
    sourceMap->header("OhoriAkaPS1 Sequence Header", reader.range(layout.offset, 0x50))
        .kind("ohori-aka-ps1-sequence-header")
        .owner(ObjectRefs::sequence(id))
        .field("signature", reader.range(layout.offset, 5), "HOSAV")
        .field("version", reader.range(layout.offset + 5, 1), layout.version)
        .field("track_count", reader.range(layout.offset + 6, 1), layout.trackCount)
        .field("reverb_mode", reader.range(layout.offset + 7, 1), layout.reverbMode)
        .field("reverb_depth", reader.range(layout.offset + 8, 2), layout.reverbDepth)
        .field("reverb_delay", reader.range(layout.offset + 10, 1), layout.reverbDelay)
        .field("reverb_feedback", reader.range(layout.offset + 11, 1), layout.reverbFeedback)
        .field("left_gain", reader.range(layout.offset + 12, 2), layout.leftGain)
        .field("right_gain", reader.range(layout.offset + 14, 2), layout.rightGain);
    sourceMap->table("Duration Table", reader.range(layout.offset + 0x10, 0x40))
        .kind("ohori-aka-ps1-duration-table")
        .owner(ObjectRefs::sequence(id));
    sourceMap->table("Track Pointers", reader.range(layout.offset + 0x50, layout.trackCount * 2))
        .kind("ohori-aka-ps1-track-pointers")
        .owner(ObjectRefs::sequence(id));
  }

  for (u32 i = 0; i < layout.trackAddresses.size(); ++i) {
    auto track = decodeTrack(reader, id, i, layout.trackAddresses[i], layout.trackEnds[i], runtime, sourceMap, diagnostics);
    track.sourceTrackNumber = i;
    sequence.tracks.push_back(std::move(track));
  }
  return sequence;
}

}  // namespace vgmtrans::formats::ohori_aka_ps1
