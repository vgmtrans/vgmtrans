/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "../TestSupport.h"

#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceProgramConfig.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

using namespace vgmtrans::core;

namespace {

// A tiny bytecode language for testing VM flow without a format-specific driver.
struct ProbeTrackState {
  u32 program = 0;
};

struct ProbePlayback : SequencePlayback<ProbeTrackState> {
  void programChange(u8 program) {
    track.program = program;
    out.instrument(0, program);
  }

  Effects note(u8 key, u32 duration) {
    out.note(static_cast<double>(track.program * 12 + key), 0.5, duration);
    return Effects::wait(duration);
  }

  Effects repeatBreak(u8 slot, Address destination) {
    const Effects effects = vm.countedRepeatBreak(slot, destination);
    if (effects.flowOverride) {
      out.instrument(0, 99);
    }
    return effects;
  }
};

using ProbeCompilerCursor = CompilerCursor<ProbePlayback>;

[[nodiscard]] DecodedBytecodeCommand decodeProbeCommand(ByteReader reader, u32 begin) {
  ProbeCompilerCursor cursor(reader, begin, "probe");
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  switch (cursor.opcode()) {
    case 0x80: {
      auto event = cursor.command("Program", SequenceSemantic::Program, {}, "program");
      return event.invoke<&ProbePlayback::programChange>({cursor.u8("program")});
    }
    case 0x90: {
      auto event = cursor.command("Note", SequenceSemantic::Note, {}, "note");
      const u8 key = cursor.u8("key");
      const u8 duration = cursor.u8("duration");
      return event.invoke<&ProbePlayback::note>({key, duration});
    }
    case 0xfe: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump, CommandPlaybackStatus::AffectsControlFlow, "jump");
      return event.jump(cursor.addressLe("destination", SemanticOperandRole::JumpTarget));
    }
    case 0xfb: {
      auto event = cursor.command("Declared Loop", SequenceSemantic::Loop, CommandPlaybackStatus::AffectsControlFlow,
                                  "declared-loop");
      return event.declaredLoop(cursor.addressLe("destination", SemanticOperandRole::LoopTarget));
    }
    case 0xfc: {
      auto event = cursor.command("Loop Candidate", SequenceSemantic::Loop, CommandPlaybackStatus::AffectsControlFlow,
                                  "loop-candidate");
      return event.loopCandidate(cursor.addressLe("destination", SemanticOperandRole::LoopTarget));
    }
    case 0xc0: {
      auto event = cursor.command("Call", SequenceSemantic::Call, CommandPlaybackStatus::AffectsControlFlow, "call");
      return event.call(cursor.addressLe("destination", SemanticOperandRole::CallTarget));
    }
    case 0xfd:
      return cursor.command("Return", SequenceSemantic::Return, CommandPlaybackStatus::AffectsControlFlow, "return")
          .return_();
    case 0xf0: {
      auto event =
          cursor.command("Repeat", SequenceSemantic::Loop, CommandPlaybackStatus::AffectsControlFlow, "repeat");
      const u8 slot = cursor.u8("slot");
      const u8 count = cursor.u8("count");
      const Address destination = cursor.addressLe("destination", SemanticOperandRole::RepeatTarget);
      return event.repeatUntil(slot, count, destination);
    }
    case 0xf1: {
      auto event = cursor.command("Repeat Break", SequenceSemantic::Loop, CommandPlaybackStatus::AffectsControlFlow,
                                  "repeat-break");
      const u8 slot = cursor.u8("slot");
      const Address destination = cursor.addressLe("destination", SemanticOperandRole::RepeatTarget);
      return event.invoke<&ProbePlayback::repeatBreak>({slot, destination}).discoverTarget(destination);
    }
    case 0xff:
      return cursor.command("End", SequenceSemantic::End, CommandPlaybackStatus::AffectsControlFlow, "end").end();
    default:
      return cursor.unsupported("Unsupported Opcode").stop();
  }
}

[[nodiscard]] SequenceProgramConfig probeSequenceConfig(
    SequenceProgramBehavior behavior = {}, std::optional<StereoBalance> initialStereoBalance = std::nullopt) {
  behavior.initialStereoBalance = initialStereoBalance;
  return SequenceProgramConfig{
      .commandKindPrefix = "probe",
      .timebase = Timebase{.ppqn = 48},
      .behavior = behavior,
  };
}

[[nodiscard]] SequenceRuntime probeSequenceRuntime() {
  return makeCompiledRuntime<ProbePlayback>();
}

[[nodiscard]] SequenceProgram probeSequenceProgram() {
  SequenceProgram program = probeSequenceConfig().makeProgram();
  program.runtime = probeSequenceRuntime();
  return program;
}

[[nodiscard]] SourceRange probeRange(u64 offset, u64 size) {
  return SourceRange{
      .source = SourceId{0},
      .offset = offset,
      .size = size,
  };
}

CommandId appendTestCommand(TrackProgram& track, Address address, u8 opcode, SourceRange range,
                            std::optional<u32> sourceChannel, CommandFlow flow, SourceAnnotationId annotation = {},
                            CommandExecution execution = {}, SequenceSemantic semantic = SequenceSemantic::Unknown) {
  expect(track.commands.empty() || track.commands.back().address.value < address.value,
         "test commands must be appended in increasing source-address order");
  const CommandId id{static_cast<u32>(track.commands.size())};
  track.commands.push_back(SourceCommand{
      .opcode = opcode,
      .address = address,
      .range = range,
      .annotation = annotation,
      .semantic = semantic,
      .sourceChannel = sourceChannel,
      .flow = std::move(flow),
      .execution = std::move(execution),
  });
  return id;
}

template <size_t Size>
CommandId addProbeCommand(TrackProgram& track, Address address, SourceRange range, const std::array<u8, Size>& bytes) {
  const ByteReader reader(range.source, std::span<const u8>{bytes});
  auto decoded = decodeProbeCommand(reader, 0);
  // This helper decodes an isolated command buffer and then places the command
  // at its fixture address. Rebase only its physical continuation; encoded
  // flow destinations already use the fixture's track address space.
  decoded.flow.continuation.value += address.value;
  return appendTestCommand(track, address, decoded.opcode, range, {}, std::move(decoded.flow), {},
                           std::move(decoded.execution));
}

[[nodiscard]] size_t countProbeNotesAt(const PerformanceTrack& track, u64 tick) {
  return static_cast<size_t>(std::ranges::count_if(track.events, [tick](const PerformanceEvent& event) {
    const auto* note = std::get_if<NotePerformanceEvent>(&event);
    return note != nullptr && note->header.tick == tick;
  }));
}

[[nodiscard]] const MarkerPerformanceEvent* probeMarkerAt(const PerformanceTrack& track, std::string_view text,
                                                          u64 tick) {
  for (const auto& event : track.events) {
    const auto* marker = std::get_if<MarkerPerformanceEvent>(&event);
    if (marker != nullptr && marker->text == text && marker->header.tick == tick) {
      return marker;
    }
  }
  return nullptr;
}

}  // namespace
