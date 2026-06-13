/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/LevelScale.h"
#include "value/core/PerformanceModel.h"
#include "value/core/SequenceDialect.h"
#include "value/core/Source.h"

#include <algorithm>
#include <string_view>

namespace vgmtrans::core {

// Parse mixins for common source-driver command shapes. They keep format command
// structs compact while still recording named operands for the source view.
template <class Derived>
struct NoOperands {
  static Derived parse(CommandReader&) { return {}; }
};

template <class Derived>
struct U8Operand {
  u8 raw = 0;

  static Derived parse(CommandReader& in) {
    Derived result;
    result.raw = in.u8(Derived::operandName);
    return result;
  }
};

template <class Derived>
struct S8Operand {
  s8 raw = 0;

  static Derived parse(CommandReader& in) {
    Derived result;
    result.raw = in.s8(Derived::operandName);
    return result;
  }
};

template <class Derived>
struct Be16Operand {
  u16 raw = 0;

  static Derived parse(CommandReader& in) {
    Derived result;
    result.raw = in.be16(Derived::operandName);
    return result;
  }
};

// Common one-operand command bodies. Formats still name each command locally,
// but these remove boilerplate for driver opcodes that only set state or emit a
// direct performance control.
template <class Derived, auto Member>
struct U8StateCommand : U8Operand<Derived> {
  void execute(auto& rt) const { rt.state.*Member = this->raw; }
};

template <class Derived, auto Member>
struct S8StateCommand : S8Operand<Derived> {
  void execute(auto& rt) const { rt.state.*Member = this->raw; }
};

template <class Derived, auto Member>
struct U8BoolStateCommand : U8Operand<Derived> {
  void execute(auto& rt) const { rt.state.*Member = this->raw != 0; }
};

template <class Derived, auto Member>
struct ToggleBoolStateCommand : NoOperands<Derived> {
  void execute(auto& rt) const { rt.state.*Member = !(rt.state.*Member); }
};

template <class Derived, auto Member, bool Value>
struct SetBoolStateCommand : NoOperands<Derived> {
  void execute(auto& rt) const { rt.state.*Member = Value; }
};

template <class Derived, auto Member>
using SetTrueStateCommand = SetBoolStateCommand<Derived, Member, true>;

template <class Derived, auto Member>
using SetFalseStateCommand = SetBoolStateCommand<Derived, Member, false>;

template <class Derived, void (Emit::*Method)(u8)>
struct U8RawOutCommand : U8Operand<Derived> {
  void execute(auto& rt) const { (rt.out.*Method)(this->raw); }
};

template <class Derived, void (Emit::*Method)(bool)>
struct U8BoolOutCommand : U8Operand<Derived> {
  void execute(auto& rt) const { (rt.out.*Method)(this->raw != 0); }
};

template <class Derived, ModulationPerformanceTarget Target>
struct U8MidiModulationOutCommand : U8Operand<Derived> {
  void execute(auto& rt) const {
    rt.out.modulation(Target, std::clamp(static_cast<double>(this->raw) / 127.0, 0.0, 1.0));
  }
};

// For source controls that are already MIDI-shaped. The performance model
// stores linear gain, so the source byte is squared before emission.
template <class Derived, void (Emit::*Method)(double, LevelPrecisionHint),
          LevelPrecisionHint PrecisionHint = LevelPrecisionHint::SevenBit>
struct U8MidiLevelOutCommand : U8Operand<Derived> {
  void execute(auto& rt) const { (rt.out.*Method)(LevelScale::linearFromMidi7(this->raw), PrecisionHint); }
};

}  // namespace vgmtrans::core
