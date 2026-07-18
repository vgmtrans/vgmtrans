/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/RecordReader.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/SequenceDialect.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace vgmtrans::core {

// Playback code uses the names written beside its decode lambda. Commands have
// very few operands, so this intentionally performs a linear lookup instead of
// making formats declare and synchronize numeric operand IDs.
class SemanticCommandArgs {
public:
  explicit SemanticCommandArgs(const SourceCommand& command) : command_(command) {}

  [[nodiscard]] ::u32 u32(std::string_view name = {}) const { return static_cast<::u32>(get<u64>(name)); }
  [[nodiscard]] ::u8 u8(std::string_view name = {}) const { return static_cast<::u8>(get<u64>(name)); }
  [[nodiscard]] ::s8 s8(std::string_view name = {}) const { return static_cast<::s8>(get<s64>(name)); }
  [[nodiscard]] double f64(std::string_view name = {}) const { return get<double>(name); }
  [[nodiscard]] bool boolean(std::string_view name = {}) const { return get<bool>(name); }
  [[nodiscard]] Address address(std::string_view name = {}) const { return get<Address>(name); }

private:
  template <class T>
  [[nodiscard]] T get(std::string_view name) const {
    const SemanticOperand* operand =
        name.empty() && !command_.operands.empty() ? &command_.operands.front() : semanticOperand(command_, name);
    if (operand == nullptr) {
      throw std::logic_error("Semantic command is missing operand '" + std::string(name) + "'");
    }
    return std::get<T>(operand->value);
  }

  const SourceCommand& command_;
};

// Formats supply their own Decode and Playback contexts, while this small
// value owns the identical presentation/function-pointer plumbing they share.
// Actual command behavior remains beside the opcode in the format profile.
template <class Decode, class Playback>
struct SemanticCommandDefinition {
  using DecodeFunction = void (*)(Decode&);
  using ExecuteFunction = Effects (*)(SemanticCommandArgs, Playback&);

  DecodedCommandPresentation presentation;
  DecodeFunction decode = nullptr;
  ExecuteFunction execute = nullptr;
  u8 opaqueOperandBytes = 0;

  void decodeOperands(Decode& decoder) const {
    if (decode != nullptr) {
      decode(decoder);
    } else if (opaqueOperandBytes != 0) {
      static_cast<void>(decoder.rawBytes("bytes", opaqueOperandBytes));
    }
  }
};

// This is deliberately a definition factory, not an opcode schema. It removes
// repeated metadata construction while leaving profile layout and every decode
// and execute lambda under the format's control.
template <class Decode, class Playback>
class SemanticCommandDefinitions {
public:
  using Definition = SemanticCommandDefinition<Decode, Playback>;
  using DecodeFunction = typename Definition::DecodeFunction;
  using ExecuteFunction = typename Definition::ExecuteFunction;

  constexpr explicit SemanticCommandDefinitions(std::string_view detailKindPrefix) : prefix_(detailKindPrefix) {}

  [[nodiscard]] Definition command(std::string_view label, SequenceSemantic semantic, DecodeFunction decode,
                                   ExecuteFunction execute,
                                   CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback,
                                   std::string_view localKind = {}) const {
    const std::string kind = localKind.empty() ? sourceLocalKind(label) : std::string(localKind);
    return Definition{
        .presentation =
            DecodedCommandPresentation{
                .label = std::string(label),
                .localKind = kind,
                .detailKind = prefix_.empty() ? kind : std::string(prefix_) + "." + kind,
                .semantic = semantic,
                .playback = playback,
            },
        .decode = decode,
        .execute = execute,
    };
  }

  [[nodiscard]] Definition command(std::string_view label, SequenceSemantic semantic, ExecuteFunction execute,
                                   CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback,
                                   std::string_view localKind = {}) const {
    return command(label, semantic, nullptr, execute, playback, localKind);
  }

  [[nodiscard]] Definition sourceOnly(std::string_view label, DecodeFunction decode,
                                      std::string_view localKind = {}) const {
    return command(label, SequenceSemantic::Meta, decode, nullptr, CommandPlaybackStatus::SourceOnly, localKind);
  }

  [[nodiscard]] Definition opaque(std::string_view label, u8 operandBytes, std::string_view localKind = {}) const {
    auto definition = sourceOnly(label, nullptr, localKind);
    definition.opaqueOperandBytes = operandBytes;
    return definition;
  }

  [[nodiscard]] Definition terminal(std::string_view label, SequenceSemantic semantic, CommandPlaybackStatus playback,
                                    std::string_view localKind = {}) const {
    return command(label, semantic, [](Decode& decoder) { decoder.terminate(); }, nullptr, playback, localKind);
  }

  [[nodiscard]] Definition noOp(std::string_view label, std::string_view localKind = {}) const {
    return command(label, SequenceSemantic::Meta, nullptr, nullptr, CommandPlaybackStatus::NoOp, localKind);
  }

private:
  std::string_view prefix_;
};

template <class T>
struct EncodedSemanticField {
  T value{};
  SourceRange range;
  std::string_view name;
  SourceValueDisplay display = SourceValueDisplay::Default;
  bool valid = false;
};

// A source-aware builder for one semantic command. It is deliberately small:
// format code still reads fields in normal C++ control flow, while this class
// hides type erasure, raw/resolved pairing, ranges, and DecodeFlow bookkeeping.
class SemanticCommandDecoder {
public:
  SemanticCommandDecoder(ByteReader reader, u32 begin, u32 end, std::vector<Diagnostic>* diagnostics)
      : record_(reader, begin, end, diagnostics), diagnostics_(diagnostics) {
    const auto opcode = record_.u8("opcode", SourceValueDisplay::Hex);
    if (opcode) {
      opcode_ = *opcode;
      opcodeRange_ = opcode.range;
    }
  }

  [[nodiscard]] bool hasOpcode() const noexcept { return opcodeRange_.size != 0; }
  [[nodiscard]] bool ok() const noexcept { return record_.ok(); }
  [[nodiscard]] ::u8 opcode() const noexcept { return opcode_; }

  ::u8 u8(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
          SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(rawU8(name, display), role);
  }

  ::u8 u8(std::string_view name, SemanticOperandRole role) { return u8(name, SourceValueDisplay::Default, role); }

  ::s8 s8(std::string_view name, SourceValueDisplay display = SourceValueDisplay::SignedDecimal,
          SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(rawS8(name, display), role);
  }

  u16 u16le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
            SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(rawU16le(name, display), role);
  }

  u32 varLen(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
             SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(rawVarLen(name, display), role);
  }

  std::string rawBytes(std::string_view name, u32 size) {
    return decoded(field(record_.rawBytes(name, size), name, SourceValueDisplay::Hex), SemanticOperandRole::Value);
  }

  // A raw field is kept separate only when playback consumes a converted value.
  // resolved()/resolvedValue() then retain both forms in one semantic operand.
  [[nodiscard]] EncodedSemanticField<::u8> rawU8(std::string_view name,
                                                 SourceValueDisplay display = SourceValueDisplay::Default) {
    return field(record_.u8(name, display), name, display);
  }

  [[nodiscard]] EncodedSemanticField<::s8> rawS8(std::string_view name,
                                                 SourceValueDisplay display = SourceValueDisplay::SignedDecimal) {
    return field(record_.s8(name, display), name, display);
  }

  [[nodiscard]] EncodedSemanticField<u16> rawU16be(std::string_view name,
                                                   SourceValueDisplay display = SourceValueDisplay::Default) {
    return field(record_.u16be(name, display), name, display);
  }

  [[nodiscard]] EncodedSemanticField<u16> rawU16le(std::string_view name,
                                                   SourceValueDisplay display = SourceValueDisplay::Default) {
    return field(record_.u16le(name, display), name, display);
  }

  [[nodiscard]] EncodedSemanticField<u32> rawU24le(std::string_view name,
                                                   SourceValueDisplay display = SourceValueDisplay::Default) {
    return field(record_.u24le(name, display), name, display);
  }

  [[nodiscard]] EncodedSemanticField<u32> rawVarLen(std::string_view name,
                                                    SourceValueDisplay display = SourceValueDisplay::Default) {
    return field(record_.varLen(name, display), name, display);
  }

  template <class T>
  void opcodeValue(std::string_view name, T parsed, SourceValueDisplay display = SourceValueDisplay::Default,
                   SemanticOperandRole role = SemanticOperandRole::Value) {
    add(name, value(parsed), opcodeRange_, display, role);
  }

  template <class T>
  void derived(std::string_view name, T parsed, SourceValueDisplay display = SourceValueDisplay::Default,
               SemanticOperandRole role = SemanticOperandRole::Value) {
    if (record_.ok()) {
      add(name, value(parsed), {}, display, role);
    }
  }

  template <class T, class Convert>
  void resolved(std::string_view name, const EncodedSemanticField<T>& source, Convert convert,
                SourceValueDisplay display = SourceValueDisplay::Default,
                SemanticOperandRole role = SemanticOperandRole::Value) {
    if (source.valid) {
      resolvedValue(name, source, convert(source.value), display, role);
    }
  }

  template <class T, class Resolved>
  void resolvedValue(std::string_view name, const EncodedSemanticField<T>& source, Resolved resolved,
                     SourceValueDisplay display = SourceValueDisplay::Default,
                     SemanticOperandRole role = SemanticOperandRole::Value) {
    if (!source.valid) {
      return;
    }
    operands_.push_back(SemanticOperand{
        .value = value(resolved),
        .range = source.range,
        .name = std::string(name),
        .display = display,
        .role = role,
        .encodedValue = value(source.value),
        .encodedName = std::string(source.name),
        .encodedDisplay = source.display,
    });
  }

  [[nodiscard]] Address address(std::string_view name, SemanticOperandRole role) {
    const auto source = rawU16be(name, SourceValueDisplay::Address);
    const Address parsed{source.value};
    if (source.valid) {
      add(name, value(parsed), source.range, SourceValueDisplay::Address, role);
    }
    return parsed;
  }

  // Discovery flow only tells the bytecode walker which source addresses to
  // decode. Runtime branching remains in the command's adjacent playback code.
  void jumpTo(Address destination) { flow_ = DecodeFlow::jump(destination); }
  void callTo(Address destination) { flow_ = DecodeFlow::call(destination, Address{record_.position()}); }
  void return_() { flow_ = DecodeFlow::return_(); }
  void branchTo(Address destination) {
    flow_ = DecodeFlow{.kind = DecodeFlow::Kind::Fallthrough, .staticTargets = {destination}};
  }
  void terminate() { flow_ = DecodeFlow::terminalFlow(); }

  void warning(std::string message) {
    if (diagnostics_ != nullptr) {
      diagnostics_->push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = std::move(message),
          .range = record_.range(),
      });
    }
  }

  [[nodiscard]] DecodedBytecodeCommand finish(DecodedCommandPresentation presentation) {
    if (!record_.ok()) {
      flow_ = DecodeFlow::terminalFlow();
    } else if (flow_.kind == DecodeFlow::Kind::Fallthrough && !flow_.fallthrough) {
      flow_.fallthrough = Address{record_.position()};
    }
    const bool truncated = !record_.ok();
    const auto bytes = record_.bytes();
    return DecodedBytecodeCommand{
        .range = record_.range(),
        .opcode = opcode_,
        .encodedSize = std::max<u32>(1, record_.size()),
        .bytes = truncated ? std::vector<::u8>{bytes.begin(), bytes.end()} : std::vector<::u8>{},
        .flow = std::move(flow_),
        .operands = std::move(operands_),
        .presentation = std::move(presentation),
        .retainBytes = truncated,
    };
  }

