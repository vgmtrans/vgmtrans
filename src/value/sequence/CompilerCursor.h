/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/RecordReader.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CompiledCommandRuntime.h"

#include <algorithm>
#include <functional>
#include <limits>
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
[[nodiscard]] SourceValue semanticValue(T value) {
  if constexpr (std::is_same_v<T, Address>) {
    return makeSourceValue(value.value);
  } else if constexpr (std::is_enum_v<T>) {
    return makeSourceValue(static_cast<std::underlying_type_t<T>>(value));
  } else if constexpr (std::is_same_v<T, const char*> || std::is_same_v<T, char*>) {
    return makeSourceValue(std::string_view(value));
  } else {
    return makeSourceValue(std::move(value));
  }
}

template <class T>
[[nodiscard]] auto storedCommandValue(T value) {
  if constexpr (std::is_same_v<std::remove_cvref_t<T>, std::string_view>) {
    return std::string(value);
  } else {
    return std::remove_cvref_t<T>(std::move(value));
  }
}

// Playback methods supply the types for a braced list of already decoded values.
// References are copied; makeCommandBody also owns string_view arguments.
// Require every argument: a bare tuple would accept {} and value-initialize its elements.
template <class... Arguments>
struct CommandArguments {
  template <class... Values>
    requires(sizeof...(Values) == sizeof...(Arguments) &&
             std::is_constructible_v<std::tuple<Arguments...>, Values...>)
  CommandArguments(Values&&... arguments) : values(std::forward<Values>(arguments)...) {}

  std::tuple<Arguments...> values;
};

template <class Result, class Owner, class... Arguments>
auto commandArguments(Result (Owner::*)(Arguments...)) -> CommandArguments<std::decay_t<Arguments>...>;

template <class Result, class Owner, class... Arguments>
auto commandArguments(Result (Owner::*)(Arguments...) const) -> CommandArguments<std::decay_t<Arguments>...>;

template <class Playback, class... Arguments>
auto handlerArguments(CommandArguments<Playback, Arguments...>) -> CommandArguments<Arguments...>;

template <class Handler>
using HandlerArguments = decltype(handlerArguments(commandArguments(&Handler::operator())));

template <class Playback, class Callable, class... Arguments>
using CommandResult = std::invoke_result_t<
    const Callable&, Playback&, const decltype(storedCommandValue(std::declval<Arguments>()))&...>;

template <class Playback, class Callable, class... Arguments>
[[nodiscard]] CommandBody makeCommandBody(Callable callable, Arguments... arguments) {
  using Result = CommandResult<Playback, Callable, Arguments...>;
  static_assert(std::is_copy_constructible_v<Callable>, "Compiled sequence command callables must be copyable");
  static_assert(std::is_same_v<Result, void> || std::is_same_v<Result, Effects>,
                "A compiled sequence command body must return void or Effects");
  return [callable = std::move(callable), ... values = storedCommandValue(std::move(arguments))](
             void* erasedPlayback) -> Effects {
    auto& playback = *static_cast<Playback*>(erasedPlayback);
    if constexpr (std::is_same_v<Result, Effects>) {
      return std::invoke(callable, playback, values...);
    } else {
      std::invoke(callable, playback, values...);
      return Effects{};
    }
  };
}

[[nodiscard]] inline Effects combineCommandEffects(Effects first, Effects second) {
  if (second.advanceTicks > std::numeric_limits<u32>::max() - first.advanceTicks) {
    throw std::overflow_error("Compiled sequence command advanced time beyond the supported range");
  }
  first.advanceTicks += second.advanceTicks;
  if (second.flowOverride) {
    if (first.flowOverride) {
      throw std::logic_error("Compiled sequence command produced more than one control-flow override");
    }
    first.flowOverride = second.flowOverride;
  }
  return first;
}

template <class Playback, EnvelopeFields Field>
void emitEnvelopeField(Playback& playback, double value, VoiceEnvelopeScope scope) {
  constexpr auto entry = std::ranges::find_if(envelopeFields, [](const auto& field) { return field.first == Field; });
  static_assert(entry != envelopeFields.end(), "A compiled envelope field must name one stage");
  Envelope envelope;
  envelope.*entry->second = value;
  playback.out.updateEnvelope(std::move(envelope), Field, scope);
}

}  // namespace detail

