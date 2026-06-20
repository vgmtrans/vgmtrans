/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/SequenceCursor.h"
#include "value/sequence/SequenceVm.h"
#include "value/sequence/bytecode/BytecodeTable.h"
#include "value/sequence/bytecode/BytecodeWalkers.h"

#include <algorithm>
#include <any>
#include <concepts>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::core {

template <class Context>
struct CursorDialectSpec {
  std::string id;
  std::string commandKindPrefix;
  Timebase timebase;
  SequenceProgramBehavior defaultBehavior;
  Context context;
};

template <class TrackState, class Context>
[[nodiscard]] TrackState makeDecodeCursorState(const BytecodeDecodeContext& decodeContext, const Context& context) {
  if constexpr (std::constructible_from<TrackState, const BytecodeDecodeContext&, const Context&>) {
    return TrackState{decodeContext, context};
  } else if constexpr (std::constructible_from<TrackState, const BytecodeDecodeContext&>) {
    return TrackState{decodeContext};
  } else {
    return TrackState{};
  }
}

template <class TrackState, class Context>
struct DecodeCursorRuntime {
  TrackState& state;
  const Context& context;

  void note(double, double, u32, bool = false) {}
  void tempo(u32) {}
  void timeSignature(u8, u8, u8) {}
  void instrument(u32, u32, bool = false) {}
  void level(double, LevelPrecisionHint = LevelPrecisionHint::SevenBit) {}
  void levelAt(u64, double, LevelPrecisionHint = LevelPrecisionHint::SevenBit) {}
  void expression(double, LevelPrecisionHint = LevelPrecisionHint::SevenBit) {}
  void expressionAt(u64, double, LevelPrecisionHint = LevelPrecisionHint::SevenBit) {}
  void pan(double) {}
  void panAt(u64, double) {}
  void pan(double, double) {}
  void masterLevel(double) {}
  void reverb(double) {}
  void tuning(double) {}
  void globalTranspose(s32) {}
  void pitchBend(double) {}
  void pitchBendRange(u8) {}
  void portamento(double, double) {}
  void portamentoEnable(bool) {}
  void portamentoTime(double) {}
  void portamentoControl(double) {}
  void legatoPedal(bool) {}
  void modulation(ModulationPerformanceTarget, double) {}
  void marker(std::string_view) {}
  void diagnostic(Severity, std::string_view) {}

  [[nodiscard]] CommandFlow countedRepeatUntil(VmCommandCursor& cmd, u8 slot, u32 totalPlays, Address destination) {
    return cmd.countedRepeatUntil(slot, totalPlays, destination);
  }

  [[nodiscard]] RepeatBreakFlow countedRepeatBreak(VmCommandCursor& cmd, u8 slot, Address destination) {
    return cmd.countedRepeatBreak(slot, destination);
  }
};

namespace detail {

template <class Vm>
[[nodiscard]] RepeatBreakFlow resolveRenderCursorRepeatBreak(VmCommandCursor& cmd, Vm& vm, u8 slot,
                                                             Address destination) {
  const RepeatBreakFlow annotated = cmd.countedRepeatBreak(slot, destination, false);
  CommandFlow flow = annotated.flow();
  if (flow.truncated) {
    return annotated;
  }

  const BranchResult branch = vm.countedRepeatBreak(slot, destination);
  flow.resolvedEffects = branch.effects;
  return RepeatBreakFlow{flow, branch.taken};
}

}  // namespace detail

template <class TrackState, class Context>
struct RenderCursorRuntime {
  TrackState& state;
  PerformanceEmitter& out;
  VmApi& vm;
  const Context& context;

