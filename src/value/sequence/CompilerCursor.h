/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/RecordReader.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <any>
#include <functional>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace vgmtrans::core {

namespace detail {

template <class T>
[[nodiscard]] SemanticOperandValue executableValue(T value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Value, Address> || std::is_same_v<Value, bool> || std::is_same_v<Value, std::string>) {
    return SemanticOperandValue{std::move(value)};
  } else if constexpr (std::is_same_v<Value, std::string_view>) {
    return SemanticOperandValue{std::string(value)};
  } else if constexpr (std::is_enum_v<Value>) {
    return executableValue(static_cast<std::underlying_type_t<Value>>(value));
  } else if constexpr (std::is_floating_point_v<Value>) {
    return SemanticOperandValue{static_cast<double>(value)};
  } else if constexpr (std::is_signed_v<Value>) {
    return SemanticOperandValue{static_cast<s64>(value)};
  } else {
    return SemanticOperandValue{static_cast<u64>(value)};
  }
}

template <class T>
[[nodiscard]] T executableArgument(const SemanticOperandValue& value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Value, Address> || std::is_same_v<Value, bool> || std::is_same_v<Value, std::string>) {
    return std::get<Value>(value);
  } else if constexpr (std::is_enum_v<Value>) {
    return static_cast<Value>(executableArgument<std::underlying_type_t<Value>>(value));
  } else if constexpr (std::is_floating_point_v<Value>) {
    return static_cast<Value>(std::get<double>(value));
  } else if constexpr (std::is_signed_v<Value>) {
    return static_cast<Value>(std::get<s64>(value));
  } else {
    return static_cast<Value>(std::get<u64>(value));
  }
}

// Deferred values are tiny expression types whose shape is encoded in the
// generated executor. Only constants are stored in CommandAction::arguments;
// state-member identities never enter the durable sequence model.
template <class T>
struct ConstantValue {
  using deferred_value_tag = void;
  using value_type = T;

  T value;

  void store(std::vector<SemanticOperandValue>& arguments) const { arguments.push_back(executableValue(value)); }

  template <class Playback>
  [[nodiscard]] static T evaluate(std::span<const SemanticOperandValue> arguments, size_t& next, Playback&) {
    if (next >= arguments.size()) {
      throw std::logic_error("Compiled sequence expression did not have enough constant arguments");
    }
    return executableArgument<T>(arguments[next++]);
  }
};

template <class T>
struct MemberPointerTraits;

template <class Owner, class Value>
struct MemberPointerTraits<Value Owner::*> {
  using owner_type = Owner;
  using value_type = Value;
};

template <auto Member>
struct StateValue {
  using deferred_value_tag = void;
  using traits = MemberPointerTraits<std::remove_cv_t<decltype(Member)>>;
  using owner_type = typename traits::owner_type;
  using value_type = std::remove_cv_t<typename traits::value_type>;

  void store(std::vector<SemanticOperandValue>&) const {}

  template <class Playback>
  [[nodiscard]] static value_type evaluate(std::span<const SemanticOperandValue>, size_t&, Playback& playback) {
    return playback.track.*Member;
  }
};

template <class T>
concept DeferredValue = requires { typename std::remove_cvref_t<T>::deferred_value_tag; };

template <class T>
[[nodiscard]] auto defer(T value) {
  if constexpr (DeferredValue<T>) {
    return value;
  } else if constexpr (std::is_same_v<std::remove_cvref_t<T>, std::string_view>) {
    return ConstantValue<std::string>{std::string(value)};
  } else {
    return ConstantValue<std::remove_cvref_t<T>>{std::move(value)};
  }
}

template <class T>
using DeferredValueType = typename decltype(defer(std::declval<T>()))::value_type;

template <class Condition, class WhenTrue, class WhenFalse>
struct SelectedValue {
  using deferred_value_tag = void;
  using value_type = std::common_type_t<typename WhenTrue::value_type, typename WhenFalse::value_type>;

  Condition condition;
  WhenTrue whenTrue;
  WhenFalse whenFalse;

  void store(std::vector<SemanticOperandValue>& arguments) const {
    condition.store(arguments);
    whenTrue.store(arguments);
    whenFalse.store(arguments);
  }

  template <class Playback>
  [[nodiscard]] static value_type evaluate(std::span<const SemanticOperandValue> arguments, size_t& next,
                                           Playback& playback) {
    const bool selected = static_cast<bool>(Condition::template evaluate<Playback>(arguments, next, playback));
    const value_type trueValue =
        static_cast<value_type>(WhenTrue::template evaluate<Playback>(arguments, next, playback));
    const value_type falseValue =
        static_cast<value_type>(WhenFalse::template evaluate<Playback>(arguments, next, playback));
    return selected ? trueValue : falseValue;
  }
};

template <class Playback>
using CompiledExecutor = Effects (*)(std::span<const SemanticOperandValue>, Playback&);

template <class Playback, auto Operation, class... Values>
[[nodiscard]] Effects executeOperation(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  size_t next = 0;
  // List initialization guarantees that constants are consumed left-to-right.
  std::tuple<typename Values::value_type...> values{
      Values::template evaluate<Playback>(arguments, next, playback)...,
  };
  if (next != arguments.size()) {
    throw std::logic_error("Compiled sequence expression had unused constant arguments");
  }

  return std::apply(
      [&](auto&&... value) -> Effects {
        if constexpr (std::is_same_v<std::invoke_result_t<decltype(Operation), Playback&, decltype(value)...>,
                                     Effects>) {
          return std::invoke(Operation, playback, std::forward<decltype(value)>(value)...);
        } else {
          std::invoke(Operation, playback, std::forward<decltype(value)>(value)...);
          return Effects{};
        }
      },
      std::move(values));
}

