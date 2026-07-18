/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsSequence.h"

#include "value/base/LevelScale.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/SemanticCommand.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <any>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 262144;
constexpr std::string_view kSseqSignature{"SSEQ\xff\xfe\x00\x01", 8};
constexpr u32 kSseqFileSizeOffset = 0x08;
constexpr u32 kSseqDataOffsetField = 0x18;
constexpr u32 kSseqHeaderSize = 0x1c;

struct PendingBlock {
  u32 offset = 0;
  bool callTarget = false;
};

// Only registers that persist from one executed source command to the next
// belong here. Source bounds and relative-address bases are decode concerns.
struct TrackState {
  bool noteWait = false;
  s32 transpose = 0;
  u8 pitchBendRangeSemitones = 2;
};

using Operands = SemanticCommandArgs;

// Playback is the single boundary for state mutation and emitted performance
// events. Command definitions never need the VM's type-erased state directly.
struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;

  [[nodiscard]] Effects note(u8 sourceKey, u8 velocity, u32 duration) {
    const s32 key = std::clamp<s32>(static_cast<s32>(sourceKey) + track.transpose, 0, 127);
    out.note(static_cast<double>(key), LevelScale::linearFromMidi7(velocity), duration);
    return track.noteWait ? Effects::wait(duration) : Effects{};
  }
};

// NDS addresses are unsigned 24-bit offsets from the start of SSEQ data.
// Decode resolves them to absolute source addresses so playback never needs
// the source container's layout.
class Decode : public SemanticCommandDecoder {
public:
  Decode(ByteReader reader, u32 begin, u32 end, u32 sequenceDataBase, u32 sequenceEnd,
         std::vector<Diagnostic>* diagnostics)
      : SemanticCommandDecoder(reader, begin, end, diagnostics), sequenceDataBase_(sequenceDataBase),
        sequenceEnd_(sequenceEnd) {}

  [[nodiscard]] Address targetAddress(std::string_view name, SemanticOperandRole validRole) {
    const auto relative = rawU24le("relative", SourceValueDisplay::Address);
    const Address destination{sequenceDataBase_ + relative.value};
    resolvedValue(name, relative, destination, SourceValueDisplay::Address,
                  targetIsValid(destination) ? validRole : SemanticOperandRole::Address);
    return destination;
  }

  [[nodiscard]] bool targetIsValid(Address destination) const noexcept { return destination.value < sequenceEnd_; }

  [[nodiscard]] std::optional<Address> controlTarget(std::string_view name, SemanticOperandRole role,
                                                     std::string_view invalidTargetWarning) {
    const Address destination = targetAddress(name, role);
    if (!ok()) {
      return std::nullopt;
    }
    if (!targetIsValid(destination)) {
      warning(std::string(invalidTargetWarning));
      terminate();
      return std::nullopt;
    }
    return destination;
  }

private:
  u32 sequenceDataBase_ = 0;
  u32 sequenceEnd_ = 0;
};

using DecodeFunction = void (*)(Decode&);
using ExecuteFunction = Effects (*)(Operands, Playback&);

struct CommandDefinition {
  DecodedCommandPresentation presentation;
  DecodeFunction decode = nullptr;
  ExecuteFunction execute = nullptr;
  u8 opaqueOperandBytes = 0;
};

[[nodiscard]] CommandDefinition command(std::string_view label, SequenceSemantic semantic, DecodeFunction decode,
                                        ExecuteFunction execute,
                                        CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback,
                                        std::string_view localKind = {}) {
  const std::string kind = localKind.empty() ? sourceLocalKind(label) : std::string(localKind);
  return CommandDefinition{
      .presentation =
          DecodedCommandPresentation{
              .label = std::string(label),
              .localKind = kind,
              .detailKind = "nds." + kind,
              .semantic = semantic,
              .playback = playback,
          },
      .decode = decode,
      .execute = execute,
  };
}

[[nodiscard]] CommandDefinition sourceOnlyCommand(std::string_view label, DecodeFunction decode,
                                                  std::string_view localKind = {}) {
  return command(label, SequenceSemantic::Meta, decode, nullptr, CommandPlaybackStatus::SourceOnly, localKind);
}

