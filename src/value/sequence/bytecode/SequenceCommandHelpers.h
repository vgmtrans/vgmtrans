/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/LevelScale.h"
#include "value/sequence/PerformanceModel.h"
#include "value/sequence/SequenceDialect.h"
#include "value/base/Source.h"

#include <algorithm>
#include <string_view>

namespace vgmtrans::core {

struct SourceOnlyCommand {
  static constexpr CommandPlaybackStatus playbackStatus = CommandPlaybackStatus::SourceOnly;
};

struct NoOpCommand {
  static constexpr CommandPlaybackStatus playbackStatus = CommandPlaybackStatus::NoOp;
};

// Base parsers for simple command shapes. They keep format command structs small
// while still recording named operands for the source view.
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

// Address helpers remove repeated parse code while leaving the actual address
// field on the command, so opcode maps can still refer to it directly.
template <class Derived>
struct Be16AddressOperand {
  static Derived parse(CommandReader& in) {
    return Derived{.destination = in.be16Address("destination")};
  }
};

template <class Derived>
struct Le24RelativeAddressOperand {
  static Derived parse(CommandReader& in) {
    return Derived{.relativeDestination = in.le24("destination")};
  }
};

// Helpers for simple one-operand commands. Formats still name each command
// locally; these only remove repeated parse/execute boilerplate.
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

template <class Derived, void (PerformanceEmitter::*Method)(u8)>
struct U8RawOutCommand : U8Operand<Derived> {
  void execute(auto& rt) const { (rt.out.*Method)(this->raw); }
};

template <class Derived, void (PerformanceEmitter::*Method)(bool)>
struct U8BoolOutCommand : U8Operand<Derived> {
  void execute(auto& rt) const { (rt.out.*Method)(this->raw != 0); }
};

template <class Derived, ModulationPerformanceTarget Target>
struct U8NormalizedModulationOutCommand : U8Operand<Derived> {
  void execute(auto& rt) const {
    rt.out.modulation(Target, std::clamp(static_cast<double>(this->raw) / 127.0, 0.0, 1.0));
  }
};

// For source level controls that already use MIDI's 0-127 curve. The performance
// model stores linear gain, so convert before emitting the event.
template <class Derived, void (PerformanceEmitter::*Method)(double, LevelPrecisionHint),
          LevelPrecisionHint PrecisionHint = LevelPrecisionHint::SevenBit>
struct U8MidiLevelOutCommand : U8Operand<Derived> {
  void execute(auto& rt) const { (rt.out.*Method)(LevelScale::linearFromMidi7(this->raw), PrecisionHint); }
};

}  // namespace vgmtrans::core