// Commands retain a small slot rather than a callback. The dialect-owned
// registry stores one generated thunk for each operation used by the format.
template <class Playback>
class CompiledExecutorRegistry {
public:
  [[nodiscard]] u32 add(CompiledExecutor<Playback> executor) {
    std::scoped_lock lock(mutex_);
    const auto found = std::ranges::find(executors_, executor);
    if (found != executors_.end()) {
      return static_cast<u32>(std::distance(executors_.begin(), found));
    }
    executors_.push_back(executor);
    return static_cast<u32>(executors_.size() - 1);
  }

  [[nodiscard]] Effects execute(u32 slot, std::span<const SemanticOperandValue> arguments, Playback& playback) {
    CompiledExecutor<Playback> executor = nullptr;
    {
      std::scoped_lock lock(mutex_);
      if (slot >= executors_.size()) {
        throw std::logic_error("Compiled sequence command referenced an unknown executor slot");
      }
      executor = executors_[slot];
    }
    return executor(arguments, playback);
  }

private:
  std::mutex mutex_;
  std::vector<CompiledExecutor<Playback>> executors_;
};

template <class Playback>
[[nodiscard]] CompiledExecutorRegistry<Playback>& compiledExecutors() {
  static CompiledExecutorRegistry<Playback> registry;
  return registry;
}

template <class Playback, auto Method, class... Arguments>
[[nodiscard]] Effects invokeMember(Playback& playback, Arguments... arguments) {
  if constexpr (std::is_same_v<std::invoke_result_t<decltype(Method), Playback&, Arguments...>, Effects>) {
    return std::invoke(Method, playback, std::move(arguments)...);
  } else {
    std::invoke(Method, playback, std::move(arguments)...);
    return Effects{};
  }
}

template <class Playback, class Handler, class... Arguments>
[[nodiscard]] Effects invokeInline(Playback& playback, Arguments... arguments) {
  static_assert(std::is_empty_v<Handler> && std::is_default_constructible_v<Handler>,
                "Inline compiler-cursor handlers must be captureless");
  Handler handler;
  if constexpr (std::is_same_v<std::invoke_result_t<Handler&, Playback&, Arguments...>, Effects>) {
    return std::invoke(handler, playback, std::move(arguments)...);
  } else {
    std::invoke(handler, playback, std::move(arguments)...);
    return Effects{};
  }
}

template <class Playback, auto Member, class Argument>
void setMember(Playback& playback, Argument value) {
  using Value = std::remove_cvref_t<decltype(playback.track.*Member)>;
  playback.track.*Member = static_cast<Value>(value);
}

template <class Playback, auto Member, class Argument>
void addMember(Playback& playback, Argument value) {
  using Value = std::remove_cvref_t<decltype(playback.track.*Member)>;
  playback.track.*Member += static_cast<Value>(value);
}

template <class Playback, auto Member>
void toggleMember(Playback& playback) {
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(playback.track.*Member)>, bool>);
  playback.track.*Member = !(playback.track.*Member);
}

template <class Playback>
void emitLevel(Playback& playback, double gain) {
  playback.out.level(gain);
}

template <class Playback>
void emitQuantizedLevel(Playback& playback, double gain, u32 levels) {
  playback.out.level(gain, ValueQuantization{.levels = levels});
}

template <class Playback>
void emitExpression(Playback& playback, double gain) {
  playback.out.expression(gain);
}

template <class Playback>
void emitPan(Playback& playback, double position) {
  playback.out.pan(position);
}

template <class Playback>
void emitStereoBalance(Playback& playback, double leftGain, double rightGain) {
  playback.out.stereoBalance(leftGain, rightGain);
}

template <class Playback>
void emitInstrument(Playback& playback, u32 bank, u32 program) {
  playback.out.instrument(bank, program);
}

template <class Playback>
void emitSourceInstrument(Playback& playback, std::string domain, u32 key) {
  playback.out.instrument(InstrumentIdentity{
      .domain = std::move(domain),
      .key = key,
  });
}

template <class Playback>
void emitTempo(Playback& playback, u32 microsecondsPerQuarter) {
  playback.out.tempo(microsecondsPerQuarter);
}

template <class Playback>
void emitMasterLevel(Playback& playback, double gain) {
  playback.out.masterLevel(gain);
}

template <class Playback>
void emitReverb(Playback& playback, double send) {
  playback.out.reverb(send);
}

template <class Playback>
void emitTuning(Playback& playback, double cents) {
  playback.out.tuning(cents);
}

template <class Playback>
void emitGlobalTranspose(Playback& playback, s32 semitones) {
  playback.out.globalTranspose(semitones);
}

template <class Playback>
void emitLegatoPedal(Playback& playback, bool enabled) {
  playback.out.legatoPedal(enabled);
}

template <class Playback>
void emitPitchBend(Playback& playback, double semitones) {
  playback.out.pitchBend(semitones);
}

template <class Playback, auto ScaleMember>
void emitPitchBendScaledBy(Playback& playback, double fraction) {
  playback.out.pitchBend(fraction * static_cast<double>(playback.track.*ScaleMember));
}

template <class Playback>
void emitPitchBendRange(Playback& playback, u8 semitones) {
  playback.out.pitchBendRange(semitones);
}

template <class Playback>
void emitModulation(Playback& playback, ModulationPerformanceTarget target, double amount) {
  playback.out.modulation(target, amount);
}

template <class Playback>
void emitPitchDepthModulation(Playback& playback, ModulationPerformanceTarget target, double amount,
                              double pitchDepthSemitones) {
  playback.out.modulation(ModulationPerformanceEvent{
      .target = target,
      .amount = amount,
      .pitchDepthSemitones = pitchDepthSemitones,
  });
}

