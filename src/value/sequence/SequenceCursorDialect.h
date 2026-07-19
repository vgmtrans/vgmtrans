/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/SequenceCursor.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <any>
#include <concepts>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::core {

template <class Context>
struct CursorDialectSpec {
  std::string id;
  std::string commandDetailKindPrefix;
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

  [[nodiscard]] u64 tick() const noexcept { return 0; }
  void note(double, double, u32, bool = false) {}
  void tempo(u32) {}
  void tempoAt(u64, u32) {}
  void timeSignature(u8, u8, u8) {}
  void instrument(u32, u32, bool = false) {}
  void level(double, LevelPrecisionHint = LevelPrecisionHint::SevenBit) {}
  void levelAt(u64, double, LevelPrecisionHint = LevelPrecisionHint::SevenBit) {}
  void expression(double, LevelPrecisionHint = LevelPrecisionHint::SevenBit) {}
  void expressionAt(u64, double, LevelPrecisionHint = LevelPrecisionHint::SevenBit) {}
  void pan(double) {}
  void panAt(u64, double) {}
  void pan(double, double) {}
  void stereoBalance(double, double) {}
  void stereoBalanceAt(u64, double, double) {}
  void masterLevel(double) {}
  void reverb(double) {}
  void tuning(double) {}
  void globalTranspose(s32) {}
  void pitchBend(double) {}
  void pitchBendRange(PitchBendRangePerformanceEvent) {}
  void pitchBendRange(u8) {}
  void vibratoDelay(u32, u8) {}
  void tremoloDelay(u32, u8) {}
  void portamento(double, double) {}
  void portamentoEnable(bool) {}
  void portamentoTime(double) {}
  void portamentoControl(double) {}
  void legatoPedal(bool) {}
  void modulation(ModulationPerformanceEvent) {}
  void modulation(ModulationPerformanceTarget, double) {}
  void marker(std::string_view) {}
  void diagnostic(Severity, std::string_view) {}

  [[nodiscard]] RepeatUntilFlow countedRepeatUntil(VmCommandCursor& cmd, u8 slot, u32 totalPlays, Address destination) {
    CommandFlow flow = cmd.countedRepeatUntil(slot, totalPlays, destination);
    return RepeatUntilFlow{flow, !flow.truncated};
  }

  [[nodiscard]] RepeatBreakFlow countedRepeatBreak(VmCommandCursor& cmd, u8 slot, Address destination) {
    return cmd.countedRepeatBreak(slot, destination);
  }
  void finishRepeat(u8) {}

  [[nodiscard]] CommandFlow conditionalFiniteBranch(VmCommandCursor& cmd, Address destination, bool) {
    return cmd.conditionalBranch(destination);
  }
};

namespace detail {

template <class Vm>
[[nodiscard]] RepeatUntilFlow resolveRenderCursorRepeatUntil(VmCommandCursor& cmd, Vm& vm, u8 slot, u32 totalPlays,
                                                             Address destination) {
  CommandFlow flow = cmd.countedRepeatUntil(slot, totalPlays, destination);
  if (flow.truncated) {
    return RepeatUntilFlow{flow, false};
  }

  flow.resolvedEffects = vm.countedRepeatUntil(slot, totalPlays, destination);
  return RepeatUntilFlow{flow, flow.resolvedEffects->step.kind == StepKind::Next};
}

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
  const TrackProgram& track;
  const SourceCommand& command;
  const Context& context;