  void note(double key, double linearVelocity, u32 durationTicks, bool extendsPrevious = false) {
    out.note(key, linearVelocity, durationTicks, extendsPrevious);
  }
  void tempo(u32 microsecondsPerQuarter) { out.tempo(microsecondsPerQuarter); }
  void timeSignature(u8 numerator, u8 denominator, u8 clocksPerMetronomeClick) {
    out.timeSignature(numerator, denominator, clocksPerMetronomeClick);
  }
  void instrument(u32 bank, u32 program, bool forceBankSelect = false) {
    out.instrument(bank, program, forceBankSelect);
  }
  void level(double linearGain, LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit) {
    out.level(linearGain, precisionHint);
  }
  void levelAt(u64 tick, double linearGain, LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit) {
    out.at(tick).level(linearGain, precisionHint);
  }
  void expression(double linearGain, LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit) {
    out.expression(linearGain, precisionHint);
  }
  void expressionAt(u64 tick, double linearGain, LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit) {
    out.at(tick).expression(linearGain, precisionHint);
  }
  void pan(double stereoPosition) { out.pan(stereoPosition); }
  void panAt(u64 tick, double stereoPosition) { out.at(tick).pan(stereoPosition); }
  void pan(double stereoPosition, double linearGain) { out.pan(stereoPosition, linearGain); }
  void masterLevel(double linearGain) { out.masterLevel(linearGain); }
  void reverb(double send) { out.reverb(send); }
  void tuning(double cents) { out.tuning(cents); }
  void globalTranspose(s32 semitones) { out.globalTranspose(semitones); }
  void pitchBend(double semitones) { out.pitchBend(semitones); }
  void pitchBendRange(u8 semitones) { out.pitchBendRange(semitones); }
  void portamento(double timeMilliseconds, double previousKey) { out.portamento(timeMilliseconds, previousKey); }
  void portamentoEnable(bool enabled) { out.portamentoEnable(enabled); }
  void portamentoTime(double timeMilliseconds) { out.portamentoTime(timeMilliseconds); }
  void portamentoControl(double previousKey) { out.portamentoControl(previousKey); }
  void legatoPedal(bool enabled) { out.legatoPedal(enabled); }
  void modulation(ModulationPerformanceTarget target, double amount) { out.modulation(target, amount); }
  void marker(std::string_view label) { out.marker(MarkerPerformanceEvent{.text = std::string(label)}); }
  void diagnostic(Severity severity, std::string_view message) {
    vm.diagnostic(Diagnostic{.severity = severity, .message = std::string(message)});
  }

  [[nodiscard]] CommandFlow countedRepeatUntil(VmCommandCursor& cmd, u8 slot, u32 totalPlays, Address destination) {
    CommandFlow flow = cmd.countedRepeatUntil(slot, totalPlays, destination);
    if (!flow.truncated) {
      flow.resolvedEffects = vm.countedRepeatUntil(slot, totalPlays, destination);
    }
    return flow;
  }

  [[nodiscard]] RepeatBreakFlow countedRepeatBreak(VmCommandCursor& cmd, u8 slot, Address destination) {
    return detail::resolveRenderCursorRepeatBreak(cmd, vm, slot, destination);
  }
};

[[nodiscard]] inline DecodeFlow decodeFlowFromCommandFlow(const CommandFlow& flow, Address fallthrough) {
  if (flow.truncated) {
    return DecodeFlow::terminalFlow();
  }

  switch (flow.kind) {
    case FlowKind::Next:
    case FlowKind::Wait:
      return DecodeFlow::fallthroughTo(fallthrough);
    case FlowKind::Stop:
    case FlowKind::End:
      return DecodeFlow::terminalFlow();
    case FlowKind::Jump:
    case FlowKind::LoopCandidate:
    case FlowKind::DeclaredLoop:
      return flow.destination ? DecodeFlow::jump(*flow.destination) : DecodeFlow::terminalFlow();
    case FlowKind::Call:
      return flow.destination ? DecodeFlow::call(*flow.destination, fallthrough) : DecodeFlow::terminalFlow();
    case FlowKind::Return:
      return DecodeFlow::return_();
    case FlowKind::CountedRepeatUntil:
    case FlowKind::CountedRepeatBreak:
      if (!flow.destination) {
        return DecodeFlow::terminalFlow();
      }
      return DecodeFlow{
          .kind = DecodeFlow::Kind::Fallthrough,
          .fallthrough = fallthrough,
          .staticTargets = {*flow.destination},
      };
  }
  return DecodeFlow::fallthroughTo(fallthrough);
}

template <class Context>
[[nodiscard]] const Context& cursorContext(const SequenceDialect& dialect) {
  return std::any_cast<const Context&>(dialect.context);
}

template <class TrackState, class Context, class Reader>
struct CursorDialectDriver {
  static void describe(const SourceCommand&, const TrackProgram&, CommandInfo&, const std::any&) {}

  static void references(const SourceCommand& record, const TrackProgram& track, CommandReferences& references,
                         const std::any&) {
    for (const auto& reference : track.instrumentReferencesFor(record)) {
      references.instrument(reference.bank, reference.program, reference.range);
    }
  }