template <class Playback>
void emitModulationRate(Playback& playback, ModulationPerformanceTarget target, double amount, double hertz) {
  playback.out.modulation(ModulationPerformanceEvent{
      .target = target,
      .amount = amount,
      .frequencyHz = hertz,
  });
}

template <class Playback>
void emitPortamentoEnable(Playback& playback, bool enabled) {
  playback.out.portamentoEnable(enabled);
}

template <class Playback>
void emitPortamentoTime(Playback& playback, double milliseconds) {
  playback.out.portamentoTime(milliseconds);
}

template <class Playback>
[[nodiscard]] Effects wait(Playback&, u32 ticks) {
  return Effects::wait(ticks);
}

template <class Playback>
[[nodiscard]] Effects jump(Playback& playback, Address destination) {
  return Effects{.step = playback.vm.jump(destination)};
}

template <class Playback>
[[nodiscard]] Effects loopCandidate(Playback& playback, Address destination) {
  return Effects{.step = playback.vm.loopCandidate(destination)};
}

template <class Playback>
[[nodiscard]] Effects declaredLoop(Playback& playback, Address destination) {
  return Effects{.step = playback.vm.declaredLoop(destination)};
}

template <class Playback>
[[nodiscard]] Effects call(Playback& playback, Address destination) {
  return Effects{.step = playback.vm.call(destination)};
}

template <class Playback>
[[nodiscard]] Effects return_(Playback& playback) {
  return Effects{.step = playback.vm.return_()};
}

template <class Playback>
[[nodiscard]] Effects repeatUntil(Playback& playback, u8 slot, u32 totalPlays, Address destination) {
  return playback.vm.countedRepeatUntil(slot, totalPlays, destination);
}

}  // namespace detail

// CompilerCursor gives formats one imperative command block. Reads add source
// metadata immediately; event operations append typed executable actions for
// later, source-free SequenceVm execution.
template <class TrackState, class Playback>
class CompilerCursor {
public:
  class Event {
  public:
    [[nodiscard]] bool ok() const noexcept { return cursor_.record_.ok(); }

    ::u8 u8(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
            SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.u8(name, display), name, display, role);
    }

    ::u8 u8(std::string_view name, SemanticOperandRole role) { return u8(name, SourceValueDisplay::Default, role); }

    [[nodiscard]] EncodedSemanticField<::u8> rawU8(std::string_view name,
                                                   SourceValueDisplay display = SourceValueDisplay::Default) {
      return field(cursor_.record_.u8(name, display), name, display);
    }

