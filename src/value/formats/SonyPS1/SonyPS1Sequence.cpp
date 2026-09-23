/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS1/SonyPS1.h"

#include "value/base/LevelScale.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::sony_ps1 {

using namespace core;

namespace {

constexpr u32 kMaxCommands = 1048576;

struct RuntimeConfig {
  u8 numerator = 4;
  u8 denominator = 4;
};

struct TrackState {
  explicit TrackState(TrackStateContext program)
      : channel(static_cast<u8>(program.sourceTrackNumber)), program(static_cast<u8>(program.sourceTrackNumber)) {}

  u8 channel = 0;
  u8 bank = 0;
  u8 program = 0;
  u8 rpnMsb = 127;
  u8 rpnLsb = 127;
  u8 pitchBendRange = 2;
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

struct Playback : SequencePlayback<TrackState> {
  const RuntimeConfig& config;

  void beginSection() {
    out.instrument(sonyPs1InstrumentIdentity(track.bank, track.program));
    if (track.channel == 0) {
      out.timeSignature(config.numerator, config.denominator, 24);
    }
  }

  void note(u8 key, u8 velocity) {
    if (velocity == 0) {
      out.noteOff(key);
      return;
    }
    out.noteOn(key, LevelScale::linearFromMidi7(std::min<u8>(velocity, 127)));
  }

  void program(u8 value) {
    if (value < 128) {
      track.program = value;
      out.instrument(sonyPs1InstrumentIdentity(track.bank, track.program));
    }
  }

  void controller(u8 controller, u8 value) {
    switch (controller) {
      case 0:
        track.bank = value;
        out.instrument(sonyPs1InstrumentIdentity(track.bank, track.program));
        break;
      case 1:
        out.modulation(ModulationPerformanceTarget::VibratoDepth, value / 127.0);
        break;
      case 6:
        if (track.rpnMsb == 0 && track.rpnLsb == 0) {
          track.pitchBendRange = value;
          out.pitchBendRange(value);
        }
        break;
      case 7:
        out.level(LevelScale::linearFromMidi7(value));
        break;
      case 10: {
        const PanGains pan = psxPan(value);
        out.stereoBalance(pan.left, pan.right);
        break;
      }
      case 11:
        out.expression(LevelScale::linearFromMidi7(value));
        break;
      case 64: {
        out.sustainPedal(value >= 64);
        break;
      }
      case 91:
        out.reverb(value / 127.0);
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
        out.sustainPedal(false);
        out.instrument(sonyPs1InstrumentIdentity(track.bank, track.program));
        out.level(1.0);
        out.expression(1.0);
        out.stereoBalance(1.0, 1.0);
        out.pitchBend(0.0);
        out.pitchBendRange(2);
        break;
      default:
        break;
    }
  }

  void pitchBend(u8 msb) {
    // All three audited libsnd generations discard the MIDI LSB and use the
    // high seven bits as their signed wheel position.
    const double wheel = std::clamp((static_cast<int>(msb) - 64) / 64.0, -1.0, 1.0);
    out.pitchBend(PitchBendPerformanceEvent{
        .semitones = wheel * track.pitchBendRange,
        .normalizedWheelPosition = wheel,
    });
  }

  void tempo(u32 microsecondsPerQuarter) {
    if (microsecondsPerQuarter != 0) {
      out.tempo(microsecondsPerQuarter);
    }
  }

