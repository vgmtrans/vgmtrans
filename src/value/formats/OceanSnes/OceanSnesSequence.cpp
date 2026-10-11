/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/OceanSnes/OceanSnesVoice.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <fmt/format.h>
#include <algorithm>
#include <bitset>
#include <utility>

namespace vgmtrans::formats::ocean_snes {
using namespace core;

namespace {

struct Channel {
  Voice voice;
  u8 program = 0;
  u8 low = 1, high = 0, cachedLow = 0, cachedHigh = 0, durationHigh = 0;
  u8 autoPortamento = 0;
  s8 bendStep = 0;
  bool enabled = true, ready = true, immediate = false;
  std::optional<u64> preparedTick;
};

struct GainRamp {
  u16 level = 0, step = 0;
  u8 ticks = 0, target = 0;

  bool advance() {
    if (ticks == 0) {
      return false;
    }
    level = static_cast<u16>(level + step);
    if (--ticks == 0) {
      level = (u16(target) << 8) | (level & 0xff);
    }
    return true;
  }

  u8 volume() const { return level >> 8; }
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& settings) : config(settings) {
    const auto reader = config.source.reader();
    const u16 h = config.layout.header;
    speed = reader.u8At(h + 6);
    phase = static_cast<u8>(speed - 1);
    transpose = reader.u8At(h + 7);
    // Song initialization replaces the master gain's integer byte. Host gain
    // ramps survive it, including snapshots taken just before a fade-in.
    masterVolume = {.level = static_cast<u16>((u16(reader.u8At(h + 8)) << 8) | reader.u8At(0x6d)),
                    .step = reader.le16(0x71),
                    .ticks = reader.u8At(0x63),
                    .target = reader.u8At(0x66)};
    musicVolume = {
        .level = reader.le16(0x6b), .step = reader.le16(0x6f), .ticks = reader.u8At(0x64), .target = reader.u8At(0x67)};
    outputMode = reader.u8At(0xd3);
    for (u32 i = 0; i < channels.size(); ++i) {
      channels[i].voice.trackVolume = reader.u8At(h + 9 + i);
    }
    lfoAmplitude = reader.u8At(h + 22);
    lfoRate = reader.u8At(h + 23);
    lfoBias = reader.u8At(h + 24);
    random = reader.le16(config.layout.randomSeed);
  }

  const RuntimeConfig& config;
  std::array<Channel, 8> channels;
  u8 nextOrder = 1, barrier = 0;
  u8 speed = 1, phase = 0, swingPhase = 0;
  std::array<u8, 2> swing{};
  u8 transpose = 0, outputMode = 1;
  GainRamp masterVolume, musicVolume;
  u8 lfoPhase = 0, lfoRate = 0, lfoAmplitude = 0, lfoBias = 0;
  u16 random = 0;
  u8 bendPatternIndex = 0;
  bool musicRow = false;
  std::optional<u64> preparedTick, globalsTick;
  std::bitset<256> warnings;

  void clearSlides() {
    for (auto& c : channels) {
      c.bendStep = 0;
      c.voice.bendStep = 0;
      c.voice.volumeStep = 0;
    }
  }

  void prepare(u64 tick) {
    if (preparedTick == tick) {
      return;
    }
    preparedTick = tick;
    musicRow = ++phase >= speed;
    if (!musicRow) {
      return;
    }
    phase = 0;
    if (swingPhase != 0) {
      swingPhase = swingPhase == 1 ? 2 : 1;
      speed = swing[swingPhase - 1];
    }
    if (config.layout.version == Version::Waterworld) {
      clearSlides();
    }
  }

  u8 randomPan() {
    const u8 a = static_cast<u8>(random) ^ static_cast<u8>(random >> 8);
    const u8 low = static_cast<u8>((random & 0xff) >> 1) | ((a & 2) ? 0 : 0x80);
    const u8 high = static_cast<u8>((random >> 9) | ((random & 1) << 7));
    random = low | (u16(high) << 8);
    return low & 0x7f;
  }