  static Effects execute(const SourceCommand& record, const TrackProgram& track, std::any& trackState,
                         PerformanceEmitter& out, VmApi& vm, const std::any& context) {
    auto& typedTrackState = std::any_cast<TrackState&>(trackState);
    const auto& typedContext = std::any_cast<const Context&>(context);
    PerformanceTrack bufferedTrack{
        .id = track.id,
        .sourceTrackNumber = track.sourceTrackNumber,
    };
    PerformanceEmitter bufferedOut{bufferedTrack, record.id, record.annotation, vm.tick()};
    RenderCursorRuntime<TrackState, Context> runtime{
        .state = typedTrackState,
        .out = bufferedOut,
        .vm = vm,
        .context = typedContext,
    };
    VmCommandCursor cursor(CommandPhase::Render, record.range, track.bytesFor(record));
    const CommandFlow flow = Reader::read(runtime, cursor);
    if (cursor.failed() || flow.truncated) {
      return Effects{.step = Step::end()};
    }
    out.appendEvents(std::move(bufferedTrack.events));
    return effectsFromCommandFlow(flow, vm);
  }
};

template <class TrackState, class Context, class Reader>
[[nodiscard]] SequenceDialect makeCursorDialect(CursorDialectSpec<Context> spec) {
  SequenceDialect dialect{
      .id = DialectId{.value = std::move(spec.id)},
      .commandKindPrefix = std::move(spec.commandKindPrefix),
      .timebase = spec.timebase,
      .defaultBehavior = spec.defaultBehavior,
      .createTrackState = detail::createTrackState<TrackState, Context>,
      .context = std::move(spec.context),
  };
  if (dialect.commandKindPrefix.empty()) {
    dialect.commandKindPrefix = dialect.id.value;
  }

  dialect.handlers.push_back(CommandHandler{
      .id = CommandHandlerId{0},
      .typeToken = detail::commandTypeToken<CursorDialectDriver<TrackState, Context, Reader>>(),
      .describe = CursorDialectDriver<TrackState, Context, Reader>::describe,
      .collectReferences = CursorDialectDriver<TrackState, Context, Reader>::references,
      .execute = CursorDialectDriver<TrackState, Context, Reader>::execute,
  });
  return dialect;
}

template <class TrackState, class Context, class Reader>
[[nodiscard]] CommandHandlerId cursorDialectHandlerId(const SequenceDialect& dialect) {
  const auto* handler =
      dialect.handlerForType(detail::commandTypeToken<CursorDialectDriver<TrackState, Context, Reader>>());
  if (handler == nullptr) {
    throw std::logic_error("Cursor dialect is missing its generic command handler");
  }
  return handler->id;
}

template <class TrackState, class Context, class Reader>
[[nodiscard]] DecodedBytecodeCommand decodeCursorCommandWithState(ByteReader reader, u32 begin,
                                                                  const SequenceDialect& dialect,
                                                                  TrackState& decodeState,
                                                                  BytecodeDecodeContext context = {}) {
  if (context.bytecodeEnd == std::numeric_limits<u32>::max()) {
    context.bytecodeEnd = static_cast<u32>(reader.size());
  }
  if (context.sequenceEnd == std::numeric_limits<u32>::max()) {
    context.sequenceEnd = context.bytecodeEnd;
  }

  const u32 boundedEnd = static_cast<u32>(std::min<size_t>(reader.size(), context.bytecodeEnd));
  if (begin >= boundedEnd) {
    return DecodedBytecodeCommand{
        .handler = cursorDialectHandlerId<TrackState, Context, Reader>(dialect),
        .commandKind =
            CommandKind{
                .kindName = dialect.commandKindPrefix + ".truncated",
                .name = "Truncated Command",
                .detailKind = dialect.commandKindPrefix + ".truncated",
                .semantic = SequenceSemantic::Unsupported,
                .playbackStatus = CommandPlaybackStatus::Unsupported,
            },
        .range = reader.range(begin, 0),
        .flow = DecodeFlow::terminalFlow(),
    };
  }

  const u32 availableSize = boundedEnd - begin;
  const SourceRange availableRange = reader.range(begin, availableSize);
  const auto availableBytes = reader.slice(begin, availableSize);
  std::vector<CommandOperand> operands;
  CommandReferences references;
  VmCommandCursor cursor(CommandPhase::Decode, availableRange, availableBytes, context.sourceMap, context.diagnostics,
                         &operands, &references);
  DecodeCursorRuntime<TrackState, Context> runtime{
      .state = decodeState,
      .context = cursorContext<Context>(dialect),
  };
  CommandFlow commandFlow = Reader::read(runtime, cursor);
  if (cursor.failed()) {
    commandFlow = CommandFlow{
        .kind = FlowKind::Stop,
        .truncated = true,
    };
  }

  if (cursor.failed()) {
    cursor.name("Truncated Command", SequenceSemantic::Unsupported, CommandPlaybackStatus::Unsupported)
        .kind("truncated");
  }

  const auto commandSize =
      cursor.failed() ? 1u : static_cast<u32>(std::clamp<size_t>(cursor.position(), 1, availableSize));
  const auto commandBytes = availableBytes.subspan(0, commandSize);
  std::vector<u8> ownedBytes{commandBytes.begin(), commandBytes.end()};
  const SourceRange commandRange = reader.range(begin, commandSize);
  auto instrumentReferences =
      cursor.failed() ? std::vector<CommandInstrumentReference>{} : references.takeInstruments();
  for (auto& reference : instrumentReferences) {
    if (!reference.range && commandRange.valid()) {
      reference.range = commandRange;
    }
  }
  if (context.sourceMap != nullptr && cursor.annotation().valid()) {
    AnnotationBuilder{*context.sourceMap, cursor.annotation()}.range(commandRange);
  }
  cursor.finalizeDiagnostics(commandRange);

  return DecodedBytecodeCommand{
      .handler = cursorDialectHandlerId<TrackState, Context, Reader>(dialect),
      .commandKind = cursor.commandKind(dialect.commandKindPrefix),
      .range = commandRange,
      .annotation = cursor.annotation(),
      .bytes = std::move(ownedBytes),
      .operands = std::move(operands),
      .instrumentReferences = std::move(instrumentReferences),
      .referencesDecoded = true,
      .flow = decodeFlowFromCommandFlow(commandFlow, Address{begin + commandSize}),
  };
}