[[nodiscard]] CommandDefinition opaqueCommand(std::string_view label, u8 operandBytes,
                                              std::string_view localKind = {}) {
  auto definition = sourceOnlyCommand(label, nullptr, localKind);
  definition.opaqueOperandBytes = operandBytes;
  return definition;
}

[[nodiscard]] CommandDefinition terminalCommand(std::string_view label, SequenceSemantic semantic,
                                                DecodeFunction decode, CommandPlaybackStatus playback,
                                                std::string_view localKind = {}) {
  return command(label, semantic, decode, nullptr, playback, localKind);
}

using CommandProfile = std::array<CommandDefinition, 0x80>;

// Each profile entry keeps source decoding and playback together. The opaque
// entries are intentionally source-only: VGMTrans preserves their bytes and
// names without pretending to emulate NDS variables or conditionals.
[[nodiscard]] CommandProfile makeProfile() {
  const auto unknown = terminalCommand(
      "Unknown Opcode", SequenceSemantic::Unsupported,
      [](Decode& d) {
        d.warning("Unknown NDS SSEQ opcode stopped playback");
        d.terminate();
      },
      CommandPlaybackStatus::Unsupported, "unknown");
  CommandProfile profile;
  profile.fill(unknown);

  profile[0x93 - 0x80] = sourceOnlyCommand("Open Track", [](Decode& d) {
    d.u8("track");
    static_cast<void>(d.targetAddress("destination", SemanticOperandRole::Address));
  });
  profile[0xa0 - 0x80] = opaqueCommand("Cmd with Random Value", 5, "random-value");
  profile[0xa1 - 0x80] = opaqueCommand("Cmd with Variable", 2, "variable-command");
  profile[0xa2 - 0x80] = opaqueCommand("If", 0);
  profile[0xb0 - 0x80] = opaqueCommand("Set Variable", 3);
  profile[0xb1 - 0x80] = opaqueCommand("Add Variable", 3);
  profile[0xb2 - 0x80] = opaqueCommand("Sub Variable", 3);
  profile[0xb3 - 0x80] = opaqueCommand("Mul Variable", 3);
  profile[0xb4 - 0x80] = opaqueCommand("Div Variable", 3);
  profile[0xb5 - 0x80] = opaqueCommand("Shift Variable", 3);
  profile[0xb6 - 0x80] = opaqueCommand("Rand Variable", 3);
  profile[0xb8 - 0x80] = opaqueCommand("If Variable ==", 3, "if-variable-equal");
  profile[0xb9 - 0x80] = opaqueCommand("If Variable >=", 3, "if-variable-greater-equal");
  profile[0xba - 0x80] = opaqueCommand("If Variable >", 3, "if-variable-greater");
  profile[0xbb - 0x80] = opaqueCommand("If Variable <=", 3, "if-variable-less-equal");
  profile[0xbc - 0x80] = opaqueCommand("If Variable <", 3, "if-variable-less");
  profile[0xbd - 0x80] = opaqueCommand("If Variable !=", 3, "if-variable-not-equal");
  profile[0xc2 - 0x80] = opaqueCommand("Master Volume", 1);
  profile[0xc6 - 0x80] = opaqueCommand("Priority", 1);
  profile[0xc8 - 0x80] = opaqueCommand("Tie", 1);
  profile[0xc9 - 0x80] = opaqueCommand("Portamento Control", 1);
  profile[0xcb - 0x80] = opaqueCommand("Modulation Speed", 1);
  profile[0xcc - 0x80] = opaqueCommand("Modulation Type", 1);
  profile[0xcd - 0x80] = opaqueCommand("Modulation Range", 1);
  profile[0xd0 - 0x80] = opaqueCommand("Attack Rate", 1);
  profile[0xd1 - 0x80] = opaqueCommand("Decay Rate", 1);
  profile[0xd2 - 0x80] = opaqueCommand("Sustain Level", 1);
  profile[0xd3 - 0x80] = opaqueCommand("Release Rate", 1);
  profile[0xd4 - 0x80] = opaqueCommand("Loop Start", 1);
  profile[0xd6 - 0x80] = opaqueCommand("Print Variable", 1);
  profile[0xe0 - 0x80] = opaqueCommand("Modulation Delay", 2);
  profile[0xe3 - 0x80] = opaqueCommand("Sweep Pitch", 2);
  profile[0xfc - 0x80] = opaqueCommand("Loop End", 0);
  profile[0xfe - 0x80] = sourceOnlyCommand("Allocate Track", [](Decode& d) { d.u16le("track_mask"); });

  profile[0x80 - 0x80] = command(
      "Rest", SequenceSemantic::Rest,
      [](Decode& d) { d.varLen("duration", SourceValueDisplay::Default, SemanticOperandRole::Duration); },
      [](Operands a, Playback&) { return Effects::wait(a.u32("duration")); });

  profile[0x81 - 0x80] = command(
      "Program", SequenceSemantic::Program,
      [](Decode& d) {
        const u32 raw = d.varLen("raw");
        d.derived("bank", raw >> 7, SourceValueDisplay::Default, SemanticOperandRole::InstrumentBank);
        d.derived("program", raw & 0x7f, SourceValueDisplay::Default, SemanticOperandRole::InstrumentProgram);
      },
      [](Operands a, Playback& p) {
        p.out.instrument(a.u32("bank"), a.u32("program"));
        return Effects{};
      });

  profile[0x94 - 0x80] = command(
      "Jump", SequenceSemantic::Jump,
      [](Decode& d) {
        if (const auto destination =
                d.controlTarget("destination", SemanticOperandRole::JumpTarget, "Jump target outside sequence data")) {
          d.jumpTo(*destination);
        }
      },
      [](Operands a, Playback& p) { return Effects{.step = p.vm.jump(a.address("destination"))}; },
      CommandPlaybackStatus::AffectsControlFlow);

  profile[0x95 - 0x80] = command(
      "Call", SequenceSemantic::Call,
      [](Decode& d) {
        if (const auto destination =
                d.controlTarget("destination", SemanticOperandRole::CallTarget, "Call target outside sequence data")) {
          d.callTo(*destination);
        }
      },
      [](Operands a, Playback& p) { return Effects{.step = p.vm.call(a.address("destination"))}; },
      CommandPlaybackStatus::AffectsControlFlow);

  profile[0x96 - 0x80] = terminalCommand(
      "Unsupported Command", SequenceSemantic::Unsupported,
      [](Decode& d) {
        d.warning("Unsupported NDS SSEQ command stopped playback");
        d.terminate();
      },
      CommandPlaybackStatus::Unsupported, "unsupported");

  profile[0xc0 - 0x80] = command(
      "Pan", SequenceSemantic::Pan,
      [](Decode& d) {
        d.resolved("position", d.rawU8("pan"),
                   [](u8 raw) { return std::clamp((static_cast<double>(raw) / 63.5) - 1.0, -1.0, 1.0); });
      },
      [](Operands a, Playback& p) {
        p.out.pan(a.f64("position"));
        return Effects{};
      });

  profile[0xc1 - 0x80] = command(
      "Volume", SequenceSemantic::Level,
      [](Decode& d) { d.resolved("linear_gain", d.rawU8("volume"), LevelScale::linearFromMidi7); },
      [](Operands a, Playback& p) {
        p.out.level(a.f64("linear_gain"));
        return Effects{};
      });

  profile[0xc3 - 0x80] = command(
      "Transpose", SequenceSemantic::State, [](Decode& d) { d.s8("semitones"); },
      [](Operands a, Playback& p) {
        p.track.transpose = a.s8("semitones");
        return Effects{};
      });

  profile[0xc4 - 0x80] = command(
      "Pitch Bend", SequenceSemantic::Pitch,
      [](Decode& d) { d.resolved("fraction", d.rawS8("bend"), [](s8 bend) { return bend / 128.0; }); },
      [](Operands a, Playback& p) {
        p.out.pitchBend(a.f64("fraction") * p.track.pitchBendRangeSemitones);
        return Effects{};
      });

  profile[0xc5 - 0x80] = command(
      "Pitch Bend Range", SequenceSemantic::Pitch, [](Decode& d) { d.u8("semitones"); },
      [](Operands a, Playback& p) {
        p.track.pitchBendRangeSemitones = a.u8("semitones");
        p.out.pitchBendRange(p.track.pitchBendRangeSemitones);
        return Effects{};
      });

  profile[0xc7 - 0x80] = command(
      "Note Wait", SequenceSemantic::State,
      [](Decode& d) {
        const auto raw = d.rawU8("raw");
        d.resolvedValue("enabled", raw, raw.value != 0, SourceValueDisplay::Boolean);
      },
      [](Operands a, Playback& p) {
        p.track.noteWait = a.boolean("enabled");
        return Effects{};
      });

  profile[0xca - 0x80] = command(
      "Modulation Depth", SequenceSemantic::Modulation,
      [](Decode& d) {
        d.resolved("amount", d.rawU8("depth"),
                   [](u8 depth) { return std::clamp(static_cast<double>(depth) / 127.0, 0.0, 1.0); });
      },
      [](Operands a, Playback& p) {
        p.out.modulation(ModulationPerformanceTarget::VibratoDepth, a.f64("amount"));
        return Effects{};
      });

  profile[0xce - 0x80] = command(
      "Portamento", SequenceSemantic::Portamento,
      [](Decode& d) {
        const auto raw = d.rawU8("raw");
        d.resolvedValue("enabled", raw, raw.value != 0, SourceValueDisplay::Boolean);
      },
      [](Operands a, Playback& p) {
        p.out.portamentoEnable(a.boolean("enabled"));
        return Effects{};
      });

  profile[0xcf - 0x80] = command(
      "Portamento Time", SequenceSemantic::Portamento,
      [](Decode& d) { d.resolved("milliseconds", d.rawU8("time"), [](u8 time) { return static_cast<double>(time); }); },
      [](Operands a, Playback& p) {
        p.out.portamentoTime(a.f64("milliseconds"));
        return Effects{};
      });

  profile[0xd5 - 0x80] = command(
      "Expression", SequenceSemantic::Level,
      [](Decode& d) { d.resolved("linear_gain", d.rawU8("expression"), LevelScale::linearFromMidi7); },
      [](Operands a, Playback& p) {
        p.out.expression(a.f64("linear_gain"));
        return Effects{};
      });

  profile[0xe1 - 0x80] = command(
      "Tempo", SequenceSemantic::Tempo,
      [](Decode& d) {
        const auto bpm = d.rawU16le("bpm");
        const u32 microsecondsPerQuarter = bpm.value == 0 ? 0 : static_cast<u32>(std::round(60000000.0 / bpm.value));
        d.resolvedValue("microseconds_per_quarter", bpm, microsecondsPerQuarter);
      },
      [](Operands a, Playback& p) {
        const u32 microsecondsPerQuarter = a.u32("microseconds_per_quarter");
        if (microsecondsPerQuarter != 0) {
          p.out.tempo(microsecondsPerQuarter);
        }
        return Effects{};
      });

  profile[0xfd - 0x80] = command(
      "Return", SequenceSemantic::Return, [](Decode& d) { d.return_(); },
      [](Operands, Playback& p) { return Effects{.step = p.vm.return_()}; }, CommandPlaybackStatus::AffectsControlFlow);

  profile[0xff - 0x80] = terminalCommand(
      "End", SequenceSemantic::End, [](Decode& d) { d.terminate(); }, CommandPlaybackStatus::StopsPlayback);

  return profile;
}