  void updateGlobals(u64 tick) {
    if (globalsTick == tick) {
      return;
    }
    globalsTick = tick;
    const bool masterChanged = masterVolume.advance();
    const bool musicChanged = musicVolume.advance();
    if (masterChanged || musicChanged) {
      for (auto& c : channels) {
        c.voice.volumeDirty = true;
      }
    }
    if ((tick / 2) % 2 == 0) {
      return;
    }
    const auto reader = config.source.reader();
    const s8 wave = static_cast<s8>(reader.u8At(config.layout.sineTable + lfoPhase) + lfoBias);
    const int delta = (std::abs(int(wave)) * lfoAmplitude / 64) * (wave < 0 ? -1 : 1);
    lfoPhase = static_cast<u8>(lfoPhase + lfoRate);
    for (auto& c : channels) {
      auto& v = c.voice;
      switch (v.lfoTarget) {
        case 2:
          v.changeVibratoDepth(static_cast<u8>(delta));
          break;
        case 3:
          v.vibratoRate = static_cast<u8>(v.vibratoRate + delta);
          break;
        case 4:
          v.lfoPitch = static_cast<s16>(delta);
          break;
        case 5:
          v.pan = static_cast<u16>(v.pan + delta * 256);
          v.volumeDirty = true;
          break;
        case 8:
          v.regionVolume = static_cast<u8>(v.regionVolume + delta);
          break;
        default:
          break;  // Echo/FIR destinations have no write-enabled echo buffer.
      }
    }
  }
};

struct TrackState {
  explicit TrackState(TrackStateContext context) : index(context.sourceTrackNumber) {}
  u32 index;
};

struct Playback : SequencePlayback<TrackState> {
  ProgramState& song;

  Channel& channel() { return song.channels[track.index]; }
  Voice& voice() { return channel().voice; }

  void warning(u8 kind, std::string message) {
    if (!song.warnings.test(kind)) {
      song.warnings.set(kind);
      vm.diagnostic({.severity = Severity::Warning, .message = std::move(message), .range = vm.sourceRange()});
    }
  }

  void beforeCommand() {
    song.prepare(vm.tick());
    auto& c = channel();
    if (c.preparedTick == vm.tick()) {
      return;
    }
    c.preparedTick = vm.tick();
    c.ready = false;
    if (!song.musicRow || !c.enabled) {
      return;
    }
    c.immediate = false;
    c.bendStep = 0;
    c.voice.volumeStep = 0;
    c.voice.bendStep = 0;
    if (--c.low == 0) {
      c.low = 127;
      if ((--c.high & 0x80) != 0) {
        c.durationHigh = 0;
        c.ready = true;
      }
    }
  }

  void tick() {
    // Two VM ticks per driver tick keep parsing and synthesis ordered across
    // all eight streams. Output is dated at the beginning of the driver tick.
    if (vm.tick() % 2 == 0) {
      return;
    }
    song.updateGlobals(vm.tick());
    auto output = out.at(vm.tick() - 1);
    voice().tick(song.config, song.transpose, song.masterVolume.volume(), song.musicVolume.volume(), song.outputMode,
                 output);
  }

  Effects wait() {
    auto effect = vm.finiteBranch(Address{vm.sourceRange().offset});
    effect.advanceTicks = 2;
    return effect;
  }

  Effects continueAt(Address next, bool force = false) {
    auto& c = channel();
    const auto reader = song.config.source.reader();
    if (next.value >= kAramSize) {
      warning(0, "OceanSnes stream crossed the end of ARAM");
      return vm.end();
    }
    const u8 peek = reader.u8At(next.value);
    if (force || peek < 0x80 || peek == 0xca || c.cachedLow == 0) {
      return vm.finiteBranch(next);
    }
    c.low = c.cachedLow;
    c.high = c.cachedHigh;
    c.ready = false;
    auto effect = vm.finiteBranch(next);
    effect.advanceTicks = 2;
    return effect;
  }

  Effects enterOrder(Address start) { return vm.finiteBranch(start); }