// CompilerCursor gives formats one imperative command block. Reads add source
// metadata immediately; event operations compose one typed executable body for
// later, source-free SequenceVm execution.
//
// Independent fields can be read directly in a braced argument list:
//   event.invoke<&Playback::note>({cursor.u8("key"), cursor.varLen("duration")});
// A single read can also be transformed inline:
//   event.emitPan(cursor.u8("pan") / 63.5 - 1.0);
// Keep locals for validation, reuse, dependent reads, and reordered arguments:
//   const u32 duration = cursor.varLen("duration");  // Encoded before the key.
//   const u8 key = cursor.u8("key");
//   if (duration == 0) return event.ignore();
//   return event.invoke<&Playback::note>({key, duration});
// Braces order their elements, not multiple reads inside a single element:
// split expressions such as cursor.u8("a") + cursor.u8("b") into local reads.
template <class PlaybackType>
class CompilerCursor {
public:
  using Playback = PlaybackType;

  class Event {
  public:
    // Changes the label for the decoded command.
    Event& label(std::string_view label) {
      presentation_.label = label;
      return *this;
    }

    // Operations accumulate in source order and return the same builder. A
    // return statement converts the final Event expression into the decoded
    // command, so callers may freely mix chained and standalone calls.
    Event& ignore() {
      execution_ = {};
      defaultTransition_.reset();
      discoveryTargets_.clear();
      presentation_.playback = initialPlayback_;
      return *this;
    }

    Event& stop() {
      setDefaultTransition(CommandTransition::end());
      return *this;
    }

    Event& end() {
      presentation_.semantic = SequenceSemantic::End;
      presentation_.playback = CommandPlaybackStatus::StopsPlayback;
      setDefaultTransition(CommandTransition::end());
      return *this;
    }

    Event& wait(u32 ticks) {
      return appendCallable([ticks](Playback&) { return Effects::wait(ticks); });
    }

    Event& delay(u32 ticks) {
      execution_.delayTicks = ticks;
      return *this;
    }

    // Route only this command's musical effects. Channel-tagged loop commands
    // can keep their source annotation without changing the execution destination.
    Event& channel(u32 number) {
      execution_.channel = number;
      return *this;
    }

    Event& synchronizedLoopStart() {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      execution_.coordinatorSignal = SequenceCoordinatorSignal::SynchronizedLoopStart;
      return *this;
    }

    Event& synchronizedLoopEnd() {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      execution_.coordinatorSignal = SequenceCoordinatorSignal::SynchronizedLoopEnd;
      return *this;
    }

    template <auto Member>
    Event& wait() {
      return appendCallable([](Playback& playback) {
        return Effects::wait(static_cast<u32>(playback.track.*Member));
      });
    }

    Event& emitLevel(double gain, ValueQuantization quantization = {}) {
      return appendCallable([=](Playback& playback) { playback.out.level(gain, quantization); });
    }

    Event& emitExpression(double gain) {
      return appendCallable([=](Playback& playback) { playback.out.expression(gain); });
    }

    Event& emitPan(double position) {
      return appendCallable([=](Playback& playback) { playback.out.pan(position); });
    }

    Event& emitStereoBalance(double leftGain, double rightGain) {
      return appendCallable([=](Playback& playback) { playback.out.stereoBalance(leftGain, rightGain); });
    }

    Event& emitInstrument(u32 bank, u32 program,
                          InstrumentEnvelopeMode envelopeMode = InstrumentEnvelopeMode::UseInstrumentEnvelope) {
      return appendCallable([=](Playback& playback) { playback.out.instrument(bank, program, envelopeMode); });
    }