[[nodiscard]] const CommandDefinition& noteCommand() {
  static const CommandDefinition definition = command(
      "Note", SequenceSemantic::Note,
      [](Decode& d) {
        d.opcodeValue("key", d.opcode(), SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
        d.u8("velocity", SemanticOperandRole::Level);
        d.varLen("duration", SourceValueDisplay::Default, SemanticOperandRole::Duration);
      },
      [](Operands a, Playback& p) { return p.note(a.u8("key"), a.u8("velocity"), a.u32("duration")); });
  return definition;
}

[[nodiscard]] const CommandDefinition& definitionFor(u8 opcode) {
  if (opcode <= 0x7f) {
    return noteCommand();
  }
  static const CommandProfile profile = makeProfile();
  return profile[opcode - 0x80];
}

[[nodiscard]] CommandDefinition truncatedCommand() {
  return terminalCommand("Truncated Command", SequenceSemantic::Unsupported, nullptr,
                         CommandPlaybackStatus::Unsupported, "truncated");
}

// Decode is the only place that reads command bytes. Execution later selects
// the same definition by opcode and consumes only the stored named operands.
[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end, u32 sequenceDataBase,
                                                   u32 sequenceEnd, std::vector<Diagnostic>* diagnostics) {
  Decode decode(reader, begin, end, sequenceDataBase, sequenceEnd, diagnostics);
  if (!decode.hasOpcode()) {
    return decode.finish(truncatedCommand().presentation);
  }

  const CommandDefinition& definition = definitionFor(decode.opcode());
  if (definition.decode != nullptr) {
    definition.decode(decode);
  } else if (definition.opaqueOperandBytes != 0) {
    static_cast<void>(decode.rawBytes("bytes", definition.opaqueOperandBytes));
  }
  if (!decode.ok()) {
    return decode.finish(truncatedCommand().presentation);
  }
  return decode.finish(definition.presentation);
}

[[nodiscard]] std::any createTrackState(const SequenceProgram&, const TrackProgram&) {
  return TrackState{};
}

[[nodiscard]] Effects executeCommand(const SourceCommand& command, std::any&, std::any& trackStateValue,
                                     PerformanceEmitter& out, VmApi& vm) {
  auto& track = std::any_cast<TrackState&>(trackStateValue);
  Playback playback{.track = track, .out = out, .vm = vm};

  if (command.flow.terminal) {
    return Effects{.step = vm.end()};
  }
  const CommandDefinition& definition = definitionFor(command.opcode);
  return definition.execute != nullptr ? definition.execute(Operands{command}, playback) : Effects{};
}

[[nodiscard]] SequenceDialect makeDialect() {
  return SequenceDialect{
      .id = DialectId{.value = std::string(kNdsSequenceDialectId)},
      .commandDetailKindPrefix = "nds",
      .timebase = Timebase{.ppqn = 0x30},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .commandLimit = kMaxTrackCommands,
          },
      .createSemanticTrackState = createTrackState,
      .executeSemantic = executeCommand,
  };
}