private:
  template <class T>
  [[nodiscard]] static SemanticOperandValue value(T parsed) {
    using Value = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<Value, Address> || std::is_same_v<Value, bool> || std::is_same_v<Value, std::string>) {
      return SemanticOperandValue{parsed};
    } else if constexpr (std::is_same_v<Value, std::string_view>) {
      return SemanticOperandValue{std::string(parsed)};
    } else if constexpr (std::is_floating_point_v<Value>) {
      return SemanticOperandValue{static_cast<double>(parsed)};
    } else if constexpr (std::is_signed_v<Value>) {
      return SemanticOperandValue{static_cast<s64>(parsed)};
    } else {
      return SemanticOperandValue{static_cast<u64>(parsed)};
    }
  }

  void add(std::string_view name, SemanticOperandValue parsed, SourceRange range, SourceValueDisplay display,
           SemanticOperandRole role) {
    operands_.push_back(SemanticOperand{
        .value = std::move(parsed),
        .range = range,
        .name = std::string(name),
        .display = display,
        .role = role,
    });
  }

  template <class T>
  T decoded(const EncodedSemanticField<T>& source, SemanticOperandRole role) {
    if (source.valid) {
      add(source.name, value(source.value), source.range, source.display, role);
    }
    return source.value;
  }

  template <class T>
  [[nodiscard]] static EncodedSemanticField<T> field(const RangedValue<T>& parsed, std::string_view name,
                                                     SourceValueDisplay display) {
    return {.value = parsed.value, .range = parsed.range, .name = name, .display = display, .valid = parsed.valid};
  }

  RecordReader record_;
  ::u8 opcode_ = 0;
  SourceRange opcodeRange_;
  std::vector<SemanticOperand> operands_;
  DecodeFlow flow_;
  std::vector<Diagnostic>* diagnostics_ = nullptr;
};

