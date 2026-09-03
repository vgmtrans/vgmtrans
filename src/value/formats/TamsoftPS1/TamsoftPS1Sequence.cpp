/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TamsoftPS1/TamsoftPS1.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceVm.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <utility>

namespace vgmtrans::formats::tamsoft_ps1 {

using namespace core;

namespace {

constexpr u32 kPpqn = 24;
constexpr u32 kMaximumCommands = 1'048'576;
constexpr u32 kPs1InitialTempo = 404'770;  // 53.2224 MHz / 3413 / 263 VBlanks per second.
constexpr u32 kPs2InitialTempo = 400'400;  // NTSC 60000/1001 VBlanks per second.

constexpr std::array<u16, 73> kPitchTable{
    0x0100, 0x010f, 0x011f, 0x0130, 0x0142, 0x0155, 0x016a, 0x017f, 0x0196, 0x01ae, 0x01c8,
    0x01e3, 0x0200, 0x021e, 0x023e, 0x0260, 0x0285, 0x02ab, 0x02d4, 0x02ff, 0x032c, 0x035d,
    0x0390, 0x03c6, 0x0400, 0x043c, 0x047d, 0x04c1, 0x050a, 0x0556, 0x05a8, 0x05fe, 0x0659,
    0x06ba, 0x0720, 0x078d, 0x0800, 0x0879, 0x08fa, 0x0983, 0x0a14, 0x0aad, 0x0b50, 0x0bfc,
    0x0cb2, 0x0d74, 0x0e41, 0x0f1a, 0x1000, 0x10f3, 0x11f5, 0x1306, 0x1428, 0x155b, 0x16a0,
    0x17f9, 0x1965, 0x1ae8, 0x1c82, 0x1e34, 0x2000, 0x21e7, 0x23eb, 0x260d, 0x2851, 0x2ab7,
    0x2d41, 0x2ff2, 0x32cb, 0x35d1, 0x3904, 0x3c68, 0x3fff,
};

[[nodiscard]] u32 eventSize(u8 opcode) {
  if (opcode < 0xe0 || opcode == 0xe8 || opcode == 0xe9 || opcode == 0xf0 || opcode == 0xff) {
    return 1;
  }
  switch (opcode) {
    case 0xe0:
    case 0xe2:
    case 0xe6:
    case 0xe7:
    case 0xf1:
      return 2;
    case 0xe1:
    case 0xe3:
    case 0xe4:
    case 0xe5:
    case 0xea:
    case 0xf8:
    case 0xf9:
      return 3;
    default:
      return 1;
  }
}

[[nodiscard]] std::optional<u32> relativeTarget(ByteReader reader, u32 offset) {
  if (!reader.has(offset, 3)) {
    return std::nullopt;
  }
  const s64 target = static_cast<s64>(offset) + 3 + static_cast<s16>(reader.le16(offset + 1));
  if (target < 0 || target >= static_cast<s64>(reader.size())) {
    return std::nullopt;
  }
  return static_cast<u32>(target);
}

[[nodiscard]] u32 delayTicks(u8 value) { return value == 0 ? 65'536 : value; }

[[nodiscard]] double levelGain(u8 value) {
  // With the request scalar at its normal 0x100, the driver writes
  // (volume * side * 0x100) >> 8 to a 0x3fff-full-scale SPU register.
  // Keeping the default side (64) in this lane makes the independent E1
  // stereo-balance lane compose to the exact source gain.
  return static_cast<double>(value) * 64.0 / 16'383.0;
}

[[nodiscard]] double reverbDepth(u8 value) {
  return static_cast<double>(static_cast<s16>(static_cast<u16>(value) << 8)) / 32'768.0;
}

[[nodiscard]] u16 scaledPitch(u16 pitch, u16 scale) {
  return scale == 0 ? pitch : static_cast<u16>((static_cast<u32>(pitch) * scale) >> 12);
}

[[nodiscard]] double pitchKey(u16 pitch) {
  return pitch == 0 ? 0.0 : 48.0 + 12.0 * std::log2(static_cast<double>(pitch) / 4096.0);
}

struct InitialTrackState {
  u32 sourceSlot = 0;
  u32 start = 0;
  u64 startTick = 0;
  u8 priority = 1;
  u8 program = 0;
  u8 volume = 200;
  u8 left = 64;
  u8 right = 64;
  u16 pitchScale = 0;
  bool reverb = false;
  bool fork = false;
};

struct RuntimeConfig {
  Generation generation = Generation::Ps1;
  std::vector<InitialTrackState> tracks;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config)
      : mode(config.generation == Generation::Ps1 ? 3 : 0),
        depth(config.generation == Generation::Ps1 ? 0.5 : 32767.0 / 32768.0) {}

  void finalizePerformance(PerformanceSequence& performance) {
    // A driver voice can still be keyed when loop-limited rendering stops.
    // Close that final attack at the rendered boundary instead of publishing
    // an accidental zero-duration note.
    for (auto& track : performance.tracks) {
      std::set<u32> continuedNotes;
      for (const auto& automation : track.automations) {
        const auto* pitch = std::get_if<PitchTransitionIntent>(&automation.intent);
        if (pitch != nullptr && pitch->previousNote) {
          continuedNotes.insert(pitch->previousNote->value);
        }
      }
      for (auto& event : track.events) {
        auto* note = std::get_if<NotePerformanceEvent>(&event);
        if (note != nullptr && note->durationTicks == 0 && !continuedNotes.contains(note->note.value)) {
          const u64 available = track.endTick > note->header.tick ? track.endTick - note->header.tick : 1;
          note->durationTicks = static_cast<u32>(std::min<u64>(available, std::numeric_limits<u32>::max()));
        }
      }
    }
  }

  u8 mode = 0;
  double depth = 0.0;
};

struct TrackState : InitialTrackState {
  TrackState(const TrackProgram& track, const RuntimeConfig& config)
      : InitialTrackState(config.tracks.at(track.sourceTrackNumber)) {}