[[nodiscard]] TrackProgram makeTrack(u32 startOffset, u32 trackIndex) {
  return TrackProgram{
      .id = TrackId{trackIndex},
      .sourceTrackNumber = trackIndex,
      .startAddress = Address{startOffset},
  };
}

[[nodiscard]] DecodedBytecodeCommand terminalRecoveryCommand(ByteReader reader, u32 offset) {
  const SourceRange range = reader.range(offset, reader.has(offset, 1) ? 1 : 0);
  return DecodedBytecodeCommand{
      .range = range,
      .opcode = range.size == 0 ? u8{0} : reader.u8At(offset),
      .encodedSize = std::max<u32>(1, range.size),
      .flow = DecodeFlow::terminalFlow(),
      .presentation =
          DecodedCommandPresentation{
              .label = "Recovery Stop",
              .localKind = "recovery-stop",
              .detailKind = "nds.recovery-stop",
              .semantic = SequenceSemantic::Unsupported,
              .playback = CommandPlaybackStatus::StopsPlayback,
          },
      .retainBytes = false,
  };
}

// Malformed SDAT FAT ranges can overlap a real call target by one byte. This
// exceptional walker keeps the normal semantic command decoder, adding only
// the overlap stop needed to avoid swallowing the subroutine's first byte.
[[nodiscard]] TrackProgram decodeMalformedSdatRangeTrack(ByteReader reader, TrackDecodeInput input) {
  TrackProgram track = makeTrack(input.startOffset, input.trackIndex);
  TrackProgramBuilder builder{track};
  const auto trackAnnotation = createSequenceTrackAnnotation(reader, input);
  std::set<u32> decodedOffsets;
  std::set<u32> callTargetOffsets;
  std::vector<PendingBlock> pendingBlocks{{.offset = input.startOffset}};
  u32 decodedCommands = 0;
  const u32 sequenceDataBase = input.sequenceOffset + kSseqHeaderSize;

  const auto decodeAt = [&](u32 offset) {
    auto decoded =
        decodeCommand(reader, offset, input.bytecodeEnd, sequenceDataBase, input.sequenceEnd, input.diagnostics);
    decoded.annotation = projectDecodedCommand(input.sourceMap, decoded, trackAnnotation);
    return decoded;
  };

  while (!pendingBlocks.empty()) {
    const PendingBlock block = pendingBlocks.back();
    pendingBlocks.pop_back();
    u32 offset = block.offset;

    while (hasBytecodeBytes(reader, offset, 1, input.bytecodeEnd) && decodedCommands++ < input.maxCommands) {
      const u32 begin = offset;
      if (!decodedOffsets.insert(begin).second) {
        break;
      }

      auto decoded = decodeAt(begin);
      if (!block.callTarget) {
        const auto overlap = std::ranges::find_if(
            callTargetOffsets, [&](u32 target) { return begin < target && target < decoded.range.endOffset(); });
        if (overlap != callTargetOffsets.end()) {
          auto recovery = terminalRecoveryCommand(reader, begin);
          recovery.annotation = projectDecodedCommand(input.sourceMap, recovery, trackAnnotation);
          appendDecodedBytecodeCommand(builder, recovery, begin);
          break;
        }
      }

      if (decoded.flow.unconditionalJump()) {
        const u32 destination = decoded.flow.staticTargets.front().value;
        appendDecodedBytecodeCommand(builder, decoded, begin);
        if (decodedOffsets.contains(destination)) {
          break;
        }
        offset = destination;
        continue;
      }

      if (decoded.flow.callTarget()) {
        const u32 destination = decoded.flow.staticTargets.front().value;
        if (!decodedOffsets.contains(destination) && callTargetOffsets.insert(destination).second) {
          pendingBlocks.push_back(PendingBlock{.offset = destination, .callTarget = true});
        }
      }

      const auto next = decoded.flow.fallthrough;
      appendDecodedBytecodeCommand(builder, decoded, begin);
      if (!next || decoded.flow.terminal) {
        break;
      }
      offset = next->value;
    }
  }

  finishSequenceTrackAnnotation(reader, input, trackAnnotation, track);
  return track;
}

