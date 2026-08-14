/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS1/SonyPS1.h"

#include "value/base/LevelScale.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandDialect.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::sony_ps1 {

using namespace core;

namespace {

constexpr u32 kMaxCommands = 1048576;
constexpr u32 kOpenNoteDuration = std::numeric_limits<u32>::max();

struct ProgramState {
  explicit ProgramState(const SequenceProgram& sequence) {
    if (sequence.config.driverData.size() >= 2) {
      numerator = static_cast<u8>(sequence.config.driverData[0]);
      denominator = static_cast<u8>(sequence.config.driverData[1]);
    }
  }

  u8 numerator = 4;
  u8 denominator = 4;
};

struct ActiveNote {
  PerformanceNoteId id;
  u8 key = 0;
  bool released = false;
};

struct TrackState {
  explicit TrackState(const TrackProgram& program)
      : channel(static_cast<u8>(program.id.value)), program(static_cast<u8>(program.id.value)) {}

  u8 channel = 0;
  u8 bank = 0;
  u8 program = 0;
  u8 rpnMsb = 127;
  u8 rpnLsb = 127;
  u8 pitchBendRange = 2;
  bool sustain = false;
  bool initialized = false;
  std::vector<ActiveNote> activeNotes;
};

struct PanGains {
  double left = 1.0;
  double right = 1.0;
};

[[nodiscard]] PanGains psxPan(u8 raw) {
  const u8 pan = std::min<u8>(raw, 127);
  if (pan < 64) {
    const double right = pan / 64.0;
    return PanGains{.left = 1.0, .right = right * right};
  }
  const double left = (127 - pan) / 63.0;
  return PanGains{.left = left * left, .right = 1.0};
}

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& programState;

  void beforeCommand() {
    if (track.initialized) {
      return;
    }
    track.initialized = true;
    out.instrument(sonyPs1InstrumentIdentity(track.bank, track.program));
    if (track.channel == 0) {
      out.timeSignature(programState.numerator, programState.denominator, 24);
    }
  }

  [[nodiscard]] u64 eventTick(u32 delta) const {
    return vm.tick() > std::numeric_limits<u64>::max() - delta ? std::numeric_limits<u64>::max() : vm.tick() + delta;
  }

  [[nodiscard]] Effects after(u32 delta) const { return Effects::wait(delta); }

  void closeNote(ActiveNote& note, u64 tick) { static_cast<void>(out.setNoteEnd(note.id, tick)); }

  void releaseHeld(u64 tick) {
    for (auto& note : track.activeNotes) {
      if (note.released) {
        closeNote(note, tick);
      }
    }
    std::erase_if(track.activeNotes, [](const ActiveNote& note) { return note.released; });
  }

  void allNotesOff(u64 tick) {
    for (auto& note : track.activeNotes) {
      closeNote(note, tick);
    }
    track.activeNotes.clear();
  }

  Effects note(u8 channel, u8 key, u8 velocity, u32 delta) {
    if (channel != track.channel) {
      return after(delta);
    }
    const u64 tick = eventTick(delta);
    auto found = std::ranges::find(track.activeNotes, key, &ActiveNote::key);
    if (velocity == 0) {
      if (found != track.activeNotes.end()) {
        if (track.sustain) {
          found->released = true;
        } else {
          closeNote(*found, tick);
          track.activeNotes.erase(found);
        }
      }
      return after(delta);
    }
    if (found != track.activeNotes.end()) {
      closeNote(*found, tick);
      track.activeNotes.erase(found);
    }
    const PerformanceNoteId id =
        out.at(tick).note(key, LevelScale::linearFromMidi7(std::min<u8>(velocity, 127)), kOpenNoteDuration);
    track.activeNotes.push_back(ActiveNote{.id = id, .key = key});
    return after(delta);
  }

  Effects program(u8 channel, u8 value, u32 delta) {
    if (channel == track.channel && value < 128) {
      track.program = value;
      out.at(eventTick(delta)).instrument(sonyPs1InstrumentIdentity(track.bank, track.program));
    }
    return after(delta);
  }

