/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/BytecodeDecode.h"

#include <string>
#include <utility>

namespace vgmtrans::core {

namespace command {

enum class Encoding { Byte, SignedByte, WordLE, SignedWordLE, VariableLength };

// A field declaration, read and annotated when its command is decoded.
template <Encoding encoding>
struct Operand {
  Operand(std::string name, SemanticOperandRole role = SemanticOperandRole::Value)
      : name(std::move(name)), role(role) {}
  Operand(std::string name, SourceValueDisplay display, SemanticOperandRole role = SemanticOperandRole::Value)
      : name(std::move(name)), display(display), role(role) {}

  std::string name;
  SourceValueDisplay display = (encoding == Encoding::SignedByte || encoding == Encoding::SignedWordLE)
                                   ? SourceValueDisplay::SignedDecimal
                                   : SourceValueDisplay::Default;
  SemanticOperandRole role = SemanticOperandRole::Value;

  template <class Event>
  auto read(Event& event) const {
    if constexpr (encoding == Encoding::Byte) {
      return event.u8(name, display, role);
    } else if constexpr (encoding == Encoding::SignedByte) {
      return event.s8(name, display, role);
    } else if constexpr (encoding == Encoding::WordLE) {
      return event.u16le(name, display, role);
    } else if constexpr (encoding == Encoding::SignedWordLE) {
      return event.s16le(name, display, role);
    } else {
      return event.varLen(name, display, role);
    }
  }
};

using Byte = Operand<Encoding::Byte>;
using SignedByte = Operand<Encoding::SignedByte>;
using WordLE = Operand<Encoding::WordLE>;
using SignedWordLE = Operand<Encoding::SignedWordLE>;
using VariableLength = Operand<Encoding::VariableLength>;

}  // namespace command

}  // namespace vgmtrans::core
