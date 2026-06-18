/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/SequenceCursor.h"
#include "value/sequence/SequenceVm.h"
#include "value/sequence/bytecode/BytecodeTable.h"

#include <algorithm>
#include <any>
#include <concepts>
#include <stdexcept>
#include <vector>

namespace vgmtrans::core {

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
  TrackState state{};
  const Context& context;
  CommandReferences* references = nullptr;

  void note(double, double, u32, bool = false) {}
  void tempo(u32) {}
  void instrument(u32 bank, u32 program, bool = false) {
    if (references != nullptr) {
      references->instrument(bank, program);
    }
  }
  void level(double, LevelPrecisionHint = LevelPrecisionHint::SevenBit) {}
  void expression(double, LevelPrecisionHint = LevelPrecisionHint::SevenBit) {}
  void pan(double) {}
  void pan(double, double) {}
  void masterLevel(double) {}
  void reverb(double) {}
  void tuning(double) {}
  void globalTranspose(s32) {}
  void pitchBend(double) {}
  void pitchBendRange(u8) {}
  void portamentoEnable(bool) {}
  void portamentoTime(double) {}
  void modulation(ModulationPerformanceTarget, double) {}
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
[[nodiscard]] const Context& bytecodeContext(const BytecodeDecodeContext& decodeContext) {
  if (decodeContext.dialectContext == nullptr) {
    throw std::logic_error("Cursor bytecode decode requires a dialect context");
  }
  return std::any_cast<const Context&>(*decodeContext.dialectContext);
}

// CursorBytecodeCommand is the bridge from the readable cursor authoring style
// to the existing SequenceProgram/SequenceVm model. Reader::read() is called in
// decode mode to record source facts, then in render mode to emit playback events.
template <class TrackState, class Context, class Reader>
struct CursorBytecodeCommand {
  static DecodedBytecodeCommand decode(const BytecodeCommandSpec& spec, const BytecodeCommandSpec& truncatedSpec,
                                       ByteReader reader, u32 begin, BytecodeDecodeContext context) {
    const u32 boundedEnd = static_cast<u32>(std::min<size_t>(reader.size(), context.bytecodeEnd));
    if (begin >= boundedEnd) {
      return DecodedBytecodeCommand{
          .handler = spec.handler,
          .kind = spec.kind,
          .range = reader.range(begin, 0),
          .flow = DecodeFlow::terminalFlow(),
      };
    }

    const u32 availableSize = boundedEnd - begin;
    const SourceRange availableRange = reader.range(begin, availableSize);
    const auto availableBytes = reader.slice(begin, availableSize);
    std::vector<CommandOperand> operands;
    VmCommandCursor cursor(CommandPhase::Decode, availableRange, availableBytes, context.sourceMap,
                           context.diagnostics, &operands);
    DecodeCursorRuntime<TrackState, Context> runtime{
        .state = makeDecodeCursorState<TrackState, Context>(context, bytecodeContext<Context>(context)),
        .context = bytecodeContext<Context>(context),
    };
    const CommandFlow commandFlow = Reader::read(cursor, runtime);

    const bool failed = cursor.failed();
    const auto commandSize = failed ? 1u : static_cast<u32>(std::clamp<size_t>(cursor.position(), 1, availableSize));
    const auto commandBytes = availableBytes.subspan(0, commandSize);
    std::vector<u8> ownedBytes{commandBytes.begin(), commandBytes.end()};
    const SourceRange commandRange = reader.range(begin, commandSize);
    if (context.sourceMap != nullptr && cursor.annotation().valid()) {
      AnnotationBuilder{*context.sourceMap, cursor.annotation()}.range(commandRange);
    }
    return DecodedBytecodeCommand{
        .handler = failed ? truncatedSpec.handler : spec.handler,
        .kind = failed ? truncatedSpec.kind : spec.kind,
        .range = commandRange,
        .bytes = std::move(ownedBytes),
        .operands = std::move(operands),
        .flow = decodeFlowFromCommandFlow(commandFlow, Address{begin + commandSize}),
    };
  }

  static void references(const SourceCommand& record, const TrackProgram& track, CommandReferences& references,
                         const std::any& context) {
    const auto& typedContext = std::any_cast<const Context&>(context);
    DecodeCursorRuntime<TrackState, Context> runtime{
        .context = typedContext,
        .references = &references,
    };
    VmCommandCursor cursor(CommandPhase::Decode, record.range, track.bytesFor(record));
    static_cast<void>(Reader::read(cursor, runtime));
  }

  static Effects execute(const SourceCommand& record, const TrackProgram& track, std::any& trackState,
                         PerformanceEmitter& out, VmApi& vm, const std::any& context) {
    auto& typedTrackState = std::any_cast<TrackState&>(trackState);
    const auto& typedContext = std::any_cast<const Context&>(context);
    CommandRuntime<TrackState, Context> runtime{
        .state = typedTrackState,
        .out = out,
        .vm = vm,
        .context = typedContext,
    };
    VmCommandCursor cursor(CommandPhase::Render, record.range, track.bytesFor(record));
    return effectsFromCommandFlow(Reader::read(cursor, runtime));
  }
};

namespace detail {

template <class Command>
[[nodiscard]] DecodedBytecodeCommand decodeCursorBytecodeCommand(const BytecodeCommandSpec& spec,
                                                                 const BytecodeCommandSpec& truncatedSpec, ByteReader reader,
                                                                 u32 begin, BytecodeDecodeContext context) {
  return Command::decode(spec, truncatedSpec, reader, begin, context);
}

}  // namespace detail

}  // namespace vgmtrans::core