  Effects controller(u8 channel, u8 controller, u8 value, u32 delta) {
    if (channel != track.channel) {
      return after(delta);
    }
    const u64 tick = eventTick(delta);
    auto delayed = out.at(tick);
    switch (controller) {
      case 0:
        track.bank = value;
        delayed.instrument(sonyPs1InstrumentIdentity(track.bank, track.program));
        break;
      case 1:
        delayed.modulation(ModulationPerformanceTarget::VibratoDepth, value / 127.0);
        break;
      case 6:
        if (track.rpnMsb == 0 && track.rpnLsb == 0) {
          track.pitchBendRange = value;
          delayed.pitchBendRange(value);
        }
        break;
      case 7:
        delayed.level(LevelScale::linearFromMidi7(value));
        break;
      case 10: {
        const PanGains pan = psxPan(value);
        delayed.stereoBalance(pan.left, pan.right);
        break;
      }
      case 11:
        delayed.expression(LevelScale::linearFromMidi7(value));
        break;
      case 64: {
        const bool enabled = value >= 64;
        if (track.sustain && !enabled) {
          releaseHeld(tick);
        }
        track.sustain = enabled;
        break;
      }
      case 91:
        delayed.reverb(value / 127.0);
        break;
      case 98:
        track.rpnMsb = 127;
        track.rpnLsb = 127;
        break;
      case 99:
        track.rpnMsb = 127;
        track.rpnLsb = 127;
        break;
      case 100:
        track.rpnLsb = value;
        break;
      case 101:
        track.rpnMsb = value;
        break;
      case 121:
        track.bank = 0;
        track.program = track.channel;
        track.pitchBendRange = 2;
        track.sustain = false;
        releaseHeld(tick);
        delayed.instrument(sonyPs1InstrumentIdentity(track.bank, track.program));
        delayed.level(1.0);
        delayed.expression(1.0);
        delayed.stereoBalance(1.0, 1.0);
        delayed.pitchBend(0.0);
        delayed.pitchBendRange(2);
        break;
      default:
        break;
    }
    return after(delta);
  }

  Effects pitchBend(u8 channel, u8 msb, u32 delta) {
    if (channel == track.channel) {
      // All three audited libsnd generations discard the MIDI LSB and use the
      // high seven bits as their signed wheel position.
      const double wheel = std::clamp((static_cast<int>(msb) - 64) / 64.0, -1.0, 1.0);
      out.at(eventTick(delta))
          .pitchBend(PitchBendPerformanceEvent{
              .semitones = wheel * track.pitchBendRange,
              .normalizedWheelPosition = wheel,
          });
    }
    return after(delta);
  }

  Effects tempo(u32 microsecondsPerQuarter, u32 delta) {
    if (track.channel == 0 && microsecondsPerQuarter != 0) {
      out.at(eventTick(delta)).tempo(microsecondsPerQuarter);
    }
    return after(delta);
  }

  Effects loopEnd(u8 count, Address destination, u32 delta) {
    Effects effects = after(delta);
    if (count == 127) {
      effects.flowOverride = vm.declaredLoop(destination).flowOverride;
    } else if (count > 1) {
      effects.flowOverride = vm.countedRepeatUntil(0, count, destination).flowOverride;
    }
    return effects;
  }

