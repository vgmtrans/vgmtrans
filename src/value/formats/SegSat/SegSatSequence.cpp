/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SegSat/SegSat.h"

#include "value/base/LevelScale.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace vgmtrans::formats::segsat {

using namespace core;

namespace {

constexpr double kScspTlDb = 0.37529;

struct ChannelLevel {
  u8 volume = 127;
  u8 expression = 127;
};

struct VoiceLevel {
  u8 velocity = 0;
  u8 totalLevel = 0;
  s8 volumeBias = 0;
};

[[nodiscard]] u8 vlVelocity(u8 velocity, const SegSatVlTable& table) {
  u8 point = 0;
  u8 base = 0;
  u8 rate = table.rate0;
  if (velocity > table.point0) {
    point = table.point0;
    base = table.level0;
    rate = table.rate1;
    if (velocity > table.point1) {
      point = table.point1;
      base = table.level1;
      rate = table.rate2;
      if (velocity > table.point2) {
        point = table.point2;
        base = table.level2;
        rate = table.rate3;
      }
    }
  }

  const u8 margin = velocity - point;
  const u8 shift = rate >> 4;
  const bool onePointFive = (rate & 8) != 0;
  const u32 steep =
      onePointFive ? (((static_cast<u32>(margin & 0x7f) * 12) << shift) >> 3) : (static_cast<u32>(margin) << shift);
  u8 converted = base;
  switch (rate & 7) {
    case 1:
      converted = static_cast<u8>(converted + steep);
      break;
    case 2:
      converted = static_cast<u8>(converted + margin);
      break;
    case 3:
      converted = static_cast<u8>(converted + (margin >> shift));
      break;
    case 5:
      converted = static_cast<u8>(converted - (margin >> shift));
      break;
    case 6:
      converted = static_cast<u8>(converted - margin);
      break;
    case 7:
      converted = static_cast<u8>(converted - steep);
      break;
    default:
      break;
  }

  // The 68000 routine works in bytes. After wraparound, bit 6 distinguishes
  // positive overflow (0x80..0xbf) from negative underflow (0xc0..0xff).
  if (converted & 0x80) {
    converted = (converted & 0x40) ? 0 : 0x7f;
  }
  return converted;
}

[[nodiscard]] u8 driverAmplitude(SegSatVolumeModel model, VoiceLevel voice, ChannelLevel channel) {
  // Keep these calculations in integer form. Each driver rounds at different
  // points before writing the complemented result to the SCSP TL register.
  u32 amplitude = 0;
  switch (model) {
    case SegSatVolumeModel::V1_28: {
      const u32 voiceScale = static_cast<u32>(voice.velocity) * 2 * (static_cast<u32>(255) - voice.totalLevel);
      amplitude = (voiceScale * (static_cast<u32>(channel.volume) * 2)) >> 16;
      amplitude =
          static_cast<u32>(std::clamp(static_cast<int>(amplitude) + static_cast<int>(voice.volumeBias), 0, 255));
      break;
    }
    case SegSatVolumeModel::V1_33: {
      const u32 voiceScale = (static_cast<u32>(voice.velocity) + 1) * (static_cast<u32>(256) - voice.totalLevel);
      amplitude = ((voiceScale * (static_cast<u32>(channel.volume) + 1) * 4) - 1) >> 16;
      amplitude =
          static_cast<u32>(std::clamp(static_cast<int>(amplitude) + static_cast<int>(voice.volumeBias), 0, 255));
      break;
    }
    case SegSatVolumeModel::V2_20: {
      const u32 velocity = std::max<u32>(voice.velocity, 1);
      // Version 2.20 adds the instrument bias to the region level before
      // multiplying it by velocity.
      const u32 regionLevel = static_cast<u32>(
          std::clamp(255 - static_cast<int>(voice.totalLevel) + static_cast<int>(voice.volumeBias), 0, 255));
      const u32 voiceScale = (velocity * regionLevel) >> 8;
      const u32 channelScale = (static_cast<u32>(channel.volume) * channel.expression) >> 7;
      amplitude = (voiceScale * channelScale) >> 6;
      break;
    }
    case SegSatVolumeModel::V3_1: {
      const u32 voiceScale =
          ((((static_cast<u32>(voice.velocity) + 1) * (static_cast<u32>(256) - voice.totalLevel)) - 1) >> 8) + 1;
      const u32 channelScale =
          ((((static_cast<u32>(channel.volume) + 1) * (static_cast<u32>(channel.expression) + 1)) - 1) >> 7) + 1;
      amplitude = ((voiceScale * channelScale) - 1) >> 6;
      amplitude =
          static_cast<u32>(std::clamp(static_cast<int>(amplitude) + static_cast<int>(voice.volumeBias), 0, 255));
      break;
    }
  }
  return static_cast<u8>(std::min<u32>(amplitude, 255));
}

[[nodiscard]] double linearGain(u8 amplitude) {
  const u8 attenuation = static_cast<u8>(255 - amplitude);
  return std::pow(10.0, -(attenuation * kScspTlDb) / 20.0);
}

void updateChannelLevel(SegSatVolumeModel model, ChannelLevel& channel, u8 controller, u8 value) {
  if (model == SegSatVolumeModel::V1_28 || model == SegSatVolumeModel::V1_33) {
    // These drivers store volume and expression in the same byte. The most
    // recent command replaces the earlier one.
    channel.volume = value;
    return;
  }
  if (controller == 7) {
    channel.volume = value;
  } else {
    channel.expression = value;
  }
}

[[nodiscard]] const SegSatControllerChange* controllerChange(std::span<const SegSatControllerChange> changes,
                                                             CommandId command) {
  const auto found = std::ranges::lower_bound(changes, command.value, {}, &SegSatControllerChange::command);
  return found != changes.end() && found->command == command.value ? &*found : nullptr;
}

[[nodiscard]] std::vector<VoiceLevel> possibleVoices(std::span<const SegSatVelocityBank> banks) {
  std::vector<VoiceLevel> voices;
  for (const auto& bank : banks) {
    std::vector<u8> tableMaximums;
    tableMaximums.reserve(bank.tables.size());
    for (const auto& table : bank.tables) {
      u8 maximum = 0;
      for (u32 velocity = 0; velocity < 128; ++velocity) {
        maximum = std::max(maximum, vlVelocity(static_cast<u8>(velocity), table));
      }
      tableMaximums.push_back(maximum);
    }
    for (const auto& instrument : bank.instruments) {
      for (const auto& region : instrument.regions) {
        if (region.table < tableMaximums.size()) {
          voices.push_back(VoiceLevel{
              .velocity = tableMaximums[region.table],
              .totalLevel = region.totalLevel,
              .volumeBias = instrument.volumeBias,
          });
        }
      }
    }
  }
  return voices;
}

[[nodiscard]] double channelGain(SegSatVolumeModel model, ChannelLevel channel, std::span<const VoiceLevel> voices) {
  u8 loudest = 0;
  u8 loudestAtFullVolume = 0;
  for (const auto voice : voices) {
    loudest = std::max(loudest, driverAmplitude(model, voice, channel));
    loudestAtFullVolume =
        std::max(loudestAtFullVolume, driverAmplitude(model, voice, ChannelLevel{.volume = 127, .expression = 127}));
  }
  if (voices.empty()) {
    return 1.0;
  }
  // The instruments' own attenuation stays on note velocity. The controller
  // represents only the change from the driver's normal full-volume state.
  return std::min(linearGain(loudest) / linearGain(loudestAtFullVolume), 1.0);
}

struct ProgramState {
  explicit ProgramState(const SequenceProgram&) {}
  ProgramState(const SequenceProgram&, const SegSatRuntimeConfig& config) : config(config) {}

