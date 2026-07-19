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
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 262144;
constexpr u32 kSseqDataOffsetField = 0x18;
constexpr u32 kSseqHeaderSize = 0x1c;

// Resolve NDS's unsigned 24-bit, data-relative addresses during decode so
// playback never needs the source container's layout.
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

  [[nodiscard]] DecodedBytecodeCommand command(std::string_view label, SequenceSemantic semantic,
                                               StandardSequenceCommand standardCommand = {},
                                               CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback,
                                               std::string_view localKind = {}) {
    if (!ok()) {
      return SemanticCommandDecoder::finish(presentation("Truncated Command", SequenceSemantic::Unsupported,
                                                         CommandPlaybackStatus::Unsupported, "truncated"));
    }
    standard(std::move(standardCommand));
    return SemanticCommandDecoder::finish(presentation(label, semantic, playback, localKind));
  }

private:
  [[nodiscard]] static DecodedCommandPresentation presentation(std::string_view label, SequenceSemantic semantic,
                                                               CommandPlaybackStatus playback,
                                                               std::string_view localKind) {
    const std::string kind = localKind.empty() ? sourceLocalKind(label) : std::string(localKind);
    return DecodedCommandPresentation{
        .label = std::string(label),
        .localKind = kind,
        .detailKind = "nds." + kind,
        .semantic = semantic,
        .playback = playback,
    };
  }

  u32 sequenceDataBase_ = 0;
  u32 sequenceEnd_ = 0;
};

struct OpaqueCommand {
  u8 opcode;
  u8 operandBytes;
  std::string_view label;
  std::string_view localKind;
};

constexpr std::array<OpaqueCommand, 32> kOpaqueCommands{{
    {0xa0, 5, "Cmd with Random Value", "random-value"},
    {0xa1, 2, "Cmd with Variable", "variable-command"},
    {0xa2, 0, "If", {}},
    {0xb0, 3, "Set Variable", {}},
    {0xb1, 3, "Add Variable", {}},
    {0xb2, 3, "Sub Variable", {}},
    {0xb3, 3, "Mul Variable", {}},
    {0xb4, 3, "Div Variable", {}},
    {0xb5, 3, "Shift Variable", {}},
    {0xb6, 3, "Rand Variable", {}},
    {0xb8, 3, "If Variable ==", "if-variable-equal"},
    {0xb9, 3, "If Variable >=", "if-variable-greater-equal"},
    {0xba, 3, "If Variable >", "if-variable-greater"},
    {0xbb, 3, "If Variable <=", "if-variable-less-equal"},
    {0xbc, 3, "If Variable <", "if-variable-less"},
    {0xbd, 3, "If Variable !=", "if-variable-not-equal"},
    {0xc2, 1, "Master Volume", {}},
    {0xc6, 1, "Priority", {}},
    {0xc8, 1, "Tie", {}},
    {0xc9, 1, "Portamento Control", {}},
    {0xcb, 1, "Modulation Speed", {}},
    {0xcc, 1, "Modulation Type", {}},
    {0xcd, 1, "Modulation Range", {}},
    {0xd0, 1, "Attack Rate", {}},
    {0xd1, 1, "Decay Rate", {}},
    {0xd2, 1, "Sustain Level", {}},
    {0xd3, 1, "Release Rate", {}},
    {0xd4, 1, "Loop Start", {}},
    {0xd6, 1, "Print Variable", {}},
    {0xe0, 2, "Modulation Delay", {}},
    {0xe3, 2, "Sweep Pitch", {}},
    {0xfc, 0, "Loop End", {}},
}};