    ::s8 s8(std::string_view name, SourceValueDisplay display = SourceValueDisplay::SignedDecimal,
            SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.s8(name, display), name, display, role);
    }

    ::s8 s8(std::string_view name, SemanticOperandRole role) {
      return s8(name, SourceValueDisplay::SignedDecimal, role);
    }

    [[nodiscard]] EncodedSemanticField<::s8> rawS8(std::string_view name,
                                                   SourceValueDisplay display = SourceValueDisplay::SignedDecimal) {
      return field(cursor_.record_.s8(name, display), name, display);
    }

    u16 u16be(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
              SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.u16be(name, display), name, display, role);
    }

    [[nodiscard]] EncodedSemanticField<u16> rawU16be(std::string_view name,
                                                     SourceValueDisplay display = SourceValueDisplay::Default) {
      return field(cursor_.record_.u16be(name, display), name, display);
    }

    u16 u16le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
              SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.u16le(name, display), name, display, role);
    }

    [[nodiscard]] EncodedSemanticField<u16> rawU16le(std::string_view name,
                                                     SourceValueDisplay display = SourceValueDisplay::Default) {
      return field(cursor_.record_.u16le(name, display), name, display);
    }

    s16 s16le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::SignedDecimal,
              SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.s16le(name, display), name, display, role);
    }

    u32 u24le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
              SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.u24le(name, display), name, display, role);
    }

    u32 u32be(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
              SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.u32be(name, display), name, display, role);
    }

    [[nodiscard]] EncodedSemanticField<u32> rawU32be(std::string_view name,
                                                     SourceValueDisplay display = SourceValueDisplay::Default) {
      return field(cursor_.record_.u32be(name, display), name, display);
    }

    u32 u32le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
              SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.u32le(name, display), name, display, role);
    }

    [[nodiscard]] EncodedSemanticField<u32> rawU32le(std::string_view name,
                                                     SourceValueDisplay display = SourceValueDisplay::Default) {
      return field(cursor_.record_.u32le(name, display), name, display);
    }

    u32 varLen(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
               SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.varLen(name, display), name, display, role);
    }

    u32 varLen(std::string_view name, SemanticOperandRole role) {
      return varLen(name, SourceValueDisplay::Default, role);
    }

    std::string rawBytes(std::string_view name, u32 size) {
      return cursor_.decoded(cursor_.record_.rawBytes(name, size), name, SourceValueDisplay::Hex,
                             SemanticOperandRole::Value);
    }

    // Look ahead without consuming or annotating the byte. This is useful when
    // a command may have an optional suffix identified by its own opcode.
    [[nodiscard]] std::optional<::u8> peekU8() const { return cursor_.record_.peekU8(); }

    // Some commands implicitly refer to the byte immediately after themselves,
    // such as a loop start with no encoded destination.
    [[nodiscard]] Address nextAddress() const { return Address{cursor_.record_.position()}; }

    [[nodiscard]] Address address(std::string_view name, SemanticOperandRole role = SemanticOperandRole::Address) {
      return Address{u16be(name, SourceValueDisplay::Address, role)};
    }

    [[nodiscard]] Address addressLe(std::string_view name, SemanticOperandRole role = SemanticOperandRole::Address) {
      return Address{u16le(name, SourceValueDisplay::Address, role)};
    }

    template <::u8 Shift, ::u8 Width>
    ::u8 opcodeBits(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
                    SemanticOperandRole role = SemanticOperandRole::Value) {
      static_assert(Width > 0 && Width <= 8 && Shift + Width <= 8);
      constexpr u16 mask = (u16{1} << Width) - 1;
      const auto result = static_cast<::u8>((cursor_.opcode_ >> Shift) & mask);
      opcodeValue(name, result, display, role);
      return result;
    }

    template <class T>
    T opcodeValue(std::string_view name, T value, SourceValueDisplay display = SourceValueDisplay::Default,
                  SemanticOperandRole role = SemanticOperandRole::Value) {
      cursor_.add(name, detail::executableValue(value), cursor_.opcodeRange_, display, role);
      return value;
    }

    template <class T>
    T derived(std::string_view name, T value, SourceValueDisplay display = SourceValueDisplay::Default,
              SemanticOperandRole role = SemanticOperandRole::Value) {
      if (cursor_.record_.ok()) {
        cursor_.add(name, detail::executableValue(value), {}, display, role);
      }
      return value;
    }

    template <class T>
    T derived(std::string_view name, T value, SemanticOperandRole role) {
      return derived(name, std::move(value), SourceValueDisplay::Default, role);
    }

    template <class T, class Convert>
    [[nodiscard]] auto resolved(std::string_view name, const EncodedSemanticField<T>& source, Convert convert,
                                SourceValueDisplay display = SourceValueDisplay::Default,
                                SemanticOperandRole role = SemanticOperandRole::Value)
        -> std::invoke_result_t<Convert, T> {
      using Resolved = std::invoke_result_t<Convert, T>;
      return source.valid ? resolvedValue(name, source, std::invoke(convert, source.value), display, role) : Resolved{};
    }

    template <class T, class Resolved>
    [[nodiscard]] Resolved resolvedValue(std::string_view name, const EncodedSemanticField<T>& source,
                                         Resolved resolved, SourceValueDisplay display = SourceValueDisplay::Default,
                                         SemanticOperandRole role = SemanticOperandRole::Value) {
      if (source.valid) {
        cursor_.operands_.push_back(SemanticOperand{
            .value = detail::executableValue(resolved),
            .range = source.range,
            .name = std::string(name),
            .display = display,
            .role = role,
            .encodedValue = detail::executableValue(source.value),
            .encodedName = std::string(source.name),
            .encodedDisplay = source.display,
        });
      }
      return resolved;
    }

    void warning(std::string message) { cursor_.warning(std::move(message)); }

    // Operations accumulate in source order and return the same builder. A
    // return statement converts the final Event expression into the decoded
    // command, so callers may freely mix chained and standalone calls.
    Event& ignore() {
      execution_ = {};
      flow_ = {};
      presentation_.playback = initialPlayback_;
      return *this;
    }

    Event& stop() {
      flow_ = DecodeFlow::terminalFlow();
      return *this;
    }

    Event& end() {
      presentation_.semantic = SequenceSemantic::End;
      presentation_.playback = CommandPlaybackStatus::StopsPlayback;
      flow_ = DecodeFlow::terminalFlow();
      return *this;
    }

    // state() is a deferred read: it observes the member when the action using
    // it executes, after any preceding set/add/toggle action in this command.
    template <auto Member>
    [[nodiscard]] auto state() const {
      using State = detail::StateValue<Member>;
      static_assert(std::is_same_v<typename State::owner_type, TrackState>,
                    "Compiler cursor state members must belong to its TrackState");
      return State{};
    }

    // Both outcomes are explicit; the boolean condition is read when the
    // consuming action executes.
    template <class Condition, class WhenTrue, class WhenFalse>
    [[nodiscard]] auto select(Condition condition, WhenTrue whenTrue, WhenFalse whenFalse) const {
      auto deferredCondition = detail::defer(std::move(condition));
      auto deferredTrue = detail::defer(std::move(whenTrue));
      auto deferredFalse = detail::defer(std::move(whenFalse));
      using ConditionValue = decltype(deferredCondition);
      using TrueValue = decltype(deferredTrue);
      using FalseValue = decltype(deferredFalse);
      static_assert(std::is_same_v<typename ConditionValue::value_type, bool>,
                    "Compiler cursor select conditions must be boolean");
      return detail::SelectedValue<ConditionValue, TrueValue, FalseValue>{
          .condition = std::move(deferredCondition),
          .whenTrue = std::move(deferredTrue),
          .whenFalse = std::move(deferredFalse),
      };
    }

    Event& wait(auto ticks) { return append<&detail::wait<Playback>>(std::move(ticks)); }

    Event& emitLevel(auto gain) { return append<&detail::emitLevel<Playback>>(std::move(gain)); }

    Event& emitLevel(auto gain, ValueQuantization quantization) {
      return append<&detail::emitQuantizedLevel<Playback>>(std::move(gain), quantization.levels);
    }

    Event& emitExpression(auto gain) { return append<&detail::emitExpression<Playback>>(std::move(gain)); }

    Event& emitPan(auto position) { return append<&detail::emitPan<Playback>>(std::move(position)); }

    Event& emitStereoBalance(auto leftGain, auto rightGain) {
      return append<&detail::emitStereoBalance<Playback>>(std::move(leftGain), std::move(rightGain));
    }

    Event& emitInstrument(auto bank, auto program) {
      return append<&detail::emitInstrument<Playback>>(std::move(bank), std::move(program));
    }

    Event& emitInstrument(std::string_view domain, auto key) {
      return append<&detail::emitSourceInstrument<Playback>>(domain, std::move(key));
    }

    Event& emitTempo(auto microsecondsPerQuarter) {
      return append<&detail::emitTempo<Playback>>(std::move(microsecondsPerQuarter));
    }

    Event& emitMasterLevel(auto gain) { return append<&detail::emitMasterLevel<Playback>>(std::move(gain)); }

    Event& emitReverb(auto send) { return append<&detail::emitReverb<Playback>>(std::move(send)); }

    Event& emitTuning(auto cents) { return append<&detail::emitTuning<Playback>>(std::move(cents)); }

    Event& emitGlobalTranspose(auto semitones) {
      return append<&detail::emitGlobalTranspose<Playback>>(std::move(semitones));
    }

    Event& emitLegatoPedal(auto enabled) { return append<&detail::emitLegatoPedal<Playback>>(std::move(enabled)); }

    Event& emitPitchBend(auto semitones) { return append<&detail::emitPitchBend<Playback>>(std::move(semitones)); }

    template <auto ScaleMember>
    Event& emitPitchBendScaledBy(auto fraction) {
      return append<&detail::emitPitchBendScaledBy<Playback, ScaleMember>>(std::move(fraction));
    }

    Event& emitPitchBendRange(auto semitones) {
      return append<&detail::emitPitchBendRange<Playback>>(std::move(semitones));
    }

    Event& emitModulation(ModulationPerformanceTarget target, auto amount) {
      return append<&detail::emitModulation<Playback>>(target, std::move(amount));
    }

    Event& emitModulation(ModulationPerformanceTarget target, auto amount, auto pitchDepthSemitones) {
      return append<&detail::emitPitchDepthModulation<Playback>>(target, std::move(amount),
                                                                 std::move(pitchDepthSemitones));
    }

    Event& emitVibratoRate(auto amount, auto hertz) {
      return append<&detail::emitModulationRate<Playback>>(ModulationPerformanceTarget::VibratoRate,
                                                           std::move(amount), std::move(hertz));
    }

    Event& emitTremoloRate(auto amount, auto hertz) {
      return append<&detail::emitModulationRate<Playback>>(ModulationPerformanceTarget::TremoloRate,
                                                           std::move(amount), std::move(hertz));
    }

    Event& emitPortamentoEnable(auto enabled) {
      return append<&detail::emitPortamentoEnable<Playback>>(std::move(enabled));
    }

    Event& emitPortamentoTime(auto milliseconds) {
      return append<&detail::emitPortamentoTime<Playback>>(std::move(milliseconds));
    }

    template <auto Member, class Value>
    Event& set(Value value) {
      using Argument = detail::DeferredValueType<Value>;
      return append<&detail::setMember<Playback, Member, Argument>>(std::move(value));
    }

    template <auto Member, class Value>
    Event& add(Value value) {
      using Argument = detail::DeferredValueType<Value>;
      return append<&detail::addMember<Playback, Member, Argument>>(std::move(value));
    }

    template <auto Member>
    Event& toggle() {
      return append<&detail::toggleMember<Playback, Member>>();
    }

    template <auto Method, class... Arguments>
    Event& invoke(Arguments... arguments) {
      return append<&detail::invokeMember<Playback, Method, detail::DeferredValueType<Arguments>...>>(
          std::move(arguments)...);
    }

    // A captureless inline handler is the locality escape hatch for short,
    // one-off runtime behavior. Source values remain explicit positional
    // arguments; no closure object enters the durable command.
    template <class Handler, class... Arguments>
    Event& invoke(Handler, Arguments... arguments) {
      return append<&detail::invokeInline<Playback, std::decay_t<Handler>, detail::DeferredValueType<Arguments>...>>(
          std::move(arguments)...);
    }

    Event& jump(Address destination) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      targetRole(destination, SemanticOperandRole::JumpTarget);
      append<&detail::jump<Playback>>(destination);
      flow_ = DecodeFlow::jump(destination);
      return *this;
    }

    Event& loopCandidate(Address destination, SemanticOperandRole role = SemanticOperandRole::LoopTarget) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      targetRole(destination, role);
      append<&detail::loopCandidate<Playback>>(destination);
      flow_ = DecodeFlow::jump(destination);
      return *this;
    }

    Event& declaredLoop(Address destination, SemanticOperandRole role = SemanticOperandRole::LoopTarget) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      targetRole(destination, role);
      append<&detail::declaredLoop<Playback>>(destination);
      flow_ = DecodeFlow::jump(destination);
      return *this;
    }

    Event& call(Address destination) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      targetRole(destination, SemanticOperandRole::CallTarget);
      append<&detail::call<Playback>>(destination);
      flow_ = DecodeFlow::call(destination, Address{cursor_.record_.position()});
      return *this;
    }

    Event& return_() {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      append<&detail::return_<Playback>>();
      flow_ = DecodeFlow::return_();
      return *this;
    }

    // Some drivers use one opcode for both top-level end and subroutine
    // return. Discovery treats it as a block return while a typed runtime
    // action chooses the actual result from call history.
    Event& discoverReturn() {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      flow_ = DecodeFlow::return_();
      return *this;
    }

    // Marks a typed action whose destination comes from runtime state. Static
    // discovery still follows the command's ordinary fallthrough.
    Event& runtimeControlFlow() {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      return *this;
    }

    Event& repeatUntil(::u8 slot, u32 totalPlays, Address destination) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      targetRole(destination, SemanticOperandRole::RepeatTarget);
      append<&detail::repeatUntil<Playback>>(slot, totalPlays, destination);
      flow_.staticTargets.push_back(destination);
      return *this;
    }

    // Record a conditional branch destination while a format-specific action
    // decides at runtime whether the branch is taken.
    Event& mayBranchTo(Address destination, SemanticOperandRole role = SemanticOperandRole::Address) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      targetRole(destination, role);
      flow_.staticTargets.push_back(destination);
      return *this;
    }

    [[nodiscard]] operator DecodedBytecodeCommand() { return finish(); }

  private:
    friend class CompilerCursor;

    Event(CompilerCursor& cursor, DecodedCommandPresentation presentation)
        : cursor_(cursor), presentation_(std::move(presentation)), initialPlayback_(presentation_.playback) {}

    template <auto Operation, class... Arguments>
    Event& append(Arguments... arguments) {
      return appendDeferred<Operation>(detail::defer(std::move(arguments))...);
    }

    template <auto Operation, class... Values>
    Event& appendDeferred(Values... values) {
      if (presentation_.playback == CommandPlaybackStatus::SourceOnly ||
          presentation_.playback == CommandPlaybackStatus::NoOp) {
        presentation_.playback = CommandPlaybackStatus::AffectsPlayback;
      }
      CommandAction action{
          .executor =
              detail::compiledExecutors<Playback>().add(&detail::executeOperation<Playback, Operation, Values...>),
      };
      action.arguments.reserve(sizeof...(Values));
      (values.store(action.arguments), ...);
      execution_.actions.push_back(std::move(action));
      return *this;
    }

    template <class T>
    [[nodiscard]] static EncodedSemanticField<T> field(const RangedValue<T>& source, std::string_view name,
                                                       SourceValueDisplay display) {
      return EncodedSemanticField<T>{
          .value = source.value,
          .range = source.range,
          .name = name,
          .display = display,
          .valid = source.valid,
      };
    }

    void targetRole(Address destination, SemanticOperandRole role) {
      const auto found = std::ranges::find_if(cursor_.operands_.rbegin(), cursor_.operands_.rend(),
                                              [destination](const SemanticOperand& operand) {
                                                const auto* address = std::get_if<Address>(&operand.value);
                                                if (address != nullptr) {
                                                  return address->value == destination.value;
                                                }
                                                const auto* unsignedValue = std::get_if<u64>(&operand.value);
                                                return unsignedValue != nullptr && *unsignedValue == destination.value;
                                              });
      if (found != cursor_.operands_.rend()) {
        found->role = role;
      }
    }

    [[nodiscard]] DecodedBytecodeCommand finish() {
      if (finished_) {
        throw std::logic_error("Compiler cursor event was finalized more than once");
      }
      finished_ = true;
      return cursor_.finish(std::move(presentation_), std::move(execution_), std::move(flow_));
    }

    CompilerCursor& cursor_;
    DecodedCommandPresentation presentation_;
    CommandPlaybackStatus initialPlayback_;
    CommandExecution execution_;
    DecodeFlow flow_;
    bool finished_ = false;
  };

  CompilerCursor(ByteReader reader, u32 begin, u32 end, std::string_view detailKindPrefix,
                 std::vector<Diagnostic>* diagnostics = nullptr)
      : record_(reader, begin, end, diagnostics, false), detailKindPrefix_(detailKindPrefix), diagnostics_(diagnostics) {
    const auto opcode = record_.u8("opcode", SourceValueDisplay::Hex);
    if (opcode) {
      opcode_ = *opcode;
      opcodeRange_ = opcode.range;
    }
  }

  // Most extracted sequence sources use the complete byte buffer. Formats
  // with a meaningful subrange continue to pass an explicit end offset.
  CompilerCursor(ByteReader reader, u32 begin, std::string_view detailKindPrefix,
                 std::vector<Diagnostic>* diagnostics = nullptr)
      : CompilerCursor(reader, begin, static_cast<u32>(std::min<u64>(reader.size(), std::numeric_limits<u32>::max())),
                       detailKindPrefix, diagnostics) {}

  [[nodiscard]] bool hasOpcode() const noexcept { return opcodeRange_.size != 0; }
  [[nodiscard]] bool ok() const noexcept { return record_.ok(); }
  [[nodiscard]] ::u8 opcode() const noexcept { return opcode_; }

  [[nodiscard]] Event command(std::string_view label, SequenceSemantic semantic,
                              CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback,
                              std::string_view localKind = {}) {
    const std::string kind = localKind.empty() ? sourceLocalKind(label) : std::string(localKind);
    return Event{*this, DecodedCommandPresentation{
                            .label = std::string(label),
                            .localKind = kind,
                            .detailKind = detailKindPrefix_.empty() ? kind : detailKindPrefix_ + "." + kind,
                            .semantic = semantic,
                            .playback = playback,
                        }};
  }

  [[nodiscard]] Event sourceOnly(std::string_view label, std::string_view localKind = {}) {
    return command(label, SequenceSemantic::Meta, CommandPlaybackStatus::SourceOnly, localKind);
  }

  [[nodiscard]] Event noOp(std::string_view label, std::string_view localKind = {}) {
    return command(label, SequenceSemantic::Meta, CommandPlaybackStatus::NoOp, localKind);
  }

  [[nodiscard]] Event unsupported(std::string_view label, std::string_view localKind = "unsupported") {
    return command(label, SequenceSemantic::Unsupported, CommandPlaybackStatus::Unsupported, localKind);
  }

  // Records the command and its raw operands for source inspection, but
  // compiles no playback behavior and continues to the next command.
  [[nodiscard]] DecodedBytecodeCommand ignored(std::string_view label, u32 operandBytes,
                                               std::string_view localKind = {}) {
    auto event = sourceOnly(label, localKind);
    static_cast<void>(event.rawBytes("bytes", operandBytes));
    return event.ignore();
  }

  [[nodiscard]] DecodedBytecodeCommand truncated() {
    return finish(truncatedPresentation(), {}, DecodeFlow::terminalFlow());
  }