  void finalizePerformance(PerformanceSequence& performance) {
    if (performance.tracks.size() != 17) {
      return;
    }

    // Saturn stores one physical event stream whose status nibble selects one
    // of sixteen channels. Playback uses sixteen copies of that stream so each
    // copy retains ordinary single-track VM state. The extra track contains
    // the independent tempo stream; merge it into channel zero only after both
    // timelines have executed.
    PerformanceTrack tempo = std::move(performance.tracks.front());
    std::vector<PerformanceTrack> channels;
    channels.reserve(16);
    for (u32 channel = 0; channel < 16; ++channel) {
      PerformanceTrack track = std::move(performance.tracks[channel + 1]);
      track.id = TrackId{channel};
      track.sourceTrackNumber = channel;
      for (auto& event : track.events) {
        std::visit([&](auto& typed) { typed.header.track = track.id; }, event);
      }
      for (auto& automation : track.automations) {
        automation.header.track = track.id;
      }
      track.endTick = 0;
      for (const auto& event : track.events) {
        const auto& header = performanceEventHeader(event);
        u64 end = header.tick;
        if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
          end = note->header.tick > std::numeric_limits<u64>::max() - note->durationTicks
                    ? std::numeric_limits<u64>::max()
                    : note->header.tick + note->durationTicks;
        }
        track.endTick = std::max(track.endTick, end);
      }
      channels.push_back(std::move(track));
    }

