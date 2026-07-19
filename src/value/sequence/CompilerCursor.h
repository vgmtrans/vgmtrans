/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/RecordReader.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
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

template <class Playback>
using CompiledExecutor = Effects (*)(std::span<const SemanticOperandValue>, Playback&);

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

template <class... Arguments>
void requireArgumentCount(std::span<const SemanticOperandValue> arguments) {
  if (arguments.size() != sizeof...(Arguments)) {
    throw std::logic_error("Compiled sequence command had the wrong argument count");
  }
}

template <class Playback, auto Method, class... Arguments, size_t... Index>
[[nodiscard]] Effects invokeMember(std::span<const SemanticOperandValue> arguments, Playback& playback,
                                   std::index_sequence<Index...>) {
  requireArgumentCount<Arguments...>(arguments);
  if constexpr (std::is_same_v<std::invoke_result_t<decltype(Method), Playback&, Arguments...>, Effects>) {
    return std::invoke(Method, playback, executableArgument<Arguments>(arguments[Index])...);
  } else {
    std::invoke(Method, playback, executableArgument<Arguments>(arguments[Index])...);
    return Effects{};
  }
}

template <class Playback, auto Method, class... Arguments>
[[nodiscard]] Effects invokeMember(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  return invokeMember<Playback, Method, Arguments...>(arguments, playback, std::index_sequence_for<Arguments...>{});
}

template <class Playback, class Handler, class... Arguments, size_t... Index>
[[nodiscard]] Effects invokeInline(std::span<const SemanticOperandValue> arguments, Playback& playback,
                                   std::index_sequence<Index...>) {
  static_assert(std::is_empty_v<Handler> && std::is_default_constructible_v<Handler>,
                "Inline compiler-cursor handlers must be captureless");
  requireArgumentCount<Arguments...>(arguments);
  Handler handler;
  if constexpr (std::is_same_v<std::invoke_result_t<Handler&, Playback&, Arguments...>, Effects>) {
    return std::invoke(handler, playback, executableArgument<Arguments>(arguments[Index])...);
  } else {
    std::invoke(handler, playback, executableArgument<Arguments>(arguments[Index])...);
    return Effects{};
  }
}

template <class Playback, class Handler, class... Arguments>
[[nodiscard]] Effects invokeInline(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  return invokeInline<Playback, Handler, Arguments...>(arguments, playback, std::index_sequence_for<Arguments...>{});
}

template <class Playback, auto Member>
[[nodiscard]] Effects setMember(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  using Value = std::remove_cvref_t<decltype(playback.track.*Member)>;
  requireArgumentCount<Value>(arguments);
  playback.track.*Member = executableArgument<Value>(arguments[0]);
  return Effects{};
}

template <class Playback, auto Member>
[[nodiscard]] Effects addMember(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  using Value = std::remove_cvref_t<decltype(playback.track.*Member)>;
  requireArgumentCount<Value>(arguments);
  playback.track.*Member += executableArgument<Value>(arguments[0]);
  return Effects{};
}

template <class Playback, auto Member>
[[nodiscard]] Effects toggleMember(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<>(arguments);
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(playback.track.*Member)>, bool>);
  playback.track.*Member = !(playback.track.*Member);
  return Effects{};
}

template <class Playback>
[[nodiscard]] Effects emitLevel(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<double>(arguments);
  playback.out.level(executableArgument<double>(arguments[0]));
  return Effects{};
}

template <class Playback>
[[nodiscard]] Effects emitExpression(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<double>(arguments);
  playback.out.expression(executableArgument<double>(arguments[0]));
  return Effects{};
}

template <class Playback>
[[nodiscard]] Effects emitPan(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<double>(arguments);
  playback.out.pan(executableArgument<double>(arguments[0]));
  return Effects{};
}

