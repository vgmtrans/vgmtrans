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
constexpr u8 kStereoBalanceUnit = 64;
constexpr double kSpuRegisterMaximum = 16'383.0;
constexpr u16 kSpuUnityPitch = 4096;
constexpr double kSpuUnityKey = 48.0;
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

[[nodiscard]] std::optional<Address> relativeTarget(ByteReader reader, u32 offset, s16 relative) {
  const s64 target = static_cast<s64>(offset) + 3 + relative;
  if (target < 0 || target >= static_cast<s64>(reader.size())) {
    return std::nullopt;
  }
  return Address{static_cast<u64>(target)};
}

[[nodiscard]] u32 delayTicks(u8 value) { return value == 0 ? 65'536 : value; }

[[nodiscard]] double levelGain(u8 value) {
  // With the request scalar at its normal 0x100, the driver writes
  // (volume * side * 0x100) >> 8 to a 0x3fff-full-scale SPU register.
  // Keeping the default side (64) in this lane makes the independent E1
  // stereo-balance lane compose to the exact source gain.
  return static_cast<double>(value) * kStereoBalanceUnit / kSpuRegisterMaximum;
}

[[nodiscard]] double stereoBalanceGain(u8 value) {
  return static_cast<double>(value) / kStereoBalanceUnit;
}

[[nodiscard]] double signedReverbDepth(u8 value) {
  return static_cast<double>(static_cast<s16>(static_cast<u16>(value) << 8)) / 32'768.0;
}

[[nodiscard]] u16 scaledPitch(u16 pitch, u16 scale) {
  return scale == 0 ? pitch : static_cast<u16>((static_cast<u32>(pitch) * scale) >> 12);
}

[[nodiscard]] double pitchKey(u16 pitch) {
  return pitch == 0 ? 0.0
                    : kSpuUnityKey + 12.0 * std::log2(static_cast<double>(pitch) / kSpuUnityPitch);
}

struct VoiceState {
  u8 program = 0;
  u8 volume = 200;
  u8 left = kStereoBalanceUnit;
  u8 right = kStereoBalanceUnit;
  u16 pitchScale = 0;
  bool reverb = false;

  void selectProgram(u8 value) {
    program = value;
    pitchScale = 0;
  }
};

struct TrackSeed {
  VoiceState voice;
  u32 sourceSlot = 0;
  u32 start = 0;
  u64 startDelayTicks = 0;
  bool fork = false;
};

struct RuntimeConfig {
  Generation generation = Generation::Ps1;
  std::vector<TrackSeed> seeds;
};

void closeDanglingNotes(PerformanceSequence& performance) {
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

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config)
      : reverbMode(config.generation == Generation::Ps1 ? 3 : 0),
        reverbDepth(config.generation == Generation::Ps1 ? 0.5 : 32767.0 / 32768.0) {}

  void finalizePerformance(PerformanceSequence& performance) { closeDanglingNotes(performance); }

  u8 reverbMode = 0;
  double reverbDepth = 0.0;
};

struct TrackState : VoiceState {
  TrackState(const TrackProgram& track, const RuntimeConfig& config)
      : VoiceState(config.seeds.at(track.sourceTrackNumber).voice) {}

  bool initialized = false;
  std::optional<PerformanceNoteId> note;
  u64 noteStart = 0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& programState;

  void emitReverb() {
    out.reverb(ReverbPerformanceEvent{
        .send = track.reverb ? std::abs(programState.reverbDepth) : 0.0,
        .leftGain = programState.reverbDepth,
        .rightGain = programState.reverbDepth,
        .filterIndex = programState.reverbMode,
    });
  }