    auto& first = channels.front();
    for (auto& event : tempo.events) {
      std::visit([](auto& typed) { typed.header.track = TrackId{0}; }, event);
      first.events.push_back(std::move(event));
    }
    first.endTick = std::max(first.endTick, tempo.endTick);
    std::ranges::stable_sort(first.events, [](const PerformanceEvent& left, const PerformanceEvent& right) {
      const auto& a = performanceEventHeader(left);
      const auto& b = performanceEventHeader(right);
      return std::pair{a.tick, a.sequence} < std::pair{b.tick, b.sequence};
    });
    performance.tracks = std::move(channels);
    if (config) {
      finalizeSegSatPerformance(performance, config->velocityBanks, config->volumeModel, config->controllerChanges);
    }
  }

  std::optional<SegSatRuntimeConfig> config;
};

struct TrackState {
  explicit TrackState(const TrackProgram& program) : channel(static_cast<u8>(program.sourceTrackNumber)) {}

  u8 channel = 0;
  u8 bank = 0;
  u8 program = 0;
  u32 durationExtension = 0;
  u32 remainingLoopEvents = 0;
  Address countedLoopEnd;
  std::optional<Address> foreverLoopStart;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;

  [[nodiscard]] u64 eventTick(u16 delta) const {
    return vm.tick() > std::numeric_limits<u64>::max() - delta ? std::numeric_limits<u64>::max() : vm.tick() + delta;
  }

  [[nodiscard]] Effects afterEvent(u16 delta) {
    Effects effects = Effects::wait(delta);
    if (track.remainingLoopEvents != 0 && --track.remainingLoopEvents == 0) {
      effects.flowOverride = vm.finiteBranch(track.countedLoopEnd).flowOverride;
    }
    return effects;
  }

  Effects tempo(u32 microsecondsPerQuarter, u32 delta) {
    out.tempo(microsecondsPerQuarter);
    return Effects::wait(delta);
  }

  Effects note(u8 channel, u8 key, u8 velocity, u16 duration, u16 delta) {
    duration = static_cast<u16>(duration + track.durationExtension);
    track.durationExtension = 0;
    if (channel == track.channel) {
      out.at(eventTick(delta)).note(key, LevelScale::linearFromMidi7(velocity), duration);
    }
    return afterEvent(delta);
  }