    Event& emitInstrument(std::string_view domain, u32 key,
                          InstrumentEnvelopeMode envelopeMode = InstrumentEnvelopeMode::UseInstrumentEnvelope) {
      return appendCallable([identity = InstrumentIdentity{.domain = std::string(domain), .key = key},
                             envelopeMode](Playback& playback) { playback.out.instrument(identity, envelopeMode); });
    }

    Event& emitTempo(u32 microsecondsPerQuarter) {
      return appendCallable([=](Playback& playback) { playback.out.tempo(microsecondsPerQuarter); });
    }

    Event& emitMasterLevel(double gain) {
      return appendCallable([=](Playback& playback) { playback.out.masterLevel(gain); });
    }

    Event& emitReverb(double send) {
      return appendCallable([=](Playback& playback) { playback.out.reverb(send); });
    }

    Event& emitTuning(double cents) {
      return appendCallable([=](Playback& playback) { playback.out.tuning(cents); });
    }

    template <EnvelopeFields Field>
    Event& emitEnvelopeField(double value, VoiceEnvelopeScope scope = VoiceEnvelopeScope::FutureAttacks) {
      return appendCallable(&detail::emitEnvelopeField<Playback, Field>, std::move(value), scope);
    }

    Event& restoreEnvelope(EnvelopeFields fields = EnvelopeFields::All,
                           VoiceEnvelopeScope scope = VoiceEnvelopeScope::FutureAttacks) {
      return appendCallable([=](Playback& playback) { playback.out.restoreEnvelope(fields, scope); });
    }

    Event& emitGlobalTranspose(s32 semitones) {
      return appendCallable([=](Playback& playback) { playback.out.globalTranspose(semitones); });
    }

    Event& emitLegatoPedal(bool enabled) {
      return appendCallable([=](Playback& playback) { playback.out.legatoPedal(enabled); });
    }

    Event& emitPitchBend(double semitones) {
      return appendCallable([=](Playback& playback) { playback.out.pitchBend(semitones); });
    }

    Event& emitPitchBendRange(::u8 semitones) {
      return appendCallable([=](Playback& playback) { playback.out.pitchBendRange(semitones); });
    }

    template <auto Member, class Value>
    Event& set(Value value) {
      return appendCallable(
          [](Playback& playback, const auto& argument) {
            using MemberValue = std::remove_cvref_t<decltype(playback.track.*Member)>;
            playback.track.*Member = static_cast<MemberValue>(argument);
          },
          std::move(value));
    }

    template <auto Member, class Value>
    Event& add(Value value) {
      return appendCallable(
          [](Playback& playback, const auto& argument) {
            using MemberValue = std::remove_cvref_t<decltype(playback.track.*Member)>;
            playback.track.*Member += static_cast<MemberValue>(argument);
          },
          std::move(value));
    }

    template <auto Member>
    Event& toggle() {
      return appendCallable([](Playback& playback) {
        static_assert(std::is_same_v<std::remove_cvref_t<decltype(playback.track.*Member)>, bool>);
        playback.track.*Member = !(playback.track.*Member);
      });
    }

    // Braces sequence source reads before binding. There is deliberately no
    // loose argument-pack overload: invoke({read1(), read2()}) preserves order.
    template <auto Method>
    Event& invoke(decltype(detail::commandArguments(Method)) arguments) {
      return appendInvocation<false>(Method, std::move(arguments));
    }

    template <auto Method>
    Event& invoke() {
      return appendCallable(Method);
    }

    // Captures must own anything that needs to outlive decoding. Explicit
    // arguments are copied into the command body using the same rules as methods.
    template <class Handler>
    Event& invoke(Handler handler, detail::HandlerArguments<Handler> arguments) {
      return appendInvocation<false>(std::move(handler), std::move(arguments));
    }

    template <class Handler>
    Event& invoke(Handler handler) {
      return appendCallable(std::move(handler));
    }

    // Runtime control flow may override the command's decoded default path.
    template <auto Method>
    Event& invokeFlow(decltype(detail::commandArguments(Method)) arguments) {
      return appendInvocation<true>(Method, std::move(arguments));
    }

    template <auto Method>
    Event& invokeFlow() {
      return appendInvocation<true>(Method, detail::CommandArguments<>{});
    }