template <class Playback>
[[nodiscard]] Effects emitStereoBalance(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<double, double>(arguments);
  playback.out.stereoBalance(executableArgument<double>(arguments[0]), executableArgument<double>(arguments[1]));
  return Effects{};
}

template <class Playback>
[[nodiscard]] Effects emitInstrument(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<u32, u32>(arguments);
  playback.out.instrument(executableArgument<u32>(arguments[0]), executableArgument<u32>(arguments[1]));
  return Effects{};
}

template <class Playback>
[[nodiscard]] Effects emitTempo(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<u32>(arguments);
  playback.out.tempo(executableArgument<u32>(arguments[0]));
  return Effects{};
}

template <class Playback>
[[nodiscard]] Effects emitPitchBend(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<double>(arguments);
  playback.out.pitchBend(executableArgument<double>(arguments[0]));
  return Effects{};
}

template <class Playback, auto ScaleMember>
[[nodiscard]] Effects emitPitchBendScaledBy(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<double>(arguments);
  const double fraction = executableArgument<double>(arguments[0]);
  playback.out.pitchBend(fraction * static_cast<double>(playback.track.*ScaleMember));
  return Effects{};
}

template <class Playback>
[[nodiscard]] Effects emitPitchBendRange(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<u8>(arguments);
  playback.out.pitchBendRange(executableArgument<u8>(arguments[0]));
  return Effects{};
}

template <class Playback>
[[nodiscard]] Effects emitModulation(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<ModulationPerformanceTarget, double>(arguments);
  playback.out.modulation(executableArgument<ModulationPerformanceTarget>(arguments[0]),
                          executableArgument<double>(arguments[1]));
  return Effects{};
}

template <class Playback>
[[nodiscard]] Effects emitPortamentoEnable(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<bool>(arguments);
  playback.out.portamentoEnable(executableArgument<bool>(arguments[0]));
  return Effects{};
}

template <class Playback>
[[nodiscard]] Effects emitPortamentoTime(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<double>(arguments);
  playback.out.portamentoTime(executableArgument<double>(arguments[0]));
  return Effects{};
}

template <class Playback>
[[nodiscard]] Effects wait(std::span<const SemanticOperandValue> arguments, Playback&) {
  requireArgumentCount<u32>(arguments);
  return Effects::wait(executableArgument<u32>(arguments[0]));
}

template <class Playback>
[[nodiscard]] Effects jump(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<Address>(arguments);
  return Effects{.step = playback.vm.jump(executableArgument<Address>(arguments[0]))};
}

template <class Playback>
[[nodiscard]] Effects loopCandidate(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<Address>(arguments);
  return Effects{.step = playback.vm.loopCandidate(executableArgument<Address>(arguments[0]))};
}

template <class Playback>
[[nodiscard]] Effects declaredLoop(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<Address>(arguments);
  return Effects{.step = playback.vm.declaredLoop(executableArgument<Address>(arguments[0]))};
}

template <class Playback>
[[nodiscard]] Effects call(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<Address>(arguments);
  return Effects{.step = playback.vm.call(executableArgument<Address>(arguments[0]))};
}

template <class Playback>
[[nodiscard]] Effects return_(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<>(arguments);
  return Effects{.step = playback.vm.return_()};
}