  [[nodiscard]] u64 tick() const noexcept { return vm.tick(); }
  [[nodiscard]] const SourceCommand* commandAtAddress(Address address) const {
    const auto index = track.addressIndex.find(address);
    if (!index) {
      return nullptr;
    }
    return &track.commands.at(*index);
  }
  [[nodiscard]] const SourceCommand* nextCommand(const VmCommandCursor& cmd) const {
    const Address nextAddress{command.address.value + cmd.position()};
    return commandAtAddress(nextAddress);
  }
  [[nodiscard]] const SourceCommand* commandAfter(const SourceCommand& sourceCommand) const {
    const Address nextAddress{sourceCommand.address.value + sourceCommand.encodedSize};
    return commandAtAddress(nextAddress);
  }
  [[nodiscard]] std::span<const u8> commandBytes(const SourceCommand& sourceCommand) const {
    return track.bytesFor(sourceCommand);
  }
  [[nodiscard]] u64 commandAddress() const noexcept { return command.address.value; }
  [[nodiscard]] std::optional<u8> nextCommandOpcode(const VmCommandCursor& cmd) const {
    const SourceCommand* next = nextCommand(cmd);
    if (next == nullptr) {
      return std::nullopt;
    }
    return next->opcode;
  }
  void note(double key, double linearVelocity, u32 durationTicks, bool extendsPrevious = false) {
    out.note(key, linearVelocity, durationTicks, extendsPrevious);
  }
  void tempo(u32 microsecondsPerQuarter) { out.tempo(microsecondsPerQuarter); }
  void tempoAt(u64 tick, u32 microsecondsPerQuarter) { out.at(tick).tempo(microsecondsPerQuarter); }
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
  void stereoBalance(double leftGain, double rightGain) { out.stereoBalance(leftGain, rightGain); }
  void stereoBalanceAt(u64 tick, double leftGain, double rightGain) {
    out.at(tick).stereoBalance(leftGain, rightGain);
  }
  void masterLevel(double linearGain) { out.masterLevel(linearGain); }
  void reverb(double send) { out.reverb(send); }
  void tuning(double cents) { out.tuning(cents); }
  void globalTranspose(s32 semitones) { out.globalTranspose(semitones); }
  void pitchBend(double semitones) { out.pitchBend(semitones); }
  void pitchBendRange(PitchBendRangePerformanceEvent event) { out.pitchBendRange(std::move(event)); }
  void pitchBendRange(u8 semitones) { out.pitchBendRange(semitones); }
  void vibratoDelay(u32 delayTicks, u8 midiValue) { out.vibratoDelay(delayTicks, midiValue); }
  void tremoloDelay(u32 delayTicks, u8 midiValue) { out.tremoloDelay(delayTicks, midiValue); }
  void portamento(double timeMilliseconds, double previousKey) { out.portamento(timeMilliseconds, previousKey); }
  void portamentoEnable(bool enabled) { out.portamentoEnable(enabled); }
  void portamentoTime(double timeMilliseconds) { out.portamentoTime(timeMilliseconds); }
  void portamentoControl(double previousKey) { out.portamentoControl(previousKey); }
  void legatoPedal(bool enabled) { out.legatoPedal(enabled); }
  void modulation(ModulationPerformanceEvent event) { out.modulation(std::move(event)); }
  void modulation(ModulationPerformanceTarget target, double amount) { out.modulation(target, amount); }
  void marker(std::string_view label) { out.marker(MarkerPerformanceEvent{.text = std::string(label)}); }
  void diagnostic(Severity severity, std::string_view message) {
    vm.diagnostic(Diagnostic{.severity = severity, .message = std::string(message)});
  }

  [[nodiscard]] RepeatUntilFlow countedRepeatUntil(VmCommandCursor& cmd, u8 slot, u32 totalPlays, Address destination) {
    return detail::resolveRenderCursorRepeatUntil(cmd, vm, slot, totalPlays, destination);
  }

  [[nodiscard]] RepeatBreakFlow countedRepeatBreak(VmCommandCursor& cmd, u8 slot, Address destination) {
    return detail::resolveRenderCursorRepeatBreak(cmd, vm, slot, destination);
  }
  void finishRepeat(u8 slot) {
    RepeatCounter counter = vm.repeatCounter(slot);
    if (counter.active()) {
      counter.finish();
    }
  }

  [[nodiscard]] CommandFlow conditionalFiniteBranch(VmCommandCursor& cmd, Address destination, bool taken) {
    CommandFlow flow = cmd.conditionalBranch(destination);
    if (!flow.truncated) {
      flow.resolvedEffects = taken ? Effects{.step = vm.finiteBranch(destination)} : Effects::none();
    }
    return flow;
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
    case FlowKind::ConditionalBranch:
      if (!flow.destination) {
        return DecodeFlow::fallthroughTo(fallthrough);
      }
      return DecodeFlow{
          .kind = DecodeFlow::Kind::Fallthrough,
          .fallthrough = fallthrough,
          .staticTargets = {*flow.destination},
      };
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
        .track = track,
        .command = record,
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
      .commandDetailKindPrefix = std::move(spec.commandDetailKindPrefix),
      .timebase = spec.timebase,
      .defaultBehavior = spec.defaultBehavior,
      .createTrackState = detail::createTrackState<TrackState, Context>,
      .execute = CursorDialectDriver<TrackState, Context, Reader>::execute,
      .context = std::move(spec.context),
  };
  if (dialect.commandDetailKindPrefix.empty()) {
    dialect.commandDetailKindPrefix = dialect.id.value;
  }