// Each supported opcode reads source fields and names its durable operation in
// one case. Opaque commands are preserved for inspection but intentionally do
// not pretend to implement NDS variables or conditionals.
[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end, u32 sequenceDataBase,
                                                   u32 sequenceEnd, std::vector<Diagnostic>* diagnostics) {
  Decode decode(reader, begin, end, sequenceDataBase, sequenceEnd, diagnostics);
  if (!decode.hasOpcode()) {
    return decode.command("Truncated Command", SequenceSemantic::Unsupported, {}, CommandPlaybackStatus::Unsupported,
                          "truncated");
  }

  if (decode.opcode() <= 0x7f) {
    const u8 key =
        decode.opcodeValue("key", decode.opcode(), SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    const u8 velocity = decode.u8("velocity", SemanticOperandRole::Level);
    const u32 duration = decode.varLen("duration", SourceValueDisplay::Default, SemanticOperandRole::Duration);
    return decode.command("Note", SequenceSemantic::Note,
                          standard_command::Note{
                              .key = key,
                              .linearVelocity = LevelScale::linearFromMidi7(velocity),
                              .durationTicks = duration,
                          });
  }

  if (const auto opaque = std::ranges::find(kOpaqueCommands, decode.opcode(), &OpaqueCommand::opcode);
      opaque != kOpaqueCommands.end()) {
    if (opaque->operandBytes != 0) {
      static_cast<void>(decode.rawBytes("bytes", opaque->operandBytes));
    }
    return decode.command(opaque->label, SequenceSemantic::Meta, {}, CommandPlaybackStatus::SourceOnly,
                          opaque->localKind);
  }

  switch (decode.opcode()) {
    case 0x80: {
      const u32 duration = decode.varLen("duration", SourceValueDisplay::Default, SemanticOperandRole::Duration);
      return decode.command("Rest", SequenceSemantic::Rest, standard_command::Wait{.ticks = duration});
    }
    case 0x81: {
      const u32 raw = decode.varLen("raw");
      const u32 bank = raw >> 7;
      const u32 program = raw & 0x7f;
      decode.derived("bank", bank, SourceValueDisplay::Default, SemanticOperandRole::InstrumentBank);
      decode.derived("program", program, SourceValueDisplay::Default, SemanticOperandRole::InstrumentProgram);
      return decode.command("Program", SequenceSemantic::Program,
                            standard_command::Instrument{.bank = bank, .program = program});
    }
    case 0x94: {
      if (const auto destination = decode.controlTarget("destination", SemanticOperandRole::JumpTarget,
                                                        "Jump target outside sequence data")) {
        decode.jumpTo(*destination);
      }
      return decode.command("Jump", SequenceSemantic::Jump, {}, CommandPlaybackStatus::AffectsControlFlow);
    }
    case 0x95: {
      if (const auto destination = decode.controlTarget("destination", SemanticOperandRole::CallTarget,
                                                        "Call target outside sequence data")) {
        decode.callTo(*destination);
      }
      return decode.command("Call", SequenceSemantic::Call, {}, CommandPlaybackStatus::AffectsControlFlow);
    }
    case 0x96:
      decode.warning("Unsupported NDS SSEQ command stopped playback");
      decode.terminate();
      return decode.command("Unsupported Command", SequenceSemantic::Unsupported, {},
                            CommandPlaybackStatus::Unsupported, "unsupported");
    case 0xc0: {
      const double position = decode.resolved("position", decode.rawU8("pan"), [](u8 raw) {
        return std::clamp((static_cast<double>(raw) / 63.5) - 1.0, -1.0, 1.0);
      });
      return decode.command("Pan", SequenceSemantic::Pan, standard_command::Pan{.position = position});
    }
    case 0xc1: {
      const double gain = decode.resolved("linear_gain", decode.rawU8("volume"), LevelScale::linearFromMidi7);
      return decode.command("Volume", SequenceSemantic::Level, standard_command::Level{.linearGain = gain});
    }
    case 0xc3: {
      const s8 semitones = decode.s8("semitones");
      return decode.command("Transpose", SequenceSemantic::State, standard_command::Transpose{.semitones = semitones});
    }
    case 0xc4: {
      const double fraction = decode.resolved("fraction", decode.rawS8("bend"), [](s8 bend) { return bend / 128.0; });
      return decode.command("Pitch Bend", SequenceSemantic::Pitch,
                            standard_command::PitchBend{.rangeFraction = fraction});
    }
    case 0xc5: {
      const u8 semitones = decode.u8("semitones");
      return decode.command("Pitch Bend Range", SequenceSemantic::Pitch,
                            standard_command::PitchBendRange{.semitones = semitones});
    }
    case 0xc7: {
      const auto raw = decode.rawU8("raw");
      const bool enabled = raw.value != 0;
      decode.resolvedValue("enabled", raw, enabled, SourceValueDisplay::Boolean);
      return decode.command("Note Wait", SequenceSemantic::State, standard_command::NoteWait{.enabled = enabled});
    }
    case 0xca: {
      const double amount = decode.resolved("amount", decode.rawU8("depth"), [](u8 depth) {
        return std::clamp(static_cast<double>(depth) / 127.0, 0.0, 1.0);
      });
      return decode.command("Modulation Depth", SequenceSemantic::Modulation,
                            standard_command::VibratoDepth{.amount = amount});
    }
    case 0xce: {
      const auto raw = decode.rawU8("raw");
      const bool enabled = raw.value != 0;
      decode.resolvedValue("enabled", raw, enabled, SourceValueDisplay::Boolean);
      return decode.command("Portamento", SequenceSemantic::Portamento,
                            standard_command::PortamentoEnable{.enabled = enabled});
    }
    case 0xcf: {
      const double milliseconds =
          decode.resolved("milliseconds", decode.rawU8("time"), [](u8 time) { return static_cast<double>(time); });
      return decode.command("Portamento Time", SequenceSemantic::Portamento,
                            standard_command::PortamentoTime{.milliseconds = milliseconds});
    }
    case 0xd5: {
      const double gain = decode.resolved("linear_gain", decode.rawU8("expression"), LevelScale::linearFromMidi7);
      return decode.command("Expression", SequenceSemantic::Level, standard_command::Expression{.linearGain = gain});
    }
    case 0xe1: {
      const auto bpm = decode.rawU16le("bpm");
      const u32 microsecondsPerQuarter = bpm.value == 0 ? 0 : static_cast<u32>(std::round(60000000.0 / bpm.value));
      decode.resolvedValue("microseconds_per_quarter", bpm, microsecondsPerQuarter);
      return decode.command("Tempo", SequenceSemantic::Tempo,
                            standard_command::Tempo{.microsecondsPerQuarter = microsecondsPerQuarter});
    }
    case 0xfd:
      decode.return_();
      return decode.command("Return", SequenceSemantic::Return, {}, CommandPlaybackStatus::AffectsControlFlow);
    case 0xff:
      decode.terminate();
      return decode.command("End", SequenceSemantic::End, {}, CommandPlaybackStatus::StopsPlayback);
    default:
      decode.warning("Unknown NDS SSEQ opcode stopped playback");
      decode.terminate();
      return decode.command("Unknown Opcode", SequenceSemantic::Unsupported, {}, CommandPlaybackStatus::Unsupported,
                            "unknown");
  }
}

// The bootstrap is container structure, not music. Read it once to discover
// track entry points instead of decoding and then discarding fake commands.
[[nodiscard]] std::vector<u32> readTrackStarts(ByteReader reader, NdsSequenceRange range) {
  const u32 sequenceDataBase = range.offset + kSseqHeaderSize;
  const u32 sequenceEnd = range.sequenceEnd;
  u32 offset = sequenceDataBase;
  if (!hasBytecodeBytes(reader, offset, 1, sequenceEnd) || reader.u8At(offset) != 0xfe) {
    return {offset};
  }

  RecordReader allocation(reader, offset, sequenceEnd);
  static_cast<void>(allocation.u8("opcode"));
  static_cast<void>(allocation.u16le("track_mask"));
  if (!allocation.ok()) {
    return {offset};
  }
  offset = allocation.position();

  if (hasBytecodeBytes(reader, offset, 1, sequenceEnd) && reader.u8At(offset) == 0x80) {
    RecordReader delay(reader, offset, sequenceEnd);
    static_cast<void>(delay.u8("opcode"));
    static_cast<void>(delay.varLen("duration"));
    if (!delay.ok()) {
      return {offset};
    }
    offset = delay.position();
  }

  std::vector<u32> secondaryTracks;
  while (hasBytecodeBytes(reader, offset, 1, sequenceEnd) && reader.u8At(offset) == 0x93) {
    RecordReader openTrack(reader, offset, sequenceEnd);
    static_cast<void>(openTrack.u8("opcode"));
    static_cast<void>(openTrack.u8("track"));
    const auto relative = openTrack.u24le("destination", SourceValueDisplay::Address);
    if (!openTrack.ok()) {
      break;
    }
    const u64 destination = static_cast<u64>(sequenceDataBase) + relative.value;
    if (destination < sequenceEnd) {
      secondaryTracks.push_back(static_cast<u32>(destination));
    }
    offset = openTrack.position();
  }

  std::vector<u32> trackAddresses{offset};
  trackAddresses.insert(trackAddresses.end(), secondaryTracks.begin(), secondaryTracks.end());
  return trackAddresses;
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, AssetId sequenceId, u32 trackIndex, u32 startOffset,
                                       NdsSequenceRange range, std::optional<SourceAnnotationId> parentAnnotation,
                                       SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeInput input{
      .sequenceAsset = sequenceId,
      .trackIndex = trackIndex,
      .startOffset = startOffset,
      .bytecodeEnd = range.sequenceEnd,
      .sequenceOffset = range.offset,
      .sequenceEnd = range.sequenceEnd,
      .parentAnnotation = parentAnnotation,
      .sourceMap = sourceMap,
      .diagnostics = diagnostics,
      .maxCommands = kMaxTrackCommands,
  };
  const u32 sequenceDataBase = range.offset + kSseqHeaderSize;
  return decodeSemanticReachableTrack(reader, input, [reader, input, sequenceDataBase](u32 offset, u32 commandEnd) {
    return decodeCommand(reader, offset, commandEnd, sequenceDataBase, input.sequenceEnd, input.diagnostics);
  });
}

}  // namespace

