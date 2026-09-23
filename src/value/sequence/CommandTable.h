/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/CompilerCursor.h"

#include <array>
#include <functional>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace vgmtrans::core {

namespace command {

enum class Encoding { Byte, SignedByte, WordLE, WordBE, VariableLength };

// A field declaration, read and annotated when its command is decoded.
template <Encoding encoding>
struct Operand {
  std::string name;
  SourceValueDisplay display =
      encoding == Encoding::SignedByte ? SourceValueDisplay::SignedDecimal : SourceValueDisplay::Default;
  SemanticOperandRole role = SemanticOperandRole::Value;

  template <class Event>
  auto read(Event& event) const {
    if constexpr (encoding == Encoding::Byte) {
      return event.u8(name, display, role);
    } else if constexpr (encoding == Encoding::SignedByte) {
      return event.s8(name, display, role);
    } else if constexpr (encoding == Encoding::WordLE) {
      return event.u16le(name, display, role);
    } else if constexpr (encoding == Encoding::WordBE) {
      return event.u16be(name, display, role);
    } else {
      return event.varLen(name, display, role);
    }
  }
};

using Byte = Operand<Encoding::Byte>;
using SignedByte = Operand<Encoding::SignedByte>;
using WordLE = Operand<Encoding::WordLE>;
using WordBE = Operand<Encoding::WordBE>;
using VariableLength = Operand<Encoding::VariableLength>;

}  // namespace command

// Each row owns a command's presentation, encoded fields, and typed playback
// behavior. Irregular encodings can continue to use CompilerCursor directly.
// Actions are playback methods/callables, or track-state members to assign.
// Operands are declared in source order and become the action's arguments.
template <class Playback>
class CommandTable {
  using Cursor = CompilerCursor<Playback>;
  using Decode = std::function<DecodedBytecodeCommand(Cursor&)>;

public:
  struct Entry {
    template <class Action, class... Operands>
    Entry(u8 opcode, std::string label, SequenceSemantic semantic, Action action, Operands... operands)
        : opcode(opcode), decode([label = std::move(label), semantic, action,
                                  operands = std::tuple{std::move(operands)...}](Cursor& cursor) {
            auto event = cursor.command(label, semantic);
            return std::apply(
                [&](const auto&... fields) -> DecodedBytecodeCommand {
                  // List initialization guarantees source-order reads, unlike
                  // function arguments. Playback receives only the owned values.
                  const auto values = std::tuple{fields.read(event)...};
                  return std::apply(
                      [&](const auto&... values) -> DecodedBytecodeCommand {
                        if constexpr (std::is_member_object_pointer_v<Action>) {
                          static_assert(sizeof...(values) == 1, "A state assignment needs one operand");
                          return event.invoke(
                              [action](Playback& playback, const auto& value) {
                                using Value = std::remove_cvref_t<decltype(playback.track.*action)>;
                                playback.track.*action = static_cast<Value>(value);
                              },
                              values...);
                        } else {
                          return event.invoke(action, values...);
                        }
                      },
                      values);
                },
                operands);
          }) {}

    u8 opcode;
    Decode decode;
  };

  CommandTable(std::initializer_list<Entry> entries) {
    for (const auto& entry : entries) {
      if (commands_[entry.opcode]) {
        throw std::invalid_argument("Duplicate opcode in command table");
      }
      commands_[entry.opcode] = entry.decode;
    }
  }

  [[nodiscard]] std::optional<DecodedBytecodeCommand> decode(Cursor& cursor) const {
    const auto& decode = commands_[cursor.opcode()];
    return decode ? std::optional{decode(cursor)} : std::nullopt;
  }

private:
  std::array<Decode, 256> commands_;
};

}  // namespace vgmtrans::core