  Effects end(u32 delta) {
    allNotesOff(eventTick(delta));
    Effects effects = after(delta);
    effects.flowOverride = vm.end().flowOverride;
    return effects;
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] Cursor::Event beginEvent(Cursor& cursor, const SonyPs1EventLayout& source, std::string_view label,
                                       SequenceSemantic semantic,
                                       CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback) {
  auto event = cursor.command(label, semantic, playback);
  event.opcodeValue("delta_byte_0", cursor.opcode(), SourceValueDisplay::Hex, SemanticOperandRole::Duration);
  for (u32 i = 1; i < source.deltaSize; ++i) {
    event.u8("delta_byte", SourceValueDisplay::Hex, SemanticOperandRole::Duration);
  }
  event.derived("delta", source.delta, SemanticOperandRole::Duration);
  if (source.explicitStatus) {
    event.u8("status", SourceValueDisplay::Hex);
  } else {
    event.derived("running_status", source.status, SourceValueDisplay::Hex);
  }
  return event;
}

[[nodiscard]] DecodedBytecodeCommand decodeEvent(ByteReader reader, u32 begin, u32 end,
                                                 const SonyPs1EventLayout& source,
                                                 std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, end, kSonyPs1DialectId, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 family = source.status & 0xf0;
  const u8 channel = source.status & 0x0f;
  if (family == 0x90) {
    auto event = beginEvent(cursor, source, source.data2 == 0 ? "Note Off" : "Note On", SequenceSemantic::Note);
    const u8 key = event.u8("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    const u8 velocity = event.u8("velocity", SemanticOperandRole::Level);
    return event.invoke<&Playback::note>(channel, key, velocity, source.delta);
  }
  if (family == 0xc0) {
    auto event = beginEvent(cursor, source, "Program Change", SequenceSemantic::Program);
    return event.invoke<&Playback::program>(channel, event.u8("program", SemanticOperandRole::InstrumentProgram),
                                            source.delta);
  }
  if (family == 0xe0) {
    auto event = beginEvent(cursor, source, "Pitch Bend", SequenceSemantic::Pitch);
    event.u8("lsb", SemanticOperandRole::Pitch);
    const u8 msb = event.u8("msb", SemanticOperandRole::Pitch);
    event.derived("driver_wheel", static_cast<s16>((static_cast<int>(msb) - 64) * 128),
                  SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
    return event.invoke<&Playback::pitchBend>(channel, msb, source.delta);
  }
  if (family == 0xb0) {
    const bool loopStart = source.data1 == 99 && source.data2 == 20;
    const bool loopEnd = source.loopDestination.has_value();
    auto event = beginEvent(cursor, source,
                            loopEnd     ? "Loop End"
                            : loopStart ? "Loop Start"
                                        : "Controller",
                            loopEnd || loopStart ? SequenceSemantic::Loop : SequenceSemantic::State);
    const u8 controller = event.u8("controller");
    const auto role = controller == 0 ? SemanticOperandRole::InstrumentBank : SemanticOperandRole::Value;
    const u8 value = event.u8("value", role);
    if (loopStart) {
      event.derived("loop_start", Address{source.end}, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
    }
    if (loopEnd) {
      const Address destination{*source.loopDestination};
      event.derived("repeat_count", source.loopCount, SemanticOperandRole::Count);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      event.invoke<&Playback::loopEnd>(source.loopCount, destination, source.delta)
          .mayBranchTo(destination)
          .runtimeControlFlow();
      return event;
    }
    return event.invoke<&Playback::controller>(channel, controller, value, source.delta);
  }
  if (source.status == 0xff && source.data1 == 0x51) {
    auto event = beginEvent(cursor, source, "Tempo", SequenceSemantic::Tempo);
    event.u8("meta_type", SourceValueDisplay::Hex);
    const u32 tempo = (static_cast<u32>(event.u8("tempo_high")) << 16) |
                      (static_cast<u32>(event.u8("tempo_middle")) << 8) | event.u8("tempo_low");
    event.derived("microseconds_per_quarter", tempo);
    return event.invoke<&Playback::tempo>(tempo, source.delta);
  }
  if (source.status == 0xff && source.data1 == 0x2f) {
    auto event =
        beginEvent(cursor, source, "End of Sequence", SequenceSemantic::End, CommandPlaybackStatus::StopsPlayback);
    event.u8("meta_type", SourceValueDisplay::Hex);
    if (source.dataBytes > 1) {
      event.u8("terminator", SourceValueDisplay::Hex);
    }
    return event.invoke<&Playback::end>(source.delta).runtimeControlFlow();
  }
  return cursor.unsupported("Unsupported Sony PS1 Event").stop();
}

}  // namespace

const SequenceDialect& sonyPs1SequenceDialect() {
  static const SequenceDialect dialect = makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{.value = std::string(kSonyPs1DialectId)},
      .commandDetailKindPrefix = std::string(kSonyPs1DialectId),
      .timebase = Timebase{.ppqn = 48},
      .defaultBehavior =
          SequenceProgramBehavior{
              .commandLimit = kMaxCommands,
              .initialLevel = 1.0,
              .initialExpression = 1.0,
              .initialStereoBalance = StereoBalance{1.0, 1.0},
              .initialPitchBendRangeSemitones = 2,
              .initialTempoMicrosecondsPerQuarter = 500000,
          },
  });
  return dialect;
}

SequenceProgram parseSonyPs1Sequence(ByteReader reader, AssetId id, const SonyPs1SequenceLayout& layout,
                                     SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  SequenceProgram program = sonyPs1SequenceDialect().makeProgram(Address{layout.offset});
  program.timebase.ppqn = layout.ppqn;
  program.behavior.initialTempoMicrosecondsPerQuarter = layout.initialTempo;
  program.config.driverData = {
      layout.rhythmNumerator,
      static_cast<u32>(1u << layout.rhythmDenominatorPower),
  };

  if (sourceMap != nullptr) {
    const u32 headerSize = layout.dataOffset - layout.offset;
    auto header = sourceMap
                      ->header(layout.sep ? "Sony PS1 SEP Sequence Header" : "Sony PS1 SEQ Header",
                               reader.range(layout.offset, headerSize))
                      .kind(layout.sep ? "sony-ps1-sep-header" : "sony-ps1-seq-header")
                      .owner(ObjectRefs::sequence(id));
    u32 fields = layout.offset;
    if (!layout.sep || layout.sepFirst) {
      header.field("signature", reader.range(fields, 4), reader.le32(fields), SourceValueDisplay::Hex);
      fields += 4;
    }
    if (!layout.sep) {
      header.field("version", reader.range(fields, 4), reader.be32(fields));
      fields += 4;
    } else {
      if (layout.sepFirst) {
        header.field("version", reader.range(fields, 2), reader.be16(fields));
        fields += 2;
      }
      header.field("sequence_id", reader.range(fields, 2), layout.sequenceId);
      fields += 2;
    }
    header.field("ppqn", reader.range(fields, 2), layout.ppqn);
    fields += 2;
    header.field("tempo", reader.range(fields, 3), layout.initialTempo);
    fields += 3;
    header.field("rhythm_numerator", reader.range(fields, 1), layout.rhythmNumerator);
    header.field("rhythm_denominator_power", reader.range(fields + 1, 1), layout.rhythmDenominatorPower);
    if (layout.sep) {
      header.field("data_size", reader.range(fields + 2, 4), layout.length - headerSize);
    }
  }

  TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = layout.dataEnd,
      .maxCommands = kMaxCommands,
      .sourceHasTracks = false,
      .sequenceAsset = id,
      .sourceMap = sourceMap,
  };
  auto eventAt = [&](u32 offset) -> const SonyPs1EventLayout* {
    const auto found = std::ranges::lower_bound(layout.events, offset, {}, &SonyPs1EventLayout::offset);
    return found != layout.events.end() && found->offset == offset ? &*found : nullptr;
  };
  auto track = tracks.reachable(0, layout.dataOffset, [&](u32 offset) -> DecodedBytecodeCommand {
    const auto* event = eventAt(offset);
    if (event == nullptr) {
      Cursor cursor(reader, offset, layout.dataEnd, kSonyPs1DialectId, diagnostics);
      return cursor.unsupported("Invalid Sony PS1 Event Address").stop();
    }
    auto decoded = decodeEvent(reader, offset, layout.dataEnd, *event, diagnostics);
    if (event->implicitEnd) {
      decoded.flow = CommandFlow::end(Address{event->end});
    }
    return decoded;
  });
  track.sourceTrackNumber = 0;
  program.tracks.push_back(track);
  for (u32 channel = 1; channel < 16; ++channel) {
    TrackProgram copy = track;
    copy.id = TrackId{channel};
    copy.sourceTrackNumber = channel;
    program.tracks.push_back(std::move(copy));
  }
  return program;
}

}  // namespace vgmtrans::formats::sony_ps1
