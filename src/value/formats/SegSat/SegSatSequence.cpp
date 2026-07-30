/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SegSat/SegSat.h"

#include "value/base/LevelScale.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandDialect.h"

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

struct ProgramState {
  explicit ProgramState(const SequenceProgram&) {}

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
  }
};

struct TrackState {
  explicit TrackState(const TrackProgram& program)
      : channel(program.id.value == 0 ? 0 : static_cast<u8>(program.id.value - 1)) {}

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
        case 10:
          delayed.pan(std::clamp((value / 63.5) - 1.0, -1.0, 1.0));
          break;
        case 32:
          track.bank = value;
          // The source command changes one register, but downstream targets
          // need the complete effective selection. Emitting it atomically also
          // activates the new bank when the program number itself is unchanged.
          delayed.instrument(InstrumentPerformanceEvent{
              .sourceInstrument = segSatInstrumentIdentity(track.bank, track.program),
          });
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
    if (channel == track.channel) {
      track.program = encodedProgram & 0x7f;
      out.at(eventTick(delta))
          .instrument(InstrumentPerformanceEvent{
              .sourceInstrument = segSatInstrumentIdentity(track.bank, track.program),
          });
    }
    return afterEvent(delta);
  }

  Effects channelPressure(u8, u16 delta) { return afterEvent(delta); }

  Effects pitchBend(u8 channel, u8 encoded, u16 delta) {
    if (channel == track.channel) {
      // Bit 7 is not part of the Saturn driver's seven-bit bend magnitude.
      // Legacy happened to discard it later when serializing the 14-bit MIDI
      // value; mask it here so the performance event remains a physical
      // ±2-semitone bend instead of an exporter-dependent overflow.
      const s16 bend = static_cast<s16>((static_cast<s32>(encoded & 0x7f) << 7) - 8192);
      out.at(eventTick(delta)).pitchBend((bend / 8192.0) * 2.0);
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
  const u32 delta = (static_cast<u32>(event.opcodeValue("delta_high", cursor.opcode())) << 24) |
                    (static_cast<u32>(event.u8("delta_mid_high")) << 16) |
                    (static_cast<u32>(event.u8("delta_mid_low")) << 8) | event.u8("delta_low");
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
    const u8 channel = event.opcodeBits<0, 4>("channel");
    const u16 durationHigh = static_cast<u16>(event.opcodeBits<6, 1>("duration_high")) << 8;
    const u16 deltaHigh = static_cast<u16>(event.opcodeBits<5, 1>("delta_high")) << 8;
    event.opcodeBits<4, 1>("unknown_bit", SourceValueDisplay::Hex);
    const u8 key = event.u8("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    const u8 velocity = event.u8("velocity", SemanticOperandRole::Level);
    const u16 duration = durationHigh | event.u8("duration_low", SemanticOperandRole::Duration);
    const u16 delta = deltaHigh | event.u8("delta_low", SemanticOperandRole::Duration);
    return event.invoke<&Playback::note>(channel, key, velocity, duration, delta).runtimeControlFlow();
  }

  if ((status & 0xf0) == 0xb0) {
    auto event = cursor.command("Controller", SequenceSemantic::State);
    const u8 channel = event.opcodeBits<0, 4>("channel");
    const u8 controller = event.u8("controller");
    const u8 encoded = event.u8("encoded_value");
    const auto valueRole = controller == 32 ? SemanticOperandRole::InstrumentBank : SemanticOperandRole::Level;
    const u8 value = event.derived("value", static_cast<u8>(encoded & 0x7f), valueRole);
    const u16 delta = static_cast<u16>(((encoded & 0x80) << 1) | event.u8("delta_low"));
    return event.invoke<&Playback::controller>(channel, controller, value, delta).runtimeControlFlow();
  }

  if ((status & 0xf0) == 0xc0) {
    auto event = cursor.command("Program", SequenceSemantic::Program);
    const u8 channel = event.opcodeBits<0, 4>("channel");
    const u8 program = event.u8("program", SemanticOperandRole::InstrumentProgram);
    const u16 delta = event.u8("delta", SemanticOperandRole::Duration);
    return event.invoke<&Playback::programChange>(channel, program, delta).runtimeControlFlow();
  }

  if ((status & 0xf0) == 0xd0) {
    auto event = cursor.command("Channel Pressure", SequenceSemantic::State);
    event.opcodeBits<0, 4>("channel");
    const u8 pressure = event.u8("pressure");
    const u16 delta = event.u8("delta", SemanticOperandRole::Duration);
    return event.invoke<&Playback::channelPressure>(pressure, delta).runtimeControlFlow();
  }

  if ((status & 0xf0) == 0xe0) {
    auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
    const u8 channel = event.opcodeBits<0, 4>("channel");
    const u8 bend = event.u8("bend", SemanticOperandRole::Pitch);
    const u16 delta = event.u8("delta", SemanticOperandRole::Duration);
    return event.invoke<&Playback::pitchBend>(channel, bend, delta).runtimeControlFlow();
  }

  switch (status) {
    case 0x81: {
      auto event = cursor.command("Counted Event Loop", SequenceSemantic::Repeat);
      const u16 relative = event.u16be("relative", SourceValueDisplay::Address);
      const Address destination = event.derived("destination", Address{normalStart + relative},
                                                SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      const u8 count = event.u8("event_count", SemanticOperandRole::Count);
      const Address continuation = event.nextAddress();
      return event.invoke<&Playback::beginCountedLoop>(destination, count, continuation)
          .mayBranchTo(destination)
          .runtimeControlFlow();
    }
    case 0x82: {
      auto event = cursor.command("Forever Loop", SequenceSemantic::Loop);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      const Address continuation = event.nextAddress();
      return event.invoke<&Playback::foreverLoop>(delta, continuation).runtimeControlFlow();
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

void applySegSatVelocityTables(PerformanceSequence& performance, std::span<const SegSatVelocityBank> banks) {
  for (auto& track : performance.tracks) {
    u8 selectedBank = 0;
    u8 selectedProgram = 0;
    for (auto& event : track.events) {
      if (const auto* selection = std::get_if<InstrumentPerformanceEvent>(&event);
          selection != nullptr && selection->sourceInstrument &&
          selection->sourceInstrument->domain == kSegSatInstrumentDomain) {
        selectedBank = static_cast<u8>((selection->sourceInstrument->key >> 8) & 0xff);
        selectedProgram = static_cast<u8>(selection->sourceInstrument->key & 0xff);
        continue;
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
      const u8 velocity =
          segSatMidiVelocity(sourceVelocity, bank->tables[region->table], region->totalLevel, instrument.volumeBias);
      note->linearVelocity = LevelScale::linearFromMidi7(velocity);
    }
  }
}

const SequenceDialect& segSatSequenceDialect() {
  static const SequenceDialect dialect = makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{std::string(kSegSatSequenceDialectId)},
      .commandDetailKindPrefix = "segsat",
      .timebase = Timebase{.ppqn = 48},
      .defaultBehavior =
          SequenceProgramBehavior{
              .commandLimit = 1048576,
              // Sequence pan commands are already MIDI-style equal-power
              // positions; SCSP layer output uses its own constant-sum law.
              .panLaw = PanLaw::EqualPower,
          },
  });
  return dialect;
}

SequenceProgram parseSegSatSequenceProgram(ByteReader reader, AssetId id, const SegSatSequenceLayout& layout,
                                           SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  SequenceProgram program = segSatSequenceDialect().makeProgram(Address{layout.offset});
  program.timebase.ppqn = layout.ppqn;
  program.behavior.commandLimit = 1048576;
  program.behavior.panLaw = PanLaw::EqualPower;

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
      .sequenceAsset = id,
      .sourceMap = sourceMap,
  };
  const u32 tempoStart = layout.offset + 8;
  TrackProgram tempo{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .startAddress = Address{tempoStart},
  };
  if (layout.tempoEventCount != 0) {
    tempo = tracks.linear(0, tempoStart, [&](u32 offset) {
      const u32 index = (offset - tempoStart) / 8;
      return decodeTempo(reader, offset, layout.offset + layout.normalTrack, index + 1 >= layout.tempoEventCount,
                         diagnostics);
    });
  }
  tempo.sourceTrackNumber = 0;
  program.tracks.push_back(std::move(tempo));

  const u32 normalStart = layout.offset + layout.normalTrack;
  auto normal = tracks.reachable(
      1, normalStart, [&](u32 offset) { return decodeNormal(reader, offset, layout.end, normalStart, diagnostics); });
  normal.sourceTrackNumber = 0;
  program.tracks.push_back(normal);
  for (u32 channel = 1; channel < 16; ++channel) {
    TrackProgram copy = normal;
    copy.id = TrackId{channel + 1};
    copy.sourceTrackNumber = channel;
    program.tracks.push_back(std::move(copy));
  }

  if (sourceMap != nullptr && header) {
    sourceMap
        ->pointer("Normal Track Pointer", reader.range(layout.offset + 4, 2),
                  SourceTarget{reader.range(normalStart, 1)})
        .kind("segsat-normal-track-pointer")
        .owner(ObjectRefs::sequenceTrack(id, 1))
        .field("stored_destination", reader.range(layout.offset + 4, 2), layout.normalTrack,
               SourceValueDisplay::Address)
        .derived("destination", normalStart, SourceValueDisplay::Address)
        .parent(*header);
  }
  return program;
}

}  // namespace vgmtrans::formats::segsat