  return dialect;
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
        .range = reader.range(begin, 0),
        .flow = DecodeFlow::terminalFlow(),
    };
  }

  const u32 availableSize = boundedEnd - begin;
  const SourceRange availableRange = reader.range(begin, availableSize);
  const auto availableBytes = reader.slice(begin, availableSize);
  VmCommandCursor cursor(CommandPhase::Decode, availableRange, availableBytes, context.sourceMap, context.diagnostics);
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

  const auto commandSize = static_cast<u32>(std::clamp<size_t>(cursor.position(), 1, availableSize));
  const auto commandBytes = availableBytes.subspan(0, commandSize);
  std::vector<u8> ownedBytes{commandBytes.begin(), commandBytes.end()};
  const SourceRange commandRange = reader.range(begin, commandSize);
  const CursorCommandMetadata commandMetadata = cursor.metadata(dialect.commandDetailKindPrefix);
  if (context.sourceMap != nullptr && cursor.annotation().valid()) {
    auto annotation = AnnotationBuilder{*context.sourceMap, cursor.annotation()}
                          .range(commandRange)
                          .label(commandMetadata.name)
                          .detailKind(commandMetadata.detailKind)
                          .playbackStatus(commandMetadata.playbackStatus);
    if (context.parentAnnotation) {
      annotation.parent(*context.parentAnnotation);
    }
  }
  cursor.finalizeDiagnostics(commandRange);

  return DecodedBytecodeCommand{
      .range = commandRange,
      .annotation = cursor.annotation(),
      .bytes = std::move(ownedBytes),
      .flow = decodeFlowFromCommandFlow(commandFlow, Address{begin + commandSize}),
  };
}

template <class TrackState, class Context, class Reader>
[[nodiscard]] DecodedBytecodeCommand decodeCursorCommand(ByteReader reader, u32 begin, const SequenceDialect& dialect,
                                                         BytecodeDecodeContext context = {}) {
  TrackState decodeState = makeDecodeCursorState<TrackState, Context>(context, cursorContext<Context>(dialect));
  return decodeCursorCommandWithState<TrackState, Context, Reader>(reader, begin, dialect, decodeState, context);
}

using CursorTrackDecodeInput = TrackDecodeInput;

[[nodiscard]] inline BytecodeDecodeContext cursorBytecodeDecodeContext(CursorTrackDecodeInput input) {
  return BytecodeDecodeContext{
      .bytecodeEnd = input.bytecodeEnd,
      .sequenceOffset = input.sequenceOffset,
      .sequenceEnd = input.sequenceEnd,
      .parentAnnotation = input.parentAnnotation,
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
  const auto trackAnnotation = createSequenceTrackAnnotation(reader, input);
  if (trackAnnotation) {
    decodeContext.parentAnnotation = trackAnnotation;
  }
  TrackState decodeState = makeDecodeCursorState<TrackState, Context>(decodeContext, cursorContext<Context>(dialect));
  const auto decodeCommand = [&](u32 offset) {
    return decodeCursorCommandWithState<TrackState, Context, Reader>(reader, offset, dialect, decodeState,
                                                                     decodeContext);
  };

  TrackProgram track =
      decodeReachableBytecodeBlocks(reader, cursorBytecodeEnd(reader, input), input.startOffset, input.trackIndex,
                                    ReachableBytecodeDecodePolicy{.maxCommands = input.maxCommands}, decodeCommand);
  finishSequenceTrackAnnotation(reader, input, trackAnnotation, track);
  return track;
}

template <class TrackState, class Context, class Reader>
[[nodiscard]] TrackProgram decodeCursorLinearTrack(ByteReader reader, const SequenceDialect& dialect,
                                                   CursorTrackDecodeInput input) {
  BytecodeDecodeContext decodeContext = cursorBytecodeDecodeContext(input);
  const auto trackAnnotation = createSequenceTrackAnnotation(reader, input);
  if (trackAnnotation) {
    decodeContext.parentAnnotation = trackAnnotation;
  }
  TrackState decodeState = makeDecodeCursorState<TrackState, Context>(decodeContext, cursorContext<Context>(dialect));
  const auto decodeCommand = [&](u32 offset) {
    return decodeCursorCommandWithState<TrackState, Context, Reader>(reader, offset, dialect, decodeState,
                                                                     decodeContext);
  };

  TrackProgram track =
      decodeLinearBytecodeTrack(reader, input.trackIndex, input.startOffset,
                                LinearBytecodeDecodePolicy{.maxCommands = input.maxCommands}, decodeCommand);
  finishSequenceTrackAnnotation(reader, input, trackAnnotation, track);
  return track;
}

}  // namespace vgmtrans::core