  Effects loopEnd(u8 count, Address destination) {
    if (count == 127) {
      return vm.declaredLoop(destination);
    } else if (count > 1) {
      return vm.countedRepeatUntil(0, count, destination);
    }
    return {};
  }
};

using Cursor = CompilerCursor<Playback>;

[[nodiscard]] Cursor::Event beginEvent(Cursor& cursor, const SonyPs1EventLayout& source, std::string_view label,
                                       SequenceSemantic semantic,
                                       CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback) {
  auto event = cursor.command(label, semantic, playback);
  event.delay(source.delta);
  cursor.opcodeValue("delta_byte_0", cursor.opcode(), SourceValueDisplay::Hex);
  for (u32 i = 1; i < source.deltaSize; ++i) {
    cursor.u8("delta_byte", SourceValueDisplay::Hex);
  }
  cursor.derived("delta", source.delta);
  if (source.explicitStatus) {
    cursor.u8("status", SourceValueDisplay::Hex);
  } else {
    cursor.derived("running_status", source.status, SourceValueDisplay::Hex);
  }
  if ((source.status & 0xf0) != 0xf0) {
    cursor.derived("channel", static_cast<u8>(source.status & 0x0f), SemanticOperandRole::Channel);
  }
  return event;
}

[[nodiscard]] DecodedBytecodeCommand decodeEvent(ByteReader reader, u32 begin, u32 end,
                                                 const SonyPs1EventLayout& source,
                                                 std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, end, kSonyPs1CommandKindPrefix, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 family = source.status & 0xf0;
  const u8 channel = source.status & 0x0f;
  if (family == 0x90) {
    return beginEvent(cursor, source, source.data2 == 0 ? "Note Off" : "Note On", SequenceSemantic::Note)
        .channel(channel)
        .invoke<&Playback::note>({cursor.u8("key", SourceValueDisplay::MidiNote), cursor.u8("velocity")});
  }
  if (family == 0xc0) {
    return beginEvent(cursor, source, "Program Change", SequenceSemantic::Program)
        .channel(channel)
        .invoke<&Playback::program>({cursor.u8("program", SemanticOperandRole::InstrumentProgram)});
  }
  if (family == 0xe0) {
    auto event = beginEvent(cursor, source, "Pitch Bend", SequenceSemantic::Pitch);
    cursor.u8("lsb");
    const u8 msb = cursor.u8("msb");
    cursor.derived("driver_wheel", static_cast<s16>((static_cast<int>(msb) - 64) * 128),
                   SourceValueDisplay::SignedDecimal);
    return event.channel(channel).invoke<&Playback::pitchBend>({msb});
  }
  if (family == 0xb0) {
    const bool loopStart = source.data1 == 99 && source.data2 == 20;
    const bool loopEnd = source.loopDestination.has_value();
    auto event = beginEvent(cursor, source,
                            loopEnd     ? "Loop End"
                            : loopStart ? "Loop Start"
                                        : "Controller",
                            loopEnd || loopStart ? SequenceSemantic::Loop : SequenceSemantic::State);
    const u8 controller = cursor.u8("controller");
    const auto role = controller == 0 ? SemanticOperandRole::InstrumentBank : SemanticOperandRole::Value;
    const u8 value = cursor.u8("value", role);
    if (loopStart) {
      cursor.derived("loop_start", Address{source.end}, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
    }
    if (loopEnd) {
      const Address destination{*source.loopDestination};
      cursor.derived("repeat_count", source.loopCount);
      cursor.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      event.invoke<&Playback::loopEnd>({source.loopCount, destination}).discoverTarget(destination);
      return event;
    }
    return event.channel(channel).invoke<&Playback::controller>({controller, value});
  }
  if (source.status == 0xff && source.data1 == 0x51) {
    auto event = beginEvent(cursor, source, "Tempo", SequenceSemantic::Tempo);
    cursor.u8("meta_type", SourceValueDisplay::Hex);
    const u8 high = cursor.u8("tempo_high");
    const u8 middle = cursor.u8("tempo_middle");
    const u8 low = cursor.u8("tempo_low");
    const u32 tempo = (static_cast<u32>(high) << 16) | (static_cast<u32>(middle) << 8) | low;
    cursor.derived("microseconds_per_quarter", tempo);
    return event.invoke<&Playback::tempo>({tempo});
  }
  if (source.status == 0xff && source.data1 == 0x2f) {
    auto event =
        beginEvent(cursor, source, "End of Sequence", SequenceSemantic::End, CommandPlaybackStatus::StopsPlayback);
    cursor.u8("meta_type", SourceValueDisplay::Hex);
    if (source.dataBytes > 1) {
      cursor.u8("terminator", SourceValueDisplay::Hex);
    }
    return event.end();
  }
  return cursor.unsupported("Unsupported Sony PS1 Event").stop();
}

}  // namespace

const SequenceProgramConfig& sonyPs1SequenceConfig() {
  static const SequenceProgramConfig config = SequenceProgramConfig{
      .commandKindPrefix = std::string(kSonyPs1CommandKindPrefix),
      .timebase = Timebase{.ppqn = 48},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kMaxCommands,
              .initialLevel = 1.0,
              .initialExpression = 1.0,
              .initialStereoBalance = StereoBalance{1.0, 1.0},
              .initialPitchBendRangeSemitones = 2,
              .initialTempoMicrosecondsPerQuarter = 500000,
          },
  };
  return config;
}

SequenceProgram parseSonyPs1Sequence(ByteReader reader, AssetId id, const SonyPs1SequenceLayout& layout,
                                     SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  auto config = sonyPs1SequenceConfig();
  config.timebase.ppqn = layout.ppqn;
  config.behavior.initialTempoMicrosecondsPerQuarter = layout.initialTempo;
  const bool rhythmSpecified = layout.rhythmNumerator != 0;
  auto runtime = makeCompiledRuntime<Playback, RuntimeConfig>(RuntimeConfig{
      .numerator = rhythmSpecified ? layout.rhythmNumerator : u8{4},
      .denominator = rhythmSpecified ? static_cast<u8>(1u << layout.rhythmDenominatorPower) : u8{4},
  });

  const u32 headerSize = layout.dataOffset - layout.offset;
  SequenceDecodeSession sequence(reader, config, id, reader.range(layout.offset, headerSize), sourceMap, kMaxCommands,
                                 layout.dataEnd);
  if (sourceMap != nullptr) {
    auto header = sequence.header()
                      .label(layout.sep ? "Sony PS1 SEP Sequence Header" : "Sony PS1 SEQ Header")
                      .kind(layout.sep ? "sony-ps1-sep-header" : "sony-ps1-seq-header");
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

  auto tracks = sequence.trackScope();
  tracks.sourceHasTracks = false;
  auto eventAt = [&](u32 offset) -> const SonyPs1EventLayout* {
    const auto found = std::ranges::lower_bound(layout.events, offset, {}, &SonyPs1EventLayout::offset);
    return found != layout.events.end() && found->offset == offset ? &*found : nullptr;
  };
  auto track = tracks.decode(0, layout.dataOffset, [&](u32 offset) -> DecodedBytecodeCommand {
    const auto* event = eventAt(offset);
    if (event == nullptr) {
      Cursor cursor(reader, offset, layout.dataEnd, kSonyPs1CommandKindPrefix, diagnostics);
      return cursor.unsupported("Invalid Sony PS1 Event Address").stop();
    }
    auto decoded = decodeEvent(reader, offset, layout.dataEnd, *event, diagnostics);
    if (event->implicitEnd) {
      decoded.flow = CommandFlow::end(Address{event->end});
    }
    return decoded;
  });
  track.streams = {{.channels = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}}};
  sequence.addTrack(std::move(track));
  return sequence.finish(std::move(runtime));
}

}  // namespace vgmtrans::formats::sony_ps1