  void beforeCommand() {
    if (track.initialized) {
      return;
    }
    track.initialized = true;
    out.instrument(instrumentIdentity(track.program));
    out.level(levelGain(track.volume));
    out.stereoBalance(stereoBalanceGain(track.left), stereoBalanceGain(track.right));
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

  void keyOn(u8 key) {
    const u16 pitch = key < kPitchTable.size()
                          ? kPitchTable[key]
                          : static_cast<u16>(std::min(kSpuRegisterMaximum,
                                                      kSpuUnityPitch * std::exp2((key - kSpuUnityKey) / 12.0)));
    attack(pitchKey(scaledPitch(pitch, track.pitchScale)));
  }

  void setVolume(u8 value) {
    track.volume = value;
    out.level(levelGain(value));
  }

  void setStereoBalance(u8 left, u8 right) {
    track.left = left;
    track.right = right;
    out.stereoBalance(stereoBalanceGain(left), stereoBalanceGain(right));
  }

  void setInstrument(u8 value) {
    track.selectProgram(value);
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

  void keyOnByPitch(u16 value) { attack(pitchKey(scaledPitch(value, track.pitchScale))); }

  void setReverbMode(u8 mode) {
    programState.reverbMode = mode;
    emitGlobalReverb();
  }

  void setReverbDepth(u8 depth) {
    programState.reverbDepth = signedReverbDepth(depth);
    emitGlobalReverb();
  }

  void emitGlobalReverb() {
    out.reverb(ReverbPerformanceEvent{
        .voiceMask = 0xff,
        .send = std::abs(programState.reverbDepth),
        .leftGain = programState.reverbDepth,
        .rightGain = programState.reverbDepth,
        .filterIndex = programState.reverbMode,
    });
  }

  void setReverbSend(bool enabled) {
    track.reverb = enabled;
    emitReverb();
  }

  void setPitchScale(u16 value) { track.pitchScale = value; }

  void keyOff() { closeVoice(); }
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
    return event.invoke<&Playback::keyOn>(key);
  }

  switch (opcode) {
    case 0xe0: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::setVolume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xe1: {
      auto event = cursor.command("Stereo Balance", SequenceSemantic::Pan);
      const u8 left = event.u8("left", SemanticOperandRole::Level);
      const u8 right = event.u8("right", SemanticOperandRole::Level);
      return event.invoke<&Playback::setStereoBalance>(left, right);
    }
    case 0xe2: {
      auto event = cursor.command("Instrument", SequenceSemantic::Instrument);
      return event.invoke<&Playback::setInstrument>(event.u8("program", SemanticOperandRole::InstrumentProgram));
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
      return event.invoke<&Playback::keyOnByPitch>(event.u16le("spu_pitch", SourceValueDisplay::Hex,
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
      return cursor.command("Reverb Send On", SequenceSemantic::State).invoke<&Playback::setReverbSend>(true);
    case 0xe9:
      return cursor.command("Reverb Send Off", SequenceSemantic::State).invoke<&Playback::setReverbSend>(false);
    case 0xea: {
      auto event = cursor.command("Pitch Scale", SequenceSemantic::Pitch);
      return event.invoke<&Playback::setPitchScale>(event.u16le("scale", SourceValueDisplay::Hex,
                                                                SemanticOperandRole::Pitch));
    }
    case 0xf0:
      return cursor.command("Key Off", SequenceSemantic::Note).invoke<&Playback::keyOff>();
    case 0xf1: {
      auto event = cursor.command("Priority", SequenceSemantic::State, CommandPlaybackStatus::SourceOnly);
      static_cast<void>(event.u8("priority"));
      return event;
    }
    case 0xf8: {
      auto event = cursor.command("Jump", SequenceSemantic::Loop);
      const s16 relative = event.s16le("relative", SourceValueDisplay::SignedDecimal);
      const auto target = relativeTarget(reader, offset, relative);
      if (!target) {
        event.warning("Tamsoft jump target is outside the TSQ file");
        return event.end();
      }
      event.derived("destination", *target, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      return event.declaredLoop(*target);
    }
    case 0xf9: {
      auto event = cursor.command("External Channel", SequenceSemantic::State,
                                  CommandPlaybackStatus::AffectsControlFlow);
      const s16 relative = event.s16le("relative", SourceValueDisplay::SignedDecimal);
      const auto target = relativeTarget(reader, offset, relative);
      if (!target) {
        event.warning("Tamsoft external-channel target is outside the TSQ file");
        return event;
      }
      event.derived("destination", *target, SourceValueDisplay::Address, SemanticOperandRole::CallTarget);
      // Layout analysis materializes this cloned driver voice as its own track.
      return event.discoverTarget(*target);
    }
    case 0xff:
      return cursor.command("End", SequenceSemantic::End, CommandPlaybackStatus::StopsPlayback)
          .invoke<&Playback::keyOff>()
          .end();
    default:
      return cursor.unsupported("Undefined Event").invoke<&Playback::keyOff>().end();
  }
}

void reportWarning(std::vector<Diagnostic>* diagnostics, std::string message, SourceRange range) {
  if (diagnostics != nullptr) {
    diagnostics->push_back(Diagnostic{
        .severity = Severity::Warning,
        .message = std::move(message),
        .range = range,
    });
  }
}

void updateInheritedState(TrackSeed& seed, ByteReader reader, u32 offset,
                          const DecodedBytecodeCommand& command) {
  if (command.presentation.playback == CommandPlaybackStatus::Unsupported) {
    return;
  }

  if (command.opcode <= 0x7f) {
    seed.startDelayTicks += delayTicks(command.opcode);
    return;
  }

  switch (command.opcode) {
    case 0xe0:
      seed.voice.volume = reader.u8At(offset + 1);
      break;
    case 0xe1:
      seed.voice.left = reader.u8At(offset + 1);
      seed.voice.right = reader.u8At(offset + 2);
      break;
    case 0xe2:
      seed.voice.selectProgram(reader.u8At(offset + 1));
      break;
    case 0xe8:
      seed.voice.reverb = true;
      break;
    case 0xe9:
      seed.voice.reverb = false;
      break;
    case 0xea:
      seed.voice.pitchScale = reader.le16(offset + 1);
      break;
    default:
      break;
  }
}

[[nodiscard]] std::vector<TrackSeed> discoverVoiceSeeds(ByteReader reader, const SequenceLayout& layout,
                                                        std::vector<Diagnostic>* diagnostics) {
  const size_t voiceLimit = layout.generation == Generation::Ps2 ? kPs2VoiceCount : kPs1VoiceCount;
  std::vector<TrackSeed> seeds;
  seeds.reserve(voiceLimit);
  for (const auto& source : layout.tracks) {
    seeds.push_back(TrackSeed{
        .sourceSlot = source.slot,
        .start = source.offset,
    });
  }

  bool warnedRepeatedFork = false;
  for (size_t seedIndex = 0; seedIndex < seeds.size(); ++seedIndex) {
    TrackSeed state = seeds[seedIndex];
    std::set<u32> visited;
    std::vector<u32> forkSites;
    u32 pc = state.start;
    for (u32 commands = 0; commands < kMaximumCommands && reader.has(pc, 1) && !visited.contains(pc); ++commands) {
      visited.insert(pc);
      const auto command = decodeCommand(reader, pc, nullptr);
      updateInheritedState(state, reader, pc, command);

      if (command.opcode == 0xf9 && !command.discoveryTargets.empty()) {
        auto fork = state;
        fork.start = static_cast<u32>(command.discoveryTargets.front().value);
        fork.fork = true;
        if (seeds.size() < voiceLimit) {
          seeds.push_back(fork);
        }
        forkSites.push_back(pc);
      }

      if (command.flow.unconditionalJump()) {
        const u32 target = static_cast<u32>(command.flow.defaultDestination()->value);
        if (!warnedRepeatedFork && target <= pc &&
            std::ranges::any_of(forkSites, [&](u32 site) { return site >= target; })) {
          reportWarning(diagnostics,
                        "F9 external-channel creation occurs inside a repeating jump; the first cloned voice is "
                        "preserved without emulating repeated voice allocation",
                        command.range);
          warnedRepeatedFork = true;
        }
        pc = target;
        continue;
      }

      const auto next = command.flow.discoveryContinuation();
      if (!next) {
        break;
      }
      pc = static_cast<u32>(next->value);
    }
  }
  return seeds;
}

void delayTrackStart(TrackProgram& track, u64 ticks, Address delayedStart) {
  if (ticks == 0) {
    return;
  }

  const Address actualStart = track.startAddress;
  track.commands.push_back(SourceCommand{
      .address = delayedStart,
      .flow = CommandFlow::jumpTo(actualStart, Address{delayedStart.value + 1}),
      .execution = CommandExecution{
          .delayTicks = static_cast<u32>(std::min<u64>(ticks, std::numeric_limits<u32>::max()))},
  });
  track.startAddress = delayedStart;
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, AssetId id, u32 number, const TrackSeed& seed,
                                       SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope scope{
      .reader = reader,
      .bytecodeEnd = static_cast<u32>(reader.size()),
      .maxCommands = kMaximumCommands,
      .sequenceAsset = id,
      .sourceMap = sourceMap,
  };
  TrackProgram track =
      scope.decode(number, seed.start, [&](u32 offset) { return decodeCommand(reader, offset, diagnostics); });
  track.sourceTrackNumber = number;
  track.name = seed.fork ? fmt::format("Track {} fork", seed.sourceSlot + 1)
                         : fmt::format("Track {}", seed.sourceSlot + 1);
  // Synthetic command addresses follow every source byte, so appending keeps the
  // command list ordered without a second sort.
  delayTrackStart(track, seed.startDelayTicks, Address{reader.size() + 1 + number});
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
  auto seeds = discoverVoiceSeeds(reader, layout, diagnostics);

  if (sourceMap != nullptr) {
    const u32 songEntry = layout.song * kSongEntrySize;
    sourceMap->table("Song Table", reader.range(0, layout.tableSize))
        .kind("tamsoft-ps1-song-table")
        .owner(ObjectRefs::sequence(id));
    sourceMap->header("Song Entry", reader.range(songEntry, kSongEntrySize))
        .kind("tamsoft-ps1-song-entry")
        .owner(ObjectRefs::sequence(id))
        .field("type", reader.range(songEntry, 2), layout.type)
        .field("offset", reader.range(songEntry + 2, 2), layout.headerOffset, SourceValueDisplay::Address);
    if (layout.headerSize != 0) {
      sourceMap->table("Track Records", reader.range(layout.headerOffset, layout.headerSize))
          .kind("tamsoft-ps1-track-records")
          .owner(ObjectRefs::sequence(id));
    }
  }

  for (u32 number = 0; number < seeds.size(); ++number) {
    program.tracks.push_back(decodeTrack(reader, id, number, seeds[number], sourceMap, diagnostics));
  }
  program.runtime = makeCompiledRuntime<Cursor, ProgramState>(RuntimeConfig{
      .generation = layout.generation,
      .seeds = std::move(seeds),
  });
  return program;
}

}  // namespace vgmtrans::formats::tamsoft_ps1