// The format supplies only how to decode one command. This wrapper owns the
// repeated track lifecycle: walking control flow, projecting command source-map
// entries, and finishing the parent track annotation.
template <class DecodeCommand>
[[nodiscard]] TrackProgram decodeSemanticLinearTrack(ByteReader reader, TrackDecodeInput input,
                                                     DecodeCommand decodeCommand) {
  const auto trackAnnotation = createSequenceTrackAnnotation(reader, input);
  const auto decodeAndProject = [&](u32 offset) {
    auto command = decodeCommand(offset);
    command.annotation = projectDecodedCommand(input.sourceMap, command, trackAnnotation);
    return command;
  };
  TrackProgram track =
      decodeLinearBytecodeTrack(reader, input.trackIndex, input.startOffset,
                                LinearBytecodeDecodePolicy{.maxCommands = input.maxCommands}, decodeAndProject);
  finishSequenceTrackAnnotation(reader, input, trackAnnotation, track);
  return track;
}

// Reachable formats use the same semantic command projection as linear ones,
// but queue static jump and call targets instead of assuming one byte stream.
template <class DecodeCommand>
[[nodiscard]] TrackProgram decodeSemanticReachableTrack(ByteReader reader, TrackDecodeInput input,
                                                        DecodeCommand decodeCommand) {
  const auto trackAnnotation = createSequenceTrackAnnotation(reader, input);
  const auto decodeAndProject = [&](u32 offset) {
    auto command = decodeCommand(offset);
    command.annotation = projectDecodedCommand(input.sourceMap, command, trackAnnotation);
    return command;
  };
  const u32 bytecodeEnd = input.bytecodeEnd == std::numeric_limits<u32>::max()
                              ? static_cast<u32>(reader.size())
                              : std::min(static_cast<u32>(reader.size()), input.bytecodeEnd);
  TrackProgram track =
      decodeReachableBytecodeBlocks(reader, bytecodeEnd, input.startOffset, input.trackIndex,
                                    ReachableBytecodeDecodePolicy{.maxCommands = input.maxCommands}, decodeAndProject);
  finishSequenceTrackAnnotation(reader, input, trackAnnotation, track);
  return track;
}

}  // namespace vgmtrans::core