  Effects controller(u8 channel, u8 controller, u8 value, u16 delta) {
    if (channel == track.channel) {
      auto delayed = out.at(eventTick(delta));
      switch (controller) {
        case 1:
          delayed.modulation(ModulationPerformanceTarget::VibratoDepth, value / 127.0);
          break;
        case 7:
        case 11: {
          u8 attenuation = static_cast<u8>(std::max(0, 254 - 2 * value));
          if (attenuation != 0) {
            --attenuation;
          }
          const double midiAmplitude = std::pow(10.0, -(attenuation * 0.37529) / 40.0);
          const u8 converted = static_cast<u8>(std::clamp<long>(std::lround(midiAmplitude * 127.0), 0, 127));
          if (controller == 7) {
            delayed.level(LevelScale::linearFromMidi7(converted));
          } else {
            delayed.expression(LevelScale::linearFromMidi7(converted));
          }
          break;
        }
        case 10: {
          const u8 position = value >> 2;
          const u8 encoded = position < 16 ? static_cast<u8>(31 - position) : static_cast<u8>(position - 16);
          double attenuatedSide = 0.0;
          if ((encoded & 0x0f) != 0x0f) {
            attenuatedSide = std::pow(10.0, -(static_cast<double>(encoded & 0x0f) * 3.0) / 20.0);
          }
          if (encoded < 16) {
            delayed.stereoBalance(attenuatedSide, 1.0);
          } else {
            delayed.stereoBalance(1.0, attenuatedSide);
          }
        } break;
        case 32:
          track.bank = value;
          // The source command changes one register, but downstream targets
          // need the complete effective selection. Emitting it atomically also
          // activates the new bank when the program number itself is unchanged.
          delayed.instrument(segSatInstrumentIdentity(track.bank, track.program));
          break;
        case 91:
          delayed.reverb(value / 127.0);
          break;
        default:
          // The decoded command remains visible in the source map. The
          // target-neutral performance model intentionally has no raw,
          // destination-MIDI controller event for the remaining values.
          break;
      }
    }
    return afterEvent(delta);
  }

  Effects programChange(u8 channel, u8 encodedProgram, u16 delta) {
    // The driver rejects MIDI-like events whose first data byte has bit 7 set
    // (mm8audio.bin reads_from_seq at 0x580e). These bytes occur in real
    // streams and must not change the active instrument.
    if ((encodedProgram & 0x80) == 0 && channel == track.channel) {
      track.program = encodedProgram;
      out.at(eventTick(delta)).instrument(segSatInstrumentIdentity(track.bank, track.program));
    }
    return afterEvent(delta);
  }

  Effects channelPressure(u8, u16 delta) { return afterEvent(delta); }

  Effects pitchBend(u8 channel, u8 encoded, u16 delta) {
    if ((encoded & 0x80) == 0 && channel == track.channel) {
      const s16 bend = static_cast<s16>((static_cast<s32>(encoded) << 7) - 8192);
      const double wheelPosition = bend / 8192.0;
      out.at(eventTick(delta))
          .pitchBend(PitchBendPerformanceEvent{
              .semitones = wheelPosition * 2.0,
              .normalizedWheelPosition = wheelPosition,
          });
    }
    return afterEvent(delta);
  }

  Effects beginCountedLoop(Address destination, u8 eventCount, Address continuation) {
    if (track.remainingLoopEvents != 0) {
      vm.diagnostic(Diagnostic{
          .severity = Severity::Warning,
          .message = "Nested SegSat counted loop stopped playback",
      });
      return vm.end();
    }
    track.remainingLoopEvents = eventCount;
    track.countedLoopEnd = continuation;
    return vm.finiteBranch(destination);
  }

  Effects foreverLoop(u8 delta, Address continuation) {
    Effects effects = Effects::wait(delta);
    if (!track.foreverLoopStart) {
      track.foreverLoopStart = continuation;
      return effects;
    }
    effects.flowOverride = vm.declaredLoop(*track.foreverLoopStart).flowOverride;
    return effects;
  }
};

using SegSatCursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeTempo(ByteReader reader, u32 begin, u32 end, bool final,
                                                 std::vector<Diagnostic>* diagnostics) {
  SegSatCursor cursor(reader, begin, end, "segsat", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
  const u8 deltaHigh = event.opcodeValue("delta_high", cursor.opcode());
  const u8 deltaMidHigh = event.u8("delta_mid_high");
  const u8 deltaMidLow = event.u8("delta_mid_low");
  const u8 deltaLow = event.u8("delta_low");
  const u32 delta = (static_cast<u32>(deltaHigh) << 24) | (static_cast<u32>(deltaMidHigh) << 16) |
                    (static_cast<u32>(deltaMidLow) << 8) | deltaLow;
  const u32 tempo = event.u32be("microseconds_per_quarter");
  event.invoke<&Playback::tempo>(tempo, delta);
  return final ? event.stop() : static_cast<DecodedBytecodeCommand>(event);
}

[[nodiscard]] DecodedBytecodeCommand decodeNormal(ByteReader reader, u32 begin, u32 end, u32 normalStart,
                                                  std::vector<Diagnostic>* diagnostics) {
  SegSatCursor cursor(reader, begin, end, "segsat", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  const u8 status = cursor.opcode();
  if (status <= 0x7f) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 channel = event.opcodeBits<0, 4>("channel", SemanticOperandRole::Channel);
    const u16 durationHigh = static_cast<u16>(event.opcodeBits<6, 1>("duration_high")) << 8;
    const u16 deltaHigh = static_cast<u16>(event.opcodeBits<5, 1>("delta_high")) << 8;
    event.opcodeBits<4, 1>("unknown_bit", SourceValueDisplay::Hex);
    const u8 key = event.u8("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    const u8 velocity = event.u8("velocity", SemanticOperandRole::Level);
    const u16 duration = durationHigh | event.u8("duration_low", SemanticOperandRole::Duration);
    const u16 delta = deltaHigh | event.u8("delta_low", SemanticOperandRole::Duration);
    return event.invokeFlow<&Playback::note>(channel, key, velocity, duration, delta);
  }

  if ((status & 0xf0) == 0xb0) {
    auto event = cursor.command("Controller", SequenceSemantic::State);
    const u8 channel = event.opcodeBits<0, 4>("channel", SemanticOperandRole::Channel);
    const u8 controller = event.u8("controller");
    const u8 encoded = event.u8("encoded_value");
    const auto valueRole = controller == 32 ? SemanticOperandRole::InstrumentBank : SemanticOperandRole::Level;
    const u8 value = event.derived("value", static_cast<u8>(encoded & 0x7f), valueRole);
    const u16 delta = event.u8("delta", SemanticOperandRole::Duration);
    return event.invokeFlow<&Playback::controller>(channel, controller, value, delta);
  }

  if ((status & 0xf0) == 0xc0) {
    auto event = cursor.command("Program", SequenceSemantic::Program);
    const u8 channel = event.opcodeBits<0, 4>("channel", SemanticOperandRole::Channel);
    const u8 encodedProgram = event.u8("encoded_program");
    event.derived("program", static_cast<u8>(encodedProgram & 0x7f), SemanticOperandRole::InstrumentProgram);
    const u16 delta = event.u8("delta", SemanticOperandRole::Duration);
    return event.invokeFlow<&Playback::programChange>(channel, encodedProgram, delta);
  }

  if ((status & 0xf0) == 0xd0) {
    auto event = cursor.command("Channel Pressure", SequenceSemantic::State);
    event.opcodeBits<0, 4>("channel", SemanticOperandRole::Channel);
    const u8 pressure = event.u8("pressure");
    const u16 delta = event.u8("delta", SemanticOperandRole::Duration);
    return event.invokeFlow<&Playback::channelPressure>(pressure, delta);
  }

  if ((status & 0xf0) == 0xe0) {
    auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
    const u8 channel = event.opcodeBits<0, 4>("channel", SemanticOperandRole::Channel);
    const u8 encodedBend = event.u8("encoded_bend");
    event.derived("bend", static_cast<u8>(encodedBend & 0x7f), SemanticOperandRole::Pitch);
    const u16 delta = event.u8("delta", SemanticOperandRole::Duration);
    return event.invokeFlow<&Playback::pitchBend>(channel, encodedBend, delta);
  }

  switch (status) {
    case 0x81: {
      auto event = cursor.command("Counted Event Loop", SequenceSemantic::Repeat);
      const u16 relative = event.u16be("relative", SourceValueDisplay::Address);
      const Address destination = event.derived("destination", Address{normalStart + relative},
                                                SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      const u8 count = event.u8("event_count", SemanticOperandRole::Count);
      const Address continuation = event.nextAddress();
      return event.invoke<&Playback::beginCountedLoop>(destination, count, continuation).mayBranchTo(destination);
    }
    case 0x82: {
      auto event = cursor.command("Forever Loop", SequenceSemantic::Loop);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      const Address continuation = event.nextAddress();
      return event.invokeFlow<&Playback::foreverLoop>(delta, continuation);
    }
    case 0x83:
      return cursor.command("End", SequenceSemantic::End).end();
    case 0x87:
      return cursor.command("Extend Duration by 256", SequenceSemantic::State)
          .add<&TrackState::durationExtension>(256u);
    case 0x88:
      return cursor.command("Extend Duration by 512", SequenceSemantic::State)
          .add<&TrackState::durationExtension>(512u);
    case 0x89:
      return cursor.command("Extend Duration by 2048", SequenceSemantic::State)
          .add<&TrackState::durationExtension>(2048u);
    case 0x8a:
      return cursor.command("Extend Duration by 4096", SequenceSemantic::State)
          .add<&TrackState::durationExtension>(4096u);
    case 0x8b:
      return cursor.command("Extend Duration by 8192", SequenceSemantic::State)
          .add<&TrackState::durationExtension>(8192u);
    case 0x8c:
      return cursor.command("Rest 256", SequenceSemantic::Rest).wait(256u);
    case 0x8d:
      return cursor.command("Rest 512", SequenceSemantic::Rest).wait(512u);
    case 0x8e:
      return cursor.command("Rest 2048", SequenceSemantic::Rest).wait(2048u);
    case 0x8f:
      return cursor.command("Rest 4096", SequenceSemantic::Rest).wait(4096u);
    default:
      return cursor.ignored("Unknown 0x8x Command", 0, "unknown-8x");
  }
}

}  // namespace

SequenceRuntime segSatSequenceRuntime(SegSatRuntimeConfig config) {
  return makeCompiledRuntime<SegSatCursor, ProgramState>(std::move(config));
}

double segSatLinearGain(SegSatVolumeModel model, u8 velocity, const SegSatVlTable& table, u8 totalLevel, s8 volumeBias,
                        u8 volume, u8 expression) {
  return linearGain(driverAmplitude(model,
                                    VoiceLevel{
                                        .velocity = vlVelocity(velocity, table),
                                        .totalLevel = totalLevel,
                                        .volumeBias = volumeBias,
                                    },
                                    ChannelLevel{
                                        .volume = volume,
                                        .expression = expression,
                                    }));
}

double segSatRegionReferenceGain(SegSatVolumeModel model, const SegSatVlTable& table, u8 totalLevel, s8 volumeBias) {
  double gain = 0.0;
  for (u32 velocity = 0; velocity < 128; ++velocity) {
    gain = std::max(gain, segSatLinearGain(model, static_cast<u8>(velocity), table, totalLevel, volumeBias, 127, 127));
  }
  return gain;
}

u8 segSatMidiVelocity(u8 velocity, const SegSatVlTable& table, u8 totalLevel, s8 volumeBias) {
  const double gain = segSatLinearGain(SegSatVolumeModel::V1_33, velocity, table, totalLevel, volumeBias, 127, 127);
  return LevelScale::midi7FromLinear(gain);
}

std::vector<u8> segSatSequenceBanks(const SequenceProgram& program) {
  std::vector<u8> banks;
  if (program.tracks.size() > 1) {
    // Track zero is the tempo stream. Track one is the first copy of the normal
    // stream; the remaining tracks contain the same commands for other channels.
    for (const auto& command : program.tracks[1].commands) {
      const auto bank =
          std::ranges::find(command.operands, SemanticOperandRole::InstrumentBank, &SemanticOperand::role);
      if (bank == command.operands.end()) {
        continue;
      }
      const auto* value = std::get_if<u64>(&bank->value);
      if (value == nullptr || *value > std::numeric_limits<u8>::max()) {
        continue;
      }
      const u8 number = static_cast<u8>(*value);
      if (std::ranges::find(banks, number) == banks.end()) {
        banks.push_back(number);
      }
    }
  }

  if (banks.empty()) {
    banks.push_back(0);
  } else {
    // Collections attach banks in numeric order.
    std::ranges::sort(banks);
  }
  return banks;
}

std::vector<SegSatControllerChange> segSatControllerChanges(const SequenceProgram& program) {
  std::vector<SegSatControllerChange> changes;
  if (program.tracks.size() < 2) {
    return changes;
  }

  // Every channel is a copy of the same normal event stream, so command IDs
  // from the first copy also identify controller events on the other copies.
  const auto& commands = program.tracks[1].commands;
  for (u32 commandIndex = 0; commandIndex < commands.size(); ++commandIndex) {
    const auto& command = commands[commandIndex];
    const auto* controller = semanticOperand(command, "controller");
    const auto* value = semanticOperand(command, "value");
    if (controller == nullptr || value == nullptr) {
      continue;
    }
    const auto* controllerNumber = std::get_if<u64>(&controller->value);
    const auto* controllerValue = std::get_if<u64>(&value->value);
    if (controllerNumber == nullptr || controllerValue == nullptr || *controllerNumber > 127 ||
        *controllerValue > 127) {
      continue;
    }
    changes.push_back(SegSatControllerChange{
        .command = commandIndex,
        .controller = static_cast<u8>(*controllerNumber),
        .value = static_cast<u8>(*controllerValue),
    });
  }
  return changes;
}

void finalizeSegSatPerformance(PerformanceSequence& performance, std::span<const SegSatVelocityBank> banks,
                               SegSatVolumeModel model, std::span<const SegSatControllerChange> controllerChanges) {
  const std::vector<VoiceLevel> voices = possibleVoices(banks);
  for (auto& track : performance.tracks) {
    u8 selectedBank = 0;
    u8 selectedProgram = 0;
    ChannelLevel channel;
    double exportedChannelGain = 1.0;
    for (auto& event : track.events) {
      const PerformanceEventHeader header = performanceEventHeader(event);
      if (const auto* change = controllerChange(controllerChanges, header.sourceCommand);
          change != nullptr && (change->controller == 7 || change->controller == 11)) {
        updateChannelLevel(model, channel, change->controller, change->value);
        exportedChannelGain = channelGain(model, channel, voices);
        event = LevelPerformanceEvent{
            .header = header,
            .linearGain = exportedChannelGain,
            .sourceQuantization = ValueQuantization{.levels = 128},
        };
        continue;
      }

      if (const auto* selection = std::get_if<InstrumentPerformanceEvent>(&event);
          selection != nullptr && selection->sourceInstrument) {
        if (const auto address = decodeSegSatInstrumentIdentity(*selection->sourceInstrument)) {
          selectedBank = address->sourceBank;
          selectedProgram = address->program;
          continue;
        }
      }

      auto* note = std::get_if<NotePerformanceEvent>(&event);
      if (note == nullptr) {
        continue;
      }

      const SegSatVelocityBank* bank = nullptr;
      const auto selected = std::ranges::find(banks, selectedBank, &SegSatVelocityBank::sourceBank);
      if (selected != banks.end()) {
        bank = &*selected;
      } else if (banks.size() == 1) {
        // The driver and legacy matcher both fall back to the sole loaded bank
        // when the sequence names a bank that is not present in the SSF image.
        bank = &banks.front();
      }
      if (bank == nullptr || selectedProgram >= bank->instruments.size()) {
        continue;
      }

      const auto& instrument = bank->instruments[selectedProgram];
      const u8 key = static_cast<u8>(std::clamp<long>(std::lround(note->key), 0, 127));
      const auto region = std::ranges::find_if(instrument.regions, [&](const SegSatVelocityRegion& candidate) {
        return key >= candidate.keyLow && key <= candidate.keyHigh;
      });
      if (region == instrument.regions.end() || region->table >= bank->tables.size()) {
        continue;
      }

      const u8 sourceVelocity = LevelScale::midi7FromLinear(note->linearVelocity);
      const double exactGain = segSatLinearGain(model, sourceVelocity, bank->tables[region->table], region->totalLevel,
                                                instrument.volumeBias, channel.volume, channel.expression);
      // MIDI has one velocity for all of an instrument's layers. Each region
      // already stores its own maximum level, so velocity only describes how
      // far the note is below that maximum.
      note->linearVelocity =
          LevelScale::linearFromLinear(exportedChannelGain == 0.0 || region->referenceGain == 0.0
                                           ? 0.0
                                           : exactGain / (region->referenceGain * exportedChannelGain));
    }
  }
}

const SequenceProgramConfig& segSatSequenceConfig() {
  static const SequenceProgramConfig config = SequenceProgramConfig{
      .commandDetailKindPrefix = "segsat",
      .timebase = Timebase{.ppqn = 48},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = 1048576,
          },
  };
  return config;
}

SequenceProgram parseSegSatSequenceProgram(ByteReader reader, AssetId id, const SegSatSequenceLayout& layout,
                                           SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  SequenceProgram program = segSatSequenceConfig().makeProgram();
  program.timebase.ppqn = layout.ppqn;

  std::optional<SourceAnnotationId> header;
  if (sourceMap != nullptr) {
    auto annotation = sourceMap->header("SegSat Sequence Header", reader.range(layout.offset, 8))
                          .kind("segsat-sequence-header")
                          .owner(ObjectRefs::sequence(id))
                          .field("ppqn", reader.range(layout.offset, 2), layout.ppqn)
                          .field("tempo_event_count", reader.range(layout.offset + 2, 2), layout.tempoEventCount)
                          .field("normal_track_relative", reader.range(layout.offset + 4, 2), layout.normalTrack,
                                 SourceValueDisplay::Address)
                          .derived("normal_track", layout.offset + layout.normalTrack, SourceValueDisplay::Address)
                          .field("tempo_loop_relative", reader.range(layout.offset + 6, 2), layout.tempoLoop,
                                 SourceValueDisplay::Address)
                          .derived("tempo_loop", layout.offset + layout.tempoLoop, SourceValueDisplay::Address);
    header = annotation.id();
  }

  TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = layout.end,
      .maxCommands = 262144,
      .sourceHasTracks = false,
      .sequenceAsset = id,
      .sourceMap = sourceMap,
  };
  const u32 tempoStart = layout.offset + 8;
  TrackProgram tempo{
      .sourceTrackNumber = 0,
      .startAddress = Address{tempoStart},
  };
  if (layout.tempoEventCount != 0) {
    tempo = tracks.decode(0, tempoStart, [&](u32 offset) {
      const u32 index = (offset - tempoStart) / 8;
      return decodeTempo(reader, offset, layout.offset + layout.normalTrack, index + 1 >= layout.tempoEventCount,
                         diagnostics);
    });
  }
  tempo.sourceTrackNumber = 0;
  program.tracks.push_back(std::move(tempo));

  const u32 normalStart = layout.offset + layout.normalTrack;
  auto normal = tracks.decode(
      1, normalStart, [&](u32 offset) { return decodeNormal(reader, offset, layout.end, normalStart, diagnostics); });
  normal.sourceTrackNumber = 0;
  program.tracks.push_back(normal);
  for (u32 channel = 1; channel < 16; ++channel) {
    TrackProgram copy = normal;
    copy.sourceTrackNumber = channel;
    program.tracks.push_back(std::move(copy));
  }
  program.runtime = makeCompiledRuntime<SegSatCursor, ProgramState>();

  if (sourceMap != nullptr && header) {
    sourceMap
        ->pointer("Normal Track Pointer", reader.range(layout.offset + 4, 2),
                  SourceTarget{reader.range(normalStart, 1)})
        .kind("segsat-normal-track-pointer")
        .field("stored_destination", reader.range(layout.offset + 4, 2), layout.normalTrack,
               SourceValueDisplay::Address)
        .derived("destination", normalStart, SourceValueDisplay::Address)
        .parent(*header);
  }
  return program;
}

}  // namespace vgmtrans::formats::segsat