SequenceProgram decodeNdsSequence(ByteReader reader, AssetId sequenceId, NdsSequenceRange range,
                                  SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  range.sequenceEnd = static_cast<u32>(std::min<u64>(range.sequenceEnd, reader.size()));
  const SequenceDialect& dialect = ndsSequenceDialect();
  SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{range.offset + kSseqHeaderSize},
      .behavior = dialect.defaultBehavior,
  };

  SourceAnnotationId headerAnnotation;
  if (sourceMap != nullptr && reader.has(range.offset, kSseqHeaderSize)) {
    headerAnnotation =
        sourceMap->header("SSEQ Header", reader.range(range.offset, kSseqHeaderSize))
            .kind("sseq-header")
            .owner(ObjectRefs::sequence(sequenceId))
            .field("data_offset", reader.range(range.offset + kSseqDataOffsetField, 4),
                   range.offset + reader.le32(range.offset + kSseqDataOffsetField), SourceValueDisplay::Address)
            .id();
  }

  u32 trackIndex = 0;
  for (const u32 trackAddress : readTrackStarts(reader, range)) {
    program.tracks.push_back(decodeTrack(reader, sequenceId, trackIndex++, trackAddress, range,
                                         headerAnnotation.valid() ? std::optional{headerAnnotation} : std::nullopt,
                                         sourceMap, diagnostics));
  }
  return program;
}

const SequenceDialect& ndsSequenceDialect() {
  static const SequenceDialect dialect{
      .id = DialectId{.value = std::string(kNdsSequenceDialectId)},
      .commandDetailKindPrefix = "nds",
      .timebase = Timebase{.ppqn = 0x30},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .commandLimit = kMaxTrackCommands,
          },
      .createSemanticTrackState = createStandardTrackState,
      .executeSemantic = executeStandardCommand,
  };
  return dialect;
}

}  // namespace vgmtrans::formats::nds