private:
  template <class T>
  T decoded(const RangedValue<T>& field, std::string_view name, SourceValueDisplay display, SemanticOperandRole role) {
    if (field) {
      add(name, detail::executableValue(field.value), field.range, display, role);
    }
    return field.value;
  }

  void add(std::string_view name, SemanticOperandValue value, SourceRange range, SourceValueDisplay display,
           SemanticOperandRole role) {
    operands_.push_back(SemanticOperand{
        .value = std::move(value),
        .range = range,
        .name = std::string(name),
        .display = display,
        .role = role,
    });
  }

  void warning(std::string message) {
    if (diagnostics_ != nullptr) {
      diagnostics_->push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = std::move(message),
          .range = record_.range(),
      });
    }
  }

  [[nodiscard]] DecodedCommandPresentation truncatedPresentation() const {
    const std::string kind = "truncated";
    return DecodedCommandPresentation{
        .label = "Truncated Command",
        .localKind = kind,
        .detailKind = detailKindPrefix_.empty() ? kind : detailKindPrefix_ + "." + kind,
        .semantic = SequenceSemantic::Unsupported,
        .playback = CommandPlaybackStatus::Unsupported,
    };
  }

  [[nodiscard]] DecodedBytecodeCommand finish(DecodedCommandPresentation presentation, CommandExecution execution,
                                              DecodeFlow flow) {
    const bool truncated = !record_.ok();
    if (truncated) {
      presentation = truncatedPresentation();
      execution = {};
      flow = DecodeFlow::terminalFlow();
    } else if (flow.kind == DecodeFlow::Kind::Fallthrough && !flow.fallthrough) {
      flow.fallthrough = Address{record_.position()};
    }

    return DecodedBytecodeCommand{
        .range = record_.range(),
        .opcode = opcode_,
        .encodedSize = std::max<u32>(1, record_.size()),
        .flow = std::move(flow),
        .operands = std::move(operands_),
        .execution = std::move(execution),
        .presentation = std::move(presentation),
    };
  }

  RecordReader record_;
  std::string detailKindPrefix_;
  std::vector<Diagnostic>* diagnostics_ = nullptr;
  ::u8 opcode_ = 0;
  SourceRange opcodeRange_;
  std::vector<SemanticOperand> operands_;
};

