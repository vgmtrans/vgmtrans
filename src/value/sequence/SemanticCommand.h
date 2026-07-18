/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/RecordReader.h"
#include "value/sequence/BytecodeDecode.h"

#include <algorithm>
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

template <class T>
struct EncodedSemanticField {
  T value{};
  SourceRange range;
  std::string_view name;
  SourceValueDisplay display = SourceValueDisplay::Default;
};

// A source-aware builder for one semantic command. It is deliberately small:
// format code still reads fields in normal C++ control flow, while this class
// hides type erasure, raw/resolved pairing, ranges, and DecodeFlow bookkeeping.
class SemanticCommandDecoder {
public:
  SemanticCommandDecoder(ByteReader reader, u32 begin, u32 end, std::vector<Diagnostic>* diagnostics)
      : record_(reader, begin, end, diagnostics) {
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

  template <class T>
  void opcodeValue(std::string_view name, T parsed, SourceValueDisplay display = SourceValueDisplay::Default,
                   SemanticOperandRole role = SemanticOperandRole::Value) {
    add(name, value(parsed), opcodeRange_, display, role);
  }

  template <class T>
  void derived(std::string_view name, T parsed, SourceValueDisplay display = SourceValueDisplay::Default,
               SemanticOperandRole role = SemanticOperandRole::Value) {
    add(name, value(parsed), {}, display, role);
  }

  template <class T, class Convert>
  void resolved(std::string_view name, const EncodedSemanticField<T>& source, Convert convert,
                SourceValueDisplay display = SourceValueDisplay::Default,
                SemanticOperandRole role = SemanticOperandRole::Value) {
    resolvedValue(name, source, convert(source.value), display, role);
  }

  template <class T, class Resolved>
  void resolvedValue(std::string_view name, const EncodedSemanticField<T>& source, Resolved resolved,
                     SourceValueDisplay display = SourceValueDisplay::Default,
                     SemanticOperandRole role = SemanticOperandRole::Value) {
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
    add(name, value(parsed), source.range, SourceValueDisplay::Address, role);
    return parsed;
  }

  // Discovery flow only tells the bytecode walker which source addresses to
  // decode. Runtime branching remains in the command's adjacent playback code.
  void jumpTo(Address destination) { flow_ = DecodeFlow::jump(destination); }
  void branchTo(Address destination) {
    flow_ = DecodeFlow{.kind = DecodeFlow::Kind::Fallthrough, .staticTargets = {destination}};
  }
  void terminate() { flow_ = DecodeFlow::terminalFlow(); }

  [[nodiscard]] DecodedBytecodeCommand finish(DecodedCommandPresentation presentation) {
    if (!record_.ok()) {
      flow_ = DecodeFlow::terminalFlow();
    } else if (flow_.kind == DecodeFlow::Kind::Fallthrough && !flow_.fallthrough) {
      flow_.fallthrough = Address{record_.position()};
    }
    return DecodedBytecodeCommand{
        .range = record_.range(),
        .opcode = opcode_,
        .encodedSize = std::max<u32>(1, record_.size()),
        .flow = std::move(flow_),
        .operands = std::move(operands_),
        .presentation = std::move(presentation),
        .retainBytes = false,
    };
  }

private:
  template <class T>
  [[nodiscard]] static SemanticOperandValue value(T parsed) {
    using Value = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<Value, Address> || std::is_same_v<Value, bool>) {
      return SemanticOperandValue{parsed};
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
    add(source.name, value(source.value), source.range, source.display, role);
    return source.value;
  }

  template <class T>
  [[nodiscard]] static EncodedSemanticField<T> field(const RangedValue<T>& parsed, std::string_view name,
                                                     SourceValueDisplay display) {
    return {.value = parsed.value, .range = parsed.range, .name = name, .display = display};
  }

  RecordReader record_;
  ::u8 opcode_ = 0;
  SourceRange opcodeRange_;
  std::vector<SemanticOperand> operands_;
  DecodeFlow flow_;
};

}  // namespace vgmtrans::core