  Effects nextOrder() {
    auto& c = channel();
    c.enabled = false;
    if (song.nextOrder == song.config.layout.orderBlocks.size()) {
      song.nextOrder = 0;
    }
    if (song.nextOrder >= song.config.layout.orderBlocks.size()) {
      warning(0xc7, "OceanSnes next order is outside the order table");
      return vm.end();
    }
    const Address target{song.config.layout.orders + song.nextOrder * 2u};
    song.barrier |= static_cast<u8>(1u << track.index);
    if (song.barrier == 0xff) {
      for (auto& other : song.channels) {
        other.enabled = true;
      }
      song.nextOrder = static_cast<u8>((song.nextOrder + 1) & 0x7f);
      song.barrier = 0;
      song.clearSlides();
      c.immediate = false;
    }
    // The driver force-fetches this next stream even on a partial barrier.
    // Order entries, not reused pattern addresses, define song loop points.
    return vm.loopCandidate(target);
  }

  void note(u8 encoded, bool retrigger = false) {
    auto& c = channel();
    if (!retrigger && c.autoPortamento != 0) {
      voice().portamento(song.config, encoded, c.autoPortamento, song.transpose);
      return;
    }
    u8 pan = 64;
    if (const auto& instrument = song.config.data->instruments[c.program & 0x7f]) {
      const auto region = std::ranges::find_if(
          instrument->regions, [&](const InstrumentRegion& r) { return r.low <= encoded && encoded <= r.high; });
      if (region != instrument->regions.end() && region->pan == 0x80) {
        pan = song.randomPan();
      }
    }
    voice().start(song.config, c.program, encoded, pan, out, vm);
    voice().bendStep = c.bendStep;
    if (voice().region && voice().region->pitchModulation) {
      warning(0xd8, "OceanSnes hardware pitch modulation is retained in the region metadata but cannot be exported");
    }
    if (voice().lfoTarget > 14) {
      warning(0xce, "OceanSnes global LFO selector is outside its dispatch table");
    }
  }