// This adapter is the only place a compiled format sees std::any. Format
// commands and Playback methods remain fully typed.
struct EmptyCompiledProgramState {};

template <class TrackState, class Playback, class ProgramState = EmptyCompiledProgramState>
struct CompiledCommandDialect {
  [[nodiscard]] static std::any createProgramState(const SequenceProgram& program) {
    // A format can read immutable program settings in its constructor. Formats
    // that need no settings keep working with an ordinary default constructor.
    if constexpr (std::constructible_from<ProgramState, const SequenceProgram&>) {
      return ProgramState{program};
    } else {
      return ProgramState{};
    }
  }

  [[nodiscard]] static std::any createTrackState(const SequenceProgram& program, const TrackProgram& track) {
    // Choose the most informative constructor the state type provides. This
    // keeps version and track identity out of loosely typed extra settings.
    if constexpr (std::constructible_from<TrackState, const SequenceProgram&, const TrackProgram&>) {
      return TrackState{program, track};
    } else if constexpr (std::constructible_from<TrackState, const SequenceProgram&>) {
      return TrackState{program};
    } else if constexpr (std::constructible_from<TrackState, const TrackProgram&>) {
      return TrackState{track};
    } else {
      return TrackState{};
    }
  }

  template <class Execute>
  [[nodiscard]] static Effects withPlayback(std::any& programState, std::any& trackState, PerformanceEmitter& out,
                                            VmApi& vm, Execute execute) {
    auto& typedProgramState = std::any_cast<ProgramState&>(programState);
    auto& typedTrackState = std::any_cast<TrackState&>(trackState);
    // Playback may ask for song-wide state as a fourth reference. Simpler
    // formats continue to use the original three-reference form.
    if constexpr (requires { Playback{typedTrackState, out, vm, typedProgramState}; }) {
      Playback playback{typedTrackState, out, vm, typedProgramState};
      return execute(playback);
    } else {
      Playback playback{typedTrackState, out, vm};
      return execute(playback);
    }
  }