[[nodiscard]] bool matches(ByteReader reader, u64 offset, std::string_view signature) {
  if (!reader.has(offset, signature.size())) {
    return false;
  }
  for (size_t i = 0; i < signature.size(); ++i) {
    if (reader.u8At(offset + i) != static_cast<u8>(signature[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<u32> nearbySseqHeader(ByteReader reader, u32 offset, u32 size) {
  constexpr u32 kMaxPaddingBeforeSseq = 0x200;
  const u64 searchEnd = std::min<u64>(reader.size(), static_cast<u64>(offset) + size + kMaxPaddingBeforeSseq);
  for (u64 candidate = offset + 1; candidate + kSseqSignature.size() <= searchEnd; ++candidate) {
    if (matches(reader, candidate, kSseqSignature)) {
      return static_cast<u32>(candidate);
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool isZeroFilled(ByteReader reader, u32 begin, u32 end) {
  for (u32 offset = begin; offset < end && reader.has(offset, 1); ++offset) {
    if (reader.u8At(offset) != 0) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<u32> recoveredMalformedSdatSequenceOffset(ByteReader reader, u32 offset, u32 size) {
  const auto sseqOffset = nearbySseqHeader(reader, offset, size);
  if (!sseqOffset) {
    return std::nullopt;
  }

  const u32 trackAddress = offset + kSseqHeaderSize;
  const u32 paddingEnd = std::min(*sseqOffset, offset + size);
  // Some zero-filled pseudo-sequences overlap a later SSEQ. If the padding
  // would align the SSEQ signature as bogus note data, leave it empty.
  if (size <= 0x100 && *sseqOffset >= trackAddress && isZeroFilled(reader, offset, paddingEnd) &&
      ((*sseqOffset - trackAddress) % 3) == 2) {
    return std::nullopt;
  }
  return sseqOffset;
}

}  // namespace

const SequenceDialect& ndsSequenceDialect() {
  static const SequenceDialect dialect = makeDialect();
  return dialect;
}

void registerNdsSequenceDialect(SequenceDialectRegistry& registry) {
  registry.add(ndsSequenceDialect());
}

TrackProgram decodeNdsSequenceTrack(ByteReader reader, TrackDecodeInput input, bool recoverMalformedSdatRange) {
  if (input.bytecodeEnd == std::numeric_limits<u32>::max()) {
    input.bytecodeEnd = static_cast<u32>(reader.size());
  } else {
    input.bytecodeEnd = std::min(input.bytecodeEnd, static_cast<u32>(reader.size()));
  }
  if (input.sequenceEnd == std::numeric_limits<u32>::max()) {
    input.sequenceEnd = input.bytecodeEnd;
  }
  if (recoverMalformedSdatRange) {
    return decodeMalformedSdatRangeTrack(reader, input);
  }

  const u32 sequenceDataBase = input.sequenceOffset + kSseqHeaderSize;
  return decodeSemanticReachableTrack(reader, input, [reader, input, sequenceDataBase](u32 offset) {
    return decodeCommand(reader, offset, input.bytecodeEnd, sequenceDataBase, input.sequenceEnd, input.diagnostics);
  });
}

std::vector<u32> ndsSequenceTrackAddresses(ByteReader reader, u32 sequenceOffset, u32 sequenceEnd) {
  std::vector<u32> extraTrackAddresses;
  const u32 sequenceDataBase = sequenceOffset + kSseqHeaderSize;
  u32 offset = sequenceDataBase;
  if (!hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
    return {offset};
  }

  // Bootstrap commands use the same semantic decoder as normal tracks. They
  // are discarded after discovery because the driver runs them before musical
  // track playback; the primary track begins at the first following command.
  const auto decodeAt = [&](u32 commandOffset) {
    return decodeCommand(reader, commandOffset, sequenceEnd, sequenceDataBase, sequenceEnd, nullptr);
  };

  auto command = decodeAt(offset);
  if (command.opcode != 0xfe || command.retainBytes) {
    return {offset};
  }
  offset += command.encodedSize;
  if (!hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
    return {offset};
  }

  command = decodeAt(offset);
  if (command.opcode == 0x80) {
    if (command.retainBytes) {
      return {offset};
    }
    offset += command.encodedSize;
  }

  while (hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
    command = decodeAt(offset);
    if (command.opcode != 0x93 || command.retainBytes) {
      break;
    }
    const auto destination = std::ranges::find_if(
        command.operands, [](const SemanticOperand& operand) { return operand.name == "destination"; });
    if (destination != command.operands.end()) {
      if (const auto* address = std::get_if<Address>(&destination->value);
          address != nullptr && address->value < sequenceEnd) {
        extraTrackAddresses.push_back(address->value);
      }
    }
    offset += command.encodedSize;
  }

  std::vector<u32> trackAddresses{offset};
  trackAddresses.insert(trackAddresses.end(), extraTrackAddresses.begin(), extraTrackAddresses.end());
  return trackAddresses;
}

NdsSequenceRange ndsSequenceRangeForFatEntry(ByteReader reader, u32 offset, u32 size) {
  const bool hasSseqHeader = matches(reader, offset, kSseqSignature);
  const u32 fatEnd = static_cast<u32>(std::min<u64>(reader.size(), static_cast<u64>(offset) + size));
  const std::optional<u32> recoveredSequenceOffset =
      hasSseqHeader ? std::nullopt : recoveredMalformedSdatSequenceOffset(reader, offset, size);
  const bool recoverMalformedSdatRange = recoveredSequenceOffset.has_value();
  const bool zeroFilled = !hasSseqHeader && !recoverMalformedSdatRange && isZeroFilled(reader, offset, fatEnd);
  const u32 decodeOffset = recoveredSequenceOffset.value_or(offset);
  const u32 recoveredEnd = recoveredSequenceOffset && reader.has(*recoveredSequenceOffset + kSseqFileSizeOffset, 4)
                               ? static_cast<u32>(std::min<u64>(
                                     reader.size(), static_cast<u64>(*recoveredSequenceOffset) +
                                                        reader.le32(*recoveredSequenceOffset + kSseqFileSizeOffset)))
                               : static_cast<u32>(reader.size());
  const u32 emptySequenceEnd =
      static_cast<u32>(std::min<u64>(reader.size(), static_cast<u64>(offset) + kSseqHeaderSize));

  u32 sequenceEnd = fatEnd;
  if (zeroFilled) {
    sequenceEnd = emptySequenceEnd;
  } else if (recoverMalformedSdatRange) {
    sequenceEnd = recoveredEnd;
  }

  return NdsSequenceRange{
      .offset = offset,
      .decodeOffset = decodeOffset,
      .size = size,
      .sequenceEnd = sequenceEnd,
      .recoverMalformedSdatRange = recoverMalformedSdatRange,
  };
}

SequenceProgramAsset parseNdsSequenceProgram(const ScanInput& input, AssetId id, NdsSequenceRange range,
                                             const std::string& name, SourceMapBuilder* sourceMap,
                                             std::vector<Diagnostic>* diagnostics) {
  const SequenceDialect& dialect = ndsSequenceDialect();
  const u32 sequenceOffset = range.decodeOffset != 0 ? range.decodeOffset : range.offset;
  const SourceRange sequenceRange = input.reader.range(sequenceOffset, range.sequenceEnd - sequenceOffset);
  SequenceProgramAsset asset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = std::string(kNdsFormatName),
              .name = name,
              .range = sequenceRange,
          },
      .program =
          SequenceProgram{
              .dialect = dialect.id,
              .timebase = dialect.timebase,
              .sourceBaseAddress = Address{sequenceOffset + kSseqHeaderSize},
              .behavior = dialect.defaultBehavior,
          },
  };

  SourceAnnotationId headerAnnotation;
  if (sourceMap != nullptr && input.reader.has(sequenceOffset, kSseqHeaderSize)) {
    headerAnnotation = sourceMap->header("SSEQ Header", input.reader.range(sequenceOffset, kSseqHeaderSize))
                           .kind("sseq-header")
                           .owner(ObjectRefs::sequence(id))
                           .field("data_offset", input.reader.range(sequenceOffset + kSseqDataOffsetField, 4),
                                  sequenceOffset + input.reader.le32(sequenceOffset + kSseqDataOffsetField),
                                  SourceValueDisplay::Address)
                           .id();
  }

  u32 trackIndex = 0;
  for (const u32 trackAddress : ndsSequenceTrackAddresses(input.reader, sequenceOffset, range.sequenceEnd)) {
    asset.program.tracks.push_back(decodeNdsSequenceTrack(
        input.reader,
        TrackDecodeInput{
            .sequenceAsset = id,
            .trackIndex = trackIndex++,
            .startOffset = trackAddress,
            .bytecodeEnd = range.sequenceEnd,
            .sequenceOffset = sequenceOffset,
            .sequenceEnd = range.sequenceEnd,
            .parentAnnotation = headerAnnotation.valid() ? std::optional{headerAnnotation} : std::nullopt,
            .sourceMap = sourceMap,
            .diagnostics = diagnostics,
            .maxCommands = kMaxTrackCommands,
        },
        range.recoverMalformedSdatRange));
  }

  return asset;
}

}  // namespace vgmtrans::formats::nds
