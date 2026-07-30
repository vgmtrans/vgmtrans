/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"
#include "value/sequence/SequenceProgram.h"

#include <any>
#include <concepts>
#include <string>
#include <utility>

namespace vgmtrans::core {

class PerformanceEmitter;
class VmApi;

// Formats play only command values saved during decoding. They do not receive
// the original source bytes or loosely typed format context.
using CreateProgramState = std::any (*)(const SequenceProgram&);
using CreateTrackState = std::any (*)(const SequenceProgram&, const TrackProgram&);
using ExecuteCommand = Effects (*)(const SourceCommand&, std::any& programState, std::any& trackState,
                                   PerformanceEmitter& out, VmApi& vm);
using CommandReadyDuringWait = bool (*)(const SourceCommand&, std::any& programState, std::any& trackState,
                                        PerformanceEmitter& out, VmApi& vm);
using TickTrackState = void (*)(const SourceCommand&, std::any& programState, std::any& trackState,
                                PerformanceEmitter& out, VmApi& vm);
using FinishPrepass = void (*)(std::any& programState);
using BeginTrackSection = void (*)(std::any& trackState);
using FinalizePerformance = void (*)(std::any& programState, PerformanceSequence& performance);

enum class SemanticPrepassMode {
  // Render immediately; the format does not need information from later
  // commands before it can emit the first event.
  None,
  // Run a silent first pass in normal time order. Use this when one track can
  // change a song-wide value that another track reads.
  ScheduledPlayback,
  // Visit every decoded command once in source order. Use this to collect
  // limits from blocks that normal control flow might skip.
  DecodedCommands,
};

struct SequenceDialect {
  DialectId id;
  std::string commandDetailKindPrefix;
  Timebase timebase;
  SequenceProgramBehavior defaultBehavior;
  // PreserveFormat uses this default when a transition has no preference of
  // its own; an export request may still override every transition.
  PitchTransitionRenderingHint preferredPitchTransitionRendering = PitchTransitionRenderingHint::Portamento;
  CreateProgramState createProgramState = nullptr;
  CreateTrackState createTrackState = nullptr;
  ExecuteCommand execute = nullptr;
  CommandReadyDuringWait readyDuringWait = nullptr;
  TickTrackState tick = nullptr;
  FinishPrepass finishPrepass = nullptr;
  BeginTrackSection beginTrackSection = nullptr;
  FinalizePerformance finalizePerformance = nullptr;
  SemanticPrepassMode prepass = SemanticPrepassMode::None;

  // Formats normally want a program with this dialect's identity, timebase,
  // and default VM behavior. Keep that mechanical wiring out of each parser.
  [[nodiscard]] SequenceProgram makeProgram(Address sourceBaseAddress = {}) const {
    return SequenceProgram{
        .dialect = id,
        .timebase = timebase,
        .sourceBaseAddress = sourceBaseAddress,
        .behavior = defaultBehavior,
    };
  }
};

}  // namespace vgmtrans::core