  [[nodiscard]] static Effects execute(const SourceCommand& command, std::any& programState, std::any& trackState,
                                       PerformanceEmitter& out, VmApi& vm) {
    return withPlayback(programState, trackState, out, vm, [&](Playback& playback) {
      // Formats use this optional hook for information that must be emitted
      // before whichever command happens to be first.
      if constexpr (requires { playback.beforeCommand(); }) {
        playback.beforeCommand();
      }
      Effects combined;
      for (const CommandAction& action : command.execution.actions) {
        const Effects next = detail::compiledExecutors<Playback>().execute(action.executor, action.arguments, playback);
        if (next.advanceTicks > std::numeric_limits<u32>::max() - combined.advanceTicks) {
          throw std::overflow_error("Compiled sequence command advanced time beyond the supported range");
        }
        combined.advanceTicks += next.advanceTicks;
        if (next.step.kind != StepKind::Next) {
          if (combined.step.kind != StepKind::Next) {
            throw std::logic_error("Compiled sequence command produced more than one control-flow result");
          }
          combined.step = next.step;
        }
      }

      if (command.flow.terminal) {
        if (combined.step.kind != StepKind::Next) {
          throw std::logic_error("Terminal compiled sequence command also produced a control-flow result");
        }
        combined.step = vm.end();
      }
      return combined;
    });
  }

  static void tick(const SourceCommand&, std::any& programState, std::any& trackState, PerformanceEmitter& out,
                   VmApi& vm) {
    // Rebuild the lightweight Playback view for each elapsed tick so active
    // fades can use the current emitter and VM position without storing either.
    static_cast<void>(withPlayback(programState, trackState, out, vm, [](Playback& playback) {
      if constexpr (requires { playback.tick(); }) {
        playback.tick();
      }
      return Effects{};
    }));
  }