template <class Playback>
[[nodiscard]] Effects repeatUntil(std::span<const SemanticOperandValue> arguments, Playback& playback) {
  requireArgumentCount<u8, u32, Address>(arguments);
  return playback.vm.countedRepeatUntil(executableArgument<u8>(arguments[0]), executableArgument<u32>(arguments[1]),
                                        executableArgument<Address>(arguments[2]));
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

    ::s8 s8(std::string_view name, SourceValueDisplay display = SourceValueDisplay::SignedDecimal,
            SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.s8(name, display), name, display, role);
    }

    u16 u16be(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
              SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.u16be(name, display), name, display, role);
    }

    u16 u16le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
              SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.u16le(name, display), name, display, role);
    }

    u32 u24le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
              SemanticOperandRole role = SemanticOperandRole::Value) {
      return cursor_.decoded(cursor_.record_.u24le(name, display), name, display, role);
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

    [[nodiscard]] Address address(std::string_view name, SemanticOperandRole role = SemanticOperandRole::Address) {
      return Address{u16be(name, SourceValueDisplay::Address, role)};
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

    void warning(std::string message) { cursor_.warning(std::move(message)); }

    // Operations accumulate in source order and return the same builder. A
    // return statement converts the final Event expression into the decoded
    // command, so callers may freely mix chained and standalone calls.
    Event& ignore() {
      execution_ = {};
      flow_ = {};
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

    Event& wait(u32 ticks) { return append(&detail::wait<Playback>, ticks); }

    Event& emitLevel(double gain) { return append(&detail::emitLevel<Playback>, gain); }

    Event& emitExpression(double gain) { return append(&detail::emitExpression<Playback>, gain); }

    Event& emitPan(double position) { return append(&detail::emitPan<Playback>, position); }

    Event& emitStereoBalance(double leftGain, double rightGain) {
      return append(&detail::emitStereoBalance<Playback>, leftGain, rightGain);
    }

    Event& emitInstrument(u32 bank, u32 program) { return append(&detail::emitInstrument<Playback>, bank, program); }

    Event& emitTempo(u32 microsecondsPerQuarter) {
      return append(&detail::emitTempo<Playback>, microsecondsPerQuarter);
    }

    Event& emitPitchBend(double semitones) { return append(&detail::emitPitchBend<Playback>, semitones); }

    template <auto ScaleMember>
    Event& emitPitchBendScaledBy(double fraction) {
      return append(&detail::emitPitchBendScaledBy<Playback, ScaleMember>, fraction);
    }

    Event& emitPitchBendRange(::u8 semitones) { return append(&detail::emitPitchBendRange<Playback>, semitones); }

    Event& emitModulation(ModulationPerformanceTarget target, double amount) {
      return append(&detail::emitModulation<Playback>, target, amount);
    }

    Event& emitPortamentoEnable(bool enabled) { return append(&detail::emitPortamentoEnable<Playback>, enabled); }

    Event& emitPortamentoTime(double milliseconds) {
      return append(&detail::emitPortamentoTime<Playback>, milliseconds);
    }

    template <auto Member, class Value>
    Event& set(Value value) {
      return append(&detail::setMember<Playback, Member>, value);
    }

    template <auto Member, class Value>
    Event& add(Value value) {
      return append(&detail::addMember<Playback, Member>, value);
    }

    template <auto Member>
    Event& toggle() {
      return append(&detail::toggleMember<Playback, Member>);
    }

    template <auto Method, class... Arguments>
    Event& invoke(Arguments... arguments) {
      return append(&detail::invokeMember<Playback, Method, std::decay_t<Arguments>...>, arguments...);
    }

    // A captureless inline handler is the locality escape hatch for short,
    // one-off runtime behavior. Source values remain explicit positional
    // arguments; no closure object enters the durable command.
    template <class Handler, class... Arguments>
    Event& invoke(Handler, Arguments... arguments) {
      return append(&detail::invokeInline<Playback, std::decay_t<Handler>, std::decay_t<Arguments>...>, arguments...);
    }

    Event& jump(Address destination) {
      targetRole(destination, SemanticOperandRole::JumpTarget);
      append(&detail::jump<Playback>, destination);
      flow_ = DecodeFlow::jump(destination);
      return *this;
    }

    Event& loopCandidate(Address destination) {
      targetRole(destination, SemanticOperandRole::LoopTarget);
      append(&detail::loopCandidate<Playback>, destination);
      flow_ = DecodeFlow::jump(destination);
      return *this;
    }

    Event& declaredLoop(Address destination) {
      targetRole(destination, SemanticOperandRole::LoopTarget);
      append(&detail::declaredLoop<Playback>, destination);
      flow_ = DecodeFlow::jump(destination);
      return *this;
    }

    Event& call(Address destination) {
      targetRole(destination, SemanticOperandRole::CallTarget);
      append(&detail::call<Playback>, destination);
      flow_ = DecodeFlow::call(destination, Address{cursor_.record_.position()});
      return *this;
    }

    Event& return_() {
      append(&detail::return_<Playback>);
      flow_ = DecodeFlow::return_();
      return *this;
    }

    Event& repeatUntil(::u8 slot, u32 totalPlays, Address destination) {
      targetRole(destination, SemanticOperandRole::RepeatTarget);
      append(&detail::repeatUntil<Playback>, slot, totalPlays, destination);
      flow_.staticTargets.push_back(destination);
      return *this;
    }

    [[nodiscard]] operator DecodedBytecodeCommand() { return finish(); }

  private:
    friend class CompilerCursor;

    Event(CompilerCursor& cursor, DecodedCommandPresentation presentation)
        : cursor_(cursor), presentation_(std::move(presentation)) {}

    template <class... Arguments>
    Event& append(detail::CompiledExecutor<Playback> executor, Arguments... arguments) {
      CommandAction action{
          .executor = detail::compiledExecutors<Playback>().add(executor),
      };
      action.arguments.reserve(sizeof...(Arguments));
      (action.arguments.push_back(detail::executableValue(arguments)), ...);
      execution_.actions.push_back(std::move(action));
      return *this;
    }

    void targetRole(Address destination, SemanticOperandRole role) {
      const auto found = std::ranges::find_if(cursor_.operands_.rbegin(), cursor_.operands_.rend(),
                                              [destination](const SemanticOperand& operand) {
                                                const auto* address = std::get_if<Address>(&operand.value);
                                                return address != nullptr && address->value == destination.value;
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
    CommandExecution execution_;
    DecodeFlow flow_;
    bool finished_ = false;
  };

  CompilerCursor(ByteReader reader, u32 begin, u32 end, std::string_view detailKindPrefix,
                 std::vector<Diagnostic>* diagnostics = nullptr)
      : record_(reader, begin, end, diagnostics), detailKindPrefix_(detailKindPrefix), diagnostics_(diagnostics) {
    const auto opcode = record_.u8("opcode", SourceValueDisplay::Hex);
    if (opcode) {
      opcode_ = *opcode;
      opcodeRange_ = opcode.range;
    }
  }

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

  [[nodiscard]] DecodedBytecodeCommand opaque(std::string_view label, u32 operandBytes,
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
        .bytes = truncated ? std::vector<::u8>{record_.bytes().begin(), record_.bytes().end()} : std::vector<::u8>{},
        .flow = std::move(flow),
        .operands = std::move(operands_),
        .execution = std::move(execution),
        .presentation = std::move(presentation),
        // Partial bytes are diagnostic source data only. No compiled executor
        // receives them, and every valid command remains completely source-free.
        .retainBytes = truncated,
        .truncated = truncated,
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
template <class TrackState, class Playback>
struct CompiledCommandDialect {
  [[nodiscard]] static std::any createTrackState(const SequenceProgram&, const TrackProgram&) { return TrackState{}; }

  [[nodiscard]] static Effects execute(const SourceCommand& command, std::any&, std::any& trackState,
                                       PerformanceEmitter& out, VmApi& vm) {
    auto& typedTrackState = std::any_cast<TrackState&>(trackState);
    Playback playback{typedTrackState, out, vm};
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
  }
};

template <class DecodeCommand>
[[nodiscard]] TrackProgram decodeCompilerLinearTrack(ByteReader reader, TrackDecodeInput input,
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

template <class DecodeCommand>
[[nodiscard]] TrackProgram decodeCompilerReachableTrack(ByteReader reader, TrackDecodeInput input,
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