  bool initialized = false;
  std::optional<PerformanceNoteId> note;
  u64 noteStart = 0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void emitReverb() {
    out.reverb(ReverbPerformanceEvent{
        .send = track.reverb ? std::abs(program.depth) : 0.0,
        .leftGain = program.depth,
        .rightGain = program.depth,
        .filterIndex = program.mode,
    });
  }

  void beforeCommand() {
    if (track.initialized) {
      return;
    }
    track.initialized = true;
    out.instrument(instrumentIdentity(track.program));
    out.level(levelGain(track.volume));
    out.stereoBalance(track.left / 64.0, track.right / 64.0);
    emitReverb();
  }

  void closeVoice() {
    if (!track.note) {
      return;
    }
    static_cast<void>(out.setNoteEnd(*track.note, std::max<u64>(vm.tick(), track.noteStart + 1)));
    track.note.reset();
  }

  void attack(double key) {
    closeVoice();
    track.noteStart = vm.tick();
    track.note = out.note(NotePerformanceEvent{
        .key = key,
        .linearVelocity = 1.0,
    });
  }

  void note(u8 key) {
    const u16 pitch = key < kPitchTable.size()
                          ? kPitchTable[key]
                          : static_cast<u16>(std::min(16'383.0, 4096.0 * std::exp2((key - 48) / 12.0)));
    attack(pitchKey(scaledPitch(pitch, track.pitchScale)));
  }

  void volume(u8 value) {
    track.volume = value;
    out.level(levelGain(value));
  }

  void balance(u8 left, u8 right) {
    track.left = left;
    track.right = right;
    out.stereoBalance(left / 64.0, right / 64.0);
  }

  void tone(u8 value) {
    track.program = value;
    track.pitchScale = 0;
    out.instrument(instrumentIdentity(value));
  }

  void changePitch(u16 value) {
    if (!track.note) {
      return;
    }
    const double target = pitchKey(scaledPitch(value, track.pitchScale));
    const PerformanceNoteId previous = *track.note;
    static_cast<void>(out.setNoteEnd(previous, vm.tick()));
    track.note = out.continueVoice(
        previous,
        NotePerformanceEvent{
            .key = target,
            .linearVelocity = 1.0,
        });
    track.noteStart = vm.tick();
  }

  void pitchAttack(u16 value) { attack(pitchKey(scaledPitch(value, track.pitchScale))); }

  void setReverbMode(u8 mode) {
    program.mode = mode;
    emitGlobalReverb();
  }

  void setReverbDepth(u8 depth) {
    program.depth = reverbDepth(depth);
    emitGlobalReverb();
  }

  void emitGlobalReverb() {
    out.reverb(ReverbPerformanceEvent{
        .voiceMask = 0xff,
        .send = std::abs(program.depth),
        .leftGain = program.depth,
        .rightGain = program.depth,
        .filterIndex = program.mode,
    });
  }

  void reverb(bool enabled) {
    track.reverb = enabled;
    emitReverb();
  }

  void pitchCenter(u16 value) { track.pitchScale = value; }

  void priority(u8 value) { track.priority = value; }

  void end() { closeVoice(); }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 offset,
                                                    std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, offset, std::string(kCommandKindPrefix), diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode <= 0x7f) {
    auto event = cursor.command("Wait", SequenceSemantic::Rest);
    event.derived("ticks", delayTicks(opcode), SemanticOperandRole::Duration);
    return event.wait(delayTicks(opcode));
  }
  if (opcode <= 0xdf) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 key = opcode & 0x7f;
    event.derived("key", key, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    return event.invoke<&Playback::note>(key);
  }

  switch (opcode) {
    case 0xe0: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xe1: {
      auto event = cursor.command("Stereo Balance", SequenceSemantic::Pan);
      const u8 left = event.u8("left", SemanticOperandRole::Level);
      const u8 right = event.u8("right", SemanticOperandRole::Level);
      return event.invoke<&Playback::balance>(left, right);
    }
    case 0xe2: {
      auto event = cursor.command("Instrument", SequenceSemantic::Instrument);
      return event.invoke<&Playback::tone>(event.u8("program", SemanticOperandRole::InstrumentProgram));
    }
    case 0xe3: {
      auto event = cursor.noOp("Reserved Tempo Word");
      static_cast<void>(event.u16le("value", SourceValueDisplay::Hex));
      return event;
    }
    case 0xe4: {
      auto event = cursor.command("Pitch", SequenceSemantic::Pitch);
      return event.invoke<&Playback::changePitch>(event.u16le("spu_pitch", SourceValueDisplay::Hex,
                                                              SemanticOperandRole::Pitch));
    }
    case 0xe5: {
      auto event = cursor.command("Key On By Pitch", SequenceSemantic::Note);
      return event.invoke<&Playback::pitchAttack>(event.u16le("spu_pitch", SourceValueDisplay::Hex,
                                                              SemanticOperandRole::Pitch));
    }
    case 0xe6: {
      auto event = cursor.command("Reverb Mode", SequenceSemantic::State);
      return event.invoke<&Playback::setReverbMode>(event.u8("mode"));
    }
    case 0xe7: {
      auto event = cursor.command("Reverb Depth", SequenceSemantic::State);
      return event.invoke<&Playback::setReverbDepth>(event.u8("depth", SemanticOperandRole::Level));
    }
    case 0xe8:
      return cursor.command("Reverb Send On", SequenceSemantic::State).invoke<&Playback::reverb>(true);
    case 0xe9:
      return cursor.command("Reverb Send Off", SequenceSemantic::State).invoke<&Playback::reverb>(false);
    case 0xea: {
      auto event = cursor.command("Pitch Scale", SequenceSemantic::Pitch);
      return event.invoke<&Playback::pitchCenter>(event.u16le("scale", SourceValueDisplay::Hex,
                                                              SemanticOperandRole::Pitch));
    }
    case 0xf0:
      return cursor.command("Key Off", SequenceSemantic::Note).invoke<&Playback::end>();
    case 0xf1: {
      auto event = cursor.command("Priority", SequenceSemantic::State);
      return event.invoke<&Playback::priority>(event.u8("priority"));
    }
    case 0xf8: {
      auto event = cursor.command("Jump", SequenceSemantic::Loop);
      const s16 relative = event.s16le("relative", SourceValueDisplay::SignedDecimal);
      const s64 destination = static_cast<s64>(offset) + 3 + relative;
      if (destination < 0 || destination >= static_cast<s64>(reader.size())) {
        event.warning("Tamsoft jump target is outside the TSQ file");
        return event.end();
      }
      const Address target{static_cast<u64>(destination)};
      event.derived("destination", target, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      return event.declaredLoop(target);
    }
    case 0xf9: {
      auto event = cursor.command("External Channel", SequenceSemantic::State,
                                  CommandPlaybackStatus::AffectsControlFlow);
      const s16 relative = event.s16le("relative", SourceValueDisplay::SignedDecimal);
      const s64 destination = static_cast<s64>(offset) + 3 + relative;
      if (destination < 0 || destination >= static_cast<s64>(reader.size())) {
        event.warning("Tamsoft external-channel target is outside the TSQ file");
        return event;
      }
      const Address target{static_cast<u64>(destination)};
      event.derived("destination", target, SourceValueDisplay::Address, SemanticOperandRole::CallTarget);
      // Layout analysis materializes this cloned driver voice as its own track.
      return event.discoverTarget(target);
    }
    case 0xff:
      return cursor.command("End", SequenceSemantic::End, CommandPlaybackStatus::StopsPlayback)
          .invoke<&Playback::end>()
          .end();
    default:
      return cursor.unsupported("Undefined Event").invoke<&Playback::end>().end();
  }
}

void report(std::vector<Diagnostic>* diagnostics, std::string message, SourceRange range) {
  if (diagnostics != nullptr) {
    diagnostics->push_back(Diagnostic{
        .severity = Severity::Warning,
        .message = std::move(message),
        .range = range,
    });
  }
}

[[nodiscard]] std::vector<InitialTrackState> discoverTracks(ByteReader reader, const SequenceLayout& layout,
                                                            std::vector<Diagnostic>* diagnostics) {
  std::vector<InitialTrackState> tracks;
  tracks.reserve(layout.generation == Generation::Ps2 ? 48 : 24);
  for (const auto& source : layout.tracks) {
    tracks.push_back(InitialTrackState{
        .sourceSlot = source.slot,
        .start = source.offset,
        .priority = source.priority,
    });
  }

  const size_t voiceLimit = layout.generation == Generation::Ps2 ? 48 : 24;
  bool warnedRepeatedFork = false;
  for (size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
    InitialTrackState state = tracks[trackIndex];
    std::set<u32> visited;
    std::vector<u32> forkSites;
    u32 pc = state.start;
    for (u32 commands = 0; commands < kMaximumCommands && reader.has(pc, 1) && !visited.contains(pc); ++commands) {
      visited.insert(pc);
      const u8 opcode = reader.u8At(pc);
      const u32 size = eventSize(opcode);
      if (!reader.has(pc, size)) {
        break;
      }
      if (opcode <= 0x7f) {
        state.startTick += delayTicks(opcode);
      } else {
        switch (opcode) {
          case 0xe0:
            state.volume = reader.u8At(pc + 1);
            break;
          case 0xe1:
            state.left = reader.u8At(pc + 1);
            state.right = reader.u8At(pc + 2);
            break;
          case 0xe2:
            state.program = reader.u8At(pc + 1);
            state.pitchScale = 0;
            break;
          case 0xe8:
            state.reverb = true;
            break;
          case 0xe9:
            state.reverb = false;
            break;
          case 0xea:
            state.pitchScale = reader.le16(pc + 1);
            break;
          case 0xf1:
            state.priority = reader.u8At(pc + 1);
            break;
          case 0xf9:
            if (const auto target = relativeTarget(reader, pc)) {
              auto fork = state;
              fork.start = *target;
              fork.fork = true;
              if (tracks.size() < voiceLimit) {
                tracks.push_back(fork);
              }
              forkSites.push_back(pc);
            }
            break;
          case 0xf8:
            if (const auto target = relativeTarget(reader, pc)) {
              if (!warnedRepeatedFork && *target <= pc &&
                  std::ranges::any_of(forkSites, [&](u32 site) { return site >= *target; })) {
                report(diagnostics,
                       "F9 external-channel creation occurs inside a repeating jump; the first cloned voice is "
                       "preserved without emulating repeated voice allocation",
                       reader.range(pc, 3));
                warnedRepeatedFork = true;
              }
              pc = *target;
              continue;
            }
            return tracks;
          case 0xff:
            commands = kMaximumCommands;
            continue;
          default:
            if (opcode >= 0xe0 && opcode != 0xe3 && opcode != 0xe4 && opcode != 0xe5 && opcode != 0xe6 &&
                opcode != 0xe7 && opcode != 0xf0) {
              commands = kMaximumCommands;
              continue;
            }
            break;
        }
      }
      pc += size;
    }
  }
  if (tracks.size() > voiceLimit) {
    tracks.resize(voiceLimit);
  }
  return tracks;
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, AssetId id, u32 number, const InitialTrackState& source,
                                       SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope scope{
      .reader = reader,
      .bytecodeEnd = static_cast<u32>(reader.size()),
      .maxCommands = kMaximumCommands,
      .sequenceAsset = id,
      .sourceMap = sourceMap,
  };
  TrackProgram track =
      scope.decode(number, source.start, [&](u32 offset) { return decodeCommand(reader, offset, diagnostics); });
  track.sourceTrackNumber = number;
  track.name = source.fork ? fmt::format("Track {} fork", source.sourceSlot + 1)
                           : fmt::format("Track {}", source.sourceSlot + 1);
  if (source.startTick != 0) {
    const Address actualStart = track.startAddress;
    const Address delayedStart{reader.size() + 1 + number};
    track.commands.push_back(SourceCommand{
        .address = delayedStart,
        .flow = CommandFlow::jumpTo(actualStart, Address{delayedStart.value + 1}),
        .execution = CommandExecution{.delayTicks = static_cast<u32>(std::min<u64>(
                                          source.startTick, std::numeric_limits<u32>::max()))},
    });
    std::ranges::sort(track.commands,
                      [](const SourceCommand& left, const SourceCommand& right) {
                        return left.address.value < right.address.value;
                      });
    track.startAddress = delayedStart;
  }
  return track;
}

}  // namespace

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config{
      .commandKindPrefix = std::string(kCommandKindPrefix),
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior = SequenceProgramBehavior{
          .commandLimit = kMaximumCommands,
          .inferLoopsFromRepeatedState = false,
          .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
      },
  };
  return config;
}

SequenceProgram parseSequence(ByteReader reader, AssetId id, const SequenceLayout& layout,
                              SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  SequenceProgram program = sequenceConfig().makeProgram();
  program.behavior.initialTempoMicrosecondsPerQuarter =
      layout.generation == Generation::Ps2 ? kPs2InitialTempo : kPs1InitialTempo;
  auto tracks = discoverTracks(reader, layout, diagnostics);
  program.runtime = makeCompiledRuntime<Cursor, ProgramState>(RuntimeConfig{
      .generation = layout.generation,
      .tracks = tracks,
  });

  if (sourceMap != nullptr) {
    sourceMap->table("Song Table", reader.range(0, layout.tableSize))
        .kind("tamsoft-ps1-song-table")
        .owner(ObjectRefs::sequence(id));
    sourceMap->header("Song Entry", reader.range(layout.song * 4, 4))
        .kind("tamsoft-ps1-song-entry")
        .owner(ObjectRefs::sequence(id))
        .field("type", reader.range(layout.song * 4, 2), layout.type)
        .field("offset", reader.range(layout.song * 4 + 2, 2), layout.headerOffset,
               SourceValueDisplay::Address);
    if (layout.headerSize != 0) {
      sourceMap->table("Track Records", reader.range(layout.headerOffset, layout.headerSize))
          .kind("tamsoft-ps1-track-records")
          .owner(ObjectRefs::sequence(id));
    }
  }

  for (u32 number = 0; number < tracks.size(); ++number) {
    program.tracks.push_back(decodeTrack(reader, id, number, tracks[number], sourceMap, diagnostics));
  }
  return program;
}

}  // namespace vgmtrans::formats::tamsoft_ps1