  Effects execute(u8 opcode, u16 operand, u8 second, Address next) {
    auto& c = channel();
    auto& v = voice();
    if (!c.ready) {
      return wait();
    }
    if (opcode < 0x80) {
      c.cachedLow = opcode;
      c.cachedHigh = c.durationHigh;
      if (opcode == 0) {
        return continueAt(next, true);
      }
      c.low = opcode;
      c.high = c.cachedHigh;
      c.ready = false;
      auto effect = vm.finiteBranch(next);
      effect.advanceTicks = 2;
      return effect;
    }
    if (opcode < 0xbe) {
      note(opcode);
      return continueAt(next, c.immediate);
    }
    if (opcode >= 0xf0) {
      v.eventVolume = (opcode & 15) * 16;
      v.volumeDirty = true;
    } else if (opcode >= 0xea || opcode == 0xc0) {
      c.program = opcode >= 0xea ? opcode - 0xea : operand & 0x7f;
      return continueAt(next, true);
    } else {
      const u8 value = static_cast<u8>(operand);
      switch (opcode) {
        case 0xbe:
          v.release(out);
          break;
        case 0xbf:
          c.immediate = true;
          return continueAt(next, true);
        case 0xc1:
          if (v.region) {
            v.panTo(value, track.index * 2);
          }
          break;
        case 0xc2:
          if (v.region) {
            v.volumeStep = song.config.layout.version == Version::Waterworld
                               ? static_cast<s8>(value * 2)
                               : static_cast<s8>(static_cast<s8>(value) / 2);
          }
          break;
        case 0xc3:
          v.trackVolume = value;
          break;  // Takes effect at the next volume write.
        case 0xc4:
          break;  // The unconditional branch skips the apparent pan setter.
        case 0xc5:
          // The operand is ignored. Although the track-side store uses the
          // pattern index, voice lookup recovers the current slot's voice.
          song.bendPatternIndex = (song.bendPatternIndex + 1) & 15;
          v.bendStep = song.config.source.reader().s8At(song.config.layout.bendPattern + song.bendPatternIndex);
          if (song.bendPatternIndex % 2 == 0) {
            song.channels[song.bendPatternIndex / 2].bendStep = v.bendStep;
          }
          break;
        case 0xc6:
          if (v.region) {
            v.vibratoRate = static_cast<u8>(v.vibratoRate + value);
          }
          break;
        case 0xc7:
          return nextOrder();
        case 0xc8:
        case 0xe6:
        case 0xe7:
          break;  // SFX allocation priorities do not affect music.
        case 0xc9:
          warning(opcode, "OceanSnes reserved C9 clears song RAM; stopping this stream");
          return vm.end();
        case 0xca:
          c.durationHigh = value;
          return continueAt(next, true);
        case 0xcb:
          song.speed = value;
          song.swingPhase = 0;
          break;
        case 0xcc:
          c.bendStep = v.bendStep = static_cast<s8>(value);
          break;
        case 0xcd:
          note(static_cast<u8>(value - 1), true);
          v.calculatePitch = false;  // CD slides from the retained previous base pitch.
          v.portamento(song.config, value, second, song.transpose);
          break;
        case 0xce:
          if (v.region) {
            v.lfoTarget = value;
          }
          if (value > 14) {
            warning(opcode, "OceanSnes global LFO selector is outside its dispatch table");
          }
          break;
        case 0xcf:
          v.portamento(song.config, value, second, song.transpose);
          break;
        case 0xd0:
          break;  // Retained as source metadata; C1 overwrites this duration.
        case 0xd1:
          v.eventVolume = static_cast<u8>(value * 2);
          v.volumeDirty = true;
          break;
        case 0xd2:
        case 0xd3:
          warning(opcode, "OceanSnes SFX-only command encountered in a music stream; stopping");
          v.release(out);
          return vm.end();
        case 0xd4:
          if (v.region) {
            v.changeVibratoDepth(value);
          }
          break;
        case 0xd5:
          song.lfoRate = value;
          break;
        case 0xd6:
          song.lfoAmplitude = value;
          break;
        case 0xd7:
          song.lfoPhase = 0;
          break;
        case 0xd8:
          if (!v.region) {
            next = Address{vm.sourceRange().offset + 1};  // No owned voice: no operand is consumed.
          } else if (value) {
            warning(opcode, "OceanSnes hardware pitch modulation cannot be exported");
          }
          break;
        case 0xd9:
          if (song.config.layout.version == Version::JurassicPark2) {
            song.swing = {static_cast<u8>(value >> 4), static_cast<u8>(value & 15)};
            song.speed = value >> 4;
          } else {
            song.swing = {static_cast<u8>(value & 15), static_cast<u8>(value >> 4)};
            if (!(song.swingPhase & 1)) {
              song.speed = value >> 4;
            }
          }
          song.swingPhase = 1;
          break;
        case 0xda:
          song.transpose = value;
          break;
        case 0xdb:
        case 0xdc:
        case 0xdd:
        case 0xde:
        case 0xdf:
        case 0xe0:
        case 0xe1:
        case 0xe2:
        case 0xe3:
        case 0xe4:
          // Both revisions leave DSP FLG=$20 (echo writes disabled). Preserve
          // these operands in the source map instead of inventing MIDI reverb.
          break;
        case 0xe5:
          song.nextOrder = value & 0x7f;
          break;
        case 0xe8:
          c.immediate = false;
          {
            auto effect = continueAt(Address{operand});
            // Unlike an order transition, an explicit repeated jump denotes
            // a bytecode loop even within a single order.
            effect.flowOverride = CommandTransition::jump(Address{operand}, JumpSemantics::LoopCandidate);
            return effect;
          }
        case 0xe9:
          c.autoPortamento = value;
          break;
        default:
          break;
      }
    }
    c.immediate = false;
    return continueAt(next);
  }
};

struct CommandInfo {
  const char* name;
  u8 operands;
  SequenceSemantic semantic;
};
constexpr std::array<CommandInfo, 42> kCommands{{
    {"Instrument", 1, SequenceSemantic::Instrument},
    {"Pan Ramp", 1, SequenceSemantic::Pan},
    {"Event Volume Slide", 1, SequenceSemantic::Level},
    {"Track Volume", 1, SequenceSemantic::Level},
    {"Consuming No-op", 1, SequenceSemantic::Meta},
    {"Pattern Bend (Index Bug)", 1, SequenceSemantic::Pitch},
    {"Add Vibrato Rate", 1, SequenceSemantic::Modulation},
    {"Next Order Barrier", 0, SequenceSemantic::Jump},
    {"SFX Priority", 1, SequenceSemantic::Meta},
    {"Reserved RAM Clear", 0, SequenceSemantic::End},
    {"Duration High", 1, SequenceSemantic::Wait},
    {"Row Speed", 1, SequenceSemantic::Tempo},
    {"Pitch Slide", 1, SequenceSemantic::Pitch},
    {"Note and Portamento", 2, SequenceSemantic::Note},
    {"Global LFO Target", 1, SequenceSemantic::Modulation},
    {"Portamento", 2, SequenceSemantic::Pitch},
    {"Pan Ramp Duration", 1, SequenceSemantic::Pan},
    {"Event Volume", 1, SequenceSemantic::Level},
    {"SFX End", 0, SequenceSemantic::End},
    {"SFX Instrument", 1, SequenceSemantic::Instrument},
    {"Add Vibrato Depth", 1, SequenceSemantic::Modulation},
    {"Global LFO Rate", 1, SequenceSemantic::Modulation},
    {"Global LFO Amplitude", 1, SequenceSemantic::Modulation},
    {"Reset Global LFO Phase", 0, SequenceSemantic::Modulation},
    {"Hardware Pitch Modulation", 1, SequenceSemantic::Modulation},
    {"Alternating Row Speeds", 1, SequenceSemantic::Tempo},
    {"Global Transpose", 1, SequenceSemantic::Pitch},
    {"FIR Coefficient 0", 1, SequenceSemantic::Modulation},
    {"FIR Preset", 1, SequenceSemantic::Modulation},
    {"Echo Left", 1, SequenceSemantic::Modulation},
    {"Echo Right", 1, SequenceSemantic::Modulation},
    {"Echo Feedback", 1, SequenceSemantic::Modulation},
    {"Echo Both", 1, SequenceSemantic::Modulation},
    {"Echo Ramp Duration", 1, SequenceSemantic::Modulation},
    {"Echo Ramp (Scratch Dependent)", 1, SequenceSemantic::Modulation},
    {"Feedback Ramp Duration", 1, SequenceSemantic::Modulation},
    {"Feedback Ramp (Aliased State)", 1, SequenceSemantic::Modulation},
    {"Select Next Order", 1, SequenceSemantic::Jump},
    {"SFX Priority", 1, SequenceSemantic::Meta},
    {"Clear SFX Priority", 0, SequenceSemantic::Meta},
    {"Jump", 2, SequenceSemantic::Jump},
    {"Automatic Portamento", 1, SequenceSemantic::Pitch},
}};

DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 address, std::vector<Diagnostic>* diagnostics) {
  CompilerCursor<Playback> cursor(reader, address, "ocean-snes", diagnostics);
  const u8 opcode = cursor.opcode();
  CommandInfo info;
  if (opcode < 0x80) {
    info = {"Duration", 0, SequenceSemantic::Wait};
    cursor.opcodeValue("low counter", opcode);
  } else if (opcode < 0xbe) {
    info = {"Note", 0, SequenceSemantic::Note};
    cursor.opcodeValue("key", int(opcode) - 0x81 + 24);
  } else if (opcode == 0xbe) {
    info = {"Release", 0, SequenceSemantic::Note};
  } else if (opcode == 0xbf) {
    info = {"Immediate Continuation", 0, SequenceSemantic::Meta};
  } else if (opcode < 0xea) {
    info = kCommands[opcode - 0xc0];
  } else if (opcode < 0xf0) {
    info = {"Instrument", 0, SequenceSemantic::Instrument};
    cursor.opcodeValue("instrument", opcode - 0xea);
  } else {
    info = {"Event Volume", 0, SequenceSemantic::Level};
    cursor.opcodeValue("volume", (opcode & 15) * 16);
  }
  u16 operand = 0;
  u8 second = 0;
  if (opcode == 0xe8) {
    operand = cursor.u16le("destination", SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
  } else {
    if (info.operands != 0) {
      operand = cursor.u8("value");
    }
    if (info.operands == 2) {
      second = cursor.u8("duration");
    }
  }
  auto event = cursor.command(info.name, info.semantic);
  event.invoke<&Playback::execute>({opcode, operand, second, Address{address + 1u + info.operands}});
  if (opcode == 0xe8) {
    event.jump(Address{operand});
  } else if (opcode == 0xc7 || opcode == 0xc9 || opcode == 0xd2 || opcode == 0xd3) {
    event.stop();
  }
  auto decoded = static_cast<DecodedBytecodeCommand>(event);
  if (opcode == 0xd8) {
    decoded.discoveryTargets.push_back(Address{address + 1});
  }
  return decoded;
}

}  // namespace