    template <class Handler>
    Event& invokeFlow(Handler handler, detail::HandlerArguments<Handler> arguments) {
      return appendInvocation<true>(std::move(handler), std::move(arguments));
    }

    template <class Handler>
    Event& invokeFlow(Handler handler) {
      return appendInvocation<true>(std::move(handler), detail::CommandArguments<>{});
    }

    // The VM may execute this command while the preceding command's wait is
    // still active. It polls at most once when the wait begins and once per
    // nonfinal wait tick, and executes the command only when Predicate is true.
    template <auto Predicate>
    Event& duringWaitWhen() {
      static_assert(std::is_same_v<std::invoke_result_t<decltype(Predicate), Playback&>, bool>,
                    "A during-wait predicate must return bool");
      if (execution_.duringWait) {
        throw std::logic_error("Compiled sequence command declared more than one during-wait predicate");
      }
      execution_.duringWait = [](void* erasedPlayback) {
        return std::invoke(Predicate, *static_cast<Playback*>(erasedPlayback));
      };
      return *this;
    }

    Event& jump(Address destination) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      setDefaultTransition(CommandTransition::jump(destination));
      return *this;
    }

    Event& finiteBranch(Address destination) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      setDefaultTransition(CommandTransition::jump(destination, JumpSemantics::FiniteBranch));
      return *this;
    }

    Event& loopCandidate(Address destination) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      setDefaultTransition(CommandTransition::jump(destination, JumpSemantics::LoopCandidate));
      return *this;
    }

    Event& declaredLoop(Address destination) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      setDefaultTransition(CommandTransition::jump(destination, JumpSemantics::DeclaredLoop));
      return *this;
    }

    Event& call(Address destination) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      setDefaultTransition(CommandTransition::call(destination));
      return *this;
    }

    // Set the default return path for decoding and execution. A runtime body
    // may override it, for example when the same opcode ends playback at top level.
    Event& return_() {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      setDefaultTransition(CommandTransition::return_());
      return *this;
    }

    Event& repeatUntil(::u8 slot, u32 totalPlays, Address destination) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      appendCallable([=](Playback& playback) { return playback.vm.countedRepeatUntil(slot, totalPlays, destination); });
      discoveryTargets_.push_back(destination);
      return *this;
    }

    Event& repeatBreak(::u8 slot, Address destination) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      appendCallable([=](Playback& playback) { return playback.vm.countedRepeatBreak(slot, destination); });
      discoveryTargets_.push_back(destination);
      return *this;
    }

    // Decode an additional reachable block without changing the default path.
    // Conditional branches supply their runtime decision through invokeFlow().
    Event& discoverTarget(Address destination) {
      presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      discoveryTargets_.push_back(destination);
      return *this;
    }

    [[nodiscard]] operator DecodedBytecodeCommand() { return finish(); }

  private:
    friend class CompilerCursor;

    Event(CompilerCursor& cursor, DecodedCommandPresentation presentation)
        : cursor_(cursor), presentation_(std::move(presentation)), initialPlayback_(presentation_.playback) {}

    template <bool ControlFlow, class Callable, class... Arguments>
    Event& appendInvocation(Callable callable, detail::CommandArguments<Arguments...> arguments) {
      if constexpr (ControlFlow) {
        static_assert(std::is_same_v<detail::CommandResult<Playback, Callable, Arguments...>, Effects>,
                      "A runtime control-flow handler must return Effects");
        presentation_.playback = CommandPlaybackStatus::AffectsControlFlow;
      }
      return std::apply(
          [&](auto... values) -> Event& { return appendCallable(std::move(callable), std::move(values)...); },
          std::move(arguments.values));
    }

    template <class Callable, class... Arguments>
    Event& appendCallable(Callable callable, Arguments... arguments) {
      if (presentation_.playback == CommandPlaybackStatus::SourceOnly ||
          presentation_.playback == CommandPlaybackStatus::NoOp) {
        presentation_.playback = CommandPlaybackStatus::AffectsPlayback;
      }
      CommandBody next = detail::makeCommandBody<Playback>(std::move(callable), std::move(arguments)...);
      if (!execution_.body) {
        execution_.body = std::move(next);
        return *this;
      }
      CommandBody previous = std::move(execution_.body);
      execution_.body = [previous = std::move(previous), next = std::move(next)](void* playback) {
        Effects combined = previous(playback);
        return detail::combineCommandEffects(std::move(combined), next(playback));
      };
      return *this;
    }

    void setDefaultTransition(CommandTransition transition) {
      if (defaultTransition_) {
        throw std::logic_error("Compiled sequence command declared more than one default transition");
      }
      defaultTransition_ = transition;
    }

    [[nodiscard]] DecodedBytecodeCommand finish() {
      if (finished_) {
        throw std::logic_error("Compiler cursor event was finalized more than once");
      }
      finished_ = true;
      return cursor_.finish(std::move(presentation_), std::move(execution_),
                            defaultTransition_.value_or(CommandTransition{}), std::move(discoveryTargets_));
    }

    CompilerCursor& cursor_;
    DecodedCommandPresentation presentation_;
    CommandPlaybackStatus initialPlayback_;
    CommandExecution execution_;
    std::optional<CommandTransition> defaultTransition_;
    std::vector<Address> discoveryTargets_;
    bool finished_ = false;
  };

  CompilerCursor(ByteReader reader, u32 begin, u32 end, std::string_view kindPrefix,
                 std::vector<Diagnostic>* diagnostics = nullptr)
      : record_(reader, begin, end, diagnostics, false), kindPrefix_(kindPrefix), diagnostics_(diagnostics) {
    const auto opcode = record_.u8("opcode", SourceValueDisplay::Hex);
    if (opcode) {
      opcode_ = *opcode;
      opcodeRange_ = opcode.range;
    }
  }

  // Most extracted sequence sources use the complete byte buffer. Formats
  // with a meaningful subrange continue to pass an explicit end offset.
  CompilerCursor(ByteReader reader, u32 begin, std::string_view kindPrefix,
                 std::vector<Diagnostic>* diagnostics = nullptr)
      : CompilerCursor(reader, begin, static_cast<u32>(std::min<u64>(reader.size(), std::numeric_limits<u32>::max())),
                       kindPrefix, diagnostics) {}

  [[nodiscard]] bool hasOpcode() const noexcept { return opcodeRange_.size != 0; }
  [[nodiscard]] bool ok() const noexcept { return record_.ok(); }
  [[nodiscard]] ::u8 opcode() const noexcept { return opcode_; }

  // Reads consume bytes and record source metadata immediately. Keep dependent
  // reads in separate statements; braced invoke arguments are read left to right.
  ::u8 u8(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
          SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(record_.u8(name, display), name, display, role);
  }

  ::u8 u8(std::string_view name, SemanticOperandRole role) { return u8(name, SourceValueDisplay::Default, role); }

  [[nodiscard]] EncodedSemanticField<::u8> rawU8(std::string_view name,
                                                 SourceValueDisplay display = SourceValueDisplay::Default) {
    return {record_.u8(name, display), name, display};
  }

  ::s8 s8(std::string_view name, SourceValueDisplay display = SourceValueDisplay::SignedDecimal,
          SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(record_.s8(name, display), name, display, role);
  }

  ::s8 s8(std::string_view name, SemanticOperandRole role) { return s8(name, SourceValueDisplay::SignedDecimal, role); }

  [[nodiscard]] EncodedSemanticField<::s8> rawS8(std::string_view name,
                                                 SourceValueDisplay display = SourceValueDisplay::SignedDecimal) {
    return {record_.s8(name, display), name, display};
  }

  u16 u16be(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
            SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(record_.u16be(name, display), name, display, role);
  }

  [[nodiscard]] EncodedSemanticField<u16> rawU16be(std::string_view name,
                                                   SourceValueDisplay display = SourceValueDisplay::Default) {
    return {record_.u16be(name, display), name, display};
  }

  u16 u16le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
            SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(record_.u16le(name, display), name, display, role);
  }

  [[nodiscard]] EncodedSemanticField<u16> rawU16le(std::string_view name,
                                                   SourceValueDisplay display = SourceValueDisplay::Default) {
    return {record_.u16le(name, display), name, display};
  }

  s16 s16le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::SignedDecimal,
            SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(record_.s16le(name, display), name, display, role);
  }

  u32 u24le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
            SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(record_.u24le(name, display), name, display, role);
  }

  u32 u32be(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
            SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(record_.u32be(name, display), name, display, role);
  }

  [[nodiscard]] EncodedSemanticField<u32> rawU32be(std::string_view name,
                                                   SourceValueDisplay display = SourceValueDisplay::Default) {
    return {record_.u32be(name, display), name, display};
  }

  u32 u32le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
            SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(record_.u32le(name, display), name, display, role);
  }

  [[nodiscard]] EncodedSemanticField<u32> rawU32le(std::string_view name,
                                                   SourceValueDisplay display = SourceValueDisplay::Default) {
    return {record_.u32le(name, display), name, display};
  }

  u32 varLen(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
             SemanticOperandRole role = SemanticOperandRole::Value) {
    return decoded(record_.varLen(name, display), name, display, role);
  }

  u32 varLen(std::string_view name, SemanticOperandRole role) {
    return varLen(name, SourceValueDisplay::Default, role);
  }

  std::string rawBytes(std::string_view name, u32 size) {
    return decoded(record_.rawBytes(name, size), name, SourceValueDisplay::Hex, SemanticOperandRole::Value);
  }

  // Look ahead without consuming or annotating the byte. This is useful when
  // a command may have an optional suffix identified by its own opcode.
  [[nodiscard]] std::optional<::u8> peekU8() const { return record_.peekU8(); }

  // Some commands implicitly refer to the byte immediately after themselves,
  // such as a loop start with no encoded destination.
  [[nodiscard]] Address nextAddress() const { return Address{record_.position()}; }

  [[nodiscard]] Address address(std::string_view name, SemanticOperandRole role = SemanticOperandRole::Value) {
    return Address{u16be(name, SourceValueDisplay::Address, role)};
  }

  [[nodiscard]] Address addressLe(std::string_view name, SemanticOperandRole role = SemanticOperandRole::Value) {
    return Address{u16le(name, SourceValueDisplay::Address, role)};
  }

  template <::u8 Shift, ::u8 Width>
  ::u8 opcodeBits(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default,
                  SemanticOperandRole role = SemanticOperandRole::Value) {
    static_assert(Width > 0 && Width <= 8 && Shift + Width <= 8);
    constexpr u16 mask = (u16{1} << Width) - 1;
    const auto result = static_cast<::u8>((opcode_ >> Shift) & mask);
    opcodeValue(name, result, display, role);
    return result;
  }

  template <::u8 Shift, ::u8 Width>
  ::u8 opcodeBits(std::string_view name, SemanticOperandRole role) {
    return opcodeBits<Shift, Width>(name, SourceValueDisplay::Default, role);
  }

  template <class T>
  T opcodeValue(std::string_view name, T value, SourceValueDisplay display = SourceValueDisplay::Default,
                SemanticOperandRole role = SemanticOperandRole::Value) {
    add(name, detail::semanticValue(value), opcodeRange_, display, role);
    return value;
  }

  template <class T>
  T derived(std::string_view name, T value, SourceValueDisplay display = SourceValueDisplay::Default,
            SemanticOperandRole role = SemanticOperandRole::Value) {
    if (record_.ok()) {
      add(name, detail::semanticValue(value), {}, display, role);
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
  Resolved resolvedValue(std::string_view name, const EncodedSemanticField<T>& source, Resolved resolved,
                         SourceValueDisplay display = SourceValueDisplay::Default,
                         SemanticOperandRole role = SemanticOperandRole::Value) {
    if (source.valid) {
      if (!name.empty() && source.range.valid()) {
        add(source.name.empty() ? name : source.name, detail::semanticValue(source.value), source.range, source.display,
            SemanticOperandRole::Value);
      }
      add(name, detail::semanticValue(resolved), {}, display, role);
    }
    return resolved;
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

  [[nodiscard]] Event command(std::string_view label, SequenceSemantic semantic,
                              CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback,
                              std::string_view category = {}) {
    const std::string kind = category.empty() ? sourceKindFromLabel(label) : std::string(category);
    return Event{*this, DecodedCommandPresentation{
                            .label = std::string(label),
                            .kind = qualifiedKind(kind),
                            .semantic = semantic,
                            .playback = playback,
                        }};
  }

  [[nodiscard]] Event sourceOnly(std::string_view label, std::string_view category = {}) {
    return command(label, SequenceSemantic::Meta, CommandPlaybackStatus::SourceOnly, category);
  }

  [[nodiscard]] Event noOp(std::string_view label, std::string_view category = {}) {
    return command(label, SequenceSemantic::Meta, CommandPlaybackStatus::NoOp, category);
  }

  [[nodiscard]] Event unsupported(std::string_view label, std::string_view category = "unsupported") {
    return command(label, SequenceSemantic::Unsupported, CommandPlaybackStatus::Unsupported, category);
  }

  // Records the command and its raw operands for source inspection, but
  // compiles no playback behavior and continues to the next command.
  [[nodiscard]] DecodedBytecodeCommand ignored(std::string_view label, u32 operandBytes,
                                               std::string_view category = {}) {
    auto event = sourceOnly(label, category);
    rawBytes("bytes", operandBytes);
    return event;
  }

  [[nodiscard]] DecodedBytecodeCommand truncated() {
    return finish(truncatedPresentation(), {}, CommandTransition::end(), {});
  }

private:
  template <class T>
  T decoded(const RangedValue<T>& field, std::string_view name, SourceValueDisplay display, SemanticOperandRole role) {
    if (field) {
      add(name, detail::semanticValue(field.value), field.range, display, role);
    }
    return field.value;
  }

  void add(std::string_view name, SourceValue value, SourceRange range, SourceValueDisplay display,
           SemanticOperandRole role) {
    if (role != SemanticOperandRole::Value) {
      auto& operand = operands_.emplace_back();
      operand.value = value;
      operand.role = role;
    }
    if (!name.empty()) {
      fields_.push_back(SourceField{
          .name = std::string(name),
          .range = range.valid() ? range : SourceRange{},
          .value = std::move(value),
          .display = display,
      });
    }
  }


  [[nodiscard]] DecodedCommandPresentation truncatedPresentation() const {
    return DecodedCommandPresentation{
        .label = "Truncated Command",
        .kind = qualifiedKind("truncated"),
        .semantic = SequenceSemantic::Unsupported,
        .playback = CommandPlaybackStatus::Unsupported,
    };
  }

  [[nodiscard]] std::string qualifiedKind(std::string_view category) const {
    return kindPrefix_.empty() ? std::string(category) : kindPrefix_ + "." + std::string(category);
  }

  [[nodiscard]] DecodedBytecodeCommand finish(DecodedCommandPresentation presentation, CommandExecution execution,
                                              CommandTransition transition, std::vector<Address> discoveryTargets) {
    if (!record_.ok()) {
      presentation = truncatedPresentation();
      execution = {};
      transition = CommandTransition::end();
      discoveryTargets.clear();
    }

    return DecodedBytecodeCommand{
        .range = record_.range(),
        .opcode = opcode_,
        .flow = {.continuation = Address{record_.position()}, .defaultTransition = transition},
        .discoveryTargets = std::move(discoveryTargets),
        .fields = std::move(fields_),
        .operands = std::move(operands_),
        .execution = std::move(execution),
        .presentation = std::move(presentation),
    };
  }

  RecordReader record_;
  std::string kindPrefix_;
  std::vector<Diagnostic>* diagnostics_ = nullptr;
  ::u8 opcode_ = 0;
  SourceRange opcodeRange_;
  std::vector<SourceField> fields_;
  std::vector<SemanticOperand> operands_;
};

}  // namespace vgmtrans::core