template <class TrackState, class Context, class Reader>
[[nodiscard]] DecodedBytecodeCommand decodeCursorCommand(ByteReader reader, u32 begin, const SequenceDialect& dialect,
                                                         BytecodeDecodeContext context = {}) {
  TrackState decodeState = makeDecodeCursorState<TrackState, Context>(context, cursorContext<Context>(dialect));
  return decodeCursorCommandWithState<TrackState, Context, Reader>(reader, begin, dialect, decodeState, context);
}

struct CursorTrackDecodeInput {
  u32 trackIndex = 0;
  u32 startOffset = 0;
  u32 bytecodeEnd = std::numeric_limits<u32>::max();
  u32 sequenceOffset = 0;
  u32 sequenceEnd = std::numeric_limits<u32>::max();
  SourceMapBuilder* sourceMap = nullptr;
  std::vector<Diagnostic>* diagnostics = nullptr;
  u32 maxCommands = 4096;
};

[[nodiscard]] inline BytecodeDecodeContext cursorBytecodeDecodeContext(CursorTrackDecodeInput input) {
  return BytecodeDecodeContext{
      .bytecodeEnd = input.bytecodeEnd,
      .sequenceOffset = input.sequenceOffset,
      .sequenceEnd = input.sequenceEnd,
      .sourceMap = input.sourceMap,
      .diagnostics = input.diagnostics,
  };
}

[[nodiscard]] inline u32 cursorBytecodeEnd(ByteReader reader, CursorTrackDecodeInput input) {
  return input.bytecodeEnd == std::numeric_limits<u32>::max() ? static_cast<u32>(reader.size()) : input.bytecodeEnd;
}

template <class TrackState, class Context, class Reader>
[[nodiscard]] TrackProgram decodeCursorReachableTrack(ByteReader reader, const SequenceDialect& dialect,
                                                      CursorTrackDecodeInput input) {
  BytecodeDecodeContext decodeContext = cursorBytecodeDecodeContext(input);
  TrackState decodeState = makeDecodeCursorState<TrackState, Context>(decodeContext, cursorContext<Context>(dialect));
  const auto decodeCommand = [&](u32 offset) {
    return decodeCursorCommandWithState<TrackState, Context, Reader>(reader, offset, dialect, decodeState,
                                                                     decodeContext);
  };

  return decodeReachableBytecodeBlocks(reader, cursorBytecodeEnd(reader, input), input.startOffset, input.trackIndex,
                                       ReachableBytecodeDecodePolicy{.maxCommands = input.maxCommands},
                                       decodeCommand);
}

template <class TrackState, class Context, class Reader>
[[nodiscard]] TrackProgram decodeCursorLinearTrack(ByteReader reader, const SequenceDialect& dialect,
                                                   CursorTrackDecodeInput input) {
  BytecodeDecodeContext decodeContext = cursorBytecodeDecodeContext(input);
  TrackState decodeState = makeDecodeCursorState<TrackState, Context>(decodeContext, cursorContext<Context>(dialect));
  const auto decodeCommand = [&](u32 offset) {
    return decodeCursorCommandWithState<TrackState, Context, Reader>(reader, offset, dialect, decodeState,
                                                                     decodeContext);
  };

  return decodeLinearBytecodeTrack(reader, input.trackIndex, input.startOffset,
                                   LinearBytecodeDecodePolicy{.maxCommands = input.maxCommands}, decodeCommand);
}

}  // namespace vgmtrans::core