SequenceProgram decodeSequence(RetainedSource source, const Layout& layout, std::shared_ptr<const DriverData> data,
                               AssetId id, SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const auto reader = source.reader();
  const SequenceProgramConfig config{.commandKindPrefix = "ocean-snes",
                                     .timebase = {.ppqn = 48},
                                     .behavior = {.commandLimit = 4'000'000,
                                                  .inferLoopsFromRepeatedState = false,
                                                  .initialLevel = 1.0,
                                                  .initialReverbSend = 0.0,
                                                  .initialTempoMicrosecondsPerQuarter = 486000}};
  SequenceDecodeSession sequence(reader, config, id, reader.range(layout.header, 26), sourceMap, 65536, kAramSize);
  sequence.header()
      .label("OceanSnes Music Header")
      .fieldsAsChildren()
      .field("Order Table", reader.range(layout.header, 2), layout.orders, SourceValueDisplay::Address)
      .field("Instrument Table", reader.range(layout.header + 2, 2), layout.instruments, SourceValueDisplay::Address)
      .field("Sample Table", reader.range(layout.header + 4, 2), layout.samples, SourceValueDisplay::Address);
  constexpr std::array<std::string_view, 20> headerFields{"Row Speed",
                                                          "Transpose",
                                                          "Master Volume",
                                                          "Track 1 Volume",
                                                          "Track 2 Volume",
                                                          "Track 3 Volume",
                                                          "Track 4 Volume",
                                                          "Track 5 Volume",
                                                          "Track 6 Volume",
                                                          "Track 7 Volume",
                                                          "Track 8 Volume",
                                                          "Echo Delay",
                                                          "Echo Right",
                                                          "Echo Left",
                                                          "Echo Feedback",
                                                          "FIR Preset",
                                                          "Global LFO Amplitude",
                                                          "Global LFO Rate",
                                                          "Global LFO Bias",
                                                          "SFX Slots"};
  for (u32 i = 0; i < headerFields.size(); ++i) {
    sequence.header().field(headerFields[i], reader.range(layout.header + 6 + i, 1), reader.u8At(layout.header + 6 + i),
                            SourceValueDisplay::Hex);
  }
  std::vector<Address> starts;
  for (u32 i = 0; i < layout.orderBlocks.size(); ++i) {
    starts.push_back(Address{layout.orders + i * 2u});
  }
  for (u32 track = 0; track < 8; ++track) {
    sequence.trackPointer(track, reader.range(layout.orderBlocks.front() + track * 2, 2),
                          reader.le16(layout.orderBlocks.front() + track * 2));
    sequence.addTrack(sequence.trackScope().decode(track, starts, [&](u32 address) {
      if (address >= layout.orders && address < layout.orders + layout.orderBlocks.size() * 2 &&
          (address - layout.orders) % 2 == 0) {
        const u16 block = layout.orderBlocks[(address - layout.orders) / 2];
        const Address target{reader.le16(block + track * 2)};
        return DecodedBytecodeCommand{
            .range = reader.range(address, 2),
            .flow = CommandFlow::jumpTo(target, Address{address + 2}),
            .execution = {.body = detail::makeCommandBody<Playback>(&Playback::enterOrder, target)},
            .presentation = {.label = "Order", .kind = "ocean-snes-order", .semantic = SequenceSemantic::Jump}};
      }
      return decodeCommand(reader, address, diagnostics);
    }));
  }
  return sequence.finish(makeCompiledRuntime<Playback, ProgramState>(
      RuntimeConfig{.source = std::move(source), .layout = layout, .data = std::move(data)}));
}

}  // namespace vgmtrans::formats::ocean_snes