  static void endTrackTick(const SourceCommand&, std::any& programState,
                           std::any& trackState, PerformanceEmitter& out,
                           VmApi& vm, u64 tick) {
    static_cast<void>(withPlayback(programState, trackState, out, vm,
                                  [tick](Playback& playback) {
      if constexpr (requires { playback.endTick(tick); }) {
        playback.endTick(tick);
      }
      return Effects{};
    }));
  }

  static void finishPrepass(std::any& programState,
                            const PerformanceSequence& performance) {
    auto& typedProgramState = std::any_cast<ProgramState&>(programState);
    // Give the format one clear boundary between silent collection and the
    // real render. Collected results remain in the same typed object.
    if constexpr (requires { typedProgramState.finishPrepass(performance); }) {
      typedProgramState.finishPrepass(performance);
    } else if constexpr (requires { typedProgramState.finishPrepass(); }) {
      typedProgramState.finishPrepass();
    }
  }

  static void beginTrackSection(std::any& trackState,
                                std::optional<u64> sourceStop) {
    auto& typedTrackState = std::any_cast<TrackState&>(trackState);
    if constexpr (requires {
                    typedTrackState.beginSection(sourceStop);
                  }) {
      typedTrackState.beginSection(sourceStop);
    } else if constexpr (requires { typedTrackState.beginSection(); }) {
      typedTrackState.beginSection();
    }
  }

  static std::optional<u64> trackSectionSourceStop(
      const std::any& trackState) {
    const auto& typedTrackState = std::any_cast<const TrackState&>(trackState);
    if constexpr (requires { typedTrackState.sectionSourceStop(); }) {
      return typedTrackState.sectionSourceStop();
    }
    return std::nullopt;
  }

  static void finalizePerformance(std::any& programState, PerformanceSequence& performance) {
    auto& typedProgramState = std::any_cast<ProgramState&>(programState);
    if constexpr (requires { typedProgramState.finalizePerformance(performance); }) {
      typedProgramState.finalizePerformance(performance);
    }
  }

  static void reconcileTrackAfterTrim(std::any& trackState,
                                      const PerformanceTrack& performance, u64 endTick) {
    auto& typedTrackState = std::any_cast<TrackState&>(trackState);
    if constexpr (requires { typedTrackState.reconcileAfterTrim(performance, endTick); }) {
      typedTrackState.reconcileAfterTrim(performance, endTick);
    }
  }
};

// Fill the mechanical executor hooks while leaving identity, timebase, and
// playback defaults visible in the format's ordinary SequenceDialect value.
template <class TrackState, class Playback, class ProgramState = EmptyCompiledProgramState>
[[nodiscard]] SequenceDialect makeCompiledDialect(SequenceDialect dialect) {
  using Compiled = CompiledCommandDialect<TrackState, Playback, ProgramState>;
  dialect.createProgramState = Compiled::createProgramState;
  dialect.createTrackState = Compiled::createTrackState;
  dialect.execute = Compiled::execute;
  if constexpr (requires(Playback& playback) { playback.tick(); }) {
    dialect.tick = Compiled::tick;
  }
  if constexpr (requires(Playback& playback, u64 tick) {
                  playback.endTick(tick);
                }) {
    dialect.endTrackTick = Compiled::endTrackTick;
  }
  if constexpr (requires(ProgramState& state, const PerformanceSequence& performance) {
                  state.finishPrepass(performance);
                } ||
                requires(ProgramState& state) { state.finishPrepass(); }) {
    dialect.finishPrepass = Compiled::finishPrepass;
  }
  if constexpr (requires(TrackState& state,
                         std::optional<u64> sourceStop) {
                  state.beginSection(sourceStop);
                } ||
                requires(TrackState& state) { state.beginSection(); }) {
    dialect.beginTrackSection = Compiled::beginTrackSection;
  }
  if constexpr (requires(const TrackState& state) {
                  state.sectionSourceStop();
                }) {
    dialect.trackSectionSourceStop = Compiled::trackSectionSourceStop;
  }
  if constexpr (requires(ProgramState& state, PerformanceSequence& performance) {
                  state.finalizePerformance(performance);
                }) {
    dialect.finalizePerformance = Compiled::finalizePerformance;
  }
  if constexpr (requires(TrackState& state, const PerformanceTrack& performance,
                         u64 endTick) {
                  state.reconcileAfterTrim(performance, endTick);
                }) {
    dialect.reconcileTrackAfterTrim = Compiled::reconcileTrackAfterTrim;
  }
  return dialect;
}

// Execute a compiled program and project its final typed song state into a
// durable value. This is intended for sequence-defined synth preparation and
// similar analysis that must share playback's calls, repeats, and timing. The
// format-facing projector remains fully typed; only this adapter touches any.
template <class ProgramState, class Result>
[[nodiscard]] Result analyzeCompiledProgram(const SequenceProgram& program, const SequenceDialect& dialect,
                                            Result (*project)(const ProgramState&),
                                            SequenceVmOptions options = {}) {
  struct Inspection {
    Result* result = nullptr;
    Result (*project)(const ProgramState&) = nullptr;
  };

  Result result;
  Inspection inspection{.result = &result, .project = project};
  const auto inspect = [](const void* erasedState, void* erasedInspection) {
    const auto& state = *static_cast<const std::any*>(erasedState);
    auto& destination = *static_cast<Inspection*>(erasedInspection);
    *destination.result = destination.project(std::any_cast<const ProgramState&>(state));
  };
  static_cast<void>(SequenceVm(options).render(program, dialect, inspect, &inspection));
  return result;
}

}  // namespace vgmtrans::core
